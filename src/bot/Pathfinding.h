#pragma once

#include "uo/types.h"

#include <utility>
#include <vector>

namespace uo::world { class World; }

namespace uo::bot {

class Blacklist;

// UO 8-direction movement.
//   0 = N   (0, -1)
//   1 = NE  (+1, -1)
//   2 = E   (+1, 0)
//   3 = SE  (+1, +1)
//   4 = S   (0, +1)
//   5 = SW  (-1, +1)
//   6 = W   (-1, 0)
//   7 = NW  (-1, -1)
void DirToDelta(u8 dir, i32* dx, i32* dy);

// Optional diagnostics filled by FindPath when PathOptions::stats is set.
// Lets a probe see how far the search got and whether the goal column was
// ever touched, without duplicating the A* loop.
struct PathStats {
    u32  expanded         = 0;          // nodes popped/expanded
    i32  closestX         = 0;          // node with the smallest heuristic
    i32  closestY         = 0;          //   distance to the goal that the
    i8   closestZ         = 0;          //   search actually reached
    u32  closestH         = 0xFFFFFFFFu;
    bool reachedGoalColumn = false;     // a neighbor at (gx,gy) was generated
};

struct PathOptions {
    u32 maxIterations = 8192*4; // Sure to be enought
    // Classic UO step rules: walking allows +2 over flat ground but
    // up to +12 onto stair-like Surface tiles. We use the loose upper
    // limit so A* can climb out of low pockets the server placed us
    // in via teleport / kick.
    u8  maxStepUp     = 12;
    u8  maxStepDown   = 12;
    u8  charHeight    = 16;
    // For very long paths, cap; otherwise A* may run unbounded if no
    // path exists.
    u32 maxNodesExpanded = 32768;
    // Extra impassable spots consulted AFTER the normal MUL walkability
    // checks (learned server rejections / dynamic obstacles). null = none.
    const Blacklist* blacklist = nullptr;
    // Extra step cost added when stepping onto open grass-like terrain, to
    // bias routes toward roads/dirt where mobs are sparser. 0 = no bias.
    u32 grassPenalty = 0;
    // Optional: filled with search diagnostics. null = no tracing.
    PathStats* stats = nullptr;
    // Pin the destination floor: when set, the goal is only satisfied at a
    // standing z within kGoalZTolerance of goalZ. Use for columns walkable at
    // several levels (e.g. ground vs. an upper storey). Unset = any reachable z.
    bool hasGoalZ = false;
    i32  goalZ    = 0;
};

// A* on the 8-connected grid using `world.QueryCell()` for walkability.
// Returns the list of directions (0..7) the bot should send. Empty
// vector means: no path found or start == goal.
std::vector<u8> FindPath(uo::world::World& world,
                         i32 sx, i32 sy, i8 sz,
                         i32 gx, i32 gy,
                         const PathOptions& opts = {});

}
