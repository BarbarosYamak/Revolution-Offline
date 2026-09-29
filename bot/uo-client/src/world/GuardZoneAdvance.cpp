#include "world/GuardZoneAdvance.h"

namespace uo::world_atlas {

namespace {
// A little past the edge, not exactly on it, so the stepped point reads as
// OUTSIDE the RECT to a caller checking Rect::Contains -- landing precisely
// on the rim is still "inside" by inclusive bounds.
constexpr i32 kExitMargin = 3;
}  // namespace

bool StepOutOfGuardedRegion(const wm::Region& region, i32 curX, i32 curY,
                            i32 stepLimit, i32* outX, i32* outY) {
    if (!outX || !outY) return false;
    if (stepLimit < 1) stepLimit = 1;

    // Which of the region's (possibly several, disjoint) RECTs holds the
    // character right now -- a_cave_1-style regions carry more than one.
    const wm::Rect* here = nullptr;
    for (const wm::Rect& rect : region.rects) {
        if (rect.Contains(curX, curY)) { here = &rect; break; }
    }
    if (!here) return false;

    // Distance to each of the four edges; walk toward whichever is nearest.
    const i32 toWest  = curX - here->x1;
    const i32 toEast  = here->x2 - curX;
    const i32 toNorth = curY - here->y1;
    const i32 toSouth = here->y2 - curY;

    i32 best = toWest;
    i32 dx = -1, dy = 0;
    if (toEast < best)  { best = toEast;  dx = 1; dy = 0; }
    if (toNorth < best) { best = toNorth; dx = 0; dy = -1; }
    if (toSouth < best) { best = toSouth; dx = 0; dy = 1; }

    const i32 toExit = best + kExitMargin;
    const i32 step = toExit < stepLimit ? toExit : stepLimit;
    if (step <= 0) return false;

    *outX = curX + dx * step;
    *outY = curY + dy * step;
    return true;
}

}  // namespace uo::world_atlas
