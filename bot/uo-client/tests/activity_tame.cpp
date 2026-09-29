// Taming: the two facts DoTameAnimal got wrong on 2026-09-02 (Rhea, Taming
// 50.0, artifacts/wave_2026-09-02_verdict.md section d).
//
// A. NO JUDGEMENT BEFORE THE NAMES ARE KNOWN. The handler filters nearby
//    mobiles by name, and a name only exists after the 0x98 AllNames query
//    ActionScanMobiles issues. Judging in the same tick as the arrival read
//    three pastures of sheep as deserted 60 ms after `travel_done`.
//
// B. THE NEAREST FLOCK COMES FIRST. The pastures themselves are already read
//    from the world save (data/revolution_pastures.tsv); they were walked in
//    file order, from a compiled-in trio, regardless of where the character
//    stood.

#include "uo/activities/tame.h"

#include <cstdio>
#include <vector>

using namespace uo::life;

namespace {
// Stands in for Runner.cpp's own pasture row: x, y, count are all the
// ordering needs.
struct Row { int x = 0, y = 0, count = 0; };
}  // namespace

static int failures = 0;
static void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "[ ok ]" : "[FAIL]", what);
    if (!ok) ++failures;
}

int main() {
    // --- A. the seam ------------------------------------------------------
    {
        TameScanSight s;                       // nothing asked yet
        Check(!MayJudgeEmpty(s, 2000),
              "no scan issued -> may not call the spot empty");

        s.scanIssued = true;
        s.namesPending = true;
        s.msSinceScan = 5000;
        Check(!MayJudgeEmpty(s, 2000),
              "names still in flight -> may not call the spot empty");

        s.namesPending = false;
        s.msSinceScan = 60;                    // the Rhea failure, exactly
        Check(!MayJudgeEmpty(s, 2000),
              "60 ms after the scan -> still too early to judge");

        s.msSinceScan = 2000;
        Check(MayJudgeEmpty(s, 2000),
              "names in and settled -> the emptiness verdict is allowed");
    }

    // --- B. nearest flock first -------------------------------------------
    {
        // The real rows of data/revolution_pastures.tsv, map 0.
        std::vector<Row> p = {{572, 1096, 15}, {677, 1177, 15},
                              {681, 945, 15},  {5159, 3915, 7}};

        OrderPasturesNearest(p, 670, 1180);    // just south of Yew's farmland
        Check(p.size() == 4, "nothing is dropped by the ordering");
        Check(p[0].x == 677 && p[0].y == 1177,
              "nearest-first from where the character stands");
        Check(p.back().x == 5159, "the far-away flock sorts last");

        OrderPasturesNearest(p, 683, 940);     // stand by the northern flock
        Check(p[0].x == 681 && p[0].y == 945,
              "move the character and the order follows");

        // Same distance, different flock size: the bigger flock wins.
        std::vector<Row> tie = {{100, 110, 4}, {110, 100, 40}};
        OrderPasturesNearest(tie, 100, 100);
        Check(tie[0].count == 40, "on a tie, the bigger flock leads");

        std::vector<Row> none;
        OrderPasturesNearest(none, 0, 0);
        Check(none.empty(), "an empty table stays empty");
    }

    // --- C. which herd is worth the walk ----------------------------------
    // "rhea can tame a lot of things not just sheep" (project owner,
    // 2026-09-02). Real rows of data/revolution_tamables.tsv near Britain,
    // with their chardef TAMING requirements from revolution_creatures.tsv.
    {
        struct Herd { int x = 0, y = 0, count = 0; double req = 0.0; };
        const double skill = 50.0;                 // Rhea's Taming

        std::vector<Herd> herds = {
            {1425, 1695, 6, 0.9},                  // Rats, underfoot, too easy
            {1400, 1660, 4, 15.3},                 // Dogs, close, too easy
            {1300, 1600, 3, 41.1},                 // Brown bears, gainful
            {1250, 1550, 2, 47.1},                 // Alligators, further, gainful
            {1290, 1590, 2, 59.1},                 // Grizzlies, over her skill
        };

        int i = ChooseTameCluster(herds, 1420, 1690, skill, 1000);
        Check(i == 2, "the gain window beats the doorstep: bears, not rats");
        Check(herds[static_cast<unsigned>(i)].req <= skill,
              "never a herd above this character's skill");

        // Cut the clock down: the bears are 120 tiles off, the dogs 30, the
        // rats 5. Nothing in the window is affordable, so she takes what is in
        // reach rather than starting a walk the session cannot finish.
        i = ChooseTameCluster(herds, 1420, 1690, skill, 60);
        Check(i == 0, "no time for the bears -> the nearest affordable herd");
        i = ChooseTameCluster(herds, 1420, 1690, skill, 2);
        Check(i < 0, "nothing affordable -> no pick at all, not a bad one");

        // Two gainful herds, same distance: the nearer one wins, then size.
        std::vector<Herd> two = {{1400, 1700, 2, 45.0}, {1440, 1690, 9, 45.0}};
        i = ChooseTameCluster(two, 1420, 1690, skill, 1000);
        Check(i == 1, "same tier, nearest first");

        // A beginner: 5.0 is Skill_Experience's floor, so a rat still gains.
        Check(TameCanGain(0.9, 2.0), "a rat teaches a beginner something");
        Check(!TameCanGain(0.9, 50.0), "a rat teaches Rhea nothing");
        Check(TameCanGain(41.1, 50.0), "ten points below skill still gains");

        std::vector<Herd> none;
        Check(ChooseTameCluster(none, 0, 0, skill, 1000) < 0,
              "an empty table yields no herd");
    }

    // --- D. the travel budget is the clock, not a constant -----------------
    {
        // 200 ms/tile x 1.5 (EstimateTripTimeMs), minus wind-down and the
        // work still to do at the far end (kTameWorkReserveMs = 60 s).
        const uo::i64 windDown = 60000;
        Check(TameTravelBudgetTiles(25 * 60000, windDown, kTameWorkReserveMs) >
                  1000,
              "twenty-five minutes left -> a long walk is affordable");
        const uo::i32 fiveMin =
            TameTravelBudgetTiles(5 * 60000, windDown, kTameWorkReserveMs);
        Check(fiveMin > 0 && fiveMin < 1000,
              "a five-minute session buys a short walk, not a crossing");
        Check(TameTravelBudgetTiles(30000, windDown, kTameWorkReserveMs) == 0,
              "less time left than wind-down needs -> no trip at all");
    }

    std::printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
