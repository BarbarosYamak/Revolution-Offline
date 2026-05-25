#include "Client.h"

#include "bot/Pathfinding.h"
#include "uo/builders.h"
#include "uo/endian.h"
#include "uo/map.h"
#include "uo/tiledata.h"
#include "uo/world.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <utility>

namespace uo {

namespace {

// Depth-1 movement: this server's 0x22 ack carries no position, so deeper
// pipelines desync when a mid-flight move is rejected (we'd never learn where
// the surviving moves left us). One confirmed step at a time stays exact.
constexpr usize kMaxInFlight = 1;
// Canonical UO on-foot step intervals. Pacing at these rates, not faster,
// is how the original client avoids fastwalk/speedhack checks.
constexpr u32 kWalkThrottleMs = 400;
constexpr u32 kRunThrottleMs = 200;
constexpr u32 kAckWatchdogMs = 5000;
// Per-trip A* replan budget -- bounds obstacle-avoidance loops on an
// unreachable goal.
constexpr u32 kMaxReplans = 128;
// Extra A* cost for stepping onto open grass (vs the 10/14 straight/diag
// base) -- biases travel toward roads/dirt where mobs are sparser.
constexpr u32 kGrassPenalty = 14;
// Extra A* cost for stepping into woods (a cell in/next to foliage). Heavier
// than open grass so the bot skirts forests instead of threading the trees.
constexpr u32 kForestPenalty = 15;
// Mobile cache + stamina/shove handling: a tile holding a mobile, or a reject
// right after a "too fatigued" message, is never blacklisted -- we wait and
// retry (the mob moves, or stamina regenerates and the shove succeeds).
constexpr i64   kFatigueWindowMs = 1500;  // a reject this soon after a fatigue msg = stamina
constexpr i64   kStaminaWaitMs   = 2000;  // let stamina regen before retrying
constexpr i64   kMobileWaitMs    = 500;   // let the mobile step aside / shove cooldown
constexpr u32   kMobileRepathAfter = 15;  // after N mobile bumps, reroute around it
constexpr u32   kMaxStuckWaits   = 25;    // give up the trip (no blacklist) after this
constexpr i64   kFollowReplanMinMs = 120;
constexpr i64   kFollowProbeMs     = 1200;
constexpr usize kPathLookaheadScanSteps = 5;
constexpr usize kPathLookaheadAnchorExtra = 5;
constexpr u32   kLookaheadMaxNodesExpanded = 4096;
constexpr i64   kLookaheadMobileFreshMs = 5000;
constexpr i64   kDoorRetryWaitMs = 700;
constexpr i32   kGoalZPreferenceRadius = 24;
constexpr i32   kRejectedEdgeZTolerance = 2;
constexpr usize kNoPreviewStep = static_cast<usize>(-1);

// Canonical openable door graphics (wood/metal/barred/gates, closed+open
// states). Tunable; covers town/building doors a traveller meets.
bool IsDoorGraphic(u16 id) { return id >= 0x0675 && id <= 0x06F6; }

inline i32 AbsDiff(i32 a, i32 b) {
    i32 d = a - b;
    return d < 0 ? -d : d;
}

inline i32 ChebyshevDistance(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = AbsDiff(ax, bx);
    const i32 dy = AbsDiff(ay, by);
    return dx > dy ? dx : dy;
}

bool ShouldPreferBotGoalZ(bool hasGoalZ, i32 x, i32 y, i32 gx, i32 gy) {
    if (!hasGoalZ) return false;
    return ChebyshevDistance(x, y, gx, gy) <= kGoalZPreferenceRadius;
}

world::WalkQuery MakeWalkQuery(i32 x, i32 y, i8 fromZ) {
    world::WalkQuery q{};
    q.x = static_cast<u32>(x);
    q.y = static_cast<u32>(y);
    q.fromZ = fromZ;
    return q;
}

world::WalkQuery MakeGoalAwareWalkQuery(i32 x, i32 y, i8 fromZ,
                                        bool hasGoalZ, i32 goalX, i32 goalY,
                                        i32 goalZ) {
    world::WalkQuery q = MakeWalkQuery(x, y, fromZ);
    q.hasPreferredZ = ShouldPreferBotGoalZ(hasGoalZ, x, y, goalX, goalY);
    q.preferredZ = static_cast<i8>(goalZ);
    return q;
}

bool SameDirectedEdgeNearZ(i32 edgeFromX, i32 edgeFromY, i8 edgeFromZ,
                           i32 edgeToX, i32 edgeToY, i8 edgeToZ,
                           i32 fromX, i32 fromY, i8 fromZ,
                           i32 toX, i32 toY, i8 toZ) {
    if (edgeFromX != fromX || edgeFromY != fromY ||
        edgeToX != toX || edgeToY != toY) {
        return false;
    }
    return AbsDiff(fromZ, edgeFromZ) <= kRejectedEdgeZTolerance &&
           AbsDiff(toZ, edgeToZ) <= kRejectedEdgeZTolerance;
}

}

// ---------------------------------------------------------------------------
// 0x21 Move Reject (8 bytes). Server rejected our move; here is your
// authoritative position.
//   [0]    cmd
//   [1]    sequence (the move being rejected)
//   [2-3]  x BE
//   [4-5]  y BE
//   [6]    direction
//   [7]    z (signed)
// ---------------------------------------------------------------------------
void Client::OnMoveReject(const u8* data, usize size) {
    if (size < 8) return;
    const u8  seq     = data[1];
    const u16 x       = LoadBE16(data + 2);
    const u16 y       = LoadBE16(data + 4);
    const u8  dirByte = data[6];
    const i8  z       = static_cast<i8>(data[7]);

    // Direction of the move that was rejected (prefer the queued move's dir;
    // fall back to the server's reported facing).
    u8 rdir = dirByte & 0x07;
    bool rejectedWasStep = false;
    for (const auto& pm : nav_.movement.pending)
        if (pm.seq == seq) { rdir = pm.dir; rejectedWasStep = pm.wasStep; break; }

    LogWarn(
        "[0x21] move REJECTED seq=%u; server says (%u,%u,%d) facing=%u\n",
        seq, x, y, static_cast<int>(z), dirByte & 0x07);

    // Server is authoritative — snap prediction back to its reported pose and
    // drop the whole in-flight queue (anything we predicted past the reject is
    // stale). Classic UO: seq resets to 0.
    playerX_ = static_cast<i32>(x);
    playerY_ = static_cast<i32>(y);
    playerZ_ = z;
    playerFacing_ = dirByte & 0x07;
    playerRunning_ = false;
    player_.x = playerX_;
    player_.y = playerY_;
    player_.z = playerZ_;
    player_.facing = playerFacing_;
    player_.running = playerRunning_;
    BotResetMovement();

    if (!nav_.bot.active) return;

    // The cell we were blocked from entering, and its surface z.
    i32 dx, dy;
    bot::DirToDelta(rdir, &dx, &dy);
    const i32 bx = playerX_ + dx;
    const i32 by = playerY_ + dy;
    i32 bz = playerZ_;
    if (world_) {
        const world::WalkQuery q = MakeWalkQuery(bx, by, playerZ_);
        const auto r = world_->QueryCell(q);
        if (r.walkable) bz = r.standZ;
    }

    // Track repeated bumps at the same cell (and the floor we're on).
    // (0) Stamina: a reject right after a "too fatigued" message is not an
    // obstacle — we're just spent. Wait for regen and retry; never blacklist.
    const bool fatigued = nav_.lastFatigueMs != 0 &&
                          (NowMs() - nav_.lastFatigueMs) < kFatigueWindowMs;

    // (1) Is a mobile standing on the blocked cell? Walking into one is a
    // shove (succeeds once rested), so a reject there is a moving/stamina
    // obstacle, never a wall. Wait and retry; never blacklist.
    const MobileObj* mob = FindMobileAt(bx, by, static_cast<i8>(bz));

    if (fatigued || mob) {
        const bool blockedByMobile = (mob != nullptr) && !fatigued;
        if (++nav_.bot.stuckWaits > kMaxStuckWaits) {
            LogWarn(
                "[bot] (%d,%d) blocked by %s for too long; stopping (not blacklisted)\n",
                bx, by, fatigued ? "fatigue" : "a mobile");
            BotAbortPath("blocked too long");
            return;
        }
        if (blockedByMobile && nav_.bot.stuckWaits >= kMobileRepathAfter) {
            // Don't stall indefinitely behind another mover: avoid this cell
            // for this trip and rebuild a full route around it.
            nav_.bot.blacklist.AddTransient(bx, by, bz, 0);
            LogInfo("[bot] mobile still blocks (%d,%d) after %u waits; rerouting now\n",
                        bx, by, nav_.bot.stuckWaits);
            nav_.bot.stuckWaits = 0;
            if (BotReplanToGoal())
                nav_.bot.resumeAtMs = NowMs() + 150;
            return;
        }
        const i64 wait = fatigued ? kStaminaWaitMs : kMobileWaitMs;
        LogInfo("[bot] reject at (%d,%d): %s — waiting %lldms, retrying (%u) [no blacklist]\n",
                    bx, by, fatigued ? "fatigued (stamina)" : "mobile in the way",
                    static_cast<long long>(wait), nav_.bot.stuckWaits);
        if (BotReplanToGoal())
            nav_.bot.resumeAtMs = NowMs() + wait;
        return;
    }

    if (BotIsDynamicItemBlocking(bx, by, static_cast<i8>(bz))) {
        LogInfo("[bot] dynamic item blocks (%d,%d,%d); rerouting\n",
                    bx, by, static_cast<int>(bz));
        if (BotReplanToGoal())
            nav_.bot.resumeAtMs = NowMs() + 150;
        return;
    }

    // Door handling uses the official macro: no door serial/static lookup.
    // On the first reject of an edge, face is already set by the server's
    // reject packet, so try OpenDoor once and then retry the same step. If
    // the same edge rejects again, treat it as non-door geometry and reroute.
    if (rejectedWasStep &&
        !BotDoorRetryWasTried(playerX_, playerY_, playerZ_,
                              bx, by, static_cast<i8>(bz))) {
        BotRememberDoorRetry(playerX_, playerY_, playerZ_,
                             bx, by, static_cast<i8>(bz));
        u8 ob[8];
        Send(ob, build::OpenDoor(ob), "0x12 OpenDoor (0x58 reject retry)");
        nav_.bot.path.push_front(rdir);
        nav_.bot.resumeAtMs = NowMs() + kDoorRetryWaitMs;
        LogInfo("[bot] step (%d,%d,%d)->(%d,%d,%d) rejected; OpenDoor + retry\n",
                    playerX_, playerY_, static_cast<int>(playerZ_),
                    bx, by, static_cast<int>(bz));
        return;
    }

    // A repeated reject means this exact directed step is not server-walkable
    // now. Keep the directed edge fact, and preserve the existing transient
    // cell block so A* does not immediately retry the same landing spot.
    nav_.bot.rejectedEdges.push_back({playerX_, playerY_, playerZ_,
                                      bx, by, static_cast<i8>(bz)});
    nav_.bot.blacklist.AddTransient(bx, by, bz, 0);
    LogInfo("[bot] step (%d,%d,%d)->(%d,%d,%d) rejected; avoiding edge + rerouting\n",
                playerX_, playerY_, static_cast<int>(playerZ_),
                bx, by, static_cast<int>(bz));
    std::uniform_int_distribution<int> rd(200, 400);
    if (BotReplanToGoal())
        nav_.bot.resumeAtMs = NowMs() + rd(nav_.rng);
}


// ---------------------------------------------------------------------------
// 0x22 Move Ack (3 bytes). Confirms the oldest in-flight move. Position was
// already advanced when we sent it (prediction), so the ack just frees a
// flight slot and lets the pipeline top up.
//   [0]    cmd
//   [1]    sequence (echoed)
//   [2]    notoriety
// ---------------------------------------------------------------------------
void Client::OnMoveAck(const u8* data, usize size) {
    if (size < 3) return;
    const u8 seq = data[1];
    if (nav_.movement.pending.empty()) {
        LogWarn( "[0x22] unsolicited ack seq=%u\n", seq);
        return;
    }
    const navigation::PendingMove pm = nav_.movement.pending.front();
    nav_.movement.pending.pop_front();
    if (pm.seq != seq) {
        // Acks should arrive in send order; a mismatch means we lost sync.
        LogWarn( "[0x22] ack seq=%u, expected %u — resyncing\n",
                     seq, pm.seq);
    }
    // Top up the pipeline immediately rather than waiting for the next tick.
    BotPumpMoves();
}


// ---------------------------------------------------------------------------
// Bot — A* + step pump
// ---------------------------------------------------------------------------
// Classic UO sequence: starts at 0, wraps 0xFF -> 1 (0 reserved for resync).
u8 Client::NextSeq() {
    u8 s = nav_.movement.moveSeq;
    nav_.movement.moveSeq = (nav_.movement.moveSeq == 0xFF) ? 1 : (nav_.movement.moveSeq + 1);
    return s;
}


bool Client::EnsureWorldLoaded() {
    if (worldLoaded_) return true;
    if (!cfg_.tiledataPath || !cfg_.mapPath ||
        !cfg_.staidxPath   || !cfg_.staticsPath) {
        LogWarn(
            "[bot] MUL paths not configured; goto disabled\n");
        return false;
    }
    tileData_ = std::make_unique<tiledata::TileDataLoader>();
    if (!tileData_->Load(cfg_.tiledataPath)) {
        tileData_.reset();
        return false;
    }
    worldMap_ = std::make_unique<map::Map>();
    if (!worldMap_->Open(cfg_.mapPath, cfg_.staidxPath, cfg_.staticsPath,
                         map::kBritWidthBlocks, map::kBritHeightBlocks,
                         cfg_.verdataPath)) {
        worldMap_.reset();
        tileData_.reset();
        return false;
    }
    world_ = std::make_unique<world::World>(*tileData_, *worldMap_);
    world_->SetAcceptDoors(cfg_.acceptDoors);
    worldLoaded_ = true;

    // Learned static blocks persist in blacklist.mul (verdata format),
    // layered on top of the base statics. Load them so A* avoids known bad
    // tiles from the first step.
    nav_.bot.blacklist.Load("blacklist.mul", worldMap_->HeightBlocks());
    LogInfo("[bot] world data loaded (%zu blacklisted spot(s)).\n",
                nav_.bot.blacklist.PersistentCount());
    return true;
}


void Client::BotPredictStep(u8 dir) {
    i32 dx, dy;
    bot::DirToDelta(dir, &dx, &dy);
    playerX_ += dx;
    playerY_ += dy;
    // Track the surface z so the next step's walk-check uses the right base.
    if (world_) {
        const world::WalkQuery q = MakeWalkQuery(playerX_, playerY_, playerZ_);
        const auto r = world_->QueryCell(q);
        if (r.walkable) playerZ_ = r.standZ;
    }
    player_.x = playerX_;
    player_.y = playerY_;
    player_.z = playerZ_;
    player_.facing = static_cast<u8>(dir & 0x07);
    player_.running = nav_.movement.run;
}


u32 Client::BotMoveGapMs() const {
    return nav_.movement.run ? kRunThrottleMs : kWalkThrottleMs;
}


bool Client::BotIsMobileBlocking(i32 x, i32 y, i8 z) const {
    const i64 now = NowMs();
    for (const auto& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        if (m.x != x || m.y != y) continue;
        if (now - m.seenMs > kLookaheadMobileFreshMs) continue;
        if (AbsDiff(z, m.z) <= 8) return true;
    }
    return false;
}


bool Client::BotIsDynamicItemBlocking(i32 x, i32 y, i8 z) const {
    if (!tileData_ || !world_) return false;
    const i32 colLo = static_cast<i32>(z);
    const i32 colHi = colLo + 16;
    for (const auto& kv : items_) {
        const ItemObj& it = kv.second;
        if (it.x != x || it.y != y) continue;

        const u16 gid = static_cast<u16>(it.itemId + it.gfxOffset);
        const auto& st = tileData_->Static(gid);
        if (IsDoorGraphic(gid) || (st.flags & tiledata::kFlagDoor)) continue;
        const bool blocksMovement =
            world_->IsStaticBlocker(gid) ||
            ((st.flags & tiledata::kFlagSurface) != 0) ||
            st.height != 0;
        if (!blocksMovement) continue;

        const i32 obsLo = static_cast<i32>(it.z);
        const i32 obsHi = obsLo + (st.height ? st.height : 1);
        // Dynamic server items are not part of World::QueryCell's standing
        // surface stack, so a table top at exactly standZ must still block the
        // cell. Otherwise A* tries to "stand" on the table's top face and the
        // server rejects the move.
        if (obsHi >= colLo && obsLo < colHi) return true;
    }
    return false;
}


bool Client::BotStepNeedsDoorOpen(i8 fromZ, i32 toX, i32 toY, i8 toZ) const {
    if (!tileData_ || !world_) return false;

    const i32 colLo = (fromZ > toZ) ? fromZ : toZ;
    const i32 colHi = colLo + 16;
    for (const auto& kv : items_) {
        const ItemObj& it = kv.second;
        if (it.x != toX || it.y != toY) continue;

        const u16 gid = static_cast<u16>(it.itemId + it.gfxOffset);
        const auto& st = tileData_->Static(gid);
        if (!IsDoorGraphic(gid) && (st.flags & tiledata::kFlagDoor) == 0)
            continue;

        const i32 obsLo = static_cast<i32>(it.z);
        const i32 obsHi = obsLo + (st.height ? st.height : 1);
        if (obsHi > colLo && obsLo < colHi) return true;
    }

    return world_->HasDoorAt(static_cast<u32>(toX), static_cast<u32>(toY),
                             fromZ, toZ);
}


bool Client::BotIsRuntimeBlocked(i32 x, i32 y, i8 z) const {
    if (nav_.bot.blacklist.IsBlocked(x, y, z)) return true;
    if (BotIsMobileBlocking(x, y, z)) return true;
    return BotIsDynamicItemBlocking(x, y, z);
}


bool Client::BotIsRejectedEdge(i32 fromX, i32 fromY, i8 fromZ,
                               i32 toX, i32 toY, i8 toZ) const {
    for (const auto& e : nav_.bot.rejectedEdges) {
        if (SameDirectedEdgeNearZ(e.fromX, e.fromY, e.fromZ, e.toX, e.toY, e.toZ,
                                  fromX, fromY, fromZ, toX, toY, toZ)) {
            return true;
        }
    }
    return false;
}


bool Client::BotDoorRetryWasTried(i32 fromX, i32 fromY, i8 fromZ,
                                  i32 toX, i32 toY, i8 toZ) const {
    for (const auto& e : nav_.bot.doorRetryEdges) {
        if (SameDirectedEdgeNearZ(e.fromX, e.fromY, e.fromZ, e.toX, e.toY, e.toZ,
                                  fromX, fromY, fromZ, toX, toY, toZ)) {
            return true;
        }
    }
    return false;
}


void Client::BotRememberDoorRetry(i32 fromX, i32 fromY, i8 fromZ,
                                  i32 toX, i32 toY, i8 toZ) {
    nav_.bot.doorRetryEdges.push_back({fromX, fromY, fromZ, toX, toY, toZ});
}


bool Client::BotRuntimeBlockedForPath(i32 x, i32 y, i8 z, void* user) {
    const auto* c = static_cast<const Client*>(user);
    return c && (c->BotIsMobileBlocking(x, y, z) ||
                 c->BotIsDynamicItemBlocking(x, y, z));
}


bool Client::BotRuntimeBlockedStepForPath(i32 fromX, i32 fromY, i8 fromZ,
                                          i32 toX, i32 toY, i8 toZ,
                                          void* user) {
    const auto* c = static_cast<const Client*>(user);
    return c && c->BotIsRejectedEdge(fromX, fromY, fromZ, toX, toY, toZ);
}


bool Client::BotLookaheadPatchPath() {
    if (!world_ || nav_.bot.path.empty()) return false;

    struct PreviewStep {
        i32 x;
        i32 y;
        i8  z;
        bool walkable;
        bool blocked;
    };

    PreviewStep preview[kPathLookaheadScanSteps + kPathLookaheadAnchorExtra];
    usize previewCount = 0;
    usize firstBlocked = kNoPreviewStep;
    i32 x = playerX_;
    i32 y = playerY_;
    i8 z = playerZ_;

    const usize maxPreview = std::min<usize>(
        nav_.bot.path.size(), kPathLookaheadScanSteps + kPathLookaheadAnchorExtra);
    for (usize i = 0; i < maxPreview; ++i) {
        i32 dx, dy;
        bot::DirToDelta(nav_.bot.path[i], &dx, &dy);
        const i32 nx = x + dx;
        const i32 ny = y + dy;

        const world::WalkQuery q = MakeGoalAwareWalkQuery(
            nx, ny, z, nav_.bot.hasGoalZ, nav_.bot.goalX, nav_.bot.goalY,
            nav_.bot.goalZ);
        const auto wr = world_->QueryCell(q);

        bool blocked = true;
        bool walkable = wr.walkable;
        i8 standZ = z;
        if (wr.walkable) {
            standZ = wr.standZ;
            blocked = BotIsRuntimeBlocked(nx, ny, standZ) ||
                      BotIsRejectedEdge(x, y, z, nx, ny, standZ);
        }

        if (i < kPathLookaheadScanSteps && (blocked || !walkable) &&
            firstBlocked == kNoPreviewStep) {
            firstBlocked = i;
        }

        preview[previewCount++] = {nx, ny, standZ, walkable, blocked};
        x = nx;
        y = ny;
        z = standZ;
    }

    if (firstBlocked == kNoPreviewStep) return false;

    const usize firstAnchor = std::min(kPathLookaheadScanSteps - 1, previewCount - 1);
    for (usize anchor = firstAnchor; anchor < previewCount; ++anchor) {
        const auto& target = preview[anchor];
        if (!target.walkable || target.blocked) continue;

        bot::PathOptions opts;
        opts.blacklist = &nav_.bot.blacklist;
        opts.hasGoalZ = true;
        opts.goalZ = target.z;
        opts.maxNodesExpanded = kLookaheadMaxNodesExpanded;
        opts.extraBlocked = &Client::BotRuntimeBlockedForPath;
        opts.extraBlockedStep = &Client::BotRuntimeBlockedStepForPath;
        opts.extraBlockedUser = this;

        const auto t0 = std::chrono::steady_clock::now();
        auto patch = bot::FindPath(*world_, playerX_, playerY_, playerZ_,
                                   target.x, target.y, opts);
        const double searchUs = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - t0).count();
        if (patch.empty()) continue;

        std::deque<u8> next;
        for (u8 d : patch) next.push_back(d);
        for (usize i = anchor + 1; i < nav_.bot.path.size(); ++i)
            next.push_back(nav_.bot.path[i]);
        nav_.bot.path.swap(next);
        LogInfo("[bot] lookahead patched around block at step %zu: "
                    "anchor=%zu new segment=%zu path=%zu in %.1fus\n",
                    firstBlocked + 1, anchor + 1, patch.size(), nav_.bot.path.size(),
                    searchUs);
        return true;
    }

