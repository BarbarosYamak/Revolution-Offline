// ---------------------------------------------------------------------------
// uo_atlasgen — derives the world atlas and the navigation grid from the
// shard's own data. Run once per Revolution data set, not per bot session.
//
//   uo_atlasgen --scripts <runtime/scripts> --mul <runtime/mul>
//               --out-atlas data/revolution_atlas.txt
//               --out-grid  data/revolution_navgrid.bin
//               [--skip-grid]
//
// Everything it emits is traceable to a file the shard reads at boot:
//
//   regions      maps/map0/map0_areas*.scp  (AREADEF)  + map0_rooms.scp (ROOMDEF)
//   teleporters  maps/map0/map0_teleports*.scp         (RES_TELEPORTERS)
//   moongates    functions/worldgen/decoration/moongates.scp (the shard's own
//                destination table, which is what its gate gump offers)
//   town starts  maps/map0/map0_starts.scp
//   services     functions/worldgen/spawns/felucca/Vendors_spawns_felucca.scp
//   resources    .../Reagents_*, WildLife_*, Graveyards_* spawner tables,
//                mine/dock AREADEF names, and forest density measured off the
//                client's own statics
//
// Nothing is invented. Where the shard has no data for something (there is no
// "lumber region" table anywhere), the value is measured from the client's MULs
// rather than guessed, and the derivation is named in the output header.
// ---------------------------------------------------------------------------

#include "uo/map.h"
#include "uo/tiledata.h"
#include "uo/types.h"
#include "uo/world.h"
#include "uo/world_model.h"
#include "world/Atlas.h"
#include "world/NavGrid.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <unordered_set>
#include <vector>

