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
#include "uo/production.h"
#include "uo/rules.h"
#include "uo/json.h"
#include "uo/types.h"

#include <string>
#include <utility>
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
// Trip budget arithmetic -- pure, so it links into ctest without dragging
// Client along (see CMakeLists.txt "the arithmetic links into ctest without
// dragging Client" note on BuyPlan.cpp for the same split applied here).
//
// Generalises the TRADE_WITH_PLAYER-only clock check
// (life::Planner::kMarketTripBudgetMs is a Runner.h constant, itself
// measured from real market-trip legs) to any goal whose service pick can
// land far enough away to matter. BUY_SUPPLIES sent a Skara Brae fisher
// through a working moongate hop to an island 232 tiles and one gate away,
// with no check anywhere for whether the session had room for the walk
// AND the wind-down that still had to follow it -- wind-down caught the
// stalled trip 24 minutes later and logged the character out in the open,
// near Ocllo (docs/LIFE_GATE_WAVE1.md theme 2,
// run_gates/g_Dorvar.console.txt 00:40-01:04).
// ===========================================================================

// Client's running cadence (bot-client.md "Cadence": runStepMs = 200).
inline constexpr i64 kTripMsPerTile = 200;

// How long, in milliseconds, a planned trip of this many tiles is expected
// to take. The bare cadence is a floor, not a forecast -- a real trip
// crosses doors, answers gate gumps, and stops to fight, none of which the
// tile count alone sees -- so this pads the floor rather than reporting it
// bare. 50% slack is generous enough that a trip which really can finish is
// not vetoed by rounding, and tight enough that one which plainly cannot is
// still caught with the wind-down reserve intact.
inline constexpr i64 EstimateTripTimeMs(i32 estimatedTiles) {
    const i64 tiles = estimatedTiles > 0 ? static_cast<i64>(estimatedTiles) : 0;
    return tiles * kTripMsPerTile * 3 / 2;
}

// True when a trip of this many tiles can be walked AND wind-down can still
// happen afterwards, out of the session time actually left. `remainingMs`
// is what Observe() already computes per goal (sessionLimitMs minus
// elapsed); `windDownBudgetMs` is the same reserve wind-down itself is
// bounded by. The walk alone is not the trip -- logging out has to fit too.
inline bool TripFitsSessionBudget(i64 remainingMs, i32 estimatedTiles,
                                  i64 windDownBudgetMs) {
    if (remainingMs <= 0) return false;
    return remainingMs >= EstimateTripTimeMs(estimatedTiles) + windDownBudgetMs;
}

// ===========================================================================
// Deposit settlement -- confirm a bank deposit from what actually left the
// pack, never from having merely issued the drag.
//
// Lyra's gold deposit called NoteProgress() the instant it ISSUED the move,
// not once it LANDED -- and the box kept answering "item landed in
// 0x4000C91D, not the destination 0x4000C944", the same "this box is not
// really open" case the item-deposit loop (Runner::bankDepositItem_) already
// bounds and recovers from. Crediting an ask as progress hid the failure
// from the "progress==0 -> stand down" guard and NeedBank never went quiet:
// BANK kept outscoring FILL_SPELLBOOK every ~20 s for the rest of the
// session (docs/LIFE_GATE_WAVE1.md theme 3,
// run_gates/g_Lyra.console.txt 00:40:09-00:42:21).
// ===========================================================================

struct DepositOutcome {
    bool progressed = false;  // the pack's count actually went down
    bool giveUp = false;      // tries exhausted -- treat the box as not open
};

// `before`/`after` are the pack's own count of the thing being deposited,
// sampled immediately before the drag was issued and again once the action
// has resolved. `tries` is the caller's own persistent counter, passed by
// reference: reset to 0 the moment real progress is seen, incremented on
// every landing that moved nothing. Once it exceeds `maxTries`, giveUp asks
// the caller to stop trusting this box (Client::ForgetBankContainer and try
// a different one) rather than dragging at it forever.
inline DepositOutcome SettleDeposit(i32 before, i32 after, int& tries,
                                    int maxTries) {
    if (before - after > 0) {
        tries = 0;
        return DepositOutcome{true, false};
    }
    ++tries;
    return DepositOutcome{false, tries > maxTries};
}

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

// WHAT A CHARACTER MAY WEAR, AND HOW GOOD IT IS.
//
// Declared here rather than in Runner.cpp's anonymous namespace because
// Runner::MayWear takes one, and a member function cannot name a type that
// only exists inside one translation unit's unnamed namespace.
//
// Class is not a preference on this shard. revolutionuo.net's mining guide
// states that characters wearing ore-smithed metal sets "buyu atamazlar" --
// cannot cast at all -- so Metal on a caster ends its profession rather than
// costing it a little mana.
enum class ArmorClass : u8 { Cloth, Leather, Metal, Shield };

struct ArmorPiece {
    u16        graphic;
    u8         armor;
    u16        reqStr;
    ArmorClass cls;
};

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

