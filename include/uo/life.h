#pragma once

// ---------------------------------------------------------------------------
// M4 Slice 1 -- a character that LIVES: identity, persistence, needs, goals.
//
// THE THREE SOURCES OF TRUTH, kept apart on purpose (M4 brief, Phase 2):
//
//   A. SERVER CHARACTER TRUTH   skills, stats, gold, pack, position, alive.
//                               The server wins, always. We only ever cache
//                               it for the reconciliation DIFF, never as an
//                               authority.
//   B. BOT LONG-TERM INTENTION  identity, target build, learned places,
//                               learned suppliers, remembered dangers, the
//                               current objective. This file owns these, and
//                               they are what survives a logout.
//   C. EPHEMERAL OBSERVATION    what is on screen this second. Modelled here
//                               as `Observation`, passed in per tick and
//                               NEVER persisted.
//
// Nothing in this header touches Client, sockets or packets. That is what
// makes the need model, the utility planner, the schema and the reconciler
// unit-testable against the exact code the live bot runs -- the same split
// that `uo/actions.h` uses for the M2 action layer.
// ---------------------------------------------------------------------------

#include "uo/rules.h"
#include "uo/json.h"
#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::life {

// Bumped whenever the on-disk shape changes. Readers gate every field behind
// the version and default what is absent, so an older state.json loads under
// a newer build instead of being wiped.
inline constexpr int kSchemaVersion = 1;

// ===========================================================================
// Build plan -- the character's long-term intention about what it will be
// ===========================================================================

struct SkillTarget {
    int skillId = 0;
    i32 tenths  = 0;    // 1000 == 100.0
};

// A 700-point Revolution build. `unresolvedTenths` is a FIRST-CLASS field, not
// a rounding error: the M4 plan names five skills for this character and
// deliberately does not spend the rest, and the brief says to preserve
// unresolved status rather than invent a canonical build to fill it.
struct BuildPlan {
    std::string family;
    std::vector<SkillTarget> skills;
    i32 unresolvedTenths = 0;

    i32 targetStr = 0;
    i32 targetDex = 0;
    i32 targetInt = 0;

    // Character CREATION is initialisation, not a request for final stats.
    // Source-X clamps these itself (CChar::InitPlayer: 60 per stat / 80 total,
    // 50 per skill / 100 total), so what is stored here is what we ask for.
    i32 createStr = 0;
    i32 createDex = 0;
    i32 createInt = 0;
    std::vector<SkillTarget> createSkills;
};

enum class PlanViolation : u8 {
    None = 0,
    SkillBudgetExceeded,   // resolved + unresolved > 700.0
    PerSkillCap,           // a single skill above 100.0
    InactiveSkill,         // a skill Revolution did not run
    NegativeTarget,
    StatTotalExceeded,     // target stats above 225
    StatPerCapExceeded,    // one target stat above 100
    CreationTooRich,       // a creation request the server would clamp anyway
    RejectedCreationSplit, // the 55/15/10 split that killed six characters
    Count,
};

const char* PlanViolationName(PlanViolation v);

struct PlanCheck {
    bool          ok = false;
    PlanViolation violation = PlanViolation::None;
    int           skillId = 0;
    i32           resolvedTenths = 0;   // sum of named targets
    i32           plannedTotalTenths = 0;   // resolved + unresolved
    i32           statTotal = 0;
};

PlanCheck ValidatePlan(const rules::Profile& p, const BuildPlan& plan);

// The M4 Slice 1 character, exactly as docs/M4_LIFECYCLE_PLAN.md specifies it.
BuildPlan FrontierLumberjackSwordsman();

// ===========================================================================
// Memory -- what this character has learned. Private to one identity.
// ===========================================================================

struct KnownPlace {
    std::string kind;          // "bank", "healer", "forest", "town"
    std::string name;          // semantic name, as the world model gave it
    i32 x = 0, y = 0;
    i8  z = 0;
    i64 learnedMs = 0;
    i64 lastVerifiedMs = 0;
    i32 visits = 0;
};

struct KnownResourceSource {
    std::string resource;      // "logs"
    i32 x = 0, y = 0;
    i8  z = 0;
    i64 lastSuccessMs = 0;
    i64 lastSeenMs = 0;
    i32 successes = 0;
    i32 failures = 0;
};

// A supplier note is a FLATTENED, PERSISTABLE copy of a supply::Supplier the
// live registry verified. It exists so a learned supplier survives a logout;
// it is re-validated on use, never trusted as current stock (see supplier.h).
struct KnownSupplier {
    std::string need;          // what it answered
    std::string name;
    std::string sourceType;    // "npc_vendor", "player_vendor", "world_resource"
    u32 serial = 0;
    i32 x = 0, y = 0;
    i8  z = 0;
    i32 observedQuantity = 0;
    i32 observedPricePerUnit = 0;
    i64 lastVerifiedMs = 0;
    bool policyAllows = false;
};

