#include "tcp_sender.hh"
#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_sender_message.hh"
#include "wrapping_integers.hh"
#include <cstdint>
#include <utility>
#include <vector>

using namespace std;
// How many sequence numbers are outstanding?
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return NextByte2Sent - LastByteAcked;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return dup_count;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  TCPSenderMessage sendMsg;
  if ( ( SYN || reader().bytes_buffered() ) && NextByte2Sent - LastByteAcked < rwnd ) {
    sendMsg.FIN = false;
    sendMsg.SYN = SYN;
    // 在这里要物尽其用的尽可能把数据加入放到一个segment里面，保证window有空和buffer有内容即可,并且大小不能超过设定payload最大值
    while ( NextByte2Sent - LastByteAcked < rwnd && sendMsg.payload.size() < TCPConfig::MAX_PAYLOAD_SIZE
            && SYN + reader().bytes_buffered() > 0 ) {
      // 通过reader读取应用层数据，鉴别是否能完整加入
      auto peeked = reader().peek();
      if ( peeked.size() + NextByte2Sent - LastByteAcked + writer().is_closed() > rwnd
           || peeked.size() + sendMsg.payload.size() > TCPConfig::MAX_PAYLOAD_SIZE ) {
        auto size = min( (uint64_t)( rwnd + LastByteAcked - NextByte2Sent ),
                         (uint64_t)( TCPConfig::MAX_PAYLOAD_SIZE - sendMsg.payload.size() ) );
        // 不能完整加入，需要截断,则不可能是产生FIN的时候,这个地方逻辑后面可以优化
        peeked = peeked.substr( 0, size );
        // 因为前面限制得到这里一定能完整传，并且这个时候如果需要传FIN，就把FIN也传了
      } else if ( writer().is_closed() ) {
        need2Fin = false;
        sendMsg.FIN = true;
      }
      // 填充发送msg的payload和seqno和RST
      sendMsg.seqno = Wrap32::wrap( NextByte2Sent, isn_ );
      sendMsg.payload += peeked;
      sendMsg.RST = writer().has_error();
      // 把这段payload同时放入我们的记录vector里面，因为发送了但是没有接收到ack
      transButUnack.emplace_back( pair { NextByte2Sent, sendMsg } );
      // 更新Nextbyte2sent
      NextByte2Sent += sendMsg.sequence_length();
      // 从应用层的bytestream中去掉peeked
      input_.reader().pop( peeked.size() );
      if ( sendMsg.payload.size() == TCPConfig::MAX_PAYLOAD_SIZE ) {
        transmit( sendMsg );
        sendMsg.payload.clear();
        sendMsg.FIN = false;
        sendMsg.SYN = false;
      }
      SYN = false;
    }
    if ( sendMsg.sequence_length() != 0 ) {
      transmit( sendMsg );
      SYN = false;
    }
  } else if ( writer().is_closed() && need2Fin && rwnd != 0 ) { // 需要发送FIN
    sendMsg.FIN = true;
    sendMsg.seqno = Wrap32::wrap( NextByte2Sent, isn_ );
    if ( NextByte2Sent - LastByteAcked < rwnd ) {
      need2Fin = false;
    } else {
      return;
    }
    // 把这段payload同时放入我们的记录vector里面，因为发送了但是没有接收到ack
    transmit( sendMsg );
    transButUnack.emplace_back( pair { NextByte2Sent, sendMsg } );
    NextByte2Sent++;
  } else if ( rwnd == 0 && !has_trans_win0_ ) {
    // 特殊情况rwnd为0，那么直接transmit一个byte
    sendMsg.seqno = Wrap32::wrap( NextByte2Sent, isn_ );
    if ( !reader().is_finished() ) {
      sendMsg.payload = reader().peek().substr( 0, 1 );
      input_.reader().pop( 1 );
    } else {
      sendMsg.FIN = true;
      need2Fin = false;
    }
    transButUnack.emplace_back( pair { NextByte2Sent, sendMsg } );
    NextByte2Sent++;
    transmit( sendMsg );
    has_trans_win0_ = true;
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage { .seqno = Wrap32::wrap( NextByte2Sent, isn_ ), .RST = writer().has_error() };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) { // 遇到异常情况
    writer().set_error();
    writer().close();
    return;
  }
  rwnd = msg.window_size;
  // 对于没有ackno的，就是更新window信息：
  if ( not msg.ackno.has_value() ) {
    // 清空上一次的时间积累
    accumulated_time = 0;
    // 复原RTO_ms_
    RTO_ms_ = initial_RTO_ms_;
    return;
  }
  uint64_t ackno = msg.ackno->unwrap( isn_, LastByteAcked );
  // 查看是否是冗余ack，以及查看是否是超过了当前sent的packet的bytes，这种直接忽略
  if ( ackno <= LastByteAcked || ackno > NextByte2Sent
       || transButUnack.begin()->first + transButUnack.begin()->second.sequence_length()
            > ackno ) { // 如果是之前已经应答了的，那么省略掉
    return;
  }
  // 到了这里，说明是没有冗余ack，利用累计确认原则，进行清除,我这里遍历去查找，后续可能会改成set采用log算法查找（但不见得更优）
  vector<pair<uint64_t, TCPSenderMessage>>::iterator iter = transButUnack.begin();
  while ( iter != transButUnack.end() && iter->first + iter->second.sequence_length() <= ackno )
    iter++; // 出来的是刚好大于ack的，也就是没有被确认的

  transButUnack.erase( transButUnack.begin(), iter ); // 删除确认段之前的
  dup_count = 0;                                      // 清空重传次数积累
  // 更新LastByteAcked
  LastByteAcked = ackno;
  // 清空上一次的时间积累
  accumulated_time = 0;
  // 复原RTO_ms_
  RTO_ms_ = initial_RTO_ms_;
  // 复原has_trans_win0
  if ( has_trans_win0_ )
    has_trans_win0_ = false;
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( not transButUnack.empty() ) {
    accumulated_time += ms_since_last_tick; // 更新目前累计时间
    if ( accumulated_time < RTO_ms_ ) {     // 没有超时
      return;
    }
    // 发生超时，选择重传最老的outstanding分组
    accumulated_time = 0;                        // 清空时间积累
    dup_count++;                                 // 重传次数增加
    RTO_ms_ = rwnd == 0 ? RTO_ms_ : 2 * RTO_ms_; // 倍增
    // 构造重传的message，也就是传transButUnack里面第一个元素即可，它有index用来填充
    transmit( transButUnack.begin()->second );
  }
}