// PER-CREATURE-TYPE verdict, learned by fighting. Keyed on the creature's
// NAME as the client sees it -- the paperdoll/mobile name that arrives on the
// wire -- never a server-side chardef id, because no real client can read
// one. DangerMemory answers "is this SPOT bad"; this answers "is this KIND OF
// THING bad", and the two live side by side rather than one replacing the
// other: one death at the graveyard gate should not read as "everything in
// this graveyard is lethal" when only the lich actually was.
//
// SAME DECAY SHAPE AS DangerMemory ON PURPOSE (Memory::NoteDanger /
// Memory::DangerHeatAt) -- exponential decay toward zero with the same
// kDangerHalfLifeMs, capped at the same kMaxDangerHeat magnitude. The one
// difference is sign, because unlike a place, a creature type can be
// EXONERATED as well as incriminated:
//   heat > 0  ->  evidence this creature type is dangerous (a death, a
//                 near-death flee)
//   heat < 0  ->  evidence this creature type is safe (a kill that cost
//                 little)
//   heat == 0 ->  unknown -- never fought, or the evidence fully decayed
struct CreatureVerdict {
    std::string name;      // the mobile name, exactly as the client sees it
    double heat = 0.0;      // signed, decaying, capped at +/- kMaxDangerHeat
    i64 atMs = 0;
    i32 fights = 0;         // how many outcomes contributed, for a log line
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
inline constexpr usize kMaxCreatures = 64;
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

// Evidence magnitudes a fight's OUTCOME contributes to a CreatureVerdict.
// Negative = safe, positive = dangerous. Proving danger takes one bad
// surprise; proving safety takes repetition -- a single cheap kill should not
// erase a single death, so the danger-side evidence outweighs the safe-side
// evidence at the same cardinality. Whoever settles a fight (the runner, not
// this header) picks one of these per outcome and calls
// Memory::NoteCreatureOutcome with it.
inline constexpr double kCreatureEvidenceCheapKill  = -0.5;  // won, hardly scratched
inline constexpr double kCreatureEvidenceCostlyKill = -0.15; // won, but it hurt
inline constexpr double kCreatureEvidenceNearDeathFlee = 1.0;  // fled below fleeHpFraction
inline constexpr double kCreatureEvidenceDeath = 2.0;  // it killed us

// What one trade of NPC actually said about teaching one skill.
//
// Source-X decides a trainer's ceiling as
//   min(NPC's own skill x TrainSkillPercent, TrainSkillMax, the student's cap)
// (CCharNPCStatus.cpp:514-541), so the ceiling is a property of the INDIVIDUAL
// NPC, not a shard constant. There is no way to know it without asking, and
// asking costs a walk across town -- so the answer is remembered.
//
// A REFUSAL IS ABOUT THE NPC, NOT ABOUT THE TRADE.
//
// This used to say a refusal was permanent for the whole trade, reasoning that
// "you already know as much as I can teach" put the trainer's ceiling below
// the character. The first half is right and the second does not follow, and
// Source-X's own formula says why: NPC_GetTrainMax (CCharNPCStatus.cpp:514-541)
// is min(THIS NPC's own skill x NPCTrainPercent, NPCTrainMax, the student's
// cap). The ceiling is a property of the individual NPC's skill value, so two
// mages of the same trade cap at different places -- Alenne stopped teaching
// Ysolde Meditation at 21.9, which puts Alenne's own Meditation near 73.0,
// while a mage at 100.0 would have taught her to 30.0.
//
// Writing the trade off on one answer cost Ysolde her only trainable skill
// permanently and, with Inscription and Magery both already above the 30.0
// generic ceiling, left her with nothing any trainer could sell her -- which
// is what blocked the earn-then-train gate (run_m5/p0gate2,
// "want_train=nothing"). A player told "I have nothing left to teach thee"
// walks to a different mage.
struct TrainerVerdict {
    int         skillId = -1;
    std::string trade;          // "mage", "healer", ... the paperdoll title
    u32         npcSerial = 0;  // WHICH one answered; 0 = an older record
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

    // Learn from ONE fight's outcome against a creature TYPE, keyed on its
    // client-visible name. `signedEvidence` is one of the
    // kCreatureEvidence... constants above: negative moves the verdict
    // toward "safe", positive moves it toward "dangerous". Compounds onto
    // the DECAYED value exactly the way NoteDanger does -- not the raw one,
    // so a scare from an hour ago is not treated as if it just happened --
    // and is capped at +/- kMaxDangerHeat for the same reason NoteDanger's
    // heat is capped: an unbounded sum is a grudge, not a memory.
    void NoteCreatureOutcome(const char* name, double signedEvidence, i64 nowMs);
    // Decayed verdict for one creature type: positive = dangerous, negative
    // = safe, 0.0 = unknown (never fought, or the evidence fully decayed).
    // What combat::ChoosePrey asks before picking a fight.
    double CreatureDanger(const char* name, i64 nowMs) const;

    // Record what ONE trainer said about a skill. One row per
    // (skill, trade, npcSerial); a later answer from the same NPC replaces an
    // earlier one.
    void NoteTrainerVerdict(const TrainerVerdict& v);
    // Has THIS NPC already refused to teach this skill? Checked before asking
    // again, so one refusal is never re-earned from the same mouth.
    bool TrainerRefusedByNpc(int skillId, u32 npcSerial) const;
    // Has the TRADE been exhausted -- have enough different NPCs of it refused
    // that walking to another is no longer worth it? A single refusal is not
    // evidence about a trade whose ceiling is per-NPC (see TrainerVerdict).
    static constexpr int kTradeExhaustedAfter = 3;
    bool TrainerRefused(int skillId, const char* trade) const;
    // "You already know as much as I can teach", from ANY one NPC, is a
    // statement about the CHARACTER, not about that NPC -- unlike "I know
    // nothing about that", which is only ever about the one who said it. One
    // such answer retires the skill from NPC training for good.
    bool TrainerSaysMaxed(int skillId) const;
    // Serials of this trade that refused this skill, for the skip list a
    // character uses to walk to a DIFFERENT trainer.
    std::vector<u32> TrainersWhoRefused(int skillId, const char* trade) const;

    // Decayed heat at a point. 0 when nothing bad ever happened nearby.
    double DangerHeatAt(i32 x, i32 y, i64 nowMs) const;
    // Drop danger notes whose decayed heat has fallen below the floor.
    void   ExpireDanger(i64 nowMs, double floorHeat = 0.05);

    const KnownPlace*          BestPlace(const char* kind) const;
    // THE NEAREST one of a kind, which is what a character standing somewhere
    // actually wants. BestPlace answers "most recently verified" and knows
    // nothing about where the asker is -- so a fisher living in Skara Brae who
    // banked once in Yew walked back to Yew across the map every time.
    const KnownPlace*          NearestPlace(const char* kind, i32 x, i32 y) const;
    // Unlearn a place that turned out not to be one. Belief that survives
    // being disproved is not memory, it is a loop: a "bank" recorded on the
    // Britain dock sent one character on the same futile walk three times a
    // minute, and it would have done so forever.
    bool                       ForgetPlace(const char* kind, i32 x, i32 y);
    // The same lesson, for suppliers. A remembered trainer is a POSITION, and
    // the NPC that made it memorable can be gone -- despawned, re-rolled by a
    // spawner, or simply wandered off. Standing on the spot and seeing nobody
    // is the disproof; without this the goal walks to where it already is.
    bool                       ForgetSupplier(const char* need, i32 x, i32 y);
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
    const std::vector<CreatureVerdict>&     Creatures() const { return creatures_; }

    std::vector<KnownPlace>&          MutablePlaces()    { return places_; }
    std::vector<KnownResourceSource>& MutableResources() { return resources_; }
    std::vector<KnownSupplier>&       MutableSuppliers() { return suppliers_; }
    std::vector<DangerMemory>&        MutableDangers()   { return danger_; }
    std::vector<LifeEvent>&           MutableEvents()    { return events_; }
    std::vector<TrainerVerdict>&      MutableTrainers()  { return trainers_; }
    std::vector<CreatureVerdict>&     MutableCreatures() { return creatures_; }

