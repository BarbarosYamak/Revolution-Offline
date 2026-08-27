#include "uo/world_model.h"

#include <cstring>

namespace uo::wm {

namespace {

// A name table indexed by the enum, so a new enumerator without a name is a
// compile error rather than a mystery blank in a log line. The same trap the
// Scenario op table documents (`src/bot/Scenario.h`) -- append, never insert.
template <typename E, usize N>
const char* Lookup(const char* const (&table)[N], E value) {
    const usize i = static_cast<usize>(value);
    return i < N ? table[i] : "?";
}

bool EqualsNoCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == 0 && *b == 0;
}

const char* const kRegionKindNames[] = {
    "unknown", "world", "town", "wilderness", "dungeon", "cave", "building",
    "shrine", "graveyard", "moongate", "water", "special",
};
static_assert(sizeof(kRegionKindNames) / sizeof(kRegionKindNames[0]) ==
                  static_cast<usize>(RegionKind::Count),
              "kRegionKindNames is out of step with RegionKind");

const char* const kServiceNames[] = {
    "none", "banker", "healer", "blacksmith", "alchemist", "mage",
    "provisioner", "stablemaster", "tailor", "carpenter", "bowyer", "tinker",
    "scribe", "innkeeper", "butcher", "baker", "tanner", "jeweler",
    "shipwright", "cook", "miller", "mapmaker", "fisherman", "veterinarian",
    "generalvendor",
};
static_assert(sizeof(kServiceNames) / sizeof(kServiceNames[0]) ==
                  static_cast<usize>(Service::Count),
              "kServiceNames is out of step with Service");

const char* const kResourceNames[] = {
    "none", "mining", "lumber", "fishing", "reagents", "hunting",
};
static_assert(sizeof(kResourceNames) / sizeof(kResourceNames[0]) ==
                  static_cast<usize>(ResourceKind::Count),
              "kResourceNames is out of step with ResourceKind");

const char* const kPlaceCategoryNames[] = {
    "unknown", "town_center", "bank", "healer", "shop", "stable", "inn",
    "dock", "shrine", "graveyard", "moongate", "dungeon_entrance",
    "resource_area", "landmark",
};
static_assert(sizeof(kPlaceCategoryNames) / sizeof(kPlaceCategoryNames[0]) ==
                  static_cast<usize>(PlaceCategory::Count),
              "kPlaceCategoryNames is out of step with PlaceCategory");

const char* const kTransitKindNames[] = { "unknown", "teleporter", "moongate" };
static_assert(sizeof(kTransitKindNames) / sizeof(kTransitKindNames[0]) ==
                  static_cast<usize>(TransitKind::Count),
              "kTransitKindNames is out of step with TransitKind");

const char* const kDangerNames[] = { "unknown", "normal", "recently_dangerous" };
static_assert(sizeof(kDangerNames) / sizeof(kDangerNames[0]) ==
                  static_cast<usize>(Danger::Count),
              "kDangerNames is out of step with Danger");

} // namespace

const char* RegionKindName(RegionKind k) { return Lookup(kRegionKindNames, k); }

RegionKind RegionKindFromName(const char* name) {
    for (usize i = 0; i < static_cast<usize>(RegionKind::Count); ++i)
        if (EqualsNoCase(name, kRegionKindNames[i]))
            return static_cast<RegionKind>(i);
    return RegionKind::Unknown;
}

const char* ServiceName(Service s) { return Lookup(kServiceNames, s); }

Service ServiceFromName(const char* name) {
    for (usize i = 0; i < static_cast<usize>(Service::Count); ++i)
        if (EqualsNoCase(name, kServiceNames[i]))
            return static_cast<Service>(i);
    return Service::None;
}

const char* ResourceName(ResourceKind r) { return Lookup(kResourceNames, r); }

ResourceKind ResourceFromName(const char* name) {
    for (usize i = 0; i < static_cast<usize>(ResourceKind::Count); ++i)
        if (EqualsNoCase(name, kResourceNames[i]))
            return static_cast<ResourceKind>(i);
    return ResourceKind::None;
}

const char* PlaceCategoryName(PlaceCategory c) {
    return Lookup(kPlaceCategoryNames, c);
}

PlaceCategory PlaceCategoryFromName(const char* name) {
    for (usize i = 0; i < static_cast<usize>(PlaceCategory::Count); ++i)
        if (EqualsNoCase(name, kPlaceCategoryNames[i]))
            return static_cast<PlaceCategory>(i);
    return PlaceCategory::Unknown;
}

const char* TransitKindName(TransitKind k) {
    return Lookup(kTransitKindNames, k);
}

TransitKind TransitKindFromName(const char* name) {
    for (usize i = 0; i < static_cast<usize>(TransitKind::Count); ++i)
        if (EqualsNoCase(name, kTransitKindNames[i]))
            return static_cast<TransitKind>(i);
    return TransitKind::Unknown;
}

const char* DangerName(Danger d) { return Lookup(kDangerNames, d); }

} // namespace uo::wm
