// Walking a guarded town's own boundary outward (M-fix11, owner diagnosis
// 2026-08-31, watching a newborn Vorar spin on GATHER_LOGS in Britain: "if he
// left the guard zone at Britain he would see farmable trees"). The atlas
// backs this reading up literally -- data/revolution_atlas.txt carries zero
// PLACE rows with resources=lumber anywhere on the map, so
// TravelToResource(Lumber) can never succeed no matter how a lumberjack's
// memory is seeded. Walking out of town and scanning is not a workaround for
// a slow atlas here; for this one resource it is the only path that works.
//
// world_atlas::StepOutOfGuardedRegion (src/world/GuardZoneAdvance.h) is the
// pure geometry DoGatherLogs' no-lead fallback uses: the mirror image of
// DoMine's DeeperMiningPoint (tests/mine_advance.cpp) -- step toward the
// NEAREST edge of whichever RECT holds the character, a little past it, by
// at most a bounded step -- rather than toward a region's own recorded
// centre, which for a town is its plaza and usually the LONG way out.
//
// Section A proves the geometry against small, fully-controlled synthetic
// rects. Section B is the regression guard: the REAL generated atlas must
// still resolve Britain as a guarded Town region, and stepping out from
// several points inside it must actually clear its own RECTs within a small,
// bounded number of advances.

#include "world/Atlas.h"
#include "world/GuardZoneAdvance.h"

#include <cstdio>
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
    Section("A: StepOutOfGuardedRegion over a hand-built town");

    wm::Region town;
    town.id = "a_test_town";
    town.kind = wm::RegionKind::Town;
    town.flags.guarded = true;
    town.center = wm::Point{100, 100, 0, 0};
    town.rects.push_back(wm::Rect{50, 50, 150, 150});   // 100x100 square

    // Standing 10 tiles from the west wall: that is the nearest edge (10),
    // versus 90 to the east, 10... wait, choose a point closer to one edge
    // than the others to make the "nearest" pick unambiguous.
    {
        // (55,100): west edge 5 away, east 95, north 50, south 50.
        i32 ox = 0, oy = 0;
        const bool ok =
            world_atlas::StepOutOfGuardedRegion(town, 55, 100, 20, &ox, &oy);
        Check(ok, "a step out exists from just inside the west wall");
        Check(oy == 100, "the step moves along x only -- the west wall is "
                        "the nearest edge, not a diagonal");
        Check(ox < 50, "the step actually clears the RECT's west edge (a "
                      "small margin past x1=50), not stopping exactly on it");
        Check(!town.Contains(ox, oy),
              "the stepped point reads as OUTSIDE the region");
        Check(Chebyshev(55, 100, ox, oy) <= 20,
              "the step is bounded by the step limit");
    }

    // Standing near the CENTRE: no edge is obviously nearest, but the
    // function must still pick exactly one (ties broken deterministically by
    // check order) and move toward it, not toward the recorded centre
    // itself (which is where the character already is).
    {
        i32 ox = 0, oy = 0;
        const bool ok = world_atlas::StepOutOfGuardedRegion(town, 100, 100,
                                                             20, &ox, &oy);
        Check(ok, "a step out exists from the centre");
        Check(ox != 100 || oy != 100,
              "the step actually moves the character");
    }

    // A step limit too small to clear the wall: the character moves toward
    // it but does not overshoot past where the limit allows -- proving
    // repeated calls are needed for a wide region, the same bounded-advance
    // shape DeeperMiningPoint uses for a deep mine.
    {
        // (55,100): needs 5 (to the wall) + 3 (margin) = 8 tiles to clear;
        // bound it to 4 so it cannot.
        i32 ox = 0, oy = 0;
        const bool ok =
            world_atlas::StepOutOfGuardedRegion(town, 55, 100, 4, &ox, &oy);
        Check(ok, "a bounded step exists even when it cannot clear the wall "
                 "in one hop");
        Check(town.Contains(ox, oy),
              "a step too short to clear the wall lands still inside -- it "
              "moved toward the exit without leaping past the bound");
        Check(ox == 51, "the short step covers exactly the step limit (55-4)");
    }

    // CONVERGENCE: repeated bounded steps from the same starting point,
    // recomputed from wherever the previous step landed (as DoGatherLogs
    // does tick over tick), must actually clear the region within a bounded
    // number of advances rather than orbiting inside it.
    {
        i32 x = 100, y = 100;   // dead centre: 50 tiles to every wall
        bool cleared = false;
        int steps = 0;
        for (; steps < 6; ++steps) {
            i32 nx = 0, ny = 0;
            if (!world_atlas::StepOutOfGuardedRegion(town, x, y, 20, &nx, &ny))
                break;
            x = nx;
            y = ny;
            if (!town.Contains(x, y)) { cleared = true; break; }
        }
        Check(cleared,
              "repeated bounded 20-tile steps from the centre of a 100x100 "
              "town actually clear its RECT");
        Check(steps <= 3,
              "clearing takes a small, bounded number of steps (50 tiles to "
              "the wall over a 20-tile step limit is 3, not an open-ended "
              "wander)");
    }

    // Already outside every RECT: nothing to step out of.
    {
        i32 ox = 0, oy = 0;
        const bool ok =
            world_atlas::StepOutOfGuardedRegion(town, 500, 500, 20, &ox, &oy);
        Check(!ok, "a point outside every RECT has nothing to step out of");
    }

    // A region with no RECTs at all (defensive).
    {
        wm::Region empty;
        empty.kind = wm::RegionKind::Town;
        empty.flags.guarded = true;
        i32 ox = 0, oy = 0;
        const bool ok =
            world_atlas::StepOutOfGuardedRegion(empty, 0, 0, 20, &ox, &oy);
        Check(!ok, "a RECT-less region has no edge to step toward");
    }
}

