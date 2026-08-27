#include "uo/fleet.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace uo::fleet {

const char* RefusalName(Refusal r) {
    switch (r) {
        case Refusal::None:        return "allowed";
        case Refusal::Spacing:     return "spacing";
        case Refusal::BudgetSpent: return "budget_spent";
        case Refusal::Banned:      return "banned";
        case Refusal::CircuitOpen: return "circuit_open";
        default:                   return "?";
    }
}

// The server's counter "does not decay, it resets only after <NetTTL> seconds
// elapsed since last connection attempt". Not a sliding window and not a
// bucket: one silent stretch clears the whole thing, and any attempt inside
// the window restarts the wait.
void AdmissionController::ExpireIfSilent(i64 nowMs) const {
    if (!everAttempted_) return;
    if (nowMs - lastAttemptMs_ >= policy_.counterResetMs) {
        attempts_ = 0;
        everAttempted_ = false;
    }
}

// xorshift64*, seeded per fleet member. Deterministic for a given seed, which
// is what keeps the tests reproducible while still spreading a real fleet out.
i64 AdmissionController::Jitter(i64 waitMs) const {
    if (waitMs <= 0 || policy_.jitterPercent == 0) return waitMs;
    rng_ ^= rng_ >> 12; rng_ ^= rng_ << 25; rng_ ^= rng_ >> 27;
    const u64 r = (rng_ * 2685821657736338717ull) >> 33;
    const i64 span = (waitMs * static_cast<i64>(policy_.jitterPercent)) / 100;
    if (span <= 0) return waitMs;
    // Jitter upward only. Waiting longer is always safe here; waiting less
    // than the server requires is what earns the ban.
    return waitMs + static_cast<i64>(r % static_cast<u64>(span + 1));
}

Verdict AdmissionController::Request(i64 nowMs) const {
    Verdict v;

    // A believed ban is absolute. This is the single most important branch in
    // the file: an attempt now would be rejected AND would push the server's
    // reset clock forward, so the fleet would never recover. Refuse.
    if (nowMs < banUntilMs_) {
        v.refusal = Refusal::Banned;
        v.retryAfterMs = Jitter(banUntilMs_ - nowMs);
        return v;
    }

    if (nowMs < breakerUntilMs_) {
        v.refusal = Refusal::CircuitOpen;
        v.retryAfterMs = Jitter(breakerUntilMs_ - nowMs);
        return v;
    }

    ExpireIfSilent(nowMs);

    if (attempts_ >= policy_.budget) {
        // Wait out the silence the server needs to clear its counter. Note
        // this is measured from the last attempt, not from now.
        const i64 waitMs = (lastAttemptMs_ + policy_.counterResetMs) - nowMs;
        v.refusal = Refusal::BudgetSpent;
        v.retryAfterMs = Jitter(waitMs > 0 ? waitMs : policy_.counterResetMs);
        return v;
    }

    if (everAttempted_) {
        const i64 sinceLast = nowMs - lastAttemptMs_;
        i64 required = policy_.minSpacingMs;

        // After a failure, back off exponentially instead of hammering.
        if (consecutiveFailures_ > 0) {
            i64 backoff = policy_.backoffBaseMs;
            for (u32 i = 1; i < consecutiveFailures_ && backoff < policy_.backoffMaxMs; ++i)
                backoff *= 2;
            if (backoff > policy_.backoffMaxMs) backoff = policy_.backoffMaxMs;
            if (backoff > required) required = backoff;
        }

        if (sinceLast < required) {
            v.refusal = Refusal::Spacing;
            v.retryAfterMs = Jitter(required - sinceLast);
            return v;
        }
    }

    v.allowed = true;
    return v;
}

void AdmissionController::NoteAttempt(i64 nowMs) {
    ExpireIfSilent(nowMs);
    ++attempts_;
    lastAttemptMs_ = nowMs;
    everAttempted_ = true;
}

void AdmissionController::NoteSuccess(i64 nowMs) {
    (void)nowMs;
    consecutiveFailures_ = 0;
    breakerUntilMs_ = 0;
}

void AdmissionController::NoteFailure(i64 nowMs) {
    ++consecutiveFailures_;
    if (consecutiveFailures_ >= policy_.breakerFailures)
        breakerUntilMs_ = nowMs + policy_.breakerCooldownMs;
}

void AdmissionController::NoteBanned(i64 nowMs) {
    banUntilMs_ = nowMs + policy_.banMs;
    ++consecutiveFailures_;
    // A ban means the budget is gone as far as we are concerned: the server
    // will not clear its counter until it has been silent, and the ban window
    // is exactly that silence if we honour it.
    attempts_ = policy_.budget;
}

void AdmissionController::Reset() {
    attempts_ = 0;
    lastAttemptMs_ = 0;
    everAttempted_ = false;
    banUntilMs_ = 0;
    breakerUntilMs_ = 0;
    consecutiveFailures_ = 0;
}

// --- ledger ----------------------------------------------------------------
//
// Format: one entry per line, "<atMs> <0|1>". Chosen so a partial write can
// only ever lose or corrupt the final line, which a reader skips.

bool FleetLedger::Load(AdmissionController& c, i64 nowMs) const {
    c.Reset();
    std::FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) return false;

    std::vector<LedgerEntry> entries;
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        long long at = 0;
        int banned = 0;
        // A truncated final line fails to parse both fields and is dropped.
        if (std::sscanf(line, "%lld %d", &at, &banned) != 2) continue;
        entries.push_back(LedgerEntry{static_cast<i64>(at), banned != 0});
    }
    std::fclose(f);

    const Policy& p = c.GetPolicy();

    // Replay in order. Anything followed by a full silent window is already
    // forgotten by the server, so NoteAttempt's own expiry does the filtering
    // for us and the replay stays faithful to what the server saw.
    for (const LedgerEntry& e : entries) {
        if (e.atMs > nowMs) continue;   // clock skew between processes
        if (nowMs - e.atMs >= p.counterResetMs) continue;
        c.NoteAttempt(e.atMs);
        if (e.banned) c.NoteBanned(e.atMs);
    }
    return true;
}

bool FleetLedger::Append(const LedgerEntry& e) const {
    std::FILE* f = std::fopen(path_.c_str(), "ab");
    if (!f) return false;
    const int n = std::fprintf(f, "%lld %d\n", static_cast<long long>(e.atMs), e.banned ? 1 : 0);
    std::fflush(f);
    std::fclose(f);
    return n > 0;
}

} // namespace uo::fleet
