// Deterministic tests for the M2 action layer's pure state machines.
//
// These exercise include/uo/actions.h -- the exact header the client compiles
// against -- so targeting, action results, drag transactions and life state
// cannot drift from shipping behaviour. They test local logic only; proving
// the server actually does these things is the job of the live scenarios.

#include "uo/actions.h"

#include <cstdio>
#include <string>

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
// Target cursor: generations are what stop a stale reply answering the wrong
// request, and what keeps two sessions independent.
// ---------------------------------------------------------------------------
void TestTargetState() {
    Section("target state");

    act::TargetState t;
    Check(!t.Active(), "starts with no cursor");
    Check(!t.CanReply(0), "cannot reply before a cursor is armed");

    t.OnArmed(0x2CE, 0, 0, 1000);
    Check(t.Active(), "armed");
    Check(t.Generation() == 1, "first cursor is generation 1");
    Check(t.Current().id == 0x2CE, "cursor id retained for echoing back");
    Check(t.CanReply(1), "the live generation may reply");
    Check(t.CanReply(0), "generation 0 means 'whatever is current'");

    // Replying consumes the cursor: Sphere's cursor is single-shot
    // (CClient::Event_Target clears the target mode before dispatching).
    t.OnReplied();
    Check(!t.Active(), "cursor consumed by the reply");
    Check(!t.CanReply(1), "cannot reply twice to the same cursor");

    // A second cursor supersedes the first. An action still holding
    // generation 1 must not be able to answer it.
    t.OnArmed(0x2CA, 0, 1, 2000);
    Check(t.Generation() == 2, "second cursor bumps the generation");
    Check(t.Current().subtype == 1, "harmful flag retained");
    Check(t.IsStale(1), "the old generation is stale");
    Check(!t.IsStale(2), "the current generation is not stale");
    Check(!t.CanReply(1), "a stale generation is refused");
    Check(t.CanReply(2), "the current generation is accepted");

    // Cancelling leaves no cursor but keeps the generation moving forward.
    t.OnCancelled();
    Check(!t.Active(), "cancel clears the cursor");
    t.OnArmed(0x2C9, 1, 0, 3000);
    Check(t.Generation() == 3 && t.Current().type == 1,
          "ground cursor armed as generation 3");

    // Two sessions must not share any of this.
    act::TargetState a, b;
    a.OnArmed(0x2CE, 0, 0, 10);
    Check(a.Active() && !b.Active(), "one session's cursor is not the other's");
    Check(a.Generation() == 1 && b.Generation() == 0,
          "generations are per-session");
    b.OnArmed(0x2CA, 0, 0, 20);
    Check(a.Current().id == 0x2CE && b.Current().id == 0x2CA,
          "cursor ids stay separate");
}