namespace {

using uo::i32;
using uo::i8;
using uo::u32;
using uo::u8;
using uo::usize;
namespace wm = uo::wm;

// --- small text helpers ----------------------------------------------------

std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string Trim(const std::string& s) {
    usize a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

// Strip a trailing `// comment`. Sphere scripts put them everywhere, including
// on the value lines we read.
std::string StripComment(const std::string& s) {
    const usize at = s.find("//");
    return at == std::string::npos ? s : s.substr(0, at);
}

bool Contains(const std::string& hay, const char* needle) {
    return Lower(hay).find(Lower(needle)) != std::string::npos;
}

std::string Slug(const std::string& in) {
    std::string out;
    bool lastUnderscore = true;   // suppress a leading underscore
    for (char c : in) {
        const char lc = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        const bool ok = (lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9');
        if (ok) {
            out.push_back(lc);
            lastUnderscore = false;
        } else if (!lastUnderscore) {
            out.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? std::string("x") : out;
}

std::vector<std::string> SplitCommas(const std::string& s) {
    std::vector<std::string> out;
    usize start = 0;
    for (;;) {
        const usize c = s.find(',', start);
        if (c == std::string::npos) {
            out.push_back(Trim(s.substr(start)));
            break;
        }
        out.push_back(Trim(s.substr(start, c - start)));
        start = c + 1;
    }
    return out;
}

i32 ToI32(const std::string& s) {
    return static_cast<i32>(std::strtol(Trim(s).c_str(), nullptr, 10));
}

bool ReadFileLines(const std::string& path, std::vector<std::string>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string all;
    char buf[16384];
    usize n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    std::fclose(f);

    std::string line;
    for (char c : all) {
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            out.push_back(line);
            line.clear();
        } else {
            line.push_back(c);
        }
    }
    if (!line.empty()) out.push_back(line);
    return true;
}

// --- output model ----------------------------------------------------------

constexpr u32 kFlagGuarded      = 0x0001;
constexpr u32 kFlagSafe         = 0x0002;
constexpr u32 kFlagUnderground  = 0x0004;
constexpr u32 kFlagNoRecallIn   = 0x0008;
constexpr u32 kFlagNoRecallOut  = 0x0010;
constexpr u32 kFlagNoGate       = 0x0020;
constexpr u32 kFlagNoTeleport   = 0x0040;
constexpr u32 kFlagAntiMagicAll = 0x0080;
constexpr u32 kFlagNoPvp        = 0x0100;

struct GenRegion {
    std::string id, name, group;
    wm::RegionKind kind = wm::RegionKind::Unknown;
    u32 flags = 0;
    i32 px = 0, py = 0;
    i8  pz = 0;
    std::vector<wm::Rect> rects;
    bool room = false;         // came from a ROOMDEF rather than an AREADEF

    long long Area() const {
        long long a = 0;
        for (const wm::Rect& r : rects) a += r.Area();
        return a;
    }
    bool Contains(i32 x, i32 y) const {
        for (const wm::Rect& r : rects)
            if (r.Contains(x, y)) return true;
        return false;
    }
};

struct GenPlace {
    std::string id, name, regionId;
    wm::PlaceCategory category = wm::PlaceCategory::Unknown;
    i32 x = 0, y = 0;
    i8  z = 0;
    i32 radius = 3;
    std::vector<wm::Service> services;
    std::vector<wm::ResourceKind> resources;
};

struct GenTransit {
    std::string id, label;
    wm::TransitKind kind = wm::TransitKind::Unknown;
    i32 fx = 0, fy = 0, tx = 0, ty = 0;
    i8  fz = 0, tz = 0;
    bool bidirectional = false;
};

struct Atlas {
    std::vector<GenRegion>  regions;
    std::vector<GenPlace>   places;
    std::vector<GenTransit> transits;
    std::unordered_set<std::string> usedPlaceIds;

    const GenRegion* RegionAt(i32 x, i32 y, bool skipRooms) const {
        const GenRegion* best = nullptr;
        long long bestArea = 0;
        for (const GenRegion& r : regions) {
            if (r.kind == wm::RegionKind::World) continue;
            if (skipRooms && r.room) continue;
            if (!r.Contains(x, y)) continue;
            const long long a = r.Area();
            if (!best || a < bestArea) { best = &r; bestArea = a; }
        }
        return best;
    }

    std::string UniqueId(const std::string& base) {
        std::string id = base;
        int n = 2;
        while (usedPlaceIds.count(id)) {
            char suffix[16];
            std::snprintf(suffix, sizeof(suffix), "_%d", n++);
            id = base + suffix;
        }
        usedPlaceIds.insert(id);
        return id;
    }

    void AddPlace(GenPlace p) {
        if (const GenRegion* r = RegionAt(p.x, p.y, /*skipRooms=*/true))
            p.regionId = r->id;
        p.id = UniqueId(p.id);
        places.push_back(std::move(p));
    }
};

// --- region parsing --------------------------------------------------------

u32 ParseRegionFlags(const std::string& value) {
    const std::string v = Lower(value);
    u32 f = 0;
    if (v.find("region_flag_guarded")      != std::string::npos) f |= kFlagGuarded;
    if (v.find("region_flag_safe")         != std::string::npos) f |= kFlagSafe;
    if (v.find("region_flag_underground")  != std::string::npos) f |= kFlagUnderground;
    if (v.find("region_antimagic_recall_in")  != std::string::npos) f |= kFlagNoRecallIn;
    if (v.find("region_antimagic_recall_out") != std::string::npos) f |= kFlagNoRecallOut;
    if (v.find("region_antimagic_gate")    != std::string::npos) f |= kFlagNoGate;
    if (v.find("region_antimagic_teleport")!= std::string::npos) f |= kFlagNoTeleport;
    if (v.find("region_antimagic_all")     != std::string::npos) f |= kFlagAntiMagicAll;
    if (v.find("region_flag_no_pvp")       != std::string::npos) f |= kFlagNoPvp;
    return f;
}

// Classify from the shard's own GROUP/NAME/FLAGS. Deliberately conservative:
// anything we cannot place confidently becomes Wilderness (outdoors) or
// Building (a ROOMDEF), never a made-up category.
wm::RegionKind ClassifyRegion(const GenRegion& r) {
    const std::string n = Lower(r.name);
    const std::string g = Lower(r.group);
    const std::string id = Lower(r.id);

    if (id == "a_world" || g == "allmap") {
        // ALLMAP holds the facet plus the ocean/terrain default regions; only
        // the full-map rectangle is the world itself.
        if (r.rects.size() == 1 && r.rects[0].x1 == 0 && r.rects[0].y1 == 0)
            return wm::RegionKind::World;
    }
    if (g == "moongates" || n.find("moongate") != std::string::npos)
        return wm::RegionKind::Moongate;
    if (g == "shrines") return wm::RegionKind::Shrine;
    if (n.find("graveyard") != std::string::npos ||
        n.find("cemetary")  != std::string::npos ||
        n.find("cemetery")  != std::string::npos ||
        n.find("city of the dead") != std::string::npos)
        return wm::RegionKind::Graveyard;
    if (g == "mines & caves" || n.find("cave") != std::string::npos)
        return wm::RegionKind::Cave;
    if (g == "green acres" || g == "jails" || n.find("jail") != std::string::npos)
        return wm::RegionKind::Special;

    // A ROOMDEF is an interior, full stop. This has to be decided BEFORE the
    // underground test: Scripts-X sets REGION_FLAG_UNDERGROUND on every indoor
    // room (it means "no sky", not "in a dungeon"), so every inn, shop and
    // abbey in Britannia would otherwise be filed as a dungeon -- including
    // the Empath Abbey, which is where Yew's bank is.
    if (r.room) return wm::RegionKind::Building;

    if (g == "other dungeons" || g == "t2a dungeons" ||
        g == "passages" || g == "t2a entrances & exits" ||
        n.find("dungeon") != std::string::npos)
        return wm::RegionKind::Dungeon;
    // Underground and NOT guarded is a real dungeon; underground AND guarded
    // is a town building the shard happened to write as an AREADEF.
    if ((r.flags & kFlagUnderground) != 0)
        return (r.flags & kFlagGuarded) ? wm::RegionKind::Building
                                        : wm::RegionKind::Dungeon;
    if ((r.flags & kFlagGuarded) != 0) return wm::RegionKind::Town;
    return wm::RegionKind::Wilderness;
}

void ParseAreaFile(const std::string& path, bool rooms, Atlas& atlas) {
    std::vector<std::string> lines;
    if (!ReadFileLines(path, lines)) {
        std::fprintf(stderr, "atlasgen: cannot read %s\n", path.c_str());
        return;
    }

    GenRegion cur;
    bool open = false;
    const char* header = rooms ? "[roomdef " : "[areadef ";

    auto flush = [&]() {
        if (!open) return;
        if (!cur.rects.empty()) {
            cur.room = rooms;
            cur.kind = ClassifyRegion(cur);
            atlas.regions.push_back(cur);
        }
        cur = GenRegion{};
        open = false;
    };

    for (const std::string& raw : lines) {
        const std::string line = Trim(raw);
        if (line.empty() || line[0] == '/') continue;

        const std::string lower = Lower(line);
        if (!line.empty() && line[0] == '[') {
            flush();
            if (lower.compare(0, std::strlen(header), header) == 0) {
                const usize close = line.find(']');
                if (close != std::string::npos) {
                    cur.id = Trim(line.substr(std::strlen(header),
                                              close - std::strlen(header)));
                    open = true;
                }
            }
            continue;
        }
        if (!open) continue;

        const usize eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Lower(Trim(line.substr(0, eq)));
        const std::string val = Trim(StripComment(line.substr(eq + 1)));

        if (key == "name") {
            cur.name = val;
        } else if (key == "group") {
            cur.group = val;
        } else if (key == "flags") {
            cur.flags = ParseRegionFlags(val);
        } else if (key == "p") {
            const std::vector<std::string> f = SplitCommas(val);
            if (f.size() >= 2) {
                cur.px = ToI32(f[0]);
                cur.py = ToI32(f[1]);
                if (f.size() >= 3) cur.pz = static_cast<i8>(ToI32(f[2]));
            }
        } else if (key == "rect") {
            const std::vector<std::string> f = SplitCommas(val);
            if (f.size() >= 4) {
                wm::Rect rc;
                rc.x1 = ToI32(f[0]);
                rc.y1 = ToI32(f[1]);
                rc.x2 = ToI32(f[2]);
                rc.y2 = ToI32(f[3]);
                // Sphere RECTs are half-open on the far edge in practice; the
                // engine's CRect uses x2/y2 exclusive (`CRect::IsInside`), so
                // pull them in by one to get an inclusive box.
                if (rc.x2 > rc.x1) --rc.x2;
                if (rc.y2 > rc.y1) --rc.y2;
                cur.rects.push_back(rc);
            }
        }
    }
    flush();
}

// --- teleporters -----------------------------------------------------------

// `x,y,z,map=x,y,z,map=defname`, one per line under [Teleporters].
void ParseTeleportFile(const std::string& path, Atlas& atlas) {
    std::vector<std::string> lines;
    if (!ReadFileLines(path, lines)) return;

    bool inSection = false;
    int index = 0;
    for (const std::string& raw : lines) {
        const std::string line = Trim(StripComment(raw));
        if (line.empty()) continue;
        if (line[0] == '[') {
            inSection = Lower(line).compare(0, 13, "[teleporters]") == 0;
            continue;
        }
        if (!inSection) continue;

        // Split on '=' into up to three parts.
        const usize e1 = line.find('=');
        if (e1 == std::string::npos) continue;
        const usize e2 = line.find('=', e1 + 1);
        if (e2 == std::string::npos) continue;

        const std::vector<std::string> from = SplitCommas(line.substr(0, e1));
        const std::vector<std::string> to =
            SplitCommas(line.substr(e1 + 1, e2 - e1 - 1));
        const std::string label = Trim(line.substr(e2 + 1));
        if (from.size() < 2 || to.size() < 2) continue;
        // Only map 0 matters here: the shard's other facets are disabled
        // (no mapN.mul in the Revolution client data).
        if (from.size() >= 4 && ToI32(from[3]) != 0) continue;
        if (to.size() >= 4 && ToI32(to[3]) != 0) continue;

        GenTransit t;
        char idbuf[32];
        std::snprintf(idbuf, sizeof(idbuf), "tp_%d", index++);
        t.id = idbuf;
        t.kind = wm::TransitKind::Teleporter;
        t.fx = ToI32(from[0]);
        t.fy = ToI32(from[1]);
        t.fz = from.size() >= 3 ? static_cast<i8>(ToI32(from[2])) : 0;
        t.tx = ToI32(to[0]);
        t.ty = ToI32(to[1]);
        t.tz = to.size() >= 3 ? static_cast<i8>(ToI32(to[2])) : 0;
        t.label = label;
        // One-way as written: the file lists the reverse pad separately when
        // one exists, and inventing a return trip that the shard does not have
        // would strand a bot at the far end.
        t.bidirectional = false;
        atlas.transits.push_back(std::move(t));
    }
}

// --- moongates -------------------------------------------------------------

struct MoongateEntry {
    i32 x = 0, y = 0;
    i8  z = 0;
    std::string name;
};

// `moongates_facet0_N  x,y,z,map,Name` in the shard's own decoration defs.
// This is the same table `place_moongates` builds the gates from and the same
// one `d_moongates` offers as destinations, so it is authoritative for both
// "where are the gates" and "what does the gump list".
std::vector<MoongateEntry> ParseMoongates(const std::string& path) {
    std::vector<MoongateEntry> out;
    std::vector<std::string> lines;
    if (!ReadFileLines(path, lines)) return out;

    for (const std::string& raw : lines) {
        const std::string line = Trim(raw);
        if (line.empty() || line[0] == '/') continue;
        const std::string lower = Lower(line);
        if (lower.compare(0, 18, "moongates_facet0_") != 0 &&
            lower.compare(0, 17, "moongates_facet0_") != 0)
            continue;
        // Reject the `_active` and `_hue` rows; only the destination rows have
        // a comma-separated point.
        if (lower.find("_active") != std::string::npos) continue;
        if (lower.find("_hue") != std::string::npos) continue;

        // key <whitespace> x,y,z,map,Name
        usize sp = line.find_first_of(" \t");
        if (sp == std::string::npos) continue;
        const std::string value = Trim(StripComment(line.substr(sp)));
        if (value.empty()) continue;
        const std::vector<std::string> f = SplitCommas(value);
        if (f.size() < 5) continue;
        if (ToI32(f[3]) != 0) continue;   // facet 0 only

        MoongateEntry e;
        e.x = ToI32(f[0]);
        e.y = ToI32(f[1]);
        e.z = static_cast<i8>(ToI32(f[2]));
        e.name = f[4];
        out.push_back(e);
    }
    return out;
}

// --- town starts -----------------------------------------------------------

// [STARTS 2]: repeating (town, location name, "x,y,z,map", cliloc).
void ParseStarts(const std::string& path, Atlas& atlas) {
    std::vector<std::string> lines;
    if (!ReadFileLines(path, lines)) return;

    std::vector<std::string> body;
    bool inSection = false;
    for (const std::string& raw : lines) {
        const std::string line = Trim(raw);
        if (line.empty() || line[0] == '/') continue;
        if (line[0] == '[') {
            const std::string lower = Lower(line);
            inSection = lower.compare(0, 7, "[starts") == 0;
            if (!inSection && lower == "[eof]") break;
            continue;
        }
        if (inSection) body.push_back(line);
    }

    for (usize i = 0; i + 2 < body.size(); i += 4) {
        const std::string& town = body[i];
        const std::string& spot = body[i + 1];
        const std::vector<std::string> f = SplitCommas(body[i + 2]);
        if (f.size() < 3) continue;
        if (f.size() >= 4 && ToI32(f[3]) != 0) continue;

        GenPlace p;
        p.id = Slug(town) + "_start";
        p.name = town + " - " + spot;
        p.category = wm::PlaceCategory::Inn;
        p.x = ToI32(f[0]);
        p.y = ToI32(f[1]);
        p.z = static_cast<i8>(ToI32(f[2]));
        p.radius = 4;
        p.services.push_back(wm::Service::Innkeeper);
        atlas.AddPlace(std::move(p));
    }
}

// --- spawner tables --------------------------------------------------------

struct SpawnerRow {
    std::vector<std::string> lists;   // argv[0..5], ':'-separated sub-lists
    i32 x = 0, y = 0;
    i8  z = 0;
    i32 walkRange = 0;
    i32 homeRange = 0;
};

// `f_create_spawner,L1,L2,L3,L4,L5,L6,X,Y,Z,facet,Min,Max,WalkRange,HomeRange,
//  SpawnID,C1..C6` -- the layout is documented in the shard's own
// `functions/worldgen/spawns/spawner_functions.scp`.
std::vector<SpawnerRow> ParseSpawners(const std::string& path) {
    std::vector<SpawnerRow> out;
    std::vector<std::string> lines;
    if (!ReadFileLines(path, lines)) return out;

    for (const std::string& raw : lines) {
        const std::string line = Trim(StripComment(raw));
        if (Lower(line).compare(0, 17, "f_create_spawner,") != 0) continue;
        const std::vector<std::string> f = SplitCommas(line);
        if (f.size() < 15) continue;

        SpawnerRow r;
        for (int i = 1; i <= 6; ++i) r.lists.push_back(f[static_cast<usize>(i)]);
        r.x = ToI32(f[7]);
        r.y = ToI32(f[8]);
        r.z = static_cast<i8>(ToI32(f[9]));
        r.walkRange = ToI32(f[13]);
        r.homeRange = ToI32(f[14]);
        out.push_back(std::move(r));
    }
    return out;
}

// The shard's NPC job defnames, mapped onto the services a bot asks for. Jobs
// with no traveller-visible service (waiter, beggar, town crier) map to None
// and are skipped rather than invented into GeneralVendor.
wm::Service ServiceForJob(const std::string& jobRaw) {
    const std::string j = Lower(Trim(jobRaw));
    if (j.empty()) return wm::Service::None;

    struct Row { const char* job; wm::Service svc; };
    static const Row kRows[] = {
        {"banker",              wm::Service::Banker},
        {"minter",              wm::Service::Banker},
        {"healer",              wm::Service::Healer},
        {"healerguildmaster",   wm::Service::Healer},
        {"wanderinghealer",     wm::Service::Healer},
        {"blacksmith",          wm::Service::Blacksmith},
        {"blacksmithguildmaster", wm::Service::Blacksmith},
        {"weaponsmith",         wm::Service::Blacksmith},
        {"armorer",             wm::Service::Blacksmith},
        {"alchemist",           wm::Service::Alchemist},
        {"mage",                wm::Service::Mage},
        {"mageguildmaster",     wm::Service::Mage},
        {"herbalist",           wm::Service::Alchemist},
        {"provisioner",         wm::Service::Provisioner},
        {"animaltrainer",       wm::Service::Stablemaster},
        {"stablemaster",        wm::Service::Stablemaster},
        {"veterinarian",        wm::Service::Veterinarian},
        {"tailor",              wm::Service::Tailor},
        {"weaver",              wm::Service::Tailor},
        {"tailorguildmaster",   wm::Service::Tailor},
        {"carpenter",           wm::Service::Carpenter},
        {"architect",           wm::Service::Carpenter},
        {"bowyer",              wm::Service::Bowyer},
        {"tinker",              wm::Service::Tinker},
        {"tinkerguildmaster",   wm::Service::Tinker},
        {"scribe",              wm::Service::Scribe},
        {"innkeeper",           wm::Service::Innkeeper},
        {"tavernkeeper",        wm::Service::Innkeeper},
        {"barkeeper",           wm::Service::Innkeeper},
        {"butcher",             wm::Service::Butcher},
        {"baker",               wm::Service::Baker},
        {"tanner",              wm::Service::Tanner},
        {"furtrader",           wm::Service::Tanner},
        {"jeweler",             wm::Service::Jeweler},
        {"shipwright",          wm::Service::Shipwright},
        {"mapmaker",            wm::Service::Mapmaker},
        {"fisherman",           wm::Service::Fisherman},
        {"cook",                wm::Service::Cook},
        {"miller",              wm::Service::Miller},
        {"cobbler",             wm::Service::Tailor},
    };
    for (const Row& r : kRows)
        if (j == r.job) return r.svc;
    return wm::Service::None;
}

wm::PlaceCategory CategoryForServices(const std::vector<wm::Service>& svc) {
    for (wm::Service s : svc) {
        if (s == wm::Service::Banker)       return wm::PlaceCategory::Bank;
        if (s == wm::Service::Healer)       return wm::PlaceCategory::Healer;
        if (s == wm::Service::Stablemaster) return wm::PlaceCategory::Stable;
    }
    for (wm::Service s : svc)
        if (s == wm::Service::Innkeeper) return wm::PlaceCategory::Inn;
    return wm::PlaceCategory::Shop;
}

void ParseVendorSpawns(const std::string& path, Atlas& atlas) {
    for (const SpawnerRow& row : ParseSpawners(path)) {
        std::vector<wm::Service> services;
        std::string primaryJob;
        for (const std::string& list : row.lists) {
            // A list slot may hold `a:b:c` alternates on one spawner.
            usize start = 0;
            for (;;) {
                const usize colon = list.find(':', start);
                const std::string job =
                    Trim(list.substr(start, colon == std::string::npos
                                                ? std::string::npos
                                                : colon - start));
                const wm::Service s = ServiceForJob(job);
                if (s != wm::Service::None) {
                    bool dup = false;
                    for (wm::Service e : services) dup = dup || (e == s);
                    if (!dup) services.push_back(s);
                    if (primaryJob.empty()) primaryJob = job;
                }
                if (colon == std::string::npos) break;
                start = colon + 1;
            }
        }
        if (services.empty()) continue;

        const GenRegion* region = atlas.RegionAt(row.x, row.y, true);
        const std::string town = region ? region->name : std::string("wilds");

        GenPlace p;
        p.category = CategoryForServices(services);
        p.services = services;
        p.x = row.x;
        p.y = row.y;
        p.z = row.z;
        // The spawner's own home range is how far the shard lets that NPC
        // wander, so it is exactly the radius within which "the banker" is
        // still at "the bank".
        p.radius = row.homeRange > 0 ? row.homeRange : 5;
        if (p.radius < 3) p.radius = 3;

        const char* catName = wm::PlaceCategoryName(p.category);
        p.id = Slug(town) + "_" +
               (p.category == wm::PlaceCategory::Shop ? Slug(primaryJob)
                                                      : std::string(catName));
        p.name = town + " " + primaryJob;
        atlas.AddPlace(std::move(p));
    }
}

void ParseResourceSpawns(const std::string& path, wm::ResourceKind kind,
                         const char* label, Atlas& atlas) {
    for (const SpawnerRow& row : ParseSpawners(path)) {
        const GenRegion* region = atlas.RegionAt(row.x, row.y, true);
        const std::string where = region ? region->name : std::string("wilds");

        GenPlace p;
        p.category = wm::PlaceCategory::ResourceArea;
        p.resources.push_back(kind);
        p.x = row.x;
        p.y = row.y;
        p.z = row.z;
        // Resource spawners carry a real wander radius (reagents use 200), and
        // that radius *is* the size of the harvestable area.
        p.radius = row.walkRange > 0 ? row.walkRange : 10;
        p.id = Slug(where) + "_" + Slug(label);
        p.name = where + " " + label;
        atlas.AddPlace(std::move(p));
    }
}

// --- region-derived places -------------------------------------------------

void DerivePlacesFromRegions(Atlas& atlas) {
    // Snapshot the region list first: AddPlace does not touch `regions`, but
    // iterating a container while appending to a sibling is the kind of thing
    // that rots later.
    const usize regionCount = atlas.regions.size();
    for (usize i = 0; i < regionCount; ++i) {
        const GenRegion& r = atlas.regions[i];
        if (r.kind == wm::RegionKind::World) continue;
        if (r.px == 0 && r.py == 0) continue;

        GenPlace p;
        p.x = r.px;
        p.y = r.py;
        p.z = r.pz;
        p.name = r.name.empty() ? r.id : r.name;

        const std::string n = Lower(r.name);
        if (r.kind == wm::RegionKind::Shrine) {
            p.category = wm::PlaceCategory::Shrine;
            p.radius = 6;
        } else if (r.kind == wm::RegionKind::Graveyard) {
            p.category = wm::PlaceCategory::Graveyard;
            p.radius = 12;
        } else if (n.find("mine") != std::string::npos ||
                   n.find("mining") != std::string::npos) {
            p.category = wm::PlaceCategory::ResourceArea;
            p.resources.push_back(wm::ResourceKind::Mining);
            p.radius = 20;
        } else if (n.find("dock") != std::string::npos) {
            p.category = wm::PlaceCategory::Dock;
            p.resources.push_back(wm::ResourceKind::Fishing);
            p.radius = 12;
        } else if (n.find("entrance") != std::string::npos &&
                   r.kind == wm::RegionKind::Dungeon) {
            p.category = wm::PlaceCategory::DungeonEntrance;
            p.radius = 8;
        } else if (r.kind == wm::RegionKind::Town && !r.room &&
                   r.Area() > 4000) {
            // A town's own AREADEF P is the shard's idea of its centre.
            p.category = wm::PlaceCategory::TownCenter;
            p.radius = 8;
        } else if (r.kind == wm::RegionKind::Cave) {
            p.category = wm::PlaceCategory::ResourceArea;
            p.resources.push_back(wm::ResourceKind::Mining);
            p.radius = 12;
        } else {
            continue;
        }

        p.id = Slug(p.name) + "_" +
               std::string(wm::PlaceCategoryName(p.category));
        atlas.AddPlace(std::move(p));
    }
}

// --- forest measurement ----------------------------------------------------

// Britannia has no "forest" table anywhere in the shard's data, so lumber
// areas are measured off the client's own statics: the navgrid already marks
// foliage-dense cells, and this collapses the densest 8x8-cell blocks
// (128x128 tiles) into one place each. Derived, reproducible, and it moves if
// the client data does.
void DeriveForests(const uo::navgrid::NavGrid& grid, Atlas& atlas) {
    constexpr i32 kBlockCells = 8;
    constexpr u32 kMinForestCells = 20;   // of 64 -- a wood, not a hedgerow

    struct Cluster { i32 cx, cy; u32 count; };
    std::vector<Cluster> clusters;

    usize forestCells = 0, waterCells = 0;
    for (i32 y = 0; y < static_cast<i32>(grid.CellsY()); ++y)
        for (i32 x = 0; x < static_cast<i32>(grid.CellsX()); ++x)
            if (const uo::navgrid::Cell* c = grid.At(x, y)) {
                if (c->flags & uo::navgrid::kCellForest) ++forestCells;
                if (c->flags & uo::navgrid::kCellWater)  ++waterCells;
            }
    std::fprintf(stderr, "atlasgen: %zu forest cells, %zu water cells\n",
                 forestCells, waterCells);

    for (i32 by = 0; by + kBlockCells <= static_cast<i32>(grid.CellsY());
         by += kBlockCells) {
        for (i32 bx = 0; bx + kBlockCells <= static_cast<i32>(grid.CellsX());
             bx += kBlockCells) {
            u32 forest = 0;
            i32 anchorX = 0, anchorY = 0;
            i8  anchorZ = 0;
            bool haveAnchor = false;
            for (i32 dy = 0; dy < kBlockCells; ++dy) {
                for (i32 dx = 0; dx < kBlockCells; ++dx) {
                    const uo::navgrid::Cell* c = grid.At(bx + dx, by + dy);
                    if (!c) continue;
                    if (!(c->flags & uo::navgrid::kCellForest)) continue;
                    ++forest;
                    if (!haveAnchor &&
                        (c->flags & uo::navgrid::kCellPassable)) {
                        grid.Anchor(bx + dx, by + dy, &anchorX, &anchorY,
                                    &anchorZ);
                        haveAnchor = true;
                    }
                }
            }
            if (forest < kMinForestCells || !haveAnchor) continue;
            clusters.push_back(Cluster{anchorX, anchorY, forest});
        }
    }

    std::sort(clusters.begin(), clusters.end(),
              [](const Cluster& a, const Cluster& b) { return a.count > b.count; });

    // Densest-first is the wrong answer on its own: Britannia's thickest
    // foliage is all in the southern jungle, so a global top-N would report
    // that the only woods on the continent are around Trinsic and leave a Yew
    // lumberjack with nowhere to go. Keep the best few clusters *per region*
    // instead, so every wooded area is represented at its own local density.
    constexpr usize kMaxPerRegion = 3;
    std::unordered_map<std::string, usize> perRegion;

    for (const Cluster& c : clusters) {
        const GenRegion* region = atlas.RegionAt(c.cx, c.cy, true);
        const std::string where = region ? region->name : std::string("wilds");
        usize& taken = perRegion[where];
        if (taken >= kMaxPerRegion) continue;
        ++taken;
        GenPlace p;
        p.category = wm::PlaceCategory::ResourceArea;
        p.resources.push_back(wm::ResourceKind::Lumber);
        p.x = c.cx;
        p.y = c.cy;
        p.radius = 48;
        p.id = Slug(where) + "_woods";
        p.name = where + " woods";
        atlas.AddPlace(std::move(p));
    }
}

// --- output ----------------------------------------------------------------

std::string JoinServices(const std::vector<wm::Service>& v) {
    std::string out;
    for (wm::Service s : v) {
        if (!out.empty()) out.push_back(',');
        out += wm::ServiceName(s);
    }
    return out;
}

std::string JoinResources(const std::vector<wm::ResourceKind>& v) {
    std::string out;
    for (wm::ResourceKind r : v) {
        if (!out.empty()) out.push_back(',');
        out += wm::ResourceName(r);
    }
    return out;
}

bool WriteAtlas(const std::string& path, const Atlas& atlas,
                i32 mapWidth, i32 mapHeight) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    std::fprintf(f,
        "# Revolution Offline world atlas -- GENERATED by uo_atlasgen.\n"
        "# Do not hand-edit: regenerate from the shard data instead.\n"
        "#\n"
        "# Sources, all read from runtime/scripts (Scripts-X as the shard boots it):\n"
        "#   REGION/RECT   maps/map0/map0_areas*.scp (AREADEF), map0_rooms.scp (ROOMDEF)\n"
        "#   TRANSIT tp_   maps/map0/map0_teleports*.scp  (RES_TELEPORTERS)\n"
        "#   TRANSIT mg_   functions/worldgen/decoration/moongates.scp\n"
        "#   PLACE  inns   maps/map0/map0_starts.scp\n"
        "#   PLACE  shops  functions/worldgen/spawns/felucca/Vendors_spawns_felucca.scp\n"
        "#   PLACE  resrc  Reagents_/WildLife_/Graveyards_ spawner tables, mine and\n"
        "#                 dock AREADEF names, and foliage density measured from the\n"
        "#                 Revolution client's own statics0.mul\n"
        "#\n"
        "# Fields are TAB separated. Coordinates are map 0 (Felucca) tiles.\n");
    std::fprintf(f, "MAP\t0\t%d\t%d\n", mapWidth, mapHeight);

    for (const GenRegion& r : atlas.regions) {
        std::fprintf(f, "REGION\t%s\t%s\t%X\t%d\t%d\t%d\t%s\t%s\n",
                     r.id.c_str(), wm::RegionKindName(r.kind), r.flags,
                     r.px, r.py, static_cast<int>(r.pz),
                     r.group.c_str(), r.name.c_str());
        for (const wm::Rect& rc : r.rects)
            std::fprintf(f, "RECT\t%s\t%d\t%d\t%d\t%d\n", r.id.c_str(),
                         rc.x1, rc.y1, rc.x2, rc.y2);
    }

    for (const GenPlace& p : atlas.places) {
        std::fprintf(f, "PLACE\t%s\t%s\t%s\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n",
                     p.id.c_str(), wm::PlaceCategoryName(p.category),
                     p.regionId.c_str(), p.x, p.y, static_cast<int>(p.z),
                     p.radius, JoinServices(p.services).c_str(),
                     JoinResources(p.resources).c_str(), p.name.c_str());
    }

    for (const GenTransit& t : atlas.transits) {
        std::fprintf(f, "TRANSIT\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n",
                     t.id.c_str(), wm::TransitKindName(t.kind),
                     t.fx, t.fy, static_cast<int>(t.fz),
                     t.tx, t.ty, static_cast<int>(t.tz),
                     t.bidirectional ? 1 : 0, t.label.c_str());
    }

    std::fclose(f);
    return true;
}

void ProgressDot(int pct, void*) {
    static int last = -1;
    if (pct / 5 == last) return;
    last = pct / 5;
    std::fprintf(stderr, "  navgrid %d%%\n", pct);
}

} // namespace

// `--probe x y` answers "what does the generated world think is here?" from
// the files the bot actually loads. Used to choose scenario coordinates with
// evidence instead of by eye, and to check a route by hand after the fact.
int ProbeMode(const std::string& atlasPath, const std::string& gridPath,
              const std::vector<std::pair<i32, i32>>& points) {
    uo::world_atlas::Atlas atlas;
    std::string err;
    if (!atlas.Load(atlasPath.c_str(), &err)) {
        std::fprintf(stderr, "probe: %s\n", err.c_str());
        return 1;
    }
    uo::navgrid::NavGrid grid;
    const bool haveGrid = grid.Load(gridPath.c_str());
    if (!haveGrid)
        std::fprintf(stderr, "probe: no navgrid at %s\n", gridPath.c_str());

    for (const auto& pt : points) {
        const i32 x = pt.first, y = pt.second;
        const wm::Region* r = atlas.RegionAt(x, y);
        std::printf("(%d,%d) region=%s (%s) kind=%s\n", x, y,
                    r ? r->name.c_str() : "<none>",
                    r ? r->id.c_str() : "-",
                    r ? wm::RegionKindName(r->kind) : "-");
        if (haveGrid) {
            const i32 cx = uo::navgrid::NavGrid::TileToCell(x);
            const i32 cy = uo::navgrid::NavGrid::TileToCell(y);
            const uo::navgrid::Cell* c = grid.At(cx, cy);
            if (!c) {
                std::printf("    cell (%d,%d) is off the grid\n", cx, cy);
            } else {
                i32 ax = 0, ay = 0;
                i8 az = 0;
                const bool anchored = grid.Anchor(cx, cy, &ax, &ay, &az);
                std::printf("    cell (%d,%d) passable=%d forest=%d water=%d",
                            cx, cy,
                            (c->flags & uo::navgrid::kCellPassable) ? 1 : 0,
                            (c->flags & uo::navgrid::kCellForest) ? 1 : 0,
                            (c->flags & uo::navgrid::kCellWater) ? 1 : 0);
                if (anchored)
                    std::printf(" anchor=(%d,%d,%d)", ax, ay,
                                static_cast<int>(az));
                std::printf("\n");
            }
        }
        if (const wm::Place* p =
                atlas.NearestPlaceOfCategory(wm::PlaceCategory::Bank, x, y))
            std::printf("    nearest bank: %s at (%d,%d)\n", p->name.c_str(),
                        p->position.x, p->position.y);
    }
    return 0;
}

int main(int argc, char** argv) {
    std::string scriptsDir, mulDir;
    std::string outAtlas = "revolution_atlas.txt";
    std::string outGrid  = "revolution_navgrid.bin";
    bool skipGrid = false;
    std::vector<std::pair<i32, i32>> probes;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : std::string();
        };
        if (a == "--scripts")         scriptsDir = next();
        else if (a == "--mul")        mulDir = next();
        else if (a == "--out-atlas")  outAtlas = next();
        else if (a == "--out-grid")   outGrid = next();
        else if (a == "--skip-grid")  skipGrid = true;
        else if (a == "--probe") {
            const i32 px = ToI32(next());
            const i32 py = ToI32(next());
            probes.push_back({px, py});
        }
        else {
            std::fprintf(stderr,
                "usage: uo_atlasgen --scripts <dir> [--mul <dir>]\n"
                "                   [--out-atlas <file>] [--out-grid <file>]\n"
                "                   [--skip-grid]\n"
                "       uo_atlasgen --out-atlas <file> --out-grid <file>\n"
                "                   --probe <x> <y> [--probe <x> <y>]...\n");
            return 2;
        }
    }
    if (!probes.empty()) return ProbeMode(outAtlas, outGrid, probes);
    if (scriptsDir.empty()) {
        std::fprintf(stderr, "atlasgen: --scripts is required\n");
        return 2;
    }
    if (!scriptsDir.empty() && scriptsDir.back() != '/' &&
        scriptsDir.back() != '\\')
        scriptsDir.push_back('/');
    if (!mulDir.empty() && mulDir.back() != '/' && mulDir.back() != '\\')
        mulDir.push_back('/');

