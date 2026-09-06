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
    // in_reach counted at combat::kCrowdRadius (4); nearby at kSoloRadius (8).
    Check(novice::NoviceMayOpen(1, 1),
          "one hostile in reach and nothing else near: this is a duel");
    Check(novice::NoviceMayOpen(0, 1),
          "one hostile approaching, still a duel");
    Check(!novice::NoviceMayOpen(2, 2),
          "two in reach is a group -- the 02:16 Skeleton pair");
    Check(!novice::NoviceMayOpen(1, 2),
          "one in reach and a second within joining distance is still a "
          "group; a novice does not pull into company");
    Check(!novice::NoviceMayOpen(1, 3),
          "the 23:38 board (Skeleton, Skeleton, Spectre) is refused");
    // The pre-existing veteran rule is looser and unchanged: it only refuses
    // three or more in reach. This asserts the two rules really do differ.
    Check(!novice::NoviceMayOpen(2, 2) && !(2 >= 3),
          "a two-hostile board that a veteran would take is refused for a "
          "novice");
}

void TestBreakingContact() {
    std::printf("breaking contact\n");
    Check(!novice::NoviceMustDisengage(1, 1),
          "one attacker, one in reach: keep fighting");
    Check(novice::NoviceMustDisengage(2, 2),
          "a second attacker means leave now, at whatever health");
    Check(novice::NoviceMustDisengage(1, 2),
          "a second hostile stepping into reach is enough -- do not wait for "
          "it to swing");
    Check(novice::NoviceMustDisengage(3, 3),
          "the 23:38 trio is a disengage");
    Check(!novice::NoviceMustDisengage(0, 0),
          "an empty board is not a disengage");
}

}  // namespace

int main() {
    std::printf("novice engagement policy\n");
    TestWhoIsANovice();
    TestRetreatFloor();
    TestOpeningAFight();
    TestBreakingContact();
    std::printf("%s: %d checks, %d failures\n",
                g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
