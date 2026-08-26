#include "travel/Journey.h"

#include <cstring>

namespace uo::travel {

namespace {

i32 Chebyshev(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

u32 PackTile(i32 x, i32 y) {
    return (static_cast<u32>(x & 0xFFFF) << 16) | static_cast<u32>(y & 0xFFFF);
}

const char* const kPhaseNames[] = {
    "idle", "need_route", "walking", "at_transit", "arrived", "failed",
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
    recentTiles_.clear();
}

void Journey::Begin(const char* label, i32 goalX, i32 goalY, i32 arriveRadius,
                    i64 nowMs) {
    Reset();
    label_ = label ? label : "";
    goalX_ = goalX;
    goalY_ = goalY;
    arriveRadius_ = arriveRadius > 0 ? arriveRadius : 1;
    phase_ = Phase::NeedRoute;
    waitUntilMs_ = nowMs;
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
    waitUntilMs_ = nowMs;
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
    Fail(Failure::Unreachable, why ? why : "route exhausted");
}

void Journey::OnLegArrived(i32 x, i32 y, i64 nowMs) {
    if (!Active()) return;
    if (Chebyshev(x, y, goalX_, goalY_) <= arriveRadius_) {
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
    if (!Active()) return;

    // Anything the server did to our position invalidates the local plan: the
    // path we were walking starts somewhere we are no longer standing.
    if (Chebyshev(x, y, goalX_, goalY_) <= arriveRadius_) {
        phase_ = Phase::Arrived;
        return;
    }

    if (phase_ == Phase::AtTransit) {
        // The transit did its job. Continue from the far side.
        Advance(nowMs);
        if (phase_ == Phase::Walking && !CurrentLeg()) {
            // Nothing left in the route but we are not at the goal -- the far
            // side is somewhere the route did not anticipate, so replan.
            phase_ = Phase::NeedRoute;
        }
        return;
    }

    // An unplanned transition (a trap teleporter, a gate someone else opened,
    // a resurrection move). The old route is meaningless from here.
    phase_ = Phase::NeedRoute;
    legIssued_ = false;
    waitUntilMs_ = nowMs;
    bestDistance_ = 0x7FFFFFFF;
    noProgress_ = 0;
    recentTiles_.clear();
}

void Journey::OnPositionSample(i32 x, i32 y, i64 nowMs) {
    if (!Active()) {
        haveSample_ = true;
        lastX_ = x;
        lastY_ = y;
        return;
    }

    if (haveSample_ &&
        Chebyshev(x, y, lastX_, lastY_) >= limits_.transitionJumpTiles) {
        lastX_ = x;
        lastY_ = y;
        OnWorldTransition(x, y, nowMs);
        return;
    }
    haveSample_ = true;
    lastX_ = x;
    lastY_ = y;

    if (Chebyshev(x, y, goalX_, goalY_) <= arriveRadius_) {
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
    const u32 packed = PackTile(x, y);
    int repeats = 0;
    for (u32 t : recentTiles_)
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
        case Phase::NeedRoute:
            if (nowMs < waitUntilMs_) return Command::Wait;
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
                // wherever we ended up rather than declaring success.
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
    if (z) *z = 0;
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
