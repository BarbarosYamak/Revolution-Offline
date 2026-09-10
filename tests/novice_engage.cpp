// tests/novice_engage.cpp -- the novice engagement policy.
//
// A table test over src/life/runner/NoviceEngage.h: who counts as a novice,
// when a novice may open a fight, when it must break contact, and how much
// health a retreat needs. Pure -- no Client, no server, no world data.
//
// Every expectation below is anchored to one of the two 2026-09-06 Hector
// deaths in the WEAK band of the Britain graveyard (fencer, 51 hp, Fencing
// 50.7, starter leather); see artifacts/novice_engagement_2026-09-06.md.

#include "life/runner/NoviceEngage.h"
#include "uo/combat_policy.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void CheckPct(double got, double lo, double hi, const char* what) {
    ++g_checks;
    if (got < lo || got > hi) {
        std::printf("  FAIL: %s -- got %.3f, wanted [%.3f, %.3f]\n",
                    what, got, lo, hi);
        ++g_failures;
    }
}

using namespace uo::life;

// The character that actually died, twice.
constexpr int kHectorWeaponTenths = 507;   // Fencing 50.7
constexpr int kHectorHpMax        = 51;

void TestWhoIsANovice() {
    std::printf("who is a novice\n");
    Check(novice::IsNovice(kHectorWeaponTenths, kHectorHpMax),
          "Hector (507 tenths, 51 hp) is inside the novice band");
    Check(novice::IsNovice(900, 51),
          "a skilled character with a 51-hp bar is still novice-fragile");
    Check(novice::IsNovice(507, 120),
          "a tough character at 50.7 weapon skill is still a novice fighter");
    Check(!novice::IsNovice(700, 90),
          "70.0 weapon skill and 90 hp is out of the novice band");
    Check(!novice::IsNovice(novice::kNoviceWeaponTenths, novice::kNoviceHpMax),
          "the band bounds themselves are exclusive");
}

void TestRetreatFloor() {
    std::printf("retreat floor (expected hits to live)\n");
    // 8 hp a blow, two blows of margin, per hostile that can reach us.
    Check(novice::RetreatFloorHp(1) == 16, "one attacker needs 16 hp");
    Check(novice::RetreatFloorHp(2) == 32, "two attackers need 32 hp");
    Check(novice::RetreatFloorHp(3) == 48, "three attackers need 48 hp");
    Check(novice::RetreatFloorHp(0) == 16,
          "a zero board is treated as one attacker, never as free");

    // TRACE A, 2026-09-06 02:16: two Skeletons, flee fired at 25% of 51 hp
    // (13 hp) and Hector died one second later. The floor must be far above
    // that, and above the 30% the old rule printed as its bail line.
    CheckPct(novice::RetreatFloorFraction(2, kHectorHpMax), 0.60, 0.64,
             "two attackers on a 51-hp bar bail at ~63%, not at 25/30%");
    Check(novice::RetreatFloorFraction(2, kHectorHpMax) > 0.30,
          "the 02:16 'bail at 30%' line is below the floor that would "
          "have saved him");

    // TRACE B, 2026-09-06 23:38: Skeleton + Skeleton + Spectre, last observed
    // at 21/51 (41%) with "bail at 22%", dead 3.1 s later.
    Check(novice::RetreatFloorFraction(3, kHectorHpMax) > 0.41,
          "three attackers put a 51-hp bar past the floor at 41% health");
    CheckPct(novice::RetreatFloorFraction(3, kHectorHpMax), 0.90, 0.90,
             "and the floor is clamped at 90%, the cap the survival code "
             "already uses");

    // MONOTONICITY: this line may only ever make a character flee EARLIER.
    // A 100-hp veteran with one attacker sits below the existing 0.32 default,
    // so nothing that survives today is made bolder.
    CheckPct(novice::RetreatFloorFraction(1, 100), 0.15, 0.17,
             "a 100-hp character with one attacker floors at 16%");
    Check(novice::RetreatFloorFraction(1, 100) < 0.32,
          "under one attacker the veteran's nerve line still wins");
    Check(novice::RetreatFloorFraction(1, 0) == 0.0,
          "an unknown hp bar cannot manufacture a threshold");
}