    return false;
}


bool Client::BotReplanToGoal() {
    if (!pathPlanner_) {
        LogWarn("[bot] path worker is unavailable; stopping\n");
        BotAbortPath("path worker unavailable");
        return false;
    }
    if (++nav_.bot.replanCount > kMaxReplans) {
        LogWarn( "[bot] giving up after %u replans (unreachable?)\n",
                     nav_.bot.replanCount);
        BotAbortPath("too many replans");
        return false;
    }
    // Scale the node-expansion budget with goal distance. The grass penalty
    // inflates step cost without the heuristic knowing, so on a long open
    // route A* loses its straight-line guidance and expands ~O(distance^2)
    // nodes — the fixed default cap then makes a reachable-but-far goal look
    // unreachable. Budget quadratically (with headroom for detours) but bound
    // it so a genuinely unreachable goal still fails in finite time/memory.
    const u64 cheb = static_cast<u64>(
        ChebyshevDistance(playerX_, playerY_, nav_.bot.goalX, nav_.bot.goalY));
    u64 budget = cheb * cheb * 4 + 65536;
    if (budget > 2000000) budget = 2000000;

    navigation::PathRequest request;
    request.requestId = nav_.bot.nextPlanRequestId++;
    if (request.requestId == 0) request.requestId = nav_.bot.nextPlanRequestId++;
    request.startX = playerX_;
    request.startY = playerY_;
    request.startZ = playerZ_;
    request.goalX = nav_.bot.goalX;
    request.goalY = nav_.bot.goalY;
    request.goalZ = nav_.bot.goalZ;
    request.hasGoalZ = nav_.bot.hasGoalZ;
    request.maxNodesExpanded = static_cast<u32>(budget);
    // For follow we want the shortest valid path to keep up with a moving
    // target; road/grass bias only makes us lag behind.
    request.grassPenalty = nav_.follow.active ? 0u : kGrassPenalty;
    request.foliagePenalty = nav_.follow.active ? 0u : kForestPenalty;
    request.playerSerial = playerSerial_;
    request.blacklist = nav_.bot.blacklist;
    request.rejectedEdges = nav_.bot.rejectedEdges;
    request.mobiles.reserve(mobileCache_.size());
    for (const auto& m : mobileCache_) {
        request.mobiles.push_back({m.serial, m.x, m.y, m.z, m.seenMs});
    }
    request.dynamicItems.reserve(items_.size());
    for (const auto& kv : items_) {
        const ItemObj& it = kv.second;
        request.dynamicItems.push_back({it.itemId, it.x, it.y, it.z, it.gfxOffset});
    }

    nav_.bot.path.clear();
    nav_.bot.planning = true;
    nav_.bot.planRequestId = request.requestId;
    pathPlanner_->Request(std::move(request));
    LogInfo("[bot] planning path to (%d,%d) in background\n",
                nav_.bot.goalX, nav_.bot.goalY);
    return true;
}


