// "What every new player already knows" (docs/LIFE_GATE_WAVE1.md theme 1).
//
// life::SeedNewbieKnowledge (include/uo/newbie_knowledge.h) is the pure,
// Client-free half of the wave-1 newborn-bootstrap fix: a fresh character's
// EMPTY Memory used to leave its very first errands asking the atlas cold --
// Draver/Lyra's first BANK goal failed "no banker in sight" because nothing
// had ever told them where a counter was, and Vorar's GATHER_LOGS spun on
// "no known source of that resource" with nothing proven and nothing hinted.
//
// Section A runs it against the REAL generated atlas -- a Minoc miner_smith
// and a Britain fisher, both professions the atlas can genuinely back (mining
// resource areas and fishing docks both exist) -- and against Britain's own
// gap: the atlas carries literally zero PLACE rows with resources=lumber
// anywhere on the map (grep -i "resources=lumber" data/revolution_atlas.txt
// finds nothing), so a lumberjack seed correctly comes back with a bank/
// healer/provisioner and NO log lead -- SeedNewbieKnowledge must not invent
// one; the walk-out-and-scan fallback (tests/guard_zone_advance.cpp) is what
// covers that case instead. Section B exercises the general mechanism
// (capping, radius scoping, idempotency, edge cases) against a small
// synthetic atlas that DOES carry a lumber place, since the real one cannot.

#include "uo/newbie_knowledge.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace uo;
using namespace uo::life;

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

const KnownPlace* FindPlace(const PersistentState& s, const char* kind) {
    for (const KnownPlace& p : s.memory.Places())
        if (p.kind == kind) return &p;
    return nullptr;
}

