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

namespace uo {

namespace {

// Depth-1 movement: this server's 0x22 ack carries no position, so deeper
// pipelines desync when a mid-flight move is rejected (we'd never learn where
// the surviving moves left us). One confirmed step at a time stays exact.
constexpr usize kMaxInFlight = 1;
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
    for (const auto& pm : pendingMoves_)
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
    pendingMoves_.clear();
    moveSeq_ = 0;

    if (!botActive_) return;

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
    const bool fatigued = lastFatigueMs_ != 0 &&
                          (NowMs() - lastFatigueMs_) < kFatigueWindowMs;

    // (1) Is a mobile standing on the blocked cell? Walking into one is a
    // shove (succeeds once rested), so a reject there is a moving/stamina
    // obstacle, never a wall. Wait and retry; never blacklist.
    const MobileObj* mob = FindMobileAt(bx, by, static_cast<i8>(bz));

    if (fatigued || mob) {
        const bool blockedByMobile = (mob != nullptr) && !fatigued;
        if (++stuckWaits_ > kMaxStuckWaits) {
            LogWarn(
                "[bot] (%d,%d) blocked by %s for too long; stopping (not blacklisted)\n",
                bx, by, fatigued ? "fatigue" : "a mobile");
            botPath_.clear();
            botActive_ = false;
            return;
        }
        if (blockedByMobile && stuckWaits_ >= kMobileRepathAfter) {
            // Don't stall indefinitely behind another mover: avoid this cell
            // for this trip and rebuild a full route around it.
            blacklist_.AddTransient(bx, by, bz, 0);
            LogInfo("[bot] mobile still blocks (%d,%d) after %u waits; rerouting now\n",
                        bx, by, stuckWaits_);
            stuckWaits_ = 0;
            if (BotReplanToGoal())
                botResumeAtMs_ = NowMs() + 150;
            return;
        }
        const i64 wait = fatigued ? kStaminaWaitMs : kMobileWaitMs;
        LogInfo("[bot] reject at (%d,%d): %s — waiting %lldms, retrying (%u) [no blacklist]\n",
                    bx, by, fatigued ? "fatigued (stamina)" : "mobile in the way",
                    static_cast<long long>(wait), stuckWaits_);
        if (BotReplanToGoal())
            botResumeAtMs_ = NowMs() + wait;
        return;
    }

    if (BotIsDynamicItemBlocking(bx, by, static_cast<i8>(bz))) {
        LogInfo("[bot] dynamic item blocks (%d,%d,%d); rerouting\n",
                    bx, by, static_cast<int>(bz));
        if (BotReplanToGoal())
            botResumeAtMs_ = NowMs() + 150;
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
        botPath_.push_front(rdir);
        botResumeAtMs_ = NowMs() + kDoorRetryWaitMs;
        LogInfo("[bot] step (%d,%d,%d)->(%d,%d,%d) rejected; OpenDoor + retry\n",
                    playerX_, playerY_, static_cast<int>(playerZ_),
                    bx, by, static_cast<int>(bz));
        return;
    }

