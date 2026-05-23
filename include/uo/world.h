#pragma once

#include "uo/map.h"
#include "uo/tiledata.h"
#include "uo/types.h"

namespace uo::world {

// UO walkability rules (matches classic 2.x client + RunUO/POL server
// expectations — both must agree or the server rubber-bands the move):
//
//   - Standing surfaces:
//       (a) Land tile at (x,y) — z = land.z, name = land.tileId.
//           Walkable if the tile's flags lack kFlagImpassable.
//       (b) Any static at (x,y) with kFlagSurface OR kFlagBridge —
//           surface z = static.z + static.height (top face).
//
//   - Step limits (used by the client to refuse local moves):
//       up   <= +2 z-units
//       down <= -12 z-units
//
//   - Vertical clearance:
//       For a chosen surface z = S, the column [S .. S + charHeight) must
//       not collide with any static that is *itself* not a surface
//       (i.e. an obstacle: walls, doors closed, items with no Surface
//       flag). charHeight defaults to 16 (typical humanoid).
//
// The original client's tile-flag bits live in tiledata.h.

struct WalkQuery {
    u32 x;
    u32 y;
    i8  fromZ;
    // Land step-up is capped separately at +2. Static surfaces keep a looser
    // limit so stair/ramp stacks can climb; drops stay bounded to keep A*
    // from exploding across unrelated floors.
    i8  maxStepUp   = 12;
    i8  maxStepDown = 12;
    u8  charHeight  = 16;
    bool hasPreferredZ = false;
    i32 preferredZ = 0;
};

struct WalkResult {
    bool walkable;
    i8   standZ;       // z to stand at if walkable; 0 otherwise
    u32  staticCount;  // how many statics existed in this cell
    u16  landTileId;   // land terrain tile id at the cell (for terrain bias)
    bool nearFoliage;  // foliage static in/next to this cell (forest bias)
};

class World {
public:
    World(const tiledata::TileDataLoader& td, map::Map& m)
        : td_(td), map_(m) {}

    // Probe whether a character at fromZ can walk into cell (x,y).
    // Returns the highest legal standing surface within step limits.
    WalkResult QueryCell(const WalkQuery& q) const;

    // When true, statics whose only blocking flag is Door are treated as
    // passable so A* routes through doorways (the door is opened at runtime).
    void SetAcceptDoors(bool b) { acceptDoors_ = b; }

    // Treat a static as an obstacle (not a surface).
    bool IsStaticBlocker(u16 itemId) const;

    // Surface top z for a static (z + height if it provides a surface).
    // Returns false if static is not a surface.
    bool StaticSurfaceTop(u16 itemId, i8 baseZ, i8* topOut) const;

private:
    const tiledata::TileDataLoader& td_;
    map::Map&                       map_;
    bool                            acceptDoors_ = false;
};

}
