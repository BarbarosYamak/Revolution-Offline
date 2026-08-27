#pragma once

#include "bot/Blacklist.h"
#include "uo/sphere_rules.h"
#include "uo/types.h"

#include <deque>
#include <random>
#include <vector>

namespace uo::navigation {

struct PendingMove {
    u8 seq = 0;
    u8 dir = 0;
    bool wasStep = false;
    i64 sentMs = 0;
};

struct RejectedEdge {
    i32 fromX = 0;
    i32 fromY = 0;
    i8  fromZ = 0;
    i32 toX = 0;
    i32 toY = 0;
    i8  toZ = 0;
};

// All movement submission state. Every 0x02 the client ever sends is issued
// by Client::SubmitStep() using this -- A* paths, scripted actions and manual
// keys alike -- so pacing, sequencing, the outstanding-step limit and
// ack/reject bookkeeping cannot be bypassed by any caller.
struct MovementState {
    u8 moveSeq = 0;             // next sequence to send (0 = resync)
    // Session gait. Auto is the standing order and resolves to Run
    // (sphere::GaitWantsRun, include/uo/sphere_rules.h) -- players run
    // everywhere, so bots do too. Individual steps may still ask for Walk
    // (final approach, doorways, shoves); those are per-step arguments to
    // SubmitStep, not changes to this field.
    sphere::Gait gait = sphere::Gait::Auto;
    std::deque<PendingMove> pending;
    i64 lastMoveSentMs = 0;     // for throttle + watchdog

    // Canonical UO on-foot step intervals. Sphere's walk-buffer speedhack
    // check only runs for running steps (CClient::Event_Walk,
    // src/game/clients/CClientEvent.cpp:905-935), so walking at 400ms is
    // unconditionally safe; running is paced at the canonical 200ms.
    u32 walkStepMs = 400;
    u32 runStepMs = 200;

    // Outstanding 0x02s allowed at once. 1 = strict request/ack, which is what
    // M1 measured as reject-free against Sphere.
    usize maxInFlight = 1;

    // Consecutive rejects seen while draining the current step queue.
    u32 rejectStreak = 0;
};

struct BotState {
    std::deque<u8> path;        // directions still to execute
    i32 goalX = 0;
    i32 goalY = 0;
    i32 goalZ = 0;              // valid only when hasGoalZ is true
    bool hasGoalZ = false;
    bool active = false;
    bool planning = false;
    bool terrainBias = true;    // false = no grass/foliage penalty (e.g. tree-to-tree)
    u64 planRequestId = 0;
    u64 nextPlanRequestId = 1;
    u32 replanCount = 0;
    // Latched for one replan when a failed plan found the character enclosed
    // with mobiles among the walls. Cleared by any plan that produces a path,
    // so a genuinely unreachable goal still fails in finite time.
    bool softMobileRetry = false;
    i64 resumeAtMs = 0;
    u32 stuckWaits = 0;         // consecutive wait-retries at the current bump cell
    bot::Blacklist blacklist;
    std::vector<RejectedEdge> rejectedEdges;
    std::vector<RejectedEdge> doorRetryEdges;
};

struct FollowState {
    bool active = false;
    u32 serial = 0;
    u32 distance = 1;
    i64 lastReplanMs = 0;
    i64 lastProbeMs = 0;
};

struct NavigationState {
    MovementState movement;
    BotState bot;
    FollowState follow;
    std::mt19937 rng;
    i64 lastFatigueMs = 0;      // last "too fatigued to move" message time
};

}
