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

// One draw command — land or static. Land and statics share one list, ordered
// exactly like the client (World_RenderEntities @0x401E90): cells back-to-front
// (depth, then column), and WITHIN a cell by z, land before static on a z-tie.
// A stable sort keeps equal-z statics in statics0.mul order (trunk under crown).
struct Draw {
    i32 depth;          // x + y  — far-to-near tile depth
    i32 col;            // x - y  — orders cells on the same diagonal
    i32 z;              // sort z within a cell
    int order;          // z-tie: land (0) under static (1)
    bool quad;          // true: stretched/texmap quad; false: flat blit
    const u16* src;     // source pixels (art sprite or texmap)
    int sw, sh;         // source dimensions
    bool transparent;   // skip 0 texels (art) vs draw all (texmap)
    int dx, dy;                   // flat-blit top-left (when !quad)
    Renderer::TexVert N, E, S, W; // quad corners (when quad)
};

}  // namespace

Renderer::Renderer(int width, int height)
    : w_(width), h_(height), fb_(static_cast<usize>(width) * height, kBackground) {}

void Renderer::RenderWorld(map::Map& map, art::ArtLoader& art,
                           const tiledata::TileDataLoader& td, texmap::TexmapLoader& tex,
                           i32 camX, i32 camY) {
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

    // Vertical reach of z offsets, for the per-tile cull (z is a signed byte).
    const int kZPad = 128 * kZStep;

    // Per-frame heightmap over the visible range + a 2-cell margin (the
    // land-stretch model needs the +1 corner neighbors, and the official's
    // flat-vs-texmap test scans a small neighborhood). One block read per
    // covering block; lookups are then O(1).
    const i32 gx0 = std::max<i32>(0, minX - 2), gx1 = std::min<i32>(wc - 1, maxX + 2);
    const i32 gy0 = std::max<i32>(0, minY - 2), gy1 = std::min<i32>(hc - 1, maxY + 2);
    const int gw = gx1 - gx0 + 1, gh = gy1 - gy0 + 1;
    std::vector<i8> zmap(static_cast<usize>(gw) * gh, 0);
    {
        map::MapBlock zb;
        for (u32 by = static_cast<u32>(gy0)/8; by <= static_cast<u32>(gy1)/8; ++by) {
            for (u32 bx = static_cast<u32>(gx0)/8; bx <= static_cast<u32>(gx1)/8; ++bx) {
                if (!map.ReadBlock(bx, by, &zb)) continue;
                for (int cy = 0; cy < 8; ++cy) {
                    for (int cx = 0; cx < 8; ++cx) {
                        const i32 wx = static_cast<i32>(bx)*8 + cx;
                        const i32 wy = static_cast<i32>(by)*8 + cy;
                        if (wx < gx0 || wx > gx1 || wy < gy0 || wy > gy1) continue;
                        zmap[static_cast<usize>(wy - gy0) * gw + (wx - gx0)] = zb.cells[cy*8 + cx].z;
                    }
                }
            }
        }
    }
    auto zAt = [&](i32 x, i32 y) -> int {
        if (x < gx0 || x > gx1 || y < gy0 || y > gy1) return 0;
        return zmap[static_cast<usize>(y - gy0) * gw + (x - gx0)];
    };

    for (u32 by = by0; by <= by1; ++by) {
        for (u32 bx = bx0; bx <= bx1; ++bx) {
            // --- Land (drawn immediately; always under statics) ------------
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
                        const int sx = originX + (dxw - dyw) * kHalfTile;
                        const int sy = originY + (dxw + dyw) * kHalfTile;
                        if (sx + kHalfTile < 0 || sx - kHalfTile >= w_) continue;
                        if (sy + kTile + kZPad < 0 || sy - kZPad >= h_) continue;

                        // Four corner z's: own cell (north), +x (east),
                        // +x+y (south), +y (west) — the UO land-stretch model.
                        const int z0 = c.z;             // N
                        const int z1 = zAt(wx + 1, wy);     // E
                        const int z2 = zAt(wx + 1, wy + 1); // S
                        const int z3 = zAt(wx,     wy + 1); // W

                        // Faithful to the client's land factory (sub_4B74E0 @0x4B74E0):
                        //   textureId == 0                         -> 44x44 art
                        //   textured, corners equal AND neighborhood
                        //     uniform in z                          -> 44x44 art
                        //   otherwise                               -> stretched texmap
                        // The neighborhood test is why a flat water tile *beside*
                        // the sloped shore is texmapped (wavy) — smoothing the coast.
                        const texmap::Texture* t =
                            tex.Get(td.Land(c.tileId).textureId);
                        bool useArt = (z0 == z1 && z1 == z2 && z2 == z3);
                        if (useArt && t) {
                            for (i32 ny = wy - 1; ny <= wy + 2 && useArt; ++ny)
                                for (i32 nx = wx - 1; nx <= wx + 2 && useArt; ++nx)
                                    if (zAt(nx, ny) != z0) useArt = false;
                        }
                        const i32 depth = wx + wy, col = wx - wy;
                        Draw d{};
                        d.depth = depth; d.col = col; d.z = z0; d.order = 0;
                        if (!t || useArt) {
                            if (z0 == z1 && z1 == z2 && z2 == z3) {
                                d.quad = false;
                                d.src = sp->px.data(); d.sw = sp->width; d.sh = sp->height;
                                d.transparent = true;
                                d.dx = sx - kHalfTile; d.dy = sy - z0 * kZStep;
                            } else {
                                // Textureless sloped tile: stretch the art so it
                                // still meets its neighbors (avoids a crack).
                                d.quad = true;
                                d.src = sp->px.data(); d.sw = sp->width; d.sh = sp->height;
                                d.transparent = true;
                                d.N = {sx,             sy            - z0 * kZStep, 22,  0};
                                d.E = {sx + kHalfTile, sy + kHalfTile - z1 * kZStep, 43, 22};
                                d.S = {sx,             sy + kTile     - z2 * kZStep, 22, 43};
                                d.W = {sx - kHalfTile, sy + kHalfTile - z3 * kZStep,  0, 22};
                            }
                        } else {
                            const int hi = t->size - 1;
                            d.quad = true;
                            d.src = t->px.data(); d.sw = t->size; d.sh = t->size;
                            d.transparent = false;
                            d.N = {sx,             sy            - z0 * kZStep, 0,  0 };
                            d.E = {sx + kHalfTile, sy + kHalfTile - z1 * kZStep, hi, 0 };
                            d.S = {sx,             sy + kTile     - z2 * kZStep, hi, hi};
                            d.W = {sx - kHalfTile, sy + kHalfTile - z3 * kZStep, 0,  hi};
                        }
                        draws.push_back(d);
                    }
                }
            }

            // --- Statics (deferred to the painter's pass) ------------------
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
                    const int sx = originX + (dxw - dyw) * kHalfTile;
                    const int sy = originY + (dxw + dyw) * kHalfTile - s.z * kZStep;
                    Draw d{};
                    d.depth = wx + wy; d.col = wx - wy; d.z = s.z; d.order = 1;
                    d.quad = false;
                    d.src = sp->px.data(); d.sw = sp->width; d.sh = sp->height;
                    d.transparent = true;
                    d.dx = sx - sp->width / 2;
                    d.dy = sy + kTile - sp->height;   // bottom-anchored
                    draws.push_back(d);
                }
            }
        }
    }

    // Order like the client (World_RenderEntities @0x401E90): cells back-to-front
    // by depth then column; within a cell by z, land before static on a z-tie.
    // Stable so equal-z statics keep statics0.mul order (trunk under crown).
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) {
        if (a.depth != b.depth) return a.depth < b.depth;
        if (a.col   != b.col)   return a.col   < b.col;
        if (a.z     != b.z)     return a.z     < b.z;
        return a.order < b.order;
    });

    for (const Draw& d : draws) {
        if (d.quad) {
            TexTri(d.src, d.sw, d.sh, d.transparent, d.N, d.E, d.S);
            TexTri(d.src, d.sw, d.sh, d.transparent, d.N, d.S, d.W);
        } else {
            BlitRaw(d.src, d.sw, d.sh, d.dx, d.dy, d.transparent);
        }
    }
}