    Atlas atlas;

    const char* kAreaFiles[] = {
        "maps/map0/map0_areas.scp",
        "maps/map0/map0_areas_ml.scp",
        "maps/map0/map0_areas_sa.scp",
        "maps/map0/map0_areas_hs.scp",
    };
    for (const char* rel : kAreaFiles)
        ParseAreaFile(scriptsDir + rel, /*rooms=*/false, atlas);
    ParseAreaFile(scriptsDir + "maps/map0/map0_rooms.scp", /*rooms=*/true, atlas);
    std::fprintf(stderr, "atlasgen: %zu regions\n", atlas.regions.size());

    const char* kTeleportFiles[] = {
        "maps/map0/map0_teleports.scp",
        "maps/map0/map0_teleports_ml.scp",
        "maps/map0/map0_teleports_hs.scp",
        "maps/map0/map0_teleports_tol.scp",
    };
    for (const char* rel : kTeleportFiles)
        ParseTeleportFile(scriptsDir + rel, atlas);
    std::fprintf(stderr, "atlasgen: %zu teleporters\n", atlas.transits.size());

    // Moongates: a place per gate, plus the full destination mesh the gump
    // offers. Every gate reaches every other gate in one hop, which is what
    // `d_moongates` does -- it is a destination list, not a ring.
    const std::vector<MoongateEntry> gates =
        ParseMoongates(scriptsDir + "functions/worldgen/decoration/moongates.scp");
    for (const MoongateEntry& g : gates) {
        GenPlace p;
        p.id = "moongate_" + Slug(g.name);
        p.name = g.name + " Moongate";
        p.category = wm::PlaceCategory::Moongate;
        p.x = g.x;
        p.y = g.y;
        p.z = g.z;
        p.radius = 2;      // d_moongates refuses beyond distance 2
        atlas.AddPlace(std::move(p));
    }
    for (usize a = 0; a < gates.size(); ++a) {
        for (usize b = 0; b < gates.size(); ++b) {
            if (a == b) continue;
            GenTransit t;
            t.id = "mg_" + Slug(gates[a].name) + "__" + Slug(gates[b].name);
            t.kind = wm::TransitKind::Moongate;
            t.fx = gates[a].x; t.fy = gates[a].y; t.fz = gates[a].z;
            t.tx = gates[b].x; t.ty = gates[b].y; t.tz = gates[b].z;
            t.label = gates[b].name;     // exactly what the gump lists
            atlas.transits.push_back(std::move(t));
        }
    }
    std::fprintf(stderr, "atlasgen: %zu moongates\n", gates.size());

