#include "uo/tiledata.h"

#include "uo/mul.h"

#include <cstdio>
#include <cstring>

namespace uo::tiledata {

TileDataLoader::TileDataLoader()
    : lands_(nullptr), statics_(nullptr), loaded_(false) {}

TileDataLoader::~TileDataLoader() {
    delete[] lands_;
    delete[] statics_;
}

// Mirrors ObjectManager_LoadTileData @ 0x4C7AE0:
//   - First loop: 16384 land tiles, file stride 26, memory stride 28.
//     Skip 4-byte block header every 32 entries.
//     IDB comment named this buffer `pStaticsTileData` — that's the
//     misleading-IDB-name we keep flagged in tiledata.h.
//   - Second loop: 16384 static tiles, file stride 37, memory stride 40.
//     Same 4-byte block header every 32 entries.
bool TileDataLoader::Load(const char* path) {
    mul::File f;
    if (!f.Open(path)) {
        std::fprintf(stderr, "tiledata: cannot open '%s'\n", path);
        return false;
    }

    lands_   = new LandTile[kLandCount]{};
    statics_ = new StaticTile[kStaticCount]{};

    // --- Land section -------------------------------------------------
    for (u32 i = 0; i < kLandCount; ++i) {
        if ((i & 0x1F) == 0) {
            if (!f.Seek(4, 1)) goto fail;          // block header
        }
        LandTile& t = lands_[i];
        if (!f.Read(&t.flags,     4))  goto fail;  // file +0..3
        if (!f.Read(&t.textureId, 2))  goto fail;  // file +4..5
        if (!f.Read(t.name,       20)) goto fail;  // file +6..25
    }

    // --- Static section -----------------------------------------------
    for (u32 i = 0; i < kStaticCount; ++i) {
        if ((i & 0x1F) == 0) {
            if (!f.Seek(4, 1)) goto fail;
        }
        StaticTile& s = statics_[i];
        if (!f.Read(&s.flags,       4))  goto fail; // file +0..3
        if (!f.Read(&s.weight,      1))  goto fail; // file +4
        if (!f.Read(&s.quality,     1))  goto fail; // file +5
        if (!f.Read(&s.quantity,       4))  goto fail; // file +6..9
        if (!f.Read(&s.animId,         2))  goto fail; // file +10..11
        if (!f.Read(&s.hue,            2))  goto fail; // file +12..13
        if (!f.Read(&s.renderDimIndex, 2))  goto fail; // file +14..15
        if (!f.Read(&s.height,         1))  goto fail; // file +16
        if (!f.Read(s.name,         20)) goto fail; // file +17..36
    }

    loaded_ = true;
    return true;

fail:
    std::fprintf(stderr, "tiledata: short read / I/O error in '%s'\n", path);
    delete[] lands_;   lands_   = nullptr;
    delete[] statics_; statics_ = nullptr;
    loaded_ = false;
    return false;
}

}