void Client::BotPollPathPlanner() {
    if (!pathPlanner_) return;

    navigation::PathResult result;
    if (!pathPlanner_->Poll(&result)) return;

    if (!nav_.bot.planning || result.requestId != nav_.bot.planRequestId) {
        return;
    }

    nav_.bot.planning = false;
    nav_.bot.planRequestId = 0;

    if (!result.worldReady) {
        LogWarn("[bot] path worker could not load MUL data; stopping\n");
        BotAbortPath("path worker unavailable");
        return;
    }

    if (result.path.empty()) {
        LogWarn(
            "[bot] no path to (%d,%d) avoiding %zu block(s); stopping "
            "(search %.1fus)\n",
            result.goalX, result.goalY, result.blacklistCount, result.searchUs);
        BotAbortPath("no path");
        return;
    }

    LogInfo("[bot] replan to (%d,%d): %zu steps in %.1fus\n",
                result.goalX, result.goalY, result.path.size(), result.searchUs);
    nav_.bot.path.assign(result.path.begin(), result.path.end());
    BotPumpMoves();
}


// Combat-interrupt hook. Called when we detect we're under attack mid-travel.
// For now it just halts the path safely so we don't blindly run on while being
// hit. TODO: per-policy reaction — engage (war + attack), flee (path away from
// the threat), or recall ("kal ort por"); see task #6.
void Client::BotInterruptForThreat(const char* reason) {
    LogWarn(
        "[bot] THREAT (%s): halting travel (TODO engage/flee/recall)\n",
        reason ? reason : "?");
    LogEvent("threat", reason ? reason : "");
    BotAbortPath(reason);
    nav_.follow.active = false;
    BotResetMovement();
}


