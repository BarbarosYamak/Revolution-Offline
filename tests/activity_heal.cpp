// tests/activity_heal.cpp -- docs/BOT_ARCHITECTURE.md sections 3 and 47.
//
// Four ways to patch up and one decision. The case that matters most is the
// last one: Kaelen's deadlock, which nothing in the old code could name.
//
// No server, no MULs.

#include "uo/activities/heal.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void ExpectStep(const HealPlan& got, HealStep want, const char* what) {
    ++g_checks;
    if (got.step != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    HealStepName(want), HealStepName(got.step), got.reason);
        ++g_failures;
    }
}

HealSight Hurt() {
    HealSight s;
    s.hp = 40; s.hpMax = 100;
    return s;
}

HealTuning Default() { return HealTuning{}; }

void TestHealthyDoesNothing() {
    std::printf("[heal: healthy enough is an answer, not an oversight]\n");
    HealSight fine = Hurt();
    fine.hp = 85;
    fine.bandages = 30;
    ExpectStep(DecideHeal(fine, Default()), HealStep::None,
               "85% with a pack full of bandages");
}

void TestBandagesFirst() {
    std::printf("[heal: bandages are what a warrior carries them for]\n");
    HealSight s = Hurt();
    s.bandages = 12;
    s.canCastHeal = true;
    s.mana = 50;
    ExpectStep(DecideHeal(s, Default()), HealStep::Bandage,
               "bandages before mana, which is worth more elsewhere");
}

// Mid-fight a bandage takes seconds it does not have. Order by DANGER, not by
// cost.
void TestDangerChangesTheOrder() {
    std::printf("[heal: under attack, only a potion is fast enough]\n");
    HealSight s = Hurt();
    s.bandages = 12;
    s.healPotions = 3;
    s.inDanger = true;
    ExpectStep(DecideHeal(s, Default()), HealStep::DrinkPotion,
               "attacked, with both available");

    s.inDanger = false;
    ExpectStep(DecideHeal(s, Default()), HealStep::Bandage,
               "and out of the fight, the cheap one again");
}

void TestAMageCastsWhenItHasNothingElse() {
    std::printf("[heal: a caster does without bandages]\n");
    HealSight s = Hurt();
    s.canCastHeal = true;
    s.mana = 20;
    ExpectStep(DecideHeal(s, Default()), HealStep::CastHeal,
               "no bandages, but the mana to manage");

    s.mana = 0;
    ExpectStep(DecideHeal(s, Default()), HealStep::Rest,
               "and with no mana either, sit down");
}

void TestMoneyFixesIt() {
    std::printf("[heal: nothing to hand is not a problem if there is gold]\n");
    HealSight s = Hurt();
    s.gold = 500;
    ExpectStep(DecideHeal(s, Default()), HealStep::BuySupplies,
               "buy what is missing");

    // ...but not the reserve. That is the money that replaces a tool after a
    // death, and spending it here is how a character becomes unemployable.
    HealTuning guarded = Default();
    guarded.goldReserve = 500;
    ExpectStep(DecideHeal(s, guarded), HealStep::Rest,
               "the whole purse is reserve; do not raid it for bandages");
}

// "if warrior economy is good then he can buy bandage and potion, otherwise
// go get yourself wool make bandage" (project owner). The POOR branch, and
// only the poor branch.
void TestThePoorBranch() {
    std::printf("[heal: make bandages when you cannot buy them, not before]\n");
    HealSight poor = Hurt();
    poor.gold = 1;
    poor.hasBandageMaterial = true;
    ExpectStep(DecideHeal(poor, Default()), HealStep::MakeBandages,
               "no money, but cloth to cut up");

    HealSight rich = poor;
    rich.gold = 5000;
    ExpectStep(DecideHeal(rich, Default()), HealStep::BuySupplies,
               "with money, buying is the shorter errand");
}

// KAELEN'S DEADLOCK. He climbed from 10/32 to 25/32 -- 78%, two points short
// of the bar that would have let him hunt -- and idled for 73% of his picks
// while every other need reported BLOCKED. Nothing was broken; every
// individual refusal was correct. What was missing was any way to say "all
// four doors are shut AND resting cannot open them".
void TestTheDeadlockIsNamed() {
    std::printf("[heal: hungry, broke and empty is STUCK, not resting]\n");
    HealSight kaelen = Hurt();
    kaelen.hp = 25; kaelen.hpMax = 32;
    kaelen.bandages = 0;
    kaelen.healPotions = 0;
    kaelen.gold = 0;
    kaelen.hasBandageMaterial = false;
    kaelen.hungry = true;

    const HealPlan p = DecideHeal(kaelen, Default());
    ExpectStep(p, HealStep::Stuck, "hunger stops the regeneration he is waiting on");
    Expect(p.step != HealStep::Rest,
           "and calling it Rest is what hid a whole session");
    Expect(p.reason && p.reason[0], "with a reason that names the cause");

    // Feed him and the same state becomes an honest wait.
    kaelen.hungry = false;
    ExpectStep(DecideHeal(kaelen, Default()), HealStep::Rest,
               "fed, regeneration works, so waiting is real");
}

void TestEveryPlanSaysWhy() {
    std::printf("[heal: no silent decisions]\n");
    const HealPlan cases[] = {
        DecideHeal(Hurt(), Default()),
        DecideHeal(HealSight{}, Default()),
    };
    for (const HealPlan& p : cases)
        Expect(p.reason && p.reason[0], "the plan states its reasoning");
    for (int i = 0; i <= static_cast<int>(HealStep::Stuck); ++i)
        Expect(HealStepName(static_cast<HealStep>(i))[0] != '?',
               "every step has a name");
}

}  // namespace

int main() {
    std::printf("activity_heal\n");
    TestHealthyDoesNothing();
    TestBandagesFirst();
    TestDangerChangesTheOrder();
    TestAMageCastsWhenItHasNothingElse();
    TestMoneyFixesIt();
    TestThePoorBranch();
    TestTheDeadlockIsNamed();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
