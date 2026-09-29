// Which runebook page shortens a trip (uo/recall_plan.h), pure.
#include "uo/recall_plan.h"

#include <cstdio>

using namespace uo;
using namespace uo::recall;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

int main() {
    i32 x = 0, y = 0;
    Check(ParsePoint("1490,1555,30", &x, &y) && x == 1490 && y == 1555, "the gump's point text parses");
    Check(ParsePoint("(2500, 480)", &x, &y) && x == 2500 && y == 480, "punctuation and spaces are tolerated");
    Check(!ParsePoint("-", &x, &y) && !ParsePoint("", &x, &y) && !ParsePoint("12", &x, &y), "an empty page has no point");
    Check(!ParsePoint("9000,100", &x, &y), "off-map points are refused");

    std::vector<Page> book = {{1, "Britain", 1490, 1555, true}, {2, "Minoc", 2500, 480, true},
                              {3, "(empty)", 0, 0, false}, {4, "Vesper", 2890, 690, true}};
    Check(BestPage(book, 100, 100, 1480, 1600).page == 1, "the page landing nearest the goal wins");
    Check(BestPage(book, 100, 100, 2800, 700).page == 4, "Vesper beats Minoc for a Vesper goal");
    Check(BestPage(book, 2450, 500, 2500, 480).page == 0, "a trip already near its goal walks");
    Check(BestPage(book, 100, 100, 4400, 1100).page == 0, "no rune near the goal: walk (or gate)");
    const Choice c = BestPage(book, 100, 100, 1480, 1600);
    Check(c.walkAfter < c.walkNow && c.walkNow - c.walkAfter >= kMinSavingTiles, "a chosen page clearly saves walking");

    Check(HasRecallReagents(1, 1, 1, 1) && !HasRecallReagents(1, 0, 1, 1), "one of each reagent after 14.05.2009");
    Check(!HasRecallReagents(2, 2, 2, 3) && HasRecallReagents(3, 3, 3, 3), "three of each before it");

    Check(ShouldReadBook(true, false, true, false, 1000, 0), "an unread book is read when idle and safe");
    Check(!ShouldReadBook(true, true, true, false, 1000, 0), "only once per session");
    Check(!ShouldReadBook(true, false, false, false, 1000, 0) && !ShouldReadBook(true, false, true, true, 1000, 0),
          "never in danger or mid-action");
    Check(!ShouldReadBook(true, false, true, false, 60000, 1000), "a failed try waits two minutes");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
