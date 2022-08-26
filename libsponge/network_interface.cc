#include "network_interface.hh"

#include "arp_message.hh"
#include "buffer.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "ipv4_datagram.hh"
#include "parser.hh"

#include <cstdint>
#include <iostream>
#include <utility>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

inline bool NetworkInterface::is_ip_available(uint32_t ip) {
    return _ip2eth.find(ip) != _ip2eth.end() && _ip2eth[ip].is_recv_time && (_now_time - _ip2eth[ip].time <= 30000);
}

void NetworkInterface::arp_request(uint32_t ip) {
    ARPMessage arp_payload;
    arp_payload.sender_ethernet_address = _ethernet_address;
    arp_payload.sender_ip_address = _ip_address.ipv4_numeric();
    arp_payload.target_ip_address = ip;
    arp_payload.opcode = ARPMessage::OPCODE_REQUEST;
    EthernetFrame frame;
    frame.header().dst = ETHERNET_BROADCAST;
    frame.header().src = _ethernet_address;
    frame.header().type = EthernetHeader::TYPE_ARP;
    frame.payload() = arp_payload.serialize();
    _frames_out.emplace(move(frame));
}

void NetworkInterface::arp_reply(uint32_t target_ip, const EthernetAddress &target_eth_addr) {
    ARPMessage arp_payload;
    arp_payload.sender_ethernet_address = _ethernet_address;
    arp_payload.sender_ip_address = _ip_address.ipv4_numeric();
    arp_payload.target_ethernet_address = target_eth_addr;
    arp_payload.target_ip_address = target_ip;
    arp_payload.opcode = ARPMessage::OPCODE_REPLY;
    EthernetFrame frame;
    frame.header().dst = target_eth_addr;
    frame.header().src = _ethernet_address;
    frame.header().type = EthernetHeader::TYPE_ARP;
    frame.payload() = arp_payload.serialize();
    _frames_out.emplace(move(frame));
}

void NetworkInterface::send_datagram_in_tmp(uint32_t ip) {
    auto &unsend_dgrams = _unsend_frames[ip];
    for (auto &dgram : unsend_dgrams) {
        dgram.header().dst = _ip2eth[ip].addr;
        _frames_out.emplace(move(dgram));
    }
    unsend_dgrams.clear();
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetFrame frame;
    frame.header().src = _ethernet_address;
    frame.header().type = EthernetHeader::TYPE_IPv4;
    frame.payload() = dgram.serialize();
    if (is_ip_available(next_hop_ip)) {
        frame.header().dst = _ip2eth[next_hop_ip].addr;
        _frames_out.emplace(move(frame));
    } else {
        _unsend_frames[next_hop_ip].emplace_back(move(frame));
        if (_ip2eth.find(next_hop_ip) == _ip2eth.end() || _ip2eth[next_hop_ip].is_recv_time ||
            _now_time - _ip2eth[next_hop_ip].time > 5000) {
            arp_request(next_hop_ip);
            _ip2eth[next_hop_ip].is_recv_time = false;
            _ip2eth[next_hop_ip].time = _now_time;
        }
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST)
        return {};

    const auto frame_type = frame.header().type;
    if (frame_type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        if (dgram.parse(frame.payload()) == ParseResult::NoError) {
            return optional(dgram);
        }
    } else if (frame_type == EthernetHeader::TYPE_ARP) {
        ARPMessage arp_msg;
        if (arp_msg.parse(frame.payload()) == ParseResult::NoError) {
            const auto sender_ip = arp_msg.sender_ip_address;
            _ip2eth[sender_ip].addr = arp_msg.sender_ethernet_address;
            _ip2eth[sender_ip].time = _now_time;
            _ip2eth[sender_ip].is_recv_time = true;
            if (arp_msg.opcode == ARPMessage::OPCODE_REPLY) {
                send_datagram_in_tmp(sender_ip);
            } else if (arp_msg.opcode == ARPMessage::OPCODE_REQUEST) {
                if (arp_msg.target_ip_address == _ip_address.ipv4_numeric()) {
                    arp_reply(arp_msg.sender_ip_address, arp_msg.sender_ethernet_address);
                }
            }
        }
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { _now_time += ms_since_last_tick; }
