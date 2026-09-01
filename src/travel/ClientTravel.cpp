// ---------------------------------------------------------------------------
// Client travel layer (M2.5) — semantic destinations, the journey driver, the
// war/peace watchdog and the generic-gump plumbing a public moongate needs.
//
// Layering rule, and the reason this file is separate from Client.cpp: nothing
// here builds a packet except the gump reply and nothing here moves the
// character except through the existing public actions. Walking a leg is
// `ActionGoto`, which is the same call a scenario makes, which ends at
// `SubmitStep` -- still the only place a 0x02 is ever built.
// ---------------------------------------------------------------------------

#include "Client.h"

#include "uo/travel_mode.h"
#include "uo/combat_policy.h"
#include "uo/rules.h"
#include "uo/world.h"
#include "world/GuardZoneAdvance.h"
#include "world/MiningAdvance.h"
#include "world/ServiceSelection.h"

#include "uo/endian.h"

#include <cstdio>
#include <cstring>

namespace uo {

namespace {

i32 Chebyshev(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

// How often the journey is fed a position sample. Stuck detection counts
// samples, so the rate is part of the timeout: 12 no-progress samples at
// 500 ms is six seconds of standing still, which is well past any transient
// blocker and well short of a hang.
constexpr i64 kTravelSampleMs = 500;

// A leg counts as walked when the tile A* stops within this many tiles of the
// waypoint. Cell anchors are approximations by construction, and insisting on
// an exact tile would turn a good route into a stuck loop.
constexpr i32 kLegArriveSlack = 3;

// How long a live sighting of a service NPC is trusted over the atlas.
constexpr i64 kServiceSightingMaxAgeMs = 120000;

// The furthest a single leg may ask the tile A* to walk. The planner's own
// budget is 40 tiles; anything past this is not a long leg, it is a stale plan.
constexpr i32 kMaxSaneLegTiles = 160;

// Two characters are on the same floor if their z differ by less than this.
// A UO storey is about 20 z-units; Sphere's own speech and shop-keyword checks
// are three-dimensional, which is why standing above a vendor is standing
// nowhere useful.
constexpr i32 kSameFloorZ = 12;

// Escape attempts before a trip reports the character as sealed in. Three is
// enough to try the doorway, the next room and the street; more than that and
// the honest answer is that this character cannot get out on its own.
constexpr int kMaxTravelEscapes = 3;

} // namespace

// ---------------------------------------------------------------------------
// World knowledge
// ---------------------------------------------------------------------------

bool Client::EnsureWorldKnowledge() {
    if (!world_knowledge_)
        world_knowledge_ =
            world_atlas::AcquireSharedWorld(cfg_.atlasPath, cfg_.navgridPath);
    return world_knowledge_ && world_knowledge_->ok;
}

bool Client::WorldKnowledgeReady() { return EnsureWorldKnowledge(); }

const char* Client::WorldKnowledgeError() {
    EnsureWorldKnowledge();
    if (!world_knowledge_) return "world knowledge not initialised";
    return world_knowledge_->ok ? "" : world_knowledge_->error.c_str();
}

const wm::Region* Client::CurrentRegion() const {
    if (!world_knowledge_ || !world_knowledge_->ok) return nullptr;
    return world_knowledge_->atlas.RegionAt(playerX_, playerY_);
}

// The atlas itself, for callers that need more than one narrow lookup out of
// it (life::SeedNewbieKnowledge takes the whole thing so it stays Client-free
// and unit-testable against the real data file). World DATA, not personal
// knowledge -- the same shard-wide atlas every session shares (SharedWorld.h).
const world_atlas::Atlas* Client::WorldAtlas() const {
    if (!world_knowledge_ || !world_knowledge_->ok) return nullptr;
    return &world_knowledge_->atlas;
}

const wm::Place* Client::NearestServicePlace(wm::Service s) const {
    if (!world_knowledge_ || !world_knowledge_->ok) return nullptr;
    return world_knowledge_->atlas.NearestPlaceWithService(s, playerX_,
                                                           playerY_);
}

const wm::Place* Client::NearestResourcePlace(wm::ResourceKind r) const {
    if (!world_knowledge_ || !world_knowledge_->ok) return nullptr;
    return world_knowledge_->atlas.NearestPlaceWithResource(r, playerX_,
                                                            playerY_);
}

void Client::ResourcePlacesNear(wm::ResourceKind r, i32 x, i32 y,
                                std::vector<const wm::Place*>& out) const {
    out.clear();
    if (!world_knowledge_ || !world_knowledge_->ok) return;
    for (const wm::Place& p : world_knowledge_->atlas.Places()) {
        if (p.Yields(r)) out.push_back(&p);
    }
    std::sort(out.begin(), out.end(),
              [x, y](const wm::Place* a, const wm::Place* b) {
                  return Chebyshev(x, y, a->position.x, a->position.y) <
                         Chebyshev(x, y, b->position.x, b->position.y);
              });
}

bool Client::WithinPlace(const char* nameOrId) const {
    if (!world_knowledge_ || !world_knowledge_->ok) return false;
    const wm::Place* p = world_knowledge_->atlas.FindPlace(nameOrId);
    if (!p) return false;
    return Chebyshev(playerX_, playerY_, p->position.x, p->position.y) <=
           p->radius;
}

const wm::Place* Client::KnownPlace(const char* nameOrId) const {
    if (!world_knowledge_ || !world_knowledge_->ok) return nullptr;
    return world_knowledge_->atlas.FindPlace(nameOrId);
}

bool Client::PlaceGuarded(const wm::Place& p) const {
    if (!world_knowledge_ || !world_knowledge_->ok) return false;
    return world_knowledge_->atlas.PlaceIsGuarded(p);
}

bool Client::WithinRegion(const char* nameOrId) const {
    if (!world_knowledge_ || !world_knowledge_->ok) return false;
    const wm::Region* r = world_knowledge_->atlas.FindRegion(nameOrId);
    return r && r->Contains(playerX_, playerY_);
}

// ---------------------------------------------------------------------------
// Starting a journey
// ---------------------------------------------------------------------------

bool Client::TravelBegin(const char* label, i32 x, i32 y, i32 arriveRadius,
                         bool hasZ, i8 z) {
    if (!IsInWorld()) {
        travelFailure_ = "not in world";
        return false;
    }
    if (!EnsureWorldKnowledge()) {
        travelFailure_ = WorldKnowledgeError();
        return false;
    }
    // Travelling is a peaceful intent. Saying so here is what makes every
    // journey drop a stale war mode without each caller remembering to.
    war_.OnPeacefulIntent(NowMs());

    travelSucceeded_ = false;
    travelFailure_.clear();
    travelLabel_ = label ? label : "";
    travelWalkOutstanding_ = false;
    // A public gate can open its dialog merely by stepping on it.  A new
    // journey must never inherit the previous journey's choice: otherwise a
    // food trip that last selected Magincia can make the next mining trip
    // select Magincia at the Minoc gate.
    travelGateSerial_ = 0;
    travelGateDestination_.clear();
    travelStartedDead_ = IsDead();
    // The destination's own floor, where the world data knows one. Britannia's
    // shops are two and three storeys and the shard's spawner rows carry the z
    // the NPC actually stands on; without it a bot can "arrive" on the balcony.
    travelHasGoalZ_ = hasZ;
    travelGoalZ_ = z;
    travelEscapes_ = 0;
    travelEscapeTried_.clear();
    travelAvoidPads_.clear();
    // The journey gets the destination's floor too (M3.9): with the goal z in
    // its own arrival test, standing under a bridge-deck destination is "not
    // there yet" and keeps being worked at, instead of a false 2-D arrival
    // that TravelFinish then had to convert into an unrecoverable failure.
    journey_.Begin(travelLabel_.c_str(), x, y, arriveRadius, NowMs(), hasZ, z);
    travelLastSampleMs_ = 0;
    // One recall per journey. TravelPlanRoute runs again on every replan, and
    // the first version recalled from THERE -- so a single trip to Britain cast
    // Recall three times and the shard started warning "The recall rune is
    // starting to fade". Runes wear out and are eventually destroyed, so a
    // planner that re-casts on each replan quietly consumes the character's
    // property. Reset here, where a genuinely new journey starts.
    runebookRecallDone_ = false;

    LogInfo("[travel] %s -> (%d,%d) r=%d from (%d,%d)\n",
            travelLabel_.c_str(), x, y, arriveRadius, playerX_, playerY_);
    char ev[192];
    std::snprintf(ev, sizeof(ev), "label='%s' target=(%d,%d) radius=%d from=(%d,%d)",
                  travelLabel_.c_str(), x, y, arriveRadius, playerX_, playerY_);
    LogEvent("travel_start", ev);
    return true;
}

bool Client::TravelToPoint(i32 x, i32 y, i32 arriveRadius, const char* label) {
    travelEntitySerial_ = 0;
    return TravelBegin(label && *label ? label : "point", x, y, arriveRadius);
}

bool Client::TravelToPlace(const char* nameOrId) {
    if (!EnsureWorldKnowledge()) {
        travelFailure_ = WorldKnowledgeError();
        return false;
    }
    const wm::Place* p = world_knowledge_->atlas.FindPlace(nameOrId);
    if (!p) {
        travelFailure_ = "no such place";
        LogWarn("[travel] no place matches '%s'\n", nameOrId ? nameOrId : "");
        return false;
    }
    travelEntitySerial_ = 0;
    return TravelBegin(p->name.c_str(), p->position.x, p->position.y,
                       p->radius, /*hasZ=*/true, p->position.z);
}

bool Client::TravelToRegion(const char* nameOrId) {
    if (!EnsureWorldKnowledge()) {
        travelFailure_ = WorldKnowledgeError();
        return false;
    }
    const wm::Region* r = world_knowledge_->atlas.FindRegion(nameOrId);
    if (!r) {
        travelFailure_ = "no such region";
        LogWarn("[travel] no region matches '%s'\n", nameOrId ? nameOrId : "");
        return false;
    }
    // A region is an area, not a point. The shard's own AREADEF P is its
    // idea of the middle of the place, so that is where "go to Britain" means.
    travelEntitySerial_ = 0;
    return TravelBegin(r->name.c_str(), r->center.x, r->center.y, 8);
}

bool Client::TravelToService(wm::Service s, const char* regionHint) {
    static const std::vector<u32> kNoSerials;
    return TravelToServiceSkipping(s, regionHint, kNoSerials, nullptr);
}

// What the SERVER considers mineable, mirrored exactly rather than guessed.
//
// Sphere's gate is CWorldMap::CheckNaturalResource(pt, IT_ROCK) ->
// IsItemTypeNear(pt, IT_ROCK, 0, false) (Source-X CWorldMap.cpp:52): distance
// zero, so the STRUCK TILE ITSELF must be rock-typed. FindItemTypeNearby
// (CWorldMap.cpp:663-795) answers yes for exactly two things we can see from
// the muls:
//   1. land whose terrain id GetTerrainItemType maps to t_rock -- and that map
//      is the shard's own [TYPEDEF t_rock] TERRAIN ranges,
//      runtime/scripts/types/types_terrain.scp:26-47 (loaded into
//      g_World.m_TileTypes by CItemTypeDef::r_LoadVal);
//   2. a static whose ITEMDEF resolves to TYPE=t_rock
//      (CWorldMap.cpp:781-785, CItemBase::IsType).
// Everything else -- water, roads, bridges, trees -- is refused with "Try
// mining elsewhere." (DEFMSG_MINING_1, CCharSkill.cpp:1452). "Unwalkable" was
// the previous heuristic and it failed both ways: water is unwalkable but not
// rock (the Minoc bridge incident), and cave floors are walkable AND rock
// (terrain 0x245-0x259 is in the t_rock list; cave-floor statics 0x53B-0x54F
// are TYPE=t_rock via DUPEITEM i_floor_cave, i_ground_tiles.scp:7-119).

namespace {

// [TYPEDEF t_rock] TERRAIN ranges, types_terrain.scp:27-46. The file's last
// entry (0453b-0454f) is item-id space (statics), covered by the static table
// below instead.
struct IdRange { u16 lo, hi; };
constexpr IdRange kRockTerrain[] = {
    {0x0DC, 0x0E7}, {0x0EC, 0x0F7}, {0x0FC, 0x107}, {0x10C, 0x117},
    {0x11E, 0x129}, {0x141, 0x144}, {0x1D3, 0x1DA}, {0x1DC, 0x1E7},
    {0x1EC, 0x1EF}, {0x21F, 0x243}, {0x245, 0x259}, {0x262, 0x265},
    {0x6CD, 0x6DD}, {0x6EB, 0x6FE}, {0x709, 0x720}, {0x727, 0x73E},
    {0x745, 0x75C}, {0x7BD, 0x7D4}, {0x7EC, 0x7F1}, {0x834, 0x839},
};

// Every ITEMDEF the shard scripts resolve to TYPE=t_rock, DUPEITEMs followed
// (cave floors/edges i_ground_tiles.scp:7-203, stalagmites :205+, boulders
// :4389+, plus i_offset.scp and i_unsorted.scp entries). Enumerated by
// parsing runtime/scripts, not recalled from generic UO memory.
constexpr IdRange kRockStatics[] = {
    {0x040B, 0x041E}, {0x053B, 0x054F}, {0x0551, 0x0553}, {0x056A, 0x056A},
    {0x08E0, 0x08EA}, {0x2F62, 0x2FB5}, {0x3341, 0x3351}, {0x3421, 0x3424},
    {0x3426, 0x3439}, {0x3486, 0x348F}, {0x34AC, 0x34B4}, {0x3539, 0x353C},
    {0x3DB6, 0x3DB7}, {0x3F28, 0x3F28},
};

bool InRanges(u16 id, const IdRange* r, usize n) {
    for (usize i = 0; i < n; ++i)
        if (id >= r[i].lo && id <= r[i].hi) return true;
    return false;
}

}  // namespace

bool Client::RockAt(i32 tx, i32 ty, i8* z, u16* graphic) {
    if (tx < 0 || ty < 0) return false;
    if (!EnsureWorldLoaded() || !world_ || !worldMap_ || !tileData_)
        return false;
    map::LandCell cell{};
    if (worldMap_->ReadCell(static_cast<u32>(tx), static_cast<u32>(ty),
                            &cell) &&
        InRanges(cell.tileId, kRockTerrain,
                 sizeof(kRockTerrain) / sizeof(kRockTerrain[0]))) {
        if (z) *z = cell.z;
        if (graphic) *graphic = 0;
        return true;
    }
    // Rock STATICS: cave floors and the like. Same reasoning as WaterAt's wet
    // statics -- the server checks the static's scripted type, so an id
    // whitelist derived from those same scripts is the faithful mirror.
    std::vector<world::StaticHit> hits;
    world_->CollectStatics(tx, ty, 0, hits);
    for (const world::StaticHit& h : hits) {
        if (!InRanges(h.itemId, kRockStatics,
                      sizeof(kRockStatics) / sizeof(kRockStatics[0])))
            continue;
        if (z) *z = h.z;
        if (graphic) *graphic = h.itemId;
        return true;
    }
    return false;
}

bool Client::NearestMiningSpot(i32 x, i32 y, i8 z, int radius,
                               MiningSpot* out,
                               const std::vector<std::pair<i32, i32>>*
                                   exclude,
                               bool* allGuarded) {
    if (allGuarded) *allGuarded = false;
    if (!out) return false;
    if (!EnsureWorldLoaded() || !world_ || !worldMap_ || !tileData_)
        return false;
    if (radius < 1) radius = 1;
    // Atlas is a separate load from the map/statics above; harmless to ask
    // again if a prior caller already brought it up.
    EnsureWorldKnowledge();

    auto excluded = [&](i32 tx, i32 ty) -> bool {
        if (!exclude) return false;
        for (const auto& d : *exclude)
            if (d.first == tx && d.second == ty) return true;
        return false;
    };

    // OWNER RULE: no gathering inside guarded zones -- the same rule
    // NearestTree enforces for chopping. A rock whose own tile sits inside a
    // guarded region is not a candidate at all.
    auto guarded = [&](i32 tx, i32 ty) -> bool {
        if (!(world_knowledge_ && world_knowledge_->ok)) return false;
        const wm::Region* r = world_knowledge_->atlas.RegionAt(tx, ty);
        return r && r->flags.guarded;
    };
    bool sawCandidate = false;   // rock found by RockAt, before the guard test
    bool allWereGuarded = true;

    // ROCK AT EYE LEVEL FIRST. Being rock is not enough: the strike must
    // also pass CanSeeLOS(m_Act_p) (CCharSkill.cpp:1442-1444), and LOS is
    // 3D. Live at the Minoc mine mouth the nearest rock by ring order was
    // the cliff tile whose land z is 34 over the z=0 path -- "You have no
    // line of sight to that location", every time -- while the mineable cave
    // floor sat one tile away at the character's own z. So the first sweep
    // only accepts rock whose surface is within kMineLosZ of the caller's z
    // (the scale of RESOURCE_Z_CHECK=8 in the engine's own resource search,
    // CWorldMap.cpp:355, doubled for slopes); the second sweep takes any
    // rock at all, since a far stand tile may sit at the rock's own level
    // and see it fine -- the caller's dead-list absorbs a wrong guess.
    constexpr int kMineLosZ = 16;
    for (int pass = 0; pass < 2; ++pass) {
    // Nearest ring first, and rings start at r=1: the engine refuses a target
    // under 1 tile off (DEFMSG_MINING_CLOSE, CCharSkill.cpp:1432), so the
    // tile under our own feet is never a target -- even standing on a cave
    // floor a character strikes the floor BESIDE itself.
    for (int r = 1; r <= radius; ++r) {
        for (i32 dy = -r; dy <= r; ++dy) {
            for (i32 dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const i32 rx = x + dx, ry = y + dy;
                if (excluded(rx, ry)) continue;
                i8 rz = 0;
                u16 gfx = 0;
                if (!RockAt(rx, ry, &rz, &gfx)) continue;
                // Pass 1 is the exhaustive, no-LOS-restriction sweep, so it is
                // the one that gets to say "every candidate here was guarded"
                // -- pass 0's LOS rejects are not guard rejects and would
                // otherwise be miscounted against the forest.
                if (pass == 1) sawCandidate = true;
                if (guarded(rx, ry)) continue;
                if (pass == 1) allWereGuarded = false;
                if (pass == 0 && std::abs((int)rz - (int)z) > kMineLosZ)
                    continue;
                // Within striking range of the ORIGIN (RANGE=2,
                // skill45_mining.scp): the origin itself is the stand -- but
                // only if it can be stood on. When the caller scans from its
                // own feet that is trivially true; a roaming caller may pass
                // a jittered origin that landed inside a wall, and handing
                // that back as a stand tile would aim A* at an unwalkable
                // goal (the exact failure FishingSpot's vetting exists for).
                if (r <= 2 && TileIsWalkable(x, y, z)) {
                    out->standX = x;
                    out->standY = y;
                    out->rockX = rx;
                    out->rockY = ry;
                    out->rockZ = rz;
                    out->rockGraphic = gfx;
                    return true;
                }
                // Further out: the spot is only useful with somewhere legal
                // to swing FROM. Adjacent (all 8 ways -- a diagonal stand is
                // distance 1 under the shard's Chebyshev DistanceFormula=0,
                // sphere.ini:1055) and QueryCell-walkable, the same vetting
                // FishingSpot learned the hard way: a stand A* rejects kills
                // every walk aimed at it.
                bool haveStand = false;
                int bestD = 0;
                i32 bsx = 0, bsy = 0;
                for (int ny = -1; ny <= 1; ++ny) {
                    for (int nx = -1; nx <= 1; ++nx) {
                        if (!nx && !ny) continue;
                        const i32 sx = rx + nx, sy = ry + ny;
                        if (sx < 0 || sy < 0) continue;
                        if (!TileIsWalkable(sx, sy, rz)) continue;
                        const int d = std::max(std::abs(sx - x),
                                               std::abs(sy - y));
                        if (haveStand && d >= bestD) continue;
                        haveStand = true;
                        bestD = d;
                        bsx = sx;
                        bsy = sy;
                    }
                }
                if (!haveStand) continue;   // a face with no footing
                out->standX = bsx;
                out->standY = bsy;
                out->rockX = rx;
                out->rockY = ry;
                out->rockZ = rz;
                out->rockGraphic = gfx;
                return true;
            }
        }
    }
    }
    if (allGuarded) *allGuarded = sawCandidate && allWereGuarded;
    return false;
}

// How far a single deeper-advance walks into a cave before DoMine rescans.
// Bounded so an advance is a step, not a leap at ground nothing has looked
// at yet -- kMineScanRadius (24, Runner.cpp) then comfortably covers the
// newly-reached ground on the next scan. Three of these covers Minoc Mine
// 1's full depth (its RECT reaches 27 tiles from the south mouth) with room
// to spare.
constexpr i32 kMineAdvanceStep = 20;

bool Client::DeeperMiningTarget(i32 curX, i32 curY, i32* outX,
                                i32* outY) const {
    if (!(world_knowledge_ && world_knowledge_->ok)) return false;
    // Resolve the mining PLACE the same way TravelToResource does, then walk
    // its regionId to the REGION -- more reliable than RegionAt(curX,curY),
    // since TravelToResource's own arrival radius can leave the character on
    // the wrong side of a RECT boundary from the exact point it was aiming
    // at.
    const wm::Place* p = world_knowledge_->atlas.NearestPlaceWithResource(
        wm::ResourceKind::Mining, curX, curY);
    if (!p || p->regionId.empty()) return false;
    const wm::Region* r = world_knowledge_->atlas.RegionById(
        p->regionId.c_str());
    // ONLY CAVES HAVE A MOUTH TO BE PICKED CLEAN AT. Open-air mountainside
    // rock (Wilderness/Unknown regions) has no entrance geometry for this to
    // reason about; the ordinary small jitter DoMine already does is the
    // right roam there.
    if (!r || r->kind != wm::RegionKind::Cave) return false;
    return world_atlas::DeeperMiningPoint(*r, curX, curY, kMineAdvanceStep,
                                          outX, outY);
}

// How far a single "step out of the guard line" walks before DoGatherLogs
// rescans. Sized for a town AREADEF rather than a cave mouth -- Britain's own
// RECT (data/revolution_atlas.txt, a_townBritain) runs roughly 260 tiles
// across, so a handful of these clears it.
constexpr i32 kGuardZoneStepLimit = 40;

bool Client::StepOutOfGuardZone(i32 curX, i32 curY, i32* outX,
                                i32* outY) const {
    if (!(world_knowledge_ && world_knowledge_->ok)) return false;
    const wm::Region* r = world_knowledge_->atlas.RegionAt(curX, curY);
    if (!r || !r->flags.guarded) return false;
    return world_atlas::StepOutOfGuardedRegion(*r, curX, curY,
                                               kGuardZoneStepLimit, outX,
                                               outY);
}

bool Client::TileIsWalkable(i32 x, i32 y, i8 fromZ) const {
    if (!world_ || x < 0 || y < 0) return false;
    world::WalkQuery q{};
    q.x = static_cast<u32>(x);
    q.y = static_cast<u32>(y);
    q.fromZ = fromZ;
    q.maxStepUp = 127;
    q.maxStepDown = 127;
    return world_->QueryCell(q).walkable;
}

i32 Client::DistanceToResource(wm::ResourceKind r) const {
    if (!world_knowledge_) return -1;
    const wm::Place* p =
        world_knowledge_->atlas.NearestPlaceWithResource(r, playerX_, playerY_);
    if (!p) return -1;
    const i32 dx = p->position.x - playerX_;
    const i32 dy = p->position.y - playerY_;
    const i32 ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    const i32 toCentre = ax > ay ? ax : ay;
    // TO THE EDGE, NOT THE CENTRE. A resource area is a REGION, not a point:
    // Minoc's mining places carry radius 20, so a character at its boundary is
    // 20 tiles from the recorded position and very much at work. Measuring to
    // the middle made Corwyn "48 tiles from ore" while standing in the mining
    // district.
    const i32 edge = toCentre - p->radius;
    return edge < 0 ? 0 : edge;
}

bool Client::MiningInteriorTarget(i32 nearX, i32 nearY, i32* outX,
                                  i32* outY) const {
    if (!outX || !outY || !(world_knowledge_ && world_knowledge_->ok))
        return false;
    const wm::Place* p = world_knowledge_->atlas.NearestPlaceWithResource(
        wm::ResourceKind::Mining, nearX, nearY);
    if (!p || p->regionId.empty()) return false;
    const wm::Region* r = world_knowledge_->atlas.RegionById(
        p->regionId.c_str());
    if (!r || r->kind != wm::RegionKind::Cave || r->rects.empty()) return false;

    // A cave AREADEF's P commonly marks its entrance.  Choose the centre of
    // its largest declared interior instead of that edge point.  It keeps the
    // target away from doors and cliff faces, while remaining wholly within a
    // real cave RECT rather than inventing a coordinate.
    const wm::Rect* largest = &r->rects[0];
    i64 largestArea = -1;
    for (const wm::Rect& rect : r->rects) {
        const i64 w = static_cast<i64>(rect.x2) - rect.x1 + 1;
        const i64 h = static_cast<i64>(rect.y2) - rect.y1 + 1;
        const i64 area = w * h;
        if (area > largestArea) {
            largestArea = area;
            largest = &rect;
        }
    }
    *outX = (largest->x1 + largest->x2) / 2;
    *outY = (largest->y1 + largest->y2) / 2;
    return true;
}

bool Client::WithinMiningRegion(i32 nearX, i32 nearY, i32 x, i32 y) const {
    if (!(world_knowledge_ && world_knowledge_->ok)) return false;
    const wm::Place* p = world_knowledge_->atlas.NearestPlaceWithResource(
        wm::ResourceKind::Mining, nearX, nearY);
    if (!p || p->regionId.empty()) return false;
    const wm::Region* r = world_knowledge_->atlas.RegionById(
        p->regionId.c_str());
    if (!r || r->kind != wm::RegionKind::Cave) return false;
    return r->Contains(x, y);
}

bool Client::TravelToServiceSkipping(wm::Service s, const char* regionHint,
                                     const std::vector<u32>& skipSerials,
                                     std::vector<std::string>* skipPlaceIds,
                                     bool farOk) {
    if (!EnsureWorldKnowledge()) {
        travelFailure_ = WorldKnowledgeError();
        return false;
    }

    // Live state beats stored state: if this character has actually seen a
    // provider of this service recently, go to where it saw one rather than to
    // where the shard's spawner table says the shop is.
    if (const travel::ServiceSighting* seen =
            knowledge_.RecentService(s, NowMs(), kServiceSightingMaxAgeMs)) {
        bool skipped = false;
        for (u32 sk : skipSerials) {
            if (sk == seen->serial) { skipped = true; break; }
        }
        if (skipped) {
            // The one this character has actually seen is the one it has
            // already given up on. Fall through to the atlas rather than walk
            // back to it -- otherwise a sighting pins the character to a
            // single NPC for the whole session.
            goto try_atlas;
        }
        // If the NPC is still in view, select a real stand tile around it.
        // Aiming the route at the occupied NPC tile makes A* report no path
        // for vendors behind counters even when the customer side is open.
        i32 liveX = 0, liveY = 0;
        if (MobilePosition(seen->serial, &liveX, &liveY))
            return TravelToEntity(seen->serial, 2);
        travelEntitySerial_ = seen->serial;
        travelEntityWithin_ = 2;
        char label[96];
        std::snprintf(label, sizeof(label), "%s (seen)", wm::ServiceName(s));
        return TravelBegin(label, seen->x, seen->y, 2, /*hasZ=*/true,
                           seen->z);
    }

try_atlas:
    // HOME FIRST, then anywhere. A region hint that finds nothing must not
    // strand the character: a mage living in Moonglow still needs a banker
    // when it is standing in Britain.
    const wm::Place* p = nullptr;
    const std::vector<std::string> noPlaces;
    const std::vector<std::string>& skipPlaces =
        skipPlaceIds ? *skipPlaceIds : noPlaces;
    if (regionHint && *regionHint && skipPlaces.empty()) {
        p = world_knowledge_->atlas.NearestPlaceWithServiceInRegion(
                s, regionHint, playerX_, playerY_);
        // AN ARMOURY IS NOT A SMITHY (see below) -- a region-scoped pick is
        // not exempt from that rule either. Fall through to the ranked
        // search rather than settle for it.
        if (p && s == wm::Service::Blacksmith &&
            p->id.find("armorer") != std::string::npos) {
            p = nullptr;
        }
    }
    // AN ARMOURY IS NOT A SMITHY. The atlas files both under `blacksmith`,
    // and in Minoc the armorer at 2533,572 is 30 tiles nearer than the real
    // smithy -- so a smelting errand went there every single time, to a lone
    // forge with no walkable tile beside it. "do not go that armorer"
    // (project owner, 2026-08-29, after asking twice for The Forgery).
    //
    // Built once here rather than skipped by the caller, so it applies to
    // every city, not just the one that was noticed -- and only when a real
    // smithy exists somewhere; an armoury is still better than nothing.
    std::vector<std::string> effectiveSkip = skipPlaces;
    if (s == wm::Service::Blacksmith) {
        bool anyNonArmoury = false;
        for (const wm::Place& pl : world_knowledge_->atlas.Places()) {
            if (!pl.Offers(s)) continue;
            bool already = false;
            for (const std::string& id : skipPlaces)
                if (id == pl.id) { already = true; break; }
            if (already) continue;
            if (pl.id.find("armorer") == std::string::npos) {
                anyNonArmoury = true;
                break;
            }
        }
        if (anyNonArmoury) {
            for (const wm::Place& pl : world_knowledge_->atlas.Places()) {
                if (pl.Offers(s) && pl.id.find("armorer") != std::string::npos)
                    effectiveSkip.push_back(pl.id);
            }
        }
    }
    if (!p) {
        // RANKED BY REAL TRIP COST, not raw map distance. The candidate the
        // atlas calls "nearest" can be a shop with no walkable ground beside
        // it or one three moongates away; PickServicePlace tries several
        // distance-sorted candidates through the actual route planner and
        // prefers fewer transit hops, capping the trip at ~1200 tiles unless
        // `farOk` says the errand demands going further
        // (docs/CRAFTER_RUN_2026_08_30.md defect 4: a Minoc smith was sent to
        // "Sea Market blacksmith" -- no walkable ground -- then to "Papua
        // weaponsmith", 904 tiles and three gates into the Lost Lands, while
        // Minoc's own smithy went unvisited).
        if (world_knowledge_->planner) {
            std::vector<world_atlas::ServiceRejection> rejections;
            const world_atlas::ServicePick pick = world_atlas::PickServicePlace(
                world_knowledge_->atlas, *world_knowledge_->planner, s,
                playerX_, playerY_, effectiveSkip, farOk, &rejections);
            for (const world_atlas::ServiceRejection& rej : rejections) {
                LogInfo("[travel] place: skipping %s -- %s\n",
                        rej.place->name.c_str(), rej.reason.c_str());
            }
            p = pick.place;
            if (p && pick.exceededCap) {
                LogInfo("[travel] place: %s is %d tiles / %zu gate(s) -- past "
                        "the usual trip budget, but nothing closer offers "
                        "%s\n",
                        p->name.c_str(), pick.estimatedTiles,
                        pick.transitHops, wm::ServiceName(s));
            }
        } else {
            // No route planner (navgrid missing) -- fall back to plain
            // distance, same as before this policy existed.
            p = world_knowledge_->atlas.NearestPlaceWithServiceSkipping(
                s, playerX_, playerY_, effectiveSkip);
        }
        if (p && skipPlaceIds) {
            // Armouries stepped over this call stay skipped next time too.
            *skipPlaceIds = effectiveSkip;
        }
    }
    if (!p) {
        travelFailure_ = "no known provider of that service";
        LogWarn("[travel] no place offers %s%s%s\n", wm::ServiceName(s),
                regionHint && *regionHint ? " in " : "",
                regionHint ? regionHint : "");
        return false;
    }
    travelEntitySerial_ = 0;
    // Record which shop this was, so a caller that strikes out here asks
    // for a different one next time instead of walking the same road again.
    if (skipPlaceIds) skipPlaceIds->push_back(p->id);
    return TravelBegin(p->name.c_str(), p->position.x, p->position.y,
                       p->radius, /*hasZ=*/true, p->position.z);
}

bool Client::TravelToUnexploredPlace(const std::vector<std::string>& seen,
                                     std::string* chosenId) {
    if (!EnsureWorldKnowledge()) {
        travelFailure_ = WorldKnowledgeError();
        return false;
    }
    // Nearest place not already known. Nearest rather than random so a
    // character explores outward from where it is instead of criss-crossing
    // the map, and so the walk stays short enough to finish inside a goal.
    const wm::Place* best = nullptr;
    i64 bestD = 0;
    for (const wm::Place& p : world_knowledge_->atlas.Places()) {
        bool known = false;
        for (const std::string& s : seen) {
            if (s == p.id) { known = true; break; }
        }
        if (known) continue;
        // Somewhere worth knowing: a shop, a bank, a healer, an inn. Wilderness
        // and dungeon mouths are not what a character is short of.
        if (p.services.empty()) continue;
        const i64 dx = p.position.x - playerX_;
        const i64 dy = p.position.y - playerY_;
        const i64 d = dx * dx + dy * dy;
        if (!best || d < bestD) { best = &p; bestD = d; }
    }
    if (!best) {
        travelFailure_ = "every place with a service is already known";
        return false;
    }
    if (chosenId) *chosenId = best->id;
    travelEntitySerial_ = 0;
    const i32 radius = best->radius > 6 ? 6 : best->radius;
    return TravelBegin(best->name.c_str(), best->position.x, best->position.y,
                       radius, /*hasZ=*/true, best->position.z);
}

bool Client::TravelToPlaceCategory(wm::PlaceCategory c) {
    if (!EnsureWorldKnowledge()) {
        travelFailure_ = WorldKnowledgeError();
        return false;
    }
    const wm::Place* p =
        world_knowledge_->atlas.NearestPlaceOfCategory(c, playerX_, playerY_);
    if (!p) {
        travelFailure_ = "no known place of that kind";
        LogWarn("[travel] no place of category %s is known\n",
                wm::PlaceCategoryName(c));
        return false;
    }
    travelEntitySerial_ = 0;
    // Arrive INSIDE it, not at its rim: a graveyard is a place to stand in the
    // middle of, and the things worth meeting are spread across it.
    const i32 radius = p->radius > 8 ? 8 : p->radius;
    return TravelBegin(p->name.c_str(), p->position.x, p->position.y, radius,
                       /*hasZ=*/true, p->position.z);
}

bool Client::TravelToHuntingGround(std::string* chosenName) {
    if (!EnsureWorldKnowledge()) {
        travelFailure_ = WorldKnowledgeError();
        return false;
    }
    const wm::Place* p = world_knowledge_->atlas.NearestHuntingGround(
        playerX_, playerY_);
    if (!p) {
        travelFailure_ = "no known hunting ground";
        LogWarn("[travel] no hunting ground is known\n");
        return false;
    }
    if (chosenName) *chosenName = p->name;
    travelEntitySerial_ = 0;
    // Same "arrive inside it, not at its rim" reasoning as
    // TravelToPlaceCategory -- a graveyard's dead are spread across it.
    const i32 radius = p->radius > 8 ? 8 : p->radius;
    return TravelBegin(p->name.c_str(), p->position.x, p->position.y, radius,
                       /*hasZ=*/true, p->position.z);
}

bool Client::TravelToResource(wm::ResourceKind r) {
    if (!EnsureWorldKnowledge()) {
        travelFailure_ = WorldKnowledgeError();
        return false;
    }
    const wm::Place* p = world_knowledge_->atlas.NearestPlaceWithResource(
        r, playerX_, playerY_);
    if (!p) {
        travelFailure_ = "no known source of that resource";
        return false;
    }
    travelEntitySerial_ = 0;
    // A resource area is broad; arriving anywhere inside it is arriving.
    // Cap the radius so the bot still ends up somewhere useful in a 200-tile
    // reagent field rather than stopping at its rim.
    const i32 radius = p->radius > 24 ? 24 : p->radius;
    return TravelBegin(p->name.c_str(), p->position.x, p->position.y, radius);
}

bool Client::TravelToEntity(u32 serial, i32 within) {
    i32 mx = 0, my = 0;
    i8 mz = 0;
    if (!MobilePosition(serial, &mx, &my, &mz)) {
        travelFailure_ = "that mobile is not in view";
        return false;
    }
    const i32 reach = within > 0 ? within : 1;

    // Route to a tile the character can occupy, not to the mobile's occupied
    // tile. Prefer the candidate closest to us so counters and shop walls do
    // not pull the path toward the sealed side of an otherwise usable NPC.
    struct StandCandidate { i32 x, y, cost; };
    std::vector<StandCandidate> candidates;
    for (i32 dy = -reach; dy <= reach; ++dy) {
        for (i32 dx = -reach; dx <= reach; ++dx) {
            if (dx == 0 && dy == 0) continue;
            if (std::max(std::abs(dx), std::abs(dy)) > reach) continue;
            const i32 sx = mx + dx, sy = my + dy;
            if (!TileIsWalkable(sx, sy, mz)) continue;
            candidates.push_back({sx, sy,
                                  Chebyshev(playerX_, playerY_, sx, sy)});
        }
    }
    if (candidates.empty()) {
        travelFailure_ = "no walkable interaction tile around that mobile";
        return false;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const StandCandidate& a, const StandCandidate& b) {
                  if (a.cost != b.cost) return a.cost < b.cost;
                  if (a.y != b.y) return a.y < b.y;
                  return a.x < b.x;
              });
    u32& nextChoice = travelEntityApproachChoice_[serial];
    const StandCandidate& stand = candidates[nextChoice % candidates.size()];
    ++nextChoice;

