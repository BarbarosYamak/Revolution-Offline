// Regression for the "goal occupied by a live decorator item" defect
// (Aurelius/Odessa, 2026-09-07): a RoutePlanner waypoint at Britain
// (1416,1592) sits on top of a Sphere world-save decorator item
// (i_bookcase_full, DISPID 0x0A9B, spherestatics.scp) that the offline
// atlasgen never modeled -- it only samples the client's static MULs, never
// the server's decorator overlay. GoalColumnIsWalkable() used to check ONLY
// raw MUL walkability, so the request sailed past goalWalkable=true and the
// full tile A* then burned its whole node budget (~1s against ~300 nearby
// overlay items) probing every approach to a tile that could never be
// reached, instead of taking the cheap "snap to a nearby standable tile"
// salvage a literal-tree/rock goal already gets.
//
// Configuration 1 (always runs, no client data needed): an unconfigured
// PathPlanner reports worldReady=false cleanly.
//
// Configuration 2 (UO_MUL_DIR set): the real Britain MULs, the real three
// starting positions from the live failure (run_gates/g_Aurelius.err.txt,
// 2026-09-07 01:52), and a PathDynamicItem standing in for the live
// bookcase overlay entry. Confirms:
//   (a) the occupied goal is now correctly reported unwalkable instead of
//       swallowing the whole node budget, and
//   (b) the real Britain Graveyard weak-band tile is reachable from all
//       three starts once the poisoned waypoint stops being pursued.
//
// Configuration D (same UO_MUL_DIR gate): companion regression for
// PathRequest/PathOptions::allowBlockedGoal (Alder/tp_278, 2026-09-07) --
// a teleporter pad's own decoration reads as the same kind of false-positive
// obstacle as the bookcase above, and allowBlockedGoal is the exemption a
// Teleporter-kind travel leg asks for so the walk leg can actually land on
// the literal pad tile. Reuses the same poisoned (1416,1592) tile/overlay as
// a stand-in pad. Confirms the exemption (a) reports the pad walkable and
// finds a real path to it, (b) still respects a live mobile standing there,
// and (c) the raw tile A* itself -- not just the upfront verdict -- ends
// its returned path exactly on the pad tile.

#include "navigation/PathPlanner.h"
#include "bot/Pathfinding.h"
#include "uo/map.h"
#include "uo/tiledata.h"
#include "uo/world.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

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

