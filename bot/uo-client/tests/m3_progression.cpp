// Deterministic tests for the M3 progression, economy and trade state
// machines. These link the exact headers the client compiles against, so the
// cap arithmetic, the training backoff and the secure-trade sequencing cannot
// drift from shipping behaviour.
//
// The numbers used here are the shard's real ones where they matter -- a
// 100.0 per-skill cap, a 1000.0 total, 300 stats -- because a test that
// asserts against invented limits proves nothing about this shard.
//
// Live proof that Sphere actually behaves this way is the scenarios' job.

#include "uo/progression.h"
#include "uo/trade.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace uo;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

// The Revolution runtime's own limits, from skills/skillclasses.scp
// [SKILLCLASS 0]: every skill 100.0, SKILLSUM 10000 tenths, STATSUM 300.
prog::CapRules RevolutionCaps() {
    prog::CapRules c;
    c.perSkillTenths = 1000;
    c.skillSumTenths = 10000;
    c.perStat = 100;
    c.statSum = 300;
    return c;
}

// A hybrid, on purpose: this project's characters are not professions, and the
// planner must not care that one build mixes gathering, crafting and magic.
prog::CharacterBuild HybridBuild() {
    prog::CharacterBuild b;
    b.name = "miner-smith-alchemist-mage";
    b.skills = {
        {45, 1000},   // Mining
        {7, 1000},    // Blacksmithy
        {0, 1000},    // Alchemy
        {25, 1000},   // Magery
        {46, 1000},   // Meditation
    };
    b.targetStr = 100;
    b.targetDex = 50;
    b.targetInt = 100;
    return b;
}

void TestBuildCaps() {
    Section("build caps");
    const prog::CapRules caps = RevolutionCaps();
    prog::CharacterBuild b = HybridBuild();

    std::string why;
    Check(b.FitsCaps(caps, &why), "a five-skill hybrid build fits");
    Check(why.empty(), "and reports no reason");
    Check(b.SkillTotalTenths() == 5000, "skill total is 500.0");
    Check(b.StatTotal() == 250, "stat total is 250");

    // Over the per-skill cap.
    b.skills[0].targetTenths = 1200;
    Check(!b.FitsCaps(caps, &why), "a skill above 100.0 is refused");
    Check(why.find("above the 100.0 cap") != std::string::npos,
          "and says which limit it broke");

    // Over the total.
    b = HybridBuild();
    for (int i = 0; i < 6; ++i) b.skills.push_back({static_cast<u16>(60 + i), 1000});
    Check(!b.FitsCaps(caps, &why), "eleven GM skills exceed the 1000.0 total");

    // Over the stat sum.
    b = HybridBuild();
    b.targetStr = 100;
    b.targetDex = 100;
    b.targetInt = 100;
    Check(b.FitsCaps(caps, &why), "100/100/100 is exactly the stat cap");
    b.targetDex = 101;
    Check(!b.FitsCaps(caps, &why), "one point over the per-stat cap is refused");
}

