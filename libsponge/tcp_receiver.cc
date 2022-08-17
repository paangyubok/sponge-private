#include "tcp_receiver.hh"
#include "wrapping_integers.hh"
#include <cstddef>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const auto& header = seg.header();
    const auto& payload = seg.payload();
    const bool syn = header.syn;
    const bool fin = header.fin;
    
    if (syn) {
        _isn.emplace(header.seqno);
    } else if (!_isn.has_value()) {
        return;
    }
    
    auto abs_sn = unwrap(header.seqno, _isn.value(), _expect);
    if (_expect >= abs_sn + seg.length_in_sequence_space()) return;
    size_t index = abs_sn > 0 ? abs_sn - 1 : 0;
    _reassembler.push_substring(payload.copy(), 
        index, fin);
        
    _expect = _reassembler.stream_out().bytes_written() 
        + 1 + (_reassembler.stream_out().input_ended()? 1:0);
    _ackno.emplace(wrap(_expect, _isn.value()));
}

optional<WrappingInt32> TCPReceiver::ackno() const { return _ackno; }

size_t TCPReceiver::window_size() const { return _capacity - _reassembler.stream_out().buffer_size(); }
