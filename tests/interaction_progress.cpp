// tests/interaction_progress.cpp -- docs/BOT_ARCHITECTURE.md section 18.
//
// "sent packet = success" is the rule this file exists to make impossible.
// Every case below is a shape this shard actually produced, not an invented
// one, and each is named for the run that produced it.
//
// No server, no MULs, no world data.

#include "uo/interaction/progress.h"
#include "uo/interaction/activity_result.h"

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

void ExpectVerdict(const ProgressCheck& got, Verdict want, const char* what) {
    ++g_checks;
    if (got.verdict != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    VerdictName(want), VerdictName(got.verdict), got.reason);
        ++g_failures;
    }
}

// A purchase that worked: twenty bandages arrived and the gold went.
void TestAConfirmedPurchase() {
    std::printf("[progress: a purchase is confirmed by the pack AND the purse]\n");
    Expectation e;
    e.itemGraphic = 0x0E21;
    e.itemBefore = 0;
    e.itemGain = 20;
    e.goldBefore = 9326;
    e.goldSpendMin = 1;
    e.goldSpendMax = 40;

    Observed seen;
    seen.itemNow = 20;
    seen.goldNow = 9306;   // 20 bandages at 1gp

    const ProgressCheck r = Verify(e, seen);
    ExpectVerdict(r, Verdict::Confirmed, "20 bought, 20 gold gone");
    Expect(r.itemDelta == 20, "item delta is reported");
    Expect(r.goldDelta == -20, "gold delta is reported");
}

// THE BUG THIS FILE IS FOR. run_m7/r1a_Corwyn.console.txt, 11:16-11:18:
// the healer held 19, the errand asked for 20, Sphere refused the WHOLE
// order, and the bot recorded eight successful purchases while gold=9326
// never moved and no bandage ever arrived.
void TestTheRefusedOrderIsNotSuccess() {
    std::printf("[progress: an order Sphere refused wholesale is not a buy]\n");
    Expectation e;
    e.itemGraphic = 0x0E21;
    e.itemBefore = 0;
    e.itemGain = 20;
    e.goldBefore = 9326;
    e.goldSpendMin = 1;

    Observed seen;
    seen.itemNow = 0;       // nothing came
    seen.goldNow = 9326;    // and nothing was paid

    const ProgressCheck r = Verify(e, seen);
    ExpectVerdict(r, Verdict::NotYet, "nothing moved at all");
    Expect(r.verdict != Verdict::Confirmed,
           "a refused order must never read as confirmed");
}

// Gold left and no goods arrived. Definitive, not merely slow: this ends the
// errand instead of letting it time out over and over.
void TestGoldGoneAndNothingArrived() {
    std::printf("[progress: gold spent with nothing to show is contradicted]\n");
    Expectation e;
    e.itemGraphic = 0x0E21;
    e.itemBefore = 4;
    e.itemGain = 10;
    e.goldBefore = 500;
    e.goldSpendMin = 1;

    Observed seen;
    seen.itemNow = 4;
    seen.goldNow = 380;

    ExpectVerdict(Verify(e, seen), Verdict::Contradicted,
                  "120 gold left and the pack is unchanged");
}

// The ledger's problem, not the bot's luck: a purchase that costs nothing
// means the gold moved where this check cannot see it.
void TestFreeGoodsAreContradicted() {
    std::printf("[progress: goods that cost nothing are a ledger fault]\n");
    Expectation e;
    e.itemGraphic = 0x0F0E;
    e.itemBefore = 0;
    e.itemGain = 5;
    e.goldBefore = 1000;
    e.goldSpendMin = 1;

    Observed seen;
    seen.itemNow = 5;
    seen.goldNow = 1000;

    ExpectVerdict(Verify(e, seen), Verdict::Contradicted,
                  "five bottles for nothing");
}

// A price that moved between the quote and the purchase.
void TestOverpayingIsContradicted() {
    std::printf("[progress: paying more than the quote allowed is refused]\n");
    Expectation e;
    e.goldBefore = 1000;
    e.goldSpendMin = 1;
    e.goldSpendMax = 100;

    Observed seen;
    seen.goldNow = 700;   // 300 spent against a 100 ceiling

    ExpectVerdict(Verify(e, seen), Verdict::Contradicted, "spent 3x the quote");
}

// training_unverified, from run_m7/fleet2.console.txt:5470 and a dozen more:
// GOLD_DESTROYED_TRAINER with no confirmed skill gain. Never a lesson.
void TestSkillMustActuallyMove() {
    std::printf("[progress: a lesson is the server's number moving, not a fee]\n");
    Expectation e;
    e.skillId = 25;          // Magery
    e.skillBefore = 219;     // 21.9
    e.skillGainMin = 1;
    e.goldBefore = 1000;
    e.goldSpendMin = 1;

    Observed seen;
    seen.skillNow = 219;     // unchanged
    seen.goldNow = 700;      // but the trainer was paid

    const ProgressCheck r = Verify(e, seen);
    Expect(r.verdict != Verdict::Confirmed,
           "paying a trainer is not the same as learning");
}

