#pragma once

#include "bot/Blacklist.h"
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

struct MovementState {
    u8 moveSeq = 0;             // next sequence to send (0 = resync)
    bool run = true;            // send the 0x80 run bit and use run cadence
    std::deque<PendingMove> pending;
    i64 lastMoveSentMs = 0;     // for throttle + watchdog
};

struct BotState {
    std::deque<u8> path;        // directions still to execute
    i32 goalX = 0;
    i32 goalY = 0;
    i32 goalZ = 0;              // valid only when hasGoalZ is true
    bool hasGoalZ = false;
    bool active = false;
    bool planning = false;
    u64 planRequestId = 0;
    u64 nextPlanRequestId = 1;
    u32 replanCount = 0;
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
