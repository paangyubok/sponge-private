#include "byte_stream.hh"

#include <algorithm>
#include <cstddef>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : _buffer(capacity)
    , _capacity(capacity)
    , _unread_idx(0)
    , _unassem_idx(0)
    , _eif(false)
    , _eof(false)
    , _total_write(0)
    , _total_read(0) {
    DUMMY_CODE(capacity);
}

size_t ByteStream::write(const string &data) {
    size_t need_write = data.size();
    if (_capacity - _unassem_idx < need_write) {
        copy(_buffer.begin() + _unread_idx, _buffer.begin() + _unassem_idx, _buffer.begin());
        _unassem_idx -= _unread_idx;
        _unread_idx = 0;
    }
    size_t write_len = min(data.size(), _capacity - _unassem_idx);
    copy(data.begin(), data.begin() + write_len, _buffer.begin() + _unassem_idx);
    _unassem_idx += write_len;
    _total_write += write_len;
    return write_len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t read_len = min(len, _unassem_idx - _unread_idx);
    if (read_len == 0) {
        return {};
    }
    string ret(&_buffer[_unread_idx], &_buffer[_unread_idx + read_len]);
    return ret;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t pop_len = min(len, _unassem_idx - _unread_idx);
    _unread_idx += pop_len;
    _total_read += pop_len;
    if (buffer_empty() && _eif) {
        _eof = true;
    }
}
//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    size_t read_len = min(len, _unassem_idx - _unread_idx);
    if (read_len == 0)
        return {};
    string ret(&_buffer[_unread_idx], &_buffer[_unread_idx + read_len]);
    _unread_idx += read_len;
    _total_read += read_len;
    if (buffer_empty() && _eif) {
        _eof = true;
    }
    return ret;
}

void ByteStream::end_input() {
    _eif = true;
    if (buffer_empty()) {
        _eof = true;
    }
}

bool ByteStream::input_ended() const { return _eif; }

size_t ByteStream::buffer_size() const { return _unassem_idx - _unread_idx; }

bool ByteStream::buffer_empty() const { return _unread_idx == _unassem_idx; }

bool ByteStream::eof() const { return _eof; }

size_t ByteStream::bytes_written() const { return _total_write; }

size_t ByteStream::bytes_read() const { return _total_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - (_unassem_idx - _unread_idx); }
