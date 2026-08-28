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

#include "uo/market.h"
#include "uo/professions.h"
#include "uo/rules.h"
#include "uo/json.h"
#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::life {

// Bumped whenever the on-disk shape changes. Readers gate every field behind
// the version and default what is absent, so an older state.json loads under
// a newer build instead of being wiped.
// v2 (2026-08-28) adds KnownResourceSource::hinted and ::label -- the
// distinction between a seeded LEAD and an EARNED stand. A v1 file loads
// unchanged: both fields default, which is correct, because everything a v1
// file recorded was written from observation rather than seeded.
inline constexpr int kSchemaVersion = 2;

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
    // Parallel to `skills`: which of them an NPC will teach, and in what order
    // this life wants them. Empty for an M4-era plan, which had no trainers.
    std::vector<bool> viaTrainer;
    std::vector<int>  priority;
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
// KEPT so M4's persisted characters still load and its tests still mean what
// they meant; new characters come from the profession catalogue instead.
BuildPlan FrontierLumberjackSwordsman();

// M5: derive a plan from a profession record. The two starting skills become
// the creation request at 50.0 each; everything else becomes a target the
// character has to earn.
BuildPlan PlanFromProfession(const prof::Profession& p);

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

// Where this character believes a resource can be worked.
//
// TWO KINDS, and the difference is the whole point:
//
//   a HINT is basic shard knowledge -- "Yew has woods" -- seeded once at
//   creation from the atlas. A player knows that much before they ever swing
//   an axe. It is a LEAD: it says roughly where to go, and nothing about
//   whether anything is left there.
//
//   a STAND is EARNED. It is only ever recorded where a chop actually
//   produced a log. Which individual tree still holds wood is not something
//   anyone can know without trying, so it cannot be seeded.
//
// The first M4 runs blurred the two: any tick with trees in view wrote a
// "resource source", so after one session the character held 64 imaginary
// stands, preferred them over asking the atlas, and spent four sessions
// working scrub 210 tiles short of the real Yew woods.
struct KnownResourceSource {
    std::string resource;      // "logs"
    i32 x = 0, y = 0;
    i8  z = 0;
    i64 lastSuccessMs = 0;
    i64 lastSeenMs = 0;
    i32 successes = 0;
    i32 failures = 0;
    bool hinted = false;       // seeded common knowledge, not earned
    std::string label;         // the atlas name, for a legible log line
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

// Ceiling on remembered fear at one spot. Heat compounds on repeat trouble --
// that is the point -- but an unbounded sum is a grudge, not a memory: a live
// session reached 499.89 at one spot and the resulting penalty made the
// character's own profession score negative, so it idled instead of working.
inline constexpr double kMaxDangerHeat = 4.0;

// What one trade of NPC actually said about teaching one skill.
//
// Source-X decides a trainer's ceiling as
//   min(NPC's own skill x TrainSkillPercent, TrainSkillMax, the student's cap)
// (CCharNPCStatus.cpp:514-541), so the ceiling is a property of the INDIVIDUAL
// NPC, not a shard constant. There is no way to know it without asking, and
// asking costs a walk across town -- so the answer is remembered.
//
// A refusal is permanent for that trade: "you already know as much as I can
// teach" means the trainer's ceiling is BELOW the character's skill, and the
// character only grows from here.
struct TrainerVerdict {
    int         skillId = -1;
    std::string trade;          // "mage", "healer", ... the paperdoll title
    bool        taught = false; // false = refused
    i32         atTenths = 0;   // what the character's skill was when asked
    i32         quoted = 0;     // the fee the NPC named, if it named one
    std::string why;            // the refusal, in the NPC's own terms
    i64         whenMs = 0;
};

class Memory {
public:
    void NotePlace(const char* kind, const char* name, i32 x, i32 y, i8 z, i64 nowMs);
    void NoteResource(const char* resource, i32 x, i32 y, i8 z, bool success, i64 nowMs);
    // "I stood here and the resource is here." Refreshes lastSeen WITHOUT
    // scoring a success or a failure -- standing next to a tree is not an
    // attempt to chop it, and counting it as one buries a good stand under
    // hundreds of imaginary failures.
    void NoteResourceSeen(const char* resource, i32 x, i32 y, i8 z, i64 nowMs);
    // Seed a lead from atlas common knowledge. Never counts as a success, and
    // is only ever added once per place.
    void HintResource(const char* resource, const char* label, i32 x, i32 y,
                      i8 z, i64 nowMs);
    // A stand this character has PROVEN: nearest proven-productive first.
    // Returns null when nothing has ever yielded, which is the signal to fall
    // back to a hint or to go looking.
    const KnownResourceSource* BestProvenResource(const char* resource, i32 fromX,
                                                  i32 fromY, i64 nowMs) const;
    // The best untried or least-failed lead, for when nothing is proven.
    const KnownResourceSource* BestHint(const char* resource, i32 fromX, i32 fromY,
                                        i64 nowMs) const;
    void NoteSupplier(const KnownSupplier& s);
    void NoteDanger(i32 x, i32 y, i32 radius, const char* threat, double heat, i64 nowMs);
    void NoteEvent(const char* kind, const char* detail, const char* place,
                   i32 x, i32 y, i64 nowMs);