    void Clear();
    usize ApproximateBytes() const;

private:
    std::vector<KnownPlace>          places_;
    std::vector<KnownResourceSource> resources_;
    std::vector<KnownSupplier>       suppliers_;
    std::vector<DangerMemory>        danger_;
    std::vector<LifeEvent>           events_;
    std::vector<TrainerVerdict>      trainers_;
    std::vector<CreatureVerdict>     creatures_;
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
    // Heal potions carried. A crafter's only self-heal: its Healing skill
    // cannot make a bandage work, so this is what NeedEquipment watches for
    // those lives instead. See Professions.cpp CrafterHealPotions.
    i32  healPotions = 0;
    // How many of shirt/trousers/shoes are on neither the body nor in the
    // pack. A resurrected character has three, and looks it.
    i32  clothingMissing = 0;
    i32  logs = 0;
    i32  food = 0;
    // HUNGER, as the server itself says it. Sphere sends "You are <level>"
    // with the levels starving / very hungry / hungry / fairly content /
    // content / fed / well fed / stuffed (core/messages.scp:470-477,
    // CCharAct.cpp:5798 DEFMSG_MSG_HUNGER). Read from the journal, which is
    // what a player reads -- never from a server-side food value, which no
    // real client can see.
    bool hungry     = false;   // "hungry" or worse
    bool starving   = false;   // "starving" -- damage is imminent
    bool axeInPack = false;
    bool axeEquipped = false;
    bool weaponEquipped = false;
    // Three protected layers with a core torso/leg piece among them -- the
    // gate DoTrainCombat refuses to leave for a hunting ground without
    // (Runner::HasBasicArmor). Defaults TRUE so a pure need model with no
    // client behind it does not invent a starter-armour errand.
    bool hasBasicArmor = true;

    // What is happening around us, as the client can see it.
    // Has the market just been tried and found empty? A character that has
    // shouted its wares to nobody does not keep shouting -- it goes back to
    // work and tries again later. Without this the trade errand outranked
    // everything a solo character could actually finish and starved it.
    bool marketQuiet = false;
    // Items whose PLAYER market was tried and came back empty, recently.
    //
    // `marketQuiet` is the same fact with the item thrown away, which is
    // enough to damp the trade goal but not enough to decide anything about a
    // particular material: a tailor that failed to buy logs has learnt
    // nothing about cloth. Filled in Observe from the `no_player_seller`
    // events DoTradeWithPlayer already writes, inside the same memory window
    // PlayersDeclined uses for the sell side.
    std::vector<std::string> noSellerFor;
    bool NoSellerFor(const std::string& item) const {
        for (const std::string& s : noSellerFor) { if (s == item) return true; }
        return false;
    }
    // IS THERE STILL SESSION ENOUGH TO GO AND ASK? The same test
    // DoTradeWithPlayer makes on entry (Runner.cpp: `leftMs <
    // kMarketTripBudgetMs` -> goal_blocked "not enough session left for the
    // trip"), computed once in Observe so the pure need model can see it too.
    //
    // A need that waits on a market answer has to be able to tell "nobody
    // answered" from "this life will never get there to ask this session";
    // Amara sat BLOCKED on the first while the runner was refusing on the
    // second, and the two never met. Defaults TRUE: no session clock, no veto.
    bool marketTripFitsSession = true;
    // THE THIRD WAY THE MARKET CANNOT BE ASKED RIGHT NOW: the trade goal has
    // stood ITSELF down. `noSellerFor` says "asked and nobody answered" and
    // `marketTripFitsSession` says "no session left to go and ask"; neither is
    // true while TRADE_WITH_PLAYER is merely on cooldown after achieving
    // nothing, and a need that waits on an answer then waits forever. Amara
    // sat with NeedCloth BLOCKED at 0.00 -- "the player market has not been
    // asked for it yet" -- for fourteen consecutive assessments while
    // TRADE_WITH_PLAYER was cooling for another 599s, so the only goal left
    // scoring was EXPLORE at 15.0 and she wandered
    // (run_gates/g_Amara.console.txt:148,200,204; wander was 50% of that
    // session's picks). Filled in Observe from Planner::Cooling; defaults
    // FALSE so the pure need model is unchanged without it.
    bool marketAskOnCooldown = false;
    // THE MOUNT ERRAND HAS STOOD ITSELF DOWN. Same shape and the same reason as
    // marketAskOnCooldown: NeedMount fired at 0.80 (BUY_MOUNT 204, above every
    // working goal a fighter has) on every assessment of every session, while
    // DoBuyMount was refusing on its own cooldown -- "no way to an animal
    // trainer", "65-item list has no riding horse in stock", "not enough
    // session left for the trip tiles=1196". A need that cannot see its own
    // errand's stand-down asks for the same impossible thing all day.
    bool mountAskOnCooldown = false;
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
    // STANDING AT THE SHARD'S MARKET (market::kMarketBankPlaceId), judged by
    // GEOMETRY -- Runner::AtMarketBank, not by an open box. The planner needs
    // it because arriving is the expensive half of a market trip: one leg is
    // 250 s measured (docs/S5_MARKET_TRIP_PLAN.md section 3), and a character
    // that has just paid for it must not be walked away again by an ordinary
    // errand. run_r4/pair_Tarath.console.txt:1812 20:39:34.756 --
    // "goal_changed=REPLACE_EQUIPMENT from=TRADE_WITH_PLAYER reason=
    // REPLACE_EQUIPMENT 130.0 superseded TRADE_WITH_PLAYER 79.8" -- fourteen
    // tiles short of the bank, after a full journey, for heal potions.
    bool atMarket = false;
    // Standing where NO SKILL CAN ADVANCE (REGION_FLAG_SAFE). Shrines,
    // jails, the great castles, the Lycaeum, Empath Abbey. Nothing the
    // client shows says so, and mana spent practising here is simply
    // gone -- see Atlas::AllowsSkillGainAt.
    bool inNoGainRegion = false;

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
    // Tools the character actually has, by the profession's own name for them
    // ("hatchet", "fishing pole"). Filled in Observe from the pack AND both
    // hands, because a pole in the pack is not a pole in hand and several
    // Sphere skills read the character's weapon.
    std::vector<std::string> toolsHeld;
    bool HasTool(const std::string& name) const {
        for (const std::string& t : toolsHeld) { if (t == name) return true; }
        return false;
    }
    // What the character remembers having in the bank. Same keys as `pack`.
    std::vector<market::Stock> bank;
    bool bankOpen = false;

