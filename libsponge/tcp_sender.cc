#include "tcp_sender.hh"

#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

#include <iostream>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

template <typename Index, typename Content>
void Timers<Index, Content>::start_timer(size_t time, const Index& index, const Content& content) {
    _timer_list.emplace_back(TimeNode{time, index, content});
}

template <typename Index, typename Content>
bool Timers<Index, Content>::remove_timer_before_index(const Index& index) {
    auto timenode_cmp_by_index = [&index](const TimeNode& node){
        return node.index <= index;
    };
    auto new_end = remove_if(_timer_list.begin(), _timer_list.end(), 
        timenode_cmp_by_index);
    bool ret = new_end != _timer_list.end();
    if (ret) {
        _timer_list.resize(new_end - _timer_list.begin());
    }
    return ret;
}

template <typename Index, typename Content>
optional<Content> Timers<Index, Content>::expired_with_min_index(size_t now_time, Index *ret_idx) {
    optional<Content> ret;
    auto timenode_cmp_by_time = [now_time, this](const TimeNode& node){
        return now_time - node.time >= this->_timeout;
    };
    bool is_first(true);
    auto ret_p = _timer_list.end();
    for(auto p = _timer_list.begin(); p != _timer_list.end(); p++){
        const TimeNode& node = *p;
        if (timenode_cmp_by_time(node)){
            if (is_first || node.index < *ret_idx){
                *ret_idx = node.index;
                ret_p = p;
                is_first = false;
            }
        }
    }
    
    if (ret_p != _timer_list.end()){
        ret.emplace(ret_p->content);
        _timer_list.erase(ret_p);
    }
    return ret;
}

template <typename Index, typename Content>
void Timers<Index, Content>::restart_all_timers(size_t now_time) {
    auto restart_func = [now_time](TimeNode& node){node.time = now_time;};
    for_each(_timer_list.begin(), _timer_list.end(), restart_func);
}

template<typename Index, typename Content>
void Timers<Index, Content>::restart_timers_except_min_index(size_t now_time) {
    auto idx_cmp = [](const TimeNode& a, const TimeNode& b) {return a.index < b.index;};
    auto min_idx = min_element(_timer_list.begin(), _timer_list.end(), idx_cmp)->index;
    for_each(_timer_list.begin(), _timer_list.end(), [min_idx, now_time](TimeNode& node){
        if (node.index != min_idx) {
            node.time = now_time;
        }
    });
}

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _rto{_initial_retransmission_timeout}
    , _stream(capacity)
    , _timers(_rto) {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _ack_seqno; }

void TCPSender::fill_window() {
    size_t window_size = _window_size.value_or(0);
    auto remain_size = _stream.buffer_size();
    bool syn_tmp{_syn};

    while (_syn || (window_size > 0 && (remain_size > 0 || (_stream.eof() && !_fin))))  {
        TCPSegment seg;
        seg.header().syn = _syn;
        seg.header().seqno = next_seqno();
        
        auto payload_size = min(window_size - (_syn ? 1 : 0), 
            min(remain_size, TCPConfig::MAX_PAYLOAD_SIZE));
        _fin = _stream.input_ended() && (payload_size == remain_size)
            && (window_size > payload_size + (_syn ? 1 : 0));
        seg.header().fin = _fin;
        seg.payload() = Buffer(_stream.read(payload_size));

        _segments_out.emplace(seg);

        _next_seqno += seg.length_in_sequence_space();
        _timers.start_timer(_now_time, _next_seqno, seg);

        window_size -= seg.length_in_sequence_space();
        remain_size -= payload_size;
        _syn = false;
    }
    if (!syn_tmp) {
        _window_size.emplace(window_size);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    auto ack_seqno = unwrap(ackno, _isn, _next_seqno);
    if (ack_seqno <= _next_seqno) {
        auto is_remove = _timers.remove_timer_before_index(ack_seqno);
        // is_remove is true: one or more tcp segments have received.
        // Otherwise, ack is repeat or not the full segment has received, 
        // maybe it need to resend the segment.
        if (is_remove) {
            _timers.restart_all_timers(_now_time);
        } else {
            _timers.restart_timers_except_min_index(_now_time);
        }
       
        // when at least one segment has received, set the window size.
        // `_ack_seqno == ack_seqno` means that a segment with bigger index has
        // received. 
        if(is_remove || _ack_seqno == ack_seqno) {
            _ack_seqno = ack_seqno;
            _is_zero_win = window_size == 0;
            const auto actual_win_size = window_size - bytes_in_flight();
            _window_size.emplace(
                actual_win_size > 0 ? actual_win_size : 1);
        }
        _rto = _initial_retransmission_timeout;
        _timers.set_timeout(_rto);
        _retx = 0;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    _now_time += ms_since_last_tick;
    
    uint64_t index{0};
    auto expired_seg = _timers.expired_with_min_index(_now_time, &index);

    if (expired_seg.has_value()){
        _segments_out.emplace(expired_seg.value());
        _timers.start_timer(_now_time, index, expired_seg.value());
        if (!_is_zero_win) {
            _rto <<= 1;
            _timers.set_timeout(_rto);
            _retx++;
        }
        _timers.restart_all_timers(_now_time);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retx; }

// Be invoked iff send ack segment 
void TCPSender::send_empty_segment() {
    // no payload or syn/fin so it does not need to consider window size.
    TCPSegment seg;
    seg.header().seqno = next_seqno();
    _segments_out.emplace(seg);
    _next_seqno += seg.length_in_sequence_space(); 
    // _timers.start_timer(_now_time, _next_seqno, seg);
}