    // This journey targets the chosen stand tile. Callers re-evaluate the
    // mobile after arrival, so a wandering NPC naturally causes a fresh trip
    // rather than retargeting this journey back onto the occupied tile.
    travelEntitySerial_ = 0;
    travelEntityWithin_ = reach;
    char label[64];
    std::snprintf(label, sizeof(label), "mobile 0x%08X", serial);
    LogInfo("[travel] entity stand candidate %u/%zu at (%d,%d)\n",
            nextChoice, candidates.size(), stand.x, stand.y);
    return TravelBegin(label, stand.x, stand.y, 0, /*hasZ=*/true, mz);
}

bool Client::TravelToLastCorpse() {
    const travel::DeathRecord& d = knowledge_.LastDeath();
    if (!d.valid) {
        travelFailure_ = "this character has not died";
        return false;
    }
    knowledge_.NoteCorpseRecoveryAttempt();
    travelEntitySerial_ = 0;
    return TravelBegin("last corpse", d.x, d.y, 2);
}

bool Client::ReturnHome() {
    i32 hx = 0, hy = 0;
    i8 hz = 0;
    if (!knowledge_.Home(&hx, &hy, &hz)) {
        travelFailure_ = "this character has no home";
        return false;
    }
    travelEntitySerial_ = 0;
    return TravelBegin("home", hx, hy, 3);
}

void Client::TravelAbort(const char* why) {
    if (!journey_.Active()) return;
    journey_.Abort(why);
    if (nav_.bot.active || nav_.bot.planning) BotAbortPath(why ? why : "travel aborted");
    travelWalkOutstanding_ = false;
    TravelFinish(false, why ? why : "aborted");
}

const char* Client::TravelPhaseName() const {
    return travel::PhaseName(journey_.CurrentPhase());
}

void Client::TravelFinish(bool ok, const char* why) {
    // Arriving above the destination is not arriving. The journey's own goal
    // test checks the floor too now (M3.9) when it was given a goal z, so for
    // point/place/service trips this re-check should never fire; it stays as
    // the final assertion because an entity chase re-aims at a moving mobile
    // whose z the journey's goal may lag, and because reporting a success the
    // character cannot act on is the one outcome this layer must never allow.
    if (ok) {
        i32 dz = -1;
        if (travelEntitySerial_) {
            i32 mx = 0, my = 0;
            i8 mz = 0;
            if (MobilePosition(travelEntitySerial_, &mx, &my, &mz))
                dz = playerZ_ > mz ? playerZ_ - mz : mz - playerZ_;
        } else if (travelHasGoalZ_) {
            dz = playerZ_ > travelGoalZ_ ? playerZ_ - travelGoalZ_
                                         : travelGoalZ_ - playerZ_;
        }
        if (dz > kSameFloorZ) {
            ok = false;
            why = "arrived above or below the destination's floor";
        }
    }

    travelSucceeded_ = ok;
    if (!ok) travelFailure_ = why ? why : "";

    // A journey is allowed to finish inside its arrival radius, rather than
    // only on the atlas anchor itself.  Do not leave the tile A* walking the
    // last few tiles after reporting that arrival: Sphere binds an open bank
    // box to the exact character position that said "bank", so continuing
    // from that valid nearby tile made every subsequent deposit look like a
    // remote/cheating drop and bounce back into the backpack.  This is not a
    // banking special case -- any completed journey must relinquish its
    // remaining movement before its caller performs an interaction.
    if (nav_.bot.active || nav_.bot.planning)
        BotAbortPath(ok ? "travel arrived within destination radius"
                        : "travel failed");
    travelWalkOutstanding_ = false;

    LogInfo("[travel] %s %s at (%d,%d,%d)%s%s\n", travelLabel_.c_str(),
            ok ? "ARRIVED" : "FAILED", playerX_, playerY_,
            static_cast<int>(playerZ_), why && *why ? " -- " : "",
            why ? why : "");
    char ev[224];
    std::snprintf(ev, sizeof(ev),
                  "label='%s' ok=%d at=(%d,%d) legs=%zu plans=%d why='%s'",
                  travelLabel_.c_str(), ok ? 1 : 0, playerX_, playerY_,
                  journey_.LegCount(), journey_.RoutePlans(), why ? why : "");
    LogEvent(ok ? "travel_done" : "travel_failed", ev);
    if (ok) TravelNotePlaceReached(playerX_, playerY_);
}

void Client::TravelNotePlaceReached(i32 x, i32 y) {
    if (!world_knowledge_ || !world_knowledge_->ok) return;
    // Remember the place we actually stood in, not the one we aimed at.
    for (const wm::Place& p : world_knowledge_->atlas.Places()) {
        if (Chebyshev(x, y, p.position.x, p.position.y) <= p.radius) {
            knowledge_.NoteVisit(p.id.c_str(), NowMs());
            return;
        }
    }
}

void Client::TravelRetargetEntity() {
    if (!travelEntitySerial_ || !journey_.Active()) return;
    i32 mx = 0, my = 0;
    i8 mz = 0;
    if (!MobilePosition(travelEntitySerial_, &mx, &my, &mz)) {
        // Out of view. The last known position still stands as a destination:
        // the server will show us the mobile again when we get near it.
        return;
    }
    if (Chebyshev(mx, my, journey_.GoalX(), journey_.GoalY()) <= 2) return;
    // The mobile wandered. Re-aim rather than walk to where it used to be --
    // and pin its floor, so an NPC on a shop's upper storey is chased to the
    // storey, not to the ground under it.
    journey_.Begin(travelLabel_.c_str(), mx, my, travelEntityWithin_, NowMs(),
                   /*hasGoalZ=*/true, mz);
    travelWalkOutstanding_ = false;
    if (nav_.bot.active || nav_.bot.planning)
        BotAbortPath("travel target moved");
}

// ---------------------------------------------------------------------------
// Driving the journey
// ---------------------------------------------------------------------------

void Client::TravelPlanRoute() {
    if (!world_knowledge_ || !world_knowledge_->ok || !world_knowledge_->planner) {
        journey_.Abort("no world knowledge");
        TravelFinish(false, "no world knowledge");
        return;
    }

    route::RouteOptions opt;
    opt.allowTeleporters = true;
    // Moongates are only worth planning through when this character can
    // actually work one, which today means "the gate exists in the live
    // world". The journey learns that the hard way -- if the gate is not
    // there, the transit leg fails and the replan routes around it.
    // ---- M3.8 Phase 5: the planner chooses its own travel mode --------------
    //
    // travelmode::Choose has existed since M3.6 and NOTHING CALLED IT. The mode
    // layer could rank walking, moongates, loose-rune Recall and a Runebook,
    // and every journey still walked unless a scenario opted in by hand.
    //
    // That gap is wider than the brief states. It names the Runebook, but
    // moongates were default-off too (travelUseMoongates_ = false), so M2.5's
    // proven gate network sat unused: a Britain-to-Minoc trip walked 1,900
    // tiles past a working moongate because no scenario had said the word.
    //
    // Capability is built from what the character ACTUALLY has -- its own
    // Magery, its own mana -- never from what would be convenient.
    travelmode::Capability cap;
    cap.mageryTenths = PlayerSkillBase(static_cast<u16>(rules::kMagery));
    cap.manaNow      = PlayerMana();
    // ReagentsRequired is now 1 (M3.8 Phase 6), so casting is no longer free.
    // Reagent SOURCING is still an open authenticity gap and nothing tracks a
    // per-character reagent count yet, so this stays true and is recorded as
    // debt rather than faked: a Recall arm that silently assumed reagents would
    // be exactly the kind of unearned optimism this project keeps withdrawing.
    cap.haveReagents = true;
    cap.dead         = IsDead();
    cap.inCombat     = WarModeOn();
    cap.moongateRouteKnown = true;   // M2.5 proved the gate network live

    // M3.9: does this character's runebook actually hold this destination?
    //
    // Until now this stayed at its default of false, so Mode::RunebookRecall
    // could never be chosen no matter what the book contained -- the mode
    // layer's Runebook arm was unreachable code.
    //
    // The answer comes from pages READ OUT OF THE BOOK'S OWN GUMP
    // (NoteRunebookGump), not from anything we assume. That means it is only
    // true after the character has opened its book at least once this session,
    // which is the honest position: a bot that has not looked in its book does
    // not know what is in it.
    //
    // Matching is by name, and a page's name is whatever Sphere called the rune
    // when Mark succeeded -- the REGION, not the landmark. The shard owner's
    // description of where players actually mark ("inside mage shop, brit bank,
    // near moongate, other cities") is exactly why: a rune marked inside the
    // Britain mage shop is named for Britain, so a route labelled for a shop
    // still has to match a page named for its town.
    const int rbPage = RunebookPageFor(travelLabel_);
    cap.haveRunebookPage = (rbPage != 0);
    cap.runebookCharges  = runebookCharges_;

    const i32 straightTiles =
        Chebyshev(playerX_, playerY_, journey_.GoalX(), journey_.GoalY());
    const travelmode::Mode picked = travelmode::Choose(cap, straightTiles);

    // Log the whole ranking, not just the winner. A planner that only shows what
    // it chose cannot be argued with; the reasons on the rejected modes are what
    // make a wrong choice debuggable.
    for (const auto& o : travelmode::Rank(cap, straightTiles)) {
        LogInfo("[travel] mode %-16s %s%s%s\n",
                travelmode::ModeName(o.mode),
                o.usable ? "usable" : "no: ",
                o.usable ? "" : o.why.c_str(),
                (o.mode == picked) ? "   <- chosen" : "");
    }
    LogEvent("travel_mode", travelmode::ModeName(picked));

    // M3.9: EXECUTE a chosen Recall, instead of merely having chosen it.
    //
    // Selection has worked since M3.6 and execution never existed, so a mage
    // that decided to recall still walked the whole way. The recall runs FIRST
    // and the route is planned afterwards from wherever it leaves us: if it
    // works the walk is short, and if it fizzles, lacks mana or finds an empty
    // page the journey simply plans from here and walks. Nothing is faked and
    // nothing is skipped -- a failed recall costs a few seconds, not a lie.
    if (picked == travelmode::Mode::RunebookRecall && rbPage != 0 &&
        !runebookRecallDone_) {
        runebookRecallDone_ = true;   // set before, not after: a refusal still
                                      // counts as "tried", or a replan loop
                                      // would retry it forever.
        BeginRunebookRecall(rbPage);
    }

    // A scenario may still force gates on with `use_moongates on`; what has
    // changed is that it no longer has to.
    opt.allowMoongates = travelUseMoongates_ ||
                         (picked == travelmode::Mode::Moongate);
    opt.avoidCells = &journey_.AvoidCells();

    const route::WorldRoute r = world_knowledge_->planner->Plan(
        playerX_, playerY_, journey_.GoalX(), journey_.GoalY(), opt);

    LogInfo("[travel] plan %s: %s legs=%zu ~%d tiles transit=%zu nodes=%u\n",
            travelLabel_.c_str(), r.ok ? "ok" : r.failure, r.legs.size(),
            r.estimatedTiles, r.transitHops, r.nodesExpanded);

    // Recorded even when the plan failed (0 tiles is a fine answer then) so a
    // caller reading TravelLastPlannedTiles() right after a TravelToXxx() call
    // never sees a stale number from a previous, unrelated trip.
    travelPlannedTiles_ = r.ok ? r.estimatedTiles : 0;

    journey_.SetRoute(r, NowMs());
    journey_.NoteCommandIssued(travel::Command::PlanRoute, NowMs());
    if (journey_.CurrentPhase() == travel::Phase::Failed) {
        // A plan that fails because the character is standing somewhere the
        // router cannot see gets the same escape ladder as a route that runs
        // out mid-walk. This branch used to end the journey outright, which is
        // why M2.5's fix for debt item 5 did not actually cover the case it was
        // written for: being sealed into an upper storey fails HERE, at plan
        // time, and never reached the rung in TravelStep.
        if (journey_.FailureReason() == travel::Failure::NoRoute &&
            TravelTryEscape())
            return;
        TravelFinish(false, journey_.FailureDetail().c_str());
    }
}

// Transit pads within reach of this leg, minus the one the route is actually
// using. Recomputed per leg rather than per step: the atlas holds 450 of them
// and the tile A* asks about thousands of cells.
void Client::TravelRefreshAvoidPads(i32 legX, i32 legY) {
    travelAvoidPads_.clear();
    if (!world_knowledge_ || !world_knowledge_->ok) return;

    // A leg is at most 40 tiles; a radius around its midpoint that covers both
    // ends with room to spare is enough, and keeps the list at a handful.
    const i32 midX = (playerX_ + legX) / 2;
    const i32 midY = (playerY_ + legY) / 2;
    const i32 radius = Chebyshev(playerX_, playerY_, legX, legY) + 24;

    const route::RouteLeg* leg = journey_.CurrentLeg();
    const route::RouteLeg* next = journey_.NextLeg();

    // Not named `near`: <windef.h> still defines that as a macro, and the
    // error it produces points at the line after the declaration.
    std::vector<const wm::TransitNode*> pads;
    world_knowledge_->atlas.TransitsNear(midX, midY, radius, pads);
    for (const wm::TransitNode* t : pads) {
        if (t->kind != wm::TransitKind::Teleporter) continue;
        // The pad this leg is walking onto on purpose, or the one the next leg
        // will use, is not an obstacle -- it is the plan.
        auto isPlanned = [&](const route::RouteLeg* l) {
            return l && l->kind == route::LegKind::Teleporter &&
                   l->target.x == t->from.x && l->target.y == t->from.y;
        };
        if (isPlanned(leg) || isPlanned(next)) continue;
        travelAvoidPads_.push_back(t->from);
    }
    if (!travelAvoidPads_.empty())
        LogInfo("[travel] avoiding %zu teleporter pad(s) on this leg\n",
                travelAvoidPads_.size());
}

bool Client::TravelPadIsAvoided(i32 x, i32 y) const {
    for (const wm::Point& p : travelAvoidPads_)
        if (p.x == x && p.y == y) return true;
    return false;
}

void Client::TravelDriveLeg() {
    i32 tx = 0, ty = 0;
    i8 tz = 0;
    journey_.CommandTarget(&tx, &ty, &tz);

    if (GotoBusy()) return;   // a previous trip is still settling

    // A leg is supposed to be short by construction. One that is wildly out of
    // budget means the plan no longer matches where we are standing -- the
    // classic case is a transit that did not fire, leaving the next leg
    // pointing at the far side of the world. Handing that to the tile A* costs
    // seconds of search and, if it succeeds, walks the entire distance on foot.
    const i32 legDistance = Chebyshev(playerX_, playerY_, tx, ty);
    if (legDistance > kMaxSaneLegTiles) {
        LogWarn("[travel] leg to (%d,%d) is %d tiles from (%d,%d); the plan is "
                "stale, replanning\n", tx, ty, legDistance, playerX_, playerY_);
        journey_.OnLegFailed("leg target is implausibly far", NowMs());
        return;
    }

    travelLegTargetX_ = tx;
    travelLegTargetY_ = ty;

    // Sphere opens a public-gate gump as the character steps onto the gate,
    // which can be before Journey advances from this approach walk to the
    // transit leg.  Arm the intended choice *before* walking the last leg so
    // OnGenericGump can answer that immediate dialog.  Waiting until
    // TravelUseTransit was too late for Draver: the dialog had already timed
    // out and a double-click thereafter received no reply.
    const route::RouteLeg* next = journey_.NextLeg();
    if (next && next->kind == route::LegKind::Moongate &&
        !next->label.empty()) {
        travelGateSerial_ = 0;
        travelGateDestination_ = next->label;
        LogInfo("[travel] pre-armed moongate destination '%s' while approaching\n",
                travelGateDestination_.c_str());
    }
    travelWalkOutstanding_ = true;
    TravelRefreshAvoidPads(tx, ty);
    journey_.NoteCommandIssued(travel::Command::WalkTo, NowMs());

    // Chasing a mobile: pin the floor it is standing on for the final approach.
    // Without this the tile A* is free to finish on a gallery directly above
    // the NPC, which is "arrived" by every 2D measure and out of speech range
    // by the server's.
    if (travelEntitySerial_) {
        i32 mx = 0, my = 0;
        i8 mz = 0;
        if (MobilePosition(travelEntitySerial_, &mx, &my, &mz) &&
            Chebyshev(tx, ty, mx, my) <= travelEntityWithin_ + 2) {
            ActionGoto(tx, ty, /*hasZ=*/true, mz);
            return;
        }
    }
    // The final leg lands on the destination's own floor when the world data
    // knows which one that is. Intermediate legs stay floor-free: a goal z
    // applied a hundred tiles out would drag the whole route toward it.
    if (travelHasGoalZ_ &&
        Chebyshev(tx, ty, journey_.GoalX(), journey_.GoalY()) <= 2) {
        ActionGoto(tx, ty, /*hasZ=*/true, travelGoalZ_);
        return;
    }
    ActionGoto(tx, ty);
}

void Client::TravelUseTransit() {
    const route::RouteLeg* leg = journey_.CurrentLeg();
    if (!leg) return;

    if (leg->kind == route::LegKind::Teleporter) {
        // A Sphere teleporter pad fires when you step on it. The walk leg
        // before this one already put us on the tile, so there is nothing to
        // send: the journey just waits for the position jump.
        journey_.NoteCommandIssued(travel::Command::UseTransit, NowMs());
        LogInfo("[travel] standing on teleporter %s -> (%d,%d)\n",
                leg->transitId.c_str(), leg->arrive.x, leg->arrive.y);
        return;
    }

    // Moongate: find the gate object we are standing next to and use it the
    // way a player does -- double-click, then answer the destination gump.
    // The gate is a world item, so it only exists if the shard's worldgen
    // actually placed one.
    u32 gateSerial = 0;
    i32 bestD = 0x7FFFFFFF;
    for (const auto& kv : items_) {
        const ItemObj& it = kv.second;
        if (it.itemId != 0x0F6C && it.itemId != 0x0DDA) continue;  // blue/red gate
        const i32 d = Chebyshev(playerX_, playerY_, it.x, it.y);
        if (d < bestD) { bestD = d; gateSerial = kv.first; }
    }
    if (!gateSerial || bestD > 3) {
        LogWarn("[travel] no moongate object within reach at (%d,%d)\n",
                playerX_, playerY_);
        journey_.OnLegFailed("no gate object here", NowMs());
        return;
    }

    journey_.NoteCommandIssued(travel::Command::UseTransit, NowMs());
    travelGateSerial_ = gateSerial;
    travelGateDestination_ = leg->label;
    LogInfo("[travel] using moongate 0x%08X for '%s' (gump active=%d "
            "serial=0x%08X)\n", gateSerial, travelGateDestination_.c_str(),
            gump_.active ? 1 : 0, gump_.serial);

    // The gate's own @step trigger opens the destination gump as soon as we
    // walk onto it, and Sphere will not open a second one for the same context
    // -- so a double-click here is answered with silence and the trip stalls.
    // If the gump is already up, that IS the gate asking; answer it.
    if (gump_.active && gump_.serial == gateSerial) {
        LogInfo("[travel] the gate's gump is already open; answering it\n");
        AnswerGateGump();
        return;
    }
    SendDoubleClick(gateSerial);
}

// Walk toward a nearby navgrid anchor and, if we get there, start the journey
// over from the new position. Bounded hard: three attempts, each a different
// anchor, and a failure to reach any of them means the character really is
// sealed in -- which is worth reporting plainly rather than retrying forever.
bool Client::TravelTryEscape() {
    if (!world_knowledge_ || !world_knowledge_->ok || !world_knowledge_->planner)
        return false;
    if (travelEscapes_ >= kMaxTravelEscapes) return false;
    if (GotoBusy()) return true;   // an escape walk is already in progress

    std::vector<wm::Point> candidates;
    world_knowledge_->planner->EscapeCandidates(playerX_, playerY_, 12,
                                                candidates);

    for (const wm::Point& c : candidates) {
        bool tried = false;
        for (const wm::Point& t : travelEscapeTried_)
            tried = tried || (t.x == c.x && t.y == c.y);
        if (tried) continue;

        ++travelEscapes_;
        travelEscapeTried_.push_back(c);
        LogWarn("[travel] route exhausted at (%d,%d,%d); trying to reach "
                "(%d,%d,%d) to get somewhere routable (escape %d/%d)\n",
                playerX_, playerY_, static_cast<int>(playerZ_), c.x, c.y,
                static_cast<int>(c.z), travelEscapes_, kMaxTravelEscapes);
        char ev[160];
        std::snprintf(ev, sizeof(ev), "from=(%d,%d,%d) to=(%d,%d) attempt=%d",
                      playerX_, playerY_, static_cast<int>(playerZ_), c.x, c.y,
                      travelEscapes_);
        LogEvent("travel_escape", ev);

        // PARK the journey rather than restarting it. Restarting was the M2.5
        // approach and it is what orphaned the recovery: Begin() wiped the
        // trip, the parent reported itself finished, and the escape walk
        // carried on with nobody waiting for it. The journey now keeps its
        // label, goal, radius and avoid-cell memory, stays Active, and issues
        // Wait until the walk reports back.
        if (!journey_.Recovering() &&
            !journey_.BeginPositionRecovery("route unusable from here", NowMs())) {
            // Budget spent. Undo the bookkeeping for an attempt we will not
            // make, and let the caller fail cleanly.
            --travelEscapes_;
            travelEscapeTried_.pop_back();
            return false;
        }
        travelWalkOutstanding_ = false;
        ActionGoto(c.x, c.y, /*hasZ=*/true, c.z);
        travelLegTargetX_ = c.x;
        travelLegTargetY_ = c.y;
        travelWalkOutstanding_ = true;
        return true;
    }
    return false;
}

void Client::TravelTick() {
    if (!journey_.Active()) return;
    if (!IsInWorld()) {
        journey_.Abort("left the world");
        TravelFinish(false, "left the world");
        return;
    }
    // Dying mid-journey invalidates the plan: a ghost is not going to finish
    // a living character's errand, and carrying on is how a bot ends up in the
    // die/resurrect/corpse-run loop this milestone must avoid. A journey that
    // BEGAN dead is a different thing -- walking a ghost to its corpse or to a
    // healer is exactly what a player does -- so only a change of state aborts.
    if (IsDead() != travelStartedDead_) {
        journey_.Abort(IsDead() ? "died" : "resurrected");
        if (nav_.bot.active || nav_.bot.planning)
            BotAbortPath(IsDead() ? "died" : "resurrected");
        TravelFinish(false, IsDead() ? "died mid-journey"
                                     : "resurrected mid-journey");
        return;
    }

    const i64 now = NowMs();

    // Feed the journey a position sample on a fixed cadence. This is what
    // drives stuck / oscillation / transition detection.
    if (now - travelLastSampleMs_ >= kTravelSampleMs) {
        travelLastSampleMs_ = now;
        TravelRetargetEntity();
        journey_.OnPositionSample(playerX_, playerY_, playerZ_, now);
    }

    // Resolve an outstanding walk leg: GotoBusy() latches the trip result.
    if (travelWalkOutstanding_ && !GotoBusy()) {
        travelWalkOutstanding_ = false;
        const i32 off = Chebyshev(playerX_, playerY_, travelLegTargetX_,
                                  travelLegTargetY_);

        // An escape walk is not a route leg, and must not be reported as one.
        // It belongs to the recovery lifecycle: the journey parked when it
        // started, and this is the only thing that unparks it.
        if (journey_.Recovering()) {
            const bool reached = GotoSucceeded() || off <= kLegArriveSlack;
            LogInfo("[travel] recovery walk %s at (%d,%d,%d) (off %d); "
                    "attempt %d/%d\n", reached ? "reached its anchor" : "fell short",
                    playerX_, playerY_, static_cast<int>(playerZ_), off,
                    journey_.PositionRecoveries(),
                    journey_.GetLimits().maxPositionRecoveries);
            char ev[160];
            std::snprintf(ev, sizeof(ev),
                          "reached=%d at=(%d,%d,%d) off=%d attempt=%d",
                          reached ? 1 : 0, playerX_, playerY_,
                          static_cast<int>(playerZ_), off,
                          journey_.PositionRecoveries());
            LogEvent("travel_recovery_done", ev);
            journey_.OnPositionRecovered(reached, now);
            // If it fell short and budget remains, the journey is still parked
            // and the next tick will start another attempt from here.
            if (!reached && journey_.Recovering() && !TravelTryEscape()) {
                // No anchors left to try. Terminate the journey BEFORE
                // reporting: a trip finished while still parked in Recovering
                // stays Active forever -- TravelBusy() never clears and the
                // scenario's wait_travel never wakes. Same defect class as
                // the M3.9 soak spin, caught by reading for it.
                journey_.Abort("sealed in; recovery exhausted");
                TravelFinish(false, "sealed in; recovery exhausted");
            }
            return;
        }
        // Chasing a mobile also means standing on its floor, not above it.
        bool wrongFloor = false;
        if (travelEntitySerial_) {
            i32 mx = 0, my = 0;
            i8 mz = 0;
            if (MobilePosition(travelEntitySerial_, &mx, &my, &mz)) {
                const i32 dz = playerZ_ > mz ? playerZ_ - mz : mz - playerZ_;
                wrongFloor = dz > kSameFloorZ;
            }
        }
        if (!wrongFloor && (GotoSucceeded() || off <= kLegArriveSlack)) {
            journey_.OnLegArrived(playerX_, playerY_, playerZ_, now);
        } else {
            if (wrongFloor)
                LogWarn("[travel] reached (%d,%d,%d) but the target is on "
                        "another floor; not arrived\n", playerX_, playerY_,
                        static_cast<int>(playerZ_));
            // Feed the failed macro cell back so the next plan routes around
            // it instead of proposing the same impossible leg again.
            if (world_knowledge_ && world_knowledge_->planner)
                journey_.AvoidCell(world_knowledge_->planner->CellIndex(
                    travelLegTargetX_, travelLegTargetY_));
            journey_.OnLegFailed("tile route stopped short", now);
        }
    }

    switch (journey_.NextCommand(now)) {
        case travel::Command::PlanRoute: TravelPlanRoute(); break;
        case travel::Command::WalkTo:    TravelDriveLeg();  break;
        case travel::Command::UseTransit:TravelUseTransit();break;
        case travel::Command::Finish:
            // The note is non-empty when the journey settled for a nearby
            // tile because the destination itself was occupied (the crowded
            // bank/forge case) -- an arrival, but one worth being able to
            // count in a soak log.
            TravelFinish(true, journey_.ArrivalNote().c_str());
            break;
        case travel::Command::Fail:
            // Before giving up, try to get somewhere the router can see. A
            // character sealed into an upper storey or a walled pocket
            // produces this failure for every destination, and no amount of
            // replanning helps -- the plan is fine, the character is in the
            // wrong place.
            //
            // BOTH failure modes have to be caught here. Being sealed in shows
            // up as NoRoute when the planner cannot even build a route from
            // where we stand (Journey.cpp:121), and as Unreachable when a
            // route was built and then ran out under us (:200). M2.5's fix for
            // debt item 5 only covered the second, because that is the one the
            // obstacle scenario happened to produce; M3.5 hit the first on the
            // Mage Tower's upper storey -- "plan Britain banker: no world route
            // to the destination, nodes=1" -- and the escape rung never fired.
            //
            // A genuine "there is no such route" also lands on NoRoute, so this
            // will occasionally spend an escape attempt on a destination that
            // was never reachable. That costs a few seconds, is bounded to
            // three attempts, and then fails cleanly -- which is a far better
            // trade than a character that can never leave a building again.
            if ((journey_.FailureReason() == travel::Failure::Unreachable ||
                 journey_.FailureReason() == travel::Failure::NoRoute) &&
                TravelTryEscape())
                break;
            {
                const std::string why =
                    journey_.FailureDetail().empty()
                        ? travel::FailureName(journey_.FailureReason())
                        : journey_.FailureDetail();
                // A journey that answers Fail MUST NOT remain Active after we
                // report it, or this switch re-enters here every tick: the
                // trip is re-"finished" 16 times a second, TravelBusy() never
                // clears, and the scenario's wait_travel never wakes. That is
                // exactly what the M3.9 38-bot soak measured -- 99,290
                // travel_failed events, ~62ms apart, why='none' -- when a
                // route spent short of the goal left the journey in a live
                // phase with no failure_ set. The state machine now resolves
                // that case itself; this is the second line of defence, kept
                // because the cost of the belt-and-braces is one branch and
                // the cost of the spin was a whole soak run.
                if (journey_.CurrentPhase() != travel::Phase::Failed)
                    journey_.Abort(why.c_str());
                TravelFinish(false, why.c_str());
            }
            break;
        case travel::Command::Wait:
        case travel::Command::Idle:
        default:
            break;
    }
}

// A vendor's trade is only visible in its paperdoll title (M2 finding: 0x98
// returns a first name and nothing else). The title reads "<name>, the
// provisioner", so the job is whatever word follows "the". Matching against
// the same job vocabulary the atlas was generated from keeps the live world
// and the stored world speaking one language.
void Client::NoteServiceFromTitle(u32 serial, const char* title) {
    if (!title || !*title) return;
    std::string lower(title);
    for (char& c : lower)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

    const usize the = lower.rfind(" the ");
    if (the == std::string::npos) return;
    std::string job = lower.substr(the + 5);
    // Trim to the first word: some titles carry a suffix.
    const usize sp = job.find_first_of(" ,.");
    if (sp != std::string::npos) job.resize(sp);
    if (job.empty()) return;

    const wm::Service svcFromJob = ServiceForPaperdollJob(job.c_str());
    if (svcFromJob == wm::Service::None) return;
    wm::Service svc = svcFromJob;
    ServiceSightingTail(serial, title, svc);
}

// The atlas speaks Sphere's job defnames; a paperdoll speaks English. They
// agree for most trades, and the few that differ are spelled out here.
//
// This lives on its own because more than the sighting recorder needs it: a
// character looking for someone of a trade has to know that "fisherwoman" is
// a fisherman. It did not, and a fisher stood three tiles from Shika the
// fisherwoman with fifteen fish to sell and reported no buyer reachable,
// three trips running, twice over.
wm::Service ServiceForPaperdollJob(const char* jobRaw) {
    if (!jobRaw || !*jobRaw) return wm::Service::None;
    std::string job(jobRaw);
    for (char& c : job)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    struct Alias { const char* title; wm::Service svc; };
    static const Alias kAliases[] = {
        {"banker",       wm::Service::Banker},
        {"minter",       wm::Service::Banker},
        {"healer",       wm::Service::Healer},
        {"mage",         wm::Service::Mage},
        {"alchemist",    wm::Service::Alchemist},
        {"provisioner",  wm::Service::Provisioner},
        {"blacksmith",   wm::Service::Blacksmith},
        {"smith",        wm::Service::Blacksmith},
        {"armourer",     wm::Service::Blacksmith},
        {"armorer",      wm::Service::Blacksmith},
        {"weaponsmith",  wm::Service::Blacksmith},
        {"tailor",       wm::Service::Tailor},
        {"weaver",       wm::Service::Tailor},
        {"cobbler",      wm::Service::Tailor},
        {"carpenter",    wm::Service::Carpenter},
        {"bowyer",       wm::Service::Bowyer},
        {"tinker",       wm::Service::Tinker},
        {"scribe",       wm::Service::Scribe},
        {"innkeeper",    wm::Service::Innkeeper},
        {"tavernkeeper", wm::Service::Innkeeper},
        {"barkeeper",    wm::Service::Innkeeper},
        {"butcher",      wm::Service::Butcher},
        {"baker",        wm::Service::Baker},
        {"tanner",       wm::Service::Tanner},
        {"furtrader",    wm::Service::Tanner},
        {"jeweler",      wm::Service::Jeweler},
        {"shipwright",   wm::Service::Shipwright},
        {"mapmaker",     wm::Service::Mapmaker},
        {"fisherman",    wm::Service::Fisherman},
        // The vendor tables ask for a "fisher"; nobody on this shard wears
        // that word. Sphere's titles are "fisherman" and "fisherwoman", and
        // without this row the short name resolves to no service at all, so
        // the service match never even ran and the literal match could never
        // succeed. Shika stood three tiles away through six trips.
        {"fisher",       wm::Service::Fisherman},
        // Sphere's paperdoll titles are gendered (CNPC_PaperdollTitle_VT), so
        // the same trade reaches us under two names.
        {"fisherwoman",  wm::Service::Fisherman},
        {"seamstress",   wm::Service::Tailor},
        {"armourer",     wm::Service::Blacksmith},
        {"animal",       wm::Service::Stablemaster},   // "the animal trainer"
        {"cook",         wm::Service::Cook},
        {"miller",       wm::Service::Miller},
        {"stablemaster", wm::Service::Stablemaster},
        {"animaltrainer",wm::Service::Stablemaster},
        {"veterinarian", wm::Service::Veterinarian},
    };

    for (const Alias& a : kAliases)
        if (job == a.title) return a.svc;
    return wm::ServiceFromName(job.c_str());
}

void Client::ServiceSightingTail(u32 serial, const char* title,
                                 wm::Service svc) {
    i32 mx = 0, my = 0;
    if (!MobilePosition(serial, &mx, &my)) return;
    const MobileObj* m = FindMobileBySerial(serial);
    knowledge_.NoteService(svc, serial, title, mx, my, m ? m->z : playerZ_,
                           NowMs());
    LogInfo("[world] %s is a %s at (%d,%d)\n", title, wm::ServiceName(svc),
            mx, my);
}

// ---------------------------------------------------------------------------
// War / peace
// ---------------------------------------------------------------------------

void Client::EnterWarMode() {
    war_.OnWarModeRequested(NowMs());
    if (playerWarMode_) return;
    LogInfo("[war] entering war mode\n");
    SetWarMode(true);
}

void Client::ExitWarMode() {
    if (!playerWarMode_) return;
    LogInfo("[war] leaving war mode\n");
    war_.NoteExitRequested(NowMs());
    SetWarMode(false);
}

void Client::EnsurePeaceMode() {
    war_.OnPeacefulIntent(NowMs());
    if (playerWarMode_) ExitWarMode();
}

void Client::SetSurvivalEnabled(bool on) {
    survivalEnabled_ = on;
    survivalNextActionMs_ = 0;
    survivalLastTactic_ = -1;
    LogInfo("[survival] %s\n", on ? "on" : "off");
    LogEvent("survival_mode", on ? "on" : "off");
}

// Apply combat_policy's decision. The policy is pure and unit-tested; this is
// only the hands.
//
// Everything here is an ordinary client action a player could take: drink a
// potion, leave war mode, walk away, bandage yourself. Nothing reaches past the
// protocol.
void Client::SurvivalTick() {
    if (!survivalEnabled_ || !IsInWorld() || IsDead()) return;
    const i64 now = NowMs();
    if (now < survivalNextActionMs_) return;

    combat::Vitals v;
    v.hpNow    = PlayerHp();
    v.hpMax    = PlayerHpMax();
    v.inCombat = WarModeOn();

    // "Adjacent" means something we are actually fighting is within a tile.
    // A bandage takes ~3 seconds (SKILL 17 DELAY=3.0); that is the whole reason
    // the policy cares.
    const u32 target = war_.TargetSerial();
    if (target) {
        i32 tx = 0, ty = 0;
        if (MobilePosition(target, &tx, &ty)) {
            const i32 dx = tx > playerX_ ? tx - playerX_ : playerX_ - tx;
            const i32 dy = ty > playerY_ ? ty - playerY_ : playerY_ - ty;
            v.enemyAdjacent = (dx <= 1 && dy <= 1);
        }
    }

    // Count what we can actually heal with, from the pack we can actually see.
    // A supply we cannot find is a supply we do not have -- claiming otherwise
    // is how a policy decides to bandage with nothing to bandage with.
    // Graphics from the runtime, not from generic UO: i_bandage is [ITEMDEF
    // 0e21], and every heal potion (HealLess/Heal/HealGreat) is ID=i_bottle_
    // yellow = 0x0F0C. That last one means the client cannot tell a heal potion
    // from any other yellow-bottle potion by graphic alone -- so the count below
    // is "a yellow bottle is present", which is honest but not precise, and is
    // why DrinkPotion is attempted rather than assumed to work.
    constexpr u16 kBandageGraphic = 0x0E21;
    constexpr u16 kYellowPotionGraphic = 0x0F0C;
    const u32 bandage = FindBackpackItemByGraphic(kBandageGraphic);
    const u32 potion  = FindBackpackItemByGraphic(kYellowPotionGraphic);
    v.bandages    = bandage ? 1 : 0;
    v.healPotions = potion ? 1 : 0;

    const combat::Tactic t = combat::Decide(v);
    if (static_cast<int>(t) != survivalLastTactic_ || now - survivalLastLogMs_ > 5000) {
        LogInfo("[survival] hp %d/%d (%d%%) -> %s\n", v.hpNow, v.hpMax,
                combat::HealthPercent(v), combat::TacticName(t));
        survivalLastTactic_ = static_cast<int>(t);
        survivalLastLogMs_ = now;
    }

    switch (t) {
        case combat::Tactic::Fight:
            break;   // nothing to do; the fight is already happening

        case combat::Tactic::DrinkPotion: {
            if (potion) {
                LogEvent("survival_potion", "");
                ActionUseObject(potion);
                survivalNextActionMs_ = now + 2000;
            }
            break;
        }

        case combat::Tactic::Disengage:
        case combat::Tactic::Flee:
            // Leaving war mode is the disengage. Walking away is the travel
            // layer's job, not this tick's -- issuing steps from here would
            // fight whatever journey is already running.
            if (WarModeOn()) {
                LogEvent("survival_disengage", combat::TacticName(t));
                ExitWarMode();
            }
            survivalNextActionMs_ = now + 1500;
            break;

        case combat::Tactic::Bandage: {
            if (bandage && !action_.Active()) {
                LogEvent("survival_bandage", "");
                ActionUseBandage(bandage, playerSerial_);
                // A bandage is ~3 seconds; do not stack another decision on top
                // of one already in flight.
                survivalNextActionMs_ = now + 4000;
            }
            break;
        }

        case combat::Tactic::Rest:
            survivalNextActionMs_ = now + 3000;
            break;

        case combat::Tactic::Count:
            break;
    }
}

void Client::WarModeTick() {
    if (!IsInWorld()) return;
    const i64 now = NowMs();

    // Report SIGHTINGS, never absence. This used to declare the target gone
    // the first tick it was missing from mobileCache_, which was survivable
    // while the cache only purged when the player moved -- but the M3.9
    // stationary purge (Client::PurgeOutOfRange, 2s timer) evicts a fleeing
    // animal that steps briefly out of view, and "gone" here dropped the
    // combat intent to None, armed the idle rule, and had the bot sheathe its
    // katana 15s into a fight it was winning. Seven live runs where nothing
    // could be killed traced back to this one line. A momentary loss of sight
    // is what the watchdog's own targetLostMs rule exists for: refresh the
    // clock while the target is visible, do nothing while it is not, and let
    // that rule age a genuinely lost target out. OnTargetGone remains correct
    // where the server PROVED the loss -- the 0x1D remove handler.
    const u32 target = war_.TargetSerial();
    if (target && FindMobileBySerial(target)) war_.OnTargetSeen(target, now);

    if (war_.ShouldExitWar(now)) {
        LogInfo("[war] dropping war mode: %s\n", war_.ExitReason(now));
        LogEvent("war_timeout", war_.ExitReason(now));
        ExitWarMode();
    }
}

// ---------------------------------------------------------------------------
// Generic gump (0xB0) / response (0xB1)
// ---------------------------------------------------------------------------

// Sphere writes the gump as `{control}{control}...` plus a separate UTF-16
// text table; `dtext` becomes a `text` control holding an index into it. That
// indirection is why a label has to be resolved rather than read inline, and
// it is what lets us match a radio button to the destination name beside it.
void Client::OnGenericGump(const u8* data, usize size) {
    gump_ = ActiveGump{};
    if (size < 23) return;

    const u32 serial  = LoadBE32(data + 3);
    const u32 context = LoadBE32(data + 7);
    const u16 ctrlLen = LoadBE16(data + 19);
    const usize ctrlStart = 21;
    if (ctrlStart + ctrlLen > size) return;

    // Text table follows the controls.
    std::vector<std::string> texts;
    usize p = ctrlStart + ctrlLen;
    if (p + 2 <= size) {
        const u16 count = LoadBE16(data + p);
        p += 2;
        for (u16 i = 0; i < count && p + 2 <= size; ++i) {
            const u16 len = LoadBE16(data + p);
            p += 2;
            std::string s;
            for (u16 c = 0; c < len && p + 2 <= size; ++c, p += 2)
                s.push_back(static_cast<char>(data[p + 1]));  // UTF-16BE -> ASCII
            texts.push_back(std::move(s));
        }
    }

    auto textAt = [&](long idx) -> std::string {
        return (idx >= 0 && static_cast<usize>(idx) < texts.size())
                   ? texts[static_cast<usize>(idx)]
                   : std::string();
    };

    // Walk the `{...}` control blocks in order. A radio/checkbox takes the
    // label of the next text control, which is how the gump reads on screen.
    std::vector<GumpOption> options;
    long pendingChoiceIdx = -1;
    usize i = ctrlStart;
    const usize ctrlEnd = ctrlStart + ctrlLen;
    while (i < ctrlEnd) {
        while (i < ctrlEnd && data[i] != '{') ++i;
        if (i >= ctrlEnd) break;
        usize close = i + 1;
        while (close < ctrlEnd && data[close] != '}') ++close;
        if (close >= ctrlEnd) break;

        std::string body(reinterpret_cast<const char*>(data + i + 1),
                         close - i - 1);
        i = close + 1;

        // Tokenise on spaces.
        std::vector<std::string> tok;
        std::string cur;
        for (char c : body) {
            if (c == ' ' || c == '\t') {
                if (!cur.empty()) { tok.push_back(cur); cur.clear(); }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) tok.push_back(cur);
        if (tok.empty()) continue;

        std::string verb = tok[0];
        for (char& c : verb)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

        if ((verb == "radio" || verb == "checkbox") && tok.size() >= 7) {
            GumpOption o;
            o.id = static_cast<u32>(std::strtoul(tok[6].c_str(), nullptr, 10));
            o.button = false;
            options.push_back(std::move(o));
            pendingChoiceIdx = static_cast<long>(options.size()) - 1;
        } else if (verb == "button" && tok.size() >= 8) {
            GumpOption o;
            o.id = static_cast<u32>(std::strtoul(tok[7].c_str(), nullptr, 10));
            o.button = true;
            options.push_back(std::move(o));
            pendingChoiceIdx = static_cast<long>(options.size()) - 1;
        } else if (verb == "text" && tok.size() >= 5) {
            const long idx = std::strtol(tok[4].c_str(), nullptr, 10);
            if (pendingChoiceIdx >= 0 &&
                options[static_cast<usize>(pendingChoiceIdx)].label.empty()) {
                options[static_cast<usize>(pendingChoiceIdx)].label =
                    textAt(idx);
                pendingChoiceIdx = -1;
            }
        }
    }

    gump_.active = true;
    gump_.serial = serial;
    gump_.context = context;
    gump_.options = std::move(options);
    gump_.texts = texts;

    LogInfo("[gump] 0x%08X context=0x%08X: %zu option(s), %zu text(s)\n",
            serial, context, gump_.options.size(), gump_.texts.size());
    for (const GumpOption& o : gump_.options)
        LogInfo("[gump]   %s %u = '%s'\n", o.button ? "button" : "choice",
                o.id, o.label.c_str());
    // Log the text table too. A runebook's page names live here and nowhere
    // else, so a dump that shows only options makes the book look empty.
    for (usize t = 0; t < gump_.texts.size(); ++t)
        LogInfo("[gump]   text %zu = '%s'\n", t, gump_.texts[t].c_str());

    NoteRunebookGump();

    // A recall we started is waiting for exactly this gump.
    if (pendingRunebookPage_ && AnswerRunebookTravelGump()) return;
    char ev[128];
    std::snprintf(ev, sizeof(ev), "serial=0x%08X context=0x%08X options=%zu",
                  serial, context, gump_.options.size());
    LogEvent("gump_open", ev);

    // If this gump belongs to a gate the current journey is trying to use,
    // answer it. The destination comes from the route leg, not from whether we
    // happened to be the one who opened it: walking onto the gate opens it
    // too, and that is still the gate asking us where we want to go.
    if (!travelGateDestination_.empty() &&
        (travelGateSerial_ == 0 || travelGateSerial_ == serial)) {
        travelGateSerial_ = serial;
        AnswerGateGump();
        return;
    }
    // The gate opens its gump from @step, which fires while the APPROACH leg
    // is still running -- the journey has not reached the transit leg yet. So
    // look at the leg after this one too: a gump from the gate we are walking
    // onto is the gate asking, whichever leg the plan is technically on.
    if (journey_.Active()) {
        const route::RouteLeg* leg = journey_.CurrentLeg();
        const route::RouteLeg* next = journey_.NextLeg();
        const route::RouteLeg* gate =
            (leg && leg->kind == route::LegKind::Moongate)    ? leg
            : (next && next->kind == route::LegKind::Moongate) ? next
                                                               : nullptr;
        if (gate && !gate->label.empty()) {
            travelGateSerial_ = serial;
            travelGateDestination_ = gate->label;
            AnswerGateGump();
        }
    }
}

// Read a Runebook's pages out of the gump it just sent.
//
// This exists because the obvious approach does not work. AnswerGateGump picks
// a moongate destination by matching gump option LABELS, and a runebook has
// none: every travel button (11..18) arrives with an empty label, because the
// page names are drawn as DText captions rather than as button text
// (revolution_runebook.scp:119-162). Matching labels here would silently find
// nothing, forever.
//
// The layout was measured live (m39_rb_probe) rather than assumed:
//
//   text 0..6  'Runebook' 'Charges: NN' 'Page' 'Name' 'Destination' 'Travel' 'Rune'
//   then THREE texts per page: number, name, destination point
//
// so page N's name is texts[5 + 3N] and its travel button is 10 + N. An unset
// page reads back as the literal "(empty)" and its point as "-".
void Client::NoteRunebookGump() {
    if (gump_.texts.empty() || gump_.texts[0] != "Runebook") return;

    runebookSerial_ = gump_.serial;
    runebookPages_.clear();
    runebookCharges_ = 0;

    // "Charges: 00" -- read the number rather than assuming a charged book.
    if (gump_.texts.size() > 1) {
        const std::string& c = gump_.texts[1];
        const usize colon = c.find(':');
        if (colon != std::string::npos)
            runebookCharges_ =
                static_cast<int>(std::strtol(c.c_str() + colon + 1, nullptr, 10));
    }

    for (int n = 1; n <= 8; ++n) {
        const usize nameIdx = static_cast<usize>(5 + 3 * n);
        if (nameIdx >= gump_.texts.size()) break;
        RunebookPage p;
        p.page  = n;
        p.name  = gump_.texts[nameIdx];
        p.point = (nameIdx + 1 < gump_.texts.size()) ? gump_.texts[nameIdx + 1]
                                                     : std::string();
        p.filled = (p.name != "(empty)" && !p.name.empty());
        runebookPages_.push_back(std::move(p));
    }

    int filled = 0;
    for (const RunebookPage& p : runebookPages_) {
        if (!p.filled) continue;
        ++filled;
        LogInfo("[runebook] page %d = '%s' %s (travel button %d)\n", p.page,
                p.name.c_str(), p.point.c_str(), 10 + p.page);
    }
    char ev[128];
    std::snprintf(ev, sizeof(ev), "book=0x%08X pages=%d charges=%d",
                  runebookSerial_, filled, runebookCharges_);
    LogEvent("runebook_read", ev);
    if (filled == 0)
        LogInfo("[runebook] 0x%08X holds no marked pages\n", runebookSerial_);
}

// Which page holds this destination? 0 when the book does not have one.
// Substring match in both directions, because a page name is written by whoever
// marked it ("Britain") while a route label may be longer ("Britain bank").
int Client::RunebookPageFor(const std::string& destination) const {
    if (destination.empty()) return 0;
    auto lower = [](std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    };
    const std::string want = lower(destination);
    for (const RunebookPage& p : runebookPages_) {
        if (!p.filled) continue;
        const std::string have = lower(p.name);
        if (want.find(have) != std::string::npos ||
            have.find(want) != std::string::npos)
            return p.page;
    }
    return 0;
}

// Start a Recall the travel layer decided on: open the book so its gump comes
// back, and remember which page we are travelling from.
//
// Nothing here shortcuts the spell. Pressing the page's travel button makes the
// BOOK surface that page's rune and ask the character to cast Recall at it, so
// the skill check, the mana, the reagents and the fizzle roll all still happen
// on the server. A recall that "succeeded" without the server moving us would
// be exactly the kind of unearned result this project keeps withdrawing.
bool Client::BeginRunebookRecall(int page) {
    if (page < 1 || page > 8) return false;
    const u32 book = FindBackpackItemByGraphic(0x22C5);
    if (!book) {
        LogWarn("[runebook] recall wanted page %d but no runebook is in the "
                "backpack\n", page);
        return false;
    }
    pendingRunebookPage_  = page;
    awaitingRunebookRune_ = false;
    LogInfo("[runebook] recalling from page %d (book 0x%08X)\n", page, book);
    LogEvent("runebook_recall_begin", "");
    ActionUseObject(book);
    return true;
}

// The book's gump is open and we are mid-recall: press page N's travel button.
// Button ids are 10+N, confirmed live in M3.6 and again by m39_rb_probe.
bool Client::AnswerRunebookTravelGump() {
    if (!pendingRunebookPage_) return false;
    const u32 button = static_cast<u32>(10 + pendingRunebookPage_);
    bool have = false;
    for (const GumpOption& o : gump_.options)
        if (o.button && o.id == button) { have = true; break; }
    if (!have) {
        LogWarn("[runebook] gump has no travel button %u for page %d\n", button,
                pendingRunebookPage_);
        pendingRunebookPage_ = 0;
        return false;
    }
    LogInfo("[runebook] pressing travel button %u (page %d)\n", button,
            pendingRunebookPage_);
    pendingRunebookPage_  = 0;
    awaitingRunebookRune_ = true;
    AnswerGump(button, 0);
    return true;
}

// Pick the destination the current route leg wants out of the open gump and
// press the gump's own affirmative button. Both are read from the labels the
// shard sent rather than hard-coded: the button id (1000) and the radio ids
// belong to `core/dialogs/d_moongates.scp`, and a shard that reskins that
// dialog should still work.
void Client::AnswerGateGump() {
    auto upper = [](std::string s) {
        for (char& c : s)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        return s;
    };
    auto lower = [](std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    };

    u32 choice = 0, button = 0;
    const std::string want = lower(travelGateDestination_);
    for (const GumpOption& o : gump_.options) {
        if (o.button) {
            const std::string l = upper(o.label);
            if (!button && (l == "OKAY" || l == "OK" || l == "ACCEPT"))
                button = o.id;
        } else if (!choice && !want.empty() &&
                   lower(o.label).find(want) != std::string::npos) {
            choice = o.id;
        }
    }

    const std::string destination = travelGateDestination_;
    travelGateSerial_ = 0;
    travelGateDestination_.clear();

    if (choice && button) {
        LogInfo("[travel] gate gump: choosing '%s' (choice %u, button %u)\n",
                destination.c_str(), choice, button);
        char ev[128];
        std::snprintf(ev, sizeof(ev), "destination='%s' choice=%u button=%u",
                      destination.c_str(), choice, button);
        LogEvent("moongate_use", ev);
        AnswerGump(button, choice);
        return;
    }

    LogWarn("[travel] gate gump offers no '%s' (choice=%u button=%u)\n",
            destination.c_str(), choice, button);
    CloseGump();
    journey_.OnLegFailed("gate offers no such destination", NowMs());
}

void Client::SendGumpResponse(u32 serial, u32 context, u32 button,
                              const u32* checks, usize checkCount) {
    // 0xB1: len, serial, context, button, checkCount, checks[], textCount.
    const usize len = 3 + 4 + 4 + 4 + 4 + checkCount * 4 + 4;
    std::vector<u8> pkt(len, 0);
    pkt[0] = 0xB1;
    pkt[1] = static_cast<u8>((len >> 8) & 0xFF);
    pkt[2] = static_cast<u8>(len & 0xFF);
    usize p = 3;
    auto put32 = [&](u32 v) {
        pkt[p++] = static_cast<u8>((v >> 24) & 0xFF);
        pkt[p++] = static_cast<u8>((v >> 16) & 0xFF);
        pkt[p++] = static_cast<u8>((v >> 8) & 0xFF);
        pkt[p++] = static_cast<u8>(v & 0xFF);
    };
    put32(serial);
    put32(context);
    put32(button);
    put32(static_cast<u32>(checkCount));
    for (usize i = 0; i < checkCount; ++i) put32(checks[i]);
    put32(0);   // no text entries
    Send(pkt.data(), pkt.size(), "0xB1 gump response");
}

bool Client::AnswerGump(u32 button, u32 optionId) {
    if (!gump_.active) return false;
    const u32 checks[1] = { optionId };
    SendGumpResponse(gump_.serial, gump_.context, button,
                     optionId ? checks : nullptr, optionId ? 1u : 0u);
    gump_ = ActiveGump{};
    return true;
}

bool Client::CloseGump() {
    if (!gump_.active) return false;
    // Button 0 is "cancel" by Sphere convention (`onbutton=0` / no match).
    SendGumpResponse(gump_.serial, gump_.context, 0, nullptr, 0);
    gump_ = ActiveGump{};
    return true;
}

// ---------------------------------------------------------------------------
// M4 world/perception helpers for the life layer.
//
// Both of these answer questions a HUMAN CLIENT can answer. The tree lookup
// reads the shard's own statics, which every client is sent; the hostile scan
// reads notoriety and the health bar, which is what a player sees over a
// mobile's head. Neither reaches for anything the server does not send.
// ---------------------------------------------------------------------------

namespace {

// THE SHARD'S OWN LIST, not a name heuristic.
//
// Every graphic here carries `TYPE=t_tree` in runtime/scripts/items/
// i_vegetation.scp -- 51 of them, extracted from the itemdefs rather than
// guessed. That distinction is load-bearing: the first M4 live run filtered
// statics by "the tiledata name contains 'tree'", which also matches
// decorative foliage and canopy tiles, and Source-X answered every swing with
// `It appears immune to your blow` (CClientTarg.cpp:1990 -- the target
// resolved to an item that is not a harvestable type).
//
// A static NOT in this list cannot be chopped on this shard, however much its
// name looks like a tree.
constexpr u16 kTreeGraphics[] = {
    0x224A, 0x224B, 0x224C, 0x224D, 0x246C, 0x2476, 0x247D, 0x26ED,
    0x309C, 0x30BD, 0x30C3, 0x30C8, 0x30CF, 0x30D4, 0x30DA, 0x9E38,
    0x0CCA, 0x0CCB, 0x0CCC, 0x0CCD, 0x0CD0, 0x0CD3, 0x0CD6, 0x0CD8,
    0x0CD9, 0x0CDA, 0x0CDD, 0x0CE0, 0x0CE3, 0x0CE6, 0x0CF8, 0x0CFB,
    0x0CFE, 0x0D01, 0x0D41, 0x0D42, 0x0D43, 0x0D44, 0x0D57, 0x0D58,
    0x0D59, 0x0D5A, 0x0D5B, 0x0D6E, 0x0D6F, 0x0D70, 0x0D71, 0x0D72,
    0x0D84, 0x0D85, 0x0D86,
};

bool GraphicIsTree(u16 graphic) {
    for (u16 g : kTreeGraphics) {
        if (g == graphic) return true;
    }
    return false;
}

}  // namespace

bool Client::NearestTree(i32 x, i32 y, int radius, TreeHit* out,
                         const std::vector<std::pair<i32, i32>>* exclude,
                         bool* allGuarded) {
    if (allGuarded) *allGuarded = false;
    if (!out) return false;
    if (!EnsureWorldLoaded() || !world_) return false;
    if (radius < 0) radius = 0;
    // Atlas is a separate load from the map statics above; harmless to ask
    // again if a prior caller already brought it up.
    EnsureWorldKnowledge();

    std::vector<world::StaticHit> hits;
    world_->CollectStatics(x, y, radius, hits);

    bool found = false;
    bool sawCandidate = false;   // passed graphic+exclude, before the guard test
    i32 bestDist = 0;
    for (const world::StaticHit& h : hits) {
        if (!GraphicIsTree(h.itemId)) continue;
        if (exclude) {
            bool skip = false;
            for (const auto& e : *exclude) {
                if (e.first == h.x && e.second == h.y) { skip = true; break; }
            }
            if (skip) continue;
        }
        sawCandidate = true;
        // OWNER RULE: no gathering inside guarded zones. Tarath chopped a
        // tree at (1449,1635) inside guarded a_townBritain because nothing
        // here ever asked the atlas. A candidate whose own tile is guarded
        // is skipped outright, exactly like a worked-out trunk.
        if (world_knowledge_ && world_knowledge_->ok) {
            const wm::Region* r = world_knowledge_->atlas.RegionAt(h.x, h.y);
            if (r && r->flags.guarded) continue;
        }
        const i32 dx = h.x > x ? h.x - x : x - h.x;
        const i32 dy = h.y > y ? h.y - y : y - h.y;
        const i32 d = dx > dy ? dx : dy;
        if (found && d >= bestDist) continue;
        bestDist = d;
        out->x = h.x;
        out->y = h.y;
        out->z = h.z;
        out->graphic = h.itemId;
        found = true;
    }
    // Every survivor of exclude+graphic was guarded away: this is a town
    // square, not an empty forest.
    if (allGuarded) *allGuarded = !found && sawCandidate;
    return found;
}

bool Client::NearestForge(i32 x, i32 y, int radius, TreeHit* out,
                          const std::vector<std::pair<i32, i32>>* exclude) {
    if (!out) return false;
    if (radius < 0) radius = 0;

    // FORGES ARE WORLD ITEMS, NOT MAP STATICS.
    //
    // An earlier version of this read the .mul statics, on the strength of a
    // grep over the save files that found nothing. That grep was wrong twice:
    // the world save keys items by DEFNAME, not by hex id, and the keyword is
    // WORLDITEM, not ITEM -- so `^ITEM 0*(fb1|197a)` could never have matched
    // anything. There are 107 forges in spherestatics.scp: 21 i_forge, 26
    // i_forge_large_bellows and 60 i_forge_large. Six of them stand in The
    // Forgery in Minoc (2467-2469, 555/557), one beside the Minoc armorer at
    // 2535,571, and one INSIDE the Minoc mine at 2561,501 -- which is why a
    // miner can often smelt without leaving the rock face at all.
    //
    // The server sends these like any other item, so the item list is the
    // right place to look and the map statics never held them.
    //
    // Which ids count comes from TYPE=t_forge and its DUPEITEM runs:
    // 0fb1 i_forge, 02dd8 i_forge_elven, and 0197a..019a9, which are the
    // animation frames of a lit forge -- so the id actually on the wire is
    // usually not the base. 0fb0 is deliberately excluded: it looks like it
    // belongs beside 0fb1 but dupes to 0faf, which is i_anvil.
    auto isForgeId = [](u16 id) -> bool {
        return id == 0x0FB1 || id == 0x2DD8 ||
               (id >= 0x197A && id <= 0x19A9) ||
               (id >= 0x423B && id <= 0x4243) ||
               (id >= 0x4263 && id <= 0x4272) ||
               (id >= 0x4277 && id <= 0x4286) ||
               (id >= 0x44C7 && id <= 0x44CA);
    };

    bool found = false;
    i32 bestDist = 0;
    for (const auto& kv : items_) {
        if (!isForgeId(kv.second.itemId)) continue;
        if (exclude) {
            bool skip = false;
            for (const auto& e : *exclude)
                if (e.first == kv.second.x && e.second == kv.second.y) {
                    skip = true; break;
                }
            if (skip) continue;
        }
        const i32 dx = kv.second.x > x ? kv.second.x - x : x - kv.second.x;
        const i32 dy = kv.second.y > y ? kv.second.y - y : y - kv.second.y;
        const i32 d = dx > dy ? dx : dy;      // Chebyshev, as isneartype uses
        if (d > radius) continue;
        if (found && d >= bestDist) continue;
        bestDist = d;
        out->x = kv.second.x;
        out->y = kv.second.y;
        out->z = static_cast<i8>(kv.second.z);
        out->graphic = kv.second.itemId;
        forgeSerial_ = kv.first;   // the forge itself is what gets clicked
        found = true;
    }
    return found;
}

int Client::TreeCount(i32 x, i32 y, int radius) {
    if (!EnsureWorldLoaded() || !world_) return 0;
    if (radius < 0) radius = 0;
    std::vector<world::StaticHit> hits;
    world_->CollectStatics(x, y, radius, hits);
    int n = 0;
    for (const world::StaticHit& h : hits) {
        if (GraphicIsTree(h.itemId)) ++n;
    }
    return n;
}

bool Client::JournalSaidSince(const char* needle, i64 sinceMs) const {
    if (!needle || !needle[0]) return false;
    std::string want(needle);
    for (char& c : want) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) {
        if (it->timeMs < sinceMs) break;   // journal is in time order
        std::string hay = it->text;
        for (char& c : hay) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (hay.find(want) != std::string::npos) return true;
    }
    return false;
}

i32 Client::JournalNumberSince(const char* needle, i64 sinceMs) const {
    if (!needle || !needle[0]) return -1;
    std::string want(needle);
    for (char& c : want) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) {
        if (it->timeMs < sinceMs) break;
        std::string hay = it->text;
        for (char& c : hay) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (hay.find(want) == std::string::npos) continue;
        for (usize i = 0; i < it->text.size(); ++i) {
            if (it->text[i] < '0' || it->text[i] > '9') continue;
            i64 v = 0;
            while (i < it->text.size() && it->text[i] >= '0' && it->text[i] <= '9') {
                v = v * 10 + (it->text[i] - '0');
                if (v > 100000000) return -1;   // not a price
                ++i;
            }
            return static_cast<i32>(v);
        }
        return -1;   // matched the line but it carries no number
    }
    return -1;
}

