// Housing decisions (uo/housing.h), pure.
#include "uo/housing.h"

#include <cstdio>
#include <set>

using namespace uo;
using namespace uo::housing;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

int main() {
    Wealth w; w.totalGold = 5000;
    Check(!WantsHouse(w), "5000 gold is not house money");
    w.totalGold = kWealthToShop;
    Check(WantsHouse(w), "enough saved: go looking");
    w.ownsHouse = true;
    Check(!WantsHouse(w), "one house per account");
    w = {}; w.deedInPack = true;
    Check(WantsHouse(w), "a deed in the pack always wants placing");
    Check(DeedBudget(40000) == 40000 - kKeepAfterHouse && DeedBudget(1000) == 0, "a reserve is always kept");

    const auto a = Candidates(1500, 1600, 7), b = Candidates(1500, 1600, 7), c = Candidates(1500, 1600, 8);
    Check(a.size() == 64 && a.size() == b.size() && a[0].x == b[0].x && a[0].y == b[0].y, "the same sites in the same order");
    Check(a[0].x != c[0].x || a[0].y != c[0].y, "a different character starts on a different bearing");
    bool outside = true;
    for (const Site& s : a) {
        const i32 dx = s.x - 1500, dy = s.y - 1600;
        if (dx * dx + dy * dy < 38 * 38) outside = false;
    }
    Check(outside, "every site is out of town (40+ tiles)");

    Sight s; s.deedBudget = 30000;
    Check(Decide(s).step == Step::BuyDeed, "no deed: buy one");
    s.deedErrandFailed = true;
    Check(Decide(s).step == Step::GiveUp, "no architect sells one within budget: give up for the day");
    s = {}; s.deedInPack = true; s.siteCount = 10; s.tilesToSite = 50;
    Check(Decide(s).step == Step::Travel, "a deed: go to the site");
    s.tilesToSite = 1;
    Check(Decide(s).step == Step::UseDeed, "at the site: use the deed");
    s.cursorActive = true;
    Check(Decide(s).step == Step::Place, "the cursor is up: place");
    s.cursorActive = false; s.triesHere = 1;
    Check(Decide(s).step == Step::NextSite, "refused here: next site");
    s.placedHere = true;
    Check(Decide(s).step == Step::Done, "a multi at the site: done");
    s = {}; s.deedInPack = true; s.siteIndex = kMaxSitesPerSession; s.siteCount = 64;
    Check(Decide(s).step == Step::GiveUp, "a dozen refusals in one session: stop");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
