// Deterministic tests for the M2.5 world/travel layer.
//
// These link the same objects the client links -- uo_world plus the travel
// state machines -- so the routing, the journey's recovery ladder, the war
// watchdog and the personal-knowledge isolation rule cannot drift from
// shipping behaviour. Nothing here needs a server, the MUL files or the
// generated atlas: the world under test is synthesised inline, which is what
// makes the results repeatable.
//
// Proving the shard actually behaves this way is the job of the live
// scenarios, not of this file.

#include "travel/Journey.h"
#include "travel/PersonalKnowledge.h"
#include "travel/WarMode.h"
#include "world/Atlas.h"
#include "world/NavGrid.h"
#include "world/RoutePlanner.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace uo;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

// ---------------------------------------------------------------------------
// A miniature world, in exactly the format uo_atlasgen emits. Two towns with a
// bank each, a wilderness between them, a dungeon that bans recall, a
// teleporter and a pair of moongates.
// ---------------------------------------------------------------------------
const char* const kAtlasText =
    "# test atlas\n"
    "MAP\t0\t512\t512\n"
    "REGION\ta_world\tworld\t0\t256\t256\t0\tALLMAP\tFelucca\n"
    "RECT\ta_world\t0\t0\t511\t511\n"
    "REGION\ta_alpha\ttown\t1\t40\t40\t0\tAlpha\tAlpha\n"
    "RECT\ta_alpha\t20\t20\t60\t60\n"
    "REGION\ta_alpha_bank\tbuilding\t1\t42\t42\t0\tAlpha\tAlpha Bank House\n"
    "RECT\ta_alpha_bank\t40\t40\t45\t45\n"
    "REGION\ta_beta\ttown\t1\t400\t400\t0\tBeta\tBeta\n"
    "RECT\ta_beta\t380\t380\t420\t420\n"
    "REGION\ta_moor\twilderness\t0\t200\t200\t0\tMisc\tThe Moor\n"
    "RECT\ta_moor\t100\t100\t300\t300\n"
    "REGION\ta_pit\tdungeon\t8C\t150\t450\t0\tOther Dungeons\tThe Pit\n"
    "RECT\ta_pit\t140\t440\t170\t470\n"
    "PLACE\talpha_bank\tbank\ta_alpha\t42\t42\t0\t5\tbanker\t\tAlpha Bank\n"
    "PLACE\talpha_healer\thealer\ta_alpha\t50\t30\t0\t4\thealer\t\tAlpha Healer\n"
    "PLACE\tbeta_bank\tbank\ta_beta\t402\t402\t0\t5\tbanker\t\tBeta Bank\n"
    "PLACE\tmoor_mine\tresource_area\ta_moor\t200\t210\t0\t12\t\tmining\tMoor Mine\n"
    "PLACE\tmoongate_alpha\tmoongate\ta_alpha\t56\t56\t0\t2\t\t\tAlpha Moongate\n"
    "PLACE\tmoongate_beta\tmoongate\ta_beta\t392\t392\t0\t2\t\t\tBeta Moongate\n"
    "TRANSIT\ttp_0\tteleporter\t120\t120\t0\t180\t180\t0\t0\ttp_moor_shortcut\n"
    "TRANSIT\tmg_alpha__beta\tmoongate\t56\t56\t0\t392\t392\t0\t0\tBeta\n"
    "TRANSIT\tmg_beta__alpha\tmoongate\t392\t392\t0\t56\t56\t0\t0\tAlpha\n";

// Atlas is deliberately non-copyable (one shared world, never duplicated), so
// tests fill one in place rather than returning it.
void MakeAtlas(world_atlas::Atlas& a) {
    std::string err;
    if (!a.LoadFromText(kAtlasText, &err))
        std::printf("  FAIL  atlas did not load: %s\n", err.c_str());
}

