#pragma once

// ---------------------------------------------------------------------------
// Deeper-into-the-mine target selection (M-fix9).
//
// A newborn miner has no `KnownResourceSource` memory, so DoMine's fast path
// (start the scan from a remembered productive spot) never fires and every
// scan starts wherever `TravelToResource` left him -- the mine's REGION
// BOUNDARY closest to town, i.e. the mouth. Cave-wall statics there pass
// `RockAt` (they are genuine rock graphics) but sit outside the shard's
// scripted resource RECT, so every swing is refused, and the true vein can be
// well past the mining scan radius: Minoc Mine 1's RECT reaches y=474, which
// is 27+ tiles from the south mouth Draver and Corwyn both arrived at.
//
// This is pure geometry over the atlas region's own RECTs: no walkability
// check, no world/MUL state, nothing that needs a live Client. The caller
// (Client::DeeperMiningTarget) is responsible for gating on region kind and
// for vetting the result before ever walking there, same as FishingSpot's
// stand tiles.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/world_model.h"

namespace uo::world_atlas {

// Steps (curX, curY) toward the region's own DEEP POINT by at most
// `stepLimit` tiles, and reports the stepped position.
//
// The deep point is fixed per region, not recomputed from wherever the
// character currently stands: it is the RECT corner farthest (Chebyshev)
// from `region.center` -- the AREADEF's own P=, which for a cave sits at or
// near its mouth. Anchoring on that FIXED reference, rather than on curX/
// curY, is deliberate: a farthest-from-here computation bounces between a
// rectangle's opposite corners as the caller's own position changes across
// repeated advances, so a walker chasing "the point farthest from me" can
// zigzag back toward the entrance on its second step instead of continuing
// inward. Anchoring on the entrance makes every advance a step along the
// SAME line, so repeated calls converge on one interior point instead of
// oscillating.
//
// Returns false when the region carries no RECTs, when its recorded center
// gives no meaningful depth (every corner is at or behind it), or when
// (curX, curY) has already arrived at the deep point (nothing left to
// advance to -- the caller's own bounded advance count is the backstop for
// every other case, but this is the natural "we are there" signal).
bool DeeperMiningPoint(const wm::Region& region, i32 curX, i32 curY,
                       i32 stepLimit, i32* outX, i32* outY);

}  // namespace uo::world_atlas
