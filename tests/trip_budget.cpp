// Wave-1 fix 2 -- the trip-veto arithmetic, isolated from Client so ctest can
// hold it without a server, MULs or world data.
//
// docs/LIFE_GATE_WAVE1.md theme 2: BUY_SUPPLIES sent a Skara Brae fisher
// through a working moongate hop to an island 232 tiles and one gate away,
// with 24 minutes of session left, and nothing ever asked whether that trip
// fit on the clock before wind-down had to start. wind-down caught the
// stalled trip 24 minutes later and logged the character out in the open,
// near Ocllo (run_gates/g_Dorvar.console.txt 00:40-01:04).
//
// uo::life::TripFitsSessionBudget (include/uo/life.h) is the generalised
// check: any goal that plans a long trip asks it, with the tile estimate the
// route planner already produced, before committing to the walk.

#include "uo/life.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

using namespace uo;
using namespace uo::life;

void TestEstimateTripTimeMs() {
    Section("A: tile count -> trip time");

    Check(EstimateTripTimeMs(0) == 0, "zero tiles takes no time");
    Check(EstimateTripTimeMs(-5) == 0, "a negative estimate floors at zero");

    // kTripMsPerTile (200) * 1.5 slack = 300 ms/tile.
    Check(EstimateTripTimeMs(1) == 300, "one tile is 300 ms with the slack");
    Check(EstimateTripTimeMs(100) == 30000, "100 tiles is 30 s");

    // The Ocllo trip itself: ~232 tiles, one gate hop
    // (run_gates/g_Dorvar.console.txt "plan Ocllo provisioner: ok legs=4
    // ~232 tiles transit=1").
    Check(EstimateTripTimeMs(232) == 232 * 300,
          "the actual Ocllo trip is 69,600 ms -- 69.6 s of walking");
}

void TestTripFitsSessionBudget() {
    Section("B: does the session have room for the trip");

    const i64 windDown = 2 * 60 * 1000;  // kWindDownBudgetMs, mirrored here

    // Plenty of session left for a short trip.
    Check(TripFitsSessionBudget(10 * 60 * 1000, 100, windDown),
          "10 minutes left comfortably covers a 100-tile trip plus wind-down");

    // Exactly on the boundary: remaining == trip + wind-down.
    {
        const i32 tiles = 50;
        const i64 need = EstimateTripTimeMs(tiles) + windDown;
        Check(TripFitsSessionBudget(need, tiles, windDown),
              "exactly enough time fits (>=, not >)");
        Check(!TripFitsSessionBudget(need - 1, tiles, windDown),
              "one millisecond short does not");
    }

    // Dorvar's actual numbers: BUY_SUPPLIES picked "Ocllo provisioner" at
    // 00:40:59 with the session's wind-down deadline at 01:04:15 -- about 23
    // minutes of session left by the run's own clock, comfortably more than
    // the ~70 s walk this trip actually needed. TripFitsSessionBudget alone
    // would have LET this specific trip through (it was not, in isolation,
    // too slow to walk); the wave-1 fix is the veto firing on every ISSUE of
    // a long goal-travel, not a claim that this one trip was unaffordable --
    // the real Dorvar failure was the repeated stall (fix 1: the missing
    // skip list) burning the clock on retries of the SAME unreachable pick,
    // which this function has no way to see. It is checked here so the
    // arithmetic that time-boxes every OTHER long trip is pinned down.
    Check(TripFitsSessionBudget(23 * 60 * 1000, 232, windDown),
          "232 tiles fits comfortably inside 23 minutes of session");

    // A trip that plainly cannot fit: 1800 tiles (the Delucia fallback the
    // console log rejected in Ocllo's favour) against a session with only
    // ten minutes left.
    Check(!TripFitsSessionBudget(10 * 60 * 1000, 1800, windDown),
          "1800 tiles does not fit in 10 minutes: "
          "1800*300ms=540s=9m, +2m wind-down=11m > 10m left");

    // No session clock at all (sessionLimitMs <= 0 in the caller) is handled
    // by the caller, not this function -- but a non-positive remainder must
    // still read as "no room", never as an unbounded allowance.
    Check(!TripFitsSessionBudget(0, 1, windDown),
          "zero time left never fits, even a one-tile trip");
    Check(!TripFitsSessionBudget(-1000, 1, windDown),
          "negative time left never fits");
}

// ---------------------------------------------------------------------------
// C: the BANK stand-down (wave-1 fix 3, Lyra's BANK<->FILL_SPELLBOOK churn).
//
// SettleDeposit is the piece DoBank now calls to decide whether a gold or
// item deposit actually landed. Before this fix the gold path called
// NoteProgress() the instant it ISSUED the drag, so a box that kept
// answering "item landed elsewhere" (run_gates/g_Lyra.console.txt, gold
// stuck at 8564 for a full minute) was credited as progress every single
// time and never stood down.
// ---------------------------------------------------------------------------
void TestSettleDeposit() {
    Section("C: settling a deposit from what actually left the pack");

    // Real progress: the pack count went down. Resets the tries counter.
    {
        int tries = 3;
        const DepositOutcome out = SettleDeposit(8564, 0, tries, 5);
        Check(out.progressed, "8564 -> 0 is real progress");
        Check(!out.giveUp, "and there is no reason to give up");
        Check(tries == 0, "a landed deposit resets the retry counter");
    }

    // Nothing moved: the exact Lyra case, "item landed in a different
    // container" -- goldOnHand reads the same before and after.
    {
        int tries = 0;
        const DepositOutcome out = SettleDeposit(8564, 8564, tries, 5);
        Check(!out.progressed, "8564 -> 8564 is not progress");
        Check(!out.giveUp, "one failed landing is not yet a reason to give up");
        Check(tries == 1, "and it counts as one try");
    }

    // Bounded: kMaxBankDepositTries (5) failures in a row gives up rather
    // than dragging at the same box forever -- the fifteen-times-a-minute
    // loop this fix ends.
    {
        int tries = 0;
        DepositOutcome out;
        for (int i = 0; i < 5; ++i) {
            out = SettleDeposit(8564, 8564, tries, 5);
            Check(!out.giveUp, "attempt 1..5 keeps trying, not giving up yet");
        }
        Check(tries == 5, "five straight failures is five tries");
        out = SettleDeposit(8564, 8564, tries, 5);
        Check(out.giveUp, "the sixth straight failure gives up on this box");
    }

    // A partial deposit (the amount asked for exceeded what actually moved,
    // or a second stack contributed) still counts as progress as long as
    // SOME gold left the pack -- this only cares about direction, not size.
    {
        int tries = 2;
        const DepositOutcome out = SettleDeposit(8564, 100, tries, 5);
        Check(out.progressed, "8564 -> 100 is still progress, just partial");
        Check(tries == 0, "and it still resets the counter");
    }
}

}  // namespace

int main() {
    std::printf("trip_budget\n");

    TestEstimateTripTimeMs();
    TestTripFitsSessionBudget();
    TestSettleDeposit();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