void TestProgressionPlan() {
    Section("progression plan");
    const prog::CapRules caps = RevolutionCaps();
    prog::CharacterBuild b = HybridBuild();

    // What the shard actually hands a new character: the three chosen skills,
    // plus a random fraction of everything else (MaxBaseSkill=200).
    std::vector<prog::SkillSnapshot> have = {
        {25, 500},   // Magery 50.0, the creation clamp
        {45, 120},   // Mining 12.0, random junk
        {7, 186},    // Blacksmithy 18.6
        {0, 100},    // Alchemy 10.0
        {46, 36},    // Meditation 3.6
    };

    prog::ProgressionPlan plan = prog::PlanProgression(b, have, caps);
    Check(!plan.complete, "an unfinished build is not complete");
    Check(plan.needs.size() == 5, "every short skill is a need");
    Check(plan.needs[0].skill == 46, "the biggest gap comes first (Meditation)");
    Check(plan.needs[0].GapTenths() == 964, "and its gap is 96.4");
    Check(plan.skillSumTenths == 942, "the current total is counted");
    Check(plan.headroomTenths == 10000 - 942, "headroom is cap minus total");
    Check(!plan.needsSkillLoss, "this build fits in the headroom");

    // A skill the snapshot never mentions reads as zero, not as met.
    prog::CharacterBuild missing;
    missing.skills = {{18, 500}};
    prog::ProgressionPlan p2 = prog::PlanProgression(missing, have, caps);
    Check(p2.needs.size() == 1 && p2.needs[0].haveTenths == 0,
          "an unreported skill is treated as zero");

    // Complete build.
    std::vector<prog::SkillSnapshot> maxed = {
        {45, 1000}, {7, 1000}, {0, 1000}, {25, 1000}, {46, 1000},
    };
    Check(prog::PlanProgression(b, maxed, caps).complete,
          "a met build reports complete");

    // The cap bites: a character already carrying 943.0 of junk skill cannot
    // add 500 more without losing some. This is the shard's real shape --
    // every character starts with a fraction of all 58 skills.
    std::vector<prog::SkillSnapshot> cluttered = have;
    cluttered.push_back({99, 9430 - 942});
    prog::ProgressionPlan p3 = prog::PlanProgression(b, cluttered, caps);
    Check(p3.needsSkillLoss,
          "a build that needs more than the headroom says so");
}

void TestBudget() {
    Section("budget");
    prog::Budget b;
    b.gold = 1000;
    b.reserve = 200;

    Check(b.Spendable() == 800, "the reserve is not spendable");
    Check(b.CanAfford(800), "the whole spendable amount is affordable");
    Check(!b.CanAfford(801), "one coin more is not");

    Check(b.Spend(300) == 300, "a spend returns what it spent");
    Check(b.gold == 700, "and takes it off the pile");
    Check(b.Spend(10000) == 500, "an overspend is clamped to what is spendable");
    Check(b.gold == 200, "leaving exactly the reserve");
    Check(b.Spendable() == 0, "with nothing left to spend");

    b.Earn(50);
    Check(b.gold == 250, "earnings go in");
    b.Earn(-5);
    Check(b.gold == 250, "a negative earning is ignored, not subtracted");

    prog::PurchaseNeed n;
    n.graphic = 0x0F7B;
    n.quantity = 30;
    n.unitPrice = 5;
    Check(n.TotalCost() == 150, "cost is price times quantity");
    Check(n.AffordableQuantity(b) == 10, "a thin budget buys what it can");
    b.gold = 1000;
    Check(n.AffordableQuantity(b) == 30, "a fat budget buys the whole need");
    n.unitPrice = 0;
    Check(n.AffordableQuantity(b) == 30, "an unpriced need is not zero");
}

void TestResourceNeed() {
    Section("resource needs");
    prog::ResourceNeed n;
    n.graphic = 0x0E21;   // bandages
    n.name = "bandage";
    n.have = 12;
    n.want = 50;
    Check(n.Shortfall() == 38, "the shortfall is what is missing");
    Check(!n.Met(), "and it is not met");
    n.have = 60;
    Check(n.Shortfall() == 0, "a surplus is not a negative shortfall");
    Check(n.Met(), "and it is met");
}

// ---------------------------------------------------------------------------

