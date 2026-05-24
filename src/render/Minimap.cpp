#include "render/Minimap.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace uo::render {

namespace {

// Minimum view, in isometric screen units (su = x-y, sv = x+y). A ±40-tile
// world square spans ~160 screen units, so this keeps roughly that area in
// view when standing still or following a tiny path.
constexpr int kMinSpan = 160;
// Padding (screen units) around the player+path bounding box so the route
// isn't flush against the panel edge.
constexpr int kMargin = 10;

constexpr u16 Rgb(int r, int g, int b) {
    return static_cast<u16>(0x8000 | ((r & 31) << 10) | ((g & 31) << 5) | (b & 31));
}

constexpr u16 kColVoid     = Rgb(1, 1, 2);     // off-map / no terrain
constexpr u16 kColSentinel = Rgb(30, 30, 30);  // radarcol 0x7FFF light-grey marker
constexpr u16 kColBorder   = Rgb(20, 20, 22);  // panel frame
constexpr u16 kColPath     = Rgb(0, 31, 31);   // planned route
constexpr u16 kColPlayer   = Rgb(31, 31, 31);  // the player marker
constexpr u16 kColGoal     = Rgb(31, 4, 4);    // destination marker

// radarcol RGB555 -> our opaque ARGB1555 (top bit = alpha). 0x7FFF is the
// client's light-grey sentinel.
inline u16 ToPixel(u16 rgb555) {
    return rgb555 == 0x7FFF ? kColSentinel : static_cast<u16>(rgb555 | 0x8000);
}

}  // namespace

Minimap::Minimap(int size)
    : size_(size < 16 ? 16 : size),
      fb_(static_cast<usize>(size_) * size_, kColVoid) {}

// Build the 64 radar colours for one 8x8 block: topmost surface wins, exactly
// like CRadarGump_Update — land first, then any static whose z is >= the
// running top z overrides it (later statics win ties, matching the client's
// z-buffer compare `staticZ+128 >= zbuf`).
void Minimap::FillBlock(uo::map::Map& map, const RadarColors& radar,
                        u32 bx, u32 by, std::vector<u16>& cols) {
    cols.assign(64, kColVoid);
    int topZ[64];
    for (int i = 0; i < 64; ++i) topZ[i] = INT_MIN;

    uo::map::MapBlock mb;
    if (map.ReadBlock(bx, by, &mb)) {
        for (int i = 0; i < 64; ++i) {
            bool has = false;
            const u16 c = radar.Land(mb.cells[i].tileId, &has);
            topZ[i] = mb.cells[i].z;
            cols[i] = has ? ToPixel(c) : kColVoid;
        }
    }

    if (staticsBuf_.size() < 2048) staticsBuf_.resize(2048);
    u32 n = 0;
    if (map.ReadStatics(bx, by, staticsBuf_.data(),
                        static_cast<u32>(staticsBuf_.size()), &n)) {
        for (u32 i = 0; i < n; ++i) {
            const uo::map::StaticItem& s = staticsBuf_[i];
            const int idx = (s.cellY & 7) * 8 + (s.cellX & 7);
            if (static_cast<int>(s.z) >= topZ[idx]) {
                topZ[idx] = s.z;
                cols[idx] = ToPixel(radar.Static(s.itemId));
            }
        }
    }
}

u16 Minimap::CellColor(uo::map::Map& map, const RadarColors& radar, i32 x, i32 y) {
    if (x < 0 || y < 0) return kColVoid;
    const u32 bx = static_cast<u32>(x) / 8, by = static_cast<u32>(y) / 8;
    if (bx >= map.WidthBlocks() || by >= map.HeightBlocks()) return kColVoid;
    const u64 key = (static_cast<u64>(bx) << 32) | by;
    auto it = blockColors_.find(key);
    if (it == blockColors_.end()) {
        std::vector<u16> cols;
        FillBlock(map, radar, bx, by, cols);
        it = blockColors_.emplace(key, std::move(cols)).first;
    }
    return it->second[(static_cast<u32>(y) & 7) * 8 + (static_cast<u32>(x) & 7)];
}

void Minimap::PutPixel(int x, int y, u16 color) {
    if (x < 0 || x >= size_ || y < 0 || y >= size_) return;
    fb_[static_cast<usize>(y) * size_ + x] = color;
}

void Minimap::FillMarker(int cx, int cy, int r, u16 color) {
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            PutPixel(cx + dx, cy + dy, color);
}

