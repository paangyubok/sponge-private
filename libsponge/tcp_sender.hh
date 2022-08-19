#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <queue>
#include <vector>

// Timer
template <typename Index, typename Content>
class Timers{
  using TimeNode = struct __time_node{
    size_t time{};
    Index index{};
    Content content{};
  };
  size_t _timeout;
  std::deque<TimeNode> _timer_list{};

  public:
  Timers<Index, Content>(size_t timeout)
    : _timeout(timeout)
    {}
  void set_timeout(size_t timeout) {_timeout = timeout;}
  size_t get_timeout() {return _timeout;}

  void start_timer(size_t time, const Index& index, const Content& content);
  bool remove_timer_before_index(const Index& index);
  std::optional<Content> expired_with_min_index(size_t now_time, Index *ret_idx);
  void restart_all_timers(size_t now_time);
  void restart_timers_except_min_index(size_t now_time);
};

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;
    size_t _rto;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;
    
    Timers<uint64_t, TCPSegment> _timers;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};
    uint64_t _ack_seqno{0};
    size_t _now_time{0};
    uint32_t _retx{0};
    std::optional<uint16_t> _window_size{};
    bool _is_zero_win{false};
    bool _syn{true};
    bool _fin{false};
  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