    // COINS ACTUALLY IN THE PACK, as distinct from `gold`.
    //
    // `gold` is the status-bar figure (packet 0x11, offset 58) and on this
    // shard that counts the BANK BOX as well. Reading it as "what I am
    // carrying" produced two bugs at once: the bank goal kept trying to
    // deposit a spare 8,785 coins that were already in the box, and the
    // trainer goal accepted a 196 gold quote and then found "no gold stack in
    // the pack". What a life can hand over is only ever what it holds.
    i32 goldOnHand = 0;
    // Coin a pending purchase needs in the pack but does not have. Set by the
    // goal that wants to buy something; read by NeedBank, so "go to the bank"
    // can mean withdraw and not only deposit.
    i32 coinWanted = 0;
    std::vector<int> trainerRefusedSkills;
    int wantTrainSkill = -1;
    // Which skill this life should PRACTISE next, and how. -1 = none.
    int wantPracticeSkill = -1;
    // REAGENTS THE PRACTICE SPELL EATS AND THE PACK HAS NOT GOT.
    //
    // Written by the runner when PRACTICE_SKILL looks in the pack and finds it
    // cannot pay for anything its spellbook holds (uo::spell::
    // ChoosePracticeSpell); read by AssessNeeds so BUY_SUPPLIES gets a turn.
    // It is a shopping list rather than a flag because a single spell costs up
    // to four different reagents and one trip should fetch them all.
    //
    // The quantity is per reagent and is DERIVED, not constant: it is the
    // number of casts this character still expects to make this session at its
    // own observed rate (uo::spell::PlanReagentBuy).
    std::vector<std::string> practiceReagentsShort;
    i32 practiceReagentQty = 0;
    // Does this character already have a tamed animal following it? A tamer
    // that keeps taming while it owns one is hoarding, not working.
    bool hasPet = false;
    // The spellbook this character is carrying, and how many spells are in it.
    // 0 serial means no book at all -- which is a different problem from an
    // empty one, and the goal treats it as such.
    u32 spellbookSerial = 0;
    int spellsKnown = 0;
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
// --- crafting ---------------------------------------------------------------
//
// What this life should make next, and what stands in the way. Answered in one
// place because two systems ask it: the need model (is there a reason to go
// shopping or to sit down and work?) and the goal that carries it out.
//
// Only ever names something the faucet registry says may legitimately be sold.
// A character does not manufacture goods it has nowhere to take.
struct CraftIntent {
    const char* item = nullptr;      // what to make, or nullptr for nothing
    bool skillsMet = false;          // the recipe's own skill requirements
    // Inputs the pack is short of, in the quantities still needed.
    std::vector<prod::Ingredient> missing;
    const char* why = "";            // printable, always set
};
// WHAT WAS MADE LAST, SO A CRAFTER'S DAY IS NOT ONE PRODUCT REPEATED.
//
// "full crafters should not only do 1 craft always through the day; maybe
// different craft focuses" (project owner, 2026-09-01).
//
// The planner already refuses to let one GOAL or one FAMILY own a session
// (Planner::Satiation / Planner::FamilySatiation). CRAFT is a single
// GoalKind, so none of that damping can see the choice made INSIDE it:
// ChooseCraft takes the first workable entry of `produces`, in list order, so
// a full_crafter with eight recipes made the same one every time it sat down
// and the goal histogram could not tell the difference.
//
// Same shape, one level down, and deliberately the SMALLEST thing that fits
// it: a run of kFocusRun of the same output is a sitting at the bench. After
// that the output is satiated and the next workable recipe wins -- until a
// different one is made (the streak breaks, exactly as Planner::NoteRan
// breaks a goal's streak) or the streak simply goes stale.
//
// BOUNDED, like every other satiation here: if nothing else is workable the
// satiated recipe is still chosen. A crafter that would rather make nothing
// than repeat itself has stopped being a crafter.
class CraftFocus {
 public:
    // One completed piece (or batch) of `item`.
    void NoteMade(const char* item, i64 nowMs);
    // Has this life just spent a sitting on `item`?
    bool Satiated(const char* item, i64 nowMs) const;
    const std::string& Last() const { return last_; }
    i32 Run() const { return run_; }

    // A PRODUCT THIS SESSION CANNOT REACH.
    //
    // CRAFT reaches an output by walking the shard's legacy skill menu, and
    // the route is a static table (Runner.cpp kCraftMenus). When an output has
    // no entry, or the menu that opened offers nothing on its path, the goal
    // refuses with REFUSE_MISSING_RECIPE -- and ChooseCraft, which knows only
    // about the recipe graph, hands back the SAME output on the very next
    // tick. Wave 2026-09-02: i_board 45 refusals (Cyras/Halain/Vorar),
    // i_gears 30 (Serena), i_ingot_iron 10 (Draver), 17 goal_spinning=CRAFT
    // flags between them and zero craft output fleet-wide.
    //
    // So the refusal is remembered. Three strikes, not one, because the
    // menu-content branch can fire on a half-read dialog whereas the
    // table-miss branch is deterministic; three costs a deterministic miss
    // nothing and protects a transient one. Session-scoped on purpose -- it is
    // a fact about this build of the route table, not about the character.
    void NoteNoRoute(const char* item);
    bool Unreachable(const char* item) const;
    static constexpr i32 kNoRouteStrikes = 3;

    // Four in a row is a sitting: long enough to be worth setting up for and
    // to make a sale batch, short enough that a session shows more than one
    // product. Not evidence-derived -- there is no Revolution statement about
    // how long a crafter stays on one item -- so it is named here rather than
    // buried, and it is the number to change if live runs look wrong.
    static constexpr i32 kFocusRun = 4;
    // Fades on the same three minutes the goal/family measures use, for the
    // same reason: a preference, not a punishment.
    static constexpr i64 kFocusFadeMs = 3 * 60 * 1000;

