#pragma once

// ---------------------------------------------------------------------------
// RoutePlanner — the whole-world half of navigation.
//
// Given "I am here, I want to be there", it produces a short list of legs:
// a handful of walk waypoints and, where they help, the transit hops between
// them (a teleporter to step on, a moongate to use). It never emits a step and
// never touches the socket. Each Walk leg is then handed to the proven M1.5
// tile A*, which is what actually moves the character.
//
// Two properties matter and are deliberate:
//
//   * The search runs on navgrid cells (16x16 tiles), not tiles, so crossing
//     Britannia expands thousands of nodes instead of hundreds of thousands.
//   * Legs are capped in length, so the tile A* underneath always runs on a
//     problem it was measured on -- a short one.
//
// The planner is stateless and const; a caller supplies per-trip avoidance
// (cells this character has failed to cross) as an argument, so two sessions
// planning at once cannot contaminate each other.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/world_model.h"
#include "world/Atlas.h"
#include "world/NavGrid.h"

#include <string>
#include <vector>

namespace uo::route {

enum class LegKind : u8 {
    Walk = 0,     // reach `target` on foot; the tile A* owns the details
    Teleporter,   // step onto `target`; the shard moves us to `arrive`
    Moongate,     // reach `target`, use the gate, choose `label` as destination
};

const char* LegKindName(LegKind k);

struct RouteLeg {
    LegKind     kind = LegKind::Walk;
    wm::Point   target;      // where this leg ends (or the transit's entry tile)
    wm::Point   arrive;      // where a transit leg puts us (unused for Walk)
    std::string transitId;
    std::string label;       // moongate destination name, as the gump shows it
};

struct RouteOptions {
    // Travel modes this character may use right now. A bot with no way to work
    // a gate simply plans without them rather than planning a route it cannot
    // execute.
    bool allowTeleporters = true;
    bool allowMoongates   = false;

    // Longest walk leg handed to the tile A*. 40 tiles keeps every local
    // search far inside the measured node budget even when the direct line is
    // blocked and it has to go around.
    i32 maxLegTiles = 40;

    // Macro cells this character has already failed to cross on this trip.
    // Cleared per trip by the caller, never shared between sessions.
    const std::vector<u32>* avoidCells = nullptr;

    // Safety valve. Britannia is ~115k cells; a real cross-world route expands
    // far fewer, and a search that blows this budget has found no route.
    u32 maxNodesExpanded = 300000;
};

struct WorldRoute {
    bool ok = false;
    const char* failure = "";     // static string, safe to log
    std::vector<RouteLeg> legs;
    i32   estimatedTiles = 0;
    u32   nodesExpanded = 0;
    usize transitHops = 0;
};

class RoutePlanner {
public:
    RoutePlanner(const world_atlas::Atlas& atlas, const navgrid::NavGrid& grid);

    RoutePlanner(const RoutePlanner&) = delete;
    RoutePlanner& operator=(const RoutePlanner&) = delete;

    bool Ready() const { return grid_.Ready(); }

    WorldRoute Plan(i32 startX, i32 startY, i32 goalX, i32 goalY,
                    const RouteOptions& opt) const;

    // Cell index helpers, shared with the caller so a failing leg can be
    // reported back as an avoid-cell without duplicating the arithmetic.
    u32  CellIndex(i32 tileX, i32 tileY) const;
    void CellCoords(u32 index, i32* cx, i32* cy) const;

private:
    struct TransitEdge {
        u32 fromCell = 0;
        u32 toCell = 0;
        wm::Point entry;
        wm::Point arrive;
        wm::TransitKind kind = wm::TransitKind::Unknown;
        const wm::TransitNode* node = nullptr;
    };

    void BuildTransitIndex();
    const std::vector<TransitEdge>* EdgesFrom(u32 cell) const;

    const world_atlas::Atlas& atlas_;
    const navgrid::NavGrid&   grid_;

    // cell -> outgoing transit edges. Built once at construction from the
    // atlas, immutable afterwards, so it is shared like the atlas itself.
    std::vector<u32> transitCellKeys_;                 // sorted, parallel to ...
    std::vector<std::vector<TransitEdge>> transitEdges_;
};

} // namespace uo::route