void Client::BotNoteFatigueMessage() {
    nav_.lastFatigueMs = NowMs();
}


void Client::BotAbortPath(const char*) {
    nav_.bot.path.clear();
    nav_.bot.active = false;
    nav_.bot.planning = false;
    nav_.bot.planRequestId = 0;
}


void Client::BotResetMovement() {
    nav_.movement.pending.clear();
    nav_.movement.moveSeq = 0;
}


void Client::BotStopFollow(const char* reason) {
    if (!nav_.follow.active) return;
    LogInfo("[follow] stopped (%s)\n", reason ? reason : "off");
    nav_.follow.active = false;
    nav_.follow.serial = 0;
    nav_.follow.distance = 1;
    nav_.follow.lastReplanMs = 0;
    nav_.follow.lastProbeMs = 0;
    BotAbortPath(reason);
    BotResetMovement();
}


void Client::BotStartFollow(u32 serial, u32 followDistance) {
    if (!EnsureWorldLoaded()) return;
    nav_.follow.active = true;
    nav_.follow.serial = serial;
    nav_.follow.distance = followDistance ? followDistance : 1;
    nav_.follow.lastReplanMs = 0;
    nav_.follow.lastProbeMs = 0;
    BotAbortPath("follow start");
    BotResetMovement();
    const char* name = MobileName(serial);
    LogInfo("[follow] tracking %s0x%08X (distance=%u)\n",
                name ? name : "", serial, nav_.follow.distance);
    u8 pkt[8];
    const usize n = build::MobNameQuery(pkt, serial);
    Send(pkt, n, "0x98 AllNames (follow start)");
}