// Poll with a short deadline instead of spinning forever if the worker
// thread never answers (a hang here must fail loudly, not wedge ctest).
bool PollWithTimeout(navigation::PathPlanner& planner, navigation::PathResult* out,
                     int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (planner.Poll(out)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

void TestUnconfiguredPlannerIsSafe() {
    Section("A: an unconfigured PathPlanner reports worldReady=false, never hangs");
    navigation::PathPlannerConfig cfg;  // all paths empty
    navigation::PathPlanner planner(cfg);
    navigation::PathRequest req;
    req.requestId = 1;
    req.startX = 0; req.startY = 0; req.startZ = 0;
    req.goalX = 1; req.goalY = 1;
    planner.Request(req);
    navigation::PathResult res;
    const bool got = PollWithTimeout(planner, &res);
    Check(got, "an unconfigured planner still answers (no client data required)");
    if (got) Check(!res.worldReady, "no MUL paths configured => worldReady=false");
}

// The real bookcase sitting exactly on the (1416,1592) navgrid waypoint,
// per runtime/save/spherestatics.scp:80239 (SERIAL=040000d4c, DISPID=0a9b,
// P=1416,1592,30). itemId here is the tiledata graphic id (0x0A9B), not the
// Sphere defname -- the overlay keys on graphic like the live client does.
navigation::PathDynamicItem BookcaseOverlayItem() {
    navigation::PathDynamicItem it;
    it.itemId = 0x0A9B;
    it.x = 1416;
    it.y = 1592;
    it.z = 30;
    it.gfxOffset = 0;
    return it;
}

// Stands in for a live decoration overlay that blocks exactly one cell --
// used by TestAllowBlockedGoalPathTerminatesOnPad as bot::PathOptions::
// extraBlocked, the same shape PathPlanner.cpp wires ExtraBlocked into.
struct SingleCellOverlay {
    i32 x = 0;
    i32 y = 0;
    static bool Blocked(i32 x, i32 y, i8 /*z*/, void* user) {
        const auto* self = static_cast<const SingleCellOverlay*>(user);
        return self && x == self->x && y == self->y;
    }
};

struct Start {
    const char* label;
    i32 x, y;
    i8 z;
};

// The three starts from the live failure (g_Aurelius.err.txt / g_Odessa,
// 2026-09-07 01:52-01:58): three different floors, all failing the same way.
constexpr Start kStarts[] = {
    {"start1 (1432,1672,10)", 1432, 1672, 10},
    {"start2 (1432,1560,30)", 1432, 1560, 30},
    {"start3 (1423,1599,20)", 1423, 1599, 20},
};

void TestOccupiedGoalIsNotWalkable(navigation::PathPlanner& planner) {
    Section("B: a goal sitting under a live decorator item is not walkable");
    u64 nextId = 100;
    for (const Start& s : kStarts) {
        navigation::PathRequest req;
        req.requestId = nextId++;
        req.startX = s.x; req.startY = s.y; req.startZ = s.z;
        req.goalX = 1416; req.goalY = 1592;  // the poisoned waypoint
        req.maxNodesExpanded = 65732;         // matches the live budget formula
        req.dynamicItems.push_back(BookcaseOverlayItem());
        planner.Request(req);

        navigation::PathResult res;
        const bool got = PollWithTimeout(planner, &res);
        char what[160];
        std::snprintf(what, sizeof(what), "%s: planner answered", s.label);
        Check(got, what);
        if (!got) continue;

        std::snprintf(what, sizeof(what),
                      "%s: (1416,1592) with the bookcase overlay is reported "
                      "unwalkable, not searched to exhaustion", s.label);
        Check(!res.goalWalkable, what);

        // The point of catching this at GoalColumnIsWalkable time is that the
        // full A* never runs at all -- confirm the search stayed cheap
        // instead of burning the ~1s the live bug spent per attempt.
        std::snprintf(what, sizeof(what),
                      "%s: rejecting the occupied goal is near-instant, not a "
                      "budget-exhausting search", s.label);
        Check(res.searchUs < 50000.0, what);  // 50ms, generous vs. the live ~1.05s
    }
}

// Regression for the teleporter-pad companion defect (Alder/tp_278,
// 2026-09-07): a Sphere teleporter pad's own decoration -- here stood in for
// by the same bookcase overlay used above -- reads as an obstacle to the
// ordinary walkable/dynamic-item verdict even though stepping onto that
// exact tile is how the mechanic fires. PathRequest::allowBlockedGoal (and
// bot::PathOptions::allowBlockedGoal underneath it) exempt the literal goal
// cell from that verdict while a live mobile standing there still blocks it.
void TestAllowBlockedGoalStepsOntoPad(navigation::PathPlanner& planner) {
    Section("D: allowBlockedGoal steps onto a pad the ordinary verdict "
            "would reject");
    u64 nextId = 200;

    // D1: the same poisoned tile, now requested as a teleporter pad. Without
    // the flag this is TestOccupiedGoalIsNotWalkable's case (goalWalkable
    // false); with it, the overlay item stops mattering.
    {
        navigation::PathRequest req;
        req.requestId = nextId++;
        req.startX = 1432; req.startY = 1672; req.startZ = 10;
        req.goalX = 1416; req.goalY = 1592;
        req.hasGoalZ = true;
        req.goalZ = 30;  // the bookcase's own z, per spherestatics.scp
        req.allowBlockedGoal = true;
        req.maxNodesExpanded = 65732;
        req.dynamicItems.push_back(BookcaseOverlayItem());
        planner.Request(req);

        navigation::PathResult res;
        const bool got = PollWithTimeout(planner, &res);
        Check(got, "D1: planner answered");
        if (got) {
            Check(res.goalWalkable,
                  "D1: allowBlockedGoal exempts the pad tile from the "
                  "dynamic-item verdict");
            Check(!res.path.empty(),
                  "D1: a path to the pad tile is actually found, not just "
                  "declared walkable");
        }
    }

    // D2: a live mobile standing exactly on the pad still blocks it --
    // allowBlockedGoal is not a blanket "ignore everything" escape hatch,
    // only an exemption from the terrain/decoration false positive.
    {
        navigation::PathRequest req;
        req.requestId = nextId++;
        req.startX = 1432; req.startY = 1672; req.startZ = 10;
        req.goalX = 1416; req.goalY = 1592;
        req.hasGoalZ = true;
        req.goalZ = 30;
        req.allowBlockedGoal = true;
        req.maxNodesExpanded = 65732;
        req.dynamicItems.push_back(BookcaseOverlayItem());
        navigation::PathMobileBlocker body;
        body.serial = 0xDEAD0001;
        body.x = 1416; body.y = 1592; body.z = 30;
        body.seenMs = 0;
        req.mobiles.push_back(body);
        planner.Request(req);

        navigation::PathResult res;
        const bool got = PollWithTimeout(planner, &res);
        Check(got, "D2: planner answered");
        if (got) {
            Check(!res.goalWalkable,
                  "D2: a live mobile actually standing on the pad still "
                  "blocks it even with allowBlockedGoal");
        }
    }
}

// D3: the raw A* (no PathPlanner overlay-goal short-circuit) must actually
// terminate its returned path ON the exact goal tile when allowBlockedGoal
// is set, walking the direction list out to confirm -- goalWalkable=true is
// not proof the search itself ever reaches the column.
void TestAllowBlockedGoalPathTerminatesOnPad(world::World& world) {
    Section("D3: the tile A* path itself ends exactly on the pad tile");
    constexpr i32 kGoalX = 1416, kGoalY = 1592;
    constexpr i32 kStartX = 1432, kStartY = 1672;
    constexpr i8  kStartZ = 10;

    SingleCellOverlay overlay{kGoalX, kGoalY};  // stands in for the pad decoration

    bot::PathOptions opts;
    opts.maxNodesExpanded = 400000;
    opts.hasGoalZ = true;
    opts.goalZ = 30;
    opts.allowBlockedGoal = true;
    opts.extraBlocked = &SingleCellOverlay::Blocked;
    opts.extraBlockedUser = &overlay;

    const auto path = bot::FindPath(world, kStartX, kStartY, kStartZ,
                                    kGoalX, kGoalY, opts);
    Check(!path.empty(), "D3: a path exists despite extraBlocked rejecting "
                          "the literal goal cell");
    if (path.empty()) return;

    i32 x = kStartX, y = kStartY;
    for (u8 dir : path) {
        i32 dx = 0, dy = 0;
        bot::DirToDelta(dir, &dx, &dy);
        x += dx;
        y += dy;
    }
    char what[128];
    std::snprintf(what, sizeof(what),
                  "D3: replaying the path lands on the literal pad tile "
                  "(%d,%d), got (%d,%d)", kGoalX, kGoalY, x, y);
    Check(x == kGoalX && y == kGoalY, what);
}

void TestGraveyardBandReachable(world::World& world) {
    Section("C: the Britain Graveyard weak band is reachable from all three "
            "starts once the poisoned waypoint is not the target");
    // britain_graveyard_knights, revolution_atlas.txt:2628 -- the novice-tier
    // weak band inside a_britain_graveyard_1 (RECT 1336,1443..1390,1493).
    constexpr i32 kGraveX = 1385, kGraveY = 1459;
    bot::PathOptions opts;
    opts.maxNodesExpanded = 400000;  // generous; this is a real cross-town hop
    for (const Start& s : kStarts) {
        bot::PathStats stats{};
        opts.stats = &stats;
        const auto path = bot::FindPath(world, s.x, s.y, s.z, kGraveX, kGraveY, opts);
        char what[160];
        std::snprintf(what, sizeof(what),
                      "%s: a route to the Britain Graveyard weak band exists",
                      s.label);
        Check(!path.empty(), what);
    }
}

}  // namespace

int main() {
    std::printf("nav_dynamic_goal (occupied-goal detection + graveyard reachability)\n");

    TestUnconfiguredPlannerIsSafe();

    const char* mulDir = std::getenv("UO_MUL_DIR");
    if (mulDir && mulDir[0]) {
        const std::string d = mulDir;
        navigation::PathPlannerConfig cfg;
        cfg.tiledataPath = d + "/tiledata.mul";
        cfg.mapPath = d + "/map0.mul";
        cfg.staidxPath = d + "/staidx0.mul";
        cfg.staticsPath = d + "/statics0.mul";
        cfg.verdataPath = d + "/verdata.mul";
        cfg.acceptDoors = true;
        navigation::PathPlanner planner(cfg);
        TestOccupiedGoalIsNotWalkable(planner);
        TestAllowBlockedGoalStepsOntoPad(planner);

        tiledata::TileDataLoader td;
        if (td.Load(cfg.tiledataPath.c_str())) {
            map::Map m;
            if (m.Open(cfg.mapPath.c_str(), cfg.staidxPath.c_str(),
                       cfg.staticsPath.c_str(), map::kBritWidthBlocks,
                       map::kBritHeightBlocks, cfg.verdataPath.c_str())) {
                world::World world(td, m);
                world.SetAcceptDoors(true);
                TestGraveyardBandReachable(world);
                TestAllowBlockedGoalPathTerminatesOnPad(world);
            } else {
                std::printf("[skip] map0.mul open failed under UO_MUL_DIR=%s\n", d.c_str());
            }
        } else {
            std::printf("[skip] tiledata.mul load failed under UO_MUL_DIR=%s\n", d.c_str());
        }
    } else {
        std::printf("[skip] configuration B/C (UO_MUL_DIR not set)\n");
    }

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