    // A repeated reject means this exact directed step is not server-walkable
    // now. Keep the directed edge fact, and preserve the existing transient
    // cell block so A* does not immediately retry the same landing spot.
    rejectedEdges_.push_back({playerX_, playerY_, playerZ_,
                              bx, by, static_cast<i8>(bz)});
    blacklist_.AddTransient(bx, by, bz, 0);
    LogInfo("[bot] step (%d,%d,%d)->(%d,%d,%d) rejected; avoiding edge + rerouting\n",
                playerX_, playerY_, static_cast<int>(playerZ_),
                bx, by, static_cast<int>(bz));
    std::uniform_int_distribution<int> rd(200, 400);
    if (BotReplanToGoal())
        botResumeAtMs_ = NowMs() + rd(rng_);
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
    if (pendingMoves_.empty()) {
        LogWarn( "[0x22] unsolicited ack seq=%u\n", seq);
        return;
    }
    const PendingMove pm = pendingMoves_.front();
    pendingMoves_.pop_front();
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
    u8 s = moveSeq_;
    moveSeq_ = (moveSeq_ == 0xFF) ? 1 : (moveSeq_ + 1);
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
    blacklist_.Load("blacklist.mul", worldMap_->HeightBlocks());
    LogInfo("[bot] world data loaded (%zu blacklisted spot(s)).\n",
                blacklist_.PersistentCount());
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
    player_.running = botRun_;
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
    if (blacklist_.IsBlocked(x, y, z)) return true;
    if (BotIsMobileBlocking(x, y, z)) return true;
    return BotIsDynamicItemBlocking(x, y, z);
}


bool Client::BotIsRejectedEdge(i32 fromX, i32 fromY, i8 fromZ,
                               i32 toX, i32 toY, i8 toZ) const {
    for (const auto& e : rejectedEdges_) {
        if (SameDirectedEdgeNearZ(e.fromX, e.fromY, e.fromZ, e.toX, e.toY, e.toZ,
                                  fromX, fromY, fromZ, toX, toY, toZ)) {
            return true;
        }
    }
    return false;
}


bool Client::BotDoorRetryWasTried(i32 fromX, i32 fromY, i8 fromZ,
                                  i32 toX, i32 toY, i8 toZ) const {
    for (const auto& e : doorRetryEdges_) {
        if (SameDirectedEdgeNearZ(e.fromX, e.fromY, e.fromZ, e.toX, e.toY, e.toZ,
                                  fromX, fromY, fromZ, toX, toY, toZ)) {
            return true;
        }
    }
    return false;
}


void Client::BotRememberDoorRetry(i32 fromX, i32 fromY, i8 fromZ,
                                  i32 toX, i32 toY, i8 toZ) {
    doorRetryEdges_.push_back({fromX, fromY, fromZ, toX, toY, toZ});
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
    if (!world_ || botPath_.empty()) return false;

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
        botPath_.size(), kPathLookaheadScanSteps + kPathLookaheadAnchorExtra);
    for (usize i = 0; i < maxPreview; ++i) {
        i32 dx, dy;
        bot::DirToDelta(botPath_[i], &dx, &dy);
        const i32 nx = x + dx;
        const i32 ny = y + dy;

        const world::WalkQuery q = MakeGoalAwareWalkQuery(
            nx, ny, z, botHasGoalZ_, botGoalX_, botGoalY_, botGoalZ_);
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
        opts.blacklist = &blacklist_;
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
        for (usize i = anchor + 1; i < botPath_.size(); ++i)
            next.push_back(botPath_[i]);
        botPath_.swap(next);
        LogInfo("[bot] lookahead patched around block at step %zu: "
                    "anchor=%zu new segment=%zu path=%zu in %.1fus\n",
                    firstBlocked + 1, anchor + 1, patch.size(), botPath_.size(),
                    searchUs);
        return true;
    }

    return false;
}


bool Client::BotReplanToGoal() {
    if (++botReplanCount_ > kMaxReplans) {
        LogWarn( "[bot] giving up after %u replans (unreachable?)\n",
                     botReplanCount_);
        botPath_.clear();
        botActive_ = false;
        return false;
    }
    bot::PathOptions opts;
    opts.blacklist = &blacklist_;
    // For follow we want the shortest valid path to keep up with a moving
    // target; road/grass bias only makes us lag behind.
    opts.grassPenalty   = followActive_ ? 0u : kGrassPenalty;
    opts.foliagePenalty = followActive_ ? 0u : kForestPenalty;
    opts.hasGoalZ = botHasGoalZ_;       // pin destination floor when given
    opts.goalZ    = botGoalZ_;
    opts.extraBlocked = &Client::BotRuntimeBlockedForPath;
    opts.extraBlockedStep = &Client::BotRuntimeBlockedStepForPath;
    opts.extraBlockedUser = this;

    // Scale the node-expansion budget with goal distance. The grass penalty
    // inflates step cost without the heuristic knowing, so on a long open
    // route A* loses its straight-line guidance and expands ~O(distance^2)
    // nodes — the fixed default cap then makes a reachable-but-far goal look
    // unreachable. Budget quadratically (with headroom for detours) but bound
    // it so a genuinely unreachable goal still fails in finite time/memory.
    const u64 cheb = static_cast<u64>(
        ChebyshevDistance(playerX_, playerY_, botGoalX_, botGoalY_));
    u64 budget = cheb * cheb * 4 + 65536;
    if (budget > 2000000) budget = 2000000;
    opts.maxNodesExpanded = static_cast<u32>(budget);
    const auto t0 = std::chrono::steady_clock::now();
    auto path = bot::FindPath(*world_, playerX_, playerY_, playerZ_,
                              botGoalX_, botGoalY_, opts);
    const double searchUs = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - t0).count();
    if (path.empty()) {
        LogWarn(
            "[bot] no path to (%d,%d) avoiding %zu block(s); stopping "
            "(search %.1fus)\n",
            botGoalX_, botGoalY_, blacklist_.Count(), searchUs);
        botPath_.clear();
        botActive_ = false;
        return false;
    }
    LogInfo("[bot] replan to (%d,%d): %zu steps in %.1fus\n",
                botGoalX_, botGoalY_, path.size(), searchUs);
    botPath_.assign(path.begin(), path.end());
    return true;
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
    botPath_.clear();
    pendingMoves_.clear();
    botActive_ = false;
    followActive_ = false;
    moveSeq_ = 0;
}


