#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms ),RTO_ms_(initial_RTO_ms_)
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
  Writer& writer() { return input_.writer(); }
  const Writer& writer() const { return input_.writer(); }

  // Access input stream reader, but const-only (can't read from outside)
  const Reader& reader() const { return input_.reader(); }

private:
  // Variables initialized in constructor
  ByteStream input_;
  Wrap32 isn_;
  uint16_t rwnd { 1 };
  uint64_t dup_count { 0 }; // 计数冗余，老实说我觉得64位有点多余
  uint64_t initial_RTO_ms_;
  uint64_t RTO_ms_;
  uint64_t accumulated_time { 0 }; // 累计时间，用于计算超时
  uint64_t NextByte2Sent {0};    // absolute sequence number denote the next Bytes to be sent
  uint64_t LastByteAcked {0};    //  absolute sequence number denote the latest last bytes that have acked
  // 开一个pair的vector，把在传输层切片但是没有得到ack的数据保存起来
  std::vector<std::pair<uint64_t, TCPSenderMessage>> transButUnack {};
  bool has_trans_win0_{false};
  bool SYN{true};
  bool FIN{true};
  void FindMaxSeg(TCPSenderMessage& sendMsg);
  // void transmitFunc(const TCPSenderMessage& msg);//用在push方法传递的参数
};
