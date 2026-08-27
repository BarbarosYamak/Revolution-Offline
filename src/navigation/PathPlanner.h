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
    // MOBILES ARE SOFT OBSTACLES; terrain and furniture are hard.
    //
    // A cached mobile never expires -- deliberately, because a stationary one
    // never resends 0x77 and expiring it would blind A* to a real blocker. But
    // that also means a bystander who has since wandered off still walls a
    // doorway forever, which is how M3.7 lost a miner inside the Minoc bank.
    //
    // The resolution is not a timeout. It is that being WRONG about a mobile is
    // cheap: the server rejects one step and the existing reject/reroute path
    // handles it. Being wrong about a wall is not. So when a plan fails with
    // the character enclosed and mobiles among the walls, the planner retries
    // once with them ignored.
    bool ignoreMobiles = false;
};

// Why a search failed, computed only when it did.
//
// "no path, 23.7us" is not a diagnosis -- a search that expands almost nothing
// has usually been sealed in at the START, and the interesting question is by
// WHAT. M3.7 lost a run inside the Minoc bank to exactly this and could only
// guess at the cause, because the failure carried no evidence with it.
//
// Each counter is the number of the eight neighbours of the start cell rejected
// for that reason. openExits == 0 means the character is enclosed where it
// stands, and the remaining fields say who built the wall.
struct StartEnclosure {
    u8 openExits        = 0;   // neighbours A* could actually step to
    u8 terrainBlocked   = 0;   // rejected by the baked MUL/statics grid
    u8 mobileBlocked    = 0;   // a cached mobile stands there
    u8 dynamicBlocked   = 0;   // a dynamic (decorator) item blocks the column
    u8 doorBlocked      = 0;   // a closed door -- openable, not a wall
    u8 blacklistBlocked = 0;   // previously learned server rejection
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
    // Populated only on failure (empty path).
    bool hasEnclosure = false;
    StartEnclosure enclosure;
    usize dynamicItemCount = 0;
    usize mobileCount = 0;
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
