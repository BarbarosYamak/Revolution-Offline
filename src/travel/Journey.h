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
    // Extra pause before walking each successive REPLAN of the same trip,
    // multiplied by how many plans have already been spent. The first plan
    // walks immediately; the ones after it are reactions to trouble, and the
    // M3.9 38-bot soak showed what no pause does: four replans in eight
    // seconds against a bank tile that 37 other bots were also standing on.
    // The blocker there was other characters, and characters move -- given a
    // few seconds. Escalating linearly (1.5s, 3s, 4.5s) buys that time while
    // keeping the whole ladder bounded at ~10s.
    i64 replanBackoffMs = 1500;
    // Two z values are on the same floor when they differ by no more than
    // this. A UO storey is ~20 z-units; Sphere's own speech and shop checks
    // are three-dimensional, so "at the same (x,y), one floor up" is not
    // arrived. Mirrors the client's kSameFloorZ.
    i32 sameFloorZ = 12;
    // When every retry is spent and the character stands within this many
    // tiles PAST the arrive radius of the goal -- on the goal's floor -- the
    // trip arrives "nearby" instead of failing. Banks, forges and moongates
    // are exactly where every bot wants to stand, so the goal tile being
    // occupied is the common case, not the error case; the tile A* has
    // already walked us to the best free tile it could reach, and "within a
    // few tiles" is almost always what the caller actually wanted. Kept small
    // so an interaction-radius destination (a vendor, a banker) still ends in
    // speech range.
    i32 crowdedArriveSlack = 3;
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
    //
    // `hasGoalZ`/`goalZ` pin the destination's FLOOR when the caller knows it.
    // The journey was 2-D from M2.5 until M3.9 and that was the bridge bug:
    // the goal test could not tell a bridge deck from the water under it, so
    // walking beneath the destination counted as arriving and the trip was
    // then failed after the fact by the client's own floor check -- with no
    // recovery, because as far as the journey knew it had succeeded. With the
    // floor known here, standing under the goal is simply "not there yet" and
    // the normal replan/recovery ladder keeps working toward the right level.
    void Begin(const char* label, i32 goalX, i32 goalY, i32 arriveRadius,
               i64 nowMs, bool hasGoalZ = false, i8 goalZ = 0);
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
    bool HasGoalZ() const { return hasGoalZ_; }
    i8  GoalZ() const { return goalZ_; }
    i32 ArriveRadius() const { return arriveRadius_; }
    // Non-empty when the trip ended at a nearby tile because the destination
    // itself was not free (see Limits::crowdedArriveSlack). Empty for a clean
    // on-the-tile arrival. For the caller's logs; an arrival either way.
    const std::string& ArrivalNote() const { return arrivalNote_; }

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

    // Each event exists in a 2-D form (z unknown -- floor checks are skipped,
    // which is how the unit tests and any caller without a live z behave) and
    // a 3-D form (the client always knows its own z and should say so).
    void OnLegArrived(i32 x, i32 y, i64 nowMs);
    void OnLegArrived(i32 x, i32 y, i8 z, i64 nowMs);
    void OnLegFailed(const char* reason, i64 nowMs);
    // The client noticed the server moved us a long way in one step.
    void OnWorldTransition(i32 x, i32 y, i64 nowMs);
    void OnWorldTransition(i32 x, i32 y, i8 z, i64 nowMs);
    // Called every tick with the live position; drives stuck and oscillation
    // detection and finishes the trip when the goal radius is reached.
    void OnPositionSample(i32 x, i32 y, i64 nowMs);
    void OnPositionSample(i32 x, i32 y, i8 z, i64 nowMs);

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
    // The 3-D goal test: inside the arrive radius AND (when both sides know
    // their z) on the goal's floor.
    bool AtGoal(i32 x, i32 y, i8 z, bool zKnown) const;
    bool SameFloorAsGoal(i8 z, bool zKnown) const;
    // Every retry is spent. If the last known position is already within
    // crowdedArriveSlack of the goal on the right floor, arrive there instead
    // of failing; the tile A* has already put us on the best free tile.
    bool TryCrowdedArrival();
    void LegArrived(i32 x, i32 y, i8 z, bool zKnown, i64 nowMs);
    void WorldTransition(i32 x, i32 y, i8 z, bool zKnown, i64 nowMs);
    void PositionSample(i32 x, i32 y, i8 z, bool zKnown, i64 nowMs);

    Limits limits_;
    Phase  phase_ = Phase::Idle;
    Failure failure_ = Failure::None;
    std::string failureDetail_;
    std::string label_;

    i32 goalX_ = 0, goalY_ = 0;
    bool hasGoalZ_ = false;
    i8   goalZ_ = 0;
    i32 arriveRadius_ = 1;
    std::string arrivalNote_;

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
    i8  lastZ_ = 0;
    bool lastZKnown_ = false;
    // Packed (x, y, z), bounded window. z is part of the key on purpose: a
    // bridge deck and the ground beneath it share (x,y), and folding them
    // into one tile made a legitimate under-then-over crossing look like an
    // oscillation.
    std::vector<u64> recentTiles_;
};

} // namespace uo::travel