// A gear piece that never reached the paperdoll: the slot stays empty, so
// the next tick buys another. Six heater shields.
void TestEquipMustReachTheLayer() {
    std::printf("[progress: gear counts when it is WORN, not when it is bought]\n");
    Expectation e;
    e.equipLayer = 0x02;
    e.equipGraphic = 0x1B76;

    Observed inPack;
    inPack.equippedAtLayer = 0;      // still in the backpack
    ExpectVerdict(Verify(e, inPack), Verdict::NotYet, "bought but not worn");

    Observed worn;
    worn.equippedAtLayer = 0x1B76;
    ExpectVerdict(Verify(e, worn), Verdict::Confirmed, "on the paperdoll");
}

// A SALE IS GOLD ARRIVING AND GOODS LEAVING. Either half on its own is
// something else: gold rises from loot, from a player trade and from a bank
// withdrawal, and crediting a sale for it teaches the price book a number
// nobody paid.
void TestASaleIsBothHalves() {
    std::printf("[progress: a sale is gold IN and goods OUT, not either one]\n");
    Expectation e;
    e.itemGraphic = 0x0F9E;      // a dagger
    e.itemBefore = 12;
    e.itemLoss = 12;
    e.goldBefore = 1000;
    e.goldGainMin = 1;

    Observed sold;
    sold.itemNow = 0;
    sold.goldNow = 1132;
    const ProgressCheck ok = Verify(e, sold);
    ExpectVerdict(ok, Verdict::Confirmed, "twelve gone, 132 gold in");
    Expect(ok.itemDelta == -12, "the loss is reported as negative");
    Expect(ok.goldDelta == 132, "and the gain as positive");

    // The goods went and nothing came back: a give, a drop, a theft.
    Observed robbed;
    robbed.itemNow = 0;
    robbed.goldNow = 1000;
    ExpectVerdict(Verify(e, robbed), Verdict::Contradicted,
                  "goods gone, purse unchanged");

    // Gold arrived and the pack is untouched: something else paid us.
    Observed windfall;
    windfall.itemNow = 12;
    windfall.goldNow = 1500;
    ExpectVerdict(Verify(e, windfall), Verdict::Contradicted,
                  "paid without selling anything");

    // Still in progress: the vendor has taken the goods but not paid yet.
    Observed pending;
    pending.itemNow = 12;
    pending.goldNow = 1000;
    ExpectVerdict(Verify(e, pending), Verdict::NotYet, "nothing has moved");
}

// A partial sale is not a completed one. Selling a lot of twelve and seeing
// four leave means the buyer took what it could afford.
void TestAPartialSaleIsNotDone() {
    std::printf("[progress: a partial lot is not the lot]\n");
    Expectation e;
    e.itemGraphic = 0x0F9E;
    e.itemBefore = 12;
    e.itemLoss = 12;
    e.goldBefore = 1000;
    e.goldGainMin = 1;

    Observed partial;
    partial.itemNow = 8;         // only four left
    partial.goldNow = 1044;
    ExpectVerdict(Verify(e, partial), Verdict::NotYet,
                  "four of twelve sold is not twelve sold");
}

// An activity that states no expectation gets told so, rather than being
// handed a cheerful yes it did not earn.
void TestSilenceIsNotSuccess() {
    std::printf("[progress: verifying nothing is not the same as succeeding]\n");
    Expectation nothing;
    ExpectVerdict(Verify(nothing, Observed{}), Verdict::NothingChecked,
                  "an empty expectation");
}

// Section 14: only Success may tell the planner that work happened.
void TestOnlySuccessCountsAsProgress() {
    std::printf("[status: only success is progress; the other six are not]\n");
    Expect(IsProgress(ActivityStatus::Success), "success is progress");
    Expect(!IsProgress(ActivityStatus::NoProgress), "no_progress is not");
    Expect(!IsProgress(ActivityStatus::Blocked), "blocked is not");
    Expect(!IsProgress(ActivityStatus::Failed), "failed is not");
    Expect(!IsProgress(ActivityStatus::RetryableFailure), "retryable is not");
    Expect(!IsProgress(ActivityStatus::Interrupted), "interrupted is not");
    Expect(!IsProgress(ActivityStatus::Waiting), "waiting is not");

    Expect(!IsTerminal(ActivityStatus::Waiting), "waiting is not terminal");
    Expect(IsTerminal(ActivityStatus::NoProgress), "no_progress ends the tick");

    // Every state and every wake must name itself: an unexplained stand-down
    // is what cost this project whole sessions to diagnose.
    for (int i = 0; i <= static_cast<int>(ActivityStatus::RetryableFailure); ++i) {
        const char* n = ActivityStatusName(static_cast<ActivityStatus>(i));
        Expect(n && n[0] && n[0] != '?', "every status has a name");
    }
    for (int i = 0; i <= static_cast<int>(Wake::TargetCursor); ++i) {
        const char* n = WakeName(static_cast<Wake>(i));
        Expect(n && n[0] && n[0] != '?', "every wake condition has a name");
    }
}

}  // namespace

int main() {
    std::printf("interaction_progress\n");
    TestAConfirmedPurchase();
    TestTheRefusedOrderIsNotSuccess();
    TestGoldGoneAndNothingArrived();
    TestFreeGoodsAreContradicted();
    TestOverpayingIsContradicted();
    TestSkillMustActuallyMove();
    TestEquipMustReachTheLayer();
    TestASaleIsBothHalves();
    TestAPartialSaleIsNotDone();
    TestSilenceIsNotSuccess();
    TestOnlySuccessCountsAsProgress();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
