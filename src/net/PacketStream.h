#pragma once

#include "uo/types.h"

namespace uo::net {

// Buffered byte stream that emits one complete UO packet at a time.
//
// Wire format mirrors the original client:
//   [cmd : u8]
//   if PacketIsVariable(cmd): [length : u16 big-endian, includes cmd byte]
//                             [payload : length - 3 bytes]
//   else:                     [payload : kPacketLength[cmd] - 1 bytes]
//
// Caller appends raw bytes via FeedBytes(...) as recv() yields them,
// then calls TryNext(...) in a loop to extract whole packets.

class PacketStream {
public:
    static constexpr usize kCapacity = 64 * 1024;

    PacketStream() : head_(0), tail_(0) {}

    // Append n bytes from src into the internal buffer. Returns false
    // if the buffer would overflow; in that case nothing is appended.
    bool FeedBytes(const u8* src, usize n);

    // Try to pull one complete packet from the buffer.
    // On success: writes pointer + size of the packet (pointing into the
    //   internal buffer — valid until the next Feed/TryNext call) and
    //   returns true.
    // On no-data: returns false.
    // On protocol error (unknown opcode with no fixed length): returns
    //   false and sets *err to a static string.
    bool TryNext(const u8** out_data, usize* out_size, const char** err);

    // Bytes still buffered (not yet consumed by TryNext).
    usize Pending() const { return tail_ - head_; }

    void Reset() { head_ = 0; tail_ = 0; }

private:
    void Compact();

    u8 buf_[kCapacity];
    usize head_;
    usize tail_;
};

}
