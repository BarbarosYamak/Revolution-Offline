#include "RunnerInternal.h"

#include "world/ServiceSelection.h"

// The life layer links no world code, so it keeps its own copy of the cap
// (life::kStrandedFromHomeTiles). This is where the two are held together:
// past the planned-tile cap the service picker will not send a character
// anywhere, so past it there is nothing at home it can still reach.
static_assert(uo::life::kStrandedFromHomeTiles ==
                  uo::world_atlas::kMaxServiceTripTiles,
              "stranded-from-home line drifted from the service trip cap");

namespace uo::life {
namespace runner_detail {


std::unordered_map<std::string, double>& SeededCreatureDanger() {
    static std::unordered_map<std::string, double> table;
    return table;
}

void LoadSeededCreatureDanger(const std::string& dataDir) {
    std::unordered_map<std::string, double>& t = SeededCreatureDanger();
    if (!t.empty()) return;
    std::FILE* f = std::fopen((dataDir + "/revolution_creatures.tsv").c_str(), "rb");
    if (!f) return;
    char line[512];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }          // header
        std::string row(line);
        // defname 	 name 	 danger 	 ...
        const usize t1 = row.find('	');
        if (t1 == std::string::npos) continue;
        const usize t2 = row.find('	', t1 + 1);
        if (t2 == std::string::npos) continue;
        const usize t3 = row.find('	', t2 + 1);
        const std::string name = row.substr(t1 + 1, t2 - t1 - 1);
        const std::string dg =
            row.substr(t2 + 1, (t3 == std::string::npos ? row.size() : t3) - t2 - 1);
        if (name.empty()) continue;
        std::string key;
        for (char c : name)
            key.push_back(static_cast<char>(std::tolower(
                static_cast<unsigned char>(c))));
        t[key] = std::atof(dg.c_str());
        // ... and the taming requirement, which is the LAST column.
        const usize lastTab = row.find_last_of('	');
        if (lastTab != std::string::npos && lastTab > t2) {
            const double tam = std::atof(row.c_str() + lastTab + 1);
            if (tam >= 0.0) SeededTaming()[key] = tam;
        }
    }
    std::fclose(f);
}

// The TAMING requirement from the same table, or -1 for a creature that
// cannot be tamed at all. 109 of the 450 carry one.
std::unordered_map<std::string, double>& SeededTaming() {
    static std::unordered_map<std::string, double> table;
    return table;
}

std::vector<Pasture>& Pastures() {
    static std::vector<Pasture> table;
    return table;
}

void LoadPastures(const std::string& dataDir) {
    std::vector<Pasture>& t = Pastures();
    if (!t.empty()) return;
    std::FILE* f =
        std::fopen((dataDir + "/revolution_pastures.tsv").c_str(), "rb");
    if (!f) return;
    char line[256];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }              // header
        Pasture p;
        int mapId = 0;
        // x \t y \t map \t count \t radius \t label
        if (std::sscanf(line, "%d\t%d\t%d\t%d\t%d", &p.x, &p.y, &mapId,
                        &p.count, &p.radius) != 5)
            continue;
        if (mapId != 0) continue;   // the bots only ever play map 0
        t.push_back(p);
    }
    std::fclose(f);
}

std::vector<TameCluster>& Tamables() {
    static std::vector<TameCluster> table;
    return table;
}

void LoadTamables(const std::string& dataDir) {
    std::vector<TameCluster>& t = Tamables();
    if (!t.empty()) return;
    std::FILE* f =
        std::fopen((dataDir + "/revolution_tamables.tsv").c_str(), "rb");
    if (!f) return;
    char line[512];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }              // header
        // x \t y \t map \t count \t radius \t label \t defname \t taming_req
        std::vector<std::string> col;
        const std::string row(line);
        usize start = 0;
        while (start <= row.size()) {
            const usize tab = row.find('\t', start);
            if (tab == std::string::npos) {
                std::string last = row.substr(start);
                while (!last.empty() &&
                       (last.back() == '\n' || last.back() == '\r'))
                    last.pop_back();
                col.push_back(last);
                break;
            }
            col.push_back(row.substr(start, tab - start));
            start = tab + 1;
        }
        if (col.size() < 8) continue;
        if (std::atoi(col[2].c_str()) != 0) continue;   // bots play map 0 only
        TameCluster c;
        c.x = std::atoi(col[0].c_str());
        c.y = std::atoi(col[1].c_str());
        c.count = std::atoi(col[3].c_str());
        c.radius = std::atoi(col[4].c_str());
        c.label = col[5];
        c.req = std::atof(col[7].c_str());
        t.push_back(c);
    }
    std::fclose(f);
}