void TestTrainingSession() {
    Section("training session");
    prog::TrainingSession t;
    i64 now = 1000;

    t.Begin(25, 500, 600, now);
    Check(t.Phase() == prog::TrainingPhase::Training, "begins training");
    Check(t.ShouldAttempt(now), "and may attempt straight away");
    Check(t.GainedTenths() == 0, "with nothing gained yet");

    // Successes and failures both count as attempts: Sphere's Skill_Done and
    // Skill_Fail each call Skill_Experience.
    t.OnAttempt(prog::AttemptOutcome::Success, now += 1000);
    t.OnAttempt(prog::AttemptOutcome::Failed, now += 1000);
    Check(t.Attempts() == 2, "a failed attempt is still an attempt");

    // Gains are only ever taken from a server sample.
    t.OnSkillSample(501, now);
    Check(t.Gains() == 1, "a reported rise is a gain");
    Check(t.GainedTenths() == 1, "and is measured from the start value");

    // Sphere decays a skill to make room near the cap; that is real, not noise.
    t.OnSkillSample(500, now);
    Check(t.CurrentTenths() == 500, "a reported fall is believed");
    Check(t.Gains() == 1, "and is not counted as a gain");

    t.OnSkillSample(600, now);
    Check(t.Phase() == prog::TrainingPhase::Reached, "reaching the target ends it");
    Check(!t.ShouldAttempt(now), "and stops the attempts");
}

void TestTrainingBackoff() {
    Section("training backoff");
    prog::TrainingSession t;
    prog::TrainingLimits lim;
    lim.maxConsecutiveRefusals = 3;
    lim.refusalBackoffMs = 2000;
    lim.throttleBackoffMs = 60000;
    t.SetLimits(lim);

    i64 now = 0;
    t.Begin(25, 500, 1000, now);

    t.OnAttempt(prog::AttemptOutcome::Refused, now);
    Check(t.Phase() == prog::TrainingPhase::BackingOff, "a refusal backs off");
    Check(!t.ShouldAttempt(now + 1000), "and holds for the backoff");
    Check(t.ShouldAttempt(now + 2000), "then allows another attempt");

    // A success clears the streak -- a refusal now and then is ordinary.
    t.OnAttempt(prog::AttemptOutcome::Success, now += 2000);
    Check(t.ConsecutiveRefusals() == 0, "a success clears the refusal streak");

    t.OnAttempt(prog::AttemptOutcome::Refused, now);
    t.OnAttempt(prog::AttemptOutcome::Refused, now);
    t.OnAttempt(prog::AttemptOutcome::Refused, now);
    Check(t.Phase() == prog::TrainingPhase::Blocked,
          "a run of refusals blocks rather than hammering the server");
    Check(!t.ShouldAttempt(now + 1000000), "and stays blocked");
    Check(std::string(t.BlockedReason()).find("refused") != std::string::npos,
          "with a reason worth logging");

    // Throttling is the one M2 measured as actively harmful to retry.
    prog::TrainingSession u;
    u.SetLimits(lim);
    u.Begin(25, 500, 1000, 0);
    u.OnAttempt(prog::AttemptOutcome::Throttled, 0);
    Check(!u.ShouldAttempt(30000), "a throttle waits much longer than a refusal");
    Check(u.ShouldAttempt(60000), "and only then tries again");
}

void TestTrainingResources() {
    Section("training resources");
    prog::TrainingSession t;
    i64 now = 0;
    t.Begin(0, 100, 500, now);

    t.OnResourceExhausted(now);
    Check(t.Phase() == prog::TrainingPhase::NeedResources,
          "running out stops the training, not the session");
    Check(!t.ShouldAttempt(now + 100000),
          "and no amount of waiting makes an empty pack work");

    t.OnResourcesRestocked(now += 1000);
    Check(t.Phase() == prog::TrainingPhase::Training, "restocking resumes it");
    Check(t.ShouldAttempt(now), "and it may attempt again");
}

void TestTrainingStall() {
    Section("training stall");
    prog::TrainingSession t;
    prog::TrainingLimits lim;
    lim.stallAttempts = 10;
    t.SetLimits(lim);
    i64 now = 0;
    t.Begin(25, 500, 1000, now);

    for (int i = 0; i < 9; ++i)
        t.OnAttempt(prog::AttemptOutcome::Success, now += 100);
    Check(!t.Stalled(), "a barren run inside the window is just bad luck");
    t.OnAttempt(prog::AttemptOutcome::Success, now += 100);
    Check(t.Stalled(), "a long barren run is reported as a stall");
    Check(t.Phase() == prog::TrainingPhase::Training,
          "which is information, not a failure");

    t.OnSkillSample(501, now);
    Check(!t.Stalled(), "a gain clears the stall");
}

