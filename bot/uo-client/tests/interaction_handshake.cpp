// tests/interaction_handshake.cpp -- docs/BOT_ARCHITECTURE.md section 17.
//
// The rule under test is the one this project has broken five times in five
// different files:
//
//     A RETRY ISSUED INSIDE THE ACTION'S OWN DEADLINE CANNOT RESOLVE.
//     IT CAN ONLY SUPERSEDE ITSELF.
//
// Each case below is a real session, cited. No server, no MULs.

#include "uo/interaction/handshake.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

// run_m7/y_Corwyn.console.txt, 2026-08-30: the vendor open re-issued every
// 2.5s against an 8s deadline. "vendor_buy invalid_state ... superseded",
// eleven times in thirty seconds, at a healer standing four tiles away.
void TestNoRetryInsideTheDeadline() {
    std::printf("[handshake: nothing may be re-issued inside its own deadline]\n");
    Handshake h;
    RetryPolicy p;
    p.actionDeadlineMs = 8000;
    h.Configure(p);

    Expect(h.MayIssue(0), "the first attempt is allowed");
    h.NoteIssued(0);
    h.Note(Outcome::Pending, 100);

    const char* why = "";
    // THE EXACT INTERVAL THAT BROKE FOUR PATHS.
    Expect(!h.MayIssue(2500, &why), "2.5s into an 8s deadline: refused");
    Expect(why && why[0], "and it says why");
    Expect(!h.MayIssue(7999, &why), "one millisecond short of the deadline");
    Expect(!h.MayIssue(8000, &why), "at the deadline, still not clear of it");

    // Clear of the deadline AND its margin, and closed out properly.
    h.NoteExpiry(8000);
    Expect(h.State() == HandshakeState::Backoff, "an expiry backs off");
    Expect(h.MayIssue(20000), "well clear, and the attempt was closed out");
}

// The guard cannot be opted out of: MinimumGapMs is derived from the
// deadline, not supplied. A caller cannot pass 2500 and be believed.
void TestTheGapIsDerivedNotSupplied() {
    std::printf("[handshake: the minimum gap comes from the deadline itself]\n");
    RetryPolicy p;
    p.actionDeadlineMs = 8000;
    Expect(p.MinimumGapMs() > p.actionDeadlineMs,
           "the gap always exceeds the deadline");

    RetryPolicy slow;
    slow.actionDeadlineMs = 30000;
    Expect(slow.MinimumGapMs() > slow.actionDeadlineMs,
           "and it scales with a slower action");
}

// run_m7/r1b_Corwyn.console.txt: "You can't reach that." read, discarded, and
// the same forge clicked 311 times from one diagonal tile away. A refusal is
// an ANSWER and must end the wait immediately -- not at the deadline.
void TestARefusalEndsTheWaitAtOnce() {
    std::printf("[handshake: a refusal is an answer, and it lands now]\n");
    Handshake h;
    h.Configure(RetryPolicy{});

    h.NoteIssued(0);
    h.Note(Outcome::Pending, 50);
    h.Note(Outcome::Refused, 100, "You can't reach that.");

    Expect(h.State() == HandshakeState::ConfirmedFailure,
           "the refusal is recorded as a definitive answer");
    Expect(h.Refusal() && h.Refusal()[0],
           "and the server's own words are kept for the log");

    const char* why = "";
    Expect(!h.MayIssue(200, &why),
           "the same ask does not go back out at the same door");
    Expect(!h.MayIssue(60000, &why),
           "and waiting longer does not make the answer change");
}

// A lost packet is not a refusal: nothing was learned about the world, only
// about the connection. It backs off and the attempt counter decides.
void TestATimeoutIsNotARefusal() {
    std::printf("[handshake: silence is not the same as no]\n");
    Handshake h;
    RetryPolicy p;
    p.actionDeadlineMs = 8000;
    p.backoffMs = 2000;
    h.Configure(p);

    h.NoteIssued(0);
    h.Note(Outcome::Pending, 10);
    Expect(!h.Expired(7999), "not expired before the deadline");
    Expect(h.Expired(8000), "expired at the deadline");

    h.NoteExpiry(8000);
    Expect(h.State() == HandshakeState::Backoff, "a timeout backs off");
    Expect(h.Refusal()[0] == '\0',
           "and carries no refusal text, because nothing was said");
    Expect(!h.MayIssue(9000), "still resting");
    Expect(h.MayIssue(10001), "and clear once the backoff elapses");
}

// "One silent shop is not evidence about a whole trade" -- but this errand,
// at this door, does give up. The caller then moves on; that decision is the
// errand's, not the handshake's.
void TestAttemptsAreSpent() {
    std::printf("[handshake: attempts run out, and it says so]\n");
    Handshake h;
    RetryPolicy p;
    p.maxAttempts = 3;
    p.actionDeadlineMs = 1000;
    p.backoffMs = 0;
    h.Configure(p);

    uo::i64 t = 0;
    for (int i = 0; i < 3; ++i) {
        Expect(h.MayIssue(t), "an attempt remains");
        h.NoteIssued(t);
        t += 1000;
        h.NoteExpiry(t);
        t += 2000;
    }
    const char* why = "";
    Expect(h.Exhausted(), "three attempts is three attempts");
    Expect(!h.MayIssue(t, &why), "and the fourth is refused");
    Expect(why && why[0], "with a reason");
}

// Every refusal to act must be explainable. A bot that silently declines to
// do anything is indistinguishable from a hung one, and this project has
// spent whole sessions telling those two apart by hand.
void TestEveryRefusalIsExplained() {
    std::printf("[handshake: never decline to act without saying why]\n");
    Handshake h;
    h.Configure(RetryPolicy{});
    h.NoteIssued(0);

    const char* why = nullptr;
    Expect(!h.MayIssue(10, &why), "in flight");
    Expect(why && why[0], "explained");

    Expect(!h.MayIssue(10, nullptr),
           "and a caller that passes no out-param does not crash");

    for (int i = 0; i <= static_cast<int>(HandshakeState::Backoff); ++i) {
        const char* n = HandshakeStateName(static_cast<HandshakeState>(i));
        Expect(n && n[0] && n[0] != '?', "every state has a name");
    }
}

// Reset is how an errand moves to a different door: the attempt history
// belongs to this interaction, not to the character.
void TestResetClearsTheHistory() {
    std::printf("[handshake: a fresh door gets a fresh allowance]\n");
    Handshake h;
    h.Configure(RetryPolicy{});
    h.NoteIssued(0);
    h.Note(Outcome::Refused, 10, "no");
    Expect(!h.MayIssue(20), "refused here");

    h.Reset();
    Expect(h.State() == HandshakeState::Idle, "reset is idle");
    Expect(h.Attempts() == 0, "and the counter starts again");
    Expect(h.MayIssue(20), "so the next shop gets a real try");
}

}  // namespace

int main() {
    std::printf("interaction_handshake\n");
    TestNoRetryInsideTheDeadline();
    TestTheGapIsDerivedNotSupplied();
    TestARefusalEndsTheWaitAtOnce();
    TestATimeoutIsNotARefusal();
    TestAttemptsAreSpent();
    TestEveryRefusalIsExplained();
    TestResetClearsTheHistory();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
