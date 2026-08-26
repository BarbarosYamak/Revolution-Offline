#pragma once

// ---------------------------------------------------------------------------
// SharedWorld — the one copy of the immutable world every session uses.
//
// The atlas is ~220 KB of text and the navgrid ~460 KB of cells; both are
// read-only once loaded and identical for every character, so loading them per
// session would waste memory and time for no benefit. Everything mutable --
// the journey, the personal knowledge, the avoid list -- stays a Client
// member, which is what keeps the M1.5 isolation rule intact.
//
// Loading is lazy and happens on the session thread (the process drives every
// session round-robin from one thread), and a failure is not fatal: without
// world knowledge a bot still walks with the M1.5 tile A*, it just cannot be
// given a semantic destination.
// ---------------------------------------------------------------------------

#include "world/Atlas.h"
#include "world/NavGrid.h"
#include "world/RoutePlanner.h"

namespace uo::world_atlas {

struct SharedWorld {
    Atlas               atlas;
    navgrid::NavGrid    grid;
    // Constructed after both are loaded, because it indexes the atlas against
    // the grid's cell layout.
    route::RoutePlanner* planner = nullptr;
    bool ok = false;
    std::string error;
};

// Returns the process-wide world, loading it on the first call. `atlasPath`
// and `gridPath` are used only on that first call; later callers get whatever
// was loaded, which is deliberate -- two sessions must not disagree about the
// world. Never returns null; check `ok`.
const SharedWorld* AcquireSharedWorld(const char* atlasPath,
                                      const char* gridPath);

} // namespace uo::world_atlas