bool Client::ChooseFollowGoal(i32* gx, i32* gy, i8* gz) const {
    if (!nav_.follow.active || !gx || !gy || !gz || !world_) return false;
    const MobileObj* t = FindMobileBySerial(nav_.follow.serial);
    if (!t) return false;

    const u8 behind = static_cast<u8>((t->dir + 4) & 0x07);
    for (u8 rank = 0; rank < 8; ++rank) {
        const u8 d = (rank == 0) ? behind : static_cast<u8>((behind + rank) & 0x07);
        i32 dx, dy;
        bot::DirToDelta(d, &dx, &dy);
        const i32 tx = t->x + dx;
        const i32 ty = t->y + dy;
        if (FindMobileAt(tx, ty, t->z)) continue;

        // Follow must stick to the target's floor. Using our current z here
        // picks the wrong layer in multi-storey columns (e.g. target fell
        // from a second floor and we're still above).
        const world::WalkQuery q = MakeWalkQuery(tx, ty, t->z);
        const auto wr = world_->QueryCell(q);
        if (!wr.walkable) continue;

        *gx = tx;
        *gy = ty;
        *gz = wr.standZ;
        return true;
    }
    return false;
}


void Client::BotFollowTick() {
    if (!nav_.follow.active) return;
    const i64 now = NowMs();

    const MobileObj* t = FindMobileBySerial(nav_.follow.serial);
    if (!t) {
        if (now - nav_.follow.lastProbeMs >= kFollowProbeMs) {
            u8 pkt[8];
            const usize n = build::MobNameQuery(pkt, nav_.follow.serial);
            Send(pkt, n, "0x98 AllNames (follow probe)");
            nav_.follow.lastProbeMs = now;
            LogInfo("[follow] waiting for 0x%08X to appear in range\n",
                    nav_.follow.serial);
        }
        return;
    }

    const i32 dx = AbsDiff(playerX_, t->x);
    const i32 dy = AbsDiff(playerY_, t->y);
    const i32 dz = AbsDiff(playerZ_, t->z);
    if (dx <= static_cast<i32>(nav_.follow.distance) &&
        dy <= static_cast<i32>(nav_.follow.distance) && dz <= 8) {
        // Already inside follow radius: don't keep replanning. Let any
        // in-flight move settle first to avoid stop/start jitter.
        if (nav_.movement.pending.empty()) {
            BotAbortPath("inside follow radius");
        }
        return;
    }

    if (now - nav_.follow.lastReplanMs < kFollowReplanMinMs) return;
    i32 gx = 0, gy = 0;
    i8 gz = 0;
    if (!ChooseFollowGoal(&gx, &gy, &gz)) return;

    const bool goalChanged = (gx != nav_.bot.goalX || gy != nav_.bot.goalY ||
                              !nav_.bot.hasGoalZ ||
                              gz != static_cast<i8>(nav_.bot.goalZ));
    if (!goalChanged &&
        (nav_.bot.active || nav_.bot.planning || !nav_.movement.pending.empty())) {
        return;
    }

    nav_.bot.goalX = gx;
    nav_.bot.goalY = gy;
    nav_.bot.goalZ = gz;
    nav_.bot.hasGoalZ = true;
    nav_.bot.active = true;
    nav_.bot.planning = false;
    nav_.bot.replanCount = 0;
    nav_.follow.lastReplanMs = now;
    BotReplanToGoal();
    BotPumpMoves();
}


