#include "router.hh"
#include "address.hh"
#include "ipv4_datagram.hh"
#include "network_interface.hh"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  router_table.emplace_back( route_prefix, prefix_length, next_hop, interface_num );
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  // 先遍历所有的interface，一个个发送
  for ( auto interface : _interfaces ) {
    while ( !interface->datagrams_received().empty() ) { // 有datagram
      // 我们需要对于每个datagram进行最大匹配，找到发送的interface，然后发送
      choose_interface_transmit( interface->datagrams_received().front() );
      // 发送一个就pop掉
      interface->datagrams_received().pop();
    }
  }
}
// 这里实现最大匹配的算法来找到合适的interface并发送
void Router::choose_interface_transmit( InternetDatagram& dgram )
{
  // 判断ttl，如果小于等于1，直接return，不处理
  if ( dgram.header.ttl <= 1 ) {
    return;
  }
  // 目的ip地址，32位
  auto ip = dgram.header.dst;
  uint8_t max_prefix_len { 0 };
  std::optional<size_t> trans_interface_num;
  std::optional<Address> next_hop;
  // 遍历表找最大匹配项
  for ( auto router_tuple_loop : router_table ) {
    // 搞一个掩码，根据prefix_len来
    // attention: it can produce undefined behavior to shift a 32-bit integer by 32 bits,所以要特别排除
    uint32_t mask = router_tuple_loop.prefix_length ? 0xFFFFFFFFu << ( 32 - router_tuple_loop.prefix_length ) : 0;
    if ( ( ip & mask ) == router_tuple_loop.route_prefix && router_tuple_loop.prefix_length >= max_prefix_len ) {
      // 更新目标网络接口
      max_prefix_len = router_tuple_loop.prefix_length;
      trans_interface_num = router_tuple_loop.interface_num;
      next_hop = router_tuple_loop.next_hop;
    }
  }
  // 如果有match，更改ttl并发送,没有match就丢掉就好
  if ( trans_interface_num.has_value() ) {
    dgram.header.ttl--;
    // 注意checksum要更新
    dgram.header.compute_checksum();
    interface( trans_interface_num.value() )
      ->send_datagram( dgram,
                       next_hop.has_value() ? next_hop.value() : Address::from_ipv4_numeric( dgram.header.dst ) );
  }
}