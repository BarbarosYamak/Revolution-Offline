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

    Section("a hunting patrol stays inside its place's own RECT");
    {
        // Regression (wave30_bandage20_20260907/triage.md; consoles Calar/
        // Nairdris/Baelos/Kharos): a tier=novice patrol on the weak band used
        // to sweep the WHOLE region -- every RECT of a_britain_graveyard_1 --
        // which reached straight into the strong undead's ring and killed
        // four characters. The weak band's own anchor (1384,1492) and the
        // strong rings (1385, y1446-1459) are all inside the SAME RECT
        // (1336,1443)-(1391,1494) per the atlas / raw AREADEF
        // (map0_areas.scp:2868-2875); the region's other RECT,
        // (1336,1494)-(1376,1511), is unrelated ground south of the yard wall
        // with no spawns in it. So a patrol must be scoped by the place's own
        // ring (position +/- radius), not merely "which RECT" -- two places
        // sharing one RECT still need disjoint patrols.
        const auto* weak = atlas.PlaceById("britain_graveyard_graveyard");
        const auto* knights = atlas.PlaceById("britain_graveyard_knights");
        const auto* lich = atlas.PlaceById("britain_graveyard_lich");
        const auto* lichLord = atlas.PlaceById("britain_graveyard_lich_lord");
        const auto* region = atlas.RegionById("a_britain_graveyard_1");
        Check(region && region->rects.size() == 2,
              "the yard AREADEF still carries its two RECTs");

        if (weak && knights && lich && lichLord && region) {
            const auto weakPts = atlas.HuntingPatrol(*weak);
            Check(!weakPts.empty(), "the weak band resolves a patrol");
            for (const auto& point : weakPts) {
                Check(region->Contains(point.x, point.y),
                      "weak patrol stays inside the yard region");
                // The strong rings top out at y=1462 (knights r3 centered
                // 1459); a novice patrol must never reach that far north.
                Check(point.y > 1462,
                      "weak-band patrol point stays south of the strong rings");
            }

            for (const wm::Place* ring : {knights, lich, lichLord}) {
                const auto pts = atlas.HuntingPatrol(*ring);
                Check(!pts.empty(), "a strong ring resolves a patrol");
                for (const auto& point : pts) {
                    Check(region->Contains(point.x, point.y),
                          "strong-ring patrol stays inside the yard region");
                    const i32 dx = point.x > ring->position.x
                                       ? point.x - ring->position.x
                                       : ring->position.x - point.x;
                    const i32 dy = point.y > ring->position.y
                                       ? point.y - ring->position.y
                                       : ring->position.y - point.y;
                    Check(dx <= ring->radius && dy <= ring->radius,
                          "strong-ring patrol point stays within that ring's "
                          "own radius, not the whole yard");
                }
            }
        } else {
            Check(false, "Britain graveyard tier geometry exists");
        }
    }

    Section("a single-RECT region's patrol is unchanged");
    {
        // A place whose ring comfortably covers its one-and-only RECT (radius
        // far bigger than the RECT itself) must still sweep the whole thing,
        // exactly as before this fix -- the new radius clip must never shrink
        // coverage for the common, single-RECT case.
        world_atlas::Atlas single;
        std::string err;
        const char* text =
            "MAP\t0\t7168\t4096\n"
            "REGION\ta_solo\ttown\t0\t100\t100\t0\tTest\tSolo Yard\n"
            "RECT\ta_solo\t80\t80\t130\t130\n"
            "PLACE\tsolo_yard\tgraveyard\ta_solo\t100\t100\t0\t60\t\t\tSolo Yard\n";
        Check(single.LoadFromText(text, &err), "single-RECT fixture parses");
        if (err.size()) std::printf("  (%s)\n", err.c_str());

        const auto* place = single.PlaceById("solo_yard");
        Check(place != nullptr, "solo place resolves");
        if (place) {
            const auto pts = single.HuntingPatrol(*place);
            bool northwest = false, southeast = false;
            for (const auto& point : pts) {
                Check(point.x >= 83 && point.x <= 127 && point.y >= 83 &&
                          point.y <= 127,
                      "lane stays within the 3-tile margin of the one RECT");
                northwest |= point.x < 90 && point.y < 90;
                southeast |= point.x > 120 && point.y > 120;
            }
            Check(northwest && southeast,
                  "coverage still reaches both corners of the single RECT");
        }
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

    Section("tiers: a novice never resolves a strong ring, a gated one can");
    {
        // The split put lethal undead inside the same Graveyard category as
        // the weak band (chardef evidence: artifacts/hunt_tier_gate_2026-09-06
        // .md -- c_skeleton_knight DAM 18,43 vs c_skeleton DAM 3,7), so the
        // plain resolver has to mean "weak tier" and the strong tier has to be
        // asked for by name.
        for (const wm::Place& p : atlas.Places()) {
            if (p.category != wm::PlaceCategory::Graveyard) continue;
            const bool band = p.id.size() > 10 &&
                              p.id.compare(p.id.size() - 10, 10, "_graveyard") == 0;
            Check(world_atlas::HuntTierOf(p) ==
                      (band ? world_atlas::HuntTier::Weak
                            : world_atlas::HuntTier::Strong),
                  "every graveyard row's tier follows its id, band or ring");
        }

        // Standing ON the lich lord ring, the untiered call still walks the
        // novice out to the weak band -- this is the case that killed people.
        const wm::Place* fromRing = atlas.NearestHuntingGround(1385, 1446);
        Check(fromRing != nullptr, "a weak band is found from inside the rings");
        if (fromRing) {
            Check(fromRing->id == "britain_graveyard_graveyard",
                  "from the lich lord ring the untiered resolver still returns "
                  "the weak band, not the ring it is standing in");
            Check(world_atlas::HuntTierOf(*fromRing) == world_atlas::HuntTier::Weak,
                  "the untiered resolver is weak-tier by definition");
        }

        // And from Britain proper.
        const wm::Place* novice = atlas.NearestHuntingGround(1495, 1629);
        Check(novice && novice->id == "britain_graveyard_graveyard",
              "a novice near Britain still gets the weak band");

        // A character that clears the gate can ask for the hard part, and gets
        // a real strong ring rather than nothing.
        const wm::Place* strong = atlas.NearestHuntingGroundOfTier(
            world_atlas::HuntTier::Strong, 1495, 1629);
        Check(strong != nullptr, "a strong-tier ground is resolvable at all");
        if (strong) {
            Check(world_atlas::HuntTierOf(*strong) == world_atlas::HuntTier::Strong,
                  "the strong-tier resolver returns a strong ring");
            Check(strong->id != "britain_graveyard_graveyard",
                  "the strong-tier answer is not the weak band");
            Check(strong->id.rfind("britain_graveyard_", 0) == 0,
                  "and it is Britain's own ring, not another city's");
        }

        // The leash still applies per tier: nothing strong near Yew's newbie
        // yard, which has no strong ring at all ("NEWBIE YARD, KEEP IT WEAK",
        // Graveyards_spawns_felucca.scp).
        Check(atlas.NearestHuntingGroundOfTier(world_atlas::HuntTier::Strong,
                                               724, 1134, 40) == nullptr,
              "no strong ring within 40 tiles of the Yew newbie yard");
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