void TestOpeningAFight() {
    std::printf("opening a fight\n");
    // NoviceMayOpen(attackersOnMe, inReach). in_reach is counted at
    // combat::kCrowdRadius (4).
    Check(novice::NoviceMayOpen(0, 1),
          "nothing on us and one hostile in reach: this is a duel");
    Check(novice::NoviceMayOpen(1, 1),
          "one thing already swinging is a fight we are in, not a group");
    // THE HECTOR CASE, 2026-09-07: attackers=1, in_reach=2, hp 100%. The
    // first version of this policy refused it five times and he fought
    // nothing at all in ten minutes (g_Hector.console.txt:108,128,632,1106,
    // 1127,1129). A graveyard always has a second skeleton in the yard.
    Check(novice::NoviceMayOpen(1, 2),
          "a bystander within reach costs a higher retreat floor, not the "
          "fight");
    Check(!novice::NoviceMayOpen(2, 2),
          "two things already on us is not a duel -- the 02:16 Skeleton pair");
    Check(!novice::NoviceMayOpen(1, 3),
          "the 23:38 board (Skeleton, Skeleton, Spectre) is three within "
          "reach and is still refused");
    Check(!novice::NoviceMayOpen(0, 4),
          "a pack is refused even when none of it has swung yet");
    // Company is not free: the retreat floor rises with the board, which is
    // what pays for opening next to a bystander at all.
    Check(novice::RetreatFloorFraction(2, kHectorHpMax) >
              novice::RetreatFloorFraction(1, kHectorHpMax),
          "a second hostile in reach buys fewer hits-to-live, not a veto");
}

void TestBreakingContact() {
    std::printf("breaking contact\n");
    Check(!novice::NoviceMustDisengage(1, 1),
          "one attacker, one in reach: keep fighting");
    Check(!novice::NoviceMustDisengage(1, 2),
          "a bystander in reach does not end a duel -- that refusal is what "
          "gave Hector 0 fights and 0 kills on 2026-09-07");
    Check(!novice::NoviceMustDisengage(0, 2),
          "nothing is attacking us at all: there is nothing to break off");
    Check(novice::NoviceMustDisengage(2, 2),
          "a second ATTACKER means leave now, at whatever health");
    // OWNER RULING 2026-09-07 ("novice rule too strict"): the 3+ ceiling
    // refuses to OPEN on that board, but it does not end a duel already under
    // way. Hector, 2026-09-07 01:11-01:15: three engagements, every one ended
    // `disengage=yes attackers=1 in_reach=3 ... "3+ hostiles within reach"` at
    // 100% health, zero kills (g_Hector.console.txt:518,539,946,967).
    Check(!novice::NoviceMustDisengage(1, 3),
          "one attacker with three in reach: keep fighting -- the third "
          "skeleton in the yard is not what kills you");
    Check(novice::NoviceMustDisengage(3, 3),
          "the 23:38 trio is a disengage -- because three are SWINGING");
    Check(!novice::NoviceMustDisengage(0, 0),
          "an empty board is not a disengage");
    // The board is still refused before the fight starts.
    Check(!novice::NoviceMayOpen(1, 3),
          "the ceiling survives where it belongs: nobody OPENS on three");

    // A two-on-one never becomes a fight.  A fully healthy combat trainee may
    // change to another vetted lane; everybody else keeps the full retreat.
    Check(novice::MayRepositionWithinHunt(true, true, 2, 1, 1.0),
          "a healthy trainee changes lanes after a two-on-one");
    Check(!novice::MayRepositionWithinHunt(true, true, 2, 1, 0.84),
          "an injured novice takes the full safety retreat");
    Check(!novice::MayRepositionWithinHunt(true, false, 2, 1, 1.0),
          "only an active combat trainee may change hunt lanes");
    Check(!novice::MayRepositionWithinHunt(false, true, 2, 1, 1.0),
          "seasoned crowd handling does not borrow the novice lane rule");
}

