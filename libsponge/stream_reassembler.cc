#include "stream_reassembler.hh"
#include <cstddef>
#include <utility>
#include <vector>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) :
    _output(capacity),
    _capacity(capacity)
    {} 

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (index + data.size() < _expect) {
        return;
    }
    if (data.size() > _unassem_string[index].size()){
        _unassem_string[index] = data;
    }
    if (eof) {
        _eif = true;
        _end_idx = index + data.size();
    }
    assemble();
}

void StreamReassembler::assemble() {
    vector<size_t> assemed_node;
    pair<bool, pair<size_t, string>> remain_string{false, {}};
    for (auto& node : _unassem_string) {
        auto idx = node.first;
        auto& sub_string = node.second;
        if (idx > _expect) break;
        
        if (idx + sub_string.size() > _expect) {
            auto write_idx = _expect - idx;
            auto write_string = sub_string.substr(write_idx);
            if (_output.remaining_capacity() >= write_string.size()) {
                _output.write(write_string);
                _expect += write_string.size();
            } else {
                auto sub_write_string = write_string.substr(0, _output.remaining_capacity());
                auto write_len = sub_write_string.size();
                _output.write(sub_write_string);
                _expect += write_len;
                remain_string = make_pair(true, make_pair(idx+write_len, write_string.substr(write_len)));
            }
        }
        assemed_node.push_back(idx);
    }
    
    if (_eif && _expect >= _end_idx) {
        _output.end_input();
    }
    for(auto idx : assemed_node) {
        _unassem_string.erase(idx);
    }
    if (remain_string.first) {
        _unassem_string[remain_string.second.first] = move(remain_string.second.second);
    }
}

size_t StreamReassembler::unassembled_bytes() const { 
    size_t ret(0);
    size_t b(0), e(0);
    for(auto& node : _unassem_string){
        auto idx = node.first;
        const auto& sub_string = node.second;
        
        if (idx > e) {
            ret += e - b;
            b = idx;
        }
        e = max(e, idx+sub_string.size());
    }
    ret += e - b;
    return ret;
}

bool StreamReassembler::empty() const { 
    return _output.buffer_empty();
}
