#include "router.hh"

#include "address.hh"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

inline uint32_t mask(uint8_t prefix_length) { return ~(~0u >> prefix_length); }

inline optional<Router::NextHop> Router::match_ip(uint32_t ip) const {
    for (auto p = _router_table.rbegin(); p != _router_table.rend(); p++) {
        const auto prefix_length = p->first;
        const auto rules = p->second;
        auto match_rule = rules.find(ip & mask(prefix_length));
        if (match_rule != rules.end()) {
            return optional(match_rule->second);
        }
    }
    return {};
}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    // Your code here.
    NextHop next_hop_info{next_hop, interface_num};
    _router_table[prefix_length][route_prefix] = next_hop_info;
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // Your code here.
    const auto dst_ip = dgram.header().dst;
    Address next_hop = Address::from_ipv4_numeric(dst_ip);
    auto match_rule = match_ip(dst_ip);
    if (!match_rule.has_value() || dgram.header().ttl < 2)
        return;
    dgram.header().ttl--;
    if (match_rule->next_hop.has_value()) {
        next_hop = match_rule->next_hop.value();
    }

    interface(match_rule->interface_num).send_datagram(dgram, next_hop);
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
