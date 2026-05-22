#include "uo/map.h"

#include <cstdio>
#include <cstring>

namespace uo::map {

Map::Map() : widthBlocks_(0), heightBlocks_(0) {}
Map::~Map() = default;

bool Map::Open(const char* mapPath,
               const char* staidxPath,
               const char* staticsPath,
               u32 widthBlocks, u32 heightBlocks,
               const char* verdataPath) {
    if (!map_.Open(mapPath)) {
        std::fprintf(stderr, "map: cannot open '%s'\n", mapPath);
        return false;
    }
    if (!staidx_.Open(staidxPath)) {
        std::fprintf(stderr, "map: cannot open '%s'\n", staidxPath);
        return false;
    }
    if (!statics_.Open(staticsPath)) {
        std::fprintf(stderr, "map: cannot open '%s'\n", staticsPath);
        return false;
    }
    widthBlocks_  = widthBlocks;
    heightBlocks_ = heightBlocks;

    // Optional verdata.mul overlay. Open() returns true with no patches when
    // the file is absent, so a missing verdata is silently fine.
    if (verdataPath && *verdataPath) {
        if (!verdata_.Open(verdataPath)) {
            std::fprintf(stderr, "map: malformed verdata '%s' (ignored)\n",
                         verdataPath);
        } else if (verdata_.IsOpen() && verdata_.PatchCount() > 0) {
            std::fprintf(stderr, "map: verdata '%s' applied (%zu patch block(s))\n",
                         verdataPath, verdata_.PatchCount());
        }
    }

    const u64 expected_map_bytes =
        static_cast<u64>(widthBlocks_) * heightBlocks_ * kBlockBytesOnDisk;
    if (map_.Size() < expected_map_bytes) {
        std::fprintf(stderr,
            "map: '%s' size %llu < expected %llu for %ux%u blocks\n",
            mapPath,
            static_cast<unsigned long long>(map_.Size()),
            static_cast<unsigned long long>(expected_map_bytes),
            widthBlocks_, heightBlocks_);
        return false;
    }
    return true;
}

bool Map::ReadBlock(u32 bx, u32 by, MapBlock* out) {
    if (!out) return false;
    if (bx >= widthBlocks_ || by >= heightBlocks_) return false;

    // Standard UO map layout iterates by column-major: blocks[bx][by]
    // i.e. block_index = bx * heightBlocks + by.  The 8x8 cells inside
    // a block are still row-major (y * 8 + x).
    const u64 blockIndex = static_cast<u64>(bx) * heightBlocks_ + by;

    // verdata override: the patch payload is the full 196-byte on-disk block
    // (4-byte header + 64 cells), exactly what the client maps for this block.
    if (verdata_.IsOpen()) {
        const u8* p = nullptr;
        u32 len = 0;
        if (verdata_.Lookup(mul::kVerdataMap0, static_cast<u32>(blockIndex), &p, &len)) {
            if (p && len >= kBlockBytesOnDisk) {
                std::memcpy(out, p + 4, sizeof(MapBlock)); // skip the header
                return true;
            }
            // A short/cleared map patch is meaningless (a block always has
            // 64 cells); fall through to the base map for those.
        }
    }

    const u64 offset = blockIndex * kBlockBytesOnDisk + 4; // +4 skips header
    if (!map_.Seek(static_cast<i64>(offset), 0)) return false;
    return map_.Read(out, sizeof(MapBlock));
}

bool Map::ReadStatics(u32 bx, u32 by,
                      StaticItem* out, u32 cap, u32* count) {
    if (!count) return false;
    *count = 0;
    if (bx >= widthBlocks_ || by >= heightBlocks_) return false;

    const u64 blockIndex = static_cast<u64>(bx) * heightBlocks_ + by;

    // verdata override: the patch payload is the block's complete statics
    // list (a run of 7-byte records). It replaces both the staidx lookup and
    // the statics0 data; size 0 means the block was emptied of statics.
    if (verdata_.IsOpen()) {
        const u8* p = nullptr;
        u32 len = 0;
        if (verdata_.Lookup(mul::kVerdataStatics, static_cast<u32>(blockIndex), &p, &len)) {
            if (len % sizeof(StaticItem) != 0) {
                std::fprintf(stderr,
                    "map: verdata statics block (%u,%u): length %u not a multiple of 7\n",
                    bx, by, len);
                return false;
            }
            const u32 n = len / sizeof(StaticItem);
            if (out && cap > 0 && n > 0) {
                const u32 to_read = (n < cap) ? n : cap;
                std::memcpy(out, p, to_read * sizeof(StaticItem));
                *count = to_read;
            } else {
                *count = n;  // probe (no buffer) or empty block
            }
            return true;
        }
    }

    const u64 idxOffset  = blockIndex * sizeof(StaticsIndexEntry);

    if (!staidx_.Seek(static_cast<i64>(idxOffset), 0)) return false;
    StaticsIndexEntry idx;
    if (!staidx_.Read(&idx, sizeof(idx))) return false;

    if (idx.lookup == kNoStatics || idx.length == 0) return true;
    if (idx.length % sizeof(StaticItem) != 0) {
        std::fprintf(stderr,
            "map: statics block (%u,%u): length %u not a multiple of 7\n",
            bx, by, idx.length);
        return false;
    }
    const u32 n = idx.length / sizeof(StaticItem);
    if (out && cap > 0) {
        if (!statics_.Seek(static_cast<i64>(idx.lookup), 0)) return false;
        const u32 to_read = (n < cap) ? n : cap;
        if (!statics_.Read(out, to_read * sizeof(StaticItem))) return false;
        *count = to_read;
    } else {
        *count = n; // probe: return count even with no buffer
    }
    return true;
}

bool Map::ReadCell(u32 x, u32 y, LandCell* out) {
    if (!out) return false;
    const u32 bx = x / 8, by = y / 8;
    const u32 cx = x % 8, cy = y % 8;
    MapBlock blk;
    if (!ReadBlock(bx, by, &blk)) return false;
    *out = blk.cells[cy * 8 + cx];
    return true;
}

}
