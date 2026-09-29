#include "world/MiningAdvance.h"

#include <cstdlib>

namespace uo::world_atlas {

bool MiningInteriorPoint(const wm::Region& region, u32 lane, i32* outX,
                        i32* outY) {
    if (!outX || !outY || region.rects.empty()) return false;

    // The largest RECT is the actual working floor.  Small companion RECTs
    // in a cave AREADEF are normally doors or the narrow mouth connecting it
    // to the world, so never nominate one as a worker's destination.
    const wm::Rect* floor = &region.rects[0];
    i64 largestArea = -1;
    for (const wm::Rect& rect : region.rects) {
        const i64 width = static_cast<i64>(rect.x2) - rect.x1 + 1;
        const i64 height = static_cast<i64>(rect.y2) - rect.y1 + 1;
        const i64 area = width * height;
        if (area > largestArea) {
            largestArea = area;
            floor = &rect;
        }
    }

    const i32 width = floor->x2 - floor->x1 + 1;
    const i32 height = floor->y2 - floor->y1 + 1;
    // Preserve a two-tile wall buffer on normal-size cave floors.  Tiny
    // caves retain their centre point instead of being made impossible by an
    // over-eager inset.
    const i32 insetX = width >= 7 ? 2 : 0;
    const i32 insetY = height >= 7 ? 2 : 0;
    const i32 minX = floor->x1 + insetX, maxX = floor->x2 - insetX;
    const i32 minY = floor->y1 + insetY, maxY = floor->y2 - insetY;
    const i32 usableWidth = maxX - minX + 1;
    const i32 usableHeight = maxY - minY + 1;
    const i32 columns = usableWidth < 5 ? usableWidth : 5;
    const i32 rows = usableHeight < 4 ? usableHeight : 4;
    if (columns < 1 || rows < 1) return false;

    const u32 slot = lane % static_cast<u32>(columns * rows);
    const i32 col = static_cast<i32>(slot % static_cast<u32>(columns));
    const i32 row = static_cast<i32>(slot / static_cast<u32>(columns));
    // Divide the usable span into equal, interior gaps.  With Minoc Mine 1
    // this produces five columns by four rows (twenty stands) well north of
    // the south-mouth connector, enough to keep the current miner cohort
    // from converging on one target.
    *outX = minX + ((maxX - minX) * (col + 1)) / (columns + 1);
    *outY = minY + ((maxY - minY) * (row + 1)) / (rows + 1);
    return true;
}

bool DeeperMiningPoint(const wm::Region& region, i32 curX, i32 curY,
                       i32 stepLimit, i32* outX, i32* outY) {
    if (!outX || !outY) return false;
    if (region.rects.empty()) return false;
    if (stepLimit < 1) stepLimit = 1;

    // The FIXED deep point: the RECT corner farthest (Chebyshev) from the
    // region's own recorded entrance (region.center). Chebyshev distance
    // from a fixed point, over an axis-aligned rectangle, is maximised at a
    // corner -- never along an edge or in the interior -- so checking the
    // four corners of every RECT against region.center is exhaustive, not a
    // heuristic.
    const i32 ex = region.center.x, ey = region.center.y;
    i64 bestD = -1;
    i32 deepX = ex, deepY = ey;
    for (const wm::Rect& rect : region.rects) {
        const i32 cxs[2] = {rect.x1, rect.x2};
        const i32 cys[2] = {rect.y1, rect.y2};
        for (i32 cx : cxs) {
            for (i32 cy : cys) {
                const i32 dx = cx > ex ? cx - ex : ex - cx;
                const i32 dy = cy > ey ? cy - ey : ey - cy;
                const i32 d = dx > dy ? dx : dy;
                if (d > bestD) {
                    bestD = d;
                    deepX = cx;
                    deepY = cy;
                }
            }
        }
    }
    // No corner sits any further from the entrance than the entrance itself
    // -- the region has no meaningful depth to advance into.
    if (bestD <= 0) return false;

    // Step from wherever the caller actually stands toward that FIXED deep
    // point -- same target every call, so repeated advances converge on one
    // interior spot instead of chasing "farthest from here" and bouncing
    // between corners as the caller's own position moves.
    const i32 dx = deepX - curX, dy = deepY - curY;
    const i32 adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    const i32 dist = adx > ady ? adx : ady;
    if (dist == 0) return false;  // already at the deep point

    const i32 step = dist < stepLimit ? dist : stepLimit;
    *outX = curX + (dx * step) / dist;
    *outY = curY + (dy * step) / dist;
    return true;
}

}  // namespace uo::world_atlas