void Client::BotStopFollow(const char* reason) {
    if (!followActive_) return;
    LogInfo("[follow] stopped (%s)\n", reason ? reason : "off");
    followActive_ = false;
    followSerial_ = 0;
    followDistance_ = 1;
    followLastReplanMs_ = 0;
    followLastProbeMs_ = 0;
    botPath_.clear();
    pendingMoves_.clear();
    botActive_ = false;
    moveSeq_ = 0;
}


void Client::BotStartFollow(u32 serial, u32 followDistance) {
    if (!EnsureWorldLoaded()) return;
    followActive_ = true;
    followSerial_ = serial;
    followDistance_ = followDistance ? followDistance : 1;
    followLastReplanMs_ = 0;
    followLastProbeMs_ = 0;
    botPath_.clear();
    pendingMoves_.clear();
    moveSeq_ = 0;
    botActive_ = false;
    const char* name = MobileName(serial);
    LogInfo("[follow] tracking %s0x%08X (distance=%u)\n",
                name ? name : "", serial, followDistance_);
    u8 pkt[8];
    const usize n = build::MobNameQuery(pkt, serial);
    Send(pkt, n, "0x98 AllNames (follow start)");
}


bool Client::ChooseFollowGoal(i32* gx, i32* gy, i8* gz) const {
    if (!followActive_ || !gx || !gy || !gz || !world_) return false;
    const MobileObj* t = FindMobileBySerial(followSerial_);
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
    if (!followActive_) return;
    const i64 now = NowMs();

    const MobileObj* t = FindMobileBySerial(followSerial_);
    if (!t) {
        if (now - followLastProbeMs_ >= kFollowProbeMs) {
            u8 pkt[8];
            const usize n = build::MobNameQuery(pkt, followSerial_);
            Send(pkt, n, "0x98 AllNames (follow probe)");
            followLastProbeMs_ = now;
            LogInfo("[follow] waiting for 0x%08X to appear in range\n", followSerial_);
        }
        return;
    }

    const i32 dx = AbsDiff(playerX_, t->x);
    const i32 dy = AbsDiff(playerY_, t->y);
    const i32 dz = AbsDiff(playerZ_, t->z);
    if (dx <= static_cast<i32>(followDistance_) &&
        dy <= static_cast<i32>(followDistance_) && dz <= 8) {
        // Already inside follow radius: don't keep replanning. Let any
        // in-flight move settle first to avoid stop/start jitter.
        if (pendingMoves_.empty()) {
            botPath_.clear();
            botActive_ = false;
        }
        return;
    }

    if (now - followLastReplanMs_ < kFollowReplanMinMs) return;
    i32 gx = 0, gy = 0;
    i8 gz = 0;
    if (!ChooseFollowGoal(&gx, &gy, &gz)) return;

    const bool goalChanged = (gx != botGoalX_ || gy != botGoalY_ ||
                              !botHasGoalZ_ || gz != static_cast<i8>(botGoalZ_));
    if (!goalChanged && (botActive_ || !pendingMoves_.empty())) return;

    botGoalX_ = gx;
    botGoalY_ = gy;
    botGoalZ_ = gz;
    botHasGoalZ_ = true;
    botActive_ = true;
    botReplanCount_ = 0;
    followLastReplanMs_ = now;
    BotReplanToGoal();
    BotPumpMoves();
}


void Client::BotStartGoto(i32 tx, i32 ty, bool hasZ, i32 tz) {
    if (!EnsureWorldLoaded()) return;
    followActive_ = false;
    if (!pendingMoves_.empty() || !botPath_.empty()) {
        LogWarn(
            "[bot] busy (inflight=%zu path=%zu); type 'stop' first\n",
            pendingMoves_.size(), botPath_.size());
        return;
    }
    botGoalX_ = tx;
    botGoalY_ = ty;
    botGoalZ_ = tz;
    botHasGoalZ_ = hasZ;
    botActive_ = true;
    botReplanCount_ = 0;
    botResumeAtMs_ = 0;
    stuckWaits_ = 0;
    blacklist_.ClearTransient();
    rejectedEdges_.clear();
    doorRetryEdges_.clear();
    moveSeq_ = 0;  // fresh fastwalk sequence (0 = resync)

    if (botHasGoalZ_)
        LogInfo("[bot] %s from (%d,%d,%d) to (%d,%d,z%d)\n",
                    botRun_ ? "running" : "walking",
                    playerX_, playerY_, static_cast<int>(playerZ_), tx, ty, tz);
    else
        LogInfo("[bot] %s from (%d,%d,%d) to (%d,%d)\n",
                    botRun_ ? "running" : "walking",
                    playerX_, playerY_, static_cast<int>(playerZ_), tx, ty);
    if (!BotReplanToGoal()) return;
    LogInfo("[bot] path: %zu steps\n", botPath_.size());
    BotPumpMoves();
}


