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
    s.hp = 60;
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

// Faustus, 2026-09-05 01:38: raised by Papua's healer at 21/50 with 8,814 gold
// and no bandages. Papua is unguarded in the shard's own area files, so the
// rest-before-shopping gate fired and he stood beside the healer's counter
// for ten minutes at Regen0=40. The gate exists to keep a wounded character
// off a cross-map circuit; a counter a screen away is not a circuit.
void TestACounterRightHereIsNotAShoppingTrip() {
    std::printf("[heal: a bandage counter next door beats resting at 1hp/40s]\n");
    HealSight s = Hurt();
    s.hp = 21; s.hpMax = 50;   // 42%, under minHpToShop
    s.gold = 8814;
    s.supplyDistance = 12;
    ExpectStep(DecideHeal(s, Default()), HealStep::BuySupplies,
               "the healer's counter is twelve tiles off -- buy, do not rest");

    s.supplyDistance = 300;
    ExpectStep(DecideHeal(s, Default()), HealStep::Rest,
               "a counter across the map is the circuit the gate was written for");

    s.supplyDistance = -1;
    ExpectStep(DecideHeal(s, Default()), HealStep::Rest,
               "unknown distance keeps the old, cautious answer");

    s.supplyDistance = 12;
    s.canBuySupplies = false;
    ExpectStep(DecideHeal(s, Default()), HealStep::Rest,
               "a build that does not use bandages still rests");
}

// "if warrior economy is good then he can buy bandage and potion, otherwise
// go get yourself wool make bandage" (project owner). The POOR branch, and
// only the poor branch.
void TestThePoorBranch() {
    std::printf("[heal: make bandages when you cannot buy them, not before]\n");
    HealSight poor = Hurt();
    poor.hp = 60;
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

void TestResurrectionAndBuildResources() {
    HealSight s = Hurt();
    s.hp = 12; s.gold = 5000;
    ExpectStep(DecideHeal(s, Default()), HealStep::Rest,
               "fresh resurrection does not start an equipment shopping circuit");
    s.hungry = true;
    ExpectStep(DecideHeal(s, Default()), HealStep::Stuck,
               "starving resurrection needs food, not an endless rest");
    s.hungry = false; s.bandages = 20; s.useBandages = false;
    s.healPotions = 3;
    ExpectStep(DecideHeal(s, Default()), HealStep::DrinkPotion,
               "crafter does not use incidental or veterinary bandages");
    s.canCastHeal = true; s.mana = 20;
    ExpectStep(DecideHeal(s, Default()), HealStep::CastHeal,
               "caster uses its supplied healing spell before potion reserves");
    s.useBandages = true;
    ExpectStep(DecideHeal(s, Default()), HealStep::Bandage,
               "Healing build can use bandages even immediately after resurrection");
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
    TestACounterRightHereIsNotAShoppingTrip();
    TestThePoorBranch();
    TestTheDeadlockIsNamed();
    TestResurrectionAndBuildResources();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