std::string Fmt2(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

// WHICH ERRAND ACTUALLY MAKES A THING THIS LIFE PRODUCES.
//
// Only consulted once market::RouteForInput has already said SelfProduce --
// i.e. the item is in this character's own `produces` list -- so this is
// choosing between that character's own goals, never inventing a capability.
// The prefixes are the same defname families SupplierTradeFor above matches
// on, and each maps to the goal that already exists for it: ore is dug (Mine),
// ingots are smelted from ore (Smelt, which chains back to Mine through
// NeedOre), fish are caught (Fish), timber is chopped (GatherLogs). Anything
// else a profession produces, it produces at a craft menu.
GoalKind ProducingGoalFor(const std::string& item) {
    if (item.rfind("i_ore_", 0) == 0)   return GoalKind::Mine;
    if (item.rfind("i_ingot_", 0) == 0) return GoalKind::Smelt;
    if (item.rfind("i_fish", 0) == 0)   return GoalKind::Fish;
    if (item == "i_log" || item == "i_board") return GoalKind::GatherLogs;
    // THE TEXTILE CHAIN IS A STATION CHAIN, NOT A MENU CRAFT. A ball of yarn
    // comes off a spinning wheel, a bolt off a loom, and cloth off a pair of
    // scissors -- all three are Provenance::WorldProcessed in Production.cpp
    // (:56, :67, :71) with no skill and no craft gump anywhere. Left routed to
    // Craft, DoCraft could only answer REFUSE_MISSING_RECIPE: Aelia did it
    // three times in 130 ms for i_cloth_bolt while MAKE_CLOTH, the goal that
    // owns the loom, sat unpicked (artifacts/domakecloth_shear_to_target_
    // 2026-09-02.md, defect 2). Exactly Draver's i_ingot_iron/SMELT case.
    if (item == "i_yarn_ball" || item == "i_cloth_bolt" || item == "i_cloth")
        return GoalKind::MakeCloth;
    return GoalKind::Craft;
}

// --- where a miner is allowed to work, and where a wind-down runs to -------

const wm::Place* PickAllowedMine(const world_atlas::Atlas* atlas,
                                 const char** outId) {
    if (outId) *outId = "";
    if (!atlas || !atlas->Ready()) return nullptr;
    for (const char* id : kMinocMinePlaceIds) {
        const wm::Place* p = atlas->PlaceById(id);
        // Yields(Mining) is not decoration: the allow-list is written by hand
        // and the atlas is regenerated, so a row that stopped being a mine
        // must stop being a destination rather than send a miner to stand on
        // it swinging at nothing.
        if (!p || !p->Yields(wm::ResourceKind::Mining)) continue;
        if (outId) *outId = id;
        return p;
    }
    return nullptr;
}

const wm::Place* NearestGuardedPlace(const world_atlas::Atlas* atlas, i32 x,
                                     i32 y) {
    if (!atlas || !atlas->Ready()) return nullptr;
    const wm::Place* best = nullptr;
    i32 bestDist = 0;
    for (const wm::Place& p : atlas->Places()) {
        if (!atlas->PlaceIsGuarded(p)) continue;
        const i32 d = TileDist(p.position.x, p.position.y, x, y);
        if (!best || d < bestDist) { best = &p; bestDist = d; }
    }
    return best;
}

namespace {

// How near a public moongate a town has to be before it counts as somewhere
// the errand system would send anyone. MEASURED, not chosen: the shortest
// distance from each town AREADEF's own rectangles to the nearest moongate
// entry tile in data/revolution_atlas.txt is Jhelom 33, Skara Brae 41,
// Trinsic 50, Minoc 74, Magincia 90, Yew 92, Moonglow 100, Vesper 119,
// Britain 200, Cove 459 -- and then it jumps to Sea Market 805 and The
// Heartwood 2,440, which are the two towns on this map nothing can walk or
// gate to. 600 sits in that gap.
constexpr i32 kErrandTownGateTiles = 600;

i32 RectDist(const wm::Rect& r, i32 x, i32 y) {
    const i32 dx = std::max({r.x1 - x, 0, x - r.x2});
    const i32 dy = std::max({r.y1 - y, 0, y - r.y2});
    return std::max(dx, dy);
}

// Nearest public moongate ENTRY to any part of `region`, or -1 when the atlas
// carries no moongate rows at all.
i32 GateDistanceToRegion(const world_atlas::Atlas& atlas,
                         const wm::Region& region) {
    i32 best = -1;
    for (const wm::TransitNode& t : atlas.Transits()) {
        if (t.kind != wm::TransitKind::Moongate) continue;
        for (const wm::Rect& r : region.rects) {
            const i32 d = RectDist(r, t.from.x, t.from.y);
            if (best < 0 || d < best) best = d;
        }
    }
    return best;
}

// The bank a market hub resolves to: NearestPlaceWithServiceInRegion
// anchored on the region's own AREADEF centre, never on the caller's live
// position -- see ResolveMarketHub below for why that anchor matters.
const wm::Place* RegionBank(const world_atlas::Atlas& atlas, const char* city) {
    const wm::Region* r = atlas.FindRegion(city);
    if (!r) return nullptr;
    return atlas.NearestPlaceWithServiceInRegion(wm::Service::Banker, city,
                                                 r->center.x, r->center.y);
}

}  // namespace

i32 TravelTilesWithGates(const world_atlas::Atlas& atlas, i32 fromX, i32 fromY,
                         i32 toX, i32 toY) {
    i32 best = TileDist(fromX, fromY, toX, toY);
    // One hop only, and only over gate pairs the atlas really lists. This
    // shard's public network is a complete graph over ten pads (every
    // mg_<city>__<city> row in data/revolution_atlas.txt), so one hop is
    // every journey the network can shorten; a second hop would only ever
    // add walking.
    for (const wm::TransitNode& t : atlas.Transits()) {
        if (t.kind != wm::TransitKind::Moongate) continue;
        const i32 walkIn = TileDist(fromX, fromY, t.from.x, t.from.y);
        if (walkIn >= best) continue;          // already worse than not going
        const i32 cost = walkIn + TileDist(t.to.x, t.to.y, toX, toY);
        if (cost < best) best = cost;
    }
    return best;
}

bool OnErrandTownGround(const world_atlas::Atlas& atlas, i32 x, i32 y) {
    if (!atlas.Ready()) return false;
    for (const wm::Region& r : atlas.Regions()) {
        if (r.kind != wm::RegionKind::Town) continue;
        if (!r.flags.guarded) continue;
        if (!r.Contains(x, y)) continue;
        const i32 gate = GateDistanceToRegion(atlas, r);
        if (gate >= 0 && gate <= kErrandTownGateTiles) return true;
    }
    return false;
}

HomeReturn ResolveHomeReturn(const world_atlas::Atlas* atlas,
                             const std::string& homeCity, i32 x, i32 y) {
    HomeReturn h;
    if (!atlas || !atlas->Ready() || homeCity.empty()) return h;
    // FindRegion widens id -> exact NAME -> substring, so a state.homeCity of
    // "Britain" finds AREADEF a_townBritain without the life layer knowing
    // the defname.
    const wm::Region* home = atlas->FindRegion(homeCity.c_str());
    if (!home) return h;
    h.region = home;
    h.inHome = home->Contains(x, y);
    const wm::Place* bank = atlas->NearestPlaceWithServiceInRegion(
        wm::Service::Banker, homeCity.c_str(), x, y);
    if (bank) {
        h.x = bank->position.x;
        h.y = bank->position.y;
        h.arriveRadius = 5;
        h.label = bank->name.c_str();
    } else {
        h.x = home->center.x;
        h.y = home->center.y;
        h.arriveRadius = 8;
        h.label = home->name.c_str();
    }
    h.directTiles = TileDist(h.x, h.y, x, y);
    h.tiles = TravelTilesWithGates(*atlas, x, y, h.x, h.y);
    h.onErrandGround = OnErrandTownGround(*atlas, x, y);
    h.resolved = true;
    return h;
}

// S7: THE MARKET HAS TWO HUBS, not one home-town rendezvous. See the
// declaration (RunnerInternal.h) for the owner ruling and rationale. Each
// hub is resolved the same way S6 resolved a home-town rendezvous --
// RegionBank, anchored on the region's own AREADEF centre, never on the
// caller's live position, so every character asking "where is the Britain
// hub" gets the SAME Britain bank (britain_bank_2, by that anchor) rather
// than whichever one happens to be nearer to wherever it is standing today.
MarketHubPick ResolveMarketHub(const world_atlas::Atlas* atlas, i32 x, i32 y) {
    MarketHubPick pick;
    if (!atlas || !atlas->Ready()) return pick;

    const wm::Place* britain = RegionBank(*atlas, "Britain");
    const wm::Place* minoc   = RegionBank(*atlas, "Minoc");
    if (!britain && !minoc) return pick;   // the atlas knows neither hub

    if (!britain || !minoc) {
        // Only one hub exists in this atlas at all: nothing to compare, so
        // it wins by default rather than leaving the caller with no market.
        const wm::Place* only = britain ? britain : minoc;
        pick.place      = only;
        pick.placeId    = only->id;
        pick.tiles      = TravelTilesWithGates(*atlas, x, y, only->position.x,
                                               only->position.y);
        pick.otherTiles = pick.tiles;
        pick.otherLabel = "the only hub the atlas knows";
        pick.resolved   = true;
        return pick;
    }

    const i32 tilesBritain = TravelTilesWithGates(
        *atlas, x, y, britain->position.x, britain->position.y);
    const i32 tilesMinoc = TravelTilesWithGates(
        *atlas, x, y, minoc->position.x, minoc->position.y);

    // A tie favours Britain -- the hub by population (S6's own reasoning),
    // not an arbitrary pick.
    const bool britainWins = tilesBritain <= tilesMinoc;
    pick.place      = britainWins ? britain : minoc;
    pick.placeId    = pick.place->id;
    pick.tiles      = britainWins ? tilesBritain : tilesMinoc;
    pick.otherTiles = britainWins ? tilesMinoc : tilesBritain;
    pick.otherLabel = (britainWins ? minoc : britain)->name;
    pick.resolved   = true;
    return pick;
}

}  // namespace runner_detail
}  // namespace uo::life
