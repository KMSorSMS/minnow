#include <iostream>
#include <tuple>
#include <utility>
#include <vector>

#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "exception.hh"
#include "ipv4_datagram.hh"
#include "network_interface.hh"
#include "parser.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // judge if the mac of next_hop already known.
  if ( arp_table_.contains( next_hop.ipv4_numeric() ) ) {
    // send datagram immediately
    EthernetHeader header = { .dst = arp_table_[next_hop.ipv4_numeric()].second,
                              .src = ethernet_address_,
                              .type = EthernetHeader::TYPE_IPv4 };
    transmit( EthernetFrame { .header = std::move( header ), .payload = serialize( dgram ) } );
  } else { // send arp msg to get info, broadcast address
    // but first need to check if the 5s has passed since last time send arp request for this ip,then,don't send it
    // again,but cache the dgram
    // cache the untransmit InternetDatagram
    waiting_arp_datagram_.emplace( next_hop.ipv4_numeric(), std::move( dgram ) );

    if ( arp_request_table_.contains( next_hop.ipv4_numeric() )
         && arp_request_table_[next_hop.ipv4_numeric()] <= accmulate_time_ ) {
      return;
    }
    EthernetHeader header
      = { .dst = ETHERNET_BROADCAST, .src = ethernet_address_, .type = EthernetHeader::TYPE_ARP };
    ARPMessage arpmsg { .opcode = ARPMessage::OPCODE_REQUEST,
                        .sender_ethernet_address = ethernet_address_,
                        .sender_ip_address = ip_address_.ipv4_numeric(),
                        .target_ip_address = next_hop.ipv4_numeric() };
    transmit( EthernetFrame { .header = std::move( header ), .payload = serialize( arpmsg ) } );
    // add this arp to the map,ttl 是当前时间加上5s
    arp_request_table_.emplace( next_hop.ipv4_numeric(), accmulate_time_ + 5000 );
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{

  // if the inbound frame if IPv4, parse the payload as an InternetDatagram and ,if successful. push it to
  // datagrams_received_queue. but make sure the address is correct
  if ( frame.header.type == EthernetHeader::TYPE_IPv4 && frame.header.dst == ethernet_address_ ) {
    InternetDatagram interDgram {};
    if ( !parse( interDgram, frame.payload ) ) {
      return;
    }
    // 解析成功的话，直接将interDgram放入queue中
    datagrams_received_.emplace( std::move( interDgram ) );
    // if this is ARP,then it's a arp reply or request and also the dst is correct
  } else if ( frame.header.type == EthernetHeader::TYPE_ARP
              && ( frame.header.dst == ethernet_address_ || frame.header.dst == ETHERNET_BROADCAST ) ) {
    // parse it to arpmessage
    ARPMessage arpmsg {};
    if ( !parse( arpmsg, frame.payload ) ) {
      return;
    }
    // 如果是回应信息,更新我们的arp表，并且ttl设置为当前的accmulate_time+30000（30s）
    // 产生对应header
    EthernetHeader header
      = { .dst = arpmsg.sender_ethernet_address, .src = ethernet_address_, .type = EthernetHeader::TYPE_IPv4 };
    if ( arpmsg.opcode == ARPMessage::OPCODE_REPLY ) {
      arp_table_.emplace( std::piecewise_construct,
                          std::forward_as_tuple( arpmsg.sender_ip_address ),
                          std::forward_as_tuple( accmulate_time_ + 30000, arpmsg.sender_ethernet_address ) );
      // 从waiting_arp_datagram里面找到所有的ip，将他们全部发送出去：
      auto range = waiting_arp_datagram_.equal_range( arpmsg.sender_ip_address );
      for ( auto dgram = range.first; dgram != range.second; ++dgram ) {
        // 发送对应的datagram
        transmit( EthernetFrame { .header = header, .payload = serialize( dgram->second ) } );
      }
      // 如果是请求信息，要检查目标ip是否是自己
    } else if ( arpmsg.opcode == ARPMessage::OPCODE_REQUEST
                && arpmsg.target_ip_address == ip_address_.ipv4_numeric() ) {
      // 返回arp的信息，保证发送方能够更新arp table
      ARPMessage arpmsg_reply { .opcode = ARPMessage::OPCODE_REPLY,
                                .sender_ethernet_address = ethernet_address_,
                                .sender_ip_address = ip_address_.ipv4_numeric(),
                                .target_ethernet_address = arpmsg.sender_ethernet_address,
                                .target_ip_address = arpmsg.sender_ip_address };
      header.type = EthernetHeader::TYPE_ARP;
      // 发送arp msg
      transmit( EthernetFrame { .header = header, .payload = serialize( arpmsg_reply ) } );
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  accmulate_time_ += ms_since_last_tick;
}
