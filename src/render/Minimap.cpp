#include "render/Minimap.h"

#include "uo/world.h"

#include <algorithm>
#include <cmath>

namespace uo::render {

namespace {

// Never zoom in tighter than an 80-tile view, so standing still (or a tiny
// path) still shows useful surroundings rather than a handful of huge cells.
constexpr int kMinSpan = 80;
// Padding tiles around the player+path bounding box so the route isn't flush
// against the panel edge.
constexpr int kMargin = 6;

constexpr u16 Rgb(int r, int g, int b) {
    return static_cast<u16>(0x8000 | ((r & 31) << 10) | ((g & 31) << 5) | (b & 31));
}

constexpr u16 kColVoid    = Rgb(1, 1, 2);    // off-map / no data
constexpr u16 kColWater   = Rgb(4, 8, 24);   // wet tiles
constexpr u16 kColBlocked = Rgb(7, 7, 8);    // walls / impassable
constexpr u16 kColForest  = Rgb(3, 11, 3);   // foliage (woods)
constexpr u16 kColGrass    = Rgb(7, 18, 7);  // open grass
constexpr u16 kColRoad     = Rgb(20, 16, 11);// roads / dirt / floors
constexpr u16 kColBorder   = Rgb(20, 20, 22);// panel frame
constexpr u16 kColPath     = Rgb(0, 31, 31); // planned route
constexpr u16 kColPlayer   = Rgb(31, 31, 31);// the player marker
constexpr u16 kColGoal     = Rgb(31, 4, 4);  // destination marker

bool IsGrassTile(u16 id) { return id >= 0x0003 && id <= 0x0006; }

}  // namespace

Minimap::Minimap(int size)
    : size_(size < 16 ? 16 : size),
      fb_(static_cast<usize>(size_) * size_, kColVoid) {}

u16 Minimap::ComputeColor(uo::world::World& world,
                          const uo::tiledata::TileDataLoader& td,
                          i32 x, i32 y) const {
    if (x < 0 || y < 0) return kColVoid;
    uo::world::WalkQuery q{};
    q.x = static_cast<u32>(x);
    q.y = static_cast<u32>(y);
    q.fromZ = 0;
    const uo::world::WalkResult r = world.QueryCell(q);
    const u32 landFlags = td.Land(r.landTileId).flags;
    if (landFlags & uo::tiledata::kFlagWet) return kColWater;
    if (r.nearFoliage) return kColForest;
    if (!r.walkable || (landFlags & uo::tiledata::kFlagImpassable)) return kColBlocked;
    if (IsGrassTile(r.landTileId)) return kColGrass;
    return kColRoad;
}

u16 Minimap::CellColor(uo::world::World& world,
                       const uo::tiledata::TileDataLoader& td, i32 x, i32 y) {
    if (x < 0 || y < 0) return kColVoid;
    const u32 bx = static_cast<u32>(x) / 8, by = static_cast<u32>(y) / 8;
    const u64 key = (static_cast<u64>(bx) << 32) | by;
    auto it = blockColors_.find(key);
    if (it == blockColors_.end()) {
        std::vector<u16> cols(64);
        for (int cy = 0; cy < 8; ++cy)
            for (int cx = 0; cx < 8; ++cx)
                cols[cy * 8 + cx] = ComputeColor(
                    world, td, static_cast<i32>(bx) * 8 + cx, static_cast<i32>(by) * 8 + cy);
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

void Minimap::Render(uo::world::World& world,
                     const uo::tiledata::TileDataLoader& td,
                     i32 playerX, i32 playerY,
                     const i32* pathX, const i32* pathY, usize nPath) {
    // Cheap change signature: skip the (potentially thousands of) QueryCell
    // calls when neither the player nor the path moved since last frame.
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

    // Bounding box of the player + the whole route, padded, then squared up to
    // a single span so the scale is uniform on both axes (no distortion).
    i32 minX = playerX, maxX = playerX, minY = playerY, maxY = playerY;
    for (usize i = 0; i < nPath; ++i) {
        minX = std::min(minX, pathX[i]); maxX = std::max(maxX, pathX[i]);
        minY = std::min(minY, pathY[i]); maxY = std::max(maxY, pathY[i]);
    }
    minX -= kMargin; minY -= kMargin; maxX += kMargin; maxY += kMargin;
    const i32 spanX = maxX - minX + 1, spanY = maxY - minY + 1;
    const i32 span = std::max(std::max(spanX, spanY), kMinSpan);
    const double centerX = (minX + maxX) * 0.5;
    const double centerY = (minY + maxY) * 0.5;
    const double scale = static_cast<double>(size_) / span;
    const double inv = 1.0 / scale;
    const double half = size_ * 0.5;

    auto toPanel = [&](i32 wx, i32 wy, int& ox, int& oy) {
        ox = static_cast<int>(std::lround(half + (wx - centerX) * scale));
        oy = static_cast<int>(std::lround(half + (wy - centerY) * scale));
    };

    // Terrain fill: north-up, east-right (UO +x=East, +y=South maps to +panelY).
    for (int oy = 0; oy < size_; ++oy) {
        for (int ox = 0; ox < size_; ++ox) {
            const i32 wx = static_cast<i32>(std::lround(centerX + (ox - half) * inv));
            const i32 wy = static_cast<i32>(std::lround(centerY + (oy - half) * inv));
            fb_[static_cast<usize>(oy) * size_ + ox] = CellColor(world, td, wx, wy);
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
