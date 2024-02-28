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
  // 还没发起握手，则首先由发送方发起握手请求
  if ( shake_times == 1 ) {
    // 注意SYN=true和发送的seqno为isn是关键
    sendMsg
      = { .seqno = isn_, .SYN = true, .payload = {}, .FIN = writer().is_closed(), .RST = writer().has_error() };
    // 也要把它放入我们的transbutunack喔，因为可能会lost而重传的捏
    transButUnack.emplace_back( pair { 0, sendMsg } );
    // 发送
    transmit( sendMsg );
    // 正式起航，所以我们的NextByte2Sent就有意义了，就是为1；不过还需要等一个对方的ack
    NextByte2Sent = 1;
    shake_times++;
  } else if ( (shake_times >= 3 && reader().bytes_buffered())) { // 完成了三次握手后才会发送，不过第三次发送可以捎带,测试里面有对于这种捎带情况考察
    sendMsg.SYN = false;
    // 在这里要物尽其用的尽可能把数据加入放到一个segment里面，保证window有空和buffer有内容即可
    while ( NextByte2Sent - LastByteAcked < rwnd && reader().bytes_buffered() ) {
      // 通过reader读取应用层数据，鉴别是否能完整加入
      auto peeked = reader().peek();
      sendMsg.FIN = true;
      if ( peeked.size() + NextByte2Sent - LastByteAcked + reader().is_finished() > rwnd ) {
        // 不能完整加入，需要截断,则不可能是产生FIN的时候,这个地方逻辑后面可以优化
        peeked = peeked.substr( 0, rwnd - NextByte2Sent + LastByteAcked - reader().is_finished() );
        sendMsg.FIN = false;
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
    }
    transmit( sendMsg );
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage { .seqno = Wrap32::wrap( NextByte2Sent, isn_ ) };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) { // 遇到异常情况
    writer().set_error();
    writer().close();
    //清空握手次数，从1开始
    shake_times=1;
    return;
  }
  rwnd = msg.window_size;
  uint64_t ackno = msg.ackno->unwrap( isn_, LastByteAcked );
  // 如果是等待第二次握手返回信息,并且ackno表明收到syn
  if ( shake_times == 2 && ackno > LastByteAcked ) {
    shake_times++;
  }
  // 查看是否是冗余ack：
  if ( ackno <= LastByteAcked ) { // 如果是之前已经应答了的，那么省略掉
    return;
  }
  // 到了这里，说明是没有冗余ack，利用累计确认原则，进行清除,我这里遍历去查找，后续可能会改成set采用log算法查找（但不见得更优）
  vector<pair<uint64_t, TCPSenderMessage>>::iterator iter = transButUnack.begin();
  while ( iter != transButUnack.end() && iter->first < ackno )
    iter++; // 出来的是刚好大于ack的，也就是没有被确认的
  transButUnack.erase( transButUnack.begin(), iter ); // 删除确认段之前的
  dup_count = 0;                                      // 清空重传次数积累
  // 更新LastByteAcked
  LastByteAcked = ackno;
  // 清空上一次的时间积累
  accumulated_time = 0;
  //复原RTO_ms_
  RTO_ms_ = initial_RTO_ms_;
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( not transButUnack.empty() ) {
    accumulated_time += ms_since_last_tick;     // 更新目前累计时间
    if ( accumulated_time < RTO_ms_ ) { // 没有超时
      return;
    }
    // 发生超时，选择重传最老的outstanding分组
    accumulated_time = 0; // 清空时间积累
    dup_count++;//重传次数增加
    RTO_ms_ = 2*RTO_ms_;//倍增
    // 构造重传的message，也就是传transButUnack里面第一个元素即可，它有index用来填充
    transmit( transButUnack.begin()->second );
  }
}
