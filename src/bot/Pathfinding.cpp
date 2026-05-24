#include "bot/Pathfinding.h"

#include "bot/Blacklist.h"
#include "uo/world.h"

#include <queue>
#include <unordered_map>
#include <vector>

namespace uo::bot {

void DirToDelta(u8 dir, i32* dx, i32* dy) {
    static const i32 dxs[8] = { 0, +1, +1, +1,  0, -1, -1, -1 };
    static const i32 dys[8] = {-1, -1,  0, +1, +1, +1,  0, -1 };
    *dx = dxs[dir & 7];
    *dy = dys[dir & 7];
}

namespace {

struct Node {
    i32 x, y;
    i8  z;
    u32 g;          // cost so far
    u32 f;          // g + heuristic
    u32 parent;     // index into nodes_ vector (UINT32_MAX = no parent)
    u8  dirFromParent;
};

struct PqEntry {
    u32 f;
    u32 idx;
    bool operator<(const PqEntry& o) const { return f > o.f; }   // min-heap
};

// Chebyshev distance in cell units, *10 to match unit cost of 10/14.
constexpr u32 kStraightCost = 10;
constexpr u32 kDiagonalCost = 14;

// When a goal z is pinned, accept arriving within this many z-units of it
// (surface tops jitter a couple units between adjacent floor tiles).
constexpr i32 kGoalZTolerance = 4;
constexpr i32 kGoalZPreferenceRadius = 24;

u32 Heuristic(i32 x, i32 y, i32 gx, i32 gy) {
    const u32 dx = static_cast<u32>(std::abs(gx - x));
    const u32 dy = static_cast<u32>(std::abs(gy - y));
    const u32 mn = (dx < dy) ? dx : dy;
    const u32 mx = (dx < dy) ? dy : dx;
    return kStraightCost * (mx - mn) + kDiagonalCost * mn;
}

bool ShouldPreferGoalZ(const PathOptions& opts, i32 x, i32 y, i32 gx, i32 gy) {
    if (!opts.hasGoalZ) return false;
    i32 dx = gx - x;
    i32 dy = gy - y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (dx > dy ? dx : dy) <= kGoalZPreferenceRadius;
}

// A* search state is 3D: the same (x,y) column can be a distinct node at
// different standing z (switchback stairs, multi-storey buildings, bridges
// over a lower path). Keying only on (x,y) closes the column at whatever z
// it was first reached and dead-ends any climb that has to revisit it higher
// up — which is exactly how a far goal atop several stair flights becomes
// "unreachable". Pack z into the low 8 bits so both layers coexist.
//   x -> bits [32..], y -> bits [8..31], z -> bits [0..7]
u64 Pack(i32 x, i32 y, i8 z) {
    return (static_cast<u64>(static_cast<u32>(x)) << 32) |
           (static_cast<u64>(static_cast<u32>(y)) << 8)  |
            static_cast<u64>(static_cast<u8>(z));
}

// Open grass-like land tiles, penalised to bias routes toward roads/dirt.
// Tunable starting set (common Britannia grass); a soft penalty, so an
// incomplete set only weakens the bias — it never produces a wrong path.
bool IsGrassLikeTile(u16 id) {
    return (id >= 0x0003 && id <= 0x0006) || (id >= 197 && id <= 199);
}

}

std::vector<u8> FindPath(uo::world::World& world,
                         i32 sx, i32 sy, i8 sz,
                         i32 gx, i32 gy,
                         const PathOptions& opts) {
    std::vector<u8> result;
    // Already at the destination column. With a pinned goal z this is only
    // "arrived" when our standing z is within tolerance; otherwise the target
    // is another floor of the same column (e.g. a bridge directly overhead)
    // and we must route out and climb back, so fall through to the search.
    if (sx == gx && sy == gy) {
        if (!opts.hasGoalZ) return result;
        const i32 dz = static_cast<i32>(sz) - opts.goalZ;
        if (dz <= kGoalZTolerance && dz >= -kGoalZTolerance) return result;
    }

    std::vector<Node> nodes;
    nodes.reserve(1024);
    std::unordered_map<u64, u32> bestG;       // packed(x,y) -> node idx
    std::priority_queue<PqEntry> open;

    Node start{sx, sy, sz, 0, Heuristic(sx, sy, gx, gy),
               UINT32_MAX, 0};
    nodes.push_back(start);
    bestG[Pack(sx, sy, sz)] = 0;
    open.push({start.f, 0});

    u32 expanded = 0;
    while (!open.empty()) {
        if (expanded++ > opts.maxNodesExpanded) break;

        const PqEntry top = open.top(); open.pop();
        const Node n = nodes[top.idx];

        // Stale entry?
        auto it = bestG.find(Pack(n.x, n.y, n.z));
        if (it == bestG.end() || nodes[it->second].g != n.g) continue;

        if (opts.stats) {
            const u32 h = Heuristic(n.x, n.y, gx, gy);
            bool betterClosest = h < opts.stats->closestH;
            if (!betterClosest && h == opts.stats->closestH && opts.hasGoalZ) {
                const i32 curDz = static_cast<i32>(n.z) - opts.goalZ;
                const i32 bestDz = static_cast<i32>(opts.stats->closestZ) - opts.goalZ;
                const i32 curAbsDz = curDz < 0 ? -curDz : curDz;
                const i32 bestAbsDz = bestDz < 0 ? -bestDz : bestDz;
                betterClosest = curAbsDz < bestAbsDz;
            }
            if (betterClosest) {
                opts.stats->closestH = h;
                opts.stats->closestX = n.x;
                opts.stats->closestY = n.y;
                opts.stats->closestZ = n.z;
            }
        }

        const bool zOk = !opts.hasGoalZ ||
            (static_cast<i32>(n.z) - opts.goalZ <=  kGoalZTolerance &&
             static_cast<i32>(n.z) - opts.goalZ >= -kGoalZTolerance);
        if (n.x == gx && n.y == gy && zOk) {
            if (opts.stats) opts.stats->expanded = expanded;
            // Reconstruct path.
            u32 cur = top.idx;
            while (cur != UINT32_MAX && nodes[cur].parent != UINT32_MAX) {
                result.push_back(nodes[cur].dirFromParent);
                cur = nodes[cur].parent;
            }
            std::reverse(result.begin(), result.end());
            return result;
        }

        for (u8 d = 0; d < 8; ++d) {
            i32 dx, dy;
            DirToDelta(d, &dx, &dy);
            const i32 nx = n.x + dx;
            const i32 ny = n.y + dy;
            if (nx < 0 || ny < 0) continue;

            uo::world::WalkQuery wq{};
            wq.x          = static_cast<u32>(nx);
            wq.y          = static_cast<u32>(ny);
            wq.fromZ      = n.z;
            wq.maxStepUp  = static_cast<i8>(opts.maxStepUp);
            wq.maxStepDown= static_cast<i8>(opts.maxStepDown);
            wq.charHeight = opts.charHeight;
            wq.hasPreferredZ = ShouldPreferGoalZ(opts, nx, ny, gx, gy);
            wq.preferredZ    = opts.goalZ;
            const auto wr = world.QueryCell(wq);
            if (!wr.walkable) continue;

            // After the normal MUL checks, reject learned/overlay blocks.
            if (opts.blacklist && opts.blacklist->IsBlocked(nx, ny, wr.standZ))
                continue;
            if (opts.extraBlocked &&
                opts.extraBlocked(nx, ny, wr.standZ, opts.extraBlockedUser))
                continue;
            if (opts.extraBlockedStep &&
                opts.extraBlockedStep(n.x, n.y, n.z, nx, ny, wr.standZ,
                                      opts.extraBlockedUser))
                continue;

            // UO diagonal corner rule: a diagonal step from (n) to
            // (nx,ny) requires BOTH adjacent straight cells to be
            // walkable. Otherwise the move "cuts" a corner through a
            // wall and the server silently rejects (or RSTs after
            // repeats).
            if (dx != 0 && dy != 0) {
                uo::world::WalkQuery wqA{}, wqB{};
                wqA.x = static_cast<u32>(n.x + dx);
                wqA.y = static_cast<u32>(n.y);
                wqA.fromZ = n.z;
                wqA.maxStepUp = wq.maxStepUp;
                wqA.maxStepDown = wq.maxStepDown;
                wqA.charHeight = wq.charHeight;
                wqA.hasPreferredZ = ShouldPreferGoalZ(
                    opts, static_cast<i32>(wqA.x), static_cast<i32>(wqA.y), gx, gy);
                wqA.preferredZ = opts.goalZ;
                wqB.x = static_cast<u32>(n.x);
                wqB.y = static_cast<u32>(n.y + dy);
                wqB.fromZ = n.z;
                wqB.maxStepUp = wq.maxStepUp;
                wqB.maxStepDown = wq.maxStepDown;
                wqB.charHeight = wq.charHeight;
                wqB.hasPreferredZ = ShouldPreferGoalZ(
                    opts, static_cast<i32>(wqB.x), static_cast<i32>(wqB.y), gx, gy);
                wqB.preferredZ = opts.goalZ;
                const auto wrA = world.QueryCell(wqA);
                if (!wrA.walkable) continue;
                if (opts.blacklist && opts.blacklist->IsBlocked(wqA.x, wqA.y, wrA.standZ))
                    continue;
                if (opts.extraBlocked &&
                    opts.extraBlocked(wqA.x, wqA.y, wrA.standZ, opts.extraBlockedUser))
                    continue;
                if (opts.extraBlockedStep &&
                    opts.extraBlockedStep(n.x, n.y, n.z, wqA.x, wqA.y, wrA.standZ,
                                          opts.extraBlockedUser))
                    continue;

                const auto wrB = world.QueryCell(wqB);
                if (!wrB.walkable) continue;
                if (opts.blacklist && opts.blacklist->IsBlocked(wqB.x, wqB.y, wrB.standZ))
                    continue;
                if (opts.extraBlocked &&
                    opts.extraBlocked(wqB.x, wqB.y, wrB.standZ, opts.extraBlockedUser))
                    continue;
                if (opts.extraBlockedStep &&
                    opts.extraBlockedStep(n.x, n.y, n.z, wqB.x, wqB.y, wrB.standZ,
                                          opts.extraBlockedUser))
                    continue;
            }

            const u32 step = (dx != 0 && dy != 0) ? kDiagonalCost : kStraightCost;
            u32 ng = n.g + step;
            if (opts.grassPenalty != 0 && IsGrassLikeTile(wr.landTileId))
                ng += opts.grassPenalty;
            if (opts.foliagePenalty != 0 && wr.nearFoliage)
                ng += opts.foliagePenalty;
            if (opts.stats && nx == gx && ny == gy)
                opts.stats->reachedGoalColumn = true;

            const u64 key = Pack(nx, ny, wr.standZ);

            auto bi = bestG.find(key);
            if (bi != bestG.end() && nodes[bi->second].g <= ng) continue;

            Node nb{nx, ny, wr.standZ, ng,
                    ng + Heuristic(nx, ny, gx, gy),
                    top.idx, d};
            const u32 idx = static_cast<u32>(nodes.size());
            nodes.push_back(nb);
            bestG[key] = idx;
            open.push({nb.f, idx});
        }
    }

    if (opts.stats) opts.stats->expanded = expanded;
    return result; // empty = no path
}

}