 private:
    std::string last_;
    i32 run_ = 0;
    i64 lastMs_ = 0;
    std::vector<std::pair<std::string, i32>> noRoute_;
};

// HOW CRAFT REACHES ONE OUTPUT THROUGH THE SHARD'S LEGACY SKILL MENUS.
//
// At most three levels, each matched as a case-insensitive substring of an
// option's text; a leading '^' anchors the match at the start of the option
// instead. The table lives in Runner.cpp beside the goal that walks it -- only
// the lookup is public, so a test can assert that a route exists without a
// server, a session or a craft menu. An output with no row here cannot be
// crafted and the goal refuses it as REFUSE_MISSING_RECIPE.
struct CraftMenuPath {
    const char* item;
    const char* step1;
    const char* step2;   // nullptr for a flat menu
    const char* step3;   // blacksmithing nests one level deeper than the rest
};
const CraftMenuPath* CraftMenuFor(const std::string& item);

// `focus` is optional. When given, a fully-stocked recipe this life has just
// spent a sitting on yields to another fully-stocked one; when omitted (or
// when it is the only thing workable) the answer is unchanged.
CraftIntent ChooseCraft(const prof::Profession& p, const Observation& obs,
                        i32 batch, const CraftFocus* focus = nullptr);

// HOW BIG A SITTING THE PACK IS ALREADY PAYING FOR.
//
// A bulk purchase decides the size of the batch, not the other way round.
// Elara walked out of the Britain alchemist with 84 nightshade -- 42 poisons'
// worth -- and four empty bottles from her newbie kit, and then stopped
// shopping: `craftBatch` is 5, five potions want five bottles, she had four,
// and one bottle short is a shortfall so small that the two-question rule
// ("can I make one right now?") answered yes and no errand was raised at all
// (run_gates/g_Elara.console.txt, 2026-09-04 00:53). She brewed twice and ran
// dry with 82 leaves of nightshade in the pack.
//
// So: take the recipe this life would work on, ask how many outputs its
// BEST-STOCKED input could fund, and use that as the batch when it is larger
// than the floor. The best-stocked input is by definition not short at that
// size, so this can never invent a shortfall of the thing just bought -- it
// only ever asks for the OTHER half of a recipe already half paid for. Returns
// `floorBatch` when nothing can be worked on.
i32 CraftBatchFromStock(const prof::Profession& p, const Observation& obs,
                        i32 floorBatch, const CraftFocus* focus = nullptr);

// IS THIS SOMETHING THE SHEEP-TO-CLOTH CHAIN PRODUCES?
//
// Named once because two systems ask it and they must not disagree: the need
// model (may I raise NeedCloth for this shortfall?) and MAKE_CLOTH itself (am
// I done?). The four are wool, yarn, the bolt and cut cloth -- every stage the
// wheel, the loom and the scissors make between them.
//
// i_thread is deliberately NOT here. Thread is spun from COTTON, six per pile
// (Source-X CClientTarg.cpp:2078), and no amount of wool produces any; a loop
// that claimed otherwise would spin forever. Every stock Tailoring recipe on
// this runtime wants `<n> i_cloth,1 i_thread`, so sewing stays blocked on a
// cotton source that has not been proven -- recorded, not worked around.
bool IsWoolChainMaterial(const char* item);

// IS THIS ITEM WORK IN PROGRESS, RATHER THAN STOCK?
//
// Wool, yarn and the bolt are STEPS on the way to cloth: the wheel, the loom
// and the scissors each eat the previous one, and the only thing the sewing
// recipes actually take is CUT cloth. A bolt is therefore never a thing to put
// in a box or offer to a buyer while the life still owes itself cloth --
// "bolt should be cut not banked" (project owner, 2026-09-02).
//
// WHAT WENT WRONG WITHOUT IT: `produces` opens with i_cloth_bolt
// (Professions.cpp), so market::Surplus called Aelia's first bolt spare stock,
// and a bolt weighs 50 stones (runtime/scripts/items/
// i_profession_tailor_tanner.scp:74-80, WEIGHT=50.0 RESOURCES=50 i_cloth) --
// enough on its own to take her from 70/124 to 120/124 and raise NeedBank
// "deposit carried load" at 0.72 against NeedCloth's 0.51. Forty milliseconds
// after the loom answered, BANK superseded MAKE_CLOTH and she walked 47 tiles
// to deposit it, with the scissors in her pack the whole time
// (run_gates/g_Aelia.console.txt:442,460,519).
//
// THE GATE IS THE CLOTH, NOT THE PROFESSION. An intermediate is only in
// progress for a life whose OWN recipes eat cloth, and only while it holds
// less cloth than a sitting of those recipes wants -- a tailor with a chest of
// cloth and three spare bolts is holding goods, and goods may be banked or
// sold. i_cloth itself is never "in progress": it is the finished material.
bool WoolChainWorkInProgress(const prof::Profession& p,
                             const std::vector<market::Stock>& holdings,
                             i32 craftBatch, const char* item);

// Does this life go looking for fights, or only finish the ones that find it?
// Read off the build -- a profession that wants MORE than the 50.0 creation
// grant in a weapon school intends to use it. Shared because two systems ask:
// the need model (is there a reason to travel?) and the goal that travels.
bool WantsToHunt(const prof::Profession& p);

// The build's own weapon-school basic -- katana, kryss, club or bow -- read
// off the SAME weapon-skill target WantsToHunt reads (life/Identity.cpp), so
// the two can never disagree about which fighters this applies to.
//
// Pure and Client-free on purpose, the same split AcquirePlan.cpp and
// BuyPlan.cpp already made for their own decisions ("so the decision links
// without Client") -- this is what lets ctest exercise the mapping directly
// rather than through the live bot.
struct SchoolWeapon {
    int         skill = 0;             // rules::kSwordsmanship etc.
    u16         graphics[2] = {0, 0};  // {base, DUPELIST flip}; see FindAny
    const char* defname = "";          // for vendor-policy lookups/memory
    const char* item = "";             // human-readable, for log lines
    bool        bowyerFallback = false; // also try the unambiguous bowyer
};
const SchoolWeapon* SchoolWeaponFor(const prof::Profession& p);

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
    // Goods this life cannot use and no NPC will buy. The ONLY market for
    // them is other characters, so this is what sends a bot to where players
    // gather to announce what it has.
    NeedTrade,
    // Raw material this life gathers and sells. Distinct from NeedLogs, which
    // is the M4 lumberjack's own: this one is generic and reads the profession.
    NeedCatch,
    // Inputs this life needs to MAKE what it sells, and the making itself.
    // A crafter with no reagents is not idle by choice, and the difference
    // between "cannot craft" and "has nothing to craft with" is the whole
    // reason these are two needs and not one.
    NeedSupplies,
    NeedCraft,
    // PRACTISE a skill by doing it, as opposed to buying tenths from an
    // NPC (NeedSkillTraining). This is how a skill actually reaches 100:
    // a mage casts, a warrior fights, a healer bandages. It is a wholly
    // different activity from paying a guildmaster, and conflating the
    // two is why every non-combat skill sat BLOCKED with the reason
    // "nothing is here to practise combat on" -- including Inscription.
    NeedPractice,
    // A MAGE'S BOOK IS EQUIPMENT, and it is never finished on day one. Circles
    // 1-4 are bought from a mage shop but come out RANDOM
    // (random_first_circle .. random_fourth_circle); circle 5 and part of 6
    // must be bought by name from a scribe; circles 7 and 8 are sold by
    // nobody on this shard and come from dungeon chests and monster loot.
    // See docs/REVOLUTION_GAMEPLAY_TRUTH.md 3.5. So this is a need that is
    // satisfied slowly, across sessions, and never in one errand.
    NeedSpells,
    // BANDAGES THIS CHARACTER MUST MAKE RATHER THAN BUY.
    //
    // Distinct from NeedEquipment, which wants bandages and assumes a shop.
    // "if warrior economy is good then he can buy bandage and potion,
    // otherwise go get yourself wool make bandage" (project owner) -- so this
    // is the POOR branch, and it must not fire for a character who can simply
    // pay for them.
    NeedMakeBandages,
    // BETTER ARMOUR THAN IS BEING WORN. Ongoing, not a one-off: loot arrives
    // in the pack throughout a life, and on this shard the class rule is
    // absolute -- a metal set stops a caster casting at all.
    NeedGear,
    // ORE. The miner's equivalent of NeedLogs and NeedCatch, which were both
    // written for one profession each and left mining with nothing.
    NeedOre,
    // AN ANIMAL WORTH TAMING. A tamer with no pet is a tamer in name only.
    NeedPet,
    // ORE IN THE PACK AND A FORGE SOMEWHERE. The step between digging and
    // smithing, and it did not exist: Corwyn mined all session and the ore
    // just accumulated, because nothing in the life could turn it into metal.
    // "it didnt smelt iron ore" (project owner, 2026-08-29).
    NeedSmelt,
    // CLOTH THIS LIFE MUST MAKE RATHER THAN BUY.
    //
    // Owner ruling 2026-09-02: a tailor buys cloth from PLAYERS first; when
    // nobody is selling it GATHERS it -- sheep, shears, wheel, loom, scissors
    // -- and it never buys cloth, thread or yarn from an NPC. So this is the
    // second half of a two-step policy and it must not fire until the first
    // half has actually been tried: the gate is Observation::NoSellerFor,
    // which reads the `no_player_seller` the WTB window writes when its
    // listen period expires unanswered. Firing it without that gate would be
    // a tailor walking to Yew while a lumberjack stood at the bank with a
    // bale for sale.
    NeedCloth,
    // A riding horse. Every Revolution player bought one first -- a
    // dated forum price (800 gp, 24.03.2011) and the owner's own rule
    // ("buy a horse first, mount, then do the rest", 2026-09-02). Raised
    // only while unmounted, no horse of ours stands beside us, and the
    // purse clears the reserve plus the trainer's price.
    NeedMount,
    // Wool as a FIGHTER'S income. Owner ruling 2026-09-02: a tailor has no
    // attack skill, so the shear -> kill -> carve loop (4 wool a sheep) is
    // the warriors' -- "an additional income source" -- and they sell the
    // wool to tailors. Raised only for a life that hunts and lists i_wool
    // in `produces`; carrying a load already, or no blade, mutes it.
    NeedWoolIncome,
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
    // How many of a thing to make in one sitting. Small on purpose: reagents
    // cost real gold, the sale price is small, and a batch that empties the
    // purse before the first sale proves nothing about whether the trade pays.
    i32    craftBatch       = 5;
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

