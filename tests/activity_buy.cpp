// tests/activity_buy.cpp -- docs/BOT_ARCHITECTURE.md section 15.
//
// The arithmetic half of buying, which is where this project's purchase bugs
// actually lived: not in the packets, but in "how many do I still need" and
// "which money am I allowed to spend". Every case is cited to the session it
// came from.
//
// No server, no MULs.

#include "uo/activities/buy.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

BuyRequest Bandages(int want) {
    BuyRequest r;
    r.graphic = 0x0E21;
    r.item = "clean bandages";
    r.desiredTotal = want;
    return r;
}

// THE SIX HEATER SHIELDS. Corwyn's pack held six i_shield_heater, each bought
// because the equipment slot was still empty -- a request phrased "buy one"
// cannot see what is already in the pack. Phrased as a TOTAL, it can.
void TestAlreadyHoldingEnough() {
    std::printf("[buy: a request is a TOTAL to hold, not a number to buy]\n");
    const BuyPlan p = Decide(Bandages(30), /*held=*/30, /*gold=*/9000, /*unit=*/1);
    Expect(p.satisfied, "thirty wanted, thirty held: nothing to do");
    Expect(p.quantity == 0, "and nothing is ordered");
    Expect(!p.blocked, "which is not the same as being blocked");

    const BuyPlan more = Decide(Bandages(30), 41, 9000, 1);
    Expect(more.satisfied, "holding MORE than wanted is still satisfied");
}

void TestBuysOnlyTheShortfall() {
    std::printf("[buy: order the shortfall, never the whole want again]\n");
    const BuyPlan p = Decide(Bandages(30), 22, 9000, 1);
    Expect(p.quantity == 8, "thirty wanted, twenty-two held, order eight");
    Expect(!p.satisfied && !p.blocked, "and it is a live order");
}

// THE SCRIBE DEADLOCK, verbatim from the M7 notes: blank scrolls cost 3 gold,
// the purse held 781, the reserve was 900, and the character stood in the
// mage shop refusing to buy three scrolls -- every fifteen seconds, for the
// whole session. A reserve that forbids the only activity which refills it is
// not caution, it is a trap. The reserve is the CALLER's choice, and this
// function's job is only to apply the number it was handed.
void TestTheReserveIsTheCallersChoice() {
    std::printf("[buy: the reserve is applied exactly as given, and no more]\n");
    BuyRequest scrolls;
    scrolls.graphic = 0x0E34;
    scrolls.item = "blank scrolls";
    scrolls.desiredTotal = 20;
    scrolls.minimumGoldReserve = 900;

    const BuyPlan trapped = Decide(scrolls, 0, 781, 3);
    Expect(trapped.blocked, "781 gold under a 900 reserve buys nothing");
    Expect(trapped.reason && trapped.reason[0], "and it says why");

    // The same request with working capital instead of the death reserve --
    // which is what the caller should have passed -- buys the scrolls.
    scrolls.minimumGoldReserve = 100;
    const BuyPlan working = Decide(scrolls, 0, 781, 3);
    Expect(!working.blocked, "with a working-capital floor it proceeds");
    Expect(working.quantity == 20, "and affords the full twenty");
}

void TestBuysWhatThePurseAllows() {
    std::printf("[buy: a thin purse orders fewer, it does not give up]\n");
    BuyRequest r = Bandages(30);
    r.minimumGoldReserve = 100;
    const BuyPlan p = Decide(r, 0, 118, 1);
    Expect(!p.blocked, "eighteen spendable gold is not a blockage");
    Expect(p.quantity == 18, "it orders eighteen");
}

void TestBlockedWhenNotEvenOne() {
    std::printf("[buy: below the price of one, that is Blocked not Failed]\n");
    BuyRequest r = Bandages(10);
    r.minimumGoldReserve = 100;
    const BuyPlan p = Decide(r, 0, 105, 12);
    Expect(p.blocked, "five spendable against a twelve-gold unit");
    Expect(p.quantity == 0, "nothing is ordered");
    // Blocked is a promise that the answer changes when the world does --
    // Voris stood outside the alchemist unable to afford a 12 gold bottle
    // while carrying five poison potions worth about a hundred.
}

// "A bot with a full purse will otherwise accept any number a seller says."
void TestThePriceCeiling() {
    std::printf("[buy: a ceiling, so one greedy shop cannot drain a purse]\n");
    BuyRequest r = Bandages(10);
    r.maxPricePerUnit = 5;

    const BuyPlan fair = Decide(r, 0, 10000, 4);
    Expect(!fair.blocked && fair.quantity == 10, "four is under the ceiling");

    const BuyPlan gouging = Decide(r, 0, 10000, 40);
    Expect(gouging.blocked, "forty is not paid whatever the purse holds");
    Expect(gouging.quantity == 0, "and nothing is ordered");
}

// Before a shop is open there is no price. Asking for the shortfall is right;
// the errand clamps to stock and the sum is done again with a real number.
void TestNoPriceYet() {
    std::printf("[buy: with no quote yet, order the shortfall and find out]\n");
    const BuyPlan p = Decide(Bandages(20), 4, 500, 0);
    Expect(p.quantity == 16, "the shortfall stands in until a price arrives");
    Expect(!p.blocked, "an unknown price is not a blockage");
    Expect(!p.satisfied, "nor a reason to stop");
}

// Every plan explains itself. An unexplained refusal to act is
// indistinguishable from a hang, and this project has spent whole sessions
// telling those two apart by hand.
void TestEveryPlanSaysWhy() {
    std::printf("[buy: no silent decisions]\n");
    const BuyPlan cases[] = {
        Decide(Bandages(5), 5, 100, 1),      // satisfied
        Decide(Bandages(5), 0, 100, 1),      // affordable
        Decide(Bandages(5), 0, 0, 1),        // blocked
        Decide(Bandages(5), 0, 100, 0),      // no price
    };
    for (const BuyPlan& p : cases)
        Expect(p.reason && p.reason[0], "the plan states its reasoning");
}

}  // namespace

int main() {
    std::printf("activity_buy\n");
    TestAlreadyHoldingEnough();
    TestBuysOnlyTheShortfall();
    TestTheReserveIsTheCallersChoice();
    TestBuysWhatThePurseAllows();
    TestBlockedWhenNotEvenOne();
    TestThePriceCeiling();
    TestNoPriceYet();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
