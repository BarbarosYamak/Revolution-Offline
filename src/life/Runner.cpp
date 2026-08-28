#include "life/Runner.h"

#include "Client.h"
#include "uo/log.h"
#include "uo/vendor_policy.h"
#include "uo/world_model.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace uo::life {

namespace {

// Item graphics, all read off the runtime's own itemdefs -- never guessed from
// generic UO tables. i_hatchet is [ITEMDEF 0f43] with DUPELIST 0f44 (the
// flipped graphic); i_log is 0x1BDD; i_bandage is [ITEMDEF 0e21].
constexpr u16 kHatchet[]  = {0x0F43, 0x0F44};
constexpr u16 kAxe[]      = {0x0F49, 0x0F4A};
constexpr u16 kLog        = 0x1BDD;
constexpr u16 kBandage    = 0x0E21;
constexpr u16 kKatana[]   = {0x13FE, 0x13FF};
constexpr u16 kFood[]     = {0x103B, 0x09EB, 0x09F2};

// The two hand layers. Which one an item lands on is decided by THIS SHARD'S
// tiledata, not by generic UO: the newbie katana wears on layer 1 and the
// hatchet wears on layer 2, so a bot that hardcodes "the weapon layer" gets
// `worn on a different layer` from the server and never arms its own axe.
// Equips therefore ask for layer 0 -- "wherever this belongs" -- and the hands
// are READ as a pair.
constexpr u8 kLayerHand1 = 0x01;
constexpr u8 kLayerHand2 = 0x02;
constexpr u8 kLayerServerChooses = 0x00;

i32 CountAny(Client& c, const u16* list, usize n) {
    i32 total = 0;
    for (usize i = 0; i < n; ++i) total += static_cast<i32>(c.BackpackItemCount(list[i]));
    return total;
}

u32 FindAny(Client& c, const u16* list, usize n) {
    for (usize i = 0; i < n; ++i) {
        const u32 s = c.FindBackpackItemByGraphic(list[i]);
        if (s) return s;
    }
    return 0;
}

bool GraphicIsAny(u16 graphic, const u16* list, usize n) {
    for (usize i = 0; i < n; ++i) {
        if (graphic == list[i]) return true;
    }
    return false;
}

// Serial of the axe currently in a hand, or 0. The newbie kit arms a katana
// (from the Swordsmanship request), so "a hand is full" is NOT "the axe is in
// hand" -- and swinging a katana at a tree earns nothing but "The tool is out
// of charges", forever, silently.
u32 AxeSerialInHand(Client& c) {
    for (u8 layer : {kLayerHand1, kLayerHand2}) {
        const u16 g = c.EquippedGraphicAt(layer);
        if (GraphicIsAny(g, kHatchet, 2) || GraphicIsAny(g, kAxe, 2)) {
            return c.EquippedAtLayer(layer);
        }
    }
    return 0;
}

bool AxeInHand(Client& c) { return AxeSerialInHand(c) != 0; }

bool HandsBusy(Client& c) {
    return c.EquippedAtLayer(kLayerHand1) != 0 || c.EquippedAtLayer(kLayerHand2) != 0;
}

i32 TileDist(i32 ax, i32 ay, i32 bx, i32 by) {
    return std::max(ax > bx ? ax - bx : bx - ax, ay > by ? ay - by : by - ay);
}

}  // namespace

Runner::Runner() = default;
Runner::~Runner() = default;

void Runner::LogLine(const char* fmt, ...) const {
    if (!cfg_.verbose) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // One prefix for the whole autonomous layer, so a run can be read with
    // `findstr [life]` and nothing else. Deliberately NOT a packet trace: the
    // M4 brief asks for choices to be explainable without one.
    LogInfo("[life] %s\n", buf);
}

bool Runner::Configure(const RunnerConfig& cfg, std::string* err) {
    cfg_ = cfg;
    store_ = Store(cfg.dataRoot);

    const std::string id = MakeIdentityId(cfg.accountName, cfg.characterName);

    std::string loadErr;
    if (store_.Load(id, &state_, &loadErr)) {
        LogLine("loaded persistent state for %s (%zu places, %zu suppliers, "
                "%zu events, %zu prior sessions)",
                state_.identity.characterName.c_str(),
                state_.memory.Places().size(), state_.memory.Suppliers().size(),
                state_.memory.Events().size(), state_.sessions.size());
    } else if (!loadErr.empty()) {
        // A corrupt file is a hard stop. Silently starting a fresh life on top
        // of an unreadable one would destroy exactly the history M4 exists to
        // keep, and would look like a successful run.
        if (err) *err = loadErr;
        return false;
    } else {
        state_ = PersistentState{};
        state_.identity.identityId    = id;
        state_.identity.accountName   = cfg.accountName;
        state_.identity.characterName = cfg.characterName;
        state_.plan = FrontierLumberjackSwordsman();
        LogLine("no prior state for %s: this is a new life", id.c_str());
    }

    // Whatever the source -- a fresh plan or one reloaded from disk -- it has
    // to be a legal Revolution build before the character acts on it.
    const PlanCheck check = ValidatePlan(rules::Revolution(), state_.plan);
    if (!check.ok) {
        if (err) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "build plan is not a legal Revolution build: %s (skill %d)",
                          PlanViolationName(check.violation), check.skillId);
            *err = buf;
        }
        return false;
    }
    LogLine("build plan '%s': %.1f resolved + %.1f unresolved = %.1f/%.1f, "
            "stats %d/%d/%d = %d",
            state_.plan.family.c_str(), check.resolvedTenths / 10.0,
            state_.plan.unresolvedTenths / 10.0, check.plannedTotalTenths / 10.0,
            rules::Revolution().totalSkillCapTenths / 10.0,
            state_.plan.targetStr, state_.plan.targetDex, state_.plan.targetInt,
            check.statTotal);

    // The goal that came back from disk is an INTENTION. Its clock and
    // counters are transient and are rebuilt at reconciliation.
    planner_.Mutable() = state_.goal;
    configured_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Observation -- source of truth C, rebuilt from scratch every tick.
// ---------------------------------------------------------------------------

