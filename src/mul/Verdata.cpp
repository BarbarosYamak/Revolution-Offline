#include "uo/verdata.h"

#include "uo/endian.h"

#include <cstdio>

namespace uo::mul {

bool Verdata::Open(const char* path) {
    loaded_ = false;
    index_.clear();
    data_.clear();
    if (!path || !*path) return false;

    std::FILE* f = std::fopen(path, "rb");
    if (!f) return true;  // no verdata present — overlay is simply empty

    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 4) {  // header (count) must fit
        std::fclose(f);
        return false;
    }

    data_.resize(static_cast<usize>(sz));
    const bool ok = std::fread(data_.data(), 1, data_.size(), f) == data_.size();
    std::fclose(f);
    if (!ok) {
        data_.clear();
        return false;
    }

    const u32   count      = LoadLE32(data_.data());
    const usize tableBytes = static_cast<usize>(count) * 20;
    if (4 + tableBytes > data_.size()) {
        data_.clear();
        return false;
    }

    index_.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        const u8* e       = data_.data() + 4 + static_cast<usize>(i) * 20;
        const u32 fileId  = LoadLE32(e);
        const u32 blockId = LoadLE32(e + 4);
        const u32 offset  = LoadLE32(e + 8);
        const u32 size    = LoadLE32(e + 12);
        // entry[16..19] = extra (gump dims etc.) — unused for map/statics.

        // Skip entries whose payload runs past EOF rather than abort the
        // whole overlay; a single corrupt record shouldn't void valid ones.
        if (size != 0 &&
            static_cast<usize>(offset) + size > data_.size()) {
            continue;
        }
        // Last entry in file order wins (matches the client's prepend-to-head
        // hash list, whose head is the last one inserted).
        index_[Key(fileId, blockId)] = Entry{offset, size};
    }

    loaded_ = true;
    return true;
}

bool Verdata::Lookup(u32 fileId, u32 blockId,
                     const u8** outPtr, u32* outLen) const {
    const auto it = index_.find(Key(fileId, blockId));
    if (it == index_.end()) return false;

    const Entry& e = it->second;
    if (e.size == 0) {  // block explicitly cleared
        if (outPtr) *outPtr = nullptr;
        if (outLen) *outLen = 0;
        return true;
    }
    if (outPtr) *outPtr = data_.data() + e.offset;
    if (outLen) *outLen = e.size;
    return true;
}

}
