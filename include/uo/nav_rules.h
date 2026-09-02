#pragma once

// ---------------------------------------------------------------------------
// Pure navigation-geometry helpers, extracted so they can be unit-tested
// against a synthetic grid instead of the real MULs (contrast with
// tests/path_probe.cpp, which is a diagnostic against real client data, not a
// ctest). No server, no socket, no World: callers supply a walkability oracle.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::nav {

// A literal goal is very often a resource's own tile -- a tree trunk, an
// open-water cast target, a rock face -- rather than somewhere a character
// can stand (Navigation.cpp's "goal not walkable" case, wave 2, 2026-09-01:
// Dorvar/Halain/Titus each re-issued a dead literal target dozens of times).
// Searches outward from (goalX,goalY) in expanding Chebyshev rings (nearest
// ring first, so the salvaged goal lands beside the original rather than
// drifting toward whichever direction the ring happens to scan first) and
// returns the first walkable tile `walkable` reports.
//
// WalkFn signature: bool(i32 x, i32 y, i8 fromZ, i8* outStandZ) -- returns
// whether (x,y) is walkable from fromZ, and if so writes the standing z.
template <typename WalkFn>
bool FindWalkableNearGoal(i32 goalX, i32 goalY, i8 nearZ, i32 maxRadius,
                          WalkFn&& walkable, i32* outX, i32* outY,
                          i8* outZ) {
    if (!outX || !outY || !outZ) return false;
    if (maxRadius < 1) maxRadius = 1;
    for (i32 r = 1; r <= maxRadius; ++r) {
        for (i32 dy = -r; dy <= r; ++dy) {
            for (i32 dx = -r; dx <= r; ++dx) {
                const i32 adx = dx < 0 ? -dx : dx;
                const i32 ady = dy < 0 ? -dy : dy;
                if (adx != r && ady != r)
                    continue;  // ring only (Chebyshev distance == r), not the
                               // filled square already covered by smaller r
                const i32 tx = goalX + dx, ty = goalY + dy;
                if (tx < 0 || ty < 0) continue;
                i8 standZ = 0;
                if (!walkable(tx, ty, nearZ, &standZ)) continue;
                *outX = tx;
                *outY = ty;
                *outZ = standZ;
                return true;
            }
        }
    }
    return false;
}

}  // namespace uo::nav
