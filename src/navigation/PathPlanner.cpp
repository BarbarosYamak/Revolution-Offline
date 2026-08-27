#include "navigation/PathPlanner.h"

#include "bot/Pathfinding.h"
#include "uo/map.h"
#include "uo/tiledata.h"
#include "uo/world.h"

#include <chrono>
#include <cstdio>
#include <utility>

namespace uo::navigation {

namespace {

constexpr i32 kRejectedEdgeZTolerance = 2;
constexpr i32 kGoalZTolerance = 4;

bool IsDoorGraphic(u16 id) {
    return id >= 0x0675 && id <= 0x06F6;
}

i32 AbsDiff(i32 a, i32 b) {
    const i32 d = a - b;
    return d < 0 ? -d : d;
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

struct RuntimeOverlay {
    const tiledata::TileDataLoader* tileData = nullptr;
    const world::World* world = nullptr;
    const PathRequest* request = nullptr;
};

bool IsMobileBlocking(const PathRequest& request, i32 x, i32 y, i8 z) {
    for (const auto& m : request.mobiles) {
        if (m.serial == request.playerSerial) continue;
        if (m.x != x || m.y != y) continue;
        // No time-freshness: a mobile in the cache is in range (PurgeOutOfRange
        // culls by range, 0x1D on despawn), and a stationary one never resends
        // 0x77 — so a stale seenMs does NOT mean it left. Expiring it here blinds
        // A* to a present blocker the server still rejects us into.
        if (AbsDiff(z, m.z) <= 8) return true;
    }
    return false;
}

bool IsDynamicItemBlocking(const RuntimeOverlay& overlay, i32 x, i32 y, i8 z) {
    if (!overlay.tileData || !overlay.world || !overlay.request) return false;

    const i32 colLo = static_cast<i32>(z);
    const i32 colHi = colLo + 16;
    for (const auto& it : overlay.request->dynamicItems) {
        if (it.x != x || it.y != y) continue;

        const u16 gid = static_cast<u16>(it.itemId + it.gfxOffset);
        const auto& st = overlay.tileData->Static(gid);
        if (IsDoorGraphic(gid) || (st.flags & tiledata::kFlagDoor)) continue;
        const bool isSurface =
            (st.flags & (tiledata::kFlagSurface | tiledata::kFlagBridge)) != 0;
        const bool blocksMovement =
            overlay.world->IsStaticBlocker(gid) ||
            isSurface;
        if (!blocksMovement) continue;

        const i32 obsLo = static_cast<i32>(it.z);
        const i32 obsHi = obsLo +
            (isSurface ? st.height : (st.height ? st.height : 1));
        if (obsHi > colLo && obsLo < colHi) return true;
    }
    return false;
}

bool IsClosedDoorAt(const RuntimeOverlay& overlay, i32 x, i32 y, i8 z) {
    if (!overlay.tileData || !overlay.request) return false;
    const i32 colLo = static_cast<i32>(z);
    const i32 colHi = colLo + 16;
    for (const auto& it : overlay.request->dynamicItems) {
        if (it.x != x || it.y != y) continue;
        const u16 gid = static_cast<u16>(it.itemId + it.gfxOffset);
        const auto& st = overlay.tileData->Static(gid);
        if (!IsDoorGraphic(gid) && (st.flags & tiledata::kFlagDoor) == 0) continue;
        const i32 obsLo = static_cast<i32>(it.z);
        const i32 obsHi = obsLo + (st.height ? st.height : 1);
        if (obsHi > colLo && obsLo < colHi) return true;
    }
    return false;
}

bool ExtraBlocked(i32 x, i32 y, i8 z, void* user) {
    const auto* overlay = static_cast<const RuntimeOverlay*>(user);
    if (!overlay || !overlay->request) return false;
    return IsMobileBlocking(*overlay->request, x, y, z) ||
           IsDynamicItemBlocking(*overlay, x, y, z);
}

bool ExtraBlockedStep(i32 fromX, i32 fromY, i8 fromZ,
                      i32 toX, i32 toY, i8 toZ,
                      void* user) {
    const auto* overlay = static_cast<const RuntimeOverlay*>(user);
    if (!overlay || !overlay->request) return false;
    for (const auto& e : overlay->request->rejectedEdges) {
        if (SameDirectedEdgeNearZ(e.fromX, e.fromY, e.fromZ, e.toX, e.toY, e.toZ,
                                  fromX, fromY, fromZ, toX, toY, toZ)) {
            return true;
        }
    }
    return false;
}

// Classify each of the eight neighbours of the start cell. Runs only when a
// search has already failed, so its cost never touches a successful plan.
//
// The order of the checks matters and mirrors A*'s own: terrain first (the
// baked grid), then the runtime overlays. A cell rejected by terrain is not
// then asked whether a chair is standing on it -- the answer would be true of
// half the world and would say nothing about why the bot is stuck.
//
// Doors are counted SEPARATELY and are not walls: BotStepNeedsDoorOpen can
// open one. A start whose only exits are doors is a bot that needs to knock,
// not a bot that is trapped.
StartEnclosure ClassifyStartEnclosure(const world::World& world,
                                      const RuntimeOverlay& overlay,
                                      const PathRequest& request) {
    StartEnclosure e{};
    for (u8 dir = 0; dir < 8; ++dir) {
        i32 dx = 0, dy = 0;
        bot::DirToDelta(dir, &dx, &dy);
        const i32 nx = request.startX + dx;
        const i32 ny = request.startY + dy;
        if (nx < 0 || ny < 0) { ++e.terrainBlocked; continue; }

        world::WalkQuery q{};
        q.x = static_cast<u32>(nx);
        q.y = static_cast<u32>(ny);
        q.fromZ = request.startZ;
        q.maxStepUp = 12;
        q.maxStepDown = 12;
        const auto cell = world.QueryCell(q);
        if (!cell.walkable) { ++e.terrainBlocked; continue; }

        const i8 nz = cell.standZ;
        if (request.blacklist.IsBlocked(nx, ny, nz)) { ++e.blacklistBlocked; continue; }
        if (IsMobileBlocking(request, nx, ny, nz)) { ++e.mobileBlocked; continue; }
        if (IsDynamicItemBlocking(overlay, nx, ny, nz)) { ++e.dynamicBlocked; continue; }
        if (IsClosedDoorAt(overlay, nx, ny, nz)) { ++e.doorBlocked; continue; }
        ++e.openExits;
    }
    return e;
}

bool GoalColumnIsWalkable(const world::World& world, const PathRequest& request) {
    world::WalkQuery q{};
    q.x = static_cast<u32>(request.goalX);
    q.y = static_cast<u32>(request.goalY);
    q.fromZ = static_cast<i8>(request.hasGoalZ ? request.goalZ : request.startZ);
    q.maxStepUp = 127;
    q.maxStepDown = 127;
    q.hasPreferredZ = request.hasGoalZ;
    q.preferredZ = static_cast<i8>(request.goalZ);
    const auto result = world.QueryCell(q);
    if (!result.walkable) return false;
    if (!request.hasGoalZ) return true;
    return AbsDiff(static_cast<i32>(result.standZ), request.goalZ) <= kGoalZTolerance;
}

}

PathPlanner::PathPlanner(PathPlannerConfig config)
    : config_(std::move(config)),
      worker_(&PathPlanner::WorkerLoop, this) {
}

PathPlanner::~PathPlanner() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void PathPlanner::Request(PathRequest request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request_ = std::move(request);
        hasRequest_ = true;
    }
    cv_.notify_one();
}

bool PathPlanner::Poll(PathResult* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasResult_) return false;
    *out = std::move(result_);
    hasResult_ = false;
    return true;
}

