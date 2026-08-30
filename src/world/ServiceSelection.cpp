#include "world/ServiceSelection.h"

#include <cstdio>

namespace uo::world_atlas {

namespace {

struct OverCapCandidate {
    const wm::Place* place = nullptr;
    route::WorldRoute route;
};

} // namespace

ServicePick PickServicePlace(const Atlas& atlas, const route::RoutePlanner& planner,
                             wm::Service s, i32 x, i32 y,
                             const std::vector<std::string>& skipIds,
                             bool farOk,
                             std::vector<ServiceRejection>* rejections,
                             usize maxCandidates) {
    if (rejections) rejections->clear();

    std::vector<const wm::Place*> candidates;
    atlas.PlacesWithServiceSkipping(s, x, y, skipIds, candidates);

    ServicePick bestInCap;
    ServicePick bestOverall;
    std::vector<OverCapCandidate> overCap;

    route::RouteOptions opt;
    // A candidate cannot be cost-ranked without knowing whether a gate would
    // shorten it -- that is exactly the information raw Chebyshev distance
    // is missing. Evaluate as if gates are usable; a character that genuinely
    // cannot use one yet will find that out from TravelPlanRoute same as
    // before, at the cost of one bad trip rather than every trip.
    opt.allowMoongates = true;

    usize tested = 0;
    for (const wm::Place* p : candidates) {
        if (tested >= maxCandidates) break;
        ++tested;
        const route::WorldRoute r =
            planner.Plan(x, y, p->position.x, p->position.y, opt);
        if (!r.ok) continue;   // not a geography-policy rejection; just not a route

        const bool overTheCap = !farOk && r.estimatedTiles > kMaxServiceTripTiles;

        if (!bestOverall.place || r.estimatedTiles < bestOverall.estimatedTiles) {
            bestOverall = ServicePick{p, r.estimatedTiles, r.transitHops, overTheCap};
        }

        if (overTheCap) {
            overCap.push_back(OverCapCandidate{p, r});
            continue;
        }

        // Fewer transit hops always wins inside the cap; tiles only break a
        // tie. This is what stops a farther-but-fewer-gates shop from losing
        // to a nearer-but-three-gates one just because raw distance said so.
        if (!bestInCap.place || r.transitHops < bestInCap.transitHops ||
            (r.transitHops == bestInCap.transitHops &&
             r.estimatedTiles < bestInCap.estimatedTiles)) {
            bestInCap = ServicePick{p, r.estimatedTiles, r.transitHops, false};
        }
    }

    const ServicePick& chosen = bestInCap.place ? bestInCap : bestOverall;

    if (rejections && chosen.place) {
        for (const OverCapCandidate& c : overCap) {
            if (c.place == chosen.place) continue;  // the fallback pick itself
            ServiceRejection rej;
            rej.place = c.place;
            rej.estimatedTiles = c.route.estimatedTiles;
            rej.transitHops = c.route.transitHops;
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "%d tiles and %zu gate%s for a service %s offers",
                          c.route.estimatedTiles, c.route.transitHops,
                          c.route.transitHops == 1 ? "" : "s",
                          chosen.place->name.c_str());
            rej.reason = buf;
            rejections->push_back(rej);
        }
    }

    return chosen;
}

} // namespace uo::world_atlas
