// tests/strategy_combat.cpp -- docs/BOT_ARCHITECTURE.md sections 21, 54.
//
// The one deliberate exception to "professions are data": a mage and a
// warrior in a fight are different ALGORITHMS, not the same behaviour with
// different numbers. Five strategies serve seventeen professions.
//
// This does NOT test whether a fight is legal -- that is combat/Targeting.cpp
// and its 47 checks. This is "the fight is on and lawful; what now".
//
// No server, no MULs.

#include "uo/strategies/combat_strategy.h"

#include <cstdio>
#include <initializer_list>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void ExpectMove(const CombatDecision& got, CombatMove want, const char* what) {
    ++g_checks;
    if (got.move != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    CombatMoveName(want), CombatMoveName(got.move),
                    got.reason);
        ++g_failures;
    }
}

CombatSight Healthy() {
    CombatSight s;
    s.hp = 100; s.hpMax = 100;
    s.mana = 50; s.manaMax = 50;
    s.foeDistance = 1;
    s.armed = true;
    return s;
}

CombatTuning Default() { return CombatTuning{}; }

// 0.32 is the only number here with evidence behind it: a character
// disengaged at roughly a third of its health in M3.9.1 and survived.
void TestEveryoneBreaksOffWhenHurt() {
    std::printf("[combat: nobody fights to the death]\n");
    for (CombatStrategyId s : {CombatStrategyId::Melee, CombatStrategyId::Ranged,
                               CombatStrategyId::Mage, CombatStrategyId::Tamer}) {
        CombatSight hurt = Healthy();
        hurt.hp = 30;              // 30% against a 32% floor
        hurt.petAlive = true;
        ExpectMove(DecideCombat(s, hurt, Default()), CombatMove::Disengage,
                   "below the flee threshold, whatever the strategy");
    }
}

// A character happy to fight one thing at 40% is not happy to fight three.
void TestOutnumberedRaisesTheBar() {
    std::printf("[combat: being outnumbered raises the bar to keep fighting]\n");
    CombatSight one = Healthy();
    one.hp = 45;
    one.attackersOnMe = 1;
    ExpectMove(DecideCombat(CombatStrategyId::Melee, one, Default()),
               CombatMove::Swing, "45% against one foe: fight");

    CombatSight three = one;
    three.attackersOnMe = 3;
    ExpectMove(DecideCombat(CombatStrategyId::Melee, three, Default()),
               CombatMove::Disengage, "45% against three: leave");

    // ...and a braver character stays in longer. Personality, per section 31.
    CombatTuning brave = Default();
    brave.riskTolerance = 1.0;
    ExpectMove(DecideCombat(CombatStrategyId::Melee, three, brave),
               CombatMove::Swing, "the same odds, a bolder character");
}

// "for crafter upgrade gear just wear normal clothing" -- a tailor that
// trades blows is a tailor that loses its tools.
void TestACrafterLeaves() {
    std::printf("[combat: a crafter's answer to a fight is to leave it]\n");
    ExpectMove(DecideCombat(CombatStrategyId::AvoidCombat, Healthy(), Default()),
               CombatMove::Disengage, "even at full health and armed");
}

void TestMeleeClosesAndBandages() {
    std::printf("[combat: a warrior closes, swings, and bandages in place]\n");
    CombatSight far = Healthy();
    far.foeDistance = 5;
    ExpectMove(DecideCombat(CombatStrategyId::Melee, far, Default()),
               CombatMove::CloseIn, "a sword only reaches one tile");

    CombatSight hurt = Healthy();
    hurt.hp = 70;                 // under the 80% heal bar
    hurt.bandages = 12;
    ExpectMove(DecideCombat(CombatStrategyId::Melee, hurt, Default()),
               CombatMove::Bandage, "hurt with bandages in the pack");

    CombatSight noBandages = hurt;
    noBandages.bandages = 0;
    ExpectMove(DecideCombat(CombatStrategyId::Melee, noBandages, Default()),
               CombatMove::Swing, "hurt with nothing to patch up: press on");
}

