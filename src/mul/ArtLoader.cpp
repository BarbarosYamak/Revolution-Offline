#include "uo/art.h"

#include <cstring>

namespace uo::art {

namespace {

constexpr u32 kStaticBase = 0x4000;   // static item art begins here
constexpr u16 kLandDim    = 44;       // land tiles are a fixed 44x44 diamond

// little-endian u16 read from a byte buffer (MUL is LE, host is LE x86)
inline u16 RdU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }

// Land art: 1012 pixels (2024 bytes) describing a 44x44 diamond. Top half
// (rows 0..21) widens 2,4,..,44 centered; bottom half (rows 22..43) narrows
// 44,..,2. Everything outside the diamond stays transparent.
void DecodeLand(const std::vector<u8>& raw, Sprite& s) {
    s.width  = kLandDim;
    s.height = kLandDim;
    s.px.assign(static_cast<usize>(kLandDim) * kLandDim, 0);

    const usize avail = raw.size() / 2;   // pixels available
    usize cur = 0;

    for (int y = 0; y < 22; ++y) {
        const int count = (y + 1) * 2;
        const int x     = 22 - (y + 1);
        for (int k = 0; k < count && cur < avail; ++k, ++cur)
            s.px[y * kLandDim + x + k] = RdU16(&raw[cur * 2]);
    }
    for (int y = 0; y < 22; ++y) {
        const int count = (22 - y) * 2;
        const int x     = y;
        for (int k = 0; k < count && cur < avail; ++k, ++cur)
            s.px[(y + 22) * kLandDim + x + k] = RdU16(&raw[cur * 2]);
    }
}

// Static art: { u16 @0, u16 @2, u16 width @4, u16 height @6 }, then a
// height-entry u16 row-offset table (offsets in u16 words from the start of
// pixel data), then per-row runs of (u16 xOffsetDelta, u16 runLength,
// runLength * u16 pixel). A runLength of 0 ends the row.
void DecodeStatic(const std::vector<u8>& raw, Sprite& s) {
    if (raw.size() < 8) return;
    const u16 w = RdU16(&raw[4]);
    const u16 h = RdU16(&raw[6]);
    if (w == 0 || h == 0 || w > 2048 || h > 2048) return;

    s.width  = w;
    s.height = h;
    s.px.assign(static_cast<usize>(w) * h, 0);

    const usize lookup   = 8;                       // byte offset of row table
    const usize dataBase = lookup + 2u * h;         // byte offset of pixel data
    if (dataBase > raw.size()) { s.width = s.height = 0; s.px.clear(); return; }

    for (u16 row = 0; row < h; ++row) {
        const u16 wordOff = RdU16(&raw[lookup + 2u * row]);
        usize cur = dataBase + 2u * wordOff;
        int col = 0;
        while (cur + 4 <= raw.size()) {
            const u16 xOff = RdU16(&raw[cur]);     cur += 2;
            const u16 run  = RdU16(&raw[cur]);     cur += 2;
            if (run == 0) break;
            col += xOff;
            for (u16 k = 0; k < run && cur + 2 <= raw.size(); ++k, cur += 2) {
                const int dx = col + k;
                if (dx >= 0 && dx < w)
                    s.px[static_cast<usize>(row) * w + dx] = RdU16(&raw[cur]);
            }
            col += run;
        }
    }
}

}  // namespace

bool ArtLoader::Open(const char* artIdxPath, const char* artPath) {
    return idx_.Open(artIdxPath) && art_.Open(artPath);
}

const Sprite* ArtLoader::Land(u16 tileId)   { return LoadIndex(tileId, true); }
const Sprite* ArtLoader::Static(u16 itemId) { return LoadIndex(kStaticBase + itemId, false); }

const Sprite* ArtLoader::LoadIndex(u32 index, bool isLand) {
    auto it = cache_.find(index);
    if (it != cache_.end())
        return it->second.width ? &it->second : nullptr;

    Sprite& s = cache_[index];   // inserts an empty (0x0) sprite

    // 12-byte index entry: { u32 lookup, u32 length, u32 extra }.
    if (!idx_.Seek(static_cast<i64>(index) * 12, 0)) return nullptr;
    u32 entry[3];
    if (!idx_.Read(entry, sizeof(entry))) return nullptr;
    const u32 lookup = entry[0];
    const u32 length = entry[1];
    if (lookup == 0xFFFFFFFFu || length == 0 || length > (1u << 24)) return nullptr;

    std::vector<u8> raw(length);
    if (!art_.Seek(static_cast<i64>(lookup), 0)) return nullptr;
    if (!art_.Read(raw.data(), length)) return nullptr;

    if (isLand) DecodeLand(raw, s);
    else        DecodeStatic(raw, s);

    return s.width ? &s : nullptr;
}

}
