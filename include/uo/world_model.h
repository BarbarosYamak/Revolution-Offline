#pragma once

// ---------------------------------------------------------------------------
// Semantic world model (M2.5).
//
// The vocabulary a bot brain reasons in: regions, places, services, resource
// areas and transit nodes. Coordinates live *inside* these structures; nothing
// above this layer is expected to know them. "Go to a bank" is a Service query,
// not a pair of numbers.
//
// Everything here is plain data with no protocol, no sockets and no I/O, so the
// routing and lookup logic that consumes it is unit-testable without a server
// and without the MUL files.
//
// Provenance: every field is derived from the shard's own data -- Scripts-X
// AREADEFs, its worldgen spawner tables, its teleporter and moongate resource
// lists -- by tools/atlasgen. Nothing here is hand-invented geography.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::wm {

// --- primitives ------------------------------------------------------------

struct Point {
    i32 x = 0;
    i32 y = 0;
    i8  z = 0;
    u8  map = 0;
};

struct Rect {
    i32 x1 = 0, y1 = 0, x2 = 0, y2 = 0;

    bool Contains(i32 x, i32 y) const {
        return x >= x1 && x <= x2 && y >= y1 && y <= y2;
    }
    i64 Area() const {
        const i64 w = static_cast<i64>(x2) - x1 + 1;
        const i64 h = static_cast<i64>(y2) - y1 + 1;
        return (w > 0 && h > 0) ? w * h : 0;
    }
};

// --- regions ---------------------------------------------------------------

// What kind of place this region is, inferred by atlasgen from the AREADEF's
// GROUP, NAME and FLAGS. Used for route costing and for answering
// "am I in a town?" without string matching at run time.
enum class RegionKind : u8 {
    Unknown = 0,
    World,        // the map-wide catch-all AREADEF
    Town,         // a named settlement or a district inside one
    Wilderness,   // named outdoor area (forest, swamp, moor, pass)
    Dungeon,
    Cave,
    Building,     // shop/house interiors, inns, jails
    Shrine,
    Graveyard,
    Moongate,     // the small AREADEF around a public gate
    Water,
    Special,      // Green Acres, jails, staff areas
    Count,
};

const char* RegionKindName(RegionKind k);
RegionKind  RegionKindFromName(const char* name);

// Region flags that matter to a traveller. These are the REGION_* bits the
// shard itself keeps (`runtime/scripts/maps/map0/map0_areas.scp`), narrowed to
// the ones a travel planner has to respect.
struct RegionFlags {
    bool guarded      = false;  // REGION_FLAG_GUARDED
    bool safe         = false;  // REGION_FLAG_SAFE
    bool underground  = false;  // REGION_FLAG_UNDERGROUND
    bool noRecallIn   = false;  // REGION_ANTIMAGIC_RECALL_IN
    bool noRecallOut  = false;  // REGION_ANTIMAGIC_RECALL_OUT
    bool noGate       = false;  // REGION_ANTIMAGIC_GATE
    bool noTeleport   = false;  // REGION_ANTIMAGIC_TELEPORT
    bool antiMagicAll = false;  // REGION_ANTIMAGIC_ALL
    bool noPvp        = false;  // REGION_FLAG_NO_PVP

    // ANTIMAGIC_ALL subsumes the individual bans, which is how CRegion.cpp
    // checks them (`src/game/CRegion.cpp:730-750`).
    bool BlocksRecallIn()  const { return antiMagicAll || noRecallIn; }
    bool BlocksRecallOut() const { return antiMagicAll || noRecallOut; }
    bool BlocksGate()      const { return antiMagicAll || noGate; }
};

struct Region {
    std::string id;        // AREADEF defname, e.g. "a_yew"
    std::string name;      // NAME=, e.g. "Yew"
    std::string group;     // GROUP=, e.g. "Yew" / "Other Dungeons"
    RegionKind  kind = RegionKind::Unknown;
    RegionFlags flags;
    Point       center;    // AREADEF P= -- the shard's own "middle of here"
    std::vector<Rect> rects;

