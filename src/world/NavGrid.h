#pragma once

// ---------------------------------------------------------------------------
// NavGrid — the coarse walkability grid the whole-world router runs on.
//
// Tile-level A* is the right tool for the last few dozen tiles and the wrong
// tool for a 900-tile journey across Britannia: the node budget explodes and
// every bot would pay it again. So the world is summarised once, offline, into
// 16x16-tile cells that record "can a character stand somewhere in here, and
// where". Routing then happens over ~115k cells instead of ~29M tiles, and the
// proven M1.5 tile A* only ever runs between consecutive cell anchors.
//
// The grid is derived from the MULs, never hand-authored: `tools/atlasgen`
// samples every cell through the same `world::World::QueryCell` the bot walks
// with, so what the router believes is walkable is what the walker believes.
//
// It is immutable once built and contains no per-character state, so one copy
// is shared by every session in the process (M1.5 isolation rule: mutable
// state stays per-session, static topology may be shared).
//
// KNOWN DEBT -- interiors (M3.9 scope, unstarted). What works today, measured:
// the tile A* underneath is fully 3-D and interior-capable -- it climbs
// stairs (maxStepUp 12), opens doors (the door-macro retry ladder in
// Navigation.cpp), and treats the decorator's furniture and live mobiles as
// obstacles. RoutePlanner::Plan snaps an unsampled start/goal cell to the
// nearest passable one within 3 rings and hands anything within one leg
// budget (40 tiles) straight to that A*, so a ground-floor shop within a leg
// of sampled ground is already reachable end to end. What does NOT work:
//   1. BuildEdges runs offline against the raw MULs, where every door is
//      CLOSED, so cell-to-cell edges through buildings are recorded shut and
//      the macro router treats a large interior as an impassable pocket; a
//      goal deeper inside than one leg budget cannot be planned to, only
//      escaped from (EscapeCandidates).
//   2. A cell's single anchor is chosen with no notion of floors: a cell
//      containing a two-storey shop gets one anchor at one z, so a macro
//      route can end a leg on the wrong storey and the journey's floor test
//      (Journey::AtGoal, M3.9) then correctly refuses to call it arrived --
//      the trip fails cleanly instead of finishing, but it still fails.
// Closing the gap needs (a) door-aware BuildEdges -- treat kFlagDoor statics
// as passable during the offline sweep the way the walker does at runtime --
// and (b) per-floor anchors: a cell may hold one anchor per distinct standing
// z-band, with edges keyed by (cell, band) rather than cell. Both are offline
// generator changes plus a grid format bump; nothing in the runtime walker
// needs to change. Note mount_policy's Reason::DestinationIndoors is only a
// riding decision (do not ride into a bank), not a navigation answer, and
// must not be mistaken for one.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::tiledata { class TileDataLoader; }
namespace uo::map      { class Map; }
namespace uo::world    { class World; }

namespace uo::navgrid {

// Tiles per cell edge. 16 keeps a cell-to-cell leg short enough that the tile
// A* between two anchors is trivial, while still shrinking the search space by
// 256x.
constexpr u32 kCellTiles = 16;

// One summarised cell. 6 bytes, so the whole Britannia grid is ~690 KB.
#pragma pack(push, 1)
struct Cell {
    u8 anchorOffX = 0;   // 0..kCellTiles-1, relative to the cell's origin
    u8 anchorOffY = 0;
    i8 anchorZ    = 0;
    u8 flags      = 0;
    // Which of the eight neighbours a walker can actually REACH from this
    // cell's anchor, one bit per direction (the DirToDelta numbering:
    // 0=N, 1=NE, 2=E, ... 7=NW).
    //
    // This is the difference between a route that exists and a route that can
    // be walked. "Both cells hold standable ground" is not the same claim as
    // "you can get from one to the other": the far bank of a river, the far
    // side of a mountain and the inside of a locked building all satisfy the
    // first and fail the second. The first cross-region run failed exactly
    // there -- the macro route crossed a river the walker could not.
    u8 edges      = 0;
    u8 pad        = 0;
};
#pragma pack(pop)

constexpr u8 kCellPassable = 0x01;  // at least one sampled tile was standable
// Terrain summary, counted from the same sweep. These are what turn "where is
// there wood / water" into a derived answer instead of a hand-written list:
// forests are foliage-dense cells, coastline is a cell holding both standable
// ground and Wet land.
constexpr u8 kCellForest   = 0x02;  // most sampled tiles sat in/next to foliage
constexpr u8 kCellWater    = 0x04;  // at least a quarter of the cell is Wet land

class NavGrid {
public:
    NavGrid() = default;

