#include "world/SharedWorld.h"

#include <cstdio>

namespace uo::world_atlas {

const SharedWorld* AcquireSharedWorld(const char* atlasPath,
                                      const char* gridPath) {
    // Function-local static: constructed on first use, destroyed at exit, and
    // never reconstructed -- which is exactly the "load once, share, never
    // mutate" contract this object exists to provide.
    static SharedWorld world;
    static bool attempted = false;
    if (attempted) return &world;
    attempted = true;

    if (!atlasPath || !*atlasPath) {
        world.error = "no atlas path configured";
        return &world;
    }

    std::string err;
    if (!world.atlas.Load(atlasPath, &err)) {
        world.error = err;
        return &world;
    }
    if (!gridPath || !*gridPath || !world.grid.Load(gridPath)) {
        // The atlas alone still answers "where is the nearest banker"; it just
        // cannot plan a cross-world route. Say so rather than pretending.
        world.error = "navgrid missing or unreadable; world routing disabled";
        return &world;
    }

    static route::RoutePlanner planner(world.atlas, world.grid);
    world.planner = &planner;
    world.ok = true;
    world.error.clear();
    return &world;
}

} // namespace uo::world_atlas
