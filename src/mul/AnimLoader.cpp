#include "uo/anim.h"

#include <climits>

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

const Frame* AnimLoader::Body(u16 body, u8 dir, u8 action, u16 frame) {
    const Group* g = GetGroup(body, dir, action);
    if (!g || g->frames.empty()) return nullptr;
    if (frame >= g->frames.size()) frame = static_cast<u16>(g->frames.size() - 1);
    const Frame& f = g->frames[frame];
    return f.px.empty() ? nullptr : &f;   // blank frame in the cycle -> draw nothing
}

u32 AnimLoader::FrameCount(u16 body, u8 dir, u8 action) {
    const Group* g = GetGroup(body, dir, action);
    return g ? static_cast<u32>(g->frames.size()) : 0u;
}

const Group* AnimLoader::GetGroup(u16 body, u8 dir, u8 action) {
    if (!IsOpen()) return nullptr;
    const u8 d = dir & 7u;
    const u32 key = (static_cast<u32>(body) << 16) |
                    (static_cast<u32>(action) << 3) | d;
    auto it = cache_.find(key);
    if (it != cache_.end()) return &it->second;
    return LoadGroup(key, body, action, d);
}

const Group* AnimLoader::LoadGroup(u32 key, u16 body, u8 action, u8 dir) {
    Group& g = cache_[key];   // inserts an empty (negative-cached) group

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

    if (!idx_.Seek(static_cast<i64>(index) * 12, 0)) return &g;
    u32 entry[3];
    if (!idx_.Read(entry, sizeof(entry))) return &g;
    const u32 lookup = entry[0];
    const u32 length = entry[1];
    if (lookup == 0xFFFFFFFFu || length < kPaletteSize + 8u || length > (1u << 22))
        return &g;

    std::vector<u8> raw(length);
    if (!mul_.Seek(static_cast<i64>(lookup), 0)) return &g;
    if (!mul_.Read(raw.data(), length)) return &g;

    const u8* pal = raw.data();                       // 256 u16 palette
    const u32 frameCount = RdU32(&raw[kPaletteSize]);
    if (frameCount == 0 || frameCount > 1024) return &g;
    // frame offset table: u32 frameOffset[frameCount] after frameCount.
    const usize tableStart = static_cast<usize>(kPaletteSize) + 4u;
    if (tableStart + static_cast<usize>(frameCount) * 4u > raw.size()) return &g;

    // Decode every frame, keeping its index so worn-gear layers (which share the
    // body's frame count per action) stay in lockstep. A frame that fails to
    // decode is kept as a blank entry to preserve indexing.
    g.frames.resize(frameCount);
    bool any = false;
    for (u32 fi = 0; fi < frameCount; ++fi) {
        const u32 off = RdU32(&raw[tableStart + static_cast<usize>(fi) * 4u]);
        const usize frameBase = static_cast<usize>(kPaletteSize) + off;
        if (DecodeFrame(raw, pal, frameBase, mirror, g.frames[fi]))
            any = true;
    }
    if (!any) g.frames.clear();   // all-blank group -> negative cache (triggers fallback)
    return &g;
}

// Frame data sits at palette-start + frameOffset; first 8 bytes are the
// { cx, cy, w, h } header. The official client SKIPS it and decodes into a fixed
// 64x128 canvas at origin (32,80) -- clipping large bodies. We keep the same
// command space (col = 32+x, row = 80+y) but fit the canvas to the decoded-pixel
// bounding box, so nothing is clipped. The constant origin means the same
// command coordinate lands at the same screen pixel across a cycle, so the body
// bob/step encoded in the frames is preserved.
bool AnimLoader::DecodeFrame(const std::vector<u8>& raw, const u8* pal,
                             usize frameBase, bool mirror, Frame& out) {
    if (frameBase + 8u > raw.size()) return false;
    const usize cmdStart = frameBase + 8u;

    // Pass 1: bounding box of the decoded pixels.
    int minCol = INT_MAX, maxCol = INT_MIN, minRow = INT_MAX, maxRow = INT_MIN;
    for (usize cur = cmdStart; cur + 4u <= raw.size(); ) {
        const u32 cmd = RdU32(&raw[cur]);
        cur += 4u;
        if (cmd == kSentinel) break;
        const int xOff = static_cast<i32>(cmd) >> 22;          // signed bits[31:22]
        const int yOff = static_cast<i32>(cmd << 10) >> 22;    // signed bits[21:12]
        const int run  = static_cast<int>(cmd & 0xFFFu);
        if (cur + static_cast<usize>(run) > raw.size()) break;
        if (run > 0) {
            const int row = Frame::kOriginY + yOff;
            const int colBase = mirror ? (Frame::kOriginX - xOff - run)
                                       : (Frame::kOriginX + xOff);
            if (row < minRow) minRow = row;
            if (row > maxRow) maxRow = row;
            if (colBase < minCol) minCol = colBase;
            if (colBase + run - 1 > maxCol) maxCol = colBase + run - 1;
        }
        cur += static_cast<usize>(run);
    }
    if (minRow > maxRow || minCol > maxCol) return false;  // empty stream

    const int w = maxCol - minCol + 1;
    const int h = maxRow - minRow + 1;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;  // sanity guard
    out.width  = w;
    out.height = h;
    out.anchorX = Frame::kOriginX - minCol;   // canvas col mapping to command origin
    out.anchorY = Frame::kOriginY - minRow;   // canvas row mapping to command origin
    out.px.assign(static_cast<usize>(w) * h, 0);

    // Pass 2: decode into the fitted buffer. Row/col are in-bounds by
    // construction (the bbox came from the same formula), so no clamping.
    for (usize cur = cmdStart; cur + 4u <= raw.size(); ) {
        const u32 cmd = RdU32(&raw[cur]);
        cur += 4u;
        if (cmd == kSentinel) break;
        const int xOff = static_cast<i32>(cmd) >> 22;
        const int yOff = static_cast<i32>(cmd << 10) >> 22;
        const int run  = static_cast<int>(cmd & 0xFFFu);
        if (cur + static_cast<usize>(run) > raw.size()) break;
        if (run > 0) {
            const int row = Frame::kOriginY + yOff - minRow;
            const int colBase = (mirror ? (Frame::kOriginX - xOff - run)
                                        : (Frame::kOriginX + xOff)) - minCol;
            u16* drow = &out.px[static_cast<usize>(row) * w];
            for (int i = 0; i < run; ++i) {
                const u8 pidx = mirror ? raw[cur + (run - 1 - i)] : raw[cur + i];
                drow[colBase + i] = RdU16(&pal[2u * pidx]);
            }
        }
        cur += static_cast<usize>(run);
    }

    // All-transparent decode counts as "no frame".
    for (u16 p : out.px)
        if (p) return true;
    out.px.clear();
    return false;
}

}
