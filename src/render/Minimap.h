#pragma once

#include "uo/tiledata.h"
#include "uo/types.h"

#include <unordered_map>
#include <vector>

namespace uo::world { class World; }

namespace uo::render {

// Top-down orientation panel, drawn into its own ARGB1555 buffer and composited
// over the world frame by Renderer::Overlay. One world cell maps to a square of
// `scale` pixels; the view auto-scales so the player and the whole planned path
// fit inside the panel (never zooming in past kMinSpan tiles). Terrain colour is
// sampled from World::QueryCell + land flags and cached per 8x8 map block (MUL
// data is immutable, so a cell's colour never changes once computed).
class Minimap {
public:
    explicit Minimap(int size);

    // playerX/Y is the camera focus; (pathX,pathY) are the upcoming route cells
    // (first should be the player cell, last is the goal). nPath<=1 => no route
    // drawn. Re-rendering is skipped when nothing relevant changed.
    void Render(uo::world::World& world,
                const uo::tiledata::TileDataLoader& td,
                i32 playerX, i32 playerY,
                const i32* pathX, const i32* pathY, usize nPath);

    const u16* Frame() const { return fb_.data(); }
    int Size() const { return size_; }

private:
    u16  CellColor(uo::world::World& world,
                   const uo::tiledata::TileDataLoader& td, i32 x, i32 y);
    u16  ComputeColor(uo::world::World& world,
                      const uo::tiledata::TileDataLoader& td, i32 x, i32 y) const;
    void PutPixel(int x, int y, u16 color);
    void FillMarker(int cx, int cy, int r, u16 color);
    void DrawLine(int x0, int y0, int x1, int y1, u16 color);

    int size_;
    std::vector<u16> fb_;
    std::unordered_map<u64, std::vector<u16>> blockColors_;  // (bx<<32|by) -> 64 cells

    // Skip a redraw when the inputs are identical to the last frame.
    i32  lastPlayerX_ = 0x7FFFFFFF;
    i32  lastPlayerY_ = 0x7FFFFFFF;
    u64  lastPathSig_ = 0;
    bool rendered_    = false;
};

}