bool PathPlanner::EnsureWorldLoaded() {
    if (world_) return true;
    if (config_.tiledataPath.empty() || config_.mapPath.empty() ||
        config_.staidxPath.empty() || config_.staticsPath.empty()) {
        return false;
    }

    tileData_ = std::make_unique<tiledata::TileDataLoader>();
    if (!tileData_->Load(config_.tiledataPath.c_str())) {
        tileData_.reset();
        return false;
    }

    worldMap_ = std::make_unique<map::Map>();
    const char* verdataPath = config_.verdataPath.empty()
        ? nullptr
        : config_.verdataPath.c_str();
    if (!worldMap_->Open(config_.mapPath.c_str(),
                         config_.staidxPath.c_str(),
                         config_.staticsPath.c_str(),
                         map::kBritWidthBlocks,
                         map::kBritHeightBlocks,
                         verdataPath)) {
        worldMap_.reset();
        tileData_.reset();
        return false;
    }

    world_ = std::make_unique<world::World>(*tileData_, *worldMap_);
    world_->SetAcceptDoors(config_.acceptDoors);
    return true;
}

void PathPlanner::WorkerLoop() {
    for (;;) {
        PathRequest request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_ || hasRequest_; });
            if (stop_) return;
            request = std::move(request_);
            hasRequest_ = false;
        }

        PathResult result;
        result.requestId = request.requestId;
        result.blacklistCount = request.blacklist.Count();
        result.goalX = request.goalX;
        result.goalY = request.goalY;

        result.worldReady = EnsureWorldLoaded();
        if (result.worldReady) {
            if (!GoalColumnIsWalkable(*world_, request)) {
                result.goalWalkable = false;
            } else {
                bot::PathOptions opts;
                opts.blacklist = &request.blacklist;
                opts.grassPenalty = request.grassPenalty;
                opts.foliagePenalty = request.foliagePenalty;
                opts.hasGoalZ = request.hasGoalZ;
                opts.goalZ = request.goalZ;
                opts.maxNodesExpanded = request.maxNodesExpanded;

                RuntimeOverlay overlay;
                overlay.tileData = tileData_.get();
                overlay.world = world_.get();
                overlay.request = &request;
                opts.extraBlocked = &ExtraBlocked;
                opts.extraBlockedStep = &ExtraBlockedStep;
                opts.extraBlockedUser = &overlay;

                const auto t0 = std::chrono::steady_clock::now();
                result.path = bot::FindPath(*world_,
                                            request.startX, request.startY, request.startZ,
                                            request.goalX, request.goalY, opts);
                result.searchUs = std::chrono::duration<double, std::micro>(
                    std::chrono::steady_clock::now() - t0).count();

                // Only on failure, and only then: a successful plan pays
                // nothing for this.
                if (result.path.empty()) {
                    result.hasEnclosure = true;
                    result.enclosure =
                        ClassifyStartEnclosure(*world_, overlay, request);
                    result.dynamicItemCount = request.dynamicItems.size();
                    result.mobileCount = request.mobiles.size();
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ = std::move(result);
            hasResult_ = true;
        }
    }
}

} // namespace uo::navigation
