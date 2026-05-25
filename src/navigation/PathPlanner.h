#pragma once

#include "navigation/NavigationState.h"
#include "uo/types.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace uo::tiledata { class TileDataLoader; }
namespace uo::map      { class Map; }
namespace uo::world    { class World; }

namespace uo::navigation {

struct PathPlannerConfig {
    std::string tiledataPath;
    std::string mapPath;
    std::string staidxPath;
    std::string staticsPath;
    std::string verdataPath;
    bool acceptDoors = false;
};

struct PathMobileBlocker {
    u32 serial = 0;
    i32 x = 0;
    i32 y = 0;
    i8  z = 0;
    i64 seenMs = 0;
};

struct PathDynamicItem {
    u16 itemId = 0;
    i32 x = 0;
    i32 y = 0;
    i8  z = 0;
    u8  gfxOffset = 0;
};

struct PathRequest {
    u64 requestId = 0;
    i32 startX = 0;
    i32 startY = 0;
    i8  startZ = 0;
    i32 goalX = 0;
    i32 goalY = 0;
    i32 goalZ = 0;
    bool hasGoalZ = false;
    u32 maxNodesExpanded = 32768;
    u32 grassPenalty = 0;
    u32 foliagePenalty = 0;
    u32 playerSerial = 0;
    bot::Blacklist blacklist;
    std::vector<RejectedEdge> rejectedEdges;
    std::vector<PathMobileBlocker> mobiles;
    std::vector<PathDynamicItem> dynamicItems;
};

struct PathResult {
    u64 requestId = 0;
    bool worldReady = false;
    bool goalWalkable = true;
    std::vector<u8> path;
    double searchUs = 0.0;
    usize blacklistCount = 0;
    i32 goalX = 0;
    i32 goalY = 0;
};

class PathPlanner {
public:
    explicit PathPlanner(PathPlannerConfig config);
    ~PathPlanner();

    PathPlanner(const PathPlanner&) = delete;
    PathPlanner& operator=(const PathPlanner&) = delete;

    void Request(PathRequest request);
    bool Poll(PathResult* out);

private:
    void WorkerLoop();
    bool EnsureWorldLoaded();

    PathPlannerConfig config_;
    std::unique_ptr<tiledata::TileDataLoader> tileData_;
    std::unique_ptr<map::Map> worldMap_;
    std::unique_ptr<world::World> world_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    bool stop_ = false;
    bool hasRequest_ = false;
    bool hasResult_ = false;
    PathRequest request_;
    PathResult result_;
};

} // namespace uo::navigation
