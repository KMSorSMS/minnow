#include "tcp_receiver.hh"
#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "wrapping_integers.hh"
#include <cstdint>
#include <utility>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // 从message里面拿到SYN、FIN和seqno，这里需要分情况处理
  if ( message.RST ) {
    reader().set_error();
    return;
  }
  //这里多考虑的是可能最开始的segment来的没有SYN，那就不能接收，必须先来SYN字段的，给了zero_point信息过后才能接收
  if ( message.SYN && !zero_point.has_value() ) {
    reassembler_.insert( message.seqno.unwrap( message.seqno, next_bytes ), message.payload, message.FIN );
    zero_point = move( message.seqno );
  } else if ( zero_point.has_value() ) { // 这里-1特别关键，因为syn的原因，转换为stream index
    reassembler_.insert( message.seqno.unwrap( zero_point.value(), next_bytes ) - 1, message.payload, message.FIN );
  }
  // 这里是得到最新的next_bytes也就是first_unassembled
  // index，它的值来源于当前pushed的数量再加上最开始的SYN，以及可能的结束的fin
  //  突然想到一点，有可能最开始没有SYN，这个时候就不应该有一个ackno，也就是没有接收到syn的时候是没有ack的，这个在send部分解决了，通过校验zero_point的有无
  next_bytes = writer().is_closed() ? writer().bytes_pushed() + 2 : writer().bytes_pushed() + 1;
}

TCPReceiverMessage TCPReceiver::send() const
{
  return TCPReceiverMessage { zero_point.has_value() ? Wrap32::wrap( next_bytes, zero_point.value() ) : zero_point,
                              static_cast<uint16_t>( writer().available_capacity() > UINT16_MAX
                                                       ? UINT16_MAX
                                                       : static_cast<uint16_t>( writer().available_capacity() ) ),
                              reader().has_error() };
}
