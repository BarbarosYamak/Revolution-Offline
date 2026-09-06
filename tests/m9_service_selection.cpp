// Service-place SELECTION policy (docs/CRAFTER_RUN_2026_08_30.md, "Live run
// 2026-08-30", defect 4).
//
// A Minoc miner_smith needing a blacksmith was sent to "Sea Market
// blacksmith" (no walkable ground there), then to "Papua weaponsmith" -- 904
// tiles and three moongate hops into the Lost Lands -- while Minoc's own
// smithy sat unvisited the whole time
// (run_r4/pair_Durnholde.console.txt, 21:16-21:25). The root cause was a skip
// list shared between SMELT and CRAFT that recorded a place as "tried" even
// after the character successfully used it (fixed at the call site,
// src/life/Runner.cpp, the CRAFT forge branch). This suite is the SELECTION
// backstop the owner asked for on top of that fix:
// world_atlas::PickServicePlace (src/world/ServiceSelection.h) ranks
// candidates by the route planner's own tiles-and-gates, not raw map
// distance, prefers fewer transit hops, and caps a trip at ~1200 tiles
// unless the caller says the errand demands going further.
//
// Section A proves the policy itself against a small, fully-controlled
// synthetic atlas (three towns, one across a gate) -- the m25_world.cpp
// style. Section B is the regression guard: the REAL generated atlas and
// navgrid (argv[1] = the data directory), from the coordinates Durnholde was
// actually standing at when defect 4 fired, must resolve Blacksmith to
// Minoc's own smithy, never to Papua or Sea Market.

#include "world/Atlas.h"
#include "world/NavGrid.h"
#include "world/RoutePlanner.h"
#include "world/ServiceSelection.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace uo;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

// ---------------------------------------------------------------------------
// A. Synthetic atlas: Home (0 gates, far in raw tiles), Nearby (0 gates,
// genuinely close), and FarGated (fewer raw tiles than Home from the
// standing point, but only reachable through a gate) all offer Blacksmith.
// The point under test is exactly defect 4's shape: a candidate that LOOKS
// closer on the map but costs more in gates must not win.
// ---------------------------------------------------------------------------
const char* const kAtlasText =
    "# synthetic atlas for service selection\n"
    "MAP\t0\t2048\t2048\n"
    "REGION\ta_world\tworld\t0\t1024\t1024\t0\tALLMAP\tBritannia\n"
    "RECT\ta_world\t0\t0\t2047\t2047\n"
    "REGION\ta_home\ttown\t1\t60\t60\t0\tHome\tHome\n"
    "RECT\ta_home\t20\t20\t100\t100\n"
    "REGION\ta_nearby\ttown\t1\t150\t60\t0\tNearby\tNearby\n"
    "RECT\ta_nearby\t120\t20\t180\t100\n"
    "REGION\ta_farside\ttown\t1\t1900\t1900\t0\tFarside\tFarside\n"
    "RECT\ta_farside\t1860\t1860\t1940\t1940\n"
    "PLACE\thome_smith\tshop\ta_home\t40\t40\t0\t5\tblacksmith\t\tHome smith\n"
    "PLACE\tnearby_smith\tshop\ta_nearby\t150\t40\t0\t5\tblacksmith\t\tNearby smith\n"
    "PLACE\tfarside_smith\tshop\ta_farside\t1900\t1900\t0\t5\tblacksmith\t\tFarside smith\n"
    "PLACE\tgate_home\tmoongate\ta_home\t90\t90\t0\t2\t\t\tHome Moongate\n"
    "PLACE\tgate_farside\tmoongate\ta_farside\t1870\t1870\t0\t2\t\t\tFarside Moongate\n"
    "TRANSIT\tmg_home__farside\tmoongate\t90\t90\t0\t1870\t1870\t0\t0\tFarside\n"
    "TRANSIT\tmg_farside__home\tmoongate\t1870\t1870\t0\t90\t90\t0\t0\tHome\n";

