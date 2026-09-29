#pragma once

// ---------------------------------------------------------------------------
// Walking a guarded town's own boundary outward (M-fix11).
//
// Project owner, 2026-08-31: "if he left the guard zone at Britain he would
// see farmable trees" -- a lumberjack with no proven stand and no atlas-
// seeded lead should not idle in town waiting for one. A real player in that
// position walks out of town and looks. The atlas backs this up: it carries
// no `lumber`-tagged resource area anywhere at all
// (data/revolution_atlas.txt has zero PLACE rows with resources=lumber,
// despite atlasgen's own DeriveForests existing to produce them --
// AtlasGenMain.cpp:753-820 -- the shipped file simply has none), so
// TravelToResource(Lumber) can never succeed and never will until that gap
// is closed. Walking out of the guard line and scanning is not a workaround
// for a slow atlas; for this resource it is the ONLY path that can work.
//
// Pure geometry over the atlas region's own RECTs, no walkability check, no
// live Client -- the same shape as MiningAdvance.h's DeeperMiningPoint, and
// the caller (Client::StepOutOfGuardZone) carries the same division of
// labour: gate on the region actually being guarded, vet the result before
// walking there.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/world_model.h"

namespace uo::world_atlas {

// Steps (curX, curY) toward the NEAREST edge of whichever of `region`'s own
// RECTs contains it, by at most `stepLimit` tiles, and a little past that
// edge -- so repeated calls walk the character out through whichever wall is
// closest rather than toward the region's own centre (usually its plaza, not
// the short way out), and the final step actually clears the RECT rather
// than landing exactly on its rim.
//
// Returns false when the region has no RECTs, when (curX, curY) is not
// inside any of them (nothing to step out of), or when `outX`/`outY` are
// null.
bool StepOutOfGuardedRegion(const wm::Region& region, i32 curX, i32 curY,
                            i32 stepLimit, i32* outX, i32* outY);

}  // namespace uo::world_atlas