// AN EXCLUSIVE MARK. "Read replies after this point" has to EXCLUDE the line
// that was already there, and this returned the last entry's own timestamp
// while JournalSaidSince breaks on `timeMs < sinceMs` -- so the entry equal to
// the mark was still matched. The mark included the very message it existed to
// exclude.
//
// What that cost: Ysolde asked Alenne to teach Meditation and Alenne answered
// "You already know as much as I can teach of Meditation". That line became
// the last journal entry. She then walked to Caedmon, the mage guildmaster who
// CAN teach it, marked the journal -- getting Alenne's timestamp, because
// nothing had been said since -- asked him, and he said nothing at all. Two
// seconds later the refusal scan matched ALENNE's line and recorded a durable
// verdict against CAEDMON, who was thereafter skipped for a refusal he never
// made (run_m5/p0gate7:433-438; there is no Caedmon reply in that window).
//
// A silent NPC was therefore always recorded as repeating whatever the
// previous NPC had said. The same hazard applies to every other watermarked
// read -- the chop result, the craft result, the fishing cast. One call site
// had already worked around it locally with a `+ 1`; the mark itself was the
// bug.
i64 Client::JournalNowMs() const {
    if (journal_.empty()) return NowMs();
    return journal_.back().timeMs + 1;
}

// Everything nearby, no notoriety filter. Same record as ScanHostiles so a
// caller can share code; the only difference is who is left out, and here
// nobody is.
int Client::PlayersNearby(int maxDist) const {
    int n = 0;
    for (const MobileObj& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        // Human bodies only: 0x0190 male, 0x0191 female. A sheep is not an
        // audience.
        if (m.body != 0x0190 && m.body != 0x0191) continue;
        const int dx = m.x - playerX_, dy = m.y - playerY_;
        const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if ((ax > ay ? ax : ay) > maxDist) continue;
        // AN UNKNOWN TITLE IS NOT A PLAYER.
        //
        // The first version counted a mobile whose paperdoll had not arrived
        // as a person, on the grounds that being wrong that way was harmless.
        // It is not: paperdolls arrive only when asked for, so a bank full of
        // NPCs reads as a bank full of customers, and Corwyn announced "WTS 30
        // i_ingot_iron" six times to a room with nobody in it.
        //
        // Requiring a KNOWN title with no " the " in it means the answer is
        // "yes, a person" only where there is evidence of one. The cost of
        // being wrong the other way is a missed sale; the cost of being wrong
        // this way was the shouting the owner asked to stop.
        const char* title = PaperdollTitle(m.serial);
        if (!title || !*title) continue;                    // unknown: assume NPC
        if (std::strstr(title, " the ")) continue;           // a titled NPC
        ++n;
    }
    return n;
}