    bool Contains(i32 x, i32 y) const {
        for (const Rect& r : rects)
            if (r.Contains(x, y)) return true;
        return false;
    }
    i64 Area() const {
        i64 a = 0;
        for (const Rect& r : rects) a += r.Area();
        return a;
    }
};

// --- services --------------------------------------------------------------

// A service is what a bot needs, not who provides it. The shard's NPC job
// defnames map onto these; the live NPC standing there is discovered at run
// time from its paperdoll title (M2), so a moved or respawned vendor still
// works.
enum class Service : u8 {
    None = 0,
    Banker,
    Healer,
    Blacksmith,
    Alchemist,
    Mage,            // sells scrolls, blank runes, reagents
    Provisioner,
    Stablemaster,
    Tailor,
    Carpenter,
    Bowyer,
    Tinker,
    Scribe,
    Innkeeper,
    Butcher,
    Baker,
    Tanner,
    Jeweler,
    Shipwright,
    Cook,
    Miller,
    Mapmaker,
    Fisherman,
    Veterinarian,
    GeneralVendor,
    Count,
};

const char* ServiceName(Service s);
Service     ServiceFromName(const char* name);   // Service::None if unknown

// --- resources -------------------------------------------------------------

enum class ResourceKind : u8 {
    None = 0,
    Mining,
    Lumber,
    Fishing,
    Reagents,
    Hunting,
    Count,
};

const char*  ResourceName(ResourceKind r);
ResourceKind ResourceFromName(const char* name);

// --- places ----------------------------------------------------------------

enum class PlaceCategory : u8 {
    Unknown = 0,
    TownCenter,
    Bank,
    Healer,
    Shop,           // a vendor spawn point offering one or more services
    Stable,
    Inn,
    Dock,
    Shrine,
    Graveyard,
    Moongate,
    DungeonEntrance,
    ResourceArea,
    Landmark,
    Count,
};

const char*   PlaceCategoryName(PlaceCategory c);
PlaceCategory PlaceCategoryFromName(const char* name);

// A named destination. `position` is where the thing is; `radius` is how close
// a bot has to get for the place to count as reached -- an interaction radius,
// not a rendering radius. Vendor spawners carry the shard's own walking range,
// so an NPC that has wandered is still inside its own place.
struct Place {
    std::string   id;        // slug, e.g. "yew_bank"
    std::string   name;      // human label, e.g. "Yew Bank"
    PlaceCategory category = PlaceCategory::Unknown;
    std::string   regionId; // owning region (may be empty for wilderness)
    Point         position;
    i32           radius = 3;
    std::vector<Service>      services;
    std::vector<ResourceKind> resources;

    bool Offers(Service s) const {
        for (Service v : services) if (v == s) return true;
        return false;
    }
    bool Yields(ResourceKind r) const {
        for (ResourceKind v : resources) if (v == r) return true;
        return false;
    }
};

// --- transit ---------------------------------------------------------------

// A world link that is not walking. Teleporters and dungeon stairs are
// walk-on; a public moongate is an object the player interacts with and then
// answers a destination gump.
enum class TransitKind : u8 {
    Unknown = 0,
    Teleporter,     // RES_TELEPORTERS: step on the tile, arrive at the other end
    Moongate,       // public gate: reach it, use it, choose a destination
    Count,
};

const char* TransitKindName(TransitKind k);
TransitKind TransitKindFromName(const char* name);

struct TransitNode {
    std::string id;
    TransitKind kind = TransitKind::Unknown;
    Point       from;      // the tile you stand on / the gate's tile
    Point       to;        // where it puts you (moongates: the default hop)
    std::string label;     // the shard's own defname or destination name
    bool        bidirectional = false;
};

// --- danger ----------------------------------------------------------------

// Deliberately coarse. Enough to keep a route planner honest without
// pretending to be threat intelligence, which M2 showed we do not have
// (wildlife that chases but never attacks is not a threat).
enum class Danger : u8 {
    Unknown = 0,
    Normal,
    RecentlyDangerous,
    Count,
};

const char* DangerName(Danger d);

} // namespace uo::wm
