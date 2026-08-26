#pragma once

// ---------------------------------------------------------------------------
// Journey — one trip, as a state machine.
//
// The client owns packets and the tile A*; this owns "where am I in the plan,
// and what should happen next". It never sends anything: it is fed events
// (a leg arrived, a leg failed, the world moved us) and asked what to do, so
// every decision in it -- leg sequencing, stuck detection, oscillation
// detection, the bounded recovery ladder, replan budgets -- is unit-testable
// without a server.
//
// Boundedness is the whole point. M2 measured that Sphere's flood protection
// re-arms its TTL when you retry, so a navigation layer that reacts to trouble
// by trying harder makes things worse. Every counter here has a ceiling and
// every ceiling ends in a clean failure.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "world/RoutePlanner.h"

#include <string>
#include <vector>

namespace uo::travel {

enum class Phase : u8 {
    Idle = 0,
    NeedRoute,     // a (re)plan is required before anything can move
    Walking,       // a walk leg is in progress
    AtTransit,     // standing at a transit, waiting for it to take effect
    Arrived,
    // The character is somewhere the world router cannot use -- sealed into an
    // upper storey, a walled pocket -- and the CLIENT has taken ownership of
    // movement to walk it back out. The journey is parked, not finished: it
    // keeps its goal and its identity, and resumes planning when the escape
    // reports back. Nothing may end the trip while it is in this phase.
    Recovering,
    Failed,
};

const char* PhaseName(Phase p);

enum class Failure : u8 {
    None = 0,
    NoRoute,        // the world planner found nothing
    Unreachable,    // the tile A* could not cross a leg, and replanning gave up
    Stuck,          // position stopped changing and recovery ran out
    TransitFailed,  // the gate/teleporter did not move us
    Aborted,        // the caller cancelled, or the character died
    Count,
};

const char* FailureName(Failure f);

// What the client should do next. Deliberately a request, not an action: the
// client decides whether it can honour it right now (it may be mid-action,
// dead, or logging out).
enum class Command : u8 {
    Idle = 0,
    PlanRoute,      // call the RoutePlanner from the current position
    WalkTo,         // start a tile-A* trip to CommandTarget()
    UseTransit,     // interact with the transit at CommandTarget()
    Wait,           // do nothing until WaitUntilMs()
    Finish,
    Fail,
};

const char* CommandName(Command c);

struct Limits {
    // Tile-A* failures on one leg before the world route is replanned instead.
    int maxLegRetries = 2;
    // World-route replans before the trip is declared unreachable.
    int maxRoutePlans = 4;
    // Samples with no progress toward the leg target before recovery kicks in.
    int maxNoProgressSamples = 12;
    // Distinct tiles a bot may bounce between before it counts as oscillating.
    int oscillationWindow = 6;
    int maxOscillations = 3;
    // Pause between recovery attempts. Long enough that a transient blocker
    // (an NPC in a doorway) clears on its own, short enough to feel like a
    // player waiting rather than a hang.
    i64 recoveryPauseMs = 1200;
    // How long a transit gets to move us before we call it failed.
    i64 transitTimeoutMs = 8000;
    // Escape attempts, each at a different anchor, before the character is
    // reported as genuinely sealed in. Bounded so a pocket cannot become an
    // infinite walk.
    int maxPositionRecoveries = 3;
    // A jump of at least this many tiles between two position samples is a
    // world transition (recall, gate, teleporter), not walking.
    i32 transitionJumpTiles = 24;
};

class Journey {
public:
    Journey() = default;

    // Start a trip. `label` is for logs only. `arriveRadius` is how close
    // counts as arrived at the final destination -- a place's interaction
    // radius, not a tile match, because NPCs and doorways move.
    void Begin(const char* label, i32 goalX, i32 goalY, i32 arriveRadius,
               i64 nowMs);
    void Abort(const char* why);
    void Reset();

    // Recovering counts as active on purpose. A parent journey that reported
    // itself inactive while its escape walk was still running is exactly the
    // orphaned-recovery bug M3.5 found on the Mage Tower.
    bool Active() const { return phase_ == Phase::NeedRoute ||
                                 phase_ == Phase::Walking ||
                                 phase_ == Phase::AtTransit ||
                                 phase_ == Phase::Recovering; }
    Phase   CurrentPhase() const { return phase_; }
    Failure FailureReason() const { return failure_; }
    const std::string& FailureDetail() const { return failureDetail_; }
    const std::string& Label() const { return label_; }

    i32 GoalX() const { return goalX_; }
    i32 GoalY() const { return goalY_; }
    i32 ArriveRadius() const { return arriveRadius_; }

