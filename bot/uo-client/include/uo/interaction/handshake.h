#pragma once

// ---------------------------------------------------------------------------
// THE HANDSHAKE -- how a bot performs one asynchronous Sphere action and
// learns what actually happened (docs/BOT_ARCHITECTURE.md section 17).
//
// THE RULE IT ENFORCES, and the reason it is a type rather than a convention:
//
//     A RETRY ISSUED INSIDE THE ACTION'S OWN DEADLINE CANNOT RESOLVE.
//     IT CAN ONLY SUPERSEDE ITSELF.
//
// Sphere actions carry a deadline (Client's kVendorTimeoutMs is 8 seconds).
// Ask again at 2.5 seconds and the second ask cancels the first before the
// server has answered either, forever. Every one of these is that defect, and
// every one of them was found live, separately, and fixed separately:
//
//   * the bank ask                        (fixed 2026-08-29)
//   * BUY_SUPPLIES                        (fixed 2026-08-29, same day)
//   * the vendor OPEN in the bandage path -- "vendor_buy invalid_state ...
//     superseded", eleven times in thirty seconds, at an NPC standing right
//     there (run_m7/y_Corwyn.console.txt, 2026-08-30)
//   * the vendor BUY in the same path, 2.5s against the same 8s deadline
//   * the craft menu, still open at the time of writing: "craft: no result
//     from the last i_potion_poison in 8s -- trying again"
//
// Five copies of one rule, written by hand, four of them wrong at some point.
// A comment reminding the next author to pick a bigger number is not a fix;
// this type refuses to hand out permission to issue in the first place.
//
// THE SECOND RULE: A DEFINITIVE REFUSAL ENDS THE WAIT.
//
// "You can't reach that." is an answer. It was read, discarded, and the same
// click re-issued 311 times in one session while the bot stood one diagonal
// tile from the forge (run_m7/r1b_Corwyn.console.txt). A handshake that
// cannot distinguish "no reply yet" from "the server said no" will always
// spend the session asking.
//
// DELIBERATELY PROTOCOL-FREE. No Client, no packets, no clock of its own --
// the caller passes `nowMs` and reports outcomes. That is what lets ctest
// exercise the very object the live bot runs, and what stops this file from
// depending on the layer it exists to discipline.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

// Section 17's state set, unchanged.
enum class HandshakeState : u8 {
    // Nothing has been asked. Free to issue.
    Idle = 0,
    // The packet has just gone out this tick.
    ActionIssued,
    // Out there, inside its deadline, no answer yet. THE STATE IN WHICH
    // NOTHING MAY BE RE-ISSUED.
    WaitingForServer,
    // The server confirmed it -- and "confirmed" means the world moved, not
    // that the packet was accepted. See uo/interaction/progress.h.
    ConfirmedSuccess,
    // The server said no, in words, definitively. Not a timeout: an answer.
    ConfirmedFailure,
    // The deadline passed with no answer either way.
    TimedOut,
    // Failed, and resting before the next attempt.
    Backoff,
};

const char* HandshakeStateName(HandshakeState s);

// What the caller observed since the last tick.
enum class Outcome : u8 {
    // Nothing yet. The overwhelmingly common answer, and the one that used to
    // be mistaken for "try again".
    Pending = 0,
    Succeeded,
    // The server answered NO. Definitive, and worth more than a timeout,
    // because it ends the errand honestly instead of leaving it to expire.
    Refused,
};

struct RetryPolicy {
    // How many attempts before the errand is over. One silent shop is not
    // evidence about a trade, so this is small and the caller moves on.
    i32 maxAttempts = 3;
    // How long the ACTION itself is allowed. This is not a guess -- it must
    // be the deadline the client layer actually applies to the action being
    // performed (kVendorTimeoutMs and friends).
    i64 actionDeadlineMs = 8000;
    // The rest between a failure and the next attempt.
    i64 backoffMs = 2000;

    // THE INVARIANT, checked rather than remembered.
    //
    // A gap no longer than the deadline means the retry lands while the first
    // attempt is still outstanding, which is the whole defect. Callers get
    // this for free and cannot opt out of it: MinimumGapMs is what
    // MayIssue() uses, not any number the caller supplies.
    i64 MinimumGapMs() const { return actionDeadlineMs + 1000; }
};

// One asynchronous interaction, from the ask to the answer.
//
// NOT a queue and not a scheduler: one handshake covers one action, and an
// activity owns one at a time. That is the point -- a player has one pair of
// hands, and the superseding bugs all came from pretending otherwise.
class Handshake {
public:
    void Configure(const RetryPolicy& policy) { policy_ = policy; }
    const RetryPolicy& Policy() const { return policy_; }

    void Reset();

    // MAY AN ACTION GO OUT RIGHT NOW?
    //
    // The one question this type exists to answer. False while an attempt is
    // in flight inside its deadline, false during backoff, false once the
    // attempts are spent. `whyNot` is always populated when it returns false,
    // because an unexplained refusal to act is indistinguishable from a hang.
    bool MayIssue(i64 nowMs, const char** whyNot = nullptr) const;

    // Record that the packet went out. Starts the deadline.
    void NoteIssued(i64 nowMs);

    // Feed in what the world said. Drives the state machine and nothing else.
    void Note(Outcome outcome, i64 nowMs, const char* detail = "");

    HandshakeState State() const { return state_; }
    i32  Attempts() const { return attempts_; }
    bool Exhausted() const { return attempts_ >= policy_.maxAttempts; }
    // Set when the server answered no; empty otherwise. This is the string a
    // goal should log and act on rather than re-issuing.
    const char* Refusal() const { return refusal_; }

    // Has the outstanding attempt run past its deadline? Callers tick this
    // so a lost packet becomes TimedOut rather than a permanent wait.
    bool Expired(i64 nowMs) const;
    // Move an expired attempt into TimedOut/Backoff. Separate from Expired()
    // so the query stays const and the transition is deliberate.
    void NoteExpiry(i64 nowMs);

private:
    RetryPolicy    policy_{};
    HandshakeState state_ = HandshakeState::Idle;
    i64  issuedAtMs_ = 0;
    i64  readyAtMs_ = 0;     // not before this, ever
    i32  attempts_ = 0;
    const char* refusal_ = "";
};

}  // namespace uo::life