// A 32x32-cell grid (512x512 tiles) that is walkable everywhere except an
// optional wall, so a route has to go around something known.
void MakeGrid(navgrid::NavGrid& grid, bool withWall) {
    constexpr u32 kCX = 32, kCY = 32;
    std::vector<navgrid::Cell> cells(static_cast<usize>(kCX) * kCY);
    for (u32 cy = 0; cy < kCY; ++cy) {
        for (u32 cx = 0; cx < kCX; ++cx) {
            navgrid::Cell& c = cells[static_cast<usize>(cy) * kCX + cx];
            c.anchorOffX = 8;
            c.anchorOffY = 8;
            c.anchorZ = 0;
            // A full-height wall at cx == 16 with one gap, so the only route
            // across is through the gap -- a route the planner must find.
            const bool wall = withWall && cx == 16 && cy != 31;
            c.flags = wall ? 0 : navgrid::kCellPassable;
        }
    }
    // Edges: open in every direction whose neighbour is passable, which is
    // what the real generator measures. Without this the router sees a world
    // with ground but no crossings.
    static const i32 kDelta[8][2] = {
        {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    };
    for (i32 cy = 0; cy < static_cast<i32>(kCY); ++cy) {
        for (i32 cx = 0; cx < static_cast<i32>(kCX); ++cx) {
            navgrid::Cell& c = cells[static_cast<usize>(cy) * kCX + cx];
            if (!(c.flags & navgrid::kCellPassable)) continue;
            for (u8 dir = 0; dir < 8; ++dir) {
                const i32 nx = cx + kDelta[dir][0];
                const i32 ny = cy + kDelta[dir][1];
                if (nx < 0 || ny < 0 || nx >= static_cast<i32>(kCX) ||
                    ny >= static_cast<i32>(kCY))
                    continue;
                const navgrid::Cell& n =
                    cells[static_cast<usize>(ny) * kCX + nx];
                if (n.flags & navgrid::kCellPassable)
                    c.edges |= static_cast<u8>(1u << dir);
            }
        }
    }
    grid.Adopt(kCX, kCY, cells.data());
}

// ---------------------------------------------------------------------------

void TestAtlasParsing() {
    Section("atlas parsing");
    world_atlas::Atlas a; MakeAtlas(a);

    Check(a.Ready(), "atlas loaded");
    Check(a.Regions().size() == 6, "six regions parsed");
    Check(a.Places().size() == 6, "six places parsed");
    Check(a.Transits().size() == 3, "three transits parsed");
    Check(a.MapWidth() == 512 && a.MapHeight() == 512, "map extent parsed");

    // The rectangles overlap by design; the most specific one has to win, or
    // "which region am I in" answers "Felucca" everywhere and is useless.
    const wm::Region* r = a.RegionAt(42, 42);
    Check(r && r->id == "a_alpha_bank", "smallest containing region wins");
    r = a.RegionAt(30, 30);
    Check(r && r->id == "a_alpha", "town wins over the facet");
    r = a.RegionAt(490, 20);
    Check(r && r->id == "a_world", "unclaimed ground falls back to the facet");

    // Lookup widens: defname, then NAME, then substring.
    Check(a.FindRegion("a_beta") != nullptr, "region by defname");
    Check(a.FindRegion("Beta") != nullptr, "region by name");
    const wm::Region* moor = a.FindRegion("moor");
    Check(moor && moor->id == "a_moor", "region by case-insensitive substring");
    Check(a.FindRegion("Atlantis") == nullptr, "unknown region is not invented");

    // A substring that matches several regions should resolve to the largest,
    // so "Alpha" means the town and not the one shop inside it.
    const wm::Region* alpha = a.FindRegion("Alpha");
    Check(alpha && alpha->id == "a_alpha", "ambiguous name resolves to the town");
}

void TestAtlasQueries() {
    Section("atlas queries");
    world_atlas::Atlas a; MakeAtlas(a);

    const wm::Place* p = a.NearestPlaceWithService(wm::Service::Banker, 30, 30);
    Check(p && p->id == "alpha_bank", "nearest banker from Alpha is Alpha's");
    p = a.NearestPlaceWithService(wm::Service::Banker, 410, 410);
    Check(p && p->id == "beta_bank", "nearest banker from Beta is Beta's");

    // The region-scoped question is a different question: "a banker in Beta"
    // must not answer with Alpha's just because we happen to be standing there.
    p = a.NearestPlaceWithServiceInRegion(wm::Service::Banker, "Beta", 30, 30);
    Check(p && p->id == "beta_bank", "service scoped to a named region");
    p = a.NearestPlaceWithServiceInRegion(wm::Service::Healer, "Beta", 30, 30);
    Check(p == nullptr, "a region without the service answers nothing");

    p = a.NearestPlaceWithResource(wm::ResourceKind::Mining, 40, 40);
    Check(p && p->id == "moor_mine", "nearest mining area");
    Check(a.NearestPlaceWithResource(wm::ResourceKind::Fishing, 40, 40) == nullptr,
          "a resource nobody has is not invented");

    Check(a.CountPlacesWithService(wm::Service::Banker) == 2, "two banks");
    Check(a.FindPlace("Alpha Bank") != nullptr, "place by name");
    Check(a.FindPlace("alpha_bank") != nullptr, "place by id");
    Check(a.FindPlace("nowhere") == nullptr, "unknown place is not invented");

    const wm::TransitNode* t =
        a.NearestTransit(wm::TransitKind::Moongate, 50, 50);
    Check(t && t->from.x == 56, "nearest moongate");
    t = a.NearestTransit(wm::TransitKind::Teleporter, 130, 130);
    Check(t && t->id == "tp_0", "nearest teleporter");
}

void TestTravelLegality() {
    Section("travel legality from region flags");
    world_atlas::Atlas a; MakeAtlas(a);

    // a_pit carries flag 0x8C = ANTIMAGIC_ALL | NO_GATE | UNDERGROUND, which
    // is how the shard spells "you are not recalling out of here".
    Check(a.AllowsRecallInto(40, 40), "recall into a town is allowed");
    Check(!a.AllowsRecallInto(150, 450), "recall into the pit is refused");
    Check(!a.AllowsRecallOutOf(150, 450), "recall out of the pit is refused");
    Check(!a.AllowsGateAt(150, 450), "gating in the pit is refused");
    Check(a.AllowsGateAt(200, 200), "gating on the moor is allowed");
}

void TestRoutePlanning() {
    Section("world route planning");
    world_atlas::Atlas atlas; MakeAtlas(atlas);
    navgrid::NavGrid grid;
    MakeGrid(grid, /*withWall=*/false);
    route::RoutePlanner planner(atlas, grid);
    Check(planner.Ready(), "planner is ready with a grid");

    route::RouteOptions opt;
    opt.allowMoongates = false;
    // The test world also holds a teleporter; turn it off so the baseline
    // route is unambiguously "walked the whole way".
    opt.allowTeleporters = false;

    // A short hop needs no macro search at all.
    route::WorldRoute near = planner.Plan(40, 40, 60, 45, opt);
    Check(near.ok, "short hop plans");
    Check(near.legs.size() == 1, "short hop is one leg");
    Check(near.legs[0].kind == route::LegKind::Walk, "short hop is a walk");

    // The long haul must be broken into bounded legs -- that is the entire
    // reason the hierarchy exists, since the tile A* was only measured on
    // short searches.
    route::WorldRoute far = planner.Plan(40, 40, 400, 400, opt);
    Check(far.ok, "cross-world route plans");
    Check(far.legs.size() > 1, "cross-world route is broken into legs");
    i32 px = 40, py = 40;
    bool allShort = true;
    for (const route::RouteLeg& leg : far.legs) {
        const i32 dx = leg.target.x > px ? leg.target.x - px : px - leg.target.x;
        const i32 dy = leg.target.y > py ? leg.target.y - py : py - leg.target.y;
        if ((dx > dy ? dx : dy) > opt.maxLegTiles + 8) allShort = false;
        px = leg.target.x;
        py = leg.target.y;
    }
    Check(allShort, "every leg stays inside the leg budget");
    Check(far.legs.back().target.x == 400 && far.legs.back().target.y == 400,
          "the last leg lands on the true destination");
    Check(far.transitHops == 0, "no transit used when none is allowed");

    // Moongates only appear when the character says it can use them.
    opt.allowMoongates = true;
    route::WorldRoute gated = planner.Plan(40, 40, 400, 400, opt);
    Check(gated.ok, "route plans with gates permitted");
    Check(gated.transitHops >= 1, "a gate is used when it beats walking");
    Check(gated.estimatedTiles < far.estimatedTiles,
          "the gated route is cheaper than walking the whole way");

    bool sawGateLeg = false;
    for (const route::RouteLeg& leg : gated.legs)
        if (leg.kind == route::LegKind::Moongate) {
            sawGateLeg = true;
            Check(leg.label == "Beta", "gate leg carries the destination name");
        }
    Check(sawGateLeg, "the route contains an explicit gate leg");

    // ...and a nearby destination still walks: a bot that ceremonially uses a
    // gate to cross one screen is a bot that looks like a bot.
    route::WorldRoute short2 = planner.Plan(40, 40, 90, 90, opt);
    Check(short2.ok && short2.transitHops == 0,
          "a short trip walks even when gates are available");
}

void TestRouteAvoidance() {
    Section("route avoidance and impossible routes");
    world_atlas::Atlas atlas; MakeAtlas(atlas);
    navgrid::NavGrid grid;
    MakeGrid(grid, /*withWall=*/true);
    route::RoutePlanner planner(atlas, grid);

    route::RouteOptions opt;
    opt.allowTeleporters = false;
    opt.allowMoongates = false;

    // The wall has one gap, in the last row. A correct route finds it.
    route::WorldRoute r = planner.Plan(40, 40, 400, 400, opt);
    Check(r.ok, "route found through the only gap in the wall");
    bool wentSouth = false;
    for (const route::RouteLeg& leg : r.legs)
        if (leg.target.y > 460) wentSouth = true;
    Check(wentSouth, "the route detours to the gap rather than through the wall");

    // Close the gap by avoiding its cell and the crossing becomes impossible.
    // Failing cleanly is the requirement; looping forever is the bug.
    std::vector<u32> avoid;
    avoid.push_back(planner.CellIndex(16 * 16, 31 * 16));
    opt.avoidCells = &avoid;
    route::WorldRoute blocked = planner.Plan(40, 40, 400, 400, opt);
    Check(!blocked.ok, "no route when the only gap is ruled out");
    Check(blocked.failure && *blocked.failure, "failure carries a reason");
}

// ---------------------------------------------------------------------------
// Journey: sequencing, and the bounded recovery ladder.
// ---------------------------------------------------------------------------

route::WorldRoute ThreeLegRoute() {
    route::WorldRoute r;
    r.ok = true;
    for (int i = 1; i <= 3; ++i) {
        route::RouteLeg leg;
        leg.kind = route::LegKind::Walk;
        leg.target.x = 100 * i;
        leg.target.y = 100 * i;
        r.legs.push_back(leg);
    }
    r.estimatedTiles = 300;
    return r;
}

void TestJourneySequencing() {
    Section("journey sequencing");
    travel::Journey j;
    i64 t = 1000;

    j.Begin("test", 300, 300, 2, t);
    Check(j.Active(), "journey is active once begun");
    Check(j.CurrentPhase() == travel::Phase::NeedRoute, "begins needing a route");
    Check(j.NextCommand(t) == travel::Command::PlanRoute, "asks for a plan");

    j.SetRoute(ThreeLegRoute(), t);
    Check(j.CurrentPhase() == travel::Phase::Walking, "walking after a route");
    Check(j.LegCount() == 3, "three legs");
    Check(j.NextCommand(t) == travel::Command::WalkTo, "asks to walk");

    i32 tx = 0, ty = 0;
    i8 tz = 0;
    j.CommandTarget(&tx, &ty, &tz);
    Check(tx == 100 && ty == 100, "first leg targets the first waypoint");

    j.NoteCommandIssued(travel::Command::WalkTo, t);
    Check(j.NextCommand(t) == travel::Command::Wait,
          "does not re-issue a leg already under way");

    j.OnLegArrived(100, 100, t += 1000);
    Check(j.LegIndex() == 1, "advances to the second leg");
    j.NoteCommandIssued(travel::Command::WalkTo, t);
    j.OnLegArrived(200, 200, t += 1000);
    Check(j.LegIndex() == 2, "advances to the third leg");

    // The goal radius, not the leg count, decides arrival.
    j.NoteCommandIssued(travel::Command::WalkTo, t);
    j.OnLegArrived(299, 301, t += 1000);
    Check(j.CurrentPhase() == travel::Phase::Arrived,
          "arriving within the radius finishes the trip");
    Check(j.NextCommand(t) == travel::Command::Finish, "reports finished");
    Check(!j.Active(), "a finished journey is not active");
}

void TestJourneyNoRoute() {
    Section("journey with no route");
    travel::Journey j;
    j.Begin("nowhere", 900, 900, 2, 0);
    route::WorldRoute bad;
    bad.ok = false;
    bad.failure = "no world route to the destination";
    j.SetRoute(bad, 0);
    Check(j.CurrentPhase() == travel::Phase::Failed, "a failed plan fails the trip");
    Check(j.FailureReason() == travel::Failure::NoRoute, "reported as no-route");
    Check(j.FailureDetail() == "no world route to the destination",
          "the planner's reason is preserved");
    Check(j.NextCommand(0) == travel::Command::Fail, "reports failure");
}

void TestJourneyRecoveryLadder() {
    Section("journey recovery ladder");
    travel::Journey j;
    travel::Limits lim;
    lim.maxLegRetries = 2;
    lim.maxRoutePlans = 2;
    lim.recoveryPauseMs = 0;
    j.SetLimits(lim);

    i64 t = 0;
    j.Begin("hard", 300, 300, 2, t);
    j.SetRoute(ThreeLegRoute(), t);   // plan 1

    // Rung 1: retry the same leg, twice.
    j.NoteCommandIssued(travel::Command::WalkTo, t);
    j.OnLegFailed("blocked", t);
    Check(j.LegRetries() == 1, "first failure retries the leg");
    Check(j.CurrentPhase() == travel::Phase::Walking, "still walking the same leg");
    Check(j.LegIndex() == 0, "does not skip the leg it could not walk");
    j.NoteCommandIssued(travel::Command::WalkTo, t);
    j.OnLegFailed("blocked", t);
    Check(j.LegRetries() == 2, "second failure retries again");

    // Rung 2: give up on the leg and replan the world route.
    j.NoteCommandIssued(travel::Command::WalkTo, t);
    j.OnLegFailed("blocked", t);
    Check(j.CurrentPhase() == travel::Phase::NeedRoute,
          "exhausted leg retries escalate to a replan");
    Check(j.NextCommand(t) == travel::Command::PlanRoute, "asks for a new plan");

    j.SetRoute(ThreeLegRoute(), t);   // plan 2 -- the budget is now spent
    j.NoteCommandIssued(travel::Command::WalkTo, t);
    for (int i = 0; i < 3; ++i) {
        j.OnLegFailed("blocked", t);
        j.NoteCommandIssued(travel::Command::WalkTo, t);
    }

    // Rung 3: fail cleanly. The point of the ladder is that it ENDS.
    Check(j.CurrentPhase() == travel::Phase::Failed,
          "the ladder terminates instead of retrying forever");
    Check(j.FailureReason() == travel::Failure::Unreachable,
          "reported as unreachable");
    Check(j.RoutePlans() <= lim.maxRoutePlans, "the replan budget is respected");
}

void TestJourneyStuckDetection() {
    Section("journey stuck detection");
    travel::Journey j;
    travel::Limits lim;
    lim.maxNoProgressSamples = 4;
    lim.maxLegRetries = 1;
    lim.maxRoutePlans = 1;
    lim.recoveryPauseMs = 0;
    j.SetLimits(lim);

    i64 t = 0;
    j.Begin("stuck", 300, 300, 2, t);
    j.SetRoute(ThreeLegRoute(), t);
    j.NoteCommandIssued(travel::Command::WalkTo, t);

    // Standing still: distance to the waypoint never improves. The first
    // sample establishes the baseline distance, so it takes maxNoProgress + 1
    // samples to convict.
    for (int i = 0; i < 5; ++i) j.OnPositionSample(10, 10, t += 500);
    Check(j.LegRetries() == 1, "no progress triggers a retry");

    // Real progress resets the counter rather than accumulating toward a
    // false positive -- a bot walking around a house moves away first.
    j.NoteCommandIssued(travel::Command::WalkTo, t);
    j.OnPositionSample(20, 20, t += 500);
    j.OnPositionSample(40, 40, t += 500);
    j.OnPositionSample(60, 60, t += 500);
    Check(j.NoProgressSamples() == 0, "progress clears the stuck counter");
    Check(j.CurrentPhase() == travel::Phase::Walking, "still travelling");

    // Stuck again with the ladder spent: it must end, not spin. The client
    // re-issues the leg after each recovery, which is what the journey is
    // waiting for -- it never counts progress on a leg nobody is walking.
    for (int i = 0; i < 12; ++i) {
        j.OnPositionSample(60, 60, t += 500);
        if (j.NextCommand(t) == travel::Command::WalkTo)
            j.NoteCommandIssued(travel::Command::WalkTo, t);
    }
    Check(j.CurrentPhase() == travel::Phase::Failed,
          "a genuinely stuck trip fails instead of looping");
}

void TestJourneyOscillation() {
    Section("journey oscillation detection");
    travel::Journey j;
    travel::Limits lim;
    // Big enough that plain no-progress cannot be what fires.
    lim.maxNoProgressSamples = 1000;
    lim.maxOscillations = 2;
    lim.oscillationWindow = 6;
    lim.maxLegRetries = 4;
    lim.maxRoutePlans = 4;
    lim.recoveryPauseMs = 0;
    j.SetLimits(lim);

    i64 t = 0;
    j.Begin("bounce", 300, 300, 2, t);
    j.SetRoute(ThreeLegRoute(), t);
    j.NoteCommandIssued(travel::Command::WalkTo, t);

    // A B A B A B ... the classic two-cells-that-reroute-into-each-other
    // shuffle, which never trips a pure distance check because the distance
    // is the same every other sample.
    for (int i = 0; i < 10; ++i) {
        j.OnPositionSample((i % 2) ? 51 : 50, 50, t += 500);
        j.NoteCommandIssued(travel::Command::WalkTo, t);
    }
    Check(j.LegRetries() > 0, "oscillation is detected and recovered from");
}

void TestJourneyTransition() {
    Section("journey world transitions");
    travel::Journey j;
    i64 t = 0;
    j.Begin("recall", 300, 300, 2, t);
    j.SetRoute(ThreeLegRoute(), t);
    j.NoteCommandIssued(travel::Command::WalkTo, t);
    j.OnPositionSample(50, 50, t += 500);

    // The server moved us a long way in one sample: the local plan is stale
    // and must be thrown away rather than walked from a position we left.
    j.OnPositionSample(1000, 1000, t += 500);
    Check(j.CurrentPhase() == travel::Phase::NeedRoute,
          "a position jump invalidates the route");
    Check(j.NextCommand(t) == travel::Command::PlanRoute,
          "and asks for a fresh plan from where we now are");

    // A transition that lands on the goal is an arrival, not a replan.
    travel::Journey k;
    k.Begin("gate", 300, 300, 3, t);
    k.SetRoute(ThreeLegRoute(), t);
    k.NoteCommandIssued(travel::Command::WalkTo, t);
    k.OnPositionSample(50, 50, t += 500);
    k.OnPositionSample(301, 300, t += 500);
    Check(k.CurrentPhase() == travel::Phase::Arrived,
          "a transition onto the goal finishes the trip");
}

void TestJourneyTransitLeg() {
    Section("journey transit legs");
    travel::Journey j;
    travel::Limits lim;
    lim.transitTimeoutMs = 1000;
    lim.maxLegRetries = 1;
    lim.maxRoutePlans = 2;   // one plan for the trip, one left for the replan
    lim.recoveryPauseMs = 0;
    j.SetLimits(lim);

    route::WorldRoute r;
    r.ok = true;
    route::RouteLeg walk;
    walk.kind = route::LegKind::Walk;
    walk.target.x = 56;
    walk.target.y = 56;
    r.legs.push_back(walk);
    route::RouteLeg gate;
    gate.kind = route::LegKind::Moongate;
    gate.target.x = 56;
    gate.target.y = 56;
    gate.arrive.x = 392;
    gate.arrive.y = 392;
    gate.label = "Beta";
    r.legs.push_back(gate);
    route::RouteLeg walk2;
    walk2.kind = route::LegKind::Walk;
    walk2.target.x = 402;
    walk2.target.y = 402;
    r.legs.push_back(walk2);

    // A route whose FIRST leg is the transit -- what the planner emits when the
    // bot is already standing on the gate. It must start in the transit phase,
    // not "walk" to the tile it is on and skip the hop.
    {
        route::WorldRoute onGate;
        onGate.ok = true;
        onGate.legs.push_back(gate);
        onGate.legs.push_back(walk2);
        travel::Journey k;
        k.Begin("already on the gate", 402, 402, 2, 0);
        k.SetRoute(onGate, 0);
        Check(k.CurrentPhase() == travel::Phase::AtTransit,
              "a route starting with a transit starts at the transit");
        Check(k.NextCommand(0) == travel::Command::UseTransit,
              "and asks to use it rather than walking nowhere");
    }

    i64 t = 0;
    j.Begin("gate trip", 402, 402, 2, t);
    j.SetRoute(r, t);
    j.NoteCommandIssued(travel::Command::WalkTo, t);
    j.OnLegArrived(56, 56, t += 100);
    Check(j.CurrentPhase() == travel::Phase::AtTransit,
          "reaching the gate moves to the transit phase");
    Check(j.NextCommand(t) == travel::Command::UseTransit, "asks to use it");

    j.NoteCommandIssued(travel::Command::UseTransit, t);
    j.OnPositionSample(56, 56, t += 100);
    Check(j.CurrentPhase() == travel::Phase::AtTransit, "waits for the gate");

    // A gate that does nothing must not hang the trip -- but the retry is
    // "use the gate again", not "walk to the gate". Degrading a transit into a
    // walk leg is what let a failed gate advance the route past the hop it
    // never took, and then walk the whole distance on foot.
    j.OnPositionSample(56, 56, t += 2000);
    Check(j.LegRetries() == 1, "a gate that never fires is retried");
    Check(j.CurrentPhase() == travel::Phase::AtTransit,
          "the retry is still a transit, not a walk");
    Check(j.NextCommand(t) == travel::Command::UseTransit,
          "and it asks to use the gate again");

    // With the retry budget spent, the route is replanned rather than the bot
    // standing on a dead gate forever.
    j.NoteCommandIssued(travel::Command::UseTransit, t);
    j.OnPositionSample(56, 56, t += 2000);
    Check(j.CurrentPhase() == travel::Phase::NeedRoute,
          "an unusable gate escalates to a replan");
}

// ---------------------------------------------------------------------------

void TestWarModeWatchdog() {
    Section("war mode watchdog");
    travel::WarModeWatchdog w;
    travel::WarModeLimits lim;
    lim.idleTimeoutMs = 5000;
    lim.targetLostMs = 3000;
    w.SetLimits(lim);

    i64 t = 1000;
    Check(!w.ShouldExitWar(t), "nothing to do while at peace");

    // Drawing a weapon after a peaceful stretch must not be undone by the
    // stale peaceful intent -- the bug the first live war/peace run found.
    w.OnPeacefulIntent(t);
    w.OnWarModeRequested(t);
    w.OnServerWarMode(true, t);
    Check(!w.ShouldExitWar(t), "a deliberate draw is not instantly sheathed");
    // ...but it is still not a fight, so resuming travel puts it away.
    w.OnPeacefulIntent(t + 500);
    Check(w.ShouldExitWar(t + 500),
          "drawing with no target is undone as soon as the bot travels again");
    w.OnServerWarMode(false, t + 600);

    w.OnServerWarMode(true, t);
    Check(w.ServerWarMode(), "war mode is learned from the server");
    w.OnCombatIntent(0x1234, t);
    Check(w.TargetSerial() == 0x1234, "the accepted target is remembered");
    Check(!w.ShouldExitWar(t + 1000), "stays in war while fighting");

    w.OnCombatEvent(t + 2000);
    Check(!w.ShouldExitWar(t + 3000), "a swing keeps war mode alive");

    // The target vanished: that is the strongest reason to sheathe.
    w.OnTargetGone(0x1234, t + 4000);
    w.OnPeacefulIntent(t + 4000);
    Check(w.ShouldExitWar(t + 4000),
          "no target plus a peaceful intent drops war mode");
    const char* why = w.ExitReason(t + 4000);
    Check(why && *why, "the reason is reportable");

    // Asking once is enough; Sphere punishes repetition (M2 flood finding).
    w.NoteExitRequested(t + 4000);
    Check(!w.ShouldExitWar(t + 4100), "does not re-ask immediately");
    Check(w.ShouldExitWar(t + 20000), "but re-asks if the server never answers");

    // The server saying peace is what actually ends it.
    w.OnServerWarMode(false, t + 21000);
    Check(!w.ShouldExitWar(t + 21000), "nothing to do once the server agrees");
    Check(w.Intent() == travel::CombatIntent::None, "intent cleared with war mode");
}

void TestWarModeIdleTimeout() {
    Section("war mode idle timeout");
    travel::WarModeWatchdog w;
    travel::WarModeLimits lim;
    lim.idleTimeoutMs = 5000;
    lim.targetLostMs = 100000;   // isolate the idle path
    w.SetLimits(lim);

    i64 t = 0;
    w.OnServerWarMode(true, t);
    w.OnCombatEvent(t);
    Check(!w.ShouldExitWar(t + 4000), "inside the timeout, war mode stands");
    Check(w.ShouldExitWar(t + 6000),
          "war mode with no combat event for the timeout is stale");
}

// ---------------------------------------------------------------------------

void TestPersonalKnowledge() {
    Section("personal knowledge");
    travel::PersonalKnowledge k;

    Check(!k.HasVisited("yew_bank"), "a fresh character has been nowhere");
    k.NoteVisit("yew_bank", 1000);
    k.NoteVisit("yew_bank", 2000);
    Check(k.HasVisited("yew_bank"), "a visit is remembered");
    Check(k.Visits().size() == 1, "revisiting the same place is not a new place");
    Check(k.Visits()[0].visits == 2, "visits are counted");

    k.NoteService(wm::Service::Healer, 0x40, "Martina, the healer", 540, 966, 0,
                  5000);
    const travel::ServiceSighting* s =
        k.RecentService(wm::Service::Healer, 6000, 10000);
    Check(s && s->x == 540, "a sighting is recalled while fresh");
    Check(k.RecentService(wm::Service::Healer, 60000, 10000) == nullptr,
          "a stale sighting is not trusted");
    Check(k.RecentService(wm::Service::Banker, 6000, 10000) == nullptr,
          "a service nobody has seen is not invented");
    k.ForgetService(0x40);
    Check(k.RecentService(wm::Service::Healer, 6000, 10000) == nullptr,
          "a deleted mobile is forgotten");

    // Runes: a destination is only known when the server told us one.
    Check(!k.OwnsMarkedRune(), "no runes to start with");
    travel::KnownRune blank;
    blank.serial = 0x100;
    blank.graphic = 0x1F14;
    blank.marked = false;
    k.NoteRune(blank);
    Check(!k.OwnsMarkedRune(), "a blank rune is not a marked one");
    Check(k.BestRuneFor(500, 900, 0) == nullptr,
          "a blank rune is not a travel option");

    travel::KnownRune marked = blank;
    marked.serial = 0x101;
    marked.marked = true;
    marked.destinationKnown = true;
    marked.x = 652;
    marked.y = 820;
    marked.name = "Yew";
    k.NoteRune(marked);
    Check(k.OwnsMarkedRune(), "a marked rune is owned");
    const travel::KnownRune* best = k.BestRuneFor(660, 830, 50);
    Check(best && best->serial == 0x101, "a nearby rune is offered");
    Check(k.BestRuneFor(4000, 4000, 50) == nullptr,
          "a rune that lands nowhere near is not offered");

    // Death and corpse.
    Check(!k.LastDeath().valid, "no death yet");
    k.NoteDeath(689, 753, 0, "a_Yew_Ter", 9000);
    Check(k.LastDeath().valid && k.LastDeath().x == 689, "death is recorded");
    k.NoteCorpse(0x200, 690, 754, 0);
    Check(k.LastDeath().corpseSerial == 0x200, "corpse serial is recorded");
    Check(k.LastDeath().x == 690, "the corpse's own tile wins over the death tile");
    k.NoteCorpseRecoveryAttempt();
    Check(k.LastDeath().recoveryAttempts == 1,
          "recovery attempts are counted, so a loop can be bounded later");

    // Danger is coarse and expires.
    Check(k.DangerAt(100, 100, 10000) == wm::Danger::Unknown,
          "unvisited ground is unknown, not safe");
    k.NoteDanger(100, 100, 10, 20000, "killed here");
    Check(k.DangerAt(105, 105, 15000) == wm::Danger::RecentlyDangerous,
          "a recent death marks the area");
    Check(k.DangerAt(200, 200, 15000) == wm::Danger::Unknown,
          "danger is local to where it happened");
    k.ExpireDanger(25000);
    Check(k.DangerNoteCount() == 0, "danger notes expire");
}

void TestKnowledgeIsolation() {
    Section("multi-session knowledge isolation");
    // Two characters, one process. Nothing one learns may appear in the other:
    // this is the M1.5 rule restated for world knowledge, and it is the reason
    // PersonalKnowledge is a plain member and never a static.
    travel::PersonalKnowledge a, b;

    a.NoteVisit("yew_bank", 1000);
    a.NoteService(wm::Service::Banker, 0x40, "Elias, the banker", 652, 820, 0, 1000);
    a.SetHome(650, 820, 0, "yew_bank");
    a.NoteDeath(689, 753, 0, "a_Yew_Ter", 1000);
    travel::KnownRune r;
    r.serial = 0x1;
    r.marked = true;
    r.destinationKnown = true;
    a.NoteRune(r);

    Check(!b.HasVisited("yew_bank"), "visits do not cross sessions");
    Check(b.RecentService(wm::Service::Banker, 1000, 10000) == nullptr,
          "sightings do not cross sessions");
    Check(!b.HasHome(), "home does not cross sessions");
    Check(!b.LastDeath().valid, "death does not cross sessions");
    Check(!b.OwnsMarkedRune(), "runes do not cross sessions");

    // And two journeys are independent, including their avoid lists.
    travel::Journey ja, jb;
    ja.Begin("a", 100, 100, 1, 0);
    ja.AvoidCell(42);
    jb.Begin("b", 200, 200, 1, 0);
    Check(ja.AvoidCells().size() == 1, "journey A has its own avoid list");
    Check(jb.AvoidCells().empty(), "journey B does not inherit it");
    Check(ja.GoalX() == 100 && jb.GoalX() == 200, "goals are independent");

    // The avoid list is bounded, so bad luck cannot rule out the world.
    for (u32 i = 0; i < 200; ++i) ja.AvoidCell(i);
    Check(ja.AvoidCells().size() <= 32, "the avoid list is bounded");
}

}  // namespace

int main() {
    std::printf("m2.5 world / travel tests\n\n");
    TestAtlasParsing();
    TestAtlasQueries();
    TestTravelLegality();
    TestRoutePlanning();
    TestRouteAvoidance();
    TestJourneySequencing();
    TestJourneyNoRoute();
    TestJourneyRecoveryLadder();
    TestJourneyStuckDetection();
    TestJourneyOscillation();
    TestJourneyTransition();
    TestJourneyTransitLeg();
    TestWarModeWatchdog();
    TestWarModeIdleTimeout();
    TestPersonalKnowledge();
    TestKnowledgeIsolation();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
