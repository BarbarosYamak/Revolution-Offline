#pragma once

#include "uo/types.h"
#include "uo/endian.h"

#include <cstring>

namespace uo {

// Minimal bounded reader/writer for UO packets.
// All multi-byte fields are big-endian on the wire.
// Out-of-bounds Read* returns 0 and sets ok=false; callers should check ok().

class BufReader {
public:
    BufReader(const u8* data, usize size) : data_(data), size_(size), pos_(0), ok_(true) {}

    bool ok() const { return ok_; }
    usize pos() const { return pos_; }
    usize remaining() const { return pos_ <= size_ ? size_ - pos_ : 0; }
    const u8* ptr() const { return data_ + pos_; }

    u8 ReadU8() {
        if (!CanRead(1)) return 0;
        u8 v = data_[pos_];
        pos_ += 1;
        return v;
    }

    u16 ReadU16() {
        if (!CanRead(2)) return 0;
        u16 v = LoadBE16(data_ + pos_);
        pos_ += 2;
        return v;
    }

    u32 ReadU32() {
        if (!CanRead(4)) return 0;
        u32 v = LoadBE32(data_ + pos_);
        pos_ += 4;
        return v;
    }

    // Fixed-size ASCII string, NUL-terminated within len bytes.
    void ReadFixedAscii(char* dst, usize len) {
        if (!CanRead(len)) { if (len) dst[0] = '\0'; return; }
        std::memcpy(dst, data_ + pos_, len);
        // Ensure NUL terminator inside the buffer if room exists
        if (len > 0) dst[len - 1] = '\0';
        pos_ += len;
    }

    void Skip(usize n) {
        if (!CanRead(n)) return;
        pos_ += n;
    }

private:
    bool CanRead(usize n) {
        if (pos_ + n > size_) { ok_ = false; return false; }
        return true;
    }

    const u8* data_;
    usize size_;
    usize pos_;
    bool ok_;
};

class BufWriter {
public:
    BufWriter(u8* data, usize cap) : data_(data), cap_(cap), pos_(0), ok_(true) {}

    bool ok() const { return ok_; }
    usize size() const { return pos_; }
    u8* data() { return data_; }

    void WriteU8(u8 v) {
        if (!CanWrite(1)) return;
        data_[pos_++] = v;
    }
    void WriteU16(u16 v) {
        if (!CanWrite(2)) return;
        StoreBE16(data_ + pos_, v);
        pos_ += 2;
    }
    void WriteU32(u32 v) {
        if (!CanWrite(4)) return;
        StoreBE32(data_ + pos_, v);
        pos_ += 4;
    }

    // Copies up to len bytes of src; if src is shorter, pads remainder with 0.
    void WriteFixedAscii(const char* src, usize len) {
        if (!CanWrite(len)) return;
        usize i = 0;
        for (; src && src[i] && i < len; ++i) data_[pos_ + i] = static_cast<u8>(src[i]);
        for (; i < len; ++i) data_[pos_ + i] = 0;
        pos_ += len;
    }

    void WriteBytes(const u8* src, usize n) {
        if (!CanWrite(n)) return;
        std::memcpy(data_ + pos_, src, n);
        pos_ += n;
    }

    // Patch a previously-written u16 (used for variable-length packet size).
    void PatchU16(usize at, u16 v) {
        if (at + 2 > pos_) { ok_ = false; return; }
        StoreBE16(data_ + at, v);
    }

private:
    bool CanWrite(usize n) {
        if (pos_ + n > cap_) { ok_ = false; return false; }
        return true;
    }

    u8* data_;
    usize cap_;
    usize pos_;
    bool ok_;
};

}
