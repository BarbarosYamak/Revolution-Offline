// Early-hunting-grounds resolver (memory: early-hunting-grounds.md, owner
// 2026-08-31): "hunting ground can be graveyards for early hunting, brit
// sewers maybe, but they need gear too."
//
// world_atlas::Atlas::NearestHuntingGround is the pure, Client-free half of
// that rule -- picking WHERE a fighter with no fight in reach should walk
// to. It is a named wrapper over NearestPlaceOfCategory(Graveyard, ...)
// today; see the comment on the declaration (world/Atlas.h) for why Britain's
// sewers (a_brit_sewers_1, a `dungeon`-category REGION, not a graveyard
// PLACE) are not folded into it yet.
//
// This links only uo_world -- no Client, no navgrid, no route planner -- the
// m25_world.cpp style, but against the REAL generated atlas (argv[1] = the
// data directory) so a regenerated atlas that drops Britain's or Yew's
// graveyard fails this suite rather than going unnoticed.

#include "world/Atlas.h"

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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: hunt_ground <data-dir>\n");
        return 2;
    }

    world_atlas::Atlas atlas;
    std::string err;
    const std::string atlasPath = std::string(argv[1]) + "/revolution_atlas.txt";
    if (!atlas.Load(atlasPath.c_str(), &err)) {
        Check(false, "the generated atlas loads");
        std::printf("  (%s: %s)\n", atlasPath.c_str(), err.c_str());
        std::printf("%d checks, %d failed\n", g_checks, g_failures);
        return g_failures ? 1 : 0;
    }

    Section("nearest graveyard, from Yew");
    {
        // Yew town centre (a_townYew, data/revolution_atlas.txt:757).
        const wm::Place* p = atlas.NearestHuntingGround(546, 992);
        Check(p != nullptr, "a hunting ground is found near Yew");
        if (p) {
            Check(p->id == "yew_graveyard_graveyard",
                  "it is Yew Graveyard, not some farther cemetery");
            Check(p->category == wm::PlaceCategory::Graveyard,
                  "the resolved place is actually category Graveyard");
        }
    }

    Section("nearest graveyard, from Britain (Britain graveyard first)");
    {
        // Britain town centre (a_townBritain, data/revolution_atlas.txt:902).
        const wm::Place* p = atlas.NearestHuntingGround(1495, 1629);
        Check(p != nullptr, "a hunting ground is found near Britain");
        if (p) {
            Check(p->id == "britain_graveyard_graveyard",
                  "it is Britain Graveyard -- the owner's named first "
                  "hunting ground -- not some farther cemetery");
        }
    }

    Section("refuses when the atlas has nothing in range");
    {
        // Yew Graveyard is ~178 Chebyshev tiles from Yew's own town centre
        // (724,1134 vs 546,992) -- comfortably outside a 10-tile leash, and
        // nothing else of category Graveyard is anywhere near Yew either.
        const wm::Place* p = atlas.NearestHuntingGround(546, 992, 10);
        Check(p == nullptr,
              "no hunting ground within 10 tiles of Yew -- refused, not a "
              "far-away graveyard reported as reachable");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