// ---------------------------------------------------------------------------

void TestTradeSequence() {
    Section("secure trade sequence");
    trade::TradeState t;
    i64 now = 1000;

    Check(!t.Active(), "no trade to start with");
    Check(t.CurrentPhase() == trade::Phase::None, "and no phase");

    t.OnOpened(0x1234, "RevolutionBot02", 0x4000AAAA, 0x4000BBBB, now);
    Check(t.Active(), "opening makes it active");
    Check(t.CurrentPhase() == trade::Phase::Open, "in the open phase");
    Check(t.PartnerSerial() == 0x1234, "the partner is remembered");
    Check(t.PartnerName() == "RevolutionBot02", "and so is their name");
    Check(t.MyContainer() == 0x4000AAAA, "our window is ours");
    Check(t.TheirContainer() == 0x4000BBBB, "and theirs is theirs");

    // Contents come from the ordinary container packets.
    t.OnItemAdded(0x4000AAAA, 0x40001111);
    t.OnItemAdded(0x4000BBBB, 0x40002222);
    Check(t.MyOffer().size() == 1 && t.TheirOffer().size() == 1,
          "each side's offer is tracked separately");
    Check(t.Offering(0x40001111) && t.Offering(0x40002222),
          "both items are on the table");
    t.OnItemAdded(0x4000AAAA, 0x40001111);
    Check(t.MyOffer().size() == 1, "a repeated add is not a second item");
    t.OnItemAdded(0x4000CCCC, 0x40003333);
    Check(!t.Offering(0x40003333),
          "an item in some other container is not in the trade");

    // Accepting.
    t.NoteCheckSent(true, now);
    Check(t.CheckSent(), "we know we asked");
    t.OnCheckChanged(true, false, now);
    Check(t.CurrentPhase() == trade::Phase::Accepted, "the server confirms ours");
    Check(!t.CheckSent(), "and the request is no longer outstanding");
    Check(!t.TheirCheck(), "the partner has not accepted yet");

    t.OnCheckChanged(true, true, now);
    Check(t.TheirCheck(), "then they do");
    Check(t.Active(), "and the trade is still open until the server closes it");

    t.OnClosed(trade::CloseReason::BothAccepted, now);
    Check(t.CurrentPhase() == trade::Phase::Completed, "closing completes it");
    Check(t.Reason() == trade::CloseReason::BothAccepted, "for the right reason");
    Check(!t.Active(), "and it is no longer active");
}

void TestTradeAcceptReset() {
    Section("trade acceptance reset");
    trade::TradeState t;
    i64 now = 0;
    t.OnOpened(0x1234, "partner", 0xA, 0xB, now);
    t.OnItemAdded(0xB, 0x1111);
    t.OnCheckChanged(true, false, now);
    Check(t.CurrentPhase() == trade::Phase::Accepted, "we have accepted");

    // The partner changes what is on the table. Sphere would let the trade
    // complete anyway; this client will not stay accepted for goods it did not
    // agree to.
    t.OnItemAdded(0xB, 0x2222);
    Check(t.CurrentPhase() == trade::Phase::Open, "adding an item retracts us");
    Check(!t.MyCheck(), "our check is cleared locally");
    Check(t.AcceptResets() == 1, "and the retraction is counted");

    t.OnCheckChanged(true, false, now);
    t.OnItemRemoved(0x2222);
    Check(t.AcceptResets() == 2, "removing one retracts us too");
    Check(t.TheirOffer().size() == 1, "and the offer shrinks");

    // A change to something not in the trade is not a change to the trade.
    t.OnCheckChanged(true, false, now);
    t.OnItemRemoved(0x9999);
    Check(t.AcceptResets() == 2, "an unrelated item does not retract us");

    // ...but once BOTH boxes are ticked the retraction must stop firing. The
    // server is committing at that point, and the item movements that follow
    // are it handing the goods over. m3_trade4 hit exactly this live: both
    // sides accepted, the goods moved, the local rule retracted, and the CLOSE
    // that ended a successful sale was reported as `partner_cancelled`.
    trade::TradeState c;
    c.OnOpened(0x1234, "partner", 0xA, 0xB, 0);
    c.OnItemAdded(0xA, 0x1111);
    c.OnItemAdded(0xB, 0x2222);
    c.OnCheckChanged(true, true, 0);
    Check(c.BothAccepted(), "both boxes ticked is latched");

    const int resetsBefore = c.AcceptResets();
    c.OnItemRemoved(0x1111);
    c.OnItemRemoved(0x2222);
    Check(c.AcceptResets() == resetsBefore,
          "the commit's own item movements do not count as a retraction");
    Check(c.BothAccepted(), "and the latch survives them");

    c.OnCheckChanged(false, false, 0);
    Check(c.BothAccepted(), "clearing the boxes during the commit does not unlatch");

    c.OnClosed(trade::CloseReason::BothAccepted, 0);
    Check(c.CurrentPhase() == trade::Phase::Completed,
          "so the close is read as the completion it was");

    // The latch is per-trade, not per-session.
    c.OnOpened(0x5678, "someone else", 0xC, 0xD, 0);
    Check(!c.BothAccepted(), "a new trade starts unlatched");
}

