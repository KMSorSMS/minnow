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
// 根据当前情况来产生一个最大的segment能被发送
void TCPSender::FindMaxSeg( TCPSenderMessage& sendMsg )
{
  // 条件就是不超过最大payload限制，不超过rwnd，然后不停从reader里面读取
  // 有个最大的问题就是FIN的捎带，怎么带？它要占一个byte在rwnd里面，最开始就加入它吗？
  auto peeked = reader().peek();
  auto space = rwnd - NextByte2Sent + LastByteAcked;
  // 新加入的能够完整存放，就直接一直存,能在这里处理完数据是最好的，也就是触发is_finished而退出，不然就要切割
  while ( sendMsg.sequence_length() + peeked.size() <= space
          && peeked.size() + sendMsg.payload.size() <= TCPConfig::MAX_PAYLOAD_SIZE && !peeked.empty() ) {
    sendMsg.payload += peeked;
    // 弹出加入的peek部分
    input_.reader().pop( peeked.size() );
    // 更新peeked
    peeked = reader().peek();
  }
  // 退出来，检查是否已经把内容吸收完毕,否则需要切割
  // 理一下思路：如果peek里面有内容，不能直接加入，可以切割，但是这里有细节，
  // 没接收完成，因为目前已经占满或者有空，只是不够大，但是有FIN，那么FIN退出，加入peeked
  if ( not peeked.empty() ) {
    auto size = min( space - sendMsg.sequence_length() + sendMsg.FIN,
                     TCPConfig::MAX_PAYLOAD_SIZE - sendMsg.payload.size() );
    peeked = peeked.substr( 0, size ); // 多加入一位，由于FIN产生
    sendMsg.payload += peeked;
    input_.reader().pop( peeked.size() );
    sendMsg.FIN = false;
  }
  sendMsg.RST = writer().has_error();
  sendMsg.seqno = Wrap32::wrap( NextByte2Sent, isn_ );
  transButUnack.emplace_back( pair { NextByte2Sent, sendMsg } );
  NextByte2Sent += sendMsg.sequence_length();
  FIN = FIN ? !sendMsg.FIN : false;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  TCPSenderMessage sendMsg;

  if ( ( SYN || reader().bytes_buffered() || ( writer().is_closed() && FIN ) )
       && NextByte2Sent - LastByteAcked < rwnd ) {
    sendMsg.SYN = SYN;
    // 在这里要物尽其用的尽可能把数据加入放到一个segment里面，保证window有空和buffer有内容即可,并且大小不能超过设定payload最大值
    do {
      // 寻找能够加入的最大数据量，每次产生一个能够发送的segment
      sendMsg.FIN = writer().is_closed();
      FindMaxSeg( sendMsg );
      // 发送这个sendMsg
      transmit( sendMsg );
      // 传送过了，需要清空sendMsg的内容
      sendMsg.payload.clear();
      SYN = false;
    } while ( reader().bytes_buffered() != 0 && NextByte2Sent - LastByteAcked < rwnd );
  } else if ( rwnd == 0 && !has_trans_win0_ ) {
    // 特殊情况rwnd为0，那么直接transmit一个byte
    sendMsg.seqno = Wrap32::wrap( NextByte2Sent, isn_ );
    if ( !reader().is_finished() ) {
      sendMsg.payload = reader().peek().substr( 0, 1 );
      input_.reader().pop( 1 );
    } else {
      sendMsg.FIN = true;
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
