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

constexpr i64 kMobileFreshMs = 5000;
constexpr i32 kRejectedEdgeZTolerance = 2;
constexpr i32 kGoalZTolerance = 4;

bool IsDoorGraphic(u16 id) {
    return id >= 0x0675 && id <= 0x06F6;
}

i32 AbsDiff(i32 a, i32 b) {
    const i32 d = a - b;
    return d < 0 ? -d : d;
}

i64 NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
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
        if (NowMs() - m.seenMs > kMobileFreshMs) continue;
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
        const bool blocksMovement =
            overlay.world->IsStaticBlocker(gid) ||
            ((st.flags & tiledata::kFlagSurface) != 0) ||
            st.height != 0;
        if (!blocksMovement) continue;

        const i32 obsLo = static_cast<i32>(it.z);
        const i32 obsHi = obsLo + (st.height ? st.height : 1);
        if (obsHi >= colLo && obsLo < colHi) return true;
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