// COMPANY COSTS A BLOW OF MARGIN, NOT THE WHOLE YARD.
//
// The retreat board used to be max(attackers, inReach), which put a 51-hp
// novice's floor at 94% the moment a third skeleton drifted in -- ending the
// fight on health one tick after the in-reach veto stopped ending it directly.
// A bystander is not swinging, and bystanders join one at a time.
void TestRetreatBoard() {
    std::printf("what the retreat has to survive\n");
    Check(novice::RetreatBoard(1, 1) == 1,
          "a clean duel is one hostile's worth of margin");
    Check(novice::RetreatBoard(1, 3) == 2,
          "one attacker and a busy yard: one extra blow of margin, not two");
    Check(novice::RetreatBoard(1, 8) == 2,
          "eight bystanders buy the same one blow -- they join one at a time");
    Check(novice::RetreatBoard(2, 2) == 2,
          "two attackers and nobody else: the attackers are the board");
    Check(novice::RetreatBoard(3, 3) == 3, "three swinging is three");
    Check(novice::RetreatBoard(0, 0) == 1, "the board is never below one");
    Check(novice::RetreatBoard(0, 2) == 1 + 1,
          "nothing on us yet, but two are close");
    // And the whole point: a novice fighting one thing in a busy yard keeps a
    // floor it can actually fight above.
    Check(novice::RetreatFloorFraction(novice::RetreatBoard(1, 3),
                                       kHectorHpMax) < 0.70,
          "Hector can fight a skeleton down from 100% with two others in the "
          "yard; the old board put that floor at 90%");
}

// A build with no combat skill in its plan -- merchant_tinker, tailor,
// alchemist, a scribe with no offensive circle -- never engages anything.
void TestCrafterNeverEngages() {
    std::printf("a build that plans no combat skill\n");
    Check(!novice::BuildMayFight(0),
          "zero planned weapon/Magery targets: this life does not fight");
    Check(novice::BuildMayFight(1), "one planned weapon school does fight");
    Check(novice::BuildMayFight(3), "a 7x dexxer certainly does");
    // The gate is unconditional: no board, no health and no nerve makes it
    // true. Odessa died at 50 hp with a Harpy and three orcs on her.
    for (int attackers = 0; attackers <= 3; ++attackers) {
        for (int inReach = 0; inReach <= 4; ++inReach) {
            Check(!(novice::BuildMayFight(0) &&
                    novice::NoviceMayOpen(attackers, inReach)),
                  "a no-combat build opens no fight on any board");
        }
    }
}

// The HP watchdog (src/travel/ClientTravel.cpp SurvivalTick -> combat::Decide)
// must never sit down while something is still out there. Odessa, 2026-09-07:
// "hp 15/50 (30%) -> rest" and "hp 5/50 (10%) -> rest" with a Harpy and three
// orcs in reach, dead 13 seconds later (g_Odessa.console.txt:1338,1360-1361).
void TestWatchdogNeverRestsUnderAttack() {
    std::printf("the survival watchdog under attack\n");
    using uo::combat::Tactic;
    uo::combat::Vitals v;
    v.hpMax = 50;
    v.hpNow = 15;              // 30%, at the disengage line
    v.healPotions = 0;
    v.bandages = 0;
    v.inCombat = false;        // she never entered war mode
    v.enemyAdjacent = false;   // and never chose a target
    v.hostilesNear = 4;        // Harpy + 3 orcs
    v.quietSeconds = 0;
    Check(uo::combat::Decide(v) != Tactic::Rest,
          "30% health with four hostiles in scan range is not a rest");
    v.hpNow = 5;               // 10%
    Check(uo::combat::Decide(v) == Tactic::Flee,
          "10% health, nothing to heal with, hostiles present: run");

    // Board clear but not for long enough. Her trace held two ten-second
    // lulls in the middle of a fatal chase.
    v.hpNow = 15;
    v.hostilesNear = 0;
    v.quietSeconds = 10;
    Check(uo::combat::Decide(v) == Tactic::Flee,
          "ten seconds of quiet is a lull, not safety");
    v.bandages = 1;
    Check(uo::combat::Decide(v) != Tactic::Bandage,
          "a 3-second bandage is not affordable inside the lull window");

    // Genuinely clear: the old behaviour is exactly preserved.
    v.quietSeconds = uo::combat::kRestAllClearSeconds;
    Check(uo::combat::Decide(v) == Tactic::Bandage,
          "clear board and bandages in the pack: bandage");
    v.bandages = 0;
    Check(uo::combat::Decide(v) == Tactic::Rest,
          "clear board and nothing to heal with: rest is right again");

    // A default-constructed Vitals must decide exactly as it did before the
    // hostile fields existed -- no caller is silently changed.
    uo::combat::Vitals d;
    d.hpMax = 50; d.hpNow = 15;
    Check(uo::combat::Decide(d) == Tactic::Rest,
          "the default board is clear, so the pre-existing answer stands");
}

