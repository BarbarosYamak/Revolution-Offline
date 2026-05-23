#include "uo/anim.h"

namespace uo::anim {

namespace {

inline u16 RdU16(const u8* p) { return static_cast<u16>(p[0] | (p[1] << 8)); }
inline u32 RdU32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) |
           (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

constexpr u32 kSentinel    = 0x7FFF7FFFu;
constexpr u32 kPaletteSize = 256u * 2u;       // 256 ARGB1555 entries

}  // namespace

bool AnimLoader::Open(const char* idxPath, const char* mulPath) {
    return idx_.Open(idxPath) && mul_.Open(mulPath);
}

// High-detail anim.mul index: bodies are grouped by category, each category
// reserves a fixed number of action groups (110/65/175 = 22/13/35 actions of
// 5 stored directions). storedDir is the mirrored 0..4 direction.
u32 AnimLoader::IndexFor(u16 body, u8 action, u8 storedDir) {
    u32 base;
    if (body < 200)      base = static_cast<u32>(body) * 110u;
    else if (body < 400) base = 22000u + static_cast<u32>(body - 200) * 65u;
    else                 base = 35000u + static_cast<u32>(body - 400) * 175u;
    return base + static_cast<u32>(action) * 5u + storedDir;
}

const Frame* AnimLoader::Body(u16 body, u8 dir, u8 action) {
    if (!IsOpen()) return nullptr;
    const u8 d = dir & 7u;
    const u32 key = (static_cast<u32>(body) << 16) | (static_cast<u32>(action) << 4) | d;
    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second.px.empty() ? nullptr : &it->second;
    return Load(key, body, action, d);
}

const Frame* AnimLoader::Load(u32 key, u16 body, u8 action, u8 dir) {
    Frame& f = cache_[key];   // inserts an empty (negative-cached) frame

    // Facing (0=N..7=NW) -> stored anim direction (0..4) + mirror. Verified
    // from the client's tables g_CDHDHueShift (@0x514B50, stored dir) and
    // g_CDHDDirFacingColors (@0x514B70, mirror flag): stored dirs run SE..NW
    // and the east-facing half is produced by mirroring. Using the naive
    // dir/8-dir scheme made every body face the wrong way.
    static const u8  kStoredDir[8] = { 3, 2, 1, 0, 1, 2, 3, 4 };
    static const bool kMirror[8]   = { true, true, true, false, false, false, false, false };
    const bool mirror = kMirror[dir & 7];
    const u8 stored   = kStoredDir[dir & 7];
    const u32 index = IndexFor(body, action, stored);

    if (!idx_.Seek(static_cast<i64>(index) * 12, 0)) return nullptr;
    u32 entry[3];
    if (!idx_.Read(entry, sizeof(entry))) return nullptr;
    const u32 lookup = entry[0];
    const u32 length = entry[1];
    if (lookup == 0xFFFFFFFFu || length < kPaletteSize + 8u || length > (1u << 22))
        return nullptr;

    std::vector<u8> raw(length);
    if (!mul_.Seek(static_cast<i64>(lookup), 0)) return nullptr;
    if (!mul_.Read(raw.data(), length)) return nullptr;

    const u8* pal = raw.data();                       // 256 u16 palette
    const u32 frameCount = RdU32(&raw[kPaletteSize]);
    if (frameCount == 0) return nullptr;
    if (kPaletteSize + 4u + 4u > length) return nullptr;
    const u32 off0 = RdU32(&raw[kPaletteSize + 4u]);  // frameOffset[0]

    // Frame data sits at palette-start + frameOffset; first 8 bytes are the
    // { cx, cy, w, h } header (used only for hit-test bounds in the client —
    // pixels anchor at the fixed canvas origin), command stream follows.
    const usize frameBase = static_cast<usize>(kPaletteSize) + off0;
    if (frameBase + 8u > raw.size()) return nullptr;

    f.px.assign(static_cast<usize>(Frame::kW) * Frame::kH, 0);
    usize cur = frameBase + 8u;
    while (cur + 4u <= raw.size()) {
        const u32 cmd = RdU32(&raw[cur]);
        cur += 4u;
        if (cmd == kSentinel) break;
        const int xOff = static_cast<i32>(cmd) >> 22;          // signed bits[31:22]
        const int yOff = static_cast<i32>(cmd << 10) >> 22;    // signed bits[21:12]
        const int run  = static_cast<int>(cmd & 0xFFFu);
        if (run <= 0) continue;
        if (cur + static_cast<usize>(run) > raw.size()) break;

        const int row = yOff + Frame::kAnchorY;
        if (row >= 0 && row < Frame::kH) {
            u16* drow = &f.px[static_cast<usize>(row) * Frame::kW];
            const int colBase = mirror ? (Frame::kAnchorX - xOff - run)
                                       : (Frame::kAnchorX + xOff);
            for (int i = 0; i < run; ++i) {
                const int col = colBase + i;
                if (col < 0 || col >= Frame::kW) continue;
                const u8 pidx = mirror ? raw[cur + (run - 1 - i)] : raw[cur + i];
                drow[col] = RdU16(&pal[2u * pidx]);
            }
        }
        cur += static_cast<usize>(run);
    }

    // Guard against an all-transparent decode (treat as "no frame").
    for (u16 p : f.px)
        if (p) return &f;
    f.px.clear();
    return nullptr;
}

}
