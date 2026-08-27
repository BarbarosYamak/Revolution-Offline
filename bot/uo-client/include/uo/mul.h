#pragma once

#include "uo/types.h"

#include <cstdio>

namespace uo::mul {

// Tiny bounded FILE* wrapper. MUL loaders use it the same way the
// original client uses FileManager_Open/Read/Seek/Close: synchronous
// stdio, no mmap. Keeps us close to IDB behaviour for byte-parity.

class File {
public:
    File();
    ~File();

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    bool Open(const char* path);
    void Close();
    bool IsOpen() const { return f_ != nullptr; }
    u64  Size() const   { return size_; }

    // Read n bytes into dst. Returns false on short read / failure.
    bool Read(void* dst, usize n);

    // SEEK_SET (whence == 0) or SEEK_CUR (whence == 1).
    bool Seek(i64 offset, int whence);

    u64 Tell() const;

private:
    std::FILE* f_;
    u64        size_;
};

}
