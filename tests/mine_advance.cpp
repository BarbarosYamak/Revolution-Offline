// First-visit mining deeper-advance (owner diagnosis 2026-08-31, watching a
// newborn Draver in Minoc: "it is not going to mine deep -- that is the
// problem"). A veteran mines fine because a remembered productive spot
// exists; a newborn has none, so DoMine's scan starts wherever
// TravelToResource left him -- the mine's own boundary nearest town, i.e.
// the mouth -- and every rock-graphic tile there is refused
// ("try mining elsewhere") forever
// (run_r4/pair1_Durnholde.console.txt 21:19, run_gates/g_Draver.console.txt
// "no rock in reach").
//
// world_atlas::DeeperMiningPoint (src/world/MiningAdvance.h) is the pure
// geometry DoMine's deeper-advance branch uses to pick where to walk next:
// a FIXED point deep inside a Region's own RECTs (the corner farthest, by
// Chebyshev distance, from the region's own recorded entrance --
// region.center), approached from wherever the character stands in bounded
// steps so DoMine can rescan between advances instead of leaping at ground
// nothing has looked at. Fixed, not recomputed from the caller's own
// position each call, so repeated advances converge on one interior spot
// instead of bouncing between a rectangle's opposite corners as the
// character's own position changes underneath a "farthest from here"
// computation.
//
// Section A proves the geometry, including the convergence property, against
// small, fully-controlled synthetic rects. Section B is the regression
// guard: the REAL generated atlas must still resolve Minoc Mine 1
// (a_minoc_mine_1_1) as a Cave-kind region and walk a character standing at
// its recorded mouth to its actual deep interior within a small, bounded
// number of advances.

#include "world/Atlas.h"
#include "world/MiningAdvance.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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

