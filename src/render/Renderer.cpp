#include "render/Renderer.h"

#include <algorithm>

namespace uo::render {

namespace {

// Isometric tile geometry. A cell's diamond is 44 wide, 44 tall, stepping 22px
// per cell on each screen axis; z raises by 4px per unit.
constexpr int kHalfTile = 22;
constexpr int kTile     = 44;
constexpr int kZStep    = 4;

constexpr u16 kBackground = 0;   // black

struct Draw {
    i32 key;            // x + y (paint far-to-near)
    i32 z;              // height (then within a cell)
    int isStatic;       // land (0) before statics (1) in the same cell
    int dx, dy;         // top-left blit position
    const art::Sprite* sp;
};

}  // namespace

Renderer::Renderer(int width, int height)
    : w_(width), h_(height), fb_(static_cast<usize>(width) * height, kBackground) {}

void Renderer::RenderWorld(map::Map& map, art::ArtLoader& art, i32 camX, i32 camY) {
    std::fill(fb_.begin(), fb_.end(), kBackground);

    // Cells whose screen position can land on-screen. The margin pads for tall
    // statics anchored below the window and for elevation offsets.
    const int R = (w_ + h_) / kTile + 48;

    const i32 wc = static_cast<i32>(map.WidthCells());
    const i32 hc = static_cast<i32>(map.HeightCells());
    const i32 minX = std::max<i32>(0, camX - R), maxX = std::min<i32>(wc - 1, camX + R);
    const i32 minY = std::max<i32>(0, camY - R), maxY = std::min<i32>(hc - 1, camY + R);
    if (minX > maxX || minY > maxY) return;

    const u32 bx0 = static_cast<u32>(minX) / 8, bx1 = static_cast<u32>(maxX) / 8;
    const u32 by0 = static_cast<u32>(minY) / 8, by1 = static_cast<u32>(maxY) / 8;

    const int originX = w_ / 2;
    const int originY = h_ / 2;

    std::vector<Draw> draws;
    std::vector<map::StaticItem> statics(2048);

    auto screenY = [&](i32 dxw, i32 dyw, i32 z) {
        return originY + (dxw + dyw) * kHalfTile - z * kZStep;
    };
    auto screenX = [&](i32 dxw, i32 dyw) {
        return originX + (dxw - dyw) * kHalfTile;
    };

    for (u32 by = by0; by <= by1; ++by) {
        for (u32 bx = bx0; bx <= bx1; ++bx) {
            // Land
            map::MapBlock mb;
            if (map.ReadBlock(bx, by, &mb)) {
                for (int cy = 0; cy < 8; ++cy) {
                    for (int cx = 0; cx < 8; ++cx) {
                        const i32 wx = static_cast<i32>(bx) * 8 + cx;
                        const i32 wy = static_cast<i32>(by) * 8 + cy;
                        const map::LandCell& c = mb.cells[cy * 8 + cx];
                        const art::Sprite* sp = art.Land(c.tileId);
                        if (!sp) continue;
                        const i32 dxw = wx - camX, dyw = wy - camY;
                        const int sx = screenX(dxw, dyw);
                        const int sy = screenY(dxw, dyw, c.z);
                        draws.push_back({wx + wy, c.z, 0, sx - kHalfTile, sy, sp});
                    }
                }
            }
            // Statics
            u32 n = 0;
            if (map.ReadStatics(bx, by, statics.data(),
                                static_cast<u32>(statics.size()), &n)) {
                for (u32 i = 0; i < n; ++i) {
                    const map::StaticItem& s = statics[i];
                    const i32 wx = static_cast<i32>(bx) * 8 + s.cellX;
                    const i32 wy = static_cast<i32>(by) * 8 + s.cellY;
                    const art::Sprite* sp = art.Static(s.itemId);
                    if (!sp) continue;
                    const i32 dxw = wx - camX, dyw = wy - camY;
                    const int sx = screenX(dxw, dyw);
                    const int sy = screenY(dxw, dyw, s.z);
                    const int dx = sx - sp->width / 2;
                    const int dy = sy + kTile - sp->height;   // bottom-anchored
                    draws.push_back({wx + wy, s.z, 1, dx, dy, sp});
                }
            }
        }
    }

    std::sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) {
        if (a.key != b.key) return a.key < b.key;
        if (a.z   != b.z)   return a.z   < b.z;
        return a.isStatic < b.isStatic;
    });

    for (const Draw& d : draws) Blit(*d.sp, d.dx, d.dy);
}

void Renderer::Blit(const art::Sprite& s, int dx, int dy) {
    if (dx >= w_ || dy >= h_ || dx + s.width <= 0 || dy + s.height <= 0) return;

    for (int row = 0; row < s.height; ++row) {
        const int py = dy + row;
        if (py < 0 || py >= h_) continue;
        const u16* srow = &s.px[static_cast<usize>(row) * s.width];
        u16* drow = &fb_[static_cast<usize>(py) * w_];
        for (int col = 0; col < s.width; ++col) {
            const u16 p = srow[col];
            if (!p) continue;
            const int px = dx + col;
            if (px < 0 || px >= w_) continue;
            drow[px] = p;
        }
    }
}

}
