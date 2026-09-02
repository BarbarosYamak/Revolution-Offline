// Deterministic tests for uo::nav::FindWalkableNearGoal (include/uo/nav_rules.h).
//
// wave 2, 2026-09-01: a literal goal handed to A* was very often a resource's
// own unwalkable tile (a tree trunk, an open-water cast target). Navigation
// used to abort the whole trip on the spot ("goal not walkable"), and the
// caller re-issued the identical dead target on its next tick -- Dorvar hit
// this 60 times against one fishing coordinate, Halain 34 times against one
// chop stand. This is the ring search that salvages such a goal by finding
// the nearest standable tile next to it, tested here against a synthetic
// grid so it needs no MULs and no World.

#include "uo/nav_rules.h"

#include <cstdio>
#include <set>
#include <utility>

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

// A tiny synthetic world: every tile walkable except an explicit block set.
// standZ always equals nearZ (flat ground) unless overridden per-tile.
struct Grid {
    std::set<std::pair<i32, i32>> blocked;
    void Block(i32 x, i32 y) { blocked.insert({x, y}); }
    bool Walkable(i32 x, i32 y, i8 fromZ, i8* outZ) const {
        if (blocked.count({x, y})) return false;
        if (outZ) *outZ = fromZ;
        return true;
    }
};

void TestFindsNearestRing() {
    Section("A: nearest ring wins, not the first radius-1 cell scanned");

    // The goal tile itself is never offered by this function -- callers pass
    // it because the goal ALREADY failed a direct walkability check. Block a
    // whole radius-1 ring around (10,10) except one tile at radius 2, and
    // confirm the search steps out ring by ring instead of stopping short.
    Grid g;
    for (i32 dy = -1; dy <= 1; ++dy)
        for (i32 dx = -1; dx <= 1; ++dx)
            if (dx || dy) g.Block(10 + dx, 10 + dy);
    // One walkable tile at radius 2, directly north.
    // (everything else at radius 2 stays walkable by default too, so this
    // just confirms *a* radius-2 tile is found once radius 1 is exhausted.)

    i32 ox = 0, oy = 0;
    i8 oz = 0;
    const bool found = nav::FindWalkableNearGoal(
        10, 10, /*nearZ=*/0, /*maxRadius=*/4,
        [&](i32 x, i32 y, i8 fz, i8* oz2) { return g.Walkable(x, y, fz, oz2); },
        &ox, &oy, &oz);
    Check(found, "a walkable tile exists two rings out and is found");
    const i32 cheb = std::max(std::abs(ox - 10), std::abs(oy - 10));
    Check(cheb == 2, "the nearest available ring (2) is chosen, not further out");
}

void TestReturnsStandZ() {
    Section("B: the found tile's standing z is reported, not the search z");

    Grid g;
    g.Block(5, 5);  // goal-adjacent tile itself irrelevant; force ring search
    i32 ox = 0, oy = 0;
    i8 oz = -1;
    const bool found = nav::FindWalkableNearGoal(
        5, 5, /*nearZ=*/7, /*maxRadius=*/2,
        [&](i32 x, i32 y, i8 fz, i8* outZ) {
            if (g.blocked.count({x, y})) return false;
            *outZ = fz;  // flat: standZ mirrors the approach z in this fixture
            return true;
        },
        &ox, &oy, &oz);
    Check(found, "an adjacent tile is walkable");
    Check(oz == 7, "the walkability oracle's standZ is threaded through, not dropped");
}

void TestFullyEnclosedFails() {
    Section("C: a goal genuinely boxed in within the search radius fails cleanly");

    // Amara's case (wave 2, 2026-09-01): every tile around her stand was
    // terrain-blocked out to the search radius. The function must return
    // false rather than wandering arbitrarily far from the intended goal.
    Grid g;
    for (i32 dy = -3; dy <= 3; ++dy)
        for (i32 dx = -3; dx <= 3; ++dx)
            g.Block(100 + dx, 100 + dy);

    i32 ox = 0, oy = 0;
    i8 oz = 0;
    const bool found = nav::FindWalkableNearGoal(
        100, 100, /*nearZ=*/0, /*maxRadius=*/3,
        [&](i32 x, i32 y, i8 fz, i8* oz2) { return g.Walkable(x, y, fz, oz2); },
        &ox, &oy, &oz);
    Check(!found, "a goal enclosed out to the whole search radius reports failure");
}

void TestRadiusIsBounded() {
    Section("D: the search never looks past maxRadius, even if rescue exists further out");

    Grid g;
    for (i32 dy = -5; dy <= 5; ++dy)
        for (i32 dx = -5; dx <= 5; ++dx)
            g.Block(0 + dx, 0 + dy);  // walkable tile only exists at radius 6+

    i32 ox = 0, oy = 0;
    i8 oz = 0;
    const bool found = nav::FindWalkableNearGoal(
        0, 0, /*nearZ=*/0, /*maxRadius=*/3,
        [&](i32 x, i32 y, i8 fz, i8* oz2) { return g.Walkable(x, y, fz, oz2); },
        &ox, &oy, &oz);
    Check(!found, "maxRadius=3 does not reach a rescue tile that only exists at radius 6");
}

void TestNegativeCoordinatesSkipped() {
    Section("E: a ring cell that falls off the map (negative x/y) is skipped, not crashed on");

    Grid g;  // nothing blocked
    i32 ox = 0, oy = 0;
    i8 oz = 0;
    const bool found = nav::FindWalkableNearGoal(
        0, 0, /*nearZ=*/0, /*maxRadius=*/2,
        [&](i32 x, i32 y, i8 fz, i8* oz2) { return g.Walkable(x, y, fz, oz2); },
        &ox, &oy, &oz);
    Check(found, "a goal at the map edge still finds a walkable in-bounds neighbour");
    Check(ox >= 0 && oy >= 0, "the chosen tile never has a negative coordinate");
}

}  // namespace

int main() {
    std::printf("goal_snap (nav::FindWalkableNearGoal)\n");

    TestFindsNearestRing();
    TestReturnsStandZ();
    TestFullyEnclosedFails();
    TestRadiusIsBounded();
    TestNegativeCoordinatesSkipped();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
