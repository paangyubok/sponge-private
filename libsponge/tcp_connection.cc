#include "tcp_connection.hh"

#include "tcp_header.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPConnection::set_ack_everytime(TCPHeader &header) {
    header.ack = _receiver.ackno().has_value();
    if (header.ack) {
        header.ackno = _receiver.ackno().value();
    }
    header.win = _receiver.window_size();
}

void TCPConnection::check_is_fin(TCPHeader &header) { _is_fin |= header.fin; }

void TCPConnection::move_all_segments_to_out(std::function<void(TCPHeader &)> edit_header) {
    auto &product_segments = _sender.segments_out();
    while (!product_segments.empty()) {
        auto &segment = product_segments.front();
        check_is_fin(segment.header());
        set_ack_everytime(segment.header());
        edit_header(segment.header());
        _segments_out.emplace(std::move(segment));
        product_segments.pop();
    }
}

bool TCPConnection::check_is_active() const {
    if (_is_rst)
        return false;
    bool is_active = !(_receiver.stream_out().input_ended() && _is_fin && _sender.bytes_in_flight() == 0);
    if (!is_active && _linger_after_streams_finish) {
        is_active = time_since_last_segment_received() < 10 * _cfg.rt_timeout;
    }
    return is_active;
}

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _now_time - _last_receive_time; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _last_receive_time = _now_time;
    if (seg.header().rst) {
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
        _is_rst = true;
        return;
    }

    _receiver.segment_received(seg);
    if (!_is_fin) {
        _linger_after_streams_finish = !_receiver.stream_out().input_ended();
    }

    if (_receiver.ackno().has_value() && seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _sender.fill_window();
    }

    if (_receiver.ackno().has_value() &&
        (seg.length_in_sequence_space() != 0 || _receiver.ackno().value() - 1 == seg.header().seqno)) {
        _sender.fill_window();
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
    }

    move_all_segments_to_out();
}

bool TCPConnection::active() const { return check_is_active(); }

size_t TCPConnection::write(const string &data) {
    size_t ret = _sender.stream_in().write(data);
    _sender.fill_window();
    move_all_segments_to_out();
    return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _now_time += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        move_all_segments_to_out([](TCPHeader &header) { header.rst = true; });
        _is_rst = true;
        _receiver.stream_out().set_error();
        _sender.stream_in().set_error();
    } else {
        move_all_segments_to_out();
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    move_all_segments_to_out();
}

void TCPConnection::connect() {
    _sender.fill_window();
    move_all_segments_to_out();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            _sender.send_empty_segment();
            move_all_segments_to_out([](TCPHeader &header) { header.rst = true; });
            _is_rst = true;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