Observation Runner::Observe(Client& client, i64 nowMs) const {
    Observation obs;
    obs.nowMs = nowMs;
    obs.inWorld = client.IsInWorld();
    if (!obs.inWorld) return obs;

    obs.dead    = client.IsDead();
    obs.mounted = client.PlayerIsMounted();
    obs.warMode = client.WarModeOn();

    obs.x = client.PlayerX();
    obs.y = client.PlayerY();
    obs.z = client.PlayerZ();

    obs.hp    = client.PlayerHp();
    obs.hpMax = client.PlayerHpMax();
    obs.mana  = client.PlayerMana();

    obs.str   = client.PlayerStr();
    obs.dex   = client.PlayerDex();
    obs.intel = client.PlayerInt();

    for (const SkillTarget& t : state_.plan.skills) {
        const i32 base = client.PlayerSkillBase(static_cast<u16>(t.skillId));
        if (base >= 0) obs.skills.push_back({t.skillId, base});
    }

    obs.gold      = client.PlayerGold();
    obs.weight    = client.PlayerWeight();
    obs.maxWeight = client.PlayerMaxWeight();

    obs.bandages = static_cast<i32>(client.BackpackItemCount(kBandage));
    obs.logs     = static_cast<i32>(client.BackpackItemCount(kLog));
    obs.food     = CountAny(client, kFood, sizeof(kFood) / sizeof(kFood[0]));

    obs.axeInPack = FindAny(client, kHatchet, 2) != 0 || FindAny(client, kAxe, 2) != 0;
    obs.weaponEquipped = HandsBusy(client);
    // Read the worn graphic rather than inferring from a full hand. The first
    // live run swung the newbie katana at a tree for two minutes because a
    // filled weapon hand was taken to mean "the axe is out".
    obs.axeEquipped = AxeInHand(client);

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    obs.hostilesNear = static_cast<i32>(hostiles.size());
    const u32 warTarget = client.WarWatchdog().TargetSerial();
    i32 adjacent = 0;
    for (const Client::HostileHit& h : hostiles) {
        if (TileDist(h.x, h.y, obs.x, obs.y) <= 1) ++adjacent;
    }
    // What the client can honestly claim about "how many are on me": a foe we
    // are in a fight with, plus anything hostile standing in melee range.
    obs.attackersOnMe = adjacent;
    obs.underAttack = warTarget != 0 || adjacent > 0;

    const travel::DeathRecord& death = client.Knowledge().LastDeath();
    obs.corpseKnown = death.valid && death.corpseSerial != 0;
    obs.corpseX = death.x;
    obs.corpseY = death.y;
    obs.corpseRecoveryAttempts = death.recoveryAttempts;

    // Arrival is a claim about the TILE. `TreeCount` asks the shard's own
    // statics whether there is anything here to chop, which travel success
    // does not answer (see docs/UOOFFLINE_BEHAVIOR_AUDIT.md section 3.6).
    obs.atWorkSite = client.TreeCount(obs.x, obs.y, cfg_.searchRadius) > 0;
    obs.treeAdjacent = client.TreeCount(obs.x, obs.y, 2) > 0;
    obs.atBank = client.BankContainer() != 0 &&
                 client.ContainerKnown(client.BankContainer());

    return obs;
}

// ---------------------------------------------------------------------------
// Learning. Only from things actually observed.
// ---------------------------------------------------------------------------

// Take the axe in hand, unequipping whatever is there first.
//
// A hand already holding something cannot take a second weapon: Sphere answers
// the lift-and-wear with "You put the hatchet in your pack" and the character
// keeps swinging the newbie katana at trees. So this is two actions, and the
// first live run is the reason it is not one.
//
// Returns true while the arming is still in progress (the caller should wait),
// false when the axe is in hand or there is no axe to arm.
bool Runner::ArmAxe(Client& client, const Observation& obs) {
    if (AxeInHand(client)) return false;
    if (client.ActionBusy()) return true;

    const u32 hatchet = FindAny(client, kHatchet, 2);
    const u32 axe = hatchet ? hatchet : FindAny(client, kAxe, 2);
    if (!axe) return false;   // nothing to arm; the caller decides what that means

    // Clear BOTH hands. Which one the axe wants is the shard's decision, so
    // the only reliable way to make room is to empty the pair.
    for (u8 layer : {kLayerHand1, kLayerHand2}) {
        const u32 worn = client.EquippedAtLayer(layer);
        if (!worn) continue;
        LogLine("arming the axe: taking off 0x%04X from layer %u first",
                client.EquippedGraphicAt(layer), static_cast<unsigned>(layer));
        client.ActionUnequip(worn);
        nextActionMs_ = obs.nowMs + 1400;
        return true;
    }

    // Layer 0 = "wherever this belongs". Naming a layer here is how the first
    // live run earned `worn on a different layer` and never armed its axe.
    LogLine("arming the axe");
    client.ActionEquip(axe, kLayerServerChooses);
    nextActionMs_ = obs.nowMs + 1600;
    return true;
}

