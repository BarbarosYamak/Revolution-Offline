#include "travel/Journey.h"

#include <cstring>

namespace uo::travel {

namespace {

i32 Chebyshev(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

// z is part of the key (see the field comment in Journey.h): a 2-D pack made
// a bridge deck and the ground under it the same tile.
u64 PackTile(i32 x, i32 y, i8 z) {
    return (static_cast<u64>(static_cast<u16>(x)) << 32) |
           (static_cast<u64>(static_cast<u16>(y)) << 16) |
           static_cast<u64>(static_cast<u8>(z));
}

i32 AbsDiff(i8 a, i8 b) {
    const i32 d = static_cast<i32>(a) - static_cast<i32>(b);
    return d < 0 ? -d : d;
}

const char* const kPhaseNames[] = {
    "idle", "need_route", "walking", "at_transit", "arrived", "recovering",
    "failed",
};

const char* const kFailureNames[] = {
    "none", "no_route", "unreachable", "stuck", "transit_failed", "aborted",
};
static_assert(sizeof(kFailureNames) / sizeof(kFailureNames[0]) ==
                  static_cast<usize>(Failure::Count),
              "kFailureNames is out of step with Failure");

const char* const kCommandNames[] = {
    "idle", "plan_route", "walk_to", "use_transit", "wait", "finish", "fail",
};

} // namespace

const char* PhaseName(Phase p) {
    const usize i = static_cast<usize>(p);
    return i < sizeof(kPhaseNames) / sizeof(kPhaseNames[0]) ? kPhaseNames[i]
                                                            : "?";
}

const char* FailureName(Failure f) {
    const usize i = static_cast<usize>(f);
    return i < static_cast<usize>(Failure::Count) ? kFailureNames[i] : "?";
}

const char* CommandName(Command c) {
    const usize i = static_cast<usize>(c);
    return i < sizeof(kCommandNames) / sizeof(kCommandNames[0])
               ? kCommandNames[i]
               : "?";
}

void Journey::Reset() {
    phase_ = Phase::Idle;
    failure_ = Failure::None;
    failureDetail_.clear();
    label_.clear();
    route_ = route::WorldRoute{};
    legIndex_ = 0;
    legIssued_ = false;
    legRetries_ = 0;
    routePlans_ = 0;
    avoidCells_.clear();
    waitUntilMs_ = 0;
    transitStartedMs_ = 0;
    bestDistance_ = 0x7FFFFFFF;
    noProgress_ = 0;
    oscillations_ = 0;
    haveSample_ = false;
    lastZKnown_ = false;
    recentTiles_.clear();
    positionRecoveries_ = 0;
    recoveryReason_.clear();
    hasGoalZ_ = false;
    goalZ_ = 0;
    arrivalNote_.clear();
}

void Journey::Begin(const char* label, i32 goalX, i32 goalY, i32 arriveRadius,
                    i64 nowMs, bool hasGoalZ, i8 goalZ) {
    Reset();
    label_ = label ? label : "";
    goalX_ = goalX;
    goalY_ = goalY;
    hasGoalZ_ = hasGoalZ;
    goalZ_ = goalZ;
    arriveRadius_ = arriveRadius > 0 ? arriveRadius : 1;
    phase_ = Phase::NeedRoute;
    waitUntilMs_ = nowMs;
}

bool Journey::SameFloorAsGoal(i8 z, bool zKnown) const {
    // A floor mismatch can only be asserted when BOTH sides know their z.
    // Failing "unknown" would break every caller that plans in 2-D.
    if (!hasGoalZ_ || !zKnown) return true;
    return AbsDiff(z, goalZ_) <= limits_.sameFloorZ;
}

bool Journey::AtGoal(i32 x, i32 y, i8 z, bool zKnown) const {
    return Chebyshev(x, y, goalX_, goalY_) <= arriveRadius_ &&
           SameFloorAsGoal(z, zKnown);
}

bool Journey::TryCrowdedArrival() {
    if (!haveSample_) return false;
    if (Chebyshev(lastX_, lastY_, goalX_, goalY_) >
        arriveRadius_ + limits_.crowdedArriveSlack)
        return false;
    if (!SameFloorAsGoal(lastZ_, lastZKnown_)) return false;
    arrivalNote_ = "arrived nearby; the destination tile was not free";
    phase_ = Phase::Arrived;
    return true;
}

// --- position recovery ------------------------------------------------------
//
// The lifecycle this replaces, and why:
//
// M2.5 recovered from "sealed into an upper storey" by calling Begin() again
// for the same destination. That restarted the journey from scratch while the
// escape walk was still being planned, so the parent trip died and the walk
// carried on with nobody waiting for it -- an orphaned recovery. M3.5 saw it
// live on the Mage Tower and could only half-fix it.
//
// Now the journey PARKS. It keeps its label, its goal, its arrive radius and
// its avoid-cell memory, reports itself active throughout, and issues Wait so
// the client is the only thing moving. When the escape reports back, planning
// resumes for the ORIGINAL destination.
bool Journey::BeginPositionRecovery(const char* why, i64 nowMs) {
    // Recovery must be allowed FROM Failed: a character sealed into an upper
    // storey fails at plan time, and that failed plan is precisely the moment
    // the escape has to start. Refusing here was the first version's bug.
    // Idle and Arrived are genuinely over, and an exhausted budget is what
    // makes a failure final.
    if (phase_ == Phase::Idle || phase_ == Phase::Arrived) return false;
    if (positionRecoveries_ >= limits_.maxPositionRecoveries) return false;

    // Un-fail the trip: it is alive again for as long as recovery runs.
    failure_ = Failure::None;
    failureDetail_.clear();

    ++positionRecoveries_;
    recoveryReason_ = why ? why : "unreachable from here";
    phase_ = Phase::Recovering;
    // Per-leg progress state belongs to the route we are abandoning.
    legIssued_ = false;
    legRetries_ = 0;
    noProgress_ = 0;
    oscillations_ = 0;
    bestDistance_ = 0x7FFFFFFF;
    haveSample_ = false;
    recentTiles_.clear();
    waitUntilMs_ = nowMs;
    return true;
}

void Journey::OnPositionRecovered(bool reached, i64 nowMs) {
    if (phase_ != Phase::Recovering) return;

    if (reached) {
        // Somewhere the router can see. Re-plan for the goal we never gave up
        // on. Replan budget is refreshed because the earlier failures were
        // about the old position, not about this route being impossible.
        routePlans_ = 0;
        route_ = route::WorldRoute{};
        legIndex_ = 0;
        phase_ = Phase::NeedRoute;
        waitUntilMs_ = nowMs;
        return;
    }

    // The anchor was not reached. Another attempt may still work from here;
    // when the budget runs out the trip fails with the reason that started it,
    // which is far more useful than "no route".
    if (positionRecoveries_ >= limits_.maxPositionRecoveries) {
        Fail(Failure::Unreachable, recoveryReason_.empty()
                                       ? "sealed in; recovery exhausted"
                                       : recoveryReason_.c_str());
        return;
    }
    phase_ = Phase::Recovering;
    waitUntilMs_ = nowMs + limits_.recoveryPauseMs;
}

void Journey::Abort(const char* why) {
    if (phase_ == Phase::Arrived || phase_ == Phase::Failed) return;
    Fail(Failure::Aborted, why ? why : "aborted");
}

void Journey::Fail(Failure f, const char* detail) {
    phase_ = Phase::Failed;
    failure_ = f;
    failureDetail_ = detail ? detail : "";
}

const route::RouteLeg* Journey::CurrentLeg() const {
    if (legIndex_ < 0 ||
        static_cast<usize>(legIndex_) >= route_.legs.size())
        return nullptr;
    return &route_.legs[static_cast<usize>(legIndex_)];
}

const route::RouteLeg* Journey::NextLeg() const {
    const usize i = static_cast<usize>(legIndex_) + 1;
    if (legIndex_ < 0 || i >= route_.legs.size()) return nullptr;
    return &route_.legs[i];
}

void Journey::AvoidCell(u32 cell) {
    for (u32 c : avoidCells_)
        if (c == cell) return;
    // Bounded: an avoid list that grows without limit is how a planner ends up
    // ruling out the whole map after enough bad luck.
    if (avoidCells_.size() >= 32) return;
    avoidCells_.push_back(cell);
}

void Journey::SetRoute(const route::WorldRoute& r, i64 nowMs) {
    ++routePlans_;
    if (!r.ok || r.legs.empty()) {
        Fail(Failure::NoRoute, r.failure && *r.failure ? r.failure
                                                       : "planner found no route");
        return;
    }
    route_ = r;
    legIndex_ = 0;
    legIssued_ = false;
    legRetries_ = 0;
    bestDistance_ = 0x7FFFFFFF;
    noProgress_ = 0;
    recentTiles_.clear();
    // The first plan of a trip walks immediately; every later plan is a
    // reaction to trouble and backs off before walking (see Limits
    // ::replanBackoffMs -- the 38-bot soak replanned four times in eight
    // seconds at a tile other bots were standing on, and the pause is what
    // gives a crowd time to move).
    waitUntilMs_ = nowMs + (routePlans_ > 1
                                ? (routePlans_ - 1) * limits_.replanBackoffMs
                                : 0);
    // The FIRST leg can be a transit: a bot that is already standing on the
    // gate needs no walk to reach it, so the planner emits none. Assuming
    // Walking here made the journey "arrive" at the tile it was already on and
    // advance straight past the hop.
    EnterLegPhase(nowMs);
}

void Journey::EnterLegPhase(i64 nowMs) {
    const route::RouteLeg* leg = CurrentLeg();
    phase_ = (leg && leg->kind != route::LegKind::Walk) ? Phase::AtTransit
                                                        : Phase::Walking;
    if (phase_ == Phase::AtTransit) transitStartedMs_ = nowMs;
}

i32 Journey::DistanceToLegTarget(i32 x, i32 y) const {
    const route::RouteLeg* leg = CurrentLeg();
    if (!leg) return Chebyshev(x, y, goalX_, goalY_);
    return Chebyshev(x, y, leg->target.x, leg->target.y);
}

void Journey::Advance(i64 nowMs) {
    ++legIndex_;
    legIssued_ = false;
    legRetries_ = 0;
    bestDistance_ = 0x7FFFFFFF;
    noProgress_ = 0;
    recentTiles_.clear();
    waitUntilMs_ = nowMs;

    if (static_cast<usize>(legIndex_) >= route_.legs.size()) {
        // The route is spent. Whether we actually arrived is decided by the
        // position sample against the goal radius, not by the leg count --
        // the last leg can legitimately stop a tile or two short.
        phase_ = Phase::Walking;
        // Pause before the replan this state leads to: a route that ran out
        // short of the goal means the world disagreed with the plan, and the
        // commonest disagreement -- somebody standing on the tile -- clears
        // by itself given a moment.
        waitUntilMs_ = nowMs + limits_.recoveryPauseMs;
        if (routePlans_ >= limits_.maxRoutePlans) {
            // No replan budget left either. This state MUST resolve here.
            // Leaving it pending was the M3.9 soak's 99,290-event spin:
            // NextCommand() answered Fail but nothing ever moved the phase to
            // Failed, so the journey stayed Active and the client re-reported
            // the same failure every tick, 16 times a second, forever.
            if (!TryCrowdedArrival())
                Fail(Failure::Unreachable,
                     "route spent short of the goal; replan budget exhausted");
        }
        return;
    }
    EnterLegPhase(nowMs);
}

void Journey::BeginRecovery(const char* why, i64 nowMs) {
    // The ladder, in order of how much it costs:
    //   1. re-run the tile A* for the same leg (a transient blocker moved)
    //   2. rule this macro cell out and replan the world route around it
    //   3. give up cleanly
    const route::RouteLeg* leg = CurrentLeg();
    const bool onTransit = leg && leg->kind != route::LegKind::Walk;

    if (legRetries_ < limits_.maxLegRetries) {
        ++legRetries_;
        legIssued_ = false;
        bestDistance_ = 0x7FFFFFFF;
        noProgress_ = 0;
        recentTiles_.clear();
        waitUntilMs_ = nowMs + limits_.recoveryPauseMs;
        // Retrying a transit means using the gate again, NOT walking to it:
        // the bot is already standing on it, so a walk leg would "arrive"
        // instantly and advance past the hop it never took -- which is how a
        // failed gate turned into a nine-minute walk across the continent.
        phase_ = onTransit ? Phase::AtTransit : Phase::Walking;
        return;
    }
    if (routePlans_ < limits_.maxRoutePlans) {
        phase_ = Phase::NeedRoute;
        legIssued_ = false;
        waitUntilMs_ = nowMs + limits_.recoveryPauseMs;
        return;
    }
    // The ladder is spent. If it was spent within a few tiles of the goal on
    // the goal's own floor, that is a crowded destination, not an unreachable
    // one -- take the tile we are standing on and call it arrived.
    if (TryCrowdedArrival()) return;
    Fail(Failure::Unreachable, why ? why : "route exhausted");
}

void Journey::OnLegArrived(i32 x, i32 y, i64 nowMs) {
    LegArrived(x, y, 0, /*zKnown=*/false, nowMs);
}

void Journey::OnLegArrived(i32 x, i32 y, i8 z, i64 nowMs) {
    LegArrived(x, y, z, /*zKnown=*/true, nowMs);
}

void Journey::LegArrived(i32 x, i32 y, i8 z, bool zKnown, i64 nowMs) {
    if (!Active()) return;
    // A leg report is a position observation too; keep the last-known
    // position fresh so a crowded-arrival decision made inside Advance sees
    // where we actually stand, not where a 500ms-old sample left us.
    haveSample_ = true;
    lastX_ = x;
    lastY_ = y;
    if (zKnown) { lastZ_ = z; lastZKnown_ = true; }
    if (AtGoal(x, y, z, zKnown)) {
        phase_ = Phase::Arrived;
        return;
    }
    Advance(nowMs);
}

void Journey::OnLegFailed(const char* reason, i64 nowMs) {
    if (!Active()) return;
    BeginRecovery(reason, nowMs);
}

void Journey::OnWorldTransition(i32 x, i32 y, i64 nowMs) {
    WorldTransition(x, y, 0, /*zKnown=*/false, nowMs);
}

void Journey::OnWorldTransition(i32 x, i32 y, i8 z, i64 nowMs) {
    WorldTransition(x, y, z, /*zKnown=*/true, nowMs);
}

void Journey::WorldTransition(i32 x, i32 y, i8 z, bool zKnown, i64 nowMs) {
    if (!Active()) return;

    // Anything the server did to our position invalidates the local plan: the
    // path we were walking starts somewhere we are no longer standing.
    if (AtGoal(x, y, z, zKnown)) {
        phase_ = Phase::Arrived;
        return;
    }

    if (phase_ == Phase::AtTransit) {
        // The transit did its job. Continue from the far side. Advance owns
        // the route-spent case, including failing cleanly when there is no
        // replan budget left to route the unexpected far side.
        Advance(nowMs);
        if (phase_ == Phase::Walking && !CurrentLeg()) {
            // Nothing left in the route but we are not at the goal -- the far
            // side is somewhere the route did not anticipate, so replan.
            // (Advance guarantees budget remains when it leaves this state.)
            phase_ = Phase::NeedRoute;
        }
        return;
    }

    // An unplanned transition (a trap teleporter, a gate someone else opened,
    // a resurrection move). The old route is meaningless from here.
    if (routePlans_ >= limits_.maxRoutePlans) {
        // NeedRoute with no plan budget is not a state, it is a pending
        // failure -- resolving it here rather than letting NextCommand report
        // an unexplained Fail forever is part of the M3.9 spin fix.
        Fail(Failure::Unreachable,
             "the world moved us and the replan budget is spent");
        return;
    }
    phase_ = Phase::NeedRoute;
    legIssued_ = false;
    waitUntilMs_ = nowMs;
    bestDistance_ = 0x7FFFFFFF;
    noProgress_ = 0;
    recentTiles_.clear();
}

void Journey::OnPositionSample(i32 x, i32 y, i64 nowMs) {
    PositionSample(x, y, 0, /*zKnown=*/false, nowMs);
}

void Journey::OnPositionSample(i32 x, i32 y, i8 z, i64 nowMs) {
    PositionSample(x, y, z, /*zKnown=*/true, nowMs);
}

void Journey::PositionSample(i32 x, i32 y, i8 z, bool zKnown, i64 nowMs) {
    if (!Active()) {
        haveSample_ = true;
        lastX_ = x;
        lastY_ = y;
        if (zKnown) { lastZ_ = z; lastZKnown_ = true; }
        return;
    }

    if (haveSample_ &&
        Chebyshev(x, y, lastX_, lastY_) >= limits_.transitionJumpTiles) {
        lastX_ = x;
        lastY_ = y;
        if (zKnown) { lastZ_ = z; lastZKnown_ = true; }
        WorldTransition(x, y, z, zKnown, nowMs);
        return;
    }
    haveSample_ = true;
    lastX_ = x;
    lastY_ = y;
    if (zKnown) { lastZ_ = z; lastZKnown_ = true; }

    if (AtGoal(x, y, z, zKnown)) {
        phase_ = Phase::Arrived;
        return;
    }

    if (phase_ == Phase::AtTransit) {
        if (transitStartedMs_ && legIssued_ &&
            nowMs - transitStartedMs_ > limits_.transitTimeoutMs)
            BeginRecovery("transit did not move us", nowMs);
        return;
    }

    if (phase_ != Phase::Walking || !legIssued_) return;

    // Progress: strictly decreasing distance to the leg target. A bot that
    // walks around an obstacle briefly moves away, which is why this counts
    // samples rather than reacting to a single bad one.
    const i32 d = DistanceToLegTarget(x, y);
    if (d < bestDistance_) {
        bestDistance_ = d;
        noProgress_ = 0;
    } else {
        ++noProgress_;
    }

    // Oscillation: the same tile keeps coming back inside a short window. This
    // catches the shuffle a bot does between two cells that each reroute into
    // the other, which "no progress" alone can miss when the distance is equal.
    const u64 packed = PackTile(x, y, zKnown ? z : 0);
    int repeats = 0;
    for (u64 t : recentTiles_)
        if (t == packed) ++repeats;
    recentTiles_.push_back(packed);
    if (static_cast<int>(recentTiles_.size()) > limits_.oscillationWindow)
        recentTiles_.erase(recentTiles_.begin());
    if (repeats >= 2) {
        ++oscillations_;
        if (oscillations_ >= limits_.maxOscillations) {
            oscillations_ = 0;
            BeginRecovery("oscillating between tiles", nowMs);
            return;
        }
    }

    if (noProgress_ >= limits_.maxNoProgressSamples) {
        noProgress_ = 0;
        BeginRecovery("no progress toward the waypoint", nowMs);
    }
}

Command Journey::NextCommand(i64 nowMs) const {
    switch (phase_) {
        case Phase::Idle:    return Command::Idle;
        case Phase::Arrived: return Command::Finish;
        case Phase::Failed:  return Command::Fail;
        // The client owns movement while an escape walk is in flight. Asking
        // it to walk or plan here is what produced two movement owners and an
        // orphaned recovery.
        case Phase::Recovering: return Command::Wait;
        case Phase::NeedRoute:
            if (nowMs < waitUntilMs_) return Command::Wait;
            // Last resort only. Every transition INTO NeedRoute now checks the
            // plan budget and fails the trip properly instead, because a Fail
            // answered from a live phase leaves failure_ unset and the phase
            // untouched -- the caller reports "none" and, worse, the journey
            // stays Active, which is the M3.9 16-Hz spin. The client also
            // force-terminates on Fail as a second line of defence.
            if (routePlans_ >= limits_.maxRoutePlans) return Command::Fail;
            return Command::PlanRoute;
        case Phase::AtTransit:
            if (legIssued_) return Command::Wait;
            if (nowMs < waitUntilMs_) return Command::Wait;
            return Command::UseTransit;
        case Phase::Walking:
            if (legIssued_) return Command::Wait;
            if (nowMs < waitUntilMs_) return Command::Wait;
            if (!CurrentLeg()) {
                // Route spent without reaching the goal radius: replan from
                // wherever we ended up rather than declaring success. The
                // budget-exhausted arm is a last resort like NeedRoute's:
                // Advance resolves that case itself (crowded arrival or a
                // proper Fail) precisely so this Fail is never the answer.
                return routePlans_ < limits_.maxRoutePlans ? Command::PlanRoute
                                                           : Command::Fail;
            }
            return Command::WalkTo;
    }
    return Command::Idle;
}

void Journey::CommandTarget(i32* x, i32* y, i8* z) const {
    const route::RouteLeg* leg = CurrentLeg();
    if (leg) {
        if (x) *x = leg->target.x;
        if (y) *y = leg->target.y;
        if (z) *z = leg->target.z;
        return;
    }
    if (x) *x = goalX_;
    if (y) *y = goalY_;
    if (z) *z = hasGoalZ_ ? goalZ_ : 0;
}

void Journey::NoteCommandIssued(Command c, i64 nowMs) {
    switch (c) {
        case Command::WalkTo:
            legIssued_ = true;
            bestDistance_ = 0x7FFFFFFF;
            noProgress_ = 0;
            break;
        case Command::UseTransit:
            legIssued_ = true;
            transitStartedMs_ = nowMs;
            break;
        case Command::PlanRoute:
            // routePlans_ is counted in SetRoute so a planner that never
            // answers cannot silently burn the budget.
            legIssued_ = false;
            break;
        default:
            break;
    }
}

} // namespace uo::travel