    // --- planning ----------------------------------------------------------

    // Hand the planner's answer back. A failed route is a failed trip unless
    // there is replan budget left and something has changed.
    void SetRoute(const route::WorldRoute& r, i64 nowMs);
    int  RoutePlans() const { return routePlans_; }
    int  LegIndex() const { return legIndex_; }
    usize LegCount() const { return route_.legs.size(); }
    const route::RouteLeg* CurrentLeg() const;
    // The leg after the current one. A transit's gump can arrive while the
    // approach leg is still running, so the client has to be able to see it
    // coming.
    const route::RouteLeg* NextLeg() const;
    const route::WorldRoute& Route() const { return route_; }

    // Macro cells this trip has failed to cross, to feed back into the next
    // plan. Per-journey, so it dies with the trip and never leaks to another
    // character.
    const std::vector<u32>& AvoidCells() const { return avoidCells_; }
    void AvoidCell(u32 cell);

    // --- events ------------------------------------------------------------

    void OnLegArrived(i32 x, i32 y, i64 nowMs);
    void OnLegFailed(const char* reason, i64 nowMs);
    // The client noticed the server moved us a long way in one step.
    void OnWorldTransition(i32 x, i32 y, i64 nowMs);
    // Called every tick with the live position; drives stuck and oscillation
    // detection and finishes the trip when the goal radius is reached.
    void OnPositionSample(i32 x, i32 y, i64 nowMs);

    // --- driving -----------------------------------------------------------

    // What the client should do now. Pure query except for the wait clock.
    Command NextCommand(i64 nowMs) const;
    void CommandTarget(i32* x, i32* y, i8* z) const;
    i64  WaitUntilMs() const { return waitUntilMs_; }
    // The client calls this after it has actually started the walk/transit it
    // was told to, so the journey stops re-issuing it.
    void NoteCommandIssued(Command c, i64 nowMs);

    // --- position recovery -------------------------------------------------
    //
    // Distinct from the per-leg retry below: this is "the character is in the
    // wrong PLACE", not "this leg was hard". The client owns the movement
    // while it runs; the journey parks and keeps the destination.
    //
    // Returns false when the recovery budget is spent, which is the caller's
    // signal to fail the trip cleanly -- with evidence -- rather than loop.
    bool BeginPositionRecovery(const char* why, i64 nowMs);
    // The escape walk finished. `reached` says whether the anchor was actually
    // arrived at. Reaching it resumes planning for the ORIGINAL goal; not
    // reaching it spends an attempt and lets the caller try another anchor.
    void OnPositionRecovered(bool reached, i64 nowMs);
    bool Recovering() const { return phase_ == Phase::Recovering; }
    int  PositionRecoveries() const { return positionRecoveries_; }
    const std::string& RecoveryReason() const { return recoveryReason_; }

    // --- introspection for tests and logs ----------------------------------
    int LegRetries() const { return legRetries_; }
    int NoProgressSamples() const { return noProgress_; }
    int Oscillations() const { return oscillations_; }
    const Limits& GetLimits() const { return limits_; }
    void SetLimits(const Limits& l) { limits_ = l; }

private:
    void Fail(Failure f, const char* detail);
    void Advance(i64 nowMs);
    // Set the phase from the current leg's kind. Shared by SetRoute and
    // Advance so "the leg is a transit" is decided in exactly one place.
    void EnterLegPhase(i64 nowMs);
    void BeginRecovery(const char* why, i64 nowMs);
    i32  DistanceToLegTarget(i32 x, i32 y) const;

    Limits limits_;
    Phase  phase_ = Phase::Idle;
    Failure failure_ = Failure::None;
    std::string failureDetail_;
    std::string label_;

    i32 goalX_ = 0, goalY_ = 0;
    i32 arriveRadius_ = 1;

    route::WorldRoute route_;
    int  legIndex_ = 0;
    bool legIssued_ = false;      // the current leg's command has been started
    int  legRetries_ = 0;
    int  routePlans_ = 0;
    std::vector<u32> avoidCells_;

    i64 waitUntilMs_ = 0;
    i64 transitStartedMs_ = 0;
    int positionRecoveries_ = 0;
    std::string recoveryReason_;

    // Progress tracking for the current leg.
    i32 bestDistance_ = 0x7FFFFFFF;
    int noProgress_ = 0;
    int oscillations_ = 0;
    bool haveSample_ = false;
    i32 lastX_ = 0, lastY_ = 0;
    std::vector<u32> recentTiles_;   // packed (x<<16|y), bounded window
};

} // namespace uo::travel
