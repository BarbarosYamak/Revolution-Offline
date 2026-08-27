#pragma once

// ---------------------------------------------------------------------------
// Fleet connection admission control (M3.5).
//
// WHY THIS EXISTS
//
// M3 locked itself out of its own shard. Re-verifying four scenarios back to
// back tripped the server's connection guard, and then every retry renewed the
// ban -- the log shows the same address re-banned at 17:43, 17:44, 17:44 and
// 17:45. Nothing in the client knew it was doing it. With one bot that is an
// annoyance; with a population it is a self-inflicted denial of service, so it
// is a blocker for autonomous work rather than a nicety.
//
// THE ACTUAL SERVER RULE, measured and then read out of the runtime's own
// configuration rather than guessed:
//
//   runtime/sphere.ini
//     MaxConnectRequestsPerIP=50   // "Maximum number of connection requests
//                                  //  before rejecting/blocking IP (similar
//                                  //  to MaxPings but DOES NOT DECAY, it
//                                  //  resets only after <NetTTL> seconds
//                                  //  elapsed since last connection attempt)."
//     NetTTL=60*5                  // 300 seconds
//
//   runtime/scripts/core/serv_triggers.scp
//     [FUNCTION f_onserver_connectreq_ex]
//       LOCAL.BAN_TIMEOUT --> 5 * 60 (default), seconds to keep the ip banned
//       RETURN 2 --> Reject and ban the IP for <LOCAL.BAN_TIMEOUT> seconds.
//
// Three consequences that shape every decision below:
//
//   1. It is NOT a rate limiter and NOT a leaky bucket. It is a cumulative
//      counter over the life of the session that only clears after a period of
//      TOTAL SILENCE. Refilling tokens over time would model it wrongly and
//      would walk straight into the ban.
//   2. The silence is measured from the LAST ATTEMPT, and a *rejected* attempt
//      is still an attempt. So retrying while banned is not merely useless, it
//      resets the very clock you are waiting on. Retry-on-failure is the one
//      strategy guaranteed never to recover.
//   3. The unit is the IP, not the account. Every bot process on this machine
//      shares one budget, so admission has to be a fleet-level decision. See
//      FleetLedger for the cross-process half.
//
// The controller therefore mirrors the server's own bookkeeping: count every
// attempt, reset only after `counterResetMs` of silence, and refuse to emit an
// attempt at all while a ban is believed to be in force.
//
// No I/O, no clock, no randomness of its own: the caller passes `nowMs` and a
// seed. That is what makes it testable and what keeps fixed-seed tests
// reproducible.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::fleet {

// Why an attempt was refused. Reported rather than swallowed so a caller can
// log the difference between "wait your turn" and "you are banned".
enum class Refusal : u8 {
    None = 0,       // allowed
    Spacing,        // too soon after the previous attempt (stagger)
    BudgetSpent,    // our own conservative ceiling reached; wait for the reset
    Banned,         // a ban is believed to be in force -- DO NOT touch the socket
    CircuitOpen,    // too many consecutive failures; something is wrong
    Count,
};

const char* RefusalName(Refusal r);

struct Policy {
    // The server's own threshold, recorded so the margin is visible rather
    // than a magic number. Never used as our limit.
    u32 serverMaxRequestsPerIp = 50;

    // Our ceiling. Deliberately well under the server's: the server counts
    // things we may not (a dropped connection re-handshaking, another client
    // on the same machine, the operator's own UO client), and the penalty for
    // guessing high is a five-minute outage for the whole fleet.
    u32 budget = 30;

    // Silence required before the server's counter resets. Mirrors NetTTL.
    i64 counterResetMs = 300000;

    // How long a ban lasts once earned. Mirrors LOCAL.BAN_TIMEOUT.
    i64 banMs = 300000;

    // Minimum gap between two attempts from this fleet. Staggers a population
    // start-up instead of letting fifty processes race the same second.
    i64 minSpacingMs = 3000;

    // Backoff after a failed attempt: base * 2^(consecutive-1), capped.
    i64 backoffBaseMs = 5000;
    i64 backoffMaxMs = 300000;