// Danger decays exponentially rather than expiring on a cliff (uo-offline's
// BotDangerMap half-life, made per-character). Repeated trouble at one spot
// compounds; a single scare fades.
struct DangerMemory {
    i32 x = 0, y = 0;
    i32 radius = 0;
    std::string threat;
    double heat = 0.0;
    i64 atMs = 0;
};

struct LifeEvent {
    std::string kind;          // "first_logs", "first_death", "supplier_learned", ...
    std::string detail;
    std::string place;
    i32 x = 0, y = 0;
    i64 atMs = 0;
};

// Bounds. Persistent state that can grow without limit is a scaling bug that
// only shows up at 300 characters, so every list is capped here rather than
// in the writer.
inline constexpr usize kMaxPlaces    = 128;
inline constexpr usize kMaxResources = 64;
inline constexpr usize kMaxSuppliers = 64;
inline constexpr usize kMaxDanger    = 64;
inline constexpr usize kMaxEvents    = 256;
inline constexpr usize kMaxSessions  = 32;

// Half-life of a danger memory. 45 minutes, taken from uo-offline's measured
// choice; ours is per-character and is a weight, not a switch.
inline constexpr i64 kDangerHalfLifeMs = 45 * 60 * 1000;

class Memory {
public:
    void NotePlace(const char* kind, const char* name, i32 x, i32 y, i8 z, i64 nowMs);
    void NoteResource(const char* resource, i32 x, i32 y, i8 z, bool success, i64 nowMs);
    // "I stood here and the resource is here." Refreshes lastSeen WITHOUT
    // scoring a success or a failure -- standing next to a tree is not an
    // attempt to chop it, and counting it as one buries a good stand under
    // hundreds of imaginary failures.
    void NoteResourceSeen(const char* resource, i32 x, i32 y, i8 z, i64 nowMs);
    void NoteSupplier(const KnownSupplier& s);
    void NoteDanger(i32 x, i32 y, i32 radius, const char* threat, double heat, i64 nowMs);
    void NoteEvent(const char* kind, const char* detail, const char* place,
                   i32 x, i32 y, i64 nowMs);

    // Decayed heat at a point. 0 when nothing bad ever happened nearby.
    double DangerHeatAt(i32 x, i32 y, i64 nowMs) const;
    // Drop danger notes whose decayed heat has fallen below the floor.
    void   ExpireDanger(i64 nowMs, double floorHeat = 0.05);

    const KnownPlace*          BestPlace(const char* kind) const;
    const KnownResourceSource* BestResource(const char* resource, i32 fromX, i32 fromY,
                                            i64 nowMs) const;
    const KnownSupplier*       BestSupplier(const char* need) const;
    bool HasEvent(const char* kind) const;

    const std::vector<KnownPlace>&          Places()    const { return places_; }
    const std::vector<KnownResourceSource>& Resources() const { return resources_; }
    const std::vector<KnownSupplier>&       Suppliers() const { return suppliers_; }
    const std::vector<DangerMemory>&        Dangers()   const { return danger_; }
    const std::vector<LifeEvent>&           Events()    const { return events_; }

    std::vector<KnownPlace>&          MutablePlaces()    { return places_; }
    std::vector<KnownResourceSource>& MutableResources() { return resources_; }
    std::vector<KnownSupplier>&       MutableSuppliers() { return suppliers_; }
    std::vector<DangerMemory>&        MutableDangers()   { return danger_; }
    std::vector<LifeEvent>&           MutableEvents()    { return events_; }

    void Clear();
    usize ApproximateBytes() const;

private:
    std::vector<KnownPlace>          places_;
    std::vector<KnownResourceSource> resources_;
    std::vector<KnownSupplier>       suppliers_;
    std::vector<DangerMemory>        danger_;
    std::vector<LifeEvent>           events_;
};

// ===========================================================================
// Observation -- source of truth C. Filled fresh every tick, never persisted.
// ===========================================================================

struct Observation {
    i64  nowMs = 0;
    bool inWorld = false;
    bool dead = false;
    bool mounted = false;
    bool warMode = false;

    i32 x = 0, y = 0;
    i8  z = 0;

    i32 hp = 0, hpMax = 0;
    i32 mana = 0;

    i32 str = 0, dex = 0, intel = 0;
    std::vector<SkillTarget> skills;   // as the server reported them

    i32 gold = 0;
    i32 weight = 0, maxWeight = 0;

    // Pack contents that matter to this build. Counted from the real backpack.
    i32  bandages = 0;
    i32  logs = 0;
    i32  food = 0;
    bool axeInPack = false;
    bool axeEquipped = false;
    bool weaponEquipped = false;

