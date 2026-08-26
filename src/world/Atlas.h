#pragma once

// ---------------------------------------------------------------------------
// Atlas — the shard's world, as semantics rather than coordinates.
//
// Holds every Region, Place and TransitNode that `tools/atlasgen` derived from
// the Revolution/Scripts-X data, and answers the questions a bot brain asks:
// "which region am I in", "where is the nearest banker", "what gates exist".
//
// Immutable after Load(). One instance is shared by every session in the
// process; nothing in here is per-character. Personal knowledge (what *this*
// bot has seen, marked or died at) lives in travel::PersonalKnowledge instead.
// ---------------------------------------------------------------------------

#include "uo/world_model.h"
#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::world_atlas {

class Atlas {
public:
    Atlas() = default;

    Atlas(const Atlas&) = delete;
    Atlas& operator=(const Atlas&) = delete;

    // Parse the generated atlas file. `err` receives a reason on failure.
    bool Load(const char* path, std::string* err);
    // Parse from memory (used by tests, which must not depend on a data file).
    bool LoadFromText(const char* text, std::string* err);

    bool Ready() const { return !regions_.empty(); }

    const std::vector<wm::Region>&      Regions()  const { return regions_; }
    const std::vector<wm::Place>&       Places()   const { return places_; }
    const std::vector<wm::TransitNode>& Transits() const { return transits_; }

    u8  MapId()       const { return mapId_; }
    i32 MapWidth()    const { return mapWidth_; }
    i32 MapHeight()   const { return mapHeight_; }

    // --- region lookup -----------------------------------------------------

    // The most specific region containing (x, y): the smallest by total area,
    // so a shop interior wins over the town and the town wins over the facet.
    // Null when the point is outside every AREADEF.
    const wm::Region* RegionAt(i32 x, i32 y) const;
    const wm::Region* RegionById(const char* id) const;
    // Case-insensitive lookup by defname first, then by NAME, then by a name
    // substring -- so TravelToRegion("Britain") works without the caller
    // knowing that the defname is "a_britain".
    const wm::Region* FindRegion(const char* needle) const;

    // --- place lookup ------------------------------------------------------

    const wm::Place* PlaceById(const char* id) const;
    // Same widening search as FindRegion: id, then exact name, then substring.
    const wm::Place* FindPlace(const char* needle) const;

    // Nearest place offering a service / yielding a resource, measured by
    // Chebyshev tiles from (x, y). `maxDist` <= 0 means "anywhere".
    const wm::Place* NearestPlaceWithService(wm::Service s, i32 x, i32 y,
                                             i32 maxDist = 0) const;
    const wm::Place* NearestPlaceWithResource(wm::ResourceKind r, i32 x, i32 y,
                                              i32 maxDist = 0) const;
    const wm::Place* NearestPlaceOfCategory(wm::PlaceCategory c, i32 x, i32 y,
                                            i32 maxDist = 0) const;
    // Nearest place inside a named region, so "the bank in Yew" is expressible
    // without hard-coding which bank that is.
    const wm::Place* NearestPlaceWithServiceInRegion(wm::Service s,
                                                     const char* regionId,
                                                     i32 x, i32 y) const;

    // --- transit -----------------------------------------------------------

    const wm::TransitNode* NearestTransit(wm::TransitKind kind, i32 x, i32 y,
                                          i32 maxDist = 0) const;
    const wm::TransitNode* TransitById(const char* id) const;

    // --- travel legality ---------------------------------------------------

    // Whether the shard's own region flags allow arriving at / leaving from a
    // point by Recall or Gate Travel. A point outside every AREADEF is treated
    // as unrestricted, which is what CRegion does when there is no area.
    bool AllowsRecallInto(i32 x, i32 y) const;
    bool AllowsRecallOutOf(i32 x, i32 y) const;
    bool AllowsGateAt(i32 x, i32 y) const;

    usize CountPlacesWithService(wm::Service s) const;

private:
    bool ParseLine(const char* line, usize lineNo, std::string* err);

    u8  mapId_ = 0;
    i32 mapWidth_ = 0;
    i32 mapHeight_ = 0;
    std::vector<wm::Region>      regions_;
    std::vector<wm::Place>       places_;
    std::vector<wm::TransitNode> transits_;
};

} // namespace uo::world_atlas