void Minimap::DrawLine(int x0, int y0, int x1, int y1, u16 color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        PutPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Minimap::Render(uo::map::Map& map, const RadarColors& radar,
                     i32 playerX, i32 playerY,
                     const i32* pathX, const i32* pathY, usize nPath) {
    // Cheap change signature: skip the (potentially thousands of) cell lookups
    // when neither the player nor the path moved since last frame.
    u64 sig = nPath * 2654435761ull;
    for (usize i = 0; i < nPath; ++i)
        sig = sig * 1099511628211ull ^ static_cast<u32>(pathX[i]) ^
              (static_cast<u64>(static_cast<u32>(pathY[i])) << 20);
    if (rendered_ && playerX == lastPlayerX_ && playerY == lastPlayerY_ &&
        sig == lastPathSig_)
        return;
    lastPlayerX_ = playerX;
    lastPlayerY_ = playerY;
    lastPathSig_ = sig;
    rendered_ = true;

    // Match the world window's isometric projection: su = x - y is the screen
    // right axis, sv = x + y the screen down axis. A north step (0,-1) -> (+1,-1)
    // goes up-right, so north points to the top-right corner. The bounding box
    // is computed in this rotated space so the rotated route always fits.
    auto su = [](i32 x, i32 y) { return x - y; };
    auto sv = [](i32 x, i32 y) { return x + y; };

    i32 minU = su(playerX, playerY), maxU = minU;
    i32 minV = sv(playerX, playerY), maxV = minV;
    for (usize i = 0; i < nPath; ++i) {
        const i32 u = su(pathX[i], pathY[i]), v = sv(pathX[i], pathY[i]);
        minU = std::min(minU, u); maxU = std::max(maxU, u);
        minV = std::min(minV, v); maxV = std::max(maxV, v);
    }
    minU -= kMargin; minV -= kMargin; maxU += kMargin; maxV += kMargin;
    const i32 spanU = maxU - minU + 1, spanV = maxV - minV + 1;
    const i32 span = std::max(std::max(spanU, spanV), kMinSpan);
    const double centerU = (minU + maxU) * 0.5;
    const double centerV = (minV + maxV) * 0.5;
    const double scale = static_cast<double>(size_) / span;
    const double inv = 1.0 / scale;
    const double half = size_ * 0.5;

    auto toPanel = [&](i32 wx, i32 wy, int& ox, int& oy) {
        ox = static_cast<int>(std::lround(half + (su(wx, wy) - centerU) * scale));
        oy = static_cast<int>(std::lround(half + (sv(wx, wy) - centerV) * scale));
    };

    // Terrain fill: invert each panel pixel to (u,v) screen space, then back to
    // world (x,y) = ((u+v)/2, (v-u)/2).
    for (int oy = 0; oy < size_; ++oy) {
        for (int ox = 0; ox < size_; ++ox) {
            const double u = centerU + (ox - half) * inv;
            const double v = centerV + (oy - half) * inv;
            const i32 wx = static_cast<i32>(std::lround((u + v) * 0.5));
            const i32 wy = static_cast<i32>(std::lround((v - u) * 0.5));
            fb_[static_cast<usize>(oy) * size_ + ox] = CellColor(map, radar, wx, wy);
        }
    }

    // Route polyline.
    for (usize i = 1; i < nPath; ++i) {
        int x0, y0, x1, y1;
        toPanel(pathX[i - 1], pathY[i - 1], x0, y0);
        toPanel(pathX[i], pathY[i], x1, y1);
        DrawLine(x0, y0, x1, y1, kColPath);
    }
    // Goal marker (only when there is an actual route beyond the player cell).
    if (nPath > 1) {
        int gx, gy;
        toPanel(pathX[nPath - 1], pathY[nPath - 1], gx, gy);
        FillMarker(gx, gy, 2, kColGoal);
    }
    // Player marker.
    {
        int mx, my;
        toPanel(playerX, playerY, mx, my);
        FillMarker(mx, my, 2, kColPlayer);
    }

    // Panel frame.
    for (int i = 0; i < size_; ++i) {
        fb_[i] = kColBorder;
        fb_[static_cast<usize>(size_ - 1) * size_ + i] = kColBorder;
        fb_[static_cast<usize>(i) * size_] = kColBorder;
        fb_[static_cast<usize>(i) * size_ + (size_ - 1)] = kColBorder;
    }
}

}
