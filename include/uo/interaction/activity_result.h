#pragma once

// ---------------------------------------------------------------------------
// ACTIVITY RESULTS AND WAKE CONDITIONS -- the shared vocabulary every
// activity answers in (docs/BOT_ARCHITECTURE.md sections 5 and 14).
//
// TWO DISTINCTIONS, AND BOTH WERE PAID FOR IN LIVE SESSIONS.
//
// 1. "NOTHING HAPPENED" IS NOT SUCCESS.
//
//    Section 14 states the rule and this project has the receipts. A goal that
//    completed with progress=0 was re-picked 60 ms later, forever, because
//    Finish(true) told the planner the work was done:
//      * GATHER_LOGS, forty completions, progress=0 every time (M5 section 10)
//      * EARN_GOLD, a 60-millisecond loop (M7 section 4b1)
//      * UPGRADE_GEAR, 11,645 "buying" lines across the recorded runs
//      * REPLACE_EQUIPMENT, 24 of Corwyn's 24 picks and 27 of Tarath's 27
//    Two states -- worked, failed -- cannot express "I ran, nothing was
//    wrong, and nothing moved". Seven can.
//
// 2. AN ACTIVITY MUST SAY WHAT IT IS WAITING FOR.
//
//    Section 5: a bot blocked on a vendor reply should cost no CPU. That is
//    only possible if it reports the CONDITION rather than a guessed sleep.
//    Today Runner turns these into its own nextActionMs_; the Scheduler will
//    turn them into a wake queue, and nothing above has to change.
//
// Deliberately protocol-free: no Client, no packets, no world model. That is
// what lets ctest exercise the very objects the live bot runs -- the same
// construction rule the life and combat layers already follow.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

// What an activity has to say about itself after a tick.
enum class ActivityStatus : u8 {
    // Still going, and legitimately so -- something is in flight.
    Waiting = 0,
    // The activity did what it set out to do, AND the world confirms it.
    // Never returned on the strength of a packet having been sent; see
    // uo/interaction/progress.h.
    Success,
    // Definitively cannot be done. Do not retry this errand as it stands.
    Failed,
    // Ran, hit nothing wrong, and moved nothing. The state that did not
    // exist and should have: this is what a spinning goal actually is.
    NoProgress,
    // Cannot proceed until something outside this activity changes -- no
    // gold, no tool, a policy refusal. Distinct from Failed because the
    // answer changes when the world does.
    Blocked,
    // Abandoned from outside: an emergency preempted it, or the session is
    // winding down. Not a fault of the activity.
    Interrupted,
    // Failed, but the failure is one worth trying again later or elsewhere.
    // One silent shop is not evidence about a whole trade.
    RetryableFailure,
};

const char* ActivityStatusName(ActivityStatus s);

// Does this status mean the activity is over, whatever the verdict?
bool IsTerminal(ActivityStatus s);
// Did the world actually move? The planner's satiation and the session
// histogram both need this, and both used to guess it.
bool IsProgress(ActivityStatus s);

// What the activity is waiting FOR. A hint, never a correctness requirement:
// a caller may always simply tick again.
enum class Wake : u8 {
    Now = 0,            // nothing to wait on
    AfterDelay,         // a fixed pause; see ActivityTickResult::delayMs
    ActionResolves,     // an action is in flight; its own deadline governs
    TravelArrives,      // walking; the travel layer reports arrival
    InventoryChanges,   // waiting for items to appear or leave
    GoldChanges,        // waiting for the purse to move
    SkillChanges,       // waiting for the server's own skill report
    TargetCursor,       // waiting for the server to arm a target
};

const char* WakeName(Wake w);

struct ActivityTickResult {
    ActivityStatus status = ActivityStatus::Waiting;
    Wake           wake = Wake::Now;
    i64            delayMs = 0;   // meaningful when wake == AfterDelay

    // DID THIS TICK ACTUALLY ASK THE SERVER FOR ANYTHING?
    //
    // A caller that counts every non-terminal tick as an attempt is counting
    // its own polling rate, not the activity's. Measured: Bruin's potion
    // errand issued ONE vendor ask -- which needs its full 8s deadline to
    // resolve -- and then returned Waiting("an action is already in flight")
    // every 60ms while it waited (run_r4/w_Bruin.console.txt:317-322). Five
    // of those polls exhausted the planner's whole attempt budget in 300ms,
    // and REPLACE_EQUIPMENT was re-picked 39 times while the single real ask
    // was still outstanding.
    //
    // So: true only on a tick that ISSUED something -- a speech, a vendor
    // request, a purchase. A tick spent waiting for an answer is not a try.
    bool           acted = false;
    // THE COUNTER IS OPEN OR A PURCHASE IS IN FLIGHT. An ask that got this
    // far is progress, not a try: Castor's cloth errand (2026-09-05 11:33:50)
    // walked, found the weaver, opened the shop and issued the buy -- five
    // acted ticks -- and the planner's attempt backstop abandoned the goal in
    // the same millisecond, before the pack could move.
    bool           offerOpen = false;

    // ALWAYS POPULATED, success or failure. A refusal nobody can read is the
    // defect this whole layer exists to stop repeating, and every unexplained
    // stand-down in this project's logs cost a live session to diagnose.
    const char* reason = "";

    static ActivityTickResult Waiting(Wake w, i64 delayMs, const char* why) {
        ActivityTickResult r;
        r.status = ActivityStatus::Waiting;
        r.wake = w;
        r.delayMs = delayMs;
        r.reason = why;
        return r;
    }
    static ActivityTickResult Done(ActivityStatus s, const char* why) {
        ActivityTickResult r;
        r.status = s;
        r.wake = Wake::Now;
        r.reason = why;
        return r;
    }
};

}  // namespace uo::life
