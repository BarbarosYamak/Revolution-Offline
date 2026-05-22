#include "net/PacketStream.h"

#include "uo/endian.h"
#include "uo/packet_lengths.h"

#include <cstring>

namespace uo::net {

bool PacketStream::FeedBytes(const u8* src, usize n) {
    if (n == 0) return true;
    // Compact to keep head at zero when most data is consumed.
    if (head_ > 0 && tail_ + n > kCapacity) Compact();
    if (tail_ + n > kCapacity) return false;
    std::memcpy(buf_ + tail_, src, n);
    tail_ += n;
    return true;
}

void PacketStream::Compact() {
    usize n = tail_ - head_;
    if (n > 0 && head_ > 0) std::memmove(buf_, buf_ + head_, n);
    head_ = 0;
    tail_ = n;
}

bool PacketStream::TryNext(const u8** out_data, usize* out_size, const char** err) {
    if (err) *err = nullptr;
    if (head_ == tail_) return false;

    u8 cmd = buf_[head_];
    u16 len_field = kPacketLength[cmd];

    if (len_field == 0) {
        if (err) *err = "unknown opcode (no length entry)";
        return false;
    }

    usize total;
    if ((len_field & kPacketLengthVariable) != 0) {
        // Need at least 3 bytes to read the big-endian length.
        if (tail_ - head_ < 3) return false;
        u16 wire_len = LoadBE16(buf_ + head_ + 1);
        if (wire_len < 3) {
            if (err) *err = "variable-length packet declares length < 3";
            return false;
        }
        total = wire_len;
    } else {
        total = len_field;
    }

    if (tail_ - head_ < total) return false;

    *out_data = buf_ + head_;
    *out_size = total;
    head_ += total;

    // Cheap compaction when the buffer is drained.
    if (head_ == tail_) { head_ = 0; tail_ = 0; }
    return true;
}

}