    // What is happening around us, as the client can see it.
    i32  hostilesNear = 0;
    i32  attackersOnMe = 0;
    bool underAttack = false;

    // Corpse state, only meaningful after a death we observed.
    bool corpseKnown = false;
    i32  corpseX = 0, corpseY = 0;
    i32  corpseRecoveryAttempts = 0;

    // Whether the character is standing where the work actually is. Travel
    // success is a claim about the journey; this is a claim about the tile
    // (uo-offline's "arriving near is not arriving", audit section 3.6).
    bool atWorkSite = false;
    bool treeAdjacent = false;
    bool atBank = false;

    i32 SkillTenths(int skillId) const;
    i32 SkillSumTenths() const;
    double HpFraction() const { return hpMax > 0 ? static_cast<double>(hp) / hpMax : 1.0; }
    double WeightFraction() const {
        return maxWeight > 0 ? static_cast<double>(weight) / maxWeight : 0.0;
    }
};

// ===========================================================================
// Needs -- small, specific, and always able to say WHAT and WHY
// ===========================================================================

enum class NeedKind : u8 {
    StayAlive = 0,
    Heal,
    RecoverCorpse,
    NeedTool,
    NeedEquipment,
    NeedFood,
    NeedBank,
    NeedGold,
    NeedLogs,
    NeedTraining,
    NeedTravel,
    Count,
};

const char* NeedKindName(NeedKind k);

// "NeedTool=true" is not a need. A need names the thing and the evidence.
struct Need {
    NeedKind    kind = NeedKind::StayAlive;
    double      urgency = 0.0;     // 0..1
    std::string what;              // "hatchet", "logs", "Swordsmanship"
    std::string reason;            // why it is needed
    std::string evidence;          // the observed numbers behind the claim
    bool        blocked = false;   // BLOCKED_NEED: nothing legitimate satisfies it
};

// Config for the thresholds a need model needs. Values that came from a live
// measurement carry that in their comment; the rest are starting points.
struct NeedConfig {
    double fleeHpFraction   = 0.32;  // M3.9.1 live: disengaged at ~32% and survived
    double healHpFraction   = 0.80;
    i32    bandageLow       = 8;     // uo-offline's threshold shape, our numbers
    i32    bandageFull      = 30;
    double bankWeightFrac   = 0.85;
    i32    goldFloor        = 100;   // below this, gold itself becomes a need
    i32    logsWorthBanking = 20;
    i32    foodLow          = 1;
    bool   hungerLive       = true;  // HitsHungerLoss=1 on this shard
};

std::vector<Need> AssessNeeds(const BuildPlan& plan, const Memory& mem,
                              const Observation& obs, const NeedConfig& cfg);

// ===========================================================================
// Goals -- utility selection, with commitment
// ===========================================================================

enum class GoalKind : u8 {
    Survive = 0,
    Heal,
    RecoverCorpse,
    GetTool,
    ReplaceEquipment,
    Bank,
    GatherLogs,
    TrainCombat,
    EarnGold,
    TravelToRequiredPlace,
    IdleBriefly,
    Count,
};

const char* GoalKindName(GoalKind g);

struct ScoredGoal {
    GoalKind kind = GoalKind::IdleBriefly;
    double   score = 0.0;
    bool     feasible = false;
    // Contributing terms, printable. uo-offline's target scorer is legible
    // precisely because its magnitudes are separable; ours prints them.
    std::vector<std::string> reasons;
    std::string blockedWhy;      // set when feasible == false
};

struct GoalState {
    GoalKind    kind = GoalKind::IdleBriefly;
    bool        active = false;
    i64         startedAtMs = 0;
    i32         attempts = 0;
    i32         progress = 0;
    std::string failureReason;
    double      scoreAtSelection = 0.0;
};

struct PlannerConfig {
    // A challenger must beat the incumbent by this fraction to take over.
    // Inverted from uo-offline's anti-stay bias on purpose: its phases are
    // moods, ours are tasks with sunk cost (audit section 3.3).
    double incumbentBonus = 0.15;
    // Below this, a goal is not re-evaluated at all -- the floor against
    // gather/bank/gather flapping.
    i64 minCommitMs = 20 * 1000;
    // Above this, a goal has failed to finish and is abandoned with a reason.
    i64 maxGoalMs = 5 * 60 * 1000;
    // Bounded failure: this many attempts and the goal is given up.
    i32 maxAttempts = 5;
    // Emergencies preempt regardless of commitment.
    double preemptScore = 900.0;
};

class Planner {
public:
    explicit Planner(PlannerConfig cfg = {}) : cfg_(cfg) {}

