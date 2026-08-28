#include "world/Atlas.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace uo::world_atlas {

using wm::Place;
using wm::Point;
using wm::Rect;
using wm::Region;
using wm::TransitNode;

namespace {

// The atlas file is tab-separated so free text (region names like "Yew Forest",
// group names like "Other Dungeons") needs no quoting or escaping.
void SplitTabs(const char* line, std::vector<std::string>& out) {
    out.clear();
    const char* p = line;
    const char* start = line;
    for (;; ++p) {
        if (*p == '\t' || *p == 0) {
            out.emplace_back(start, static_cast<usize>(p - start));
            if (*p == 0) break;
            start = p + 1;
        }
    }
}

i32 ToI32(const std::string& s) {
    return static_cast<i32>(std::strtol(s.c_str(), nullptr, 10));
}

u32 ToU32Hex(const std::string& s) {
    return static_cast<u32>(std::strtoul(s.c_str(), nullptr, 16));
}

char LowerCh(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = LowerCh(c);
    return out;
}

bool EqualsNoCase(const std::string& a, const char* b) {
    if (!b) return false;
    usize i = 0;
    for (; i < a.size(); ++i) {
        if (b[i] == 0) return false;
        if (LowerCh(a[i]) != LowerCh(b[i])) return false;
    }
    return b[i] == 0;
}

bool ContainsNoCase(const std::string& hay, const std::string& needleLower) {
    if (needleLower.empty()) return false;
    return Lower(hay).find(needleLower) != std::string::npos;
}

i32 Chebyshev(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

// Flag bits as written by atlasgen. Kept here (rather than in the model) so
// the on-disk encoding is one file's business.
constexpr u32 kFlagGuarded      = 0x0001;
constexpr u32 kFlagSafe         = 0x0002;
constexpr u32 kFlagUnderground  = 0x0004;
constexpr u32 kFlagNoRecallIn   = 0x0008;
constexpr u32 kFlagNoRecallOut  = 0x0010;
constexpr u32 kFlagNoGate       = 0x0020;
constexpr u32 kFlagNoTeleport   = 0x0040;
constexpr u32 kFlagAntiMagicAll = 0x0080;
constexpr u32 kFlagNoPvp        = 0x0100;

wm::RegionFlags DecodeFlags(u32 bits) {
    wm::RegionFlags f;
    f.guarded      = (bits & kFlagGuarded)      != 0;
    f.safe         = (bits & kFlagSafe)         != 0;
    f.underground  = (bits & kFlagUnderground)  != 0;
    f.noRecallIn   = (bits & kFlagNoRecallIn)   != 0;
    f.noRecallOut  = (bits & kFlagNoRecallOut)  != 0;
    f.noGate       = (bits & kFlagNoGate)       != 0;
    f.noTeleport   = (bits & kFlagNoTeleport)   != 0;
    f.antiMagicAll = (bits & kFlagAntiMagicAll) != 0;
    f.noPvp        = (bits & kFlagNoPvp)        != 0;
    return f;
}

void ParseCsvServices(const std::string& csv, std::vector<wm::Service>& out) {
    usize start = 0;
    while (start <= csv.size()) {
        const usize comma = csv.find(',', start);
        const usize end = (comma == std::string::npos) ? csv.size() : comma;
        if (end > start) {
            const std::string tok = csv.substr(start, end - start);
            const wm::Service s = wm::ServiceFromName(tok.c_str());
            if (s != wm::Service::None) out.push_back(s);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
}

void ParseCsvResources(const std::string& csv,
                       std::vector<wm::ResourceKind>& out) {
    usize start = 0;
    while (start <= csv.size()) {
        const usize comma = csv.find(',', start);
        const usize end = (comma == std::string::npos) ? csv.size() : comma;
        if (end > start) {
            const std::string tok = csv.substr(start, end - start);
            const wm::ResourceKind r = wm::ResourceFromName(tok.c_str());
            if (r != wm::ResourceKind::None) out.push_back(r);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
}

} // namespace

bool Atlas::ParseLine(const char* line, usize lineNo, std::string* err) {
    if (!*line || line[0] == '#') return true;

    std::vector<std::string> f;
    SplitTabs(line, f);
    if (f.empty() || f[0].empty()) return true;

    const std::string& verb = f[0];

    auto fail = [&](const char* why) {
        if (err) {
            char buf[192];
            std::snprintf(buf, sizeof(buf), "line %zu: %s (%s)", lineNo, why,
                          verb.c_str());
            *err = buf;
        }
        return false;
    };

    if (verb == "MAP") {
        if (f.size() < 4) return fail("MAP needs id, width, height");
        mapId_     = static_cast<u8>(ToI32(f[1]));
        mapWidth_  = ToI32(f[2]);
        mapHeight_ = ToI32(f[3]);
        return true;
    }

    if (verb == "REGION") {
        // REGION id kind flagsHex px py pz group name
        if (f.size() < 9) return fail("REGION needs 8 fields");
        Region r;
        r.id     = f[1];
        r.kind   = wm::RegionKindFromName(f[2].c_str());
        r.flags  = DecodeFlags(ToU32Hex(f[3]));
        r.center.x = ToI32(f[4]);
        r.center.y = ToI32(f[5]);
        r.center.z = static_cast<i8>(ToI32(f[6]));
        r.center.map = mapId_;
        r.group  = f[7];
        r.name   = f[8];
        regions_.push_back(std::move(r));
        return true;
    }

    if (verb == "RECT") {
        // RECT regionId x1 y1 x2 y2 -- always follows its REGION.
        if (f.size() < 6) return fail("RECT needs 5 fields");
        if (regions_.empty() || regions_.back().id != f[1])
            return fail("RECT does not belong to the preceding REGION");
        Rect rc;
        rc.x1 = ToI32(f[2]);
        rc.y1 = ToI32(f[3]);
        rc.x2 = ToI32(f[4]);
        rc.y2 = ToI32(f[5]);
        regions_.back().rects.push_back(rc);
        return true;
    }

    if (verb == "PLACE") {
        // PLACE id category regionId x y z radius services resources name
        if (f.size() < 11) return fail("PLACE needs 10 fields");
        Place p;
        p.id       = f[1];
        p.category = wm::PlaceCategoryFromName(f[2].c_str());
        p.regionId = f[3];
        p.position.x = ToI32(f[4]);
        p.position.y = ToI32(f[5]);
        p.position.z = static_cast<i8>(ToI32(f[6]));
        p.position.map = mapId_;
        p.radius   = ToI32(f[7]);
        if (p.radius <= 0) p.radius = 3;
        ParseCsvServices(f[8], p.services);
        ParseCsvResources(f[9], p.resources);
        p.name = f[10];
        places_.push_back(std::move(p));
        return true;
    }

    if (verb == "TRANSIT") {
        // TRANSIT id kind fx fy fz tx ty tz bidir label
        if (f.size() < 11) return fail("TRANSIT needs 10 fields");
        TransitNode t;
        t.id   = f[1];
        t.kind = wm::TransitKindFromName(f[2].c_str());
        t.from.x = ToI32(f[3]);
        t.from.y = ToI32(f[4]);
        t.from.z = static_cast<i8>(ToI32(f[5]));
        t.from.map = mapId_;
        t.to.x = ToI32(f[6]);
        t.to.y = ToI32(f[7]);
        t.to.z = static_cast<i8>(ToI32(f[8]));
        t.to.map = mapId_;
        t.bidirectional = ToI32(f[9]) != 0;
        t.label = f[10];
        transits_.push_back(std::move(t));
        return true;
    }

    // Unknown verbs are ignored, not fatal: a newer generator may add rows an
    // older client does not care about, and refusing to load the whole world
    // over one unrecognised line would be the worse failure.
    return true;
}

bool Atlas::LoadFromText(const char* text, std::string* err) {
    regions_.clear();
    places_.clear();
    transits_.clear();

    std::string line;
    usize lineNo = 0;
    for (const char* p = text;; ++p) {
        if (*p == '\n' || *p == 0) {
            ++lineNo;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!ParseLine(line.c_str(), lineNo, err)) return false;
            line.clear();
            if (*p == 0) break;
        } else {
            line.push_back(*p);
        }
    }
    if (regions_.empty()) {
        if (err) *err = "atlas holds no regions";
        return false;
    }
    return true;
}

bool Atlas::Load(const char* path, std::string* err) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        if (err) {
            char buf[320];
            std::snprintf(buf, sizeof(buf), "cannot open atlas '%s'", path);
            *err = buf;
        }
        return false;
    }
    std::string text;
    char chunk[8192];
    usize n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
        text.append(chunk, n);
    std::fclose(f);
    return LoadFromText(text.c_str(), err);
}

const Region* Atlas::RegionAt(i32 x, i32 y) const {
    const Region* best = nullptr;
    i64 bestArea = 0;
    for (const Region& r : regions_) {
        if (!r.Contains(x, y)) continue;
        const i64 area = r.Area();
        if (!best || area < bestArea) {
            best = &r;
            bestArea = area;
        }
    }
    return best;
}

const Region* Atlas::RegionById(const char* id) const {
    if (!id) return nullptr;
    for (const Region& r : regions_)
        if (EqualsNoCase(r.id, id)) return &r;
    return nullptr;
}

const Region* Atlas::FindRegion(const char* needle) const {
    if (!needle || !*needle) return nullptr;
    if (const Region* r = RegionById(needle)) return r;
    for (const Region& r : regions_)
        if (EqualsNoCase(r.name, needle)) return &r;

    // Substring, largest match wins: "Britain" should resolve to the town, not
    // to the first Britain shop interior the file happens to list.
    const std::string lower = Lower(needle);
    const Region* best = nullptr;
    i64 bestArea = -1;
    for (const Region& r : regions_) {
        if (r.kind == wm::RegionKind::World) continue;
        if (!ContainsNoCase(r.name, lower) && !ContainsNoCase(r.id, lower))
            continue;
        const i64 area = r.Area();
        if (area > bestArea) {
            best = &r;
            bestArea = area;
        }
    }
    return best;
}

const Place* Atlas::PlaceById(const char* id) const {
    if (!id) return nullptr;
    for (const Place& p : places_)
        if (EqualsNoCase(p.id, id)) return &p;
    return nullptr;
}

const Place* Atlas::FindPlace(const char* needle) const {
    if (!needle || !*needle) return nullptr;
    if (const Place* p = PlaceById(needle)) return p;
    for (const Place& p : places_)
        if (EqualsNoCase(p.name, needle)) return &p;
    const std::string lower = Lower(needle);
    for (const Place& p : places_)
        if (ContainsNoCase(p.name, lower)) return &p;
    for (const Place& p : places_)
        if (ContainsNoCase(p.id, lower)) return &p;
    return nullptr;
}

bool Atlas::PlaceIsGuarded(const wm::Place& p) const {
    if (const wm::Region* r = RegionById(p.regionId.c_str())) {
        if (r->flags.guarded) return true;
    }
    // A place can sit inside a guarded region without being FILED under it,
    // so fall back to geometry rather than to an optimistic guess.
    for (const wm::Region& r : regions_) {
        if (!r.flags.guarded) continue;
        if (r.Contains(p.position.x, p.position.y)) return true;
    }
    return false;
}

const Place* Atlas::NearestPlaceWithService(wm::Service s, i32 x, i32 y,
                                            i32 maxDist) const {
    // GUARDED FIRST, then distance. A shop in an unguarded field is not a
    // cheaper version of the same shop -- it is a different proposition, and
    // eight 15hp mages walked into one because this function ranked on
    // distance alone.
    //
    // A nearer unguarded shop still wins over NOTHING: the second pass runs
    // only when no guarded provider exists at all, so a service that simply
    // has no safe location stays reachable.
    const Place* best = nullptr;
    i32 bestD = 0;
    for (int pass = 0; pass < 2 && !best; ++pass) {
        const bool wantGuarded = (pass == 0);
        for (const Place& p : places_) {
            if (!p.Offers(s)) continue;
            if (wantGuarded && !PlaceIsGuarded(p)) continue;
            const i32 d = Chebyshev(x, y, p.position.x, p.position.y);
            if (maxDist > 0 && d > maxDist) continue;
            if (!best || d < bestD) { best = &p; bestD = d; }
        }
    }
    return best;
}

const Place* Atlas::NearestPlaceWithServiceSkipping(
    wm::Service s, i32 x, i32 y, const std::vector<std::string>& skipIds,
    i32 maxDist) const {
    const Place* best = nullptr;
    i32 bestD = 0;
    for (int pass = 0; pass < 2 && !best; ++pass) {
        const bool wantGuarded = (pass == 0);
        for (const Place& p : places_) {
            if (!p.Offers(s)) continue;
            if (wantGuarded && !PlaceIsGuarded(p)) continue;
            bool skipped = false;
            for (const std::string& id : skipIds) {
                if (p.id == id) { skipped = true; break; }
            }
            if (skipped) continue;
            const i32 d = Chebyshev(x, y, p.position.x, p.position.y);
            if (maxDist > 0 && d > maxDist) continue;
            if (!best || d < bestD) { best = &p; bestD = d; }
        }
    }
    return best;
}

const Place* Atlas::NearestPlaceWithResource(wm::ResourceKind r, i32 x, i32 y,
                                             i32 maxDist) const {
    const Place* best = nullptr;
    i32 bestD = 0;
    for (const Place& p : places_) {
        if (!p.Yields(r)) continue;
        const i32 d = Chebyshev(x, y, p.position.x, p.position.y);
        if (maxDist > 0 && d > maxDist) continue;
        if (!best || d < bestD) { best = &p; bestD = d; }
    }
    return best;
}

const Place* Atlas::NearestPlaceOfCategory(wm::PlaceCategory c, i32 x, i32 y,
                                           i32 maxDist) const {
    const Place* best = nullptr;
    i32 bestD = 0;
    for (const Place& p : places_) {
        if (p.category != c) continue;
        const i32 d = Chebyshev(x, y, p.position.x, p.position.y);
        if (maxDist > 0 && d > maxDist) continue;
        if (!best || d < bestD) { best = &p; bestD = d; }
    }
    return best;
}

const Place* Atlas::NearestPlaceWithServiceInRegion(wm::Service s,
                                                    const char* regionId,
                                                    i32 x, i32 y) const {
    const Region* region = regionId ? FindRegion(regionId) : nullptr;
    if (!region) return nullptr;
    const Place* best = nullptr;
    i32 bestD = 0;
    for (int pass = 0; pass < 2 && !best; ++pass) {
        const bool wantGuarded = (pass == 0);
        for (const Place& p : places_) {
            if (!p.Offers(s)) continue;
            // Either the generator filed the place under this region, or the
            // place simply falls inside its rectangles -- a shop just outside
            // the town AREADEF still belongs to the town for a traveller.
            if (!EqualsNoCase(p.regionId, region->id.c_str()) &&
                !region->Contains(p.position.x, p.position.y))
                continue;
            if (wantGuarded && !PlaceIsGuarded(p)) continue;
            const i32 d = Chebyshev(x, y, p.position.x, p.position.y);
            if (!best || d < bestD) { best = &p; bestD = d; }
        }
    }
    return best;
}

const TransitNode* Atlas::NearestTransit(wm::TransitKind kind, i32 x, i32 y,
                                         i32 maxDist) const {
    const TransitNode* best = nullptr;
    i32 bestD = 0;
    for (const TransitNode& t : transits_) {
        if (t.kind != kind) continue;
        const i32 d = Chebyshev(x, y, t.from.x, t.from.y);
        if (maxDist > 0 && d > maxDist) continue;
        if (!best || d < bestD) { best = &t; bestD = d; }
    }
    return best;
}

const TransitNode* Atlas::TransitById(const char* id) const {
    if (!id) return nullptr;
    for (const TransitNode& t : transits_)
        if (EqualsNoCase(t.id, id)) return &t;
    return nullptr;
}

void Atlas::TransitsNear(i32 x, i32 y, i32 radius,
                         std::vector<const wm::TransitNode*>& out) const {
    out.clear();
    for (const TransitNode& t : transits_) {
        if (Chebyshev(x, y, t.from.x, t.from.y) <= radius) out.push_back(&t);
    }
}

bool Atlas::AllowsRecallInto(i32 x, i32 y) const {
    const Region* r = RegionAt(x, y);
    return !r || !r->flags.BlocksRecallIn();
}

bool Atlas::AllowsRecallOutOf(i32 x, i32 y) const {
    const Region* r = RegionAt(x, y);
    return !r || !r->flags.BlocksRecallOut();
}

bool Atlas::AllowsGateAt(i32 x, i32 y) const {
    const Region* r = RegionAt(x, y);
    return !r || !r->flags.BlocksGate();
}

usize Atlas::CountPlacesWithService(wm::Service s) const {
    usize n = 0;
    for (const Place& p : places_)
        if (p.Offers(s)) ++n;
    return n;
}

} // namespace uo::world_atlas