u32 Client::AudienceFingerprint(int maxDist) const {
    // Same test PlayersNearby uses, summed rather than counted. XOR would
    // cancel a pair out; a sum of serials will not, and exact identity is not
    // needed -- only "has this room changed".
    u32 sum = 0;
    for (const MobileObj& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        if (m.body != 0x0190 && m.body != 0x0191) continue;
        const int dx = m.x - playerX_, dy = m.y - playerY_;
        const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if ((ax > ay ? ax : ay) > maxDist) continue;
        const char* title = PaperdollTitle(m.serial);
        if (!title || !*title) continue;
        if (std::strstr(title, " the ")) continue;
        sum += m.serial;
    }
    return sum;
}

int Client::ScanMobiles(int maxDist, std::vector<HostileHit>& out) const {
    out.clear();
    for (const MobileObj& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        const int dx = m.x - playerX_, dy = m.y - playerY_;
        const int d = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                          ? (dx < 0 ? -dx : dx)
                          : (dy < 0 ? -dy : dy);
        if (d > maxDist) continue;
        HostileHit h;
        h.serial = m.serial;
        h.x = m.x; h.y = m.y; h.z = m.z;
        h.noto = m.noto;
        const char* nm = MobileName(m.serial);
        if (nm) h.name = nm;
        out.push_back(h);
    }
    return static_cast<int>(out.size());
}