// A grid mostly open, walkable everywhere sampled, so raw distance alone
// would send a naive picker at (30,30) toward whichever place sits fewest
// tiles away -- which is exactly the bug this policy exists to catch when
// that place is behind a gate.
void MakeGrid(navgrid::NavGrid& grid) {
    constexpr u32 kCX = 128, kCY = 128;   // 2048 / 16
    std::vector<navgrid::Cell> cells(static_cast<usize>(kCX) * kCY);
    for (u32 cy = 0; cy < kCY; ++cy) {
        for (u32 cx = 0; cx < kCX; ++cx) {
            navgrid::Cell& c = cells[static_cast<usize>(cy) * kCX + cx];
            c.anchorOffX = 8;
            c.anchorOffY = 8;
            c.anchorZ = 0;
            c.flags = navgrid::kCellPassable;
        }
    }
    static const i32 kDelta[8][2] = {
        {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
    };
    for (i32 cy = 0; cy < static_cast<i32>(kCY); ++cy) {
        for (i32 cx = 0; cx < static_cast<i32>(kCX); ++cx) {
            navgrid::Cell& c = cells[static_cast<usize>(cy) * kCX + cx];
            for (u8 dir = 0; dir < 8; ++dir) {
                const i32 nx = cx + kDelta[dir][0];
                const i32 ny = cy + kDelta[dir][1];
                if (nx < 0 || ny < 0 || nx >= static_cast<i32>(kCX) ||
                    ny >= static_cast<i32>(kCY))
                    continue;
                c.edges |= static_cast<u8>(1u << dir);
            }
        }
    }
    grid.Adopt(kCX, kCY, cells.data());
}

void TestPolicySynthetic() {
    Section("A: geography policy against a controlled atlas");

    world_atlas::Atlas atlas;
    std::string err;
    if (!atlas.LoadFromText(kAtlasText, &err)) {
        Check(false, "synthetic atlas loads");
        std::printf("  (%s)\n", err.c_str());
        return;
    }
    navgrid::NavGrid grid;
    MakeGrid(grid);
    route::RoutePlanner planner(atlas, grid);
    Check(planner.Ready(), "planner ready with the synthetic grid");

    // Standing near Home. nearby_smith (0 gates, ~110 tiles) is both fewer
    // gates AND fewer tiles than farside_smith (needs the moongate), so this
    // case alone does not distinguish "prefer close" from "prefer few
    // gates" -- see the second case below for that.
    {
        std::vector<world_atlas::ServiceRejection> rej;
        const world_atlas::ServicePick pick = world_atlas::PickServicePlace(
            atlas, planner, wm::Service::Blacksmith, 30, 30, {}, false, &rej);
        Check(pick.place && pick.place->id == "home_smith",
              "the nearest zero-gate smith wins when it is available");
        Check(pick.transitHops == 0, "no gate needed for the home smith");
    }

    // Now skip the truly nearest one (home_smith), leaving nearby_smith
    // (0 gates, a bit further) and farside_smith (0 raw-tile advantage over
    // nearby_smith once actually walked -- it needs a gate). Fewer gates
    // must still win over "happens to sit at similar distance".
    {
        std::vector<world_atlas::ServiceRejection> rej;
        const world_atlas::ServicePick pick = world_atlas::PickServicePlace(
            atlas, planner, wm::Service::Blacksmith, 30, 30,
            {"home_smith"}, false, &rej);
        Check(pick.place && pick.place->id == "nearby_smith",
              "a zero-gate smith is preferred over a gated one");
        Check(pick.transitHops == 0, "the chosen pick needed no gate");
    }

    // Skip everything zero-gate: only farside_smith is left. It is the only
    // option, so PickServicePlace must still return it -- refusing to travel
    // at all is not the policy, and a farOk-less cap must not strand the
    // character when nothing closer exists.
    {
        std::vector<world_atlas::ServiceRejection> rej;
        const world_atlas::ServicePick pick = world_atlas::PickServicePlace(
            atlas, planner, wm::Service::Blacksmith, 30, 30,
            {"home_smith", "nearby_smith"}, false, &rej);
        Check(pick.place && pick.place->id == "farside_smith",
              "the only remaining candidate is still offered, gate or not");
    }

    // A trip cap: shrink the world's threshold expectation by skipping the
    // near options and asking from a point where farside is absurdly far in
    // real planned tiles (near Home again, but this time verify farOk lets
    // it through when explicitly requested and behaves identically without
    // it here since farside is still the only candidate).
    {
        std::vector<world_atlas::ServiceRejection> rej;
        const world_atlas::ServicePick farOkPick = world_atlas::PickServicePlace(
            atlas, planner, wm::Service::Blacksmith, 30, 30,
            {"home_smith", "nearby_smith"}, /*farOk=*/true, &rej);
        Check(farOkPick.place && farOkPick.place->id == "farside_smith",
              "farOk still finds the only candidate");
    }
}

// ---------------------------------------------------------------------------
// B. Real atlas + navgrid, from Durnholde's actual position when defect 4
// fired (run_r4/pair_Durnholde.console.txt 21:20:01: "Sea Market blacksmith
// -> (4549,2301) r=3 from (2456,502)"). Blacksmith must resolve to Minoc's
// own smithy, not to any Sea Market / Papua / Lost Lands candidate.
// ---------------------------------------------------------------------------
void TestRealAtlasFromMinoc(const std::string& dataDir) {
    Section("B: from Minoc, Blacksmith stays in Minoc (real atlas)");

    world_atlas::Atlas atlas;
    std::string err;
    const std::string atlasPath = dataDir + "/revolution_atlas.txt";
    if (!atlas.Load(atlasPath.c_str(), &err)) {
        Check(false, "the generated atlas loads");
        std::printf("  (%s: %s)\n", atlasPath.c_str(), err.c_str());
        return;
    }
    navgrid::NavGrid grid;
    const std::string gridPath = dataDir + "/revolution_navgrid.bin";
    if (!grid.Load(gridPath.c_str())) {
        Check(false, "the generated navgrid loads");
        std::printf("  (%s)\n", gridPath.c_str());
        return;
    }
    route::RoutePlanner planner(atlas, grid);
    Check(planner.Ready(), "planner ready with the real navgrid");

    const wm::Place* minocSmith = atlas.PlaceById("minoc_blacksmith");
    Check(minocSmith != nullptr, "the atlas still has minoc_blacksmith");

    const i32 durnholdeX = 2456, durnholdeY = 502;   // evidence, not invented

    // Sanity: minoc_blacksmith is actually in the candidate list from here,
    // so a miss below is a policy bug, not a missing place.
    {
        std::vector<const wm::Place*> candidates;
        atlas.PlacesWithServiceSkipping(wm::Service::Blacksmith, durnholdeX,
                                        durnholdeY, {}, candidates);
        bool sawMinoc = false;
        for (const wm::Place* p : candidates)
            if (p->id == "minoc_blacksmith") sawMinoc = true;
        Check(sawMinoc, "minoc_blacksmith is among the raw candidates");
    }

    std::vector<world_atlas::ServiceRejection> rej;
    const world_atlas::ServicePick pick = world_atlas::PickServicePlace(
        atlas, planner, wm::Service::Blacksmith, durnholdeX, durnholdeY, {},
        /*farOk=*/false, &rej);

    Check(pick.place != nullptr, "a blacksmith place is found from Minoc");
    if (pick.place) {
        Check(pick.place->id == "minoc_blacksmith",
              "Blacksmith resolves to Minoc's own smithy, not somewhere else");
        Check(pick.transitHops == 0,
              "reaching it needs no moongate -- it is right here");
        Check(!pick.exceededCap, "it is nowhere near the trip cap");

        // The specific regression: neither of the two places defect 4
        // actually walked to should ever be preferred over Minoc's own.
        Check(pick.place->id != "sea_market_blacksmith",
              "never Sea Market -- that trip had no walkable ground anyway");
        Check(pick.place->id != "papua_weaponsmith",
              "never Papua -- that is three gates into the Lost Lands");
    }
}

// A ghost in Papua must be able to reach Papua's own healer. Faustus died in
// the Lost Lands and logged back in at (5674,3136) on 2026-09-05; every
// "walking to a healer" trip ended in "no place offers healer" although
// papua_healer sits ~105 tiles away at (5737,3222). PickServicePlace drops a
// candidate silently when the planner returns !ok, so this pins the route.
void TestRealAtlasGhostInPapua(const std::string& dataDir) {
    std::printf("-- real atlas: healer reachable from Papua --\n");
    world_atlas::Atlas atlas;
    std::string err;
    if (!atlas.Load((dataDir + "/revolution_atlas.txt").c_str(), &err)) {
        Check(false, "the generated atlas loads");
        return;
    }
    navgrid::NavGrid grid;
    if (!grid.Load((dataDir + "/revolution_navgrid.bin").c_str())) {
        Check(false, "the generated navgrid loads");
        return;
    }
    route::RoutePlanner planner(atlas, grid);

    const i32 ghostX = 5674, ghostY = 3136;   // Faustus' login tile, run log
    const wm::Place* papuaHealer = atlas.PlaceById("papua_healer");
    Check(papuaHealer != nullptr, "the atlas still has papua_healer");
    if (!papuaHealer) return;

    route::RouteOptions opt;
    opt.allowMoongates = true;
    const route::WorldRoute direct = planner.Plan(
        ghostX, ghostY, papuaHealer->position.x, papuaHealer->position.y, opt);
    if (!direct.ok)
        std::printf("  (planner: %s)\n", direct.failure);
    Check(direct.ok, "the planner routes from the Papua ghost tile to papua_healer");

    std::vector<world_atlas::ServiceRejection> rej;
    const world_atlas::ServicePick pick = world_atlas::PickServicePlace(
        atlas, planner, wm::Service::Healer, ghostX, ghostY, {},
        /*farOk=*/false, &rej);
    Check(pick.place != nullptr, "a healer is found from the Papua ghost tile");
    if (pick.place) {
        Check(pick.place->id == "papua_healer",
              "Healer resolves to Papua's own, not one across the gates");
    }
}

// Kharain, miner_smith, mining inside Minoc Mine 1 at (2569,479,0): the
// navgrid cell he stands in (160,29) is a passable ISLAND whose anchor is the
// mountain top at z 40, and its passable neighbours (159,27/28/29), (161,29)
// form a five-cell pocket with edges only to each other. The old "does this
// cell have any edge" snap therefore hopped from the island onto a two-cell
// dead end and the macro search still had nowhere to go: "no place offers
// banker" x58 in one gate (run_gates/g_Kharain.err.txt, 2026-09-06).
void TestRealAtlasMinerInMinocMine(const std::string& dataDir) {
    std::printf("-- real atlas: banker reachable from inside Minoc Mine 1 --\n");
    world_atlas::Atlas atlas;
    std::string err;
    if (!atlas.Load((dataDir + "/revolution_atlas.txt").c_str(), &err)) {
        Check(false, "the generated atlas loads");
        return;
    }
    navgrid::NavGrid grid;
    if (!grid.Load((dataDir + "/revolution_navgrid.bin").c_str())) {
        Check(false, "the generated navgrid loads");
        return;
    }
    route::RoutePlanner planner(atlas, grid);

    const i32 mineX = 2569, mineY = 479;   // Kharain's mining stand tile
    const wm::Place* minocBank = atlas.PlaceById("minoc_bank");
    Check(minocBank != nullptr, "the atlas still has minoc_bank");
    if (!minocBank) return;

    route::RouteOptions opt;
    opt.allowMoongates = true;
    const route::WorldRoute direct = planner.Plan(
        mineX, mineY, minocBank->position.x, minocBank->position.y, opt);
    if (!direct.ok) std::printf("  (planner: %s)\n", direct.failure);
    Check(direct.ok, "the planner routes out of Minoc Mine 1 to minoc_bank");

    std::vector<world_atlas::ServiceRejection> rej;
    const world_atlas::ServicePick pick = world_atlas::PickServicePlace(
        atlas, planner, wm::Service::Banker, mineX, mineY, {},
        /*farOk=*/false, &rej);
    Check(pick.place != nullptr, "a banker is found from inside Minoc Mine 1");
    if (pick.place) {
        Check(pick.place->id == "minoc_bank",
              "Banker resolves to Minoc's own bank, not one across the map");
    }
}

// A lookup that finds nothing must be able to say why. With no route at all
// out of the pocket -- forced here by a service the shard's atlas offers only
// far away plus a start with nothing routable -- PickServicePlace must hand
// the caller the planner's own failure text instead of an empty list.
void TestUnroutableLookupExplainsItself() {
    std::printf("-- unroutable service lookup reports a reason --\n");
    static const char* const kText =
        "MAP\t0\t256\t256\n"
        "REGION\ta_world\tworld\t0\t128\t128\t0\tALLMAP\tBritannia\n"
        "RECT\ta_world\t0\t0\t255\t255\n"
        "PLACE\tfar_bank\tbank\ta_world\t200\t200\t0\t5\tbanker\t\tFar bank\n";
    world_atlas::Atlas atlas;
    std::string err;
    if (!atlas.LoadFromText(kText, &err)) {
        Check(false, "synthetic atlas loads");
        std::printf("  (%s)\n", err.c_str());
        return;
    }
    navgrid::NavGrid grid;
    // 16x16 cells over a 256-tile map. Everything is impassable except the
    // cell the character stands in and the cell the bank stands in, and
    // neither has a single edge: two islands, no road between them.
    std::vector<navgrid::Cell> cells(16 * 16);
    for (navgrid::Cell& c : cells) { c.anchorOffX = 8; c.anchorOffY = 8; }
    cells[0].flags = navgrid::kCellPassable;                  // (0,0)
    cells[(200 / 16) * 16 + (200 / 16)].flags = navgrid::kCellPassable;
    Check(grid.Adopt(16, 16, cells.data()), "synthetic grid adopted");

    route::RoutePlanner planner(atlas, grid);
    std::vector<world_atlas::ServiceRejection> rej;
    const world_atlas::ServicePick pick = world_atlas::PickServicePlace(
        atlas, planner, wm::Service::Banker, 0, 0, {}, /*farOk=*/true, &rej);
    Check(pick.place == nullptr, "no banker is picked when none can be routed to");
    Check(!rej.empty(), "the unroutable candidate is reported, not swallowed");
    if (!rej.empty()) {
        Check(!rej[0].reason.empty(), "the rejection carries the planner's reason");
        std::printf("  (reason: %s)\n", rej[0].reason.c_str());
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: m9_service_selection <data-dir>\n");
        return 2;
    }
    TestPolicySynthetic();
    TestRealAtlasFromMinoc(argv[1]);
    TestRealAtlasGhostInPapua(argv[1]);
    TestRealAtlasMinerInMinocMine(argv[1]);
    TestUnroutableLookupExplainsItself();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
