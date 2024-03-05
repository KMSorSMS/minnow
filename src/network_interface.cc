#include <iostream>
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
    if ( arp_request_table_.contains( next_hop.ipv4_numeric() )
         && arp_request_table_[next_hop.ipv4_numeric()].first + 5000 <= accmulate_time_ ) {
      return;
    }
    EthernetHeader header
      = { .dst = ETHERNET_BROADCAST, .src = ethernet_address_, .type = EthernetHeader::TYPE_ARP };
    ARPMessage arpmsg { .opcode = ARPMessage::OPCODE_REQUEST,
                        .sender_ethernet_address = ethernet_address_,
                        .sender_ip_address = ip_address_.ipv4_numeric(),
                        .target_ip_address = next_hop.ipv4_numeric() };
    transmit( EthernetFrame { .header = std::move( header ), .payload = serialize( arpmsg ) } );
    // cache the untransmit InternetDatagram
    waiting_arp_datagram_.emplace( next_hop.ipv4_numeric(), std::move( dgram ) );
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // if the inbound frame if IPv4, parse the payload as an InternetDatagram and ,if successful. push it to
  // datagrams_received_queue.
  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    std::vector<std::string> buffers;

  } else {
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  accmulate_time_ += ms_since_last_tick;
}