int Client::ScanHostiles(int maxDist, std::vector<HostileHit>& out) const {
    out.clear();
    if (maxDist < 0) maxDist = 0;
    for (const MobileObj& m : mobileCache_) {
        if (m.serial == playerSerial_) continue;
        // Notoriety is the whole filter, and it is deliberately conservative.
        // 1 = innocent (the farm animals a guard will execute us over),
        // 2 = guild ally, 7 = invulnerable (guards, some NPCs). Everything
        // else -- gray, orange, red -- is something a player may lawfully
        // fight. A mobile whose notoriety has not arrived yet (0) is NOT
        // assumed hostile: an unknown is not a target.
        if (m.noto == 0 || m.noto == 1 || m.noto == 2 || m.noto == 7) continue;
        const i32 dx = m.x > playerX_ ? m.x - playerX_ : playerX_ - m.x;
        const i32 dy = m.y > playerY_ ? m.y - playerY_ : playerY_ - m.y;
        const i32 d = dx > dy ? dx : dy;
        if (d > maxDist) continue;
        HostileHit h;
        h.serial = m.serial;
        h.x = m.x;
        h.y = m.y;
        h.z = m.z;
        h.noto = m.noto;
        h.hpCur = m.hpCur;
        h.hpMax = m.hpMax;
        h.warMode = m.warMode;
        const char* name = MobileName(m.serial);
        h.name = name ? name : "";
        out.push_back(std::move(h));
    }
    return static_cast<int>(out.size());
}


