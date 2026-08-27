#pragma once

#include "render/RadarColors.h"
#include "uo/map.h"
#include "uo/types.h"

#include <unordered_map>
#include <vector>

namespace uo::render {

// Isometric orientation panel, drawn into its own ARGB1555 buffer and
// composited over the world frame by Renderer::Overlay. Uses the same iso
// projection as the world window (su = x-y right, sv = x+y down), so north
// points to the top-right corner — matching the real client's radar gump
// (CRadarGump_RenderMinimap @0x48E5B0: East->down-right, South->down-left).
//
// Cell colour follows the real client's radar (CRadarGump_Update @0x48DCC0):
// the topmost surface wins — start from the land tile, then any static whose z
// is >= the running top overrides it, coloured via radarcol.mul. Colours are
// cached per 8x8 map block (MUL data is immutable). The view auto-scales so the
// player and the whole planned path fit (never zooming in past kMinSpan).
class Minimap {
public:
    explicit Minimap(int size);

    // playerX/Y is the camera focus; (pathX,pathY) are the upcoming route cells
    // (first should be the player cell, last is the goal). nPath<=1 => no route
    // drawn. Re-rendering is skipped when nothing relevant changed.
    void Render(uo::map::Map& map, const RadarColors& radar,
                i32 playerX, i32 playerY,
                const i32* pathX, const i32* pathY, usize nPath);

    const u16* Frame() const { return fb_.data(); }
    int Size() const { return size_; }

private:
    u16  CellColor(uo::map::Map& map, const RadarColors& radar, i32 x, i32 y);
    void FillBlock(uo::map::Map& map, const RadarColors& radar,
                   u32 bx, u32 by, std::vector<u16>& cols);
    void PutPixel(int x, int y, u16 color);
    void FillMarker(int cx, int cy, int r, u16 color);
    void DrawLine(int x0, int y0, int x1, int y1, u16 color);

    int size_;
    std::vector<u16> fb_;
    std::unordered_map<u64, std::vector<u16>> blockColors_;  // (bx<<32|by) -> 64 cells
    std::vector<uo::map::StaticItem> staticsBuf_;            // ReadStatics scratch

    // Skip a redraw when the inputs are identical to the last frame.
    i32  lastPlayerX_ = 0x7FFFFFFF;
    i32  lastPlayerY_ = 0x7FFFFFFF;
    u64  lastPathSig_ = 0;
    bool rendered_    = false;
};

}