    // Consecutive failures that trip the breaker, and how long it stays open.
    u32 breakerFailures = 5;
    i64 breakerCooldownMs = 600000;

    // Jitter applied to every wait, as a percentage of the wait. Keeps a fleet
    // from re-converging into a thundering herd after a shared outage.
    u32 jitterPercent = 25;
};

struct Verdict {
    bool    allowed = false;
    Refusal refusal = Refusal::None;
    // How long to wait before asking again. Always > 0 when !allowed, so a
    // caller can never spin.
    i64     retryAfterMs = 0;
};

// Pure decision logic. One per process; share the underlying history across
// processes with FleetLedger.
class AdmissionController {
public:
    AdmissionController() = default;
    explicit AdmissionController(const Policy& p, u64 seed = 1) : policy_(p), rng_(seed ? seed : 1) {}

    const Policy& GetPolicy() const { return policy_; }

    // May we open a socket right now? Does not record anything -- a caller
    // that asks and then decides not to connect has not spent budget.
    Verdict Request(i64 nowMs) const;

    // We actually opened a socket. This is what the server counts, so it is
    // what we count, whatever happens next.
    void NoteAttempt(i64 nowMs);

    // The connection came up and logged in.
    void NoteSuccess(i64 nowMs);

    // The attempt failed for an ordinary reason (refused, timeout, RST).
    void NoteFailure(i64 nowMs);

    // We have positive evidence of a ban -- the server said so, or the socket
    // died before a single byte came back. Starts the ban clock.
    void NoteBanned(i64 nowMs);

    // --- observability -----------------------------------------------------
    u32  AttemptsUsed() const { return attempts_; }
    u32  BudgetRemaining() const { return attempts_ >= policy_.budget ? 0u : policy_.budget - attempts_; }
    i64  BanUntilMs() const { return banUntilMs_; }
    bool Banned(i64 nowMs) const { return nowMs < banUntilMs_; }
    bool CircuitOpen(i64 nowMs) const { return nowMs < breakerUntilMs_; }
    u32  ConsecutiveFailures() const { return consecutiveFailures_; }
    i64  LastAttemptMs() const { return lastAttemptMs_; }
    // When the server's counter would clear if nobody touches it again.
    i64  CounterResetsAtMs() const { return lastAttemptMs_ + policy_.counterResetMs; }

    void Reset();

private:
    // Roll the cumulative counter forward: the server clears it only after a
    // full silent window since the LAST attempt.
    void ExpireIfSilent(i64 nowMs) const;
    i64  Jitter(i64 waitMs) const;

    Policy policy_{};

    // Mutable because Request() is logically const -- it answers a question --
    // but has to apply the same silence expiry the server would.
    mutable u32 attempts_ = 0;
    mutable i64 lastAttemptMs_ = 0;
    mutable bool everAttempted_ = false;
    mutable u64 rng_ = 1;

    i64 banUntilMs_ = 0;
    i64 breakerUntilMs_ = 0;
    u32 consecutiveFailures_ = 0;
};

// ---------------------------------------------------------------------------
// The cross-process half.
//
// Bots are separate processes but share one IP, so the budget is shared too.
// The ledger is a small text file holding the attempt history; each process
// loads it before asking, and appends after attempting. Deliberately dumb --
// a line-oriented file and last-writer-wins -- because the cost of a lost
// write is one wasted attempt out of a deliberately conservative budget, and
// the cost of a lock-up would be the whole fleet.
// ---------------------------------------------------------------------------
struct LedgerEntry {
    i64 atMs = 0;      // when the attempt was made
    bool banned = false;  // it earned (or observed) a ban
};

class FleetLedger {
public:
    explicit FleetLedger(std::string path) : path_(std::move(path)) {}

    // Read the shared history and fold it into `c`. Entries older than the
    // reset window are dropped, exactly as the server drops its counter.
    bool Load(AdmissionController& c, i64 nowMs) const;

    // Append one attempt. Returns false if the file could not be written --
    // the caller should then treat the fleet budget as unknown and be
    // conservative rather than assume it has room.
    bool Append(const LedgerEntry& e) const;

    const std::string& Path() const { return path_; }

private:
    std::string path_;
};

} // namespace uo::fleet