// Sends queued steps while a flight slot is free and the step cadence has
// elapsed. Each move is predicted immediately (pos for a step, facing for a
// turn) and reconciled later by 0x22 / 0x21.
void Client::BotPumpMoves() {
    if (!botActive_) return;

    const i64 now_ms = NowMs();
    if (now_ms < botResumeAtMs_) return;  // human reaction pause after a bump
    const u32 needGap = botRun_ ? runThrottleMs_ : walkThrottleMs_;

    if (pendingMoves_.empty()) BotLookaheadPatchPath();

    while (pendingMoves_.size() < kMaxInFlight && !botPath_.empty()) {
        if (lastMoveSentMs_ != 0 &&
            now_ms - lastMoveSentMs_ < static_cast<i64>(needGap)) {
            return;  // enforce only minimum legal step gap, no random jitter
        }

        const u8 dir = botPath_.front();
        const bool wasStep = (dir == playerFacing_);
        if (wasStep) {
            i32 dx, dy;
            bot::DirToDelta(dir, &dx, &dy);
            const i32 nx = playerX_ + dx;
            const i32 ny = playerY_ + dy;

            const world::WalkQuery q = MakeGoalAwareWalkQuery(
                nx, ny, playerZ_, botHasGoalZ_, botGoalX_, botGoalY_, botGoalZ_);
            const auto wr = world_->QueryCell(q);
            if (wr.walkable &&
                BotStepNeedsDoorOpen(playerZ_, nx, ny, wr.standZ) &&
                !BotDoorRetryWasTried(playerX_, playerY_, playerZ_,
                                      nx, ny, wr.standZ)) {
                if (!pendingMoves_.empty()) return;
                {
                    u8 ob[8];
                    const usize on = build::OpenDoor(ob);
                    Send(ob, on, "0x12 OpenDoor (0x58 lookahead)");
                    BotRememberDoorRetry(playerX_, playerY_, playerZ_,
                                         nx, ny, wr.standZ);
                    botResumeAtMs_ = now_ms + kDoorRetryWaitMs;
                    LogInfo("[bot] door ahead at (%d,%d,z%d); OpenDoor before step\n",
                            nx, ny, static_cast<int>(wr.standZ));
                }
                return;
            }
        }

        const u8 seq  = NextSeq();
        const u8 wire = botRun_ ? static_cast<u8>(dir | 0x80) : dir;
        u8 buf[16];
        usize n = build::MoveRequest(buf, wire, seq, 0u, cfg_.legacyMovePacket);
        char note[72];
        std::snprintf(note, sizeof(note), "0x02 Move dir=%u seq=%u %s%s",
                      dir, seq, wasStep ? "step" : "turn",
                      botRun_ ? " run" : "");
        if (!Send(buf, n, note)) {
            botActive_ = false; botPath_.clear(); pendingMoves_.clear();
            return;
        }

        pendingMoves_.push_back({seq, dir, wasStep, now_ms});
        lastMoveSentMs_ = now_ms;
        ++movesSinceClick_;

        if (wasStep) { botPath_.pop_front(); BotPredictStep(dir); }
        else {
            playerFacing_ = dir;  // turn: re-send same dir to step
            player_.facing = dir;
            player_.running = botRun_;
        }
    }

    if (botPath_.empty() && pendingMoves_.empty()) {
        botActive_ = false;
        LogInfo("[bot] arrived at (%d,%d,%d)\n",
                    playerX_, playerY_, static_cast<int>(playerZ_));
    }
}


void Client::BotTick() {
    if (mobilesListPending_ && NowMs() >= mobilesListDeadlineMs_) {
        FlushPendingMobilesList();
    }
    if (followActive_) BotFollowTick();
    if (!botActive_) return;
    if (!pendingMoves_.empty()) {
        // Watchdog: the oldest in-flight move should ack quickly. If it
        // never does, the move was silently dropped — abort the path.
        const i64 now_ms = NowMs();
        if (now_ms - pendingMoves_.front().sentMs >
                static_cast<i64>(ackWatchdogMs_)) {
            LogWarn(
                "[bot] watchdog: oldest move unacked %llds; aborting path\n",
                static_cast<long long>(
                    (now_ms - pendingMoves_.front().sentMs) / 1000));
            pendingMoves_.clear();
            moveSeq_ = 0;
            botPath_.clear();
            botActive_ = false;
            return;
        }
    }
    BotPumpMoves();
}

} // namespace uo