// ---------------------------------------------------------------------------
// B. Real atlas: Britain (a_townBritain).
// ---------------------------------------------------------------------------
void TestRealBritain(const std::string& dataDir) {
    Section("B: Britain, real atlas -- guarded town to open ground");

    world_atlas::Atlas atlas;
    std::string err;
    const std::string atlasPath = dataDir + "/revolution_atlas.txt";
    if (!atlas.Load(atlasPath.c_str(), &err)) {
        Check(false, "the generated atlas loads");
        std::printf("  (%s: %s)\n", atlasPath.c_str(), err.c_str());
        return;
    }

    // The gap StepOutOfGuardedRegion exists to route around: confirmed here
    // so this suite fails loudly (a GOOD failure) the day atlasgen's own
    // DeriveForests (AtlasGenMain.cpp:753-820) actually ships lumber places,
    // at which point DoGatherLogs' ordinary atlas/hint path should be
    // preferred again and this whole fallback revisited.
    const wm::Place* anyLumber =
        atlas.NearestPlaceWithResource(wm::ResourceKind::Lumber, 1495, 1629);
    Check(anyLumber == nullptr,
          "the shipped atlas still has no lumber-tagged resource area "
          "anywhere on the map -- this is why the walk-out-and-scan "
          "fallback exists at all; if this now fails, the atlas gained "
          "forests and the fallback should be revisited");

    const wm::Region* britain = atlas.RegionById("a_townBritain");
    Check(britain != nullptr, "the atlas still has Britain");
    if (!britain) return;

    Check(britain->kind == wm::RegionKind::Town, "Britain parses as a Town");
    Check(britain->flags.guarded,
          "Britain is guarded -- StepOutOfGuardZone gates on exactly this");
    Check(!britain->rects.empty(), "Britain carries at least one RECT");

    // Britain's own town centre (REGION P=), comfortably inside its own
    // RECT (1410,1517)-(1690,1777).
    const i32 startX = 1495, startY = 1629;
    Check(britain->Contains(startX, startY),
          "the town centre sits inside Britain's own rects (sanity on the "
          "fixture coordinates)");

    i32 x = startX, y = startY;
    bool cleared = false;
    int steps = 0;
    for (; steps < 10; ++steps) {
        i32 nx = 0, ny = 0;
        if (!world_atlas::StepOutOfGuardedRegion(*britain, x, y, 40, &nx, &ny))
            break;
        Check(Chebyshev(x, y, nx, ny) <= 40,
              "each advance is bounded by the step limit");
        x = nx;
        y = ny;
        if (!britain->Contains(x, y)) { cleared = true; break; }
    }
    Check(cleared,
          "bounded 40-tile steps from Britain's town centre actually clear "
          "every one of its RECTs within ten advances");
    if (cleared) {
        Check(atlas.RegionAt(x, y) != britain,
              "the point RegionAt resolves to after clearing is no longer "
              "Britain itself");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: guard_zone_advance <data-dir>\n");
        return 2;
    }
    TestSyntheticGeometry();
    TestRealBritain(argv[1]);

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