// ---------------------------------------------------------------------------
// Action results: the whole point is telling "sent" from "server-confirmed".
// ---------------------------------------------------------------------------
void TestAction() {
    Section("action result model");

    act::Action a;
    Check(!a.Active(), "idle action is not active");
    Check(a.kind == act::Kind::None, "idle action has no kind");

    a.Begin(act::Kind::MoveItem, 1000, 4000);
    Check(a.Active(), "started");
    Check(a.result == act::Result::Pending, "starts pending");
    Check(!act::Finished(a.result), "pending is not finished");
    Check(a.deadlineMs == 5000, "deadline is start + timeout");

    // Not yet due.
    Check(!a.ExpireIfDue(4999), "does not expire before the deadline");
    Check(a.Active(), "still active just before the deadline");

    Check(a.Finish(act::Result::Success), "finishing returns true once");
    Check(!a.Active(), "finished action is not active");
    Check(a.result == act::Result::Success, "result recorded");
    Check(act::Finished(a.result), "success is finished");
    Check(!a.Finish(act::Result::Rejected),
          "a finished action cannot be finished again");
    Check(a.result == act::Result::Success, "the first result stands");

    // Timeout path.
    act::Action t;
    t.Begin(act::Kind::CastSpell, 0, 100);
    Check(t.ExpireIfDue(100), "expires exactly at the deadline");
    Check(t.result == act::Result::Timeout, "expiry records a timeout");
    Check(!t.ExpireIfDue(200), "expiry only fires once");

    // Begin() must clear the previous action's context so stale serials can
    // never leak into the next action's confirmation rules.
    act::Action r;
    r.Begin(act::Kind::Equip, 0, 10);
    r.subject = 0xAABBCCDD;
    r.layer = 7;
    r.awaitingTarget = true;
    r.Begin(act::Kind::UseObject, 50, 10);
    Check(r.subject == 0 && r.layer == 0 && !r.awaitingTarget,
          "Begin clears the previous context");
    Check(r.kind == act::Kind::UseObject, "new kind set");

    // Names exist for every result and kind (they end up in the logs).
    Check(std::string(act::ResultName(act::Result::ServerFailure)) ==
              "server_failure", "result names");
    Check(std::string(act::KindName(act::Kind::VendorBuy)) == "vendor_buy",
          "kind names");
}

// ---------------------------------------------------------------------------
// Drag transactions: a rejected move must leave no belief that it happened.
// ---------------------------------------------------------------------------
void TestDragState() {
    Section("drag transaction");

    act::DragState d;
    Check(!d.InFlight(), "idle");
    Check(d.phase() == act::DragState::Phase::Idle, "idle phase");

    d.BeginLift(0x40001FF9, 1, 500);
    Check(d.InFlight(), "lift in flight");
    Check(d.phase() == act::DragState::Phase::Lifted, "lifted phase");
    Check(d.Serial() == 0x40001FF9 && d.Amount() == 1, "lift recorded");

    d.OnDropSent(0x40001FFE);
    Check(d.phase() == act::DragState::Phase::Dropped, "dropped phase");
    Check(d.Destination() == 0x40001FFE, "destination recorded");
    Check(d.InFlight(), "still in flight until the server confirms");

    // A 0x27 drag cancel, or a timeout, resets everything: nothing moved.
    d.Reset();
    Check(!d.InFlight() && d.Serial() == 0 && d.Destination() == 0,
          "reset clears the whole transaction");

    act::DragState x, y;
    x.BeginLift(1, 1, 0);
    Check(x.InFlight() && !y.InFlight(), "drag state is per-session");
}

// ---------------------------------------------------------------------------
// Life state is derived from the body the server sends -- never set locally.
// ---------------------------------------------------------------------------
void TestLifeState() {
    Section("life state");

    Check(!act::IsGhostBody(0x0190), "male body is alive");
    Check(!act::IsGhostBody(0x0191), "female body is alive");
    Check(act::IsGhostBody(0x0192), "0x192 is a ghost");
    Check(act::IsGhostBody(0x0193), "0x193 is a ghost");

    Check(act::LifeStateFromBody(0x0190) == act::LifeState::Alive,
          "alive from body");
    Check(act::LifeStateFromBody(0x0192) == act::LifeState::Dead,
          "dead from body");

    Check(std::string(act::LifeStateName(act::LifeState::Dead)) == "dead",
          "state name dead");
    Check(std::string(act::LifeStateName(act::LifeState::Alive)) == "alive",
          "state name alive");

    // The transition a death/resurrection cycle goes through.
    act::LifeState s = act::LifeStateFromBody(0x0190);
    s = act::LifeStateFromBody(0x0192);
    Check(s == act::LifeState::Dead, "alive -> dead");
    s = act::LifeStateFromBody(0x0190);
    Check(s == act::LifeState::Alive, "dead -> alive");
}

}  // namespace

int main() {
    std::printf("m2 action layer tests\n\n");
    TestTargetState();
    TestAction();
    TestDragState();
    TestLifeState();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
