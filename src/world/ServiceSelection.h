#pragma once

// ---------------------------------------------------------------------------
// ServiceSelection — picking WHICH place offers a service, before travel ever
// starts.
//
// This is the SELECTION layer, deliberately separate from ClientTravel's
// journey driver: it knows nothing about sockets, packets or the character's
// live position beyond the (x, y) it is handed, so it links into uo_world and
// is unit-testable against the real generated atlas with no server, no MULs
// and no Client.
//
// The defect this exists to fix (docs/CRAFTER_RUN_2026_08_30.md, "Live run
// 2026-08-30", defect 4): a Minoc miner_smith needing a blacksmith was sent to
// "Sea Market blacksmith" (no walkable ground there), then to "Papua
// weaponsmith" -- 904 tiles and three moongate hops into the Lost Lands --
// while Minoc's own smithy (minoc_blacksmith, data/revolution_atlas.txt) sat
// unvisited. The root cause was a skip list shared between two goals
// (SMELT and CRAFT) that recorded every place TRIED, including ones the
// character successfully used, so a smithy that had already served the
// character honestly could never be offered again (src/life/Runner.cpp,
// smeltSkipPlaces_) -- that is fixed at the call site. This module is the
// backstop the owner asked for on top of that fix: even with a clean skip
// list, ranking candidates by RAW MAP DISTANCE ALONE is wrong once moongates
// exist, because a geometrically nearer shop can be unreachable or need three
// gates while a farther one needs none. Rank by the route planner's own
// tiles-and-gates instead.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/world_model.h"
#include "world/Atlas.h"
#include "world/RoutePlanner.h"

#include <string>
#include <vector>

namespace uo::world_atlas {

// Past this many planned tiles, a trip is not "the nearest shop" -- it is a
// different city, and a caller that has not said the errand demands it
// should not be sent there while a cheaper candidate offers the same
// service. 1200 comfortably covers every legitimate same-facet trip measured
// in the R4 run (Britain -> Skara Brae, 736 tiles / 1 gate; Minoc -> Britain
// -> Buccaneer's Den, 416 tiles / 1 gate) while still refusing the Papua
// weaponsmith trip that caused defect 4 (904 tiles, but three gates deep into
// the Lost Lands for a service Minoc already offered at home).
constexpr i32 kMaxServiceTripTiles = 1200;

// A chosen (or best-effort) place, with the trip cost that justified it.
struct ServicePick {
    const wm::Place* place = nullptr;
    i32   estimatedTiles = 0;
    usize transitHops = 0;
    // True when this pick was returned only because nothing else planned
    // inside kMaxServiceTripTiles -- "farther than the policy likes, but it
    // is that or nothing".
    bool  exceededCap = false;
};

// A candidate that lost, and why -- for the caller to log. Never fabricated:
// only candidates whose route the planner actually rejected on cost appear
// here (a candidate the planner cannot route to at all, e.g. "no walkable
// ground near the destination", is not a geography-policy rejection and is
// simply passed over).
struct ServiceRejection {
    const wm::Place* place = nullptr;
    i32   estimatedTiles = 0;
    usize transitHops = 0;
    std::string reason;
};

// Rank every reachable place offering `s` (skipping `skipIds`) by the route
// planner's own trip cost and choose one.
//
// Policy:
//   1. Guarded places are preferred over unguarded ones, same as
//      Atlas::NearestPlaceWithService (Atlas::PlacesWithServiceSkipping
//      already applies this before candidates reach here).
//   2. Among candidates whose planned trip fits the cap (or `farOk` is set),
//      fewer transit hops always wins, tiles only break a tie -- so a
//      same-region smithy beats a three-gate one even when the three-gate
//      one happens to sit fewer raw tiles away in the atlas's coordinates.
//   3. A candidate whose planned trip exceeds kMaxServiceTripTiles is
//      rejected UNLESS `farOk` is true, or NO candidate at all fits the cap
//      (a service the shard only offers far away is still a service; the
//      cap steers away from it, it does not strand the character).
//   4. Candidates the planner cannot route to at all (no walkable ground,
//      no world route) are silently passed over -- that is a data problem,
//      not a geography-policy call, and is not what `rejections` reports.
//
// `maxCandidates` bounds how many places actually get a route plan (each is
// a RoutePlanner::Plan call); candidates arrive distance-sorted from the
// atlas, so the ones worth planning are tried first.
ServicePick PickServicePlace(const Atlas& atlas, const route::RoutePlanner& planner,
                             wm::Service s, i32 x, i32 y,
                             const std::vector<std::string>& skipIds,
                             bool farOk,
                             std::vector<ServiceRejection>* rejections = nullptr,
                             usize maxCandidates = 8);

} // namespace uo::world_atlas
