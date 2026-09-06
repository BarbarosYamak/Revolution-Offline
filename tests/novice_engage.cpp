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
    Check(novice::NoviceMustDisengage(1, 3),
          "the owner's ceiling stands: 3+ within reach and this life leaves");
    Check(novice::NoviceMustDisengage(3, 3),
          "the 23:38 trio is a disengage");
    Check(!novice::NoviceMustDisengage(0, 0),
          "an empty board is not a disengage");
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

}  // namespace

int main() {
    std::printf("novice engagement policy\n");
    TestWhoIsANovice();
    TestRetreatFloor();
    TestOpeningAFight();
    TestBreakingContact();
    TestCrafterNeverEngages();
    TestWatchdogNeverRestsUnderAttack();
    std::printf("%s: %d checks, %d failures\n",
                g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