void Client::BotStartGoto(i32 tx, i32 ty, bool hasZ, i32 tz) {
    if (!EnsureWorldLoaded()) return;
    nav_.follow.active = false;
    if (!nav_.movement.pending.empty() || !nav_.bot.path.empty() || nav_.bot.planning) {
        LogWarn(
            "[bot] busy (inflight=%zu path=%zu planning=%s); type 'stop' first\n",
            nav_.movement.pending.size(), nav_.bot.path.size(),
            nav_.bot.planning ? "yes" : "no");
        return;
    }
    nav_.bot.goalX = tx;
    nav_.bot.goalY = ty;
    nav_.bot.goalZ = tz;
    nav_.bot.hasGoalZ = hasZ;
    nav_.bot.active = true;
    nav_.bot.planning = false;
    nav_.bot.replanCount = 0;
    nav_.bot.resumeAtMs = 0;
    nav_.bot.stuckWaits = 0;
    nav_.bot.blacklist.ClearTransient();
    nav_.bot.rejectedEdges.clear();
    nav_.bot.doorRetryEdges.clear();
    BotResetMovement();  // fresh fastwalk sequence (0 = resync)

    if (nav_.bot.hasGoalZ)
        LogInfo("[bot] %s from (%d,%d,%d) to (%d,%d,z%d)\n",
                    nav_.movement.run ? "running" : "walking",
                    playerX_, playerY_, static_cast<int>(playerZ_), tx, ty, tz);
    else
        LogInfo("[bot] %s from (%d,%d,%d) to (%d,%d)\n",
                    nav_.movement.run ? "running" : "walking",
                    playerX_, playerY_, static_cast<int>(playerZ_), tx, ty);
    BotReplanToGoal();
}


