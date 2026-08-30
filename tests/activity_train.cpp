// tests/activity_train.cpp -- docs/BOT_ARCHITECTURE.md sections 18, 20.
//
// "npc training and training are different, normal training is doing actions
// to level up your skill to 100" (project owner). Every case below is a
// session this shard actually produced.
//
// No server, no MULs.

#include "uo/activities/train.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void ExpectStep(const TrainPlan& got, TrainStep want, const char* what) {
    ++g_checks;
    if (got.step != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    TrainStepName(want), TrainStepName(got.step), got.reason);
        ++g_failures;
    }
}

// Meditation, the skill Ysolde lost. An ordinary mage vendor rolls
// MEDITATION={50 75} and teaches 30% of its own value, so it caps near 22.5.
TrainRequest Meditation(uo::i32 ceiling, uo::i32 gold, uo::i32 fee) {
    TrainRequest r;
    r.skillId = 46;
    r.targetTenths = 800;        // the build wants 80.0
    r.npcCeilingTenths = ceiling;
    r.gold = gold;
    r.feeQuoted = fee;
    return r;
}

// THE YSOLDE CASE. At 21.9 against an ordinary vendor's ~22.5 ceiling there
// is almost nothing left to buy -- and at 22.5 there is nothing at all. The
// right answer is to go and DO it, not to conclude the skill is unbuyable
// and never train it again.
void TestAboveTheCeilingThereIsOnlyPractice() {
    std::printf("[train: past what an NPC can teach, only doing it works]\n");
    ExpectStep(DecideTrain(Meditation(225, 5000, 300), /*have=*/225),
               TrainStep::Practise, "exactly at the vendor ceiling");
    ExpectStep(DecideTrain(Meditation(225, 5000, 300), 240),
               TrainStep::Practise, "above it");

    const TrainPlan p = DecideTrain(Meditation(225, 5000, 300), 219);
    ExpectStep(p, TrainStep::Buy, "21.9 is still below 22.5, so buy the rest");
    Expect(p.method == TrainMethod::BuyFromNpc, "from an NPC");
}

// A guildmaster carries TAG.OVERRIDE.TRAINSKILLMAX=50.0 and teaches to 30.0,
// so the SAME character at the SAME skill gets a different answer depending
// on who is standing there. That is why the ceiling is a parameter.
void TestTheCeilingIsPerNpc() {
    std::printf("[train: the ceiling belongs to the trainer, not the skill]\n");
    ExpectStep(DecideTrain(Meditation(225, 5000, 300), 224),
               TrainStep::Buy, "just under an ordinary vendor's ceiling");
    ExpectStep(DecideTrain(Meditation(225, 5000, 300), 260),
               TrainStep::Practise, "past the vendor");
    ExpectStep(DecideTrain(Meditation(300, 5000, 300), 260),
               TrainStep::Buy, "but a guildmaster can still teach 26.0");
}

// "all these time it couldnt just mine smelt smith sell" -- a miner with a
// pickaxe and two 50.0 skills spent a session walking between tinkers because
// buying tenths outscored doing the job.
void TestSomeSkillsAreGroundNotBought() {
    std::printf("[train: a build may prefer to grind a skill]\n");
    TrainRequest r = Meditation(225, 5000, 300);
    r.worthBuying = false;
    ExpectStep(DecideTrain(r, 100), TrainStep::Practise,
               "cheap to grind, so grind it");
}

void TestAffordability() {
    std::printf("[train: too poor today is not the same as unbuyable]\n");
    const TrainPlan poor = DecideTrain(Meditation(225, 50, 300), 100);
    ExpectStep(poor, TrainStep::CannotAfford, "50 gold, 300 gold lesson");
    Expect(poor.method == TrainMethod::BuyFromNpc,
           "the method is still the right one; the purse is the problem");

    ExpectStep(DecideTrain(Meditation(225, 300, 300), 100), TrainStep::Buy,
               "exactly enough is enough");
}

void TestTargetMet() {
    std::printf("[train: a met target is done, whatever a trainer offers]\n");
    TrainRequest r = Meditation(300, 9999, 100);
    r.targetTenths = 500;
    ExpectStep(DecideTrain(r, 500), TrainStep::Done, "at target");
    ExpectStep(DecideTrain(r, 640), TrainStep::Done, "past target");
}

void TestEveryPlanSaysWhy() {
    std::printf("[train: no silent decisions]\n");
    const TrainPlan cases[] = {
        DecideTrain(Meditation(225, 5000, 300), 900),
        DecideTrain(Meditation(225, 5000, 300), 100),
        DecideTrain(Meditation(225, 5000, 300), 260),
        DecideTrain(Meditation(225, 10, 300), 100),
    };
    for (const TrainPlan& p : cases)
        Expect(p.reason && p.reason[0], "the plan states its reasoning");
    for (int i = 0; i <= static_cast<int>(TrainStep::CannotAfford); ++i)
        Expect(TrainStepName(static_cast<TrainStep>(i))[0] != '?',
               "every step has a name");
    for (int i = 0; i <= static_cast<int>(TrainMethod::Practise); ++i)
        Expect(TrainMethodName(static_cast<TrainMethod>(i))[0] != '?',
               "every method has a name");
}

}  // namespace

int main() {
    std::printf("activity_train\n");
    TestAboveTheCeilingThereIsOnlyPractice();
    TestTheCeilingIsPerNpc();
    TestSomeSkillsAreGroundNotBought();
    TestAffordability();
    TestTargetMet();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
