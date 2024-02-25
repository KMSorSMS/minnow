#include "tcp_receiver.hh"
#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "wrapping_integers.hh"
#include <cstdint>

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // 从message里面拿到SYN、FIN和seqno，这里需要分情况处理
  if ( message.RST ) {
    reader().set_error();
    return;
  }
  if ( message.SYN && !has_init ) {
    zero_point = message.seqno;
    reassembler_.insert( message.seqno.unwrap( zero_point.value(), next_bytes ), message.payload, message.FIN );
    has_init = true;
  } else if ( has_init && zero_point.has_value() ) { // 这里-1特别关键，因为syn的原因，转换为stream index
    reassembler_.insert( message.seqno.unwrap( zero_point.value(), next_bytes ) - 1, message.payload, message.FIN );
  }
  next_bytes = writer().bytes_pushed() + 1;

  if ( writer().is_closed() ) {
    next_bytes++;
  }
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage recvMsg { zero_point.has_value() ? Wrap32::wrap( next_bytes, zero_point.value() ) : zero_point,
                               static_cast<uint16_t>( writer().available_capacity() > UINT16_MAX
                                                        ? UINT16_MAX
                                                        : static_cast<uint16_t>( writer().available_capacity() ) ),
                               reader().has_error() };
  return recvMsg;
}