// THE BOW NEEDS BOTH HANDS. Closing is not a tactical choice for an archer,
// it is the end of its damage -- which is also why the catalogue gives
// archers no shield.
void TestAnArcherBacksAway() {
    std::printf("[combat: an archer in somebody's face has already lost]\n");
    CombatSight adjacent = Healthy();
    ExpectMove(DecideCombat(CombatStrategyId::Ranged, adjacent, Default()),
               CombatMove::BackOff, "a bow is useless at one tile");

    CombatSight atRange = Healthy();
    atRange.foeDistance = 6;
    ExpectMove(DecideCombat(CombatStrategyId::Ranged, atRange, Default()),
               CombatMove::Shoot, "at its preferred range");

    CombatSight tooFar = Healthy();
    tooFar.foeDistance = 12;
    ExpectMove(DecideCombat(CombatStrategyId::Ranged, tooFar, Default()),
               CombatMove::CloseIn, "out of range entirely");
}

// Metal armour ends casting on this shard, which is why the catalogue dresses
// a mage in cloth -- and why a mage in melee has already lost.
void TestAMageKitesAndMeditates() {
    std::printf("[combat: a mage keeps the distance and watches its mana]\n");
    CombatSight adjacent = Healthy();
    ExpectMove(DecideCombat(CombatStrategyId::Mage, adjacent, Default()),
               CombatMove::BackOff, "nothing gets into melee with a caster");

    CombatSight atRange = Healthy();
    atRange.foeDistance = 6;
    ExpectMove(DecideCombat(CombatStrategyId::Mage, atRange, Default()),
               CombatMove::CastAttack, "range and mana: cast");

    CombatSight hurt = atRange;
    hurt.hp = 60;
    ExpectMove(DecideCombat(CombatStrategyId::Mage, hurt, Default()),
               CombatMove::CastHeal, "a mage heals itself rather than bleeds");

    // Meditating in contact is how a mage dies.
    CombatSight emptyClose = Healthy();
    emptyClose.mana = 0;
    emptyClose.foeDistance = 3;
    ExpectMove(DecideCombat(CombatStrategyId::Mage, emptyClose, Default()),
               CombatMove::BackOff, "empty and too close to sit down");

    CombatSight emptyFar = emptyClose;
    emptyFar.foeDistance = 8;
    ExpectMove(DecideCombat(CombatStrategyId::Mage, emptyFar, Default()),
               CombatMove::Meditate, "empty and safely out of reach");
}

// The pet fights. A tamer that trades blows is a tamer without a pet shortly
// afterwards.
void TestATamerFightsThroughItsPet() {
    std::printf("[combat: the pet fights, the tamer does not]\n");
    CombatSight withPet = Healthy();
    withPet.petAlive = true;
    withPet.petHpFraction = 0.9;
    withPet.foeDistance = 6;
    ExpectMove(DecideCombat(CombatStrategyId::Tamer, withPet, Default()),
               CombatMove::CommandPet, "set it on the foe");

    CombatSight petHurt = withPet;
    petHurt.petHpFraction = 0.3;
    ExpectMove(DecideCombat(CombatStrategyId::Tamer, petHurt, Default()),
               CombatMove::CastHeal, "the pet is what needs healing");

    CombatSight tooClose = withPet;
    tooClose.foeDistance = 1;
    ExpectMove(DecideCombat(CombatStrategyId::Tamer, tooClose, Default()),
               CombatMove::BackOff, "let the pet hold it");

    CombatSight noPet = Healthy();
    noPet.petAlive = false;
    ExpectMove(DecideCombat(CombatStrategyId::Tamer, noPet, Default()),
               CombatMove::Disengage, "a tamer without a pet is not a fighter");
}

void TestEveryDecisionSaysWhy() {
    std::printf("[combat: no silent decisions]\n");
    for (int i = 0; i <= static_cast<int>(CombatStrategyId::Tamer); ++i) {
        const auto s = static_cast<CombatStrategyId>(i);
        const CombatDecision d = DecideCombat(s, Healthy(), Default());
        Expect(d.reason && d.reason[0], "the decision states its reasoning");
        Expect(CombatStrategyName(s)[0] != '?', "every strategy has a name");
    }
    for (int i = 0; i <= static_cast<int>(CombatMove::Wait); ++i)
        Expect(CombatMoveName(static_cast<CombatMove>(i))[0] != '?',
               "every move has a name");
}

}  // namespace

int main() {
    std::printf("strategy_combat\n");
    TestEveryoneBreaksOffWhenHurt();
    TestOutnumberedRaisesTheBar();
    TestACrafterLeaves();
    TestMeleeClosesAndBandages();
    TestAnArcherBacksAway();
    TestAMageKitesAndMeditates();
    TestATamerFightsThroughItsPet();
    TestEveryDecisionSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
