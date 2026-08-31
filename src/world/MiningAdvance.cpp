#include "world/MiningAdvance.h"

#include <cstdlib>

namespace uo::world_atlas {

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