void Client::JournalHeardSince(i64 sinceMs, std::vector<Heard>& out) const {
    out.clear();
    for (const JournalEntry& e : journal_) {
        if (e.timeMs <= sinceMs) continue;
        // Our own speech is not news. Without this a seller answers its own
        // WTS and opens a trade window with itself.
        if (e.sourceSerial == playerSerial_) continue;
        // System messages carry no speaker to walk to.
        if (e.sourceSerial == 0 || e.sourceSerial == 0xFFFFFFFFu) continue;
        Heard h;
        h.speaker = e.sourceSerial;
        h.text = e.text;
        h.timeMs = e.timeMs;
        h.hasPosition = e.hasPosition;
        h.x = e.x; h.y = e.y; h.z = e.z;
        if (const char* n = MobileName(e.sourceSerial)) h.name = n;
        out.push_back(std::move(h));
    }
}


// Is there water at (tx,ty), in either physical form? Fills z with the
// surface a cast should target and graphic with 0 (wet land) or the wet
// static's id. Shared by NearestWater and NearestFishingSpot so the two can
// never disagree about what counts as water -- they disagreed once, and the
// fisher looped between them for a whole session.
bool Client::WaterAt(i32 tx, i32 ty, i8* z, u16* graphic) {
    if (tx < 0 || ty < 0) return false;
    map::LandCell cell{};
    if (!worldMap_->ReadCell(static_cast<u32>(tx), static_cast<u32>(ty),
                             &cell)) {
        return false;
    }
    if ((tileData_->Land(cell.tileId).flags & tiledata::kFlagWet) != 0) {
        if (z) *z = cell.z;
        if (graphic) *graphic = 0;
        return true;
    }
    // Wet STATICS: the coastline form (see the WaterHit comment in Client.h).
    // The test is the tiledata Wet flag, not an id whitelist -- the flag is
    // what makes 0x1796-0x17B2 water, and a whitelist is exactly the kind of
    // client-side guess that once rejected all the near water.
    std::vector<world::StaticHit> hits;
    world_->CollectStatics(tx, ty, 0, hits);
    for (const world::StaticHit& h : hits) {
        if ((tileData_->Static(h.itemId).flags & tiledata::kFlagWet) == 0)
            continue;
        if (z) *z = h.z;
        if (graphic) *graphic = h.itemId;
        return true;
    }
    return false;
}

