#pragma once

#include "uo/types.h"

#include <unordered_map>
#include <vector>

namespace uo::mul {

// verdata.mul patch overlay. Layout is little-endian, verbatim from the
// official 2.0.7 client's VersionFile_Load @ 0x4AAA05:
//
//   u32 count
//   count × { u32 fileId, u32 blockId, u32 offset, u32 size, u32 extra }
//   ... patch payloads live at `offset` (from file start), `size` bytes each.
//
// `fileId` indexes the client's MUL file-spec table (CDHDCache_Initialize
// @ 0x4AB000): 0=map0.mul, 1=staidx0.mul, 2=statics0.mul, 3=artidx.mul, ...
//
// When the client resolves an indexed resource it first checks the verdata
// hash (CDHDCache_GetItemPtr / GetCacheItem); a matching (fileId, blockId)
// entry's payload replaces what the base .mul would have returned. A `size`
// of 0 means "this block is now empty" (e.g. all statics removed). If two
// entries target the same block, the last one in file order wins ("latest
// patch").
//
// We mirror that override for the two resources that affect walkability:
// map0 (land tiles + z) and statics0 (blocking/surface objects).

constexpr u32 kVerdataMap0    = 0;  // map0.mul   — 196-byte land blocks
constexpr u32 kVerdataStatics = 2;  // statics0.mul — runs of 7-byte records

class Verdata {
public:
    Verdata() = default;

    Verdata(const Verdata&) = delete;
    Verdata& operator=(const Verdata&) = delete;

    // Load verdata.mul from `path`. A missing file is fine (stays closed and
    // every Lookup misses). Returns false only on a malformed file.
    bool Open(const char* path);

    bool  IsOpen()     const { return loaded_; }
    usize PatchCount() const { return index_.size(); }

    // Look up the patch for (fileId, blockId). Returns true iff an entry
    // exists. On a hit *outPtr/*outLen describe the payload; for a cleared
    // block (size 0) *outPtr is null and *outLen is 0 but the result is
    // still true so callers honor the override (the block is now empty).
    bool Lookup(u32 fileId, u32 blockId,
                const u8** outPtr, u32* outLen) const;

private:
    struct Entry { u32 offset, size; };
    static u64 Key(u32 fileId, u32 blockId) {
        return (static_cast<u64>(fileId) << 32) | blockId;
    }

    std::vector<u8>              data_;   // whole verdata.mul in memory
    std::unordered_map<u64, Entry> index_; // (fileId,blockId) -> payload span
    bool                         loaded_ = false;
};

}