void Renderer::Blit(const art::Sprite& s, int dx, int dy) {
    BlitRaw(s.px.data(), s.width, s.height, dx, dy, true);
}

void Renderer::BlitRaw(const u16* src, int sw, int sh, int dx, int dy, bool skipTransparent) {
    if (dx >= w_ || dy >= h_ || dx + sw <= 0 || dy + sh <= 0) return;

    for (int row = 0; row < sh; ++row) {
        const int py = dy + row;
        if (py < 0 || py >= h_) continue;
        const u16* srow = &src[static_cast<usize>(row) * sw];
        u16* drow = &fb_[static_cast<usize>(py) * w_];
        for (int col = 0; col < sw; ++col) {
            const u16 p = srow[col];
            if (!p && skipTransparent) continue;
            const int px = dx + col;
            if (px < 0 || px >= w_) continue;
            drow[px] = p;
        }
    }
}

void Renderer::TexTri(const u16* src, int texW, int texH, bool skipTransparent,
                      TexVert a, TexVert b, TexVert c) {
    const int area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area == 0) return;

    int minx = std::max(0,      std::min(a.x, std::min(b.x, c.x)));
    int maxx = std::min(w_ - 1, std::max(a.x, std::max(b.x, c.x)));
    int miny = std::max(0,      std::min(a.y, std::min(b.y, c.y)));
    int maxy = std::min(h_ - 1, std::max(a.y, std::max(b.y, c.y)));
    if (minx > maxx || miny > maxy) return;

    const float inv = 1.0f / static_cast<float>(area);
    const int maxU = texW - 1, maxV = texH - 1;

    for (int py = miny; py <= maxy; ++py) {
        u16* drow = &fb_[static_cast<usize>(py) * w_];
        for (int px = minx; px <= maxx; ++px) {
            // Edge functions: w0 opposite a (edge b->c), etc.
            const int w0 = (c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x);
            const int w1 = (a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x);
            const int w2 = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
            if (area > 0) { if (w0 < 0 || w1 < 0 || w2 < 0) continue; }
            else          { if (w0 > 0 || w1 > 0 || w2 > 0) continue; }

            int u = static_cast<int>((w0 * a.u + w1 * b.u + w2 * c.u) * inv + 0.5f);
            int v = static_cast<int>((w0 * a.v + w1 * b.v + w2 * c.v) * inv + 0.5f);
            u = u < 0 ? 0 : (u > maxU ? maxU : u);
            v = v < 0 ? 0 : (v > maxV ? maxV : v);

            const u16 p = src[static_cast<usize>(v) * texW + u];
            if (p || !skipTransparent) drow[px] = p;
        }
    }
}

}
