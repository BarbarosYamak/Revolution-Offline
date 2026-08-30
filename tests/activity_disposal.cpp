// tests/activity_disposal.cpp -- docs/BOT_ARCHITECTURE.md sections 7 and 54.
//
// The case this exists for is v4_Corwyn, 2026-08-30 15:36:38: Curtis listed
// two daggers AND six heater shields, Corwyn sold the daggers for 72 gold and
// walked away from 366 gold of shields, because "do I need this graphic?" was
// a yes/no question and a smith makes shields.
//
// No server, no MULs.

#include "uo/activities/disposal.h"

#include <cstdio>
#include <initializer_list>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo;          // i32
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void ExpectPlan(const DisposalPlan& got, DisposalStep step, i32 qty,
                const char* what) {
    ++g_checks;
    if (got.step != step || got.quantity != qty) {
        std::printf("  FAIL: %s -- wanted %s x%d, got %s x%d (%s)\n", what,
                    DisposalStepName(step), qty, DisposalStepName(got.step),
                    got.quantity, got.reason);
        ++g_failures;
    }
}

DisposalTuning Default() { return DisposalTuning{}; }

// CORWYN'S SIX SHIELDS. To a smith a heater shield is Produce -- the thing he
// makes in order to sell it -- so all six should go.
void TestTheSixShields() {
    std::printf("[disposal: the six heater shields Corwyn would not sell]\n");
    DisposalSight s;
    s.role = ItemRole::Produce;
    s.carried = 6;
    s.vendorTakes = 6;
    s.pricePerUnit = 61;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Sell, 6,
               "a smith's shields are stock, and stock is for selling");
}

// THE OWNER'S RULE, for a life that WEARS them rather than makes them:
// "maybe max 1 he can bank rest he should sell" (2026-08-30).
void TestAWearerKeepsExactlyOne() {
    std::printf("[disposal: one spare is a spare, six is hoarding]\n");
    DisposalSight s;
    s.role = ItemRole::Wearable;
    s.carried = 6;
    s.vendorTakes = 6;
    s.pricePerUnit = 61;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Sell, 5,
               "sell five, keep one");

    s.carried = 1;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Keep, 0,
               "the last one stays");
}

// Nothing that keeps the character alive or working is ever surplus, at any
// count. Selling your reagents to buy reagents is a round trip to nowhere.
void TestStockIsNeverSurplus() {
    std::printf("[disposal: money, food and inputs are not merchandise]\n");
    for (ItemRole r : {ItemRole::Money, ItemRole::Consumable,
                       ItemRole::CraftInput}) {
        DisposalSight s;
        s.role = r;
        s.carried = 400;
        s.vendorTakes = 400;
        s.pricePerUnit = 9;
        ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Keep, 0,
                   ItemRoleName(r));
        Expect(KeepCount(r, Default()) == kKeepEverything,
               "and its keep-count says so");
    }
}

void TestOneOfEachTool() {
    std::printf("[disposal: a second pickaxe is weight, not insurance]\n");
    DisposalSight s;
    s.role = ItemRole::Tool;
    s.carried = 3;
    s.vendorTakes = 3;
    s.pricePerUnit = 12;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Sell, 2,
               "keep the one being used");
}

void TestLootGoesEntirely() {
    std::printf("[disposal: loot this life cannot use has no keep-count]\n");
    DisposalSight s;
    s.role = ItemRole::Unknown;
    s.carried = 4;
    s.vendorTakes = 4;
    s.pricePerUnit = 30;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Sell, 4, "all of it");
}

// A price of zero is the server DECLINING to name one. Selling into it hands
// the goods over for nothing, which is worse than carrying them.
void TestNoPriceIsNotAFreeGift() {
    std::printf("[disposal: zero gold is not an offer]\n");
    DisposalSight s;
    s.role = ItemRole::Produce;
    s.carried = 5;
    s.vendorTakes = 5;
    s.pricePerUnit = 0;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Keep, 0,
               "no price, no sale");
}

// Offer no more than the vendor said it would take.
void TestNeverOfferMoreThanListed() {
    std::printf("[disposal: the vendor's own list is the ceiling]\n");
    DisposalSight s;
    s.role = ItemRole::Produce;
    s.carried = 9;
    s.vendorTakes = 2;
    s.pricePerUnit = 61;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Sell, 2,
               "it listed two, so two is the offer");

    s.vendorTakes = 0;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Keep, 0,
               "it listed none at all");
}

// The halving that answers a silent purse (Alenne's eleven poison scrolls)
// must still bound the offer.
void TestTheHalvedLotStillBinds() {
    std::printf("[disposal: a finite purse caps the lot]\n");
    DisposalSight s;
    s.role = ItemRole::Produce;
    s.carried = 11;
    s.vendorTakes = 11;
    s.pricePerUnit = 25;
    s.lotCap = 5;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Sell, 5,
               "offer what the purse has proved it can pay");

    s.lotCap = 0;
    ExpectPlan(DecideDisposal(s, Default()), DisposalStep::Sell, 11,
               "no cap in force means offer the lot");
}

void TestEveryPlanSaysWhy() {
    std::printf("[disposal: no silent decisions]\n");
    Expect(DecideDisposal(DisposalSight{}, Default()).reason[0],
           "the empty case still states its reasoning");
    for (int i = 0; i <= static_cast<int>(ItemRole::Unknown); ++i)
        Expect(ItemRoleName(static_cast<ItemRole>(i))[0] != '?',
               "every role has a name");
    for (int i = 0; i <= static_cast<int>(DisposalStep::Sell); ++i)
        Expect(DisposalStepName(static_cast<DisposalStep>(i))[0] != '?',
               "every step has a name");
}

}  // namespace

int main() {
    std::printf("activity_disposal\n");
    TestTheSixShields();
    TestAWearerKeepsExactlyOne();
    TestStockIsNeverSurplus();
    TestOneOfEachTool();
    TestLootGoesEntirely();
    TestNoPriceIsNotAFreeGift();
    TestNeverOfferMoreThanListed();
    TestTheHalvedLotStillBinds();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
