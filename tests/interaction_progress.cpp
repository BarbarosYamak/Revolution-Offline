// tests/interaction_progress.cpp -- docs/BOT_ARCHITECTURE.md section 18.
//
// "sent packet = success" is the rule this file exists to make impossible.
// Every case below is a shape this shard actually produced, not an invented
// one, and each is named for the run that produced it.
//
// No server, no MULs, no world data.

#include "uo/interaction/progress.h"
#include "uo/interaction/activity_result.h"
#include "uo/activities/craft_confirm.h"
#include "uo/activities/train_confirm.h"

#include <cstdio>
#include <cstring>

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

// BANK-PAID PURCHASE, NOT A LEDGER FAULT.
//
// This used to be TestFreeGoodsAreContradicted, and it was wrong: this shard
// pays vendors from the BANK box, not the pack (sphere.ini
// PayFromPackOnly=0), so a genuinely successful buy can complete with the
// PACK's gold never moving at all. This exact shape -- goods arrived,
// out.goldDelta == 0 -- fired the old "goods arrived without the purse
// moving" Contradicted verdict on BOTH bots in tonight's live run
// (run_r4/pair_Tarath.console.txt, run_r4/pair_Durnholde.console.txt) even
// though the goods had genuinely arrived. The pack rising by the requested
// amount IS success here; only a pack that did NOT rise is a failure (see
// TestTheRefusedOrderIsNotSuccess, just above, for that case).
void TestBankPaidPurchaseIsConfirmed() {
    std::printf("[progress: goods arriving with the pack's gold untouched is "
                "a bank-paid buy, not a ledger fault]\n");
    Expectation e;
    e.itemGraphic = 0x0F0E;
    e.itemBefore = 0;
    e.itemGain = 5;
    e.goldBefore = 1000;
    e.goldSpendMin = 1;

    Observed seen;
    seen.itemNow = 5;
    seen.goldNow = 1000;   // PayFromPackOnly=0: the bank paid, not the pack

    const ProgressCheck r = Verify(e, seen);
    ExpectVerdict(r, Verdict::Confirmed,
                  "five bottles arrived with the pack's gold untouched -- "
                  "the bank paid");
    Expect(r.itemDelta == 5, "item delta is reported");
    Expect(r.goldDelta == 0,
           "the pack's gold really did not move, and that is not a fault");
    Expect(std::strstr(r.reason, "bank paid") != nullptr,
           "the reason names the shard rule, not a ledger fault");
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

// ---------------------------------------------------------------------------
// CRAFT (section 18: "crafted item count increased, or a definitive craft
// failure received"). S3.
// ---------------------------------------------------------------------------

// Look a failure up by its verbatim shard text, the way the runner does.
const uo::life::CraftFailure* Failure(const char* text) {
    uo::usize n = 0;
    const uo::life::CraftFailure* f = uo::life::CraftFailures(&n);
    for (uo::usize i = 0; i < n; ++i)
        if (std::strcmp(f[i].text, text) == 0) return &f[i];
    return nullptr;
}

void TestACraftIsTheDaggerArriving() {
    std::printf("[craft: a craft is the PACK moving, never the click landing]\n");
    CraftConfirmInput in;
    in.packBefore = 3;
    in.packNow = 3;
    ExpectVerdict(ConfirmCraft(in).check, Verdict::NotYet,
                  "the menu was answered and nothing came of it yet");
    Expect(ConfirmCraft(in).verdict == CraftVerdict::Waiting,
           "a click that has not produced anything is still waiting");

    in.packNow = 4;                 // "craft: made i_dagger pack 3->4"
    const CraftConfirmResult r = ConfirmCraft(in);
    Expect(r.verdict == CraftVerdict::Made, "the fourth dagger is a craft");
    Expect(r.made == 1, "and exactly one of it");
}

// The 17 ruined scrolls: SRC.SYSMESSAGE in skill23_inscription.scp, heard
// verbatim in run_m5/run_m7. The blank is consumed and nothing is produced,
// so the PACK alone looks exactly like silence -- which is why the shard's
// own words are the second half of the rule.
void TestARuinedScrollIsAnAnswerNotSilence() {
    std::printf("[craft: a ruined scroll is a real answer, not a timeout]\n");
    const CraftFailure* ruined =
        Failure("you fail to inscribe the scroll, and the scroll is ruined.");
    Expect(ruined != nullptr, "the shard's inscription failure is in the table");
    if (!ruined) return;
    Expect(!ruined->blocking, "a ruined scroll does not end the trade");

    CraftConfirmInput in;
    in.packBefore = 2;
    in.packNow = 2;               // nothing arrived, and nothing will
    in.heard = ruined;
    const CraftConfirmResult r = ConfirmCraft(in);
    Expect(r.verdict == CraftVerdict::Spoiled, "spoiled, so the swing is over");
    Expect(r.verdict != CraftVerdict::Made, "and emphatically not a craft");
    Expect(r.made == 0, "nothing was made");
}

// THE PACK OUTRANKS THE COMPLAINT. A batch can ruin one scroll and finish the
// next inside the same window; reading the complaint first would throw away
// the item that actually arrived.
void TestAnItemThatArrivedOutranksAComplaint() {
    std::printf("[craft: an item that ARRIVED beats a failure line]\n");
    CraftConfirmInput in;
    in.packBefore = 0;
    in.packNow = 1;
    in.heard =
        Failure("you fail to inscribe the scroll, and the scroll is ruined.");
    Expect(ConfirmCraft(in).verdict == CraftVerdict::Made,
           "one scroll was ruined and the next one landed");
}

// Blocking failures END the goal: swinging again from the same tile cannot
// light a fire or make the shard change its mind about the opener.
void TestBlockingFailuresEndTheGoal() {
    std::printf("[craft: 'no fire' and 'cannot use that' are stand-downs]\n");
    const char* blockers[] = {"you must be near a fire source to cook.",
                              "you can't think of a way to use that item."};
    for (const char* text : blockers) {
        const CraftFailure* f = Failure(text);
        Expect(f != nullptr, "the shard's blocking failure is in the table");
        if (!f) continue;
        Expect(f->blocking, "and it is marked blocking");
        Expect(f->evidence && f->evidence[0],
               "every string names where it is written down");
        CraftConfirmInput in;
        in.packBefore = 0;
        in.packNow = 0;
        in.heard = f;
        Expect(ConfirmCraft(in).verdict == CraftVerdict::ShardRefused,
               "the shard refused, so the goal stands down");
    }
}

// Section 14: the batch produced nothing, and saying so is what lets the
// planner give the turn to something else. Never Success.
void TestSpentAttemptsAreNoProgressNotSuccess() {
    std::printf("[craft: three swings and an empty pack is no_progress]\n");
    CraftConfirmInput in;
    in.packBefore = 5;
    in.packNow = 5;
    in.deadlineExpired = true;
    in.attemptsExhausted = true;
    const CraftConfirmResult r = ConfirmCraft(in);
    Expect(r.verdict == CraftVerdict::NoProgress, "no_progress, by name");
    Expect(r.verdict != CraftVerdict::Made, "and never a craft");
    for (int i = 0; i <= static_cast<int>(CraftVerdict::NoProgress); ++i) {
        const char* n = CraftVerdictName(static_cast<CraftVerdict>(i));
        Expect(n && n[0] && n[0] != '?', "every craft verdict has a name");
    }
}

// ---------------------------------------------------------------------------
// TRAIN (section 18: "skill changed, or the trainer definitively refused").
// ---------------------------------------------------------------------------

// The live purchase that WORKED and was filed as a failure: 11.8 -> 21.1 for
// 93 gold. Sphere does not push the number, so the errand must ask for it.
void TestALessonIsTheServersNumberMoving() {
    std::printf("[train: a lesson is the server's number moving]\n");
    TrainConfirmInput in;
    in.skillBefore = 118;
    in.skillNow = 211;
    in.goldBefore = 1000;
    in.goldNow = 907;
    in.quoted = 93;
    in.msSincePaid = 3000;
    in.skillsAsked = true;
    const TrainConfirmResult r = ConfirmTraining(in);
    Expect(r.verdict == TrainVerdict::Learned, "11.8 -> 21.1 is a lesson");
    Expect(r.check.skillDelta == 93, "and the delta is reported in tenths");
    Expect(r.check.goldDelta == -93, "with the fee that bought it");
}

// `training_unverified`, named at last. GOLD_DESTROYED_TRAINER with no skill
// gain -- a fact about THIS trainer, never about the skill.
void TestTheFeeTakenAndNothingTaught() {
    std::printf("[train: a fee taken with no gain is not a lesson]\n");
    TrainConfirmInput in;
    in.skillBefore = 219;
    in.skillNow = 219;          // unchanged
    in.goldBefore = 1000;
    in.goldNow = 819;           // but the trainer was paid
    in.quoted = 181;
    in.msSincePaid = 16000;     // past the window
    in.skillsAsked = true;
    const TrainConfirmResult r = ConfirmTraining(in);
    Expect(r.verdict == TrainVerdict::FeeTakenNoLesson,
           "the fee went and the skill did not move");
    Expect(r.verdict != TrainVerdict::Learned, "which is never a lesson");
    Expect(r.check.verdict != Verdict::Confirmed,
           "and the progress check refuses to confirm it");
}

// The give that addressed a serial Sphere had already retired: nothing moved
// anywhere, and nothing reported an error.
void TestTheSilentGiveIsNamedSeparately() {
    std::printf("[train: a give that moved nothing is not the same failure]\n");
    TrainConfirmInput in;
    in.skillBefore = 199;
    in.skillNow = 199;
    in.goldBefore = 9801;
    in.goldNow = 9801;          // the purse never moved either
    in.quoted = 101;
    in.msSincePaid = 16000;
    in.skillsAsked = true;
    Expect(ConfirmTraining(in).verdict == TrainVerdict::NoAnswer,
           "no fee taken and no lesson given is its own answer");
}

// Ask for the number ONCE, promptly -- not after the timeout has already
// written the errand off.
void TestTheSkillListIsAskedForOnce() {
    std::printf("[train: ask for the skill report, promptly and once]\n");
    TrainConfirmInput in;
    in.skillBefore = 118;
    in.skillNow = 118;
    in.goldBefore = 1000;
    in.goldNow = 907;
    in.quoted = 93;
    in.msSincePaid = 2000;      // past askSkillsAfterMs, well inside giveUp
    in.skillsAsked = false;
    Expect(ConfirmTraining(in).verdict == TrainVerdict::AskForSkills,
           "the report has not arrived, so ask for it");

    in.skillsAsked = true;
    Expect(ConfirmTraining(in).verdict == TrainVerdict::Waiting,
           "and having asked, wait rather than asking again");

    in.msSincePaid = 500;       // too soon even to ask
    in.skillsAsked = false;
    Expect(ConfirmTraining(in).verdict == TrainVerdict::Waiting,
           "half a second after paying, nothing is wrong yet");
}

// A refusal is an ANSWER. Silence is not, and must never be in this table.
void TestTheRefusalsAreTheNpcsOwnWords() {
    std::printf("[train: a refusal is what the NPC SAID, silence is not]\n");
    uo::usize n = 0;
    const TrainerRefusal* r = TrainerRefusals(&n);
    Expect(n >= 5, "the five refusals this shard's trainers speak");
    for (uo::usize i = 0; i < n; ++i) {
        Expect(r[i].text && r[i].text[0], "every refusal has its text");
        Expect(r[i].why && r[i].why[0], "and a reason for the verdict");
        // Matched case-insensitively against the journal, so the table itself
        // must be lower case or the match silently never fires.
        for (const char* c = r[i].text; *c; ++c)
            Expect(!(*c >= 'A' && *c <= 'Z'),
                   "refusal text is lower case for the journal match");
    }
    for (int i = 0; i <= static_cast<int>(TrainVerdict::NoAnswer); ++i) {
        const char* nm = TrainVerdictName(static_cast<TrainVerdict>(i));
        Expect(nm && nm[0] && nm[0] != '?', "every train verdict has a name");
    }
}

}  // namespace

int main() {
    std::printf("interaction_progress\n");
    TestAConfirmedPurchase();
    TestTheRefusedOrderIsNotSuccess();
    TestGoldGoneAndNothingArrived();
    TestBankPaidPurchaseIsConfirmed();
    TestOverpayingIsContradicted();
    TestSkillMustActuallyMove();
    TestEquipMustReachTheLayer();
    TestASaleIsBothHalves();
    TestAPartialSaleIsNotDone();
    TestSilenceIsNotSuccess();
    TestOnlySuccessCountsAsProgress();
    // S3 -- craft and train, section 18's remaining two rows.
    TestACraftIsTheDaggerArriving();
    TestARuinedScrollIsAnAnswerNotSilence();
    TestAnItemThatArrivedOutranksAComplaint();
    TestBlockingFailuresEndTheGoal();
    TestSpentAttemptsAreNoProgressNotSuccess();
    TestALessonIsTheServersNumberMoving();
    TestTheFeeTakenAndNothingTaught();
    TestTheSilentGiveIsNamedSeparately();
    TestTheSkillListIsAskedForOnce();
    TestTheRefusalsAreTheNpcsOwnWords();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