// A MAGE OUT OF MANA IS NOT A CRAFTER.
//
// Aurelius (pure mage, TRAIN_COMBAT, fleet122d30_20260907 23:17:22) picked a
// foe, cast Harm, and one second later disengaged at hp=100% attackers=1 with
// reason "this life avoids combat" -- because the old flag folded "no attack
// spell castable right now" (which includes mana) into a build property.
// Seven mages/warlocks did the same 62 times in that wave.
void TestManaIsNotPacifism() {
    std::printf("- an empty mana bar is not a build that avoids combat\n");
    using S = uo::life::CombatStrategyId;

    // Pure mage, TRAIN_COMBAT, 100% hp, one attacker in reach, book and
    // Magery hold Harm -- the pool is empty and nothing else is wrong.
    const bool mageAvoids =
        novice::LifeAvoidsCombat(true, S::Mage, /*knowsAnAttackSpell=*/true,
                                 /*hasAmmo=*/true);
    Check(!mageAvoids, "a mage who knows an attack spell never 'avoids combat'");
    Check(!novice::ShouldBreakContact(mageAvoids, /*attackers=*/1,
                                      /*crowdTolerated=*/1, /*hp=*/1.0,
                                      /*bailAt=*/0.36),
          "mage, 100% hp, one attacker, no mana: finish the fight");

    // Alchemist in the same spot: no combat skill in the 700 points, and the
    // crafter strategy on top of it. This one leaves, and must keep leaving.
    const bool alchemistAvoids =
        novice::LifeAvoidsCombat(/*buildFightsAtAll=*/false, S::AvoidCombat,
                                 true, true);
    Check(alchemistAvoids, "a build with no combat skill still avoids combat");
    Check(novice::ShouldBreakContact(alchemistAvoids, 1, 1, 1.0, 0.36),
          "alchemist, 100% hp, one attacker: break contact");

    // The same mage at 30% of a 45-hp bar. One attacker on the board needs
    // 16 hp of margin (RetreatFloorHp), i.e. 35.6% of that bar, so health --
    // not the mana bar -- ends this fight.
    const double bail = novice::RetreatFloorFraction(1, 45);
    CheckPct(bail, 0.35, 0.36, "45-hp bar, one attacker: retreat floor");
    Check(novice::ShouldBreakContact(mageAvoids, 1, 1, 0.30, bail),
          "mage at 30% hp: real danger still ends the fight");

    // Genuine inability keeps the old answer.
    Check(novice::LifeAvoidsCombat(true, S::Mage, /*knowsAnAttackSpell=*/false,
                                   true),
          "a mage with no attack spell in the book avoids combat");
    Check(novice::LifeAvoidsCombat(true, S::Ranged, true, /*hasAmmo=*/false),
          "an archer with no arrows avoids combat");
    Check(novice::LifeAvoidsCombat(true, S::Tamer, true, true),
          "a tamer has no pet transport to fight through");
    Check(!novice::LifeAvoidsCombat(true, S::Melee, false, false),
          "a melee build fights with what is in its hand");
}

}  // namespace

int main() {
    Check(!novice::GroupMayOpen(1, 3, 0), "solo novice refuses a crowded opening");
    Check(novice::GroupMayOpen(1, 3, 1), "nearby friendly support permits a shared fight");
    Check(novice::GroupAttackerLimit(3) == 2, "support never permits tanking three attackers");
    Check(!novice::GroupMayOpen(3, 3, 3), "three personal attackers still force retreat");
    std::printf("novice engagement policy\n");
    TestWhoIsANovice();
    TestRetreatFloor();
    TestOpeningAFight();
    TestBreakingContact();
    TestRetreatBoard();
    TestCrafterNeverEngages();
    TestWatchdogNeverRestsUnderAttack();
    TestManaIsNotPacifism();
    std::printf("%s: %d checks, %d failures\n",
                g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