bool Client::NearestWater(i32 x, i32 y, int radius, WaterHit* out) {
    if (!out) return false;
    if (!EnsureWorldLoaded() || !world_ || !worldMap_ || !tileData_) return false;
    if (radius < 0) radius = 0;

    // Nearest-first, so a character fishes from where it stands rather than
    // walking to the far side of the lake. r STARTS AT 0, which makes
    // radius 0 mean "is THIS tile water" -- the form a caller needs when it
    // already knows which tile it wants to test.
    for (int r = 0; r <= radius; ++r) {
        for (i32 dy = -r; dy <= r; ++dy) {
            for (i32 dx = -r; dx <= r; ++dx) {
                // Only the ring at distance r; the interior was covered by a
                // previous pass.
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const i32 tx = x + dx, ty = y + dy;

                // NO STATIC FILTER. The first version rejected any water tile
                // carrying a static that provides a surface, meaning to skip
                // planks and bridges -- and around a dock that is most of the
                // near water, so the search skipped everything close and
                // returned a tile ten tiles out that could not be reached.
                //
                // Deciding what is fishable is the SERVER'S job. Sphere
                // answers a bad target itself, and this project's whole
                // discipline is to ask rather than to out-think it: the cast
                // reads the reply and moves on. Guessing here cost a fisher
                // its entire session.
                i8 wz = 0;
                u16 gfx = 0;
                if (!WaterAt(tx, ty, &wz, &gfx)) continue;

                out->x = tx;
                out->y = ty;
                out->z = wz;
                out->graphic = gfx;
                return true;
            }
        }
    }
    return false;
}