    NavGrid(const NavGrid&) = delete;
    NavGrid& operator=(const NavGrid&) = delete;

    bool Ready() const { return !cells_.empty(); }
    u32  CellsX() const { return cellsX_; }
    u32  CellsY() const { return cellsY_; }
    u32  WidthTiles()  const { return cellsX_ * kCellTiles; }
    u32  HeightTiles() const { return cellsY_ * kCellTiles; }

    // Build by sampling the world. Slow (tens of seconds over all of
    // Britannia) and meant for the offline generator, not for a bot session.
    // `progress` is called with a 0..100 percentage, or may be null.
    bool Build(map::Map& worldMap, const world::World& world,
               const tiledata::TileDataLoader& tileData,
               void (*progress)(int, void*) = nullptr, void* user = nullptr);

    // Second pass: decide, for every adjacent pair of passable cells, whether
    // the walker can actually cross between their anchors. Runs the same tile
    // A* the bot walks with, bounded hard so an impossible crossing costs a
    // fixed amount rather than a full-map search.
    bool BuildEdges(world::World& world,
                    void (*progress)(int, void*) = nullptr,
                    void* user = nullptr);

    bool Save(const char* path) const;
    bool Load(const char* path);

    // Adopt an already-summarised grid. Load() and the tests both arrive here,
    // which is deliberate: the routing tests run on a synthetic world so they
    // need neither the MULs nor a 15-second sweep to be deterministic.
    bool Adopt(u32 cellsX, u32 cellsY, const Cell* cells);

    bool InRange(i32 cx, i32 cy) const {
        return cx >= 0 && cy >= 0 &&
               static_cast<u32>(cx) < cellsX_ && static_cast<u32>(cy) < cellsY_;
    }
    const Cell* At(i32 cx, i32 cy) const {
        if (!InRange(cx, cy)) return nullptr;
        return &cells_[static_cast<usize>(cy) * cellsX_ + cx];
    }
    bool Passable(i32 cx, i32 cy) const {
        const Cell* c = At(cx, cy);
        return c && (c->flags & kCellPassable);
    }
    // True when a walker can get from this cell's anchor to the neighbour in
    // `dir`. Routing must ask this, not Passable(), or it will happily plan
    // across water.
    bool EdgeOpen(i32 cx, i32 cy, u8 dir) const {
        const Cell* c = At(cx, cy);
        return c && (c->edges & static_cast<u8>(1u << (dir & 7)));
    }

    static i32 TileToCell(i32 tile) { return tile / static_cast<i32>(kCellTiles); }

    // The representative standable tile of a cell. False when the cell holds
    // none (open sea, solid rock, off the map).
    bool Anchor(i32 cx, i32 cy, i32* x, i32* y, i8* z) const;

    // Nearest passable cell to (cx, cy) within `maxRings` rings, for snapping
    // a start or goal that landed in an unsampled pocket (a shop interior can
    // easily have no standable *sample* even though the bot is standing in it).
    bool NearestPassable(i32 cx, i32 cy, i32 maxRings,
                         i32* outCx, i32* outCy) const;

    usize PassableCells() const;

private:
    u32 cellsX_ = 0;
    u32 cellsY_ = 0;
    std::vector<Cell> cells_;
};

} // namespace uo::navgrid