void Runner::LearnFromObservation(Client& client, const Observation& obs) {
    if (obs.atWorkSite) {
        // "I have stood here and there are trees" is a SIGHTING, not a failed
        // harvest. The first live run scored 64 failures on a healthy stand in
        // ninety seconds because every idle tick counted as one.
        state_.memory.NoteResourceSeen("logs", obs.x, obs.y, obs.z, obs.nowMs);
    }
    if (obs.atBank) {
        state_.memory.NotePlace("bank", "bank", obs.x, obs.y, obs.z, obs.nowMs);
        if (!state_.memory.HasEvent("bank_learned")) {
            state_.memory.NoteEvent("bank_learned", "opened a bank box", "bank",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("memory_learned=PLACE kind=bank at %d,%d", obs.x, obs.y);
        }
    }

    // A healer we have actually seen. The world model records the sighting;
    // this copies it into the character's own memory so it survives logout.
    const travel::ServiceSighting* healer =
        client.Knowledge().RecentService(wm::Service::Healer, obs.nowMs,
                                         60 * 60 * 1000);
    if (healer) {
        state_.memory.NotePlace("healer", healer->title.c_str(), healer->x,
                                healer->y, healer->z, obs.nowMs);
    }

    state_.memory.ExpireDanger(obs.nowMs);
}

// ---------------------------------------------------------------------------
// Unreachable-foe memory (audit section 3.7). Lives on the RUNNER, not on the
// goal, so abandoning a fight and coming back does not forget it.
// ---------------------------------------------------------------------------

bool Runner::IsUnreachable(u32 serial, i64 nowMs) const {
    for (const auto& e : unreachable_) {
        if (e.first == serial && nowMs < e.second) return true;
    }
    return false;
}

void Runner::MarkUnreachable(u32 serial, i64 nowMs) {
    const i64 until = nowMs + 30 * 1000;
    for (auto& e : unreachable_) {
        if (e.first == serial) { e.second = until; return; }
    }
    unreachable_.emplace_back(serial, until);
    if (unreachable_.size() > 32) unreachable_.erase(unreachable_.begin());
}

// ---------------------------------------------------------------------------
// Checkpoint
// ---------------------------------------------------------------------------

bool Runner::Checkpoint(Client& client, i64 nowMs, const char* why) {
    if (!configured_) return false;
    state_.checkpointMs = nowMs;
    state_.goal = planner_.Current();
    if (client.IsInWorld()) {
        state_.lastKnownGold = client.PlayerGold();
        state_.lastKnownStr  = client.PlayerStr();
        state_.lastKnownDex  = client.PlayerDex();
        state_.lastKnownInt  = client.PlayerInt();
        state_.lastKnownX    = client.PlayerX();
        state_.lastKnownY    = client.PlayerY();
        state_.lastKnownDead = client.IsDead();
        state_.lastKnownSkills.clear();
        for (const SkillTarget& t : state_.plan.skills) {
            const i32 base = client.PlayerSkillBase(static_cast<u16>(t.skillId));
            if (base >= 0) state_.lastKnownSkills.push_back({t.skillId, base});
        }
    }
    std::string err;
    if (!store_.Save(state_, &err)) {
        LogError("[life] checkpoint failed (%s): %s\n", why, err.c_str());
        return false;
    }
    lastCheckpointMs_ = nowMs;
    LogLine("checkpoint (%s) -> %s", why,
            store_.PathFor(state_.identity.identityId).c_str());
    return true;
}

void Runner::EndSession(const char* why) {
    if (phase_ == Phase::WindDown || phase_ == Phase::LoggingOut ||
        phase_ == Phase::Done) {
        return;
    }
    LogLine("session_end_requested reason=\"%s\"", why ? why : "");
    phase_ = Phase::WindDown;
    windDownStartedMs_ = lastTickMs_;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void Runner::Tick(Client& client, i64 nowMs) {
    if (!configured_ || finished_) return;
    lastTickMs_ = nowMs;

    switch (phase_) {
        case Phase::AwaitWorld: {
            if (!client.IsInWorld()) return;
            if (sessionStartMs_ == 0) {
                sessionStartMs_ = nowMs;
                // The server has to tell us the skills before anything can be
                // reconciled against them.
                client.ActionRequestSkills();
                LogLine("in world; asked the server for the skill list");
                nextActionMs_ = nowMs + 2500;
                return;
            }
            if (nowMs < nextActionMs_) return;
            phase_ = Phase::Reconcile;
            return;
        }

        case Phase::Reconcile: {
            const Observation obs = Observe(client, nowMs);
            const ReconcileReport rep = Reconcile(&state_, obs);

            LogLine("reconciliation: %s, %d field(s) differed",
                    rep.firstEverLogin ? "first ever login" : "resuming a life",
                    rep.driftFields);
            for (const ReconcileLine& l : rep.lines) {
                LogLine("  %-12s persisted=%-10s server=%-10s -> %s",
                        l.field.c_str(), l.persisted.c_str(), l.server.c_str(),
                        l.result.c_str());
            }
            if (rep.goalDropped) {
                LogLine("  restored objective DROPPED: %s", rep.goalDropReason.c_str());
            } else if (state_.goal.active) {
                LogLine("  restored objective KEPT: %s (progress %d)",
                        GoalKindName(state_.goal.kind), state_.goal.progress);
            }
            planner_.Mutable() = state_.goal;

            // Transient observations from the previous life are gone by
            // construction -- they were never persisted -- but the runtime
            // caches that DO exist in this process are cleared explicitly so
            // nothing carries over between sessions in one host.
            unreachable_.clear();
            chopTargetValid_ = false;
            chopCursorPending_ = false;
            travelInFlight_ = false;

            session_ = SessionSummary{};
            session_.startedMs = nowMs;
            session_.goldStart = obs.gold;
            session_.skillTenthsStart = obs.SkillSumTenths();
            logsAtSessionStart_ = obs.logs;

            state_.identity.sessions++;

            // Survival on, and left on. M3.9.1 proved this path live: a
            // character disengaged at ~32%, bandaged, and survived.
            client.SetSurvivalEnabled(true);
            client.EnsurePeaceMode();

            Checkpoint(client, nowMs, "login reconciled");
            phase_ = Phase::Live;
            return;
        }

        case Phase::Live: {
            if (!client.IsInWorld()) return;
            const Observation obs = Observe(client, nowMs);
            LearnFromObservation(client, obs);

            // --- session limits -------------------------------------------
            const i64 elapsed = nowMs - sessionStartMs_;
            if (cfg_.sessionLimitMs > 0 && elapsed >= cfg_.sessionLimitMs) {
                // Deferral rules from the audit (section 3.13): never end a
                // session on top of a corpse run, and never while dead.
                if (!obs.dead && planner_.Current().kind != GoalKind::RecoverCorpse) {
                    EndSession("session time limit reached");
                    return;
                }
            }
            if (cfg_.goalLimit > 0 && session_.goalsCompleted >= cfg_.goalLimit &&
                !obs.dead) {
                EndSession("goal limit reached");
                return;
            }

            // --- decide ----------------------------------------------------
            const std::vector<Need> needs =
                AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
            std::string why;
            const GoalKind previous = planner_.Current().kind;
            const bool wasActive = planner_.Current().active;
            if (planner_.Select(needs, obs, state_.memory, nowMs, &why)) {
                session_.goalsAttempted++;
                if (wasActive) {
                    LogLine("goal_changed=%s from=%s reason=\"%s\"",
                            GoalKindName(planner_.Current().kind),
                            GoalKindName(previous), why.c_str());
                } else {
                    LogLine("goal=%s reason=\"%s\"",
                            GoalKindName(planner_.Current().kind), why.c_str());
                }
                LogGoalChange(obs, why);
                // A new goal starts from a clean transient slate.
                chopTargetValid_ = false;
                chopCursorPending_ = false;
                travelInFlight_ = false;
                travelAttempts_ = 0;
                logsAtGoalStart_ = obs.logs;
            }

            // --- act -------------------------------------------------------
            RunGoal(client, obs);

            // --- checkpoint ------------------------------------------------
            if (cfg_.checkpointIntervalMs > 0 &&
                nowMs - lastCheckpointMs_ >= cfg_.checkpointIntervalMs) {
                Checkpoint(client, nowMs, "periodic");
            }
            return;
        }

        case Phase::WindDown: {
            if (!client.IsInWorld()) { phase_ = Phase::Done; finished_ = true; return; }
            // Logging out somewhere hostile is how this project lost three
            // characters -- Source-X does not drop a combat-flagged connection
            // immediately, and one died in the gap AFTER logout_ack. So the
            // wind-down walks to a known bank first and only then logs out.
            client.EnsurePeaceMode();

            if (client.TravelBusy()) return;
            const KnownPlace* bank = state_.memory.BestPlace("bank");
            const bool safeHere = client.BankContainer() != 0 ||
                                  windDownArrived_ ||
                                  (bank && TileDist(bank->x, bank->y, client.PlayerX(),
                                                    client.PlayerY()) <= 6);
            const bool outOfTime = nowMs - windDownStartedMs_ > 2 * 60 * 1000;

            if (travelInFlight_) {
                travelInFlight_ = false;
                if (client.TravelSucceeded()) {
                    // Arriving IS the safe state. Requiring an open bank box
                    // here made the first live run re-plan the same
                    // already-arrived trip every tick for four minutes.
                    windDownArrived_ = true;
                    state_.memory.NotePlace("safe", "logout point", client.PlayerX(),
                                            client.PlayerY(), client.PlayerZ(), nowMs);
                    LogLine("wind-down: arrived somewhere safe at %d,%d",
                            client.PlayerX(), client.PlayerY());
                } else {
                    LogLine("wind-down: the trip did not arrive (%s)",
                            client.TravelFailureText());
                }
                return;
            }

            // Bounded, and it has to be: the alternative to "log out here" is
            // never logging out, and a character that never ends its session
            // never proves anything about resuming one.
            if (!safeHere && !outOfTime && windDownTrips_ < 2) {
                windDownTrips_++;
                if (bank) {
                    LogLine("wind-down: travelling to a known bank at %d,%d before "
                            "logout (attempt %d)", bank->x, bank->y, windDownTrips_);
                    travelInFlight_ =
                        client.TravelToPoint(bank->x, bank->y, 3, "logout_safe");
                } else {
                    LogLine("wind-down: no bank learned yet; asking the world for one "
                            "(attempt %d)", windDownTrips_);
                    travelInFlight_ = client.TravelToService(wm::Service::Banker);
                }
                if (!travelInFlight_) {
                    LogLine("wind-down: could not start the trip (%s); logging out here",
                            client.TravelFailureText());
                }
                return;
            }
            if (!safeHere) {
                LogLine("wind-down: giving up on reaching a safe spot after %d "
                        "attempt(s); logging out at %d,%d and recording it",
                        windDownTrips_, client.PlayerX(), client.PlayerY());
                state_.memory.NoteEvent("logout_unsafe",
                                        "could not reach a known-safe spot", "",
                                        client.PlayerX(), client.PlayerY(), nowMs);
            }

            session_.endedMs = nowMs;
            session_.goldEnd = client.PlayerGold();
            {
                i32 sum = 0;
                for (const SkillTarget& t : state_.plan.skills) {
                    const i32 b = client.PlayerSkillBase(static_cast<u16>(t.skillId));
                    if (b >= 0) sum += b;
                }
                session_.skillTenthsEnd = sum;
            }
            session_.logsGathered =
                logsAtSessionStart_ >= 0
                    ? std::max(0, static_cast<i32>(client.BackpackItemCount(kLog)) -
                                      logsAtSessionStart_)
                    : 0;
            session_.placesLearned = static_cast<i32>(state_.memory.Places().size());
            session_.suppliersLearned = static_cast<i32>(state_.memory.Suppliers().size());
            session_.cleanLogout = true;

            state_.identity.totalPlayTimeMs += (nowMs - sessionStartMs_);
            state_.sessions.push_back(session_);
            if (state_.sessions.size() > kMaxSessions) {
                state_.sessions.erase(state_.sessions.begin());
            }

            LogLine("session_summary duration=%llds goals=%d/%d gold=%d->%d "
                    "skills=%.1f->%.1f logs=+%d deaths=%d places=%d suppliers=%d",
                    static_cast<long long>((nowMs - sessionStartMs_) / 1000),
                    session_.goalsCompleted, session_.goalsAttempted,
                    session_.goldStart, session_.goldEnd,
                    session_.skillTenthsStart / 10.0, session_.skillTenthsEnd / 10.0,
                    session_.logsGathered, session_.deaths,
                    session_.placesLearned, session_.suppliersLearned);

            Checkpoint(client, nowMs, "clean logout");
            LogLine("logging out");
            client.ActionLogout();
            phase_ = Phase::LoggingOut;
            return;
        }

        case Phase::LoggingOut:
            if (!client.IsInWorld()) { phase_ = Phase::Done; finished_ = true; }
            return;

        case Phase::Done:
            finished_ = true;
            return;
    }
}

void Runner::LogGoalChange(const Observation& obs, const std::string& why) {
    const std::vector<Need> needs =
        AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
    const std::vector<ScoredGoal> scored = planner_.Score(needs, obs, state_.memory);
    for (const ScoredGoal& g : scored) {
        if (g.kind != planner_.Current().kind) continue;
        LogLine("  score=%.1f", g.score);
        for (const std::string& r : g.reasons) LogLine("  reason: %s", r.c_str());
        break;
    }
    // Blocked goals are reported at every change, so "why didn't it do X"
    // never needs a packet trace to answer.
    for (const ScoredGoal& g : scored) {
        if (g.feasible) continue;
        LogLine("  BLOCKED_NEED %s: %s", GoalKindName(g.kind), g.blockedWhy.c_str());
    }
    (void)why;
}

// ---------------------------------------------------------------------------
// Goal execution
// ---------------------------------------------------------------------------

void Runner::RunGoal(Client& client, const Observation& obs) {
    if (obs.nowMs < nextActionMs_) return;

    // Bounded failure, checked before the body runs so a wedged goal cannot
    // keep acting after it has been abandoned.
    std::string exhaustedWhy;
    if (planner_.Exhausted(obs.nowMs, &exhaustedWhy)) {
        LogLine("goal_failed=%s reason=\"%s\"", GoalKindName(planner_.Current().kind),
                exhaustedWhy.c_str());
        session_.goalsFailed++;
        planner_.Finish(false, exhaustedWhy.c_str(), obs.nowMs);
        return;
    }

    bool done = false;
    switch (planner_.Current().kind) {
        case GoalKind::Survive:               done = DoSurvive(client, obs); break;
        case GoalKind::Heal:                  done = DoHeal(client, obs); break;
        case GoalKind::RecoverCorpse:         done = DoRecoverCorpse(client, obs); break;
        case GoalKind::GetTool:               done = DoGetTool(client, obs); break;
        case GoalKind::ReplaceEquipment:      done = DoReplaceEquipment(client, obs); break;
        case GoalKind::Bank:                  done = DoBank(client, obs); break;
        case GoalKind::GatherLogs:            done = DoGatherLogs(client, obs); break;
        case GoalKind::TrainCombat:           done = DoTrainCombat(client, obs); break;
        case GoalKind::EarnGold:              done = DoEarnGold(client, obs); break;
        case GoalKind::TravelToRequiredPlace: done = DoTravel(client, obs); break;
        case GoalKind::IdleBriefly:           done = DoIdle(client, obs); break;
        case GoalKind::Count:                 break;
    }

    if (done) {
        LogLine("goal_completed=%s progress=%d",
                GoalKindName(planner_.Current().kind), planner_.Current().progress);
        session_.goalsCompleted++;
        planner_.Finish(true, nullptr, obs.nowMs);
        Checkpoint(client, obs.nowMs, "goal completed");
    }
}

// --- survival --------------------------------------------------------------
//
// SurvivalTick already owns potion / bandage / disengage, proven live in
// M3.9.1. This goal adds the two things a tick-level policy cannot decide:
// whether to fight back at all, and where to go when the answer is no.

bool Runner::DoSurvive(Client& client, const Observation& obs) {
    if (obs.dead) {
        client.ActionResurrectAccept();
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    if (hostiles.empty()) {
        currentFoe_ = 0;
        client.EnsurePeaceMode();
        return true;   // the danger passed
    }

    // Remember where this went badly, whatever we decide next. Heat compounds
    // at a spot that keeps producing fights.
    state_.memory.NoteDanger(obs.x, obs.y, 14, hostiles.front().name.c_str(), 0.5,
                             obs.nowMs);

    double bailAt = needCfg_.fleeHpFraction;
    const i32 extra = obs.attackersOnMe - 1;
    if (extra > 0) bailAt = std::min(0.90, bailAt + 0.08 * std::min(3, extra));

    if (obs.HpFraction() < bailAt) {
        LogLine("interrupt=FLEE reason=\"HP %.0f%%; %d attacker(s); bail at %.0f%%\"",
                obs.HpFraction() * 100.0, obs.attackersOnMe, bailAt * 100.0);
        client.EnsurePeaceMode();
        state_.memory.NoteDanger(obs.x, obs.y, 18, hostiles.front().name.c_str(), 1.5,
                                 obs.nowMs);
        if (!state_.memory.HasEvent("first_near_death")) {
            state_.memory.NoteEvent("first_near_death", hostiles.front().name.c_str(),
                                    "", obs.x, obs.y, obs.nowMs);
        }
        // Retreat toward somewhere known-safe rather than a random direction.
        const KnownPlace* bank = state_.memory.BestPlace("bank");
        if (bank && !client.TravelBusy()) {
            client.TravelToPoint(bank->x, bank->y, 3, "flee_to_bank");
        }
        nextActionMs_ = obs.nowMs + 2000;
        planner_.NoteAttempt(obs.nowMs);
        return false;
    }

    // Fight back at whatever is actually on us. Never pick a NEW fight here:
    // this goal exists because something already started one.
    const Client::HostileHit* target = nullptr;
    for (const Client::HostileHit& h : hostiles) {
        if (IsUnreachable(h.serial, obs.nowMs)) continue;
        if (!target || TileDist(h.x, h.y, obs.x, obs.y) <
                           TileDist(target->x, target->y, obs.x, obs.y)) {
            target = &h;
        }
    }
    if (!target) {
        client.EnsurePeaceMode();
        return true;
    }

    const i32 dist = TileDist(target->x, target->y, obs.x, obs.y);

    if (currentFoe_ != target->serial) {
        currentFoe_ = target->serial;
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
        LogLine("engaging %s (noto %d) at %d,%d",
                target->name.empty() ? "a hostile" : target->name.c_str(),
                target->noto, target->x, target->y);
    }

    // BOUNDED CHASE. A wounded animal runs, and a lumberjack that follows it
    // across the countryside has stopped being a lumberjack -- the first live
    // run spent four of its five minutes chasing one fleeing mobile and never
    // returned to the trees. Progress means getting CLOSER; when there is none
    // for a while, the foe is written off and work resumes.
    if (dist <= 1) {
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
    } else if (dist < chaseBestDist_) {
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
    } else if (obs.nowMs - chaseProgressMs_ > kChaseGiveUpMs) {
        LogLine("interrupt=DISENGAGE reason=\"cannot close on %s in %llds "
                "(best %d tiles); it is not worth the chase\"",
                target->name.empty() ? "it" : target->name.c_str(),
                static_cast<long long>(kChaseGiveUpMs / 1000), chaseBestDist_);
        MarkUnreachable(target->serial, obs.nowMs);
        currentFoe_ = 0;
        client.EnsurePeaceMode();
        return true;
    }

    if (!client.WarModeOn()) client.EnterWarMode();
    client.ActionAttack(target->serial);
    if (dist > 1 && !client.GotoBusy()) client.ActionGotoMobile(target->serial, 1);
    nextActionMs_ = obs.nowMs + 1200;
    return false;
}

bool Runner::DoHeal(Client& client, const Observation& obs) {
    if (obs.HpFraction() >= 0.95) return true;
    if (obs.bandages <= 0) {
        LogLine("goal_failed=HEAL reason=\"no bandages carried\"");
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    // SurvivalTick owns the actual bandage timing (it knows the ~3s skill
    // delay and will not restart a running heal, which is the bug that made
    // uo-offline's first bandage loop heal nothing at all). Here we only make
    // sure nothing else is competing for the body.
    if (client.WarModeOn() && obs.hostilesNear == 0) client.EnsurePeaceMode();
    nextActionMs_ = obs.nowMs + 2000;
    planner_.NoteProgress();
    return false;
}

// --- corpse ----------------------------------------------------------------

bool Runner::DoRecoverCorpse(Client& client, const Observation& obs) {
    if (obs.dead) {
        client.ActionResurrectAccept();
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    if (!obs.corpseKnown) return true;   // nothing to recover

    const travel::DeathRecord& death = client.Knowledge().LastDeath();
    if (TileDist(death.x, death.y, obs.x, obs.y) > 2) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            LogLine("corpse run: heading to %d,%d (attempt %d)", death.x, death.y,
                    death.recoveryAttempts + 1);
            travelInFlight_ = client.TravelToLastCorpse();
            if (!travelInFlight_) {
                LogLine("corpse run: no route (%s)", client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 5000;
            }
            return false;
        }
        travelInFlight_ = false;
        if (!client.TravelSucceeded()) {
            client.Knowledge().NoteCorpseRecoveryAttempt();
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    // Standing on it. Open, then take everything the container reports.
    if (!client.ContainerKnown(death.corpseSerial)) {
        if (client.ActionBusy()) return false;
        client.ActionOpenContainer(death.corpseSerial);
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    const usize count = client.ContainerItemCount(death.corpseSerial);
    if (count == 0) {
        LogLine("corpse recovered or empty at %d,%d", death.x, death.y);
        state_.memory.NoteEvent("corpse_recovered", "", "", death.x, death.y,
                                obs.nowMs);
        client.Knowledge().ClearDeath();
        return true;
    }
    if (client.ActionBusy()) return false;
    u32 serial = 0;
    u16 graphic = 0, amount = 0;
    if (client.ContainerItemAt(death.corpseSerial, 0, &serial, &graphic, &amount)) {
        client.TakeFromContainer(serial, amount ? amount : 1);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 900;
    }
    return false;
}

// --- tools and equipment ---------------------------------------------------

bool Runner::DoGetTool(Client& client, const Observation& obs) {
    if (obs.axeInPack || obs.axeEquipped) return true;

    const KnownSupplier* known = state_.memory.BestSupplier("hatchet");

    // A tool purchase is legal under the vendor policy -- a tool is not a
    // resource, and buying one shortcuts no production chain. Verify that
    // here rather than assuming it, because the policy is the thing that
    // keeps the shard's player economy alive.
    const econ::VendorRuling ruling = econ::CanUseNPCVendorForGraphic(kHatchet[0]);
    if (!ruling.allowed) {
        LogLine("BLOCKED_NEED hatchet: vendor policy refuses (%s / %s)",
                econ::VendorClassName(ruling.klass),
                ruling.reason ? ruling.reason : "");
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 30000;
        return false;
    }

    if (client.TravelBusy()) return false;

    const u32 vendor = client.VendorOfferFrom();
    if (vendor == 0) {
        if (!travelInFlight_) {
            if (known) {
                LogLine("get_tool: returning to a remembered supplier '%s' at %d,%d",
                        known->name.c_str(), known->x, known->y);
                travelInFlight_ = client.TravelToPoint(known->x, known->y, 2, "supplier");
            } else {
                LogLine("get_tool: no remembered supplier; looking for a blacksmith");
                travelInFlight_ = client.TravelToService(wm::Service::Blacksmith);
            }
            if (!travelInFlight_) {
                LogLine("BLOCKED_NEED hatchet: %s", client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 15000;
            }
            return false;
        }
        travelInFlight_ = false;
        // Arrived (or gave up). Ask whoever is here to show their wares.
        const u32 keeper = client.NearestMobileWithTrade("blacksmith");
        if (!keeper) {
            LogLine("get_tool: arrived but no blacksmith is here");
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        client.ActionVendorOpen(keeper);
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // A shop window is open. A supplier exists only once we have READ its
    // stock and seen the item -- never because a place was tagged with a
    // profession (supplier.h, and three journeys that ended at a guildmaster).
    for (const Client::VendorItem& v : client.VendorOffer()) {
        if (v.graphic != kHatchet[0] && v.graphic != kHatchet[1] &&
            v.graphic != kAxe[0] && v.graphic != kAxe[1]) {
            continue;
        }
        KnownSupplier s;
        s.need = "hatchet";
        s.name = v.name;
        s.sourceType = "npc_vendor";
        s.serial = vendor;
        s.x = obs.x; s.y = obs.y; s.z = obs.z;
        s.observedQuantity = v.amount;
        s.observedPricePerUnit = static_cast<i32>(v.price);
        s.lastVerifiedMs = obs.nowMs;
        s.policyAllows = true;
        state_.memory.NoteSupplier(s);
        if (!state_.memory.HasEvent("supplier_learned")) {
            state_.memory.NoteEvent("supplier_learned", v.name.c_str(), "", obs.x,
                                    obs.y, obs.nowMs);
        }
        LogLine("memory_learned=SUPPLIER need=hatchet name=\"%s\" price=%u qty=%u",
                v.name.c_str(), v.price, v.amount);

        if (obs.gold < static_cast<i32>(v.price)) {
            LogLine("BLOCKED_NEED hatchet: costs %u, carrying %d", v.price, obs.gold);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 10000;
            return false;
        }
        client.ActionVendorBuy(vendor, v.serial, 1);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    LogLine("BLOCKED_NEED hatchet: this vendor's %zu-item list contains no axe",
            client.VendorOffer().size());
    planner_.NoteAttempt(obs.nowMs);
    nextActionMs_ = obs.nowMs + 8000;
    return false;
}

bool Runner::DoReplaceEquipment(Client& client, const Observation& obs) {
    // The cheapest fix first: something usable is already in the pack. The axe
    // is preferred -- it is this build's weapon AND its tool, so arming it
    // solves both needs at once.
    if (!obs.weaponEquipped) {
        if (ArmAxe(client, obs)) { planner_.NoteProgress(); return false; }
        const u32 sword = FindAny(client, kKatana, 2);
        if (!AxeInHand(client) && sword) {
            if (client.ActionBusy()) return false;
            LogLine("arming: no axe carried, equipping the sword instead");
            client.ActionEquip(sword, kLayerServerChooses);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
    }
    if (obs.weaponEquipped && obs.bandages >= needCfg_.bandageLow) return true;

    if (obs.bandages < needCfg_.bandageLow) {
        const econ::VendorRuling ruling = econ::CanUseNPCVendorForGraphic(kBandage);
        if (!ruling.allowed) {
            LogLine("BLOCKED_NEED bandages: vendor policy refuses (%s)",
                    econ::VendorClassName(ruling.klass));
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 30000;
            return false;
        }
        if (client.TravelBusy()) return false;
        const u32 vendor = client.VendorOfferFrom();
        if (vendor == 0) {
            if (!travelInFlight_) {
                travelInFlight_ = client.TravelToService(wm::Service::Healer);
                if (!travelInFlight_) {
                    LogLine("BLOCKED_NEED bandages: %s", client.TravelFailureText());
                    planner_.NoteAttempt(obs.nowMs);
                    nextActionMs_ = obs.nowMs + 15000;
                }
                return false;
            }
            travelInFlight_ = false;
            const u32 keeper = client.NearestMobileWithTrade("healer");
            if (!keeper) {
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 5000;
                return false;
            }
            client.ActionVendorOpen(keeper);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        for (const Client::VendorItem& v : client.VendorOffer()) {
            if (v.graphic != kBandage) continue;
            const i32 want = std::min<i32>(needCfg_.bandageFull - obs.bandages, 20);
            if (want <= 0) return true;
            if (obs.gold < static_cast<i32>(v.price) * want) {
                LogLine("BLOCKED_NEED bandages: %d cost %u each, carrying %d gold",
                        want, v.price, obs.gold);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 10000;
                return false;
            }
            KnownSupplier s;
            s.need = "bandage";
            s.name = v.name;
            s.sourceType = "npc_vendor";
            s.serial = vendor;
            s.x = obs.x; s.y = obs.y; s.z = obs.z;
            s.observedQuantity = v.amount;
            s.observedPricePerUnit = static_cast<i32>(v.price);
            s.lastVerifiedMs = obs.nowMs;
            s.policyAllows = true;
            state_.memory.NoteSupplier(s);
            LogLine("memory_learned=SUPPLIER need=bandage name=\"%s\"", v.name.c_str());
            client.ActionVendorBuy(vendor, v.serial, static_cast<u16>(want));
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        LogLine("BLOCKED_NEED bandages: this vendor's list has none");
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 8000;
        return false;
    }
    return true;
}

// --- banking ---------------------------------------------------------------

bool Runner::DoBank(Client& client, const Observation& obs) {
    const u32 box = client.BankContainer();
    if (box && client.ContainerKnown(box)) {
        if (client.ActionBusy()) return false;
        const u32 logs = client.FindBackpackItemByGraphic(kLog);
        if (logs) {
            const u16 amount = static_cast<u16>(client.BackpackItemCount(kLog));
            LogLine("banking %u logs", amount);
            client.ActionMoveItem(logs, amount, box);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        state_.memory.NotePlace("bank", "bank", obs.x, obs.y, obs.z, obs.nowMs);
        if (!state_.memory.HasEvent("first_bank_deposit") && planner_.Current().progress > 0) {
            state_.memory.NoteEvent("first_bank_deposit", "logs", "bank", obs.x,
                                    obs.y, obs.nowMs);
        }
        return true;
    }

    if (client.TravelBusy()) return false;
    const u32 banker = client.NearestMobileWithTrade("banker");
    if (banker) {
        client.ActionOpenBank(banker);
        nextActionMs_ = obs.nowMs + 2500;
        planner_.NoteProgress();
        return false;
    }
    if (!travelInFlight_) {
        const KnownPlace* known = state_.memory.BestPlace("bank");
        if (known) {
            LogLine("bank: returning to a remembered bank at %d,%d", known->x, known->y);
            travelInFlight_ = client.TravelToPoint(known->x, known->y, 2, "bank");
        } else {
            LogLine("bank: no bank learned yet; asking the world model for one");
            travelInFlight_ = client.TravelToService(wm::Service::Banker);
        }
        if (!travelInFlight_) {
            LogLine("goal_blocked=BANK reason=\"%s\"", client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 10000;
        }
        return false;
    }
    travelInFlight_ = false;
    if (!client.TravelSucceeded()) {
        LogLine("bank: the trip did not arrive (%s)", client.TravelFailureText());
        planner_.NoteAttempt(obs.nowMs);
    }
    return false;
}

// --- the work --------------------------------------------------------------

bool Runner::DoGatherLogs(Client& client, const Observation& obs) {
    if (!obs.axeInPack && !obs.axeEquipped) {
        LogLine("goal_failed=GATHER_LOGS reason=\"no axe\"");
        planner_.Finish(false, "no axe", obs.nowMs);
        return false;
    }
    if (obs.WeightFraction() >= 0.95) {
        LogLine("gather: pack full at %.0f%%", obs.WeightFraction() * 100.0);
        return true;
    }

    // Arm the axe. `skill44_lumberjacking.scp` requires SRC.WEAPON, so the axe
    // has to be IN HAND, not merely carried -- and it must be the AXE, not
    // whatever the newbie kit armed. This is also the weapon the character
    // fights with, which is the whole point of the era Lumberjack template.
    if (!obs.axeEquipped) {
        // Arming is progress, not a failed attempt -- counting it against the
        // attempt budget would exhaust the goal before the first swing.
        if (ArmAxe(client, obs)) return false;
        if (!AxeInHand(client)) {
            LogLine("goal_failed=GATHER_LOGS reason=\"no axe to arm\"");
            planner_.Finish(false, "no axe to arm", obs.nowMs);
            return false;
        }
    }

    // --- am I actually where the work is? ---------------------------------
    if (!obs.atWorkSite) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            const KnownResourceSource* stand =
                state_.memory.BestResource("logs", obs.x, obs.y, obs.nowMs);
            if (stand) {
                LogLine("gather: heading to a remembered stand at %d,%d "
                        "(successes %d, failures %d)",
                        stand->x, stand->y, stand->successes, stand->failures);
                travelInFlight_ = client.TravelToPoint(stand->x, stand->y, 4, "forest");
            } else {
                LogLine("gather: no stand remembered; asking the world for lumber");
                travelInFlight_ = client.TravelToResource(wm::ResourceKind::Lumber);
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=GATHER_LOGS reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 10000;
            }
            return false;
        }
        travelInFlight_ = false;
        // ARRIVAL IS A CLAIM ABOUT THE TILE. A journey that reports success
        // and leaves us six tiles short of the trees is a failure here, and
        // saying so is what keeps it out of the "worked fine" column.
        if (client.TreeCount(client.PlayerX(), client.PlayerY(), cfg_.searchRadius) == 0) {
            LogLine("gather: trip reported %s but there are no trees within %d tiles",
                    client.TravelSucceeded() ? "success" : "failure", cfg_.searchRadius);
            state_.memory.NoteResource("logs", client.PlayerX(), client.PlayerY(),
                                       client.PlayerZ(), false, obs.nowMs);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 3000;
        }
        return false;
    }

    // --- pick a tree and stand next to it ---------------------------------
    if (!chopTargetValid_) {
        Client::TreeHit tree;
        // MarkStump already hides a worked-out tree from the statics, but the
        // overlay has a TTL and this list does not: within one goal, a tree we
        // proved barren must not come back as "the nearest one".
        bool found = false;
        for (int radius = 4; radius <= cfg_.searchRadius && !found; radius *= 2) {
            if (!client.NearestTree(obs.x, obs.y, radius, &tree)) continue;
            bool skip = false;
            for (const auto& v : visitedTrees_) {
                if (v.first == tree.x && v.second == tree.y) { skip = true; break; }
            }
            if (!skip) found = true;
        }
        if (!found && client.NearestTree(obs.x, obs.y, cfg_.searchRadius, &tree)) {
            bool skip = false;
            for (const auto& v : visitedTrees_) {
                if (v.first == tree.x && v.second == tree.y) { skip = true; break; }
            }
            found = !skip;
        }
        if (!found) {
            LogLine("gather: every tree within %d tiles is worked out -> "
                    "this stand is done", cfg_.searchRadius);
            state_.memory.NoteResource("logs", obs.x, obs.y, obs.z, false, obs.nowMs);
            visitedTrees_.clear();
            return true;   // the goal completed what this stand could give
        }
        chopX_ = tree.x; chopY_ = tree.y; chopZ_ = tree.z;
        chopGraphic_ = tree.graphic;
        chopTargetValid_ = true;
        chopCursorPending_ = false;
        swingsOnTree_ = 0;
    }

    // Adjacency is read from the LIVE position, not from the tick's snapshot:
    // the walk finishes between ticks, and swinging from the stale reading is
    // what earned "You can't reach this." eight times per tree in the first
    // live run. A tree is surrounded by eight cells and some of them are other
    // trees, so try them nearest-first rather than always stepping east.
    // RANGE=2 in skill44_lumberjacking.scp -- the shard's own number. Standing
    // adjacent is not required, and demanding it wastes walks.
    if (TileDist(chopX_, chopY_, client.PlayerX(), client.PlayerY()) > 2) {
        if (client.GotoBusy()) return false;
        if (approachCell_ >= 8) {
            LogLine("cannot stand next to the tree at %d,%d -> moving on",
                    chopX_, chopY_);
            visitedTrees_.emplace_back(chopX_, chopY_);
            chopTargetValid_ = false;
            approachCell_ = 0;
            planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        static const int kdx[8] = {1, -1, 0,  0, 1,  1, -1, -1};
        static const int kdy[8] = {0,  0, 1, -1, 1, -1,  1, -1};
        // Order the eight cells by how close they are to where we stand now,
        // then walk to the next untried one.
        int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        const i32 px = client.PlayerX(), py = client.PlayerY();
        std::stable_sort(order, order + 8, [&](int a, int b) {
            return TileDist(chopX_ + kdx[a], chopY_ + kdy[a], px, py) <
                   TileDist(chopX_ + kdx[b], chopY_ + kdy[b], px, py);
        });
        const int pick = order[approachCell_++];
        client.ActionGoto(chopX_ + kdx[pick], chopY_ + kdy[pick]);
        nextActionMs_ = obs.nowMs + 1200;
        return false;
    }
    approachCell_ = 0;

    // --- swing --------------------------------------------------------------
    if (client.ActionBusy()) return false;

    if (chopCursorPending_) {
        if (client.TargetActive()) {
            client.ActionTargetStatic(chopX_, chopY_, chopZ_, chopGraphic_);
            chopCursorPending_ = false;
            lastChopMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        if (obs.nowMs - lastChopMs_ > 6000) {
            chopCursorPending_ = false;
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    // Did the last swing produce anything? The honest signal a client has is
    // the pack: logs went up, or they did not.
    //
    // BOUNDED PER TREE. The first live run swung at one tree for two minutes
    // straight because the "nothing came out" branch was gated on a timer that
    // the next swing kept resetting. Count swings instead of watching a clock:
    // a counter cannot be reset by the thing it is counting.
    if (obs.logs > logsSeen_) {
        logsSeen_ = obs.logs;
        swingsOnTree_ = 0;
        planner_.NoteProgress();
        state_.memory.NoteResource("logs", chopX_, chopY_, chopZ_, true, obs.nowMs);
        if (!state_.memory.HasEvent("first_logs")) {
            state_.memory.NoteEvent("first_logs", "first logs gathered", "forest",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("first logs gathered at %d,%d (pack now holds %d)", obs.x, obs.y,
                    obs.logs);
        }
    } else if (swingsOnTree_ >= kMaxSwingsPerTree) {
        // A barren tree is NOT a failure. `regionresources.scp` gives
        // `r_default_tree` 60.0 mr_nothing against 40.0 mr_tree, so three trees
        // in five hold no wood at all and Source-X says so on the first swing
        // ("There is nothing here to chop", CCharSkill.cpp:1682). Counting
        // that against the goal's attempt budget ended GATHER_LOGS every few
        // trees in the live run -- the character was working correctly and
        // being told it had failed.
        LogLine("tree at %d,%d holds no wood -> next tree (%d tried this stand)",
                chopX_, chopY_, static_cast<int>(visitedTrees_.size()) + 1);
        client.MarkStump(chopX_, chopY_, chopZ_, chopGraphic_);
        state_.memory.NoteResource("logs", chopX_, chopY_, chopZ_, false, obs.nowMs);
        visitedTrees_.emplace_back(chopX_, chopY_);
        if (visitedTrees_.size() > 64) visitedTrees_.erase(visitedTrees_.begin());
        chopTargetValid_ = false;
        swingsOnTree_ = 0;
        return false;
    }

    const u32 axe = AxeSerialInHand(client);
    if (!axe) {
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    client.ActionUseObject(axe);
    chopCursorPending_ = true;
    swingsOnTree_++;
    lastChopMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 800;
    return false;
}

bool Runner::DoTrainCombat(Client& client, const Observation& obs) {
    // Deliberately narrow for Slice 1: the Lumberjack does not go hunting as
    // its main activity. This trains by finishing fights that find it, which
    // is what the era template actually describes.
    if (obs.hostilesNear == 0) return true;
    return DoSurvive(client, obs);
}

bool Runner::DoEarnGold(Client& client, const Observation& obs) {
    if (obs.logs <= 0) return true;

    // Phase 9: do NOT sell to a stock Sphere NPC merely because SELL exists.
    // If no authentic buyer is observed, the logs go to the bank and the
    // character stays resource-rich and wealth-poor -- which is a valid state,
    // not a failure to work around.
    const u32 vendor = client.VendorOfferFrom();
    if (vendor == 0) {
        LogLine("earn_gold: no observed buyer for logs; banking them instead");
        state_.memory.NoteEvent("no_log_buyer",
                                "no authentic NPC buyer observed for logs", "",
                                obs.x, obs.y, obs.nowMs);
        return true;   // the planner will pick BANK next
    }
    for (const Client::VendorItem& v : client.VendorSellOffer()) {
        if (v.graphic != kLog) continue;
        LogLine("earn_gold: selling logs at %u each to an observed buyer", v.price);
        client.ActionVendorSell(vendor, v.serial,
                                static_cast<u16>(client.BackpackItemCount(kLog)));
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }
    LogLine("earn_gold: this buyer's list does not take logs; banking instead");
    return true;
}

bool Runner::DoTravel(Client& client, const Observation& obs) {
    if (obs.atWorkSite || obs.atBank) return true;
    if (client.TravelBusy()) return false;
    if (travelAttempts_ >= 3) {
        LogLine("goal_failed=TRAVEL_TO_REQUIRED_PLACE reason=\"three trips did not arrive\"");
        planner_.Finish(false, "three trips did not arrive", obs.nowMs);
        return false;
    }
    if (!travelInFlight_) {
        travelAttempts_++;
        const KnownResourceSource* stand =
            state_.memory.BestResource("logs", obs.x, obs.y, obs.nowMs);
        if (stand) {
            travelInFlight_ = client.TravelToPoint(stand->x, stand->y, 4, "forest");
        } else {
            travelInFlight_ = client.TravelToResource(wm::ResourceKind::Lumber);
        }
        if (!travelInFlight_) {
            LogLine("travel: could not start (%s)", client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 8000;
        }
        return false;
    }
    travelInFlight_ = false;
    if (client.TravelSucceeded()) {
        planner_.NoteProgress();
        return true;
    }
    LogLine("travel: did not arrive (%s)", client.TravelFailureText());
    planner_.NoteAttempt(obs.nowMs);
    return false;
}

bool Runner::DoIdle(Client& client, const Observation& obs) {
    (void)client;
    // A bounded no-op. It exists so a tick with nothing to do SAYS so rather
    // than spinning, and so the planner is never in a "no goal" state.
    nextActionMs_ = obs.nowMs + 5000;
    if (obs.nowMs - planner_.Current().startedAtMs > 15000) return true;
    return false;
}

}  // namespace uo::life