    // WHAT THIS LIFE HAS JUST BEEN MAKING. Optional; nullptr is the
    // pre-rotation answer. Held here so the NEED model asks exactly the
    // rotation question the CRAFT goal answers -- a need that names one item
    // while the errand makes another is a telemetry lie.
    const CraftFocus* craftFocus = nullptr;

    // WHICH SESSION THIS IS, counted by Identity::sessions.
    //
    // The only durable clock a need can read. LifeEvent::atMs is a steady_clock
    // reading taken inside one process (Memory.cpp NoteEvent), so it is not
    // comparable across logins -- a "rest for two hours" written in ms is a
    // rest that expires the moment the next process starts, which is exactly
    // the 600 s BUY_MOUNT cooldown that re-fired every session. Counting
    // SESSIONS is the honest unit for "not again today".
    i32 sessionIndex = 0;
};

std::vector<Need> AssessNeeds(const BuildPlan& plan, const Memory& mem,
                              const Observation& obs, const NeedConfig& cfg);

// Does this life carry the named tool / consumable AT ALL? The catalogue
// answers; a life saved before the catalogue existed (cfg.profession ==
// nullptr) wants everything, which is the pre-M5 behaviour.
//
// Public because the ERRAND must ask the same question the NEED asks. The
// bandage need is gated on WantsConsumable(cfg, "bandage") -- false for every
// crafting life since "so crafter do not buy bandages" (project owner,
// 2026-08-30) -- while DoReplaceEquipment asked nothing, so a miner_smith
// walked to a healer and bought thirty bandages its zero Healing could not
// use, and the heal-potion branch sitting behind it never ran.
bool WantsTool(const NeedConfig& cfg, const char* name);
bool WantsConsumable(const NeedConfig& cfg, const char* name);

// ===========================================================================
// Goals -- utility selection, with commitment
// ===========================================================================

// WHICH PART OF A LIFE A GOAL BELONGS TO.
//
// Satiation on a single GOAL is not enough to produce a rounded day, because
// the goals that crowd everything out are not one goal -- they are one FAMILY
// taking turns among themselves. A crafter alternating BUY_SUPPLIES, CRAFT and
// EARN_GOLD never repeats a single goal twice in a row, so per-goal damping
// never fires, and the day is still nothing but work: that is exactly
// run_m5/p0gate10, three goals in a ring at 47/33/20%.
//
// Damping the FAMILY is what lets the next kind of thing have a turn, which is
// the owner's rule -- "sometimes train, sometimes make money, sometimes sell
// item, sometimes PvM, socialize in between".
enum class GoalFamily : u8 {
    Emergency = 0,   // never damped; nobody gets bored of not dying
    Upkeep,          // banking, replacing gear -- protects what was earned
    Work,            // gather, fish, craft, buy inputs, sell
    Training,        // grow the build, at a trainer or by practice
    Social,          // needs another character to exist
    Wander,          // travel for its own sake, and the bounded no-op
    Count,
};

const char* GoalFamilyName(GoalFamily f);

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
    // Stand where players gather, announce what is spare, and answer anybody
    // who wants it. The one goal that needs another character to exist.
    TradeWithPlayer,
    // The one gold faucet a character can reach on day one.
    Fish,
    // Buy the inputs, then make the thing. The half of the economy a
    // gatherer never needed and a scribe cannot earn a copper without.
    BuySupplies,
    Craft,
    // Do the thing that raises the skill: cast, meditate, bandage.
    // TrainCombat is the fighting half of the same idea.
    // Eat something. The simplest need there is, and it had no goal at all
    // until 2026-08-29 -- NeedFood was scored every tick into a void.
    GetFood,
    PracticeSkill,
    // Buy scrolls and put them in the book. The mage's equivalent of the
    // warrior buying a better sword, and the reason a mage with Magery 50
    // could still cast nothing at all.
    FillSpellbook,
    // Shear a sheep, spin it, weave it, cut it. What a fighter does when it
    // cannot afford bandages -- which is exactly when it most needs them.
    MakeBandages,
    // Go and look at somewhere new. The fallback BEFORE standing still: a bot
    // with nothing to do should be learning the world, because almost every
    // other goal is blocked for want of knowing where something is.
    Explore,
    // Swing a pickaxe at rock. A miner had NO goal at all -- GatherLogs wants
    // an axe and a tree, Fish wants a pole and water, and ore had neither.
    Mine,
    // Tame an animal. Same gap on the tamer's side.
    TameAnimal,
    // Wear the best this class is allowed and strong enough for, and buy a
    // piece for an empty slot. Checked constantly, not once at creation.
    UpgradeGear,
    // Turn ore into ingots at a forge. NOT a menu craft: the shard smelts by
    // double-clicking the ore within 2 tiles of a t_forge (type_ore.scp
    // @dclick), which is why walking the crafting menu could never have done
    // it and why mine -> smelt -> smith stopped dead at the first arrow.
    Smelt,
    // Sheep -> wool -> yarn -> bolt -> cloth. The tailor's gather chain, and
    // the only route to cloth this shard's economy allows a bot to take when
    // no player is selling: cloth, thread and yarn are all WorldProcessed and
    // the vendor policy refuses them.
    //
    // Distinct from MakeBandages, which walks the same five gestures for a
    // different reason and stops the moment it has enough bandages. This one
    // stops when the craft batch has enough CLOTH, and it is Work rather than
    // Upkeep because for a tailor it IS the work.
    MakeCloth,
    // Walk to an animal trainer, buy a riding horse, mount it. Sphere
    // releases an NPC-bought figurine at the buyer's feet in the same
    // packet (CClientEvent.cpp:1309 -> Use_Figurine), so the only step
    // after the purchase is a double-click on the animal.
    BuyMount,
    // A fighter's wool trip: pasture, shear a sheep (1 wool), kill it, carve
    // the corpse with the blade (3 wool, CItemCorpse.cpp:191 _iPrev_id),
    // take the wool, next sheep; home when the pack is a load or the flock
    // is gone. Owner ruling 2026-09-02: warriors, never the tailor.
    HarvestWool,
    IdleBriefly,
    Count,
};

