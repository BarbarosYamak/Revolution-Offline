// Deterministic tests for the 0x22 move-ack policy in include/uo/sphere_rules.h.
//
// Two rules are under test, both derived from the wave10 fleet run of
// 2026-08-31 (run_gates/wave10, 33 concurrent clients):
//
//   1. The ack watchdog must not blame our step when the whole server has gone
//      quiet. Source-X saves the world on the main thread; the run shows
//      "World save has been initiated." at 17:34:04.598 and the held-back acks
//      delivered at 17:34:09.90 -- a 5.3s global stall that tripped a flat 5s
//      deadline on 18 of 33 bots and aborted healthy paths.
//
//   2. An ack for a move that a reset already discarded must be dropped, not
//      consumed. Consuming it would pop a *newer* pending move that the server
//      has not acked, freeing a flight slot and desyncing the queue for good.
//
// Pure functions over plain values: no server, no socket, no world data.

#include "uo/sphere_rules.h"

#include <cstdio>

using namespace uo;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

// ---------------------------------------------------------------------------
// Watchdog: silence is the server's fault, chatter-without-an-ack is ours.
// ---------------------------------------------------------------------------
void TestWatchdog() {
    Section("move ack watchdog");

    // Healthy: acked well inside the deadline, server chatty.
    Check(!sphere::MoveAckWatchdogExpired(200, 50),
          "a fresh move never expires");
    Check(!sphere::MoveAckWatchdogExpired(4999, 0),
          "just under the soft deadline does not expire");

    // The genuine failure the watchdog exists for: the server is still talking
    // to us (mobile updates, status, chat) but never answered this step.
    Check(sphere::MoveAckWatchdogExpired(5001, 100),
          "unacked past the deadline while the server is chatty -> lost move");
    Check(sphere::MoveAckWatchdogExpired(8000, 1499),
          "inbound gap just under the stall grace still counts as chatty");

    // The wave10 regression: a Source-X world save froze every client for
    // ~5.3s. The step was queued behind the save, not dropped. At the moment
    // the old watchdog fired, the socket had been silent for the same 5.3s.
    Check(!sphere::MoveAckWatchdogExpired(5037, 5037),
          "world-save stall (wave10 17:34:04.598 -> 17:34:09.90) must not abort");
    Check(!sphere::MoveAckWatchdogExpired(6000, 6000),
          "total server silence is attributed to the server, not the step");
    Check(!sphere::MoveAckWatchdogExpired(5001, 1500),
          "inbound gap at the stall grace is already 'quiet'");

    // ...but silence cannot pin a path forever; a dead session still aborts.
    Check(sphere::MoveAckWatchdogExpired(20000, 20000),
          "hard ceiling fires however quiet the socket is");
    Check(sphere::MoveAckWatchdogExpired(60000, 60000),
          "long-dead session aborts");
    Check(!sphere::MoveAckWatchdogExpired(19999, 19999),
          "just under the hard ceiling still waits");

    // Ordering sanity: the soft deadline is real, the ceiling is well past the
    // longest stall we have measured.
    Check(sphere::kMoveAckDeadlineMs < sphere::kMoveAckHardDeadlineMs,
          "soft deadline sits below the hard ceiling");
    Check(sphere::kMoveAckHardDeadlineMs > 6000,
          "hard ceiling clears the measured 5.3s world-save stall");
}

// ---------------------------------------------------------------------------
// Ack classification, including the late ack that follows an aborted path.
// ---------------------------------------------------------------------------
void TestClassify() {
    Section("move ack classification");

    using K = sphere::MoveAckKind;

    // Normal steady state: acks arrive in send order.
    Check(sphere::ClassifyMoveAck(true, 31, 31, 0) == K::Expected,
          "matching sequence with a move in flight is consumed");

    // Nothing in flight and nothing owed: genuinely unexplained.
    Check(sphere::ClassifyMoveAck(false, 0, 31, 0) == K::Unsolicited,
          "no pending move and no orphans -> unsolicited");

    // In flight but out of order with no reset to explain it: real desync.
    Check(sphere::ClassifyMoveAck(true, 31, 12, 0) == K::Mismatched,
          "wrong sequence with nothing abandoned -> resync");

    // The wave10 sequence of events, exactly: the watchdog cleared the pending
    // queue at 17:34:09.700, then the server's ack for that discarded move
    // arrived at 17:34:09.906 (wave10_RevGen3_09_Rhaler, seq=31). It must be
    // recognised as an orphan rather than logged as unsolicited.
    Check(sphere::ClassifyMoveAck(false, 0, 31, 1) == K::Abandoned,
          "late ack after an abort is an orphan, not an unsolicited ack");

    // The desync this prevents: had the bot re-pumped a step before the late
    // ack landed, the old code would have popped the *new* move's entry.
    Check(sphere::ClassifyMoveAck(true, 0, 31, 1) == K::Abandoned,
          "orphan ack must not be consumed as the ack for a freshly sent move");

    // A reset restarts the sequence at 0, so the new move's seq is 0 and the
    // orphan's is whatever it was -- they differ in every observed case.
    Check(sphere::ClassifyMoveAck(true, 0, 0, 1) == K::Expected,
          "an orphan owed does not stop a correctly matched ack being consumed");

    // Several moves in flight (maxInFlight > 1) discarded by one reset.
    Check(sphere::ClassifyMoveAck(false, 0, 44, 3) == K::Abandoned,
          "each of several orphaned moves is dropped in turn");
}

}  // namespace

int main() {
    std::printf("move ack policy tests\n");
    TestWatchdog();
    TestClassify();
    std::printf("%d check(s), %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