void TestTradeCancellation() {
    Section("trade cancellation");
    trade::TradeState t;
    t.OnOpened(0x1234, "partner", 0xA, 0xB, 0);
    t.OnItemAdded(0xA, 0x1111);
    t.OnClosed(trade::CloseReason::PartnerCancelled, 100);
    Check(t.CurrentPhase() == trade::Phase::Cancelled, "a cancel is not a completion");
    Check(t.Reason() == trade::CloseReason::PartnerCancelled, "with the reason kept");
    Check(!t.Active(), "and the trade is over");

    // Events after the close must not resurrect it.
    t.OnItemAdded(0xA, 0x2222);
    t.OnCheckChanged(true, true, 200);
    Check(t.CurrentPhase() == trade::Phase::Cancelled,
          "a closed trade ignores late packets");

    trade::TradeState u;
    u.OnOpened(0x1234, "partner", 0xA, 0xB, 0);
    u.OnClosed(trade::CloseReason::PartnerGone, 100);
    Check(u.Reason() == trade::CloseReason::PartnerGone,
          "a vanished partner is its own reason");
}

void TestTradeIsolation() {
    Section("trade session isolation");
    // Two characters trading at once in one process must not see each other's
    // window. The state is a plain Client member for exactly this reason.
    trade::TradeState a, b;
    a.OnOpened(0x1111, "alpha", 0xA1, 0xA2, 0);
    b.OnOpened(0x2222, "beta", 0xB1, 0xB2, 0);

    a.OnItemAdded(0xA1, 0xDEAD);
    Check(a.MyOffer().size() == 1, "A sees its own offer");
    Check(b.MyOffer().empty(), "B does not");
    Check(b.PartnerSerial() == 0x2222, "and keeps its own partner");

    a.OnCheckChanged(true, true, 0);
    Check(!b.MyCheck() && !b.TheirCheck(), "acceptance does not cross sessions");

    a.OnClosed(trade::CloseReason::BothAccepted, 0);
    Check(b.Active(), "closing one does not close the other");

    a.Reset();
    Check(a.CurrentPhase() == trade::Phase::None, "reset clears a session");
    Check(b.Active(), "and still leaves the other alone");
}

}  // namespace

int main() {
    std::printf("m3 progression / economy / trade tests\n\n");
    TestBuildCaps();
    TestProgressionPlan();
    TestBudget();
    TestResourceNeed();
    TestTrainingSession();
    TestTrainingBackoff();
    TestTrainingResources();
    TestTrainingStall();
    TestTradeSequence();
    TestTradeAcceptReset();
    TestTradeCancellation();
    TestTradeIsolation();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