// Which part of a life a goal belongs to. Declared here, after GoalKind.
GoalFamily FamilyOf(GoalKind k);

const char* GoalKindName(GoalKind g);

// HOW LONG A MAGE LEAVES SCROLL-SHOPPING ALONE AFTER NOBODY WOULD SELL ONE.
//
// Spell scrolls are not a shop staple on this shard: the NPC mage sells four
// RANDOM circle 1-4 scrolls (templates/tm_vend.scp:721-724) and blank scrolls,
// the scribe sells named ones it may or may not have, and circles 7-8 are loot
// only. So "nobody within reach stocks a spell this book lacks" is a fact about
// the WORLD, not about this minute -- unlike BUY_SUPPLIES's "nobody was
// selling", which rests 119 s because a reagent shelf restocks. The first rest
// is therefore long, and each further empty errand doubles it, so a mage that
// keeps finding an empty street spends less and less of its day on the errand
// and more on practising, earning and training.
//
// `standDowns` is 1 for the first stand-down. Pure so it can be reasoned about
// and tested without a Client.
i64 ScrollShoppingRestMs(int standDowns);

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

    // HOW LONG THIS KIND OF GOAL MAY RUN, which is not one number.
    //
    // `maxGoalMs` is 5 minutes and that is right for everything a character
    // does within sight of where it is standing. A MARKET TRIP IS NOT THAT:
    // the rendezvous point is one fixed bank for the whole fleet, and a life
    // that lives elsewhere has to walk there. Measured Minoc -> Britain by
    // moongate is 249.8s one way (run_m7/n10_Corran.console.txt:620 ->
    // 17:43:12.365), so out + one listen window + back is ~560s and the
    // default kills the errand mid-journey, every single time.
    //
    // Deliberately narrow: ONE kind, one number, cited. It is a real
    // loosening -- a wedged trip now costs 12 minutes instead of 5 -- bounded
    // only by kMaxTradeTrips and maxAttempts.
    i64 TimeLimitFor(GoalKind k) const;

    // A GOAL THAT RAN AND ACHIEVED NOTHING MUST NOT BE RE-PICKED IMMEDIATELY.
    //
    // Finish() only clears `active`; the very next Select() sees the same top
    // need and starts the same goal again with a fresh clock. Ysolde stood at
    // an open, empty bank box carrying nothing she was allowed to deposit and
    // logged goal=BANK -> goal_completed=BANK progress=0 -> checkpoint every
    // 60 ms for the rest of the session, writing state.json each time
    // (run_m5/pair2). The commitment floor does not help: it governs
    // TRANSITIONS away from a RUNNING goal, and this goal was never running
    // when the decision was made.
    //
    // So a goal can put ITSELF out of the running for a while. The need is
    // still reported and still scored -- it is marked infeasible with the
    // reason, the same way a blocked need is, so "why didn't it bank" stays
    // answerable.
    void Cooldown(GoalKind kind, i64 untilMs);
    bool Cooling(GoalKind kind, i64 nowMs) const;
    // THE REST IS OVER WHEN THE REASON FOR IT IS.
    //
    // A cooldown means "nothing about this will be different in a moment", and
    // Cooldown() deliberately refuses to SHORTEN one so two callers cannot
    // undercut each other. But when the very thing the goal was waiting for
    // arrives, the wait is finished and holding it is just idleness: Aurelius
    // stood down PRACTICE_SKILL for four minutes for want of reagents, bought
    // both of them twenty seconds later, and then went EXPLORING with a full
    // pouch (run_gates/g_Aurelius.console.txt:626,635,717,729).
    //
    // Only for the goal that OWNS the fix, and only on evidence that it landed
    // -- never as a general "try again now".
    void ClearCooldown(GoalKind kind);
    // Which goal the anti-spin backstop just cooled off, or GoalKind::Count.
    // Reading it clears it, so the Runner logs the event exactly once.
    GoalKind TakeSpinDetected();

    // A LIFE IS NOT ONE ERRAND REPEATED.
    //
    // Scoring alone always picks the same winner, so a character does the one
    // thing its top need names until that need is gone -- a scribe buys, makes
    // and sells in a tight ring and never trains, never hunts, never wanders.
    // The owner's rule for this project is that a character should "sometimes
    // train, sometimes make money, sometimes sell, sometimes PvM, socialise in
    // between", and that unsold goods simply going in the bank is a fine
    // outcome rather than a failure.
    //
    // So a goal that keeps winning gets progressively less attractive while
    // it is fresh, letting the runner-up have its turn. This is satiation, not
    // a ban: the damping decays with time and clears the moment something else
    // runs, and it never touches an emergency.
    void NoteRan(GoalKind kind, i64 nowMs);
    // 0.0 = no damping. Exposed so the reason line can print it.
    double Satiation(GoalKind kind, i64 nowMs) const;
    // The same measure for the goal's whole FAMILY -- the one that actually
    // breaks a monotonous day. See GoalFamily.
    double FamilySatiation(GoalKind kind, i64 nowMs) const;
    // HOW MUCH OF THE DAY THIS FAMILY HAS ALREADY HAD.
    //
    // FamilySatiation only counts CONSECUTIVE runs, so it never sees the
    // pattern that actually produced every varied=0 session on record:
    // alternation. Work, Upkeep, Work, Upkeep resets famRepeatRuns_ on every
    // pick and damps nothing, and the histogram comes out as two families
    // splitting the whole day between them (Corwyn 46/39/15, Ysolde 68/29/3).
    //
    // This is the session-wide half of the same idea: a family that has taken
    // more than its share of the picks so far yields, in proportion to how far
    // past the share it is. It is not a rotation and not a quota -- an urgent
    // need still wins, because the damping is a multiplier on score and
    // emergencies are exempt entirely.
    double FamilyShareDamp(GoalKind kind) const;

    const PlannerConfig& Config() const { return cfg_; }

