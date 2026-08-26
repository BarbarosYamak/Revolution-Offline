#include "world/RoutePlanner.h"

#include <algorithm>
#include <queue>
#include <unordered_map>

namespace uo::route {

namespace {

// Cell-to-cell costs, in tiles, so the total is directly comparable to a walk
// distance and the heuristic stays admissible.
constexpr i32 kStraightCost = static_cast<i32>(navgrid::kCellTiles);        // 16
constexpr i32 kDiagonalCost = static_cast<i32>(navgrid::kCellTiles) * 3 / 2; // 24

// A teleporter is free to cross once you are standing on it, but finding and
// lining up on the pad costs a couple of steps.
constexpr i32 kTeleporterCost = 4;

// A moongate costs the walk to it (already paid by the preceding legs) plus
// the interaction: reach it, open the gump, pick a destination, arrive. Priced
// as ~120 tiles of running so the planner prefers to walk anything shorter
// than about two screens rather than perform a ceremony for nothing.
constexpr i32 kMoongateCost = 120;

i32 Chebyshev(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

const char* const kLegKindNames[] = { "walk", "teleporter", "moongate" };

struct OpenNode {
    i32 f;
    u32 cell;
    bool operator>(const OpenNode& o) const { return f > o.f; }
};

} // namespace

const char* LegKindName(LegKind k) {
    const usize i = static_cast<usize>(k);
    return i < sizeof(kLegKindNames) / sizeof(kLegKindNames[0])
               ? kLegKindNames[i]
               : "?";
}

RoutePlanner::RoutePlanner(const world_atlas::Atlas& atlas,
                           const navgrid::NavGrid& grid)
    : atlas_(atlas), grid_(grid) {
    BuildTransitIndex();
}

u32 RoutePlanner::CellIndex(i32 tileX, i32 tileY) const {
    const i32 cx = navgrid::NavGrid::TileToCell(tileX);
    const i32 cy = navgrid::NavGrid::TileToCell(tileY);
    return static_cast<u32>(cy) * grid_.CellsX() + static_cast<u32>(cx);
}

void RoutePlanner::CellCoords(u32 index, i32* cx, i32* cy) const {
    const u32 w = grid_.CellsX() ? grid_.CellsX() : 1;
    if (cx) *cx = static_cast<i32>(index % w);
    if (cy) *cy = static_cast<i32>(index / w);
}

void RoutePlanner::BuildTransitIndex() {
    if (!grid_.Ready()) return;

    std::unordered_map<u32, std::vector<TransitEdge>> byCell;
    for (const wm::TransitNode& t : atlas_.Transits()) {
        if (t.kind == wm::TransitKind::Unknown) continue;

        TransitEdge e;
        e.fromCell = CellIndex(t.from.x, t.from.y);
        e.toCell   = CellIndex(t.to.x, t.to.y);
        e.entry    = t.from;
        e.arrive   = t.to;
        e.kind     = t.kind;
        e.node     = &t;
        if (e.fromCell == e.toCell) continue;   // a hop inside one cell is a walk
        byCell[e.fromCell].push_back(e);

        if (t.bidirectional) {
            TransitEdge back = e;
            back.fromCell = e.toCell;
            back.toCell   = e.fromCell;
            back.entry    = t.to;
            back.arrive   = t.from;
            byCell[back.fromCell].push_back(back);
        }
    }

    transitCellKeys_.reserve(byCell.size());
    for (const auto& kv : byCell) transitCellKeys_.push_back(kv.first);
    std::sort(transitCellKeys_.begin(), transitCellKeys_.end());
    transitEdges_.reserve(transitCellKeys_.size());
    for (u32 key : transitCellKeys_)
        transitEdges_.push_back(byCell[key]);
}

const std::vector<RoutePlanner::TransitEdge>* RoutePlanner::EdgesFrom(
    u32 cell) const {
    const auto it = std::lower_bound(transitCellKeys_.begin(),
                                     transitCellKeys_.end(), cell);
    if (it == transitCellKeys_.end() || *it != cell) return nullptr;
    return &transitEdges_[static_cast<usize>(it - transitCellKeys_.begin())];
}

void RoutePlanner::EscapeCandidates(i32 x, i32 y, usize maxCount,
                                    std::vector<wm::Point>& out) const {
    out.clear();
    if (!grid_.Ready() || maxCount == 0) return;

    const i32 homeCx = navgrid::NavGrid::TileToCell(x);
    const i32 homeCy = navgrid::NavGrid::TileToCell(y);

    struct Candidate { wm::Point p; i32 d; };
    std::vector<Candidate> found;
    // Three rings is ~48 tiles out: far enough to leave a building, close
    // enough that walking to one is a short trip rather than a second journey.
    for (i32 r = 1; r <= 3; ++r) {
        for (i32 dy = -r; dy <= r; ++dy) {
            for (i32 dx = -r; dx <= r; ++dx) {
                const i32 adx = dx < 0 ? -dx : dx;
                const i32 ady = dy < 0 ? -dy : dy;
                if (adx != r && ady != r) continue;   // ring only
                i32 ax, ay;
                i8  az;
                if (!grid_.Anchor(homeCx + dx, homeCy + dy, &ax, &ay, &az))
                    continue;
                wm::Point p;
                p.x = ax;
                p.y = ay;
                p.z = az;
                found.push_back(Candidate{p, Chebyshev(x, y, ax, ay)});
            }
        }
    }

    std::sort(found.begin(), found.end(),
              [](const Candidate& a, const Candidate& b) { return a.d < b.d; });
    for (const Candidate& c : found) {
        if (out.size() >= maxCount) break;
        out.push_back(c.p);
    }
}

WorldRoute RoutePlanner::Plan(i32 startX, i32 startY, i32 goalX, i32 goalY,
                              const RouteOptions& opt) const {
    WorldRoute out;
    if (!grid_.Ready()) {
        out.failure = "navgrid not loaded";
        return out;
    }

    // Snap both ends onto sampled cells. A character standing inside a shop can
    // legitimately occupy a cell whose 64 probes all landed on furniture, and a
    // destination can sit one tile off a sampled spot; refusing to plan in that
    // case would be a false negative, so allow a small search outward.
    i32 startCx = navgrid::NavGrid::TileToCell(startX);
    i32 startCy = navgrid::NavGrid::TileToCell(startY);
    i32 goalCx  = navgrid::NavGrid::TileToCell(goalX);
    i32 goalCy  = navgrid::NavGrid::TileToCell(goalY);
    if (!grid_.NearestPassable(startCx, startCy, 3, &startCx, &startCy)) {
        out.failure = "no walkable ground near the start";
        return out;
    }
    if (!grid_.NearestPassable(goalCx, goalCy, 3, &goalCx, &goalCy)) {
        out.failure = "no walkable ground near the destination";
        return out;
    }

    const u32 cellsX = grid_.CellsX();
    const u32 startCell = static_cast<u32>(startCy) * cellsX + startCx;
    const u32 goalCell  = static_cast<u32>(goalCy)  * cellsX + goalCx;

    // Trivially close: one walk leg, no macro search at all. This is the common
    // case for "approach the banker you can already see" and it keeps the
    // planner off the hot path for short hops.
    if (startCell == goalCell ||
        Chebyshev(startX, startY, goalX, goalY) <= opt.maxLegTiles) {
        RouteLeg leg;
        leg.kind = LegKind::Walk;
        leg.target.x = goalX;
        leg.target.y = goalY;
        out.legs.push_back(leg);
        out.ok = true;
        out.estimatedTiles = Chebyshev(startX, startY, goalX, goalY);
        return out;
    }

    struct Visit {
        i32 g = 0;
        u32 parent = 0xFFFFFFFFu;
        const TransitEdge* viaTransit = nullptr;  // edge that entered this cell
        bool closed = false;
    };
    std::unordered_map<u32, Visit> seen;
    seen.reserve(4096);

    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>>
        open;

    auto heuristic = [&](u32 cell) {
        i32 cx, cy;
        CellCoords(cell, &cx, &cy);
        return Chebyshev(cx * static_cast<i32>(navgrid::kCellTiles),
                         cy * static_cast<i32>(navgrid::kCellTiles),
                         goalX, goalY);
    };

    auto isAvoided = [&](u32 cell) {
        if (!opt.avoidCells) return false;
        for (u32 c : *opt.avoidCells)
            if (c == cell) return true;
        return false;
    };

    seen[startCell] = Visit{0, 0xFFFFFFFFu, nullptr, false};
    open.push(OpenNode{heuristic(startCell), startCell});

    bool found = false;
    while (!open.empty()) {
        const OpenNode cur = open.top();
        open.pop();

        Visit& v = seen[cur.cell];
        if (v.closed) continue;
        v.closed = true;
        ++out.nodesExpanded;
        if (out.nodesExpanded > opt.maxNodesExpanded) {
            out.failure = "world route search exhausted its node budget";
            return out;
        }

        if (cur.cell == goalCell) { found = true; break; }

        const i32 baseG = v.g;
        i32 cx, cy;
        CellCoords(cur.cell, &cx, &cy);

        auto relax = [&](u32 next, i32 cost, const TransitEdge* via) {
            if (isAvoided(next)) return;
            const i32 ng = baseG + cost;
            auto it = seen.find(next);
            if (it != seen.end()) {
                if (it->second.closed || it->second.g <= ng) return;
                it->second.g = ng;
                it->second.parent = cur.cell;
                it->second.viaTransit = via;
            } else {
                seen[next] = Visit{ng, cur.cell, via, false};
            }
            open.push(OpenNode{ng + heuristic(next), next});
        };

        // Neighbours come from the grid's measured edges, not from "both cells
        // have ground in them". The edge mask was built by running the same
        // tile A* the walker uses between the two anchors, so an edge here is
        // a crossing the bot can actually make.
        static const i32 kDelta[8][2] = {
            {0, -1}, {1, -1}, {1, 0}, {1, 1},
            {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
        };
        for (u8 dir = 0; dir < 8; ++dir) {
            if (!grid_.EdgeOpen(cx, cy, dir)) continue;
            const i32 nx = cx + kDelta[dir][0];
            const i32 ny = cy + kDelta[dir][1];
            if (!grid_.Passable(nx, ny)) continue;
            const bool diagonal = kDelta[dir][0] && kDelta[dir][1];
            relax(static_cast<u32>(ny) * cellsX + static_cast<u32>(nx),
                  diagonal ? kDiagonalCost : kStraightCost, nullptr);
        }

        if (const std::vector<TransitEdge>* edges = EdgesFrom(cur.cell)) {
            for (const TransitEdge& e : *edges) {
                if (e.kind == wm::TransitKind::Teleporter &&
                    !opt.allowTeleporters)
                    continue;
                if (e.kind == wm::TransitKind::Moongate && !opt.allowMoongates)
                    continue;
                relax(e.toCell,
                      e.kind == wm::TransitKind::Moongate ? kMoongateCost
                                                          : kTeleporterCost,
                      &e);
            }
        }
    }

    if (!found) {
        out.failure = "no world route to the destination";
        return out;
    }

    // Walk the parent chain back to the start, then reverse it.
    std::vector<u32> cells;
    std::vector<const TransitEdge*> vias;
    for (u32 c = goalCell;;) {
        const auto it = seen.find(c);
        if (it == seen.end()) break;
        cells.push_back(c);
        vias.push_back(it->second.viaTransit);
        if (it->second.parent == 0xFFFFFFFFu) break;
        c = it->second.parent;
    }
    std::reverse(cells.begin(), cells.end());
    std::reverse(vias.begin(), vias.end());

    out.estimatedTiles = seen[goalCell].g;

    // Turn the cell chain into legs. Consecutive walk cells collapse into one
    // waypoint; a waypoint is only emitted when keeping the run going would
    // push the next hop past maxLegTiles, and a transit always breaks the run
    // because the bot has to be standing on the pad or at the gate.
    //
    // The emission looks one cell AHEAD rather than reacting after the fact.
    // Emitting only once a run has already exceeded the budget is how you end
    // up handing the tile A* a leg longer than the budget it was measured on
    // -- including, at the very end, a final hop straight to the destination.
    i32 lastX = startX, lastY = startY;
    auto pushWalk = [&](i32 x, i32 y, i8 z) {
        RouteLeg leg;
        leg.kind = LegKind::Walk;
        leg.target.x = x;
        leg.target.y = y;
        leg.target.z = z;
        out.legs.push_back(leg);
        lastX = x;
        lastY = y;
    };

    bool havePending = false;
    i32 pendX = 0, pendY = 0;
    i8  pendZ = 0;
    auto flushPending = [&]() {
        if (!havePending) return;
        havePending = false;
        pushWalk(pendX, pendY, pendZ);
    };

    for (usize i = 1; i < cells.size(); ++i) {
        const TransitEdge* via = vias[i];
        if (via) {
            flushPending();
            // Reach the pad/gate first, if we are not effectively on it.
            if (Chebyshev(lastX, lastY, via->entry.x, via->entry.y) > 0)
                pushWalk(via->entry.x, via->entry.y, via->entry.z);

            RouteLeg leg;
            leg.kind = (via->kind == wm::TransitKind::Moongate)
                           ? LegKind::Moongate
                           : LegKind::Teleporter;
            leg.target = via->entry;
            leg.arrive = via->arrive;
            if (via->node) {
                leg.transitId = via->node->id;
                leg.label = via->node->label;
            }
            out.legs.push_back(leg);
            ++out.transitHops;
            lastX = via->arrive.x;
            lastY = via->arrive.y;
            continue;
        }

        i32 ax, ay;
        i8 az;
        i32 ccx, ccy;
        CellCoords(cells[i], &ccx, &ccy);
        if (!grid_.Anchor(ccx, ccy, &ax, &ay, &az)) continue;

        // Reaching this anchor from the last emitted point would be too far,
        // so the previous anchor becomes a waypoint and the run restarts.
        if (Chebyshev(lastX, lastY, ax, ay) > opt.maxLegTiles) flushPending();

        pendX = ax;
        pendY = ay;
        pendZ = az;
        havePending = true;
    }

    // The final hop is subject to the same budget as every other one.
    if (Chebyshev(lastX, lastY, goalX, goalY) > opt.maxLegTiles) flushPending();
    if (lastX != goalX || lastY != goalY) pushWalk(goalX, goalY, 0);

    out.ok = !out.legs.empty();
    if (!out.ok) out.failure = "route collapsed to no legs";
    return out;
}

} // namespace uo::route