// Sends queued steps while a flight slot is free and the step cadence has
// elapsed. Each move is predicted immediately (pos for a step, facing for a
// turn) and reconciled later by 0x22 / 0x21.
void Client::BotPumpMoves() {
    if (!nav_.bot.active) return;
    if (nav_.bot.planning) return;

    const i64 now_ms = NowMs();
    if (now_ms < nav_.bot.resumeAtMs) return;  // human reaction pause after a bump
    const u32 needGap = nav_.movement.run ? kRunThrottleMs : kWalkThrottleMs;

    if (nav_.movement.pending.empty()) BotLookaheadPatchPath();

    while (nav_.movement.pending.size() < kMaxInFlight && !nav_.bot.path.empty()) {
        if (nav_.movement.lastMoveSentMs != 0 &&
            now_ms - nav_.movement.lastMoveSentMs < static_cast<i64>(needGap)) {
            return;  // enforce only minimum legal step gap, no random jitter
        }

        const u8 dir = nav_.bot.path.front();
        const bool wasStep = (dir == playerFacing_);
        if (wasStep) {
            i32 dx, dy;
            bot::DirToDelta(dir, &dx, &dy);
            const i32 nx = playerX_ + dx;
            const i32 ny = playerY_ + dy;

            const world::WalkQuery q = MakeGoalAwareWalkQuery(
                nx, ny, playerZ_, nav_.bot.hasGoalZ, nav_.bot.goalX,
                nav_.bot.goalY, nav_.bot.goalZ);
            const auto wr = world_->QueryCell(q);
            if (wr.walkable &&
                BotStepNeedsDoorOpen(playerZ_, nx, ny, wr.standZ) &&
                !BotDoorRetryWasTried(playerX_, playerY_, playerZ_,
                                      nx, ny, wr.standZ)) {
                if (!nav_.movement.pending.empty()) return;
                {
                    u8 ob[8];
                    const usize on = build::OpenDoor(ob);
                    Send(ob, on, "0x12 OpenDoor (0x58 lookahead)");
                    BotRememberDoorRetry(playerX_, playerY_, playerZ_,
                                         nx, ny, wr.standZ);
                    nav_.bot.resumeAtMs = now_ms + kDoorRetryWaitMs;
                    LogInfo("[bot] door ahead at (%d,%d,z%d); OpenDoor before step\n",
                            nx, ny, static_cast<int>(wr.standZ));
                }
                return;
            }
        }

        const u8 seq  = NextSeq();
        const u8 wire = nav_.movement.run ? static_cast<u8>(dir | 0x80) : dir;
        u8 buf[16];
        usize n = build::MoveRequest(buf, wire, seq, 0u, cfg_.legacyMovePacket);
        char note[72];
        std::snprintf(note, sizeof(note), "0x02 Move dir=%u seq=%u %s%s",
                      dir, seq, wasStep ? "step" : "turn",
                      nav_.movement.run ? " run" : "");
        if (!Send(buf, n, note)) {
            BotAbortPath("send failed");
            BotResetMovement();
            return;
        }

        nav_.movement.pending.push_back({seq, dir, wasStep, now_ms});
        nav_.movement.lastMoveSentMs = now_ms;

        if (wasStep) { nav_.bot.path.pop_front(); BotPredictStep(dir); }
        else {
            playerFacing_ = dir;  // turn: re-send same dir to step
            player_.facing = dir;
            player_.running = nav_.movement.run;
        }
    }

    if (nav_.bot.path.empty() && nav_.movement.pending.empty()) {
        nav_.bot.active = false;
        LogInfo("[bot] arrived at (%d,%d,%d)\n",
                    playerX_, playerY_, static_cast<int>(playerZ_));
    }
}


void Client::BotTick() {
    if (mobilesListPending_ && NowMs() >= mobilesListDeadlineMs_) {
        FlushPendingMobilesList();
    }
    if (nav_.follow.active) BotFollowTick();
    BotPollPathPlanner();
    if (!nav_.bot.active) return;
    if (nav_.bot.planning) return;
    if (!nav_.movement.pending.empty()) {
        // Watchdog: the oldest in-flight move should ack quickly. If it
        // never does, the move was silently dropped — abort the path.
        const i64 now_ms = NowMs();
        if (now_ms - nav_.movement.pending.front().sentMs >
                static_cast<i64>(kAckWatchdogMs)) {
            LogWarn(
                "[bot] watchdog: oldest move unacked %llds; aborting path\n",
                static_cast<long long>(
                    (now_ms - nav_.movement.pending.front().sentMs) / 1000));
            BotResetMovement();
            BotAbortPath("move ack watchdog");
            return;
        }
    }
    BotPumpMoves();
}

} // namespace uo