private:
    PlannerConfig cfg_;
    GoalState     goal_;
    i64 cooldownUntilMs_[static_cast<int>(GoalKind::Count)] = {};
    // Satiation bookkeeping. `repeatRuns_` counts how many times this kind
    // has finished IN A ROW; any other kind running resets it to zero.
    i64 lastRanMs_[static_cast<int>(GoalKind::Count)] = {};
    int repeatRuns_[static_cast<int>(GoalKind::Count)] = {};
    // HOW MANY TIMES IN A ROW THIS GOAL HAS "SUCCEEDED" WITHOUT DOING
    // ANYTHING. Three separate goals have now burned a whole session by
    // completing with progress 0 and being re-picked milliseconds later --
    // GET_TOOL 2,058 times, GET_FOOD for entire sessions, EARN_GOLD 13,111
    // times. Each was fixed where it was found, which is the wrong shape of
    // fix for a bug that keeps reappearing in new goals. This counter is the
    // general backstop: a goal that reports success while achieving nothing,
    // over and over, is spinning whatever its reason.
    int noopCompletions_[static_cast<int>(GoalKind::Count)] = {};
    // Set when the backstop fires, so the Runner can say so in the log rather
    // than the character silently going quiet.
    GoalKind spinDetected_ = GoalKind::Count;
    GoalKind lastRanKind_ = GoalKind::Count;
    // The same bookkeeping one level up. A family's streak is what actually
    // breaks a monotonous day, because the crowding-out is done by a family
    // rotating internally, not by one goal repeating.
    i64 famLastRanMs_[static_cast<int>(GoalFamily::Count)] = {};
    int famRepeatRuns_[static_cast<int>(GoalFamily::Count)] = {};
    GoalFamily lastRanFamily_ = GoalFamily::Count;
    // Session totals, for FamilyShareDamp. Never reset mid-session: the
    // question is what the DAY looked like, not the last few minutes.
    int famPicks_[static_cast<int>(GoalFamily::Count)] = {};
    int pickTotal_ = 0;
    // Higher than the per-goal cap: a family has to yield hard enough that a
    // genuinely lower-weighted kind of thing can win. BANK at 240 x 0.72 is
    // 173; TRAIN_COMBAT at 110 x 0.4 is 44, so nothing under a ~60% haircut
    // ever lets a fighter hunt -- which is why no bot has ever fought.
    // RAISED 2026-08-29 to finish R1. At 0.15/0.60 a family that had just run
    // six times still kept 40% of its score, and with TRAIN_AT_NPC weighted
    // 150 against BANK's 60 that was more than enough to win again: Maribel
    // spent 15 of 19 picks in Training, Halric 5 of 6. The damping was real
    // and simply too gentle to change the outcome.
    //
    // 0.20 per repeat to a 0.85 ceiling means a family on its seventh
    // consecutive turn is down to 15% of score, which genuinely hands the turn
    // over. It decays across kSatiationMs either way, so a character comes
    // back to training a few minutes later rather than abandoning it -- which
    // is the difference between a rounded day and a distracted one.
    static constexpr double kFamilySatiationPerRepeat = 0.20;
    static constexpr double kFamilySatiationMax = 0.85;
    // How long a finished goal stays "fresh". Beyond this the damping is
    // gone entirely -- a character that banked ten minutes ago is perfectly
    // happy to bank again.
    static constexpr i64 kSatiationMs = 3 * 60 * 1000;
    // Per consecutive repeat, and the ceiling. 0.12/0.45 lets a goal win
    // three or four times running before it reliably yields -- enough for a
    // real errand (buy, craft, sell) to finish, not so much that one need
    // owns the whole session.
    static constexpr double kSatiationPerRepeat = 0.12;
    static constexpr double kSatiationMax = 0.45;
    // The share of the session's picks a single family may hold before it
    // starts yielding, and how hard it yields once past it. R1's bar is
    // "none above 50%", so the pressure has to start well below that to
    // leave room for an errand that genuinely takes several turns; 0.30 is
    // a little under a fair quarter-share of four families. The ceiling is
    // deliberately below FamilySatiation's 0.85: this is a nudge across a
    // whole session, not a hard stop, and it must never starve a family
    // that is the only useful thing left to do.
    static constexpr double kFamilyFairShare = 0.30;
    static constexpr double kFamilyShareDampMax = 0.60;
    // Below this many picks the shares are noise -- one pick out of two is
    // 50% and means nothing.
    static constexpr int kMinPicksForShare = 8;
};

// THE ARITHMETIC BEHIND THE END-OF-SESSION GOAL HISTOGRAM, pulled out as a
// pure function so it is reachable by ctest instead of living only inside a
// switch inside Runner::Tick. `families`/`topFrac` are what R1's exit proof
// reads: at least four goal families, none above half the picks (S2.8).
struct GoalHistogram {
    i32    families = 0;
    i32    picks = 0;
    i32    top = 0;
    double topFrac = 0.0;
    bool   varied = false;
};
GoalHistogram SummariseGoalPicks(const i32 picks[static_cast<int>(GoalKind::Count)]);

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
    // "goal_changed=X from=X" -- Planner::Exhausted cleared the goal and the
    // same kind was re-picked while it was still active (Runner::Tick, the
    // goal_changed log site). Greppable before this counter existed; never
    // totalled. S2.8: printed in the histogram line as self_superseded=%d.
    i32 selfSupersessions = 0;
    i32 goldStart = 0, goldEnd = 0;
    i32 skillTenthsStart = 0, skillTenthsEnd = 0;
    i32 logsGathered = 0;
    // Fights WON. Counted because nothing counted them: the danger memory
    // recorded fleeing and dying and never winning, so a session that killed
    // twenty things looked identical to one that killed none.
    i32 kills = 0;
    i32 deaths = 0;
    i32 placesLearned = 0;
    i32 suppliersLearned = 0;
    // HOW THE DAY WAS ACTUALLY SPENT, per goal family.
    //
    // R1 of the 2026-08-29 re-sequence asks for a rounded life and defines
    // its exit proof as a histogram: at least four goal families, none
    // above half the picks. That has to be printed as ONE line at session
    // end so the check is a grep rather than an argument -- reading it out
    // of a 50,000-line console log by eye is how "it looks varied enough"
    // gets said about a session that ran three goals in a ring.
    i32 goalPicks[static_cast<int>(GoalKind::Count)] = {};
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

    // WHAT IS IN THE BANK. Recorded whenever the box is open and kept, so the
    // character KNOWS its own stock while standing somewhere else.
    //
    // Without this a bot banks everything it gathers and the goods leave the
    // economy permanently: it cannot sell what it cannot see, and it has no
    // reason to walk to a bank it does not know holds anything. Five hundred
    // BANK goals in one fleet session and not one sale.
    //
    // This is not omniscience -- it is a character remembering its own box,
    // which is the most ordinary thing a player does.
    std::vector<market::Stock> bank;
    i64 bankSeenMs = 0;

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