    // Hard-filter, then additive score. Infeasible goals are returned WITH a
    // reason rather than dropped, so "why didn't it do X" is answerable.
    std::vector<ScoredGoal> Score(const std::vector<Need>& needs,
                                  const Observation& obs,
                                  const Memory& mem) const;

    // Applies commitment and hysteresis. Returns true when the goal changed.
    bool Select(const std::vector<Need>& needs, const Observation& obs,
                const Memory& mem, i64 nowMs, std::string* whyOut);

    const GoalState& Current() const { return goal_; }
    GoalState&       Mutable()       { return goal_; }

    void NoteAttempt(i64 nowMs);
    void NoteProgress();
    void Finish(bool success, const char* why, i64 nowMs);
    // True when the running goal has run out of time or attempts.
    bool Exhausted(i64 nowMs, std::string* whyOut) const;

    const PlannerConfig& Config() const { return cfg_; }

private:
    PlannerConfig cfg_;
    GoalState     goal_;
};

// ===========================================================================
// Persistent state, its store, and login reconciliation
// ===========================================================================

struct Identity {
    std::string identityId;      // filesystem-safe; the directory name
    std::string accountName;
    std::string characterName;
    i64 createdAtMs = 0;
    i64 firstSeenAtMs = 0;
    i64 lastSeenAtMs = 0;
    i64 totalPlayTimeMs = 0;
    i32 sessions = 0;
};

struct SessionSummary {
    i64 startedMs = 0;
    i64 endedMs = 0;
    i32 goalsAttempted = 0;
    i32 goalsCompleted = 0;
    i32 goalsFailed = 0;
    i32 goldStart = 0, goldEnd = 0;
    i32 skillTenthsStart = 0, skillTenthsEnd = 0;
    i32 logsGathered = 0;
    i32 deaths = 0;
    i32 placesLearned = 0;
    i32 suppliersLearned = 0;
    bool cleanLogout = false;
};

// Everything that survives a logout. NOT in here, deliberately: serials,
// path nodes, target cursors, container caches, mobile occupancy, or any
// pointer. The brief lists those; this struct has no field that could hold
// one.
struct PersistentState {
    int       schemaVersion = kSchemaVersion;
    Identity  identity;
    BuildPlan plan;
    Memory    memory;

    // The current objective, so a session RESUMES rather than restarts. It is
    // re-validated against server truth on login and may be dropped.
    GoalState goal;

    // Last figures the SERVER reported. Kept only so the reconciliation log
    // can print a real diff -- never used as an authority (Phase 2A).
    i32 lastKnownGold = 0;
    i32 lastKnownStr = 0, lastKnownDex = 0, lastKnownInt = 0;
    std::vector<SkillTarget> lastKnownSkills;
    i32 lastKnownX = 0, lastKnownY = 0;
    bool lastKnownDead = false;

    i64 checkpointMs = 0;
    i32 deathCount = 0;
    // Consecutive recent deaths, decayed after a quiet hour. A character that
    // keeps dying in one place should stop going there (audit section 3.9).
    i32 recentDeaths = 0;
    i64 lastDeathMs = 0;

    std::vector<SessionSummary> sessions;
};

json::Value ToJson(const PersistentState& st);
bool FromJson(const json::Value& v, PersistentState* out, std::string* err);

// A directory per identity: <root>/<identityId>/state.json. No passwords, no
// credentials, ever -- the account name is stored, the password is not.
class Store {
public:
    explicit Store(std::string root) : root_(std::move(root)) {}

    const std::string& Root() const { return root_; }
    std::string DirFor(const std::string& identityId) const;
    std::string PathFor(const std::string& identityId) const;

    bool Save(const PersistentState& st, std::string* err) const;
    // Returns false when there is nothing to load (a brand-new character) or
    // when the file is unreadable; `err` distinguishes the two.
    bool Load(const std::string& identityId, PersistentState* out,
              std::string* err) const;
    bool Exists(const std::string& identityId) const;

private:
    std::string root_;
};

// --- login reconciliation --------------------------------------------------
//
// Server truth wins on every field it owns. The report exists so a run can be
// read afterwards without a packet trace: it says what we believed, what the
// server said, and which one was kept.

struct ReconcileLine {
    std::string field;
    std::string persisted;
    std::string server;
    std::string result;
};

struct ReconcileReport {
    std::vector<ReconcileLine> lines;
    bool        goalDropped = false;
    std::string goalDropReason;
    bool        firstEverLogin = false;
    i32         driftFields = 0;   // how many fields disagreed
};

// Mutates `st` to agree with the server, and clears anything transient that
// must not survive. Never lowers server progression to match an older save.
ReconcileReport Reconcile(PersistentState* st, const Observation& obs);

// Filesystem-safe identity id from an account/character pair.
std::string MakeIdentityId(const std::string& account, const std::string& character);

}  // namespace uo::life