bool Client::NearestFishingSpot(i32 x, i32 y, int radius, FishingSpot* out,
                                const std::vector<std::pair<i32, i32>>* exclude) {
    if (!out) return false;
    if (!EnsureWorldLoaded() || !world_ || !worldMap_ || !tileData_) return false;
    if (radius < 0) radius = 0;

    auto excluded = [&](i32 tx, i32 ty) -> bool {
        if (!exclude) return false;
        for (const auto& e : *exclude)
            if (e.first == tx && e.second == ty) return true;
        return false;
    };

    // Search outward from where the character stands, so it fishes from the
    // nearest bank rather than hiking to the far side of the bay.
    for (int r = 0; r <= radius; ++r) {
        for (i32 dy = -r; dy <= r; ++dy) {
            for (i32 dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const i32 sx = x + dx, sy = y + dy;
                if (sx < 0 || sy < 0) continue;
                if (excluded(sx, sy)) continue;

                // The STANDING tile must be one the character can actually
                // occupy, and the ONLY judge of that is the pathfinder's own
                // World::QueryCell, statics included. "Dry by land tiledata"
                // once nominated (1463,1754), a tile under a coastline water
                // static (walkable:false), and the walk to it could not
                // succeed by construction: the A* worker itself refuses an
                // unwalkable goal ("goal not walkable", Navigation.cpp
                // BotPollPathPlanner). QueryCell alone is also the right
                // wetness test -- wet land is unwalkable by definition
                // (World.cpp treats Wet like Impassable) and a dock plank
                // over a wet static IS a legal place to stand, which a
                // separate "no water here" check would wrongly reject.
                world::WalkQuery q{};
                q.x = static_cast<u32>(sx);
                q.y = static_cast<u32>(sy);
                q.fromZ = playerZ_;
                if (!world_->QueryCell(q).walkable) continue;

                // ...and water has to be in casting reach of it. RANGE=4 in
                // skill18_fishing.scp is the MAXIMUM; there is also a minimum
                // the script does not state and the server does:
                //
                //     "You cannot fish so close to yourself."
                //
                // Adjacent water is refused, so the search starts at 2. Found
                // live, by casting -- which is the point: the shard answers
                // this question and guessing at it is what wasted the session
                // before.
                for (int wr = 2; wr <= 4; ++wr) {
                    for (i32 wy = -wr; wy <= wr; ++wy) {
                        for (i32 wx = -wr; wx <= wr; ++wx) {
                            if (std::max(std::abs(wx), std::abs(wy)) != wr) continue;
                            const i32 tx = sx + wx, ty = sy + wy;
                            if (excluded(tx, ty)) continue;
                            i8 wz = 0;
                            u16 gfx = 0;
                            if (!WaterAt(tx, ty, &wz, &gfx)) continue;
                            out->standX = sx; out->standY = sy;
                            out->waterX = tx; out->waterY = ty;
                            out->waterZ = wz;
                            out->waterGraphic = gfx;
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

} // namespace uo
