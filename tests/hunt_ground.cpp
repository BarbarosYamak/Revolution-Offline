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

    Section("patrol covers the whole Britain graveyard");
    {
        const auto* place = atlas.PlaceById("britain_graveyard_graveyard");
        const auto* region = atlas.RegionById("a_britain_graveyard_1");
        if (place && region) {
            const auto points = atlas.HuntingPatrol(*place);
            bool northwest = false, south = false;
            for (const auto& point : points) {
                Check(region->Contains(point.x, point.y), "patrol stays inside the cemetery");
                northwest |= point.x < 1350 && point.y < 1460;
                south |= point.y > 1494;
            }
            Check(northwest && south, "search reaches northwest and southern extension, beyond entrance");
        } else Check(false, "Britain graveyard geometry exists");
    }

    Section("Britain graveyard: strong tier is distinct from the weak band");
    {
        // Owner ruling 2026-09-06 (artifacts/graveyard_tier_split_2026-09-06.md):
        // the weak skeleton/zombie band and the strong undead (skeletal
        // knight, lich, lich lord) are separate, non-overlapping rings so a
        // novice fighter and a geared one can tell them apart. Coordinates
        // verified live via tools/world_query.py against the current world
        // save (--near 1385,1459/1452/1446 --type c_skeleton_knight/c_lich/
        // c_lich_lord, each n>=1 inside radius 6). DeriveGraveyardStrongTier
        // (AtlasGenMain.cpp) derives these from
        // Graveyards_spawns_felucca.scp, not the AREADEF, so they are a
        // second source alongside the existing per-region row.
        const wm::Place* weak = atlas.PlaceById("britain_graveyard_graveyard");
        const wm::Place* knights = atlas.PlaceById("britain_graveyard_knights");
        const wm::Place* lich = atlas.PlaceById("britain_graveyard_lich");
        const wm::Place* lichLord = atlas.PlaceById("britain_graveyard_lich_lord");
        Check(weak != nullptr, "the weak band's own PLACE row still exists");
        Check(knights != nullptr, "the strong knights ring is a distinct PLACE");
        Check(lich != nullptr, "the strong lich ring is a distinct PLACE");
        Check(lichLord != nullptr, "the strong lich lord ring is a distinct PLACE");

        if (weak && knights && lich && lichLord) {
            Check(knights->id != weak->id && lich->id != weak->id &&
                      lichLord->id != weak->id,
                  "strong-tier ids differ from the weak band's id");
            Check(knights->category == wm::PlaceCategory::Graveyard &&
                      lich->category == wm::PlaceCategory::Graveyard &&
                      lichLord->category == wm::PlaceCategory::Graveyard,
                  "strong-tier rings are still category Graveyard");

            Check(knights->position.x == 1385 && knights->position.y == 1459,
                  "knights ring matches the world_query-verified center");
            Check(lich->position.x == 1385 && lich->position.y == 1452,
                  "lich ring matches the world_query-verified center");
            Check(lichLord->position.x == 1385 && lichLord->position.y == 1446,
                  "lich lord ring matches the world_query-verified center");

            auto cheby = [](const wm::Point& a, const wm::Point& b) {
                const i32 dx = a.x > b.x ? a.x - b.x : b.x - a.x;
                const i32 dy = a.y > b.y ? a.y - b.y : b.y - a.y;
                return dx > dy ? dx : dy;
            };
            Check(cheby(knights->position, lich->position) > knights->radius &&
                      cheby(knights->position, lich->position) > lich->radius,
                  "knights ring and lich ring do not overlap");
            Check(cheby(lich->position, lichLord->position) > lich->radius &&
                      cheby(lich->position, lichLord->position) > lichLord->radius,
                  "lich ring and lich lord ring do not overlap");
            Check(cheby(weak->position, knights->position) > weak->radius,
                  "the weak band's own radius does not reach the nearest strong ring");
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