    ParseStarts(scriptsDir + "maps/map0/map0_starts.scp", atlas);
    ParseVendorSpawns(
        scriptsDir + "functions/worldgen/spawns/felucca/Vendors_spawns_felucca.scp",
        atlas);
    ParseResourceSpawns(
        scriptsDir + "functions/worldgen/spawns/felucca/Reagents_spawns_felucca.scp",
        wm::ResourceKind::Reagents, "reagents", atlas);
    ParseResourceSpawns(
        scriptsDir + "functions/worldgen/spawns/felucca/WildLife_spawns_felucca.scp",
        wm::ResourceKind::Hunting, "hunting", atlas);
    DerivePlacesFromRegions(atlas);
    std::fprintf(stderr, "atlasgen: %zu places before forests\n",
                 atlas.places.size());

    i32 mapWidth = 7168, mapHeight = 4096;

    if (!skipGrid) {
        if (mulDir.empty()) {
            std::fprintf(stderr,
                "atlasgen: --mul is required unless --skip-grid\n");
            return 2;
        }
        uo::tiledata::TileDataLoader td;
        if (!td.Load((mulDir + "tiledata.mul").c_str())) {
            std::fprintf(stderr, "atlasgen: cannot open tiledata.mul\n");
            return 1;
        }
        uo::map::Map worldMap;
        if (!worldMap.Open((mulDir + "map0.mul").c_str(),
                           (mulDir + "staidx0.mul").c_str(),
                           (mulDir + "statics0.mul").c_str())) {
            std::fprintf(stderr, "atlasgen: cannot open map0\n");
            return 1;
        }
        mapWidth  = static_cast<i32>(worldMap.WidthCells());
        mapHeight = static_cast<i32>(worldMap.HeightCells());
        std::fprintf(stderr, "atlasgen: map0 is %dx%d tiles\n",
                     mapWidth, mapHeight);

        uo::world::World world(td, worldMap);
        // Doors are opened at run time by the walker, so a doorway is not a
        // wall as far as world topology is concerned. Saying otherwise here
        // would carve towns into disconnected islands.
        world.SetAcceptDoors(true);

        uo::navgrid::NavGrid grid;
        if (!grid.Build(worldMap, world, td, ProgressDot, nullptr)) {
            std::fprintf(stderr, "atlasgen: navgrid build failed\n");
            return 1;
        }
        std::fprintf(stderr, "atlasgen: navgrid %ux%u cells, %zu passable\n",
                     grid.CellsX(), grid.CellsY(), grid.PassableCells());
        std::fprintf(stderr, "atlasgen: measuring cell crossings\n");
        if (!grid.BuildEdges(world, ProgressDot, nullptr)) {
            std::fprintf(stderr, "atlasgen: edge pass failed\n");
            return 1;
        }
        if (!grid.Save(outGrid.c_str())) {
            std::fprintf(stderr, "atlasgen: cannot write %s\n", outGrid.c_str());
            return 1;
        }
        DeriveForests(grid, atlas);
    }

    std::fprintf(stderr, "atlasgen: %zu places, %zu transits\n",
                 atlas.places.size(), atlas.transits.size());

    if (!WriteAtlas(outAtlas, atlas, mapWidth, mapHeight)) {
        std::fprintf(stderr, "atlasgen: cannot write %s\n", outAtlas.c_str());
        return 1;
    }
    std::fprintf(stderr, "atlasgen: wrote %s\n", outAtlas.c_str());
    return 0;
}
