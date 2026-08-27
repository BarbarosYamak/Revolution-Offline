#include "world/NavGrid.h"

#include "bot/Pathfinding.h"
#include "uo/map.h"
#include "uo/tiledata.h"
#include "uo/world.h"

#include <vector>

#include <cstdio>
#include <cstring>

namespace uo::navgrid {

namespace {

// Sample every other tile. 8x8 = 64 probes per cell is enough to find a
// standing spot in anything wider than a corridor, and cuts build time in
// half against a full 16x16 sweep.
constexpr u32 kSampleStride = 2;

// Bumped from ...D1 when the cell grew its edge mask: an older file read as a
// newer one would silently produce a grid with no edges at all, i.e. a world
// with no routes.
constexpr char kMagic[8] = { 'U', 'O', 'N', 'G', 'R', 'I', 'D', '2' };

struct FileHeader {
    char magic[8];
    u32  cellsX;
    u32  cellsY;
    u32  cellTiles;
    u32  reserved;
};

} // namespace

bool NavGrid::Build(map::Map& worldMap, const world::World& world,
                    const tiledata::TileDataLoader& tileData,
                    void (*progress)(int, void*), void* user) {
    if (!worldMap.IsOpen()) return false;

    cellsX_ = worldMap.WidthCells()  / kCellTiles;
    cellsY_ = worldMap.HeightCells() / kCellTiles;
    if (cellsX_ == 0 || cellsY_ == 0) return false;

    cells_.assign(static_cast<usize>(cellsX_) * cellsY_, Cell{});

    int lastPct = -1;
    for (u32 cy = 0; cy < cellsY_; ++cy) {
        for (u32 cx = 0; cx < cellsX_; ++cx) {
            const i32 originX = static_cast<i32>(cx * kCellTiles);
            const i32 originY = static_cast<i32>(cy * kCellTiles);

            // Prefer a standing spot near the middle of the cell: it is the
            // one most likely to be reachable from the neighbouring anchors,
            // and it keeps generated waypoints off the walls.
            const i32 midX = static_cast<i32>(kCellTiles) / 2;
            const i32 midY = static_cast<i32>(kCellTiles) / 2;
            i32 bestOffX = -1, bestOffY = -1;
            i8  bestZ = 0;
            i32 bestScore = 0;
            u32 samples = 0, foliage = 0, wet = 0;

            for (u32 oy = 0; oy < kCellTiles; oy += kSampleStride) {
                for (u32 ox = 0; ox < kCellTiles; ox += kSampleStride) {
                    const i32 tx = originX + static_cast<i32>(ox);
                    const i32 ty = originY + static_cast<i32>(oy);

                    map::LandCell land{};
                    if (!worldMap.ReadCell(static_cast<u32>(tx),
                                           static_cast<u32>(ty), &land))
                        continue;

                    world::WalkQuery q{};
                    q.x = static_cast<u32>(tx);
                    q.y = static_cast<u32>(ty);
                    q.fromZ = land.z;
                    const world::WalkResult wr = world.QueryCell(q);

                    ++samples;
                    if (wr.nearFoliage) ++foliage;
                    if (tileData.Land(wr.landTileId).flags &
                        tiledata::kFlagWet)
                        ++wet;

                    if (!wr.walkable) continue;

                    // Manhattan closeness to the cell centre, higher is better.
                    const i32 dx = (static_cast<i32>(ox) - midX);
                    const i32 dy = (static_cast<i32>(oy) - midY);
                    const i32 score = 64 - (dx < 0 ? -dx : dx) - (dy < 0 ? -dy : dy);
                    if (bestOffX < 0 || score > bestScore) {
                        bestScore = score;
                        bestOffX  = static_cast<i32>(ox);
                        bestOffY  = static_cast<i32>(oy);
                        bestZ     = wr.standZ;
                    }
                }
            }

            Cell& c = cells_[static_cast<usize>(cy) * cellsX_ + cx];
            if (bestOffX >= 0) {
                c.anchorOffX = static_cast<u8>(bestOffX);
                c.anchorOffY = static_cast<u8>(bestOffY);
                c.anchorZ    = bestZ;
                c.flags      = kCellPassable;
            }
            if (samples) {
                // A quarter of the cell touching foliage already reads as
                // woods on screen. Demanding a majority only finds the
                // southern jungle, where the canopy is continuous, and misses
                // every temperate forest -- Yew's oaks included.
                if (foliage * 4 > samples) c.flags |= kCellForest;
                if (wet * 4 >= samples)    c.flags |= kCellWater;
            }
        }

        if (progress) {
            const int pct = static_cast<int>((cy + 1) * 100 / cellsY_);
            if (pct != lastPct) {
                lastPct = pct;
                progress(pct, user);
            }
        }
    }
    return true;
}

bool NavGrid::BuildEdges(world::World& world,
                         void (*progress)(int, void*), void* user) {
    if (cells_.empty()) return false;

    // Only the four "forward" directions are tested; each result also sets the
    // mirror bit on the neighbour. Crossability is treated as symmetric, which
    // is true for everything except a one-way drop, and a coarse router that
    // occasionally proposes a leg the walker then refuses is recoverable --
    // one that never proposes a legal leg is not.
    const u8 kDirs[4] = { 2, 3, 4, 5 };   // E, SE, S, SW
    const i32 kDelta[8][2] = {
        {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    };

    bot::PathOptions opt;
    // A crossing between two neighbouring cell anchors is at most ~22 tiles.
    // Anything that needs more than a few hundred nodes is going the long way
    // round, which is not a crossing -- it is the router's job to find that.
    opt.maxNodesExpanded = 600;

    int lastPct = -1;
    for (u32 cy = 0; cy < cellsY_; ++cy) {
        for (u32 cx = 0; cx < cellsX_; ++cx) {
            i32 ax, ay;
            i8  az;
            if (!Anchor(static_cast<i32>(cx), static_cast<i32>(cy), &ax, &ay, &az))
                continue;

            for (u8 dir : kDirs) {
                const i32 nx = static_cast<i32>(cx) + kDelta[dir][0];
                const i32 ny = static_cast<i32>(cy) + kDelta[dir][1];
                i32 bx, by;
                i8  bz;
                if (!Anchor(nx, ny, &bx, &by, &bz)) continue;

                const std::vector<u8> path =
                    bot::FindPath(world, ax, ay, az, bx, by, opt);
                if (path.empty()) continue;

                Cell& a = cells_[static_cast<usize>(cy) * cellsX_ + cx];
                Cell& b = cells_[static_cast<usize>(ny) * cellsX_ +
                                 static_cast<usize>(nx)];
                a.edges |= static_cast<u8>(1u << dir);
                b.edges |= static_cast<u8>(1u << ((dir + 4) & 7));
            }
        }
        if (progress) {
            const int pct = static_cast<int>((cy + 1) * 100 / cellsY_);
            if (pct != lastPct) {
                lastPct = pct;
                progress(pct, user);
            }
        }
    }
    return true;
}

bool NavGrid::Save(const char* path) const {
    if (cells_.empty()) return false;
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    FileHeader h{};
    std::memcpy(h.magic, kMagic, sizeof(kMagic));
    h.cellsX = cellsX_;
    h.cellsY = cellsY_;
    h.cellTiles = kCellTiles;
    h.reserved = 0;

    bool ok = std::fwrite(&h, sizeof(h), 1, f) == 1;
    if (ok)
        ok = std::fwrite(cells_.data(), sizeof(Cell), cells_.size(), f) ==
             cells_.size();
    std::fclose(f);
    return ok;
}

bool NavGrid::Load(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    FileHeader h{};
    if (std::fread(&h, sizeof(h), 1, f) != 1) { std::fclose(f); return false; }
    if (std::memcmp(h.magic, kMagic, sizeof(kMagic)) != 0 ||
        h.cellTiles != kCellTiles || h.cellsX == 0 || h.cellsY == 0) {
        std::fclose(f);
        return false;
    }

    const usize count = static_cast<usize>(h.cellsX) * h.cellsY;
    cells_.assign(count, Cell{});
    const bool ok = std::fread(cells_.data(), sizeof(Cell), count, f) == count;
    std::fclose(f);
    if (!ok) { cells_.clear(); return false; }

    cellsX_ = h.cellsX;
    cellsY_ = h.cellsY;
    return true;
}

bool NavGrid::Adopt(u32 cellsX, u32 cellsY, const Cell* cells) {
    if (!cellsX || !cellsY || !cells) return false;
    const usize count = static_cast<usize>(cellsX) * cellsY;
    cells_.assign(cells, cells + count);
    cellsX_ = cellsX;
    cellsY_ = cellsY;
    return true;
}

bool NavGrid::Anchor(i32 cx, i32 cy, i32* x, i32* y, i8* z) const {
    const Cell* c = At(cx, cy);
    if (!c || !(c->flags & kCellPassable)) return false;
    if (x) *x = cx * static_cast<i32>(kCellTiles) + c->anchorOffX;
    if (y) *y = cy * static_cast<i32>(kCellTiles) + c->anchorOffY;
    if (z) *z = c->anchorZ;
    return true;
}

bool NavGrid::NearestPassable(i32 cx, i32 cy, i32 maxRings,
                              i32* outCx, i32* outCy) const {
    if (Passable(cx, cy)) {
        if (outCx) *outCx = cx;
        if (outCy) *outCy = cy;
        return true;
    }
    for (i32 r = 1; r <= maxRings; ++r) {
        for (i32 dy = -r; dy <= r; ++dy) {
            for (i32 dx = -r; dx <= r; ++dx) {
                // Ring only: skip the interior we already searched.
                const i32 adx = dx < 0 ? -dx : dx;
                const i32 ady = dy < 0 ? -dy : dy;
                if (adx != r && ady != r) continue;
                if (Passable(cx + dx, cy + dy)) {
                    if (outCx) *outCx = cx + dx;
                    if (outCy) *outCy = cy + dy;
                    return true;
                }
            }
        }
    }
    return false;
}

usize NavGrid::PassableCells() const {
    usize n = 0;
    for (const Cell& c : cells_)
        if (c.flags & kCellPassable) ++n;
    return n;
}

} // namespace uo::navgrid