i32 Chebyshev(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

// ---------------------------------------------------------------------------
// A. Synthetic geometry.
// ---------------------------------------------------------------------------
void TestSyntheticGeometry() {
    Section("A: DeeperMiningPoint over a hand-built region");

    wm::Region cave;
    cave.id = "a_test_cave";
    cave.kind = wm::RegionKind::Cave;
    // A single long RECT: the "mouth" end near (100,100), the deep end
    // toward (100,50) -- mirrors Minoc Mine 1's shape (a short entrance RECT
    // plus a long RECT reaching further from town). The recorded entrance
    // sits at the mouth, same as an AREADEF's P= does for a real cave.
    cave.center = wm::Point{100, 98, 0, 0};
    cave.rects.push_back(wm::Rect{80, 50, 120, 100});

    // Standing at the mouth itself: the deep point is the far corner (48
    // tiles away), so a small step limit must bound the advance, not
    // teleport straight to the corner.
    {
        i32 ox = 0, oy = 0;
        const bool ok = world_atlas::DeeperMiningPoint(cave, 100, 98, 20,
                                                        &ox, &oy);
        Check(ok, "a deeper point exists from the mouth");
        Check(oy < 98, "the advance moves toward smaller y, into the cave");
        Check(Chebyshev(100, 98, ox, oy) <= 20,
              "the advance is bounded by the step limit, not a leap to the "
              "corner");
        Check(cave.Contains(ox, oy),
              "the advance target stays inside the region's own rects");
    }

    // A step limit larger than the actual depth should land exactly on the
    // deep point, not overshoot past the region.
    {
        i32 ox = 0, oy = 0;
        const bool ok = world_atlas::DeeperMiningPoint(cave, 100, 98, 200,
                                                        &ox, &oy);
        Check(ok, "a deeper point exists with a generous step limit");
        Check(oy == 50, "an unbounded step lands exactly on the deep edge");
    }

    // The fixed deep point itself: farthest RECT corner from region.center
    // (100,98) by Chebyshev distance. (80,50) and (120,50) both sit 48 away
    // (dy=48 dominates dx<=40 either side), so the tie is broken by scan
    // order -- the lower-x corner, checked first, wins. That determinism
    // is the point: every caller asking about this region gets the SAME
    // target, tie or not.
    constexpr i32 kDeepX = 80, kDeepY = 50;

    // CONVERGENCE, NOT OSCILLATION: repeated bounded advances, each one
    // recomputed from wherever the previous advance actually landed (as
    // DoMine does tick over tick), must monotonically approach the SAME
    // deep point rather than bounce to a different corner once the walker
    // is closer to one corner than another. This is the property a
    // farthest-from-current-position design gets wrong.
    {
        i32 x = 100, y = 98;
        i32 prevDist = Chebyshev(x, y, kDeepX, kDeepY);
        bool everIncreased = false;
        int steps = 0;
        for (; steps < 10; ++steps) {
            i32 nx = 0, ny = 0;
            if (!world_atlas::DeeperMiningPoint(cave, x, y, 15, &nx, &ny))
                break;
            const i32 d = Chebyshev(nx, ny, kDeepX, kDeepY);
            if (d > prevDist) everIncreased = true;
            prevDist = d;
            x = nx;
            y = ny;
        }
        Check(!everIncreased,
              "distance to the deep point never increases across repeated "
              "advances -- no oscillation");
        Check(x == kDeepX && y == kDeepY,
              "repeated bounded advances converge exactly on the deep point");
        Check(steps > 1,
              "convergence actually took more than one advance (the bound "
              "is doing something, not degenerate)");
    }

    // Standing already at the deep point: nothing left to advance to.
    {
        i32 ox = 0, oy = 0;
        const bool ok = world_atlas::DeeperMiningPoint(cave, kDeepX, kDeepY,
                                                        20, &ox, &oy);
        Check(!ok, "already at the deep point -- no further advance");
    }

    // A region with no rects at all (defensive: atlasgen should never emit
    // one, but the function must not pretend it has somewhere to send the
    // character).
    {
        wm::Region empty;
        empty.kind = wm::RegionKind::Cave;
        i32 ox = 0, oy = 0;
        const bool ok =
            world_atlas::DeeperMiningPoint(empty, 0, 0, 20, &ox, &oy);
        Check(!ok, "a rect-less region has no deeper point");
    }
}

// ---------------------------------------------------------------------------
// B. Real atlas: Minoc Mine 1 (a_minoc_mine_1_1).
// ---------------------------------------------------------------------------
void TestRealMinocMine(const std::string& dataDir) {
    Section("B: Minoc Mine 1, real atlas -- mouth to deep interior");

    world_atlas::Atlas atlas;
    std::string err;
    const std::string atlasPath = dataDir + "/revolution_atlas.txt";
    if (!atlas.Load(atlasPath.c_str(), &err)) {
        Check(false, "the generated atlas loads");
        std::printf("  (%s: %s)\n", atlasPath.c_str(), err.c_str());
        return;
    }

    const wm::Region* mine = atlas.RegionById("a_minoc_mine_1_1");
    Check(mine != nullptr, "the atlas still has Minoc Mine 1");
    if (!mine) return;

    Check(mine->kind == wm::RegionKind::Cave,
          "Minoc Mine 1 parses as a Cave-kind region -- DeeperMiningTarget "
          "gates on exactly this");
    Check(!mine->rects.empty(), "Minoc Mine 1 carries at least one RECT");

    // The recorded mouth: the PLACE minoc_mine_1_resource_area sits at
    // (2558,499), and the small connecting RECT (2556,501,2561,503) is the
    // doorway a character arrives through from town, to its south.
    const i32 mouthX = 2558, mouthY = 502;
    Check(mine->Contains(mouthX, mouthY),
          "the recorded mouth tile is inside the region's own rects (sanity "
          "on the fixture coordinates)");

    i32 deepX = 0, deepY = 0;
    const bool ok =
        world_atlas::DeeperMiningPoint(*mine, mouthX, mouthY, 20, &deepX,
                                       &deepY);
    Check(ok, "a deeper target exists from the recorded mouth");
    // Minoc Mine 1's big RECT is 2556,474,2581,500 -- north of the mouth.
    // Heading deeper must mean smaller y (further from the RECT=501-503
    // doorway), not a sideways wobble.
    Check(deepY < mouthY, "the deeper target moves north, away from the "
                         "doorway RECT");
    Check(mine->Contains(deepX, deepY),
          "the deeper target lands inside the region's own rects");
    Check(Chebyshev(mouthX, mouthY, deepX, deepY) <= 20,
          "a single advance is bounded, not a leap across the whole mine");

    // The Runner's own kMaxMineAdvances is 3: bounded 20-tile advances from
    // the mouth must actually reach the far interior (y<=474) within that
    // budget, proving the bound solves the real defect instead of just
    // looking plausible.
    i32 x = mouthX, y = mouthY;
    bool reachedDeepEdge = false;
    for (int i = 0; i < 3; ++i) {
        i32 nx = 0, ny = 0;
        if (!world_atlas::DeeperMiningPoint(*mine, x, y, 20, &nx, &ny)) break;
        x = nx;
        y = ny;
        if (y <= 474) { reachedDeepEdge = true; break; }
    }
    Check(reachedDeepEdge,
          "three bounded 20-tile advances from the mouth reach the mine's "
          "far interior (y<=474) -- kMaxMineAdvances is actually enough");
}

// ---------------------------------------------------------------------------
// C. Region containment, not centroid distance, decides "at the interior"
// (wave15 Elvar, 18:23-18:36: ping-ponged between a rock at (2577,486) and
// the interior anchor (2568,487) -- Runner::DoMine's atHomeMineInterior
// tested only Chebyshev distance to that one centroid point against
// kMineReach (6), and Minoc Mine 1's own RECT is 26x27 tiles, so a rock near
// its edge is routinely farther than 6 from the centre. Client::
// WithinMiningRegion (src/travel/ClientTravel.cpp) replaces that with region
// containment; this section proves the fix's premise against the real atlas
// without needing a live Client/socket).
// ---------------------------------------------------------------------------
void TestInteriorIsARegionNotAPoint(const std::string& dataDir) {
    Section("C: 'at the interior' is region containment, not centroid reach");

    world_atlas::Atlas atlas;
    std::string err;
    const std::string atlasPath = dataDir + "/revolution_atlas.txt";
    if (!atlas.Load(atlasPath.c_str(), &err)) {
        Check(false, "the generated atlas loads");
        return;
    }

    const wm::Region* mine = atlas.RegionById("a_minoc_mine_1_1");
    Check(mine != nullptr, "the atlas still has Minoc Mine 1");
    if (!mine) return;

    // The interior centroid Runner::DoMine actually walked to and logged
    // ("mine: ... resident going directly to the interior of Minoc Mine 1
    // at 2568,487"): the centre of the region's largest RECT.
    const wm::Rect* largest = &mine->rects[0];
    i64 largestArea = -1;
    for (const wm::Rect& rect : mine->rects) {
        const i64 area = (static_cast<i64>(rect.x2) - rect.x1 + 1) *
                         (static_cast<i64>(rect.y2) - rect.y1 + 1);
        if (area > largestArea) { largestArea = area; largest = &rect; }
    }
    const i32 centroidX = (largest->x1 + largest->x2) / 2;
    const i32 centroidY = (largest->y1 + largest->y2) / 2;

    // The rock the live session found and could never reach: real evidence,
    // not an invented fixture.
    const i32 rockX = 2577, rockY = 486;
    constexpr i32 kMineReach = 6;  // Runner.cpp's own constant

    Check(Chebyshev(centroidX, centroidY, rockX, rockY) > kMineReach,
          "the rock sits outside kMineReach of the centroid -- this is "
          "exactly the state that flipped atHomeMineInterior false and "
          "restarted the trip to the interior every tick");
    Check(mine->Contains(rockX, rockY),
          "but the rock is still inside the mine's own RECTs -- region "
          "containment (the fix) reports 'at the interior' where centroid "
          "distance (the bug) did not");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: mine_advance <data-dir>\n");
        return 2;
    }
    TestSyntheticGeometry();
    TestRealMinocMine(argv[1]);
    TestInteriorIsARegionNotAPoint(argv[1]);

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