    // Record what a trade of trainer said about a skill. One row per
    // (skill, trade); a later answer replaces an earlier one.
    void NoteTrainerVerdict(const TrainerVerdict& v);
    // Has this trade already refused to teach this skill? Checked BEFORE
    // walking, so a refusal costs one trip in a character's whole life.
    bool TrainerRefused(int skillId, const char* trade) const;

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
    const std::vector<TrainerVerdict>&      Trainers()  const { return trainers_; }

    std::vector<KnownPlace>&          MutablePlaces()    { return places_; }
    std::vector<KnownResourceSource>& MutableResources() { return resources_; }
    std::vector<KnownSupplier>&       MutableSuppliers() { return suppliers_; }
    std::vector<DangerMemory>&        MutableDangers()   { return danger_; }
    std::vector<LifeEvent>&           MutableEvents()    { return events_; }
    std::vector<TrainerVerdict>&      MutableTrainers()  { return trainers_; }

    void Clear();
    usize ApproximateBytes() const;

private:
    std::vector<KnownPlace>          places_;
    std::vector<KnownResourceSource> resources_;
    std::vector<KnownSupplier>       suppliers_;
    std::vector<DangerMemory>        danger_;
    std::vector<LifeEvent>           events_;
    std::vector<TrainerVerdict>      trainers_;
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
    // The server said "it is too heavy" -- the pack overflowed and items are
    // going on the floor. Definitive, and independent of whether the server
    // ever told us a carry capacity.
    bool overloaded = false;

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

    // What the profession wants bought from a trainer next, or -1. Set by the
    // runner from the build plan; the need model does not know about
    // professions, only about a skill it has been pointed at.
    // Skills a trainer has already refused, so the planner stops choosing
    // them. Filled from Memory each tick; never inferred inside the chooser.
    // The pack, keyed by itemdef defname, for everything any profession in the
    // catalogue produces or consumes. This is what the M7 surplus/shortfall
    // functions read; counting graphics is done once here rather than in every
    // caller, because one item has several graphics by stack size.
    std::vector<market::Stock> pack;

    std::vector<int> trainerRefusedSkills;
    int wantTrainSkill = -1;
    i32 wantTrainTarget = 0;

    i32 SkillTenths(int skillId) const;
    i32 SkillSumTenths() const;
    double HpFraction() const { return hpMax > 0 ? static_cast<double>(hp) / hpMax : 1.0; }
    double WeightFraction() const {
        return maxWeight > 0 ? static_cast<double>(weight) / maxWeight : 0.0;
    }
};

// The next skill worth BUYING from a trainer, or -1. Chosen by the profession's
// own priority order, restricted to targets flagged `viaTrainer`, and only
// while the character is still below what a trainer could give -- there is no
// point paying an NPC for a skill already past its ceiling.
int NextSkillToBuy(const BuildPlan& plan, const Observation& obs,
                   i32 trainerCeilingTenths);


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
    // A skill this build wants that the character does NOT have, and that an
    // NPC will teach for gold. Distinct from NeedTraining, which is "grind the
    // skill I already have upward": this one is BUYABLE, and it is the need
    // that ties M5 progression to the M7 economy.
    NeedSkillTraining,
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
    // A load worth walking to town for. Below this, gathering more beats the
    // trip; at or above it, the trip wins. Same number as logsWorthBanking
    // because it is the same judgement -- "this is a load now" -- reached from
    // the selling side rather than the securing side.
    i32    surplusWorthTrip = 20;
    i32    foodLow          = 1;
    bool   hungerLive       = true;  // HitsHungerLoss=1 on this shard
    // Only used to decide whether walking to a trainer is worth it. The real
    // price is whatever the NPC quotes on arrival, and is never assumed.
    // sphere.ini gives NPCTrainCost=1 gp per 0.1 and NPCTrainPercent=30, so a
    // generic trainer taking a skill 0 -> 30.0 asks about 300; a guildmaster
    // overrides to 50.0 at 50% (c_human_guildmasters.scp:23) and asks ~500.
    i32    trainerFeeGuess  = 300;

    // WHICH LIFE is asking. Needs used to be written for one character -- an
    // axe, bandages, logs -- so a mage logged in and immediately decided it
    // needed a hatchet and eight bandages, and spent its whole session on a
    // need it could never satisfy. The catalogue already says what each life
    // carries, so the needs read it instead of assuming.
    //
    // nullptr means "the M4 lumberjack rules", which is what a life saved
    // before the catalogue existed still expects.
    const prof::Profession* profession = nullptr;
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
    // Find a trainer, ask the price, pay it, and verify the skill moved.
    TrainAtNpc,
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

    // What this character has SEEN things sell for, and where its gold came
    // from and went. Both are per-character by construction and both persist,
    // because a price learned by walking to a vendor is worth as much as a
    // trainer verdict and is lost the same way if it does not survive logout.
    market::PriceBook prices;
    market::Ledger    ledger;

    // WHERE THIS CHARACTER LIVES. Chosen once from the profession's own list
    // and then never re-rolled: a bot that picks a new home every login is a
    // tourist, and the whole point is that the same smith is at the same forge
    // every evening.
    std::string homeCity;

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