bool AnyResourceNear(const PersistentState& s, const char* resource, i32 x,
                    i32 y, i32 maxDist) {
    for (const KnownResourceSource& r : s.memory.Resources()) {
        if (r.resource != resource) continue;
        if (Chebyshev(r.x, r.y, x, y) <= maxDist) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// A. Real atlas.
// ---------------------------------------------------------------------------
void TestRealAtlas(const std::string& dataDir) {
    Section("A: real atlas");

    world_atlas::Atlas atlas;
    std::string err;
    const std::string atlasPath = dataDir + "/revolution_atlas.txt";
    if (!atlas.Load(atlasPath.c_str(), &err)) {
        Check(false, "the generated atlas loads");
        std::printf("  (%s: %s)\n", atlasPath.c_str(), err.c_str());
        return;
    }

    const i64 t0 = 1000;

    // -- A1: Minoc miner_smith -----------------------------------------
    {
        prof::Profession miner;
        miner.id = "miner_smith";
        miner.gathers = "ore";

        PersistentState state;
        SeedNewbieKnowledge(state, &miner, "Minoc", atlas, t0);

        // Minoc town centre, a_townMinoc (data/revolution_atlas.txt:953).
        const i32 minocX = 2466, minocY = 544;
        // Yew town centre, a_townYew (data/revolution_atlas.txt:757) -- a
        // Minoc smith must not have learned anything from over here.
        const i32 yewX = 546, yewY = 992;

        const KnownPlace* bank = FindPlace(state, "common_knowledge_bank");
        Check(bank != nullptr, "a Minoc miner_smith is seeded a bank");
        if (bank) {
            Check(Chebyshev(bank->x, bank->y, minocX, minocY) <=
                      kNewbieKnowledgeRadius,
                  "the seeded bank is within kNewbieKnowledgeRadius of Minoc");
            Check(Chebyshev(bank->x, bank->y, yewX, yewY) > 300,
                  "the seeded bank is nowhere near Yew");
        }

        const KnownPlace* healer = FindPlace(state, "common_knowledge_healer");
        Check(healer != nullptr, "a Minoc miner_smith is seeded a healer");

        const KnownPlace* prov =
            FindPlace(state, "common_knowledge_provisioner");
        Check(prov != nullptr, "a Minoc miner_smith is seeded a provisioner");

        Check(AnyResourceNear(state, "ore", minocX, minocY,
                              kNewbieKnowledgeRadius),
              "a Minoc miner_smith is seeded an ore resource-area lead near "
              "Minoc (the mine, the mining camp, or the miners' guild)");
        Check(!AnyResourceNear(state, "ore", yewX, yewY, 300),
              "no ore lead was seeded anywhere near Yew");

        // Minoc has exactly three mining resource areas within
        // kNewbieKnowledgeRadius of the town centre (the mine, the mining
        // camp, the miners' guild) -- so kNewbieResourceHints (3) is not
        // even exercised as a cap here; Section B proves the cap itself
        // against a synthetic atlas with more candidates than that.
        i32 oreHints = 0;
        for (const KnownResourceSource& r : state.memory.Resources())
            if (r.resource == "ore") ++oreHints;
        Check(oreHints <= kNewbieResourceHints,
              "no more than kNewbieResourceHints ore leads were seeded");

        // Every hint is marked as such, never as an earned stand.
        for (const KnownResourceSource& r : state.memory.Resources())
            Check(r.hinted && r.successes == 0,
                  "a seeded resource lead is hinted, not proven");
    }

    // -- A2: Britain fisher ----------------------------------------------
    {
        prof::Profession fisher;
        fisher.id = "fisher";
        fisher.gathers = "fish";

        PersistentState state;
        SeedNewbieKnowledge(state, &fisher, "Britain", atlas, t0);

        const i32 britX = 1495, britY = 1629;   // a_townBritain (line 902)

        Check(FindPlace(state, "common_knowledge_bank") != nullptr,
              "a Britain fisher is seeded a bank");
        Check(FindPlace(state, "common_knowledge_healer") != nullptr,
              "a Britain fisher is seeded a healer");
        Check(FindPlace(state, "common_knowledge_provisioner") != nullptr,
              "a Britain fisher is seeded a provisioner");
        Check(AnyResourceNear(state, "fish", britX, britY,
                              kNewbieKnowledgeRadius),
              "a Britain fisher is seeded a fishing lead near Britain (the "
              "docks)");
    }

    // -- A3: Britain lumberjack -- THE ATLAS GAP, DOCUMENTED --------------
    //
    // Confirmed here so this suite fails loudly (a GOOD failure) the day
    // atlasgen's own DeriveForests (AtlasGenMain.cpp:753-820) actually ships
    // a lumber-tagged place: at that point this assertion should flip and
    // Vorar's stand-seeding should be restored alongside the walk-out-and-
    // scan fallback (world/GuardZoneAdvance.h, tests/guard_zone_advance.cpp)
    // rather than instead of it.
    {
        Check(atlas.NearestPlaceWithResource(wm::ResourceKind::Lumber, 1495,
                                             1629) == nullptr,
              "the shipped atlas still has no lumber-tagged resource area "
              "anywhere on the map");

        prof::Profession lumberjack;
        lumberjack.id = "lumberjack_swordsman";
        lumberjack.gathers = "logs";

        PersistentState state;
        SeedNewbieKnowledge(state, &lumberjack, "Britain", atlas, t0);

        Check(FindPlace(state, "common_knowledge_bank") != nullptr,
              "a Britain lumberjack still gets a seeded bank even with no "
              "log lead available");
        bool anyLogHint = false;
        for (const KnownResourceSource& r : state.memory.Resources())
            if (r.resource == "logs") anyLogHint = true;
        Check(!anyLogHint,
              "SeedNewbieKnowledge does NOT invent a log stand when the "
              "atlas has none -- a fabricated hint would be worse than no "
              "hint at all (owner: trees are a walk-out-and-scan behaviour, "
              "not a seeded place)");
    }

    // -- A4: edge cases ----------------------------------------------------
    {
        prof::Profession miner;
        miner.id = "miner_smith";
        miner.gathers = "ore";

        PersistentState empty1;
        SeedNewbieKnowledge(empty1, &miner, "", atlas, t0);
        Check(empty1.memory.Places().empty() &&
                  empty1.memory.Resources().empty(),
              "an empty home city seeds nothing at all");

        PersistentState empty2;
        SeedNewbieKnowledge(empty2, &miner, "Nowhereville", atlas, t0);
        Check(empty2.memory.Places().empty() &&
                  empty2.memory.Resources().empty(),
              "a home city the atlas cannot find seeds nothing at all");

        PersistentState noProf;
        SeedNewbieKnowledge(noProf, nullptr, "Minoc", atlas, t0);
        Check(FindPlace(noProf, "common_knowledge_bank") != nullptr,
              "a null profession (an older character predating the "
              "catalogue) still gets the home bank");
        Check(noProf.memory.Resources().empty(),
              "a null profession gets no resource lead -- there is no "
              "`gathers` to ask the atlas about");

        // Idempotency: calling it twice must not duplicate entries.
        PersistentState twice;
        SeedNewbieKnowledge(twice, &miner, "Minoc", atlas, t0);
        const usize placesAfterFirst = twice.memory.Places().size();
        const usize resourcesAfterFirst = twice.memory.Resources().size();
        SeedNewbieKnowledge(twice, &miner, "Minoc", atlas, t0 + 5000);
        Check(twice.memory.Places().size() == placesAfterFirst,
              "seeding twice does not duplicate places");
        Check(twice.memory.Resources().size() == resourcesAfterFirst,
              "seeding twice does not duplicate resource leads");
    }
}

// ---------------------------------------------------------------------------
// B. Synthetic atlas -- proves the general mechanism the real atlas cannot
//    (a lumber place, and more resource-area candidates than the cap).
// ---------------------------------------------------------------------------
void TestSyntheticAtlas() {
    Section("B: synthetic atlas -- capping, scoping, hint-vs-proven");

    // A minimal, hand-built atlas: one town, a bank inside it, and five
    // lumber resource areas at increasing distance -- two beyond
    // kNewbieKnowledgeRadius, three within it (more than
    // kNewbieResourceHints, to prove the cap actually caps).
    const std::string text =
        "MAP\t0\t7168\t4096\n"
        "REGION\ta_test_town\ttown\t1\t1000\t1000\t0\tTestTown\tTestTown\n"
        "RECT\ta_test_town\t950\t950\t1050\t1050\n"
        "PLACE\ttest_bank\tbank\ta_test_town\t1000\t1010\t0\t5\tbanker\t\t"
        "Test banker\n"
        "PLACE\twood_near_1\tresource_area\t\t1050\t1000\t0\t20\t\tlumber\t"
        "Near Wood 1\n"
        "PLACE\twood_near_2\tresource_area\t\t1000\t1080\t0\t20\t\tlumber\t"
        "Near Wood 2\n"
        "PLACE\twood_near_3\tresource_area\t\t1150\t1000\t0\t20\t\tlumber\t"
        "Near Wood 3\n"
        "PLACE\twood_far_1\tresource_area\t\t2000\t1000\t0\t20\t\tlumber\t"
        "Far Wood 1\n"
        "PLACE\twood_far_2\tresource_area\t\t1000\t3000\t0\t20\t\tlumber\t"
        "Far Wood 2\n";

    world_atlas::Atlas atlas;
    std::string err;
    Check(atlas.LoadFromText(text.c_str(), &err), "the synthetic atlas parses");
    if (!err.empty()) std::printf("  (%s)\n", err.c_str());

    prof::Profession lumberjack;
    lumberjack.id = "lumberjack_swordsman";
    lumberjack.gathers = "logs";

    const i64 t0 = 1000;
    PersistentState state;
    SeedNewbieKnowledge(state, &lumberjack, "TestTown", atlas, t0);

    Check(FindPlace(state, "common_knowledge_bank") != nullptr,
          "the synthetic town's bank is seeded");

    int logHints = 0;
    bool sawFar = false;
    for (const KnownResourceSource& r : state.memory.Resources()) {
        if (r.resource != "logs") continue;
        ++logHints;
        if (Chebyshev(r.x, r.y, 1000, 1000) > kNewbieKnowledgeRadius)
            sawFar = true;
    }
    Check(logHints == kNewbieResourceHints,
          "the resource-area cap actually caps: three near candidates exist "
          "and exactly kNewbieResourceHints were seeded, not all three plus "
          "the far ones");
    Check(!sawFar,
          "neither far-away wood (well outside kNewbieKnowledgeRadius) was "
          "seeded -- home scoping actually excludes them");

    // A hint never outranks a proven stand: prove one of the seeds, then
    // confirm BestProvenResource -- what DoGatherLogs asks first -- returns
    // it in preference to the two still-unproven hints.
    const KnownResourceSource* someHint =
        state.memory.BestHint("logs", 1000, 1000, t0);
    Check(someHint != nullptr, "a hint is available to prove");
    if (someHint) {
        state.memory.NoteResource("logs", someHint->x, someHint->y, 0, true,
                                  t0 + 1000);
        const KnownResourceSource* proven =
            state.memory.BestProvenResource("logs", 1000, 1000, t0 + 2000);
        Check(proven != nullptr &&
                  proven->x == someHint->x && proven->y == someHint->y,
              "once a seeded lead actually pays out, BestProvenResource "
              "picks it over the remaining unproven hints");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: newbie_knowledge <data-dir>\n");
        return 2;
    }
    TestRealAtlas(argv[1]);
    TestSyntheticAtlas();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
