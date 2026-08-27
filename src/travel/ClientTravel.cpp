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
#include "uo/rules.h"

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

bool Client::WithinPlace(const char* nameOrId) const {
    if (!world_knowledge_ || !world_knowledge_->ok) return false;
    const wm::Place* p = world_knowledge_->atlas.FindPlace(nameOrId);
    if (!p) return false;
    return Chebyshev(playerX_, playerY_, p->position.x, p->position.y) <=
           p->radius;
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
    travelStartedDead_ = IsDead();
    // The destination's own floor, where the world data knows one. Britannia's
    // shops are two and three storeys and the shard's spawner rows carry the z
    // the NPC actually stands on; without it a bot can "arrive" on the balcony.
    travelHasGoalZ_ = hasZ;
    travelGoalZ_ = z;
    travelEscapes_ = 0;
    travelEscapeTried_.clear();
    travelAvoidPads_.clear();
    journey_.Begin(travelLabel_.c_str(), x, y, arriveRadius, NowMs());
    travelLastSampleMs_ = 0;

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
    if (!EnsureWorldKnowledge()) {
        travelFailure_ = WorldKnowledgeError();
        return false;
    }

    // Live state beats stored state: if this character has actually seen a
    // provider of this service recently, go to where it saw one rather than to
    // where the shard's spawner table says the shop is.
    if (const travel::ServiceSighting* seen =
            knowledge_.RecentService(s, NowMs(), kServiceSightingMaxAgeMs)) {
        travelEntitySerial_ = seen->serial;
        travelEntityWithin_ = 2;
        char label[96];
        std::snprintf(label, sizeof(label), "%s (seen)", wm::ServiceName(s));
        return TravelBegin(label, seen->x, seen->y, 2, /*hasZ=*/true,
                           seen->z);
    }

    const wm::Place* p =
        regionHint && *regionHint
            ? world_knowledge_->atlas.NearestPlaceWithServiceInRegion(
                  s, regionHint, playerX_, playerY_)
            : world_knowledge_->atlas.NearestPlaceWithService(s, playerX_,
                                                              playerY_);
    if (!p) {
        travelFailure_ = "no known provider of that service";
        LogWarn("[travel] no place offers %s%s%s\n", wm::ServiceName(s),
                regionHint && *regionHint ? " in " : "",
                regionHint ? regionHint : "");
        return false;
    }
    travelEntitySerial_ = 0;
    return TravelBegin(p->name.c_str(), p->position.x, p->position.y,
                       p->radius, /*hasZ=*/true, p->position.z);
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
    if (!MobilePosition(serial, &mx, &my)) {
        travelFailure_ = "that mobile is not in view";
        return false;
    }
    travelEntitySerial_ = serial;
    travelEntityWithin_ = within > 0 ? within : 1;
    char label[64];
    std::snprintf(label, sizeof(label), "mobile 0x%08X", serial);
    return TravelBegin(label, mx, my, travelEntityWithin_);
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
    // test is two-dimensional -- it plans on a 2D grid -- so the floor is
    // checked here, where the destination's z is known, rather than reporting
    // a success the character cannot act on.
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
    if (!MobilePosition(travelEntitySerial_, &mx, &my)) {
        // Out of view. The last known position still stands as a destination:
        // the server will show us the mobile again when we get near it.
        return;
    }
    if (Chebyshev(mx, my, journey_.GoalX(), journey_.GoalY()) <= 2) return;
    // The mobile wandered. Re-aim rather than walk to where it used to be.
    journey_.Begin(travelLabel_.c_str(), mx, my, travelEntityWithin_, NowMs());
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
        journey_.OnPositionSample(playerX_, playerY_, now);
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
            journey_.OnLegArrived(playerX_, playerY_, now);
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
        case travel::Command::Finish:    TravelFinish(true, ""); break;
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
            TravelFinish(false, journey_.FailureDetail().empty()
                                    ? travel::FailureName(journey_.FailureReason())
                                    : journey_.FailureDetail().c_str());
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

    // The atlas speaks Sphere's job defnames; a paperdoll speaks English. They
    // agree for most trades, and the few that differ are spelled out here.
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

    wm::Service svc = wm::Service::None;
    for (const Alias& a : kAliases)
        if (job == a.title) { svc = a.svc; break; }
    if (svc == wm::Service::None) svc = wm::ServiceFromName(job.c_str());
    if (svc == wm::Service::None) return;

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

void Client::WarModeTick() {
    if (!IsInWorld()) return;
    const i64 now = NowMs();

    // A target we can no longer see is a target that is gone. The out-of-range
    // purge already removes it from mobileCache_, so this is just reading what
    // the session already knows.
    const u32 target = war_.TargetSerial();
    if (target && !FindMobileBySerial(target)) war_.OnTargetGone(target, now);

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

    LogInfo("[gump] 0x%08X context=0x%08X: %zu option(s)\n", serial, context,
            gump_.options.size());
    for (const GumpOption& o : gump_.options)
        LogInfo("[gump]   %s %u = '%s'\n", o.button ? "button" : "choice",
                o.id, o.label.c_str());
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

} // namespace uo
