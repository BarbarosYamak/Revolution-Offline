#pragma once

// ---------------------------------------------------------------------------
// M4 step 1 -- the persistent character record.
//
// M4 is "one character that lives, logs out, and picks its life back up next
// session" (docs/M4_LIFECYCLE_PLAN.md). Everything a character is FOR, and
// everything it has LEARNED by playing, has to survive the socket closing.
// This file is that record and nothing else: no packets, no sockets, no
// decisions. The state assessment that reads it is step 2.
//
// WHAT IS NOT IN HERE, deliberately: skills, stats, gold and items. Those are
// the SERVER'S facts. A bot that saved its own skill values and trusted them
// next session would be keeping a second set of books, and the first time the
// two disagreed the bot's would be the wrong one. The record holds TARGETS
// (what the build is aiming for) and KNOWLEDGE (what was observed); the
// present state is always re-read from the shard after login.
//
// TWO CLOCKS. The live objects (PersonalKnowledge, supply::Registry) stamp
// everything with Client::NowMs(), a steady_clock reading whose epoch is
// arbitrary and resets with the process -- a timestamp from last session is
// meaningless in this one. So the record stores WALL-clock milliseconds, and
// every crossing between the two goes through Clock. Get this wrong and a
// vendor seen eight hours ago reads as seen a moment ago, or a danger note
// that expired overnight comes back live.
//
// The file format is line-oriented TSV, versioned, and closed by an `end`
// line. It is meant to be read by a person diagnosing a character, and a
// truncated write (the process killed mid-save) must be detectable rather
// than loaded as a shorter life.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/rules.h"
#include "uo/supplier.h"
#include "travel/PersonalKnowledge.h"

#include <string>
#include <string_view>
#include <vector>

namespace uo::life {

inline constexpr int kFormatVersion = 1;
inline constexpr usize kMaxScriptMemory = 256 * 1024;

// --- aspiration --------------------------------------------------------------
//
// Only aspirations that have actually been built exist here. The plan is
// explicit: one archetype survives several consecutive sessions before a
// second is added, so listing the other fifteen now would be promising
// behaviour that does not exist.
enum class Aspiration : u8 {
    Unknown = 0,
    LumberjackSwordsman,
    Count,
};

const char* AspirationName(Aspiration a);
Aspiration  AspirationFromName(std::string_view name);  // Unknown if unknown

// --- current objective -------------------------------------------------------
//
// One per branch of the plan's `assess state` ladder, in the same order. It is
// persisted so a session RESUMES what the last one was doing rather than
// restarting from the top: a character that logged out halfway to its corpse
// should log back in still going for its corpse.
enum class ObjectiveKind : u8 {
    None = 0,
    RecoverCorpse,   // dead: healer, resurrect, travel to corpse, recover gear
    Survive,         // under threat
    Eat,             // hungry
    Rearm,           // no weapon or tool
    Unload,          // overloaded: bank or sell
    Earn,            // low gold: work the profession
    Train,           // skill below target, trained through work
    Pursue,          // everything fine: the current goal
    Count,
};

const char*   ObjectiveKindName(ObjectiveKind k);
ObjectiveKind ObjectiveKindFromName(std::string_view name);  // None if unknown

struct Objective {
    ObjectiveKind kind = ObjectiveKind::None;
    std::string   target;          // free text: an itemdef, a place id, a skill
    i64           sinceWallMs = 0;
    i32           attempts = 0;
};

// --- goals -------------------------------------------------------------------

// Something the character wants to own. `required` separates "cannot do its
// job without it" (an axe) from "wants it" (better armour).
struct EquipmentGoal {
    std::string itemdef;
    i32         priority = 0;      // lower is more urgent
    bool        required = false;
};

// When to bank, when to sell, what never to spend. Gold amounts only; weight
// is read live from the status bar against the character's own STR.
struct EconomyGoals {
    i32 goldReserve = 0;             // never spent: bandages, food, a new axe
    i32 bankAboveGold = 0;           // carried gold above this goes to the bank
    i32 unloadAtWeightPercent = 80;  // of the live max weight
};

// --- history the live objects do not keep -----------------------------------

// PersonalKnowledge holds ONE death -- the last. A character that keeps dying
// in the same forest needs to know it has died there before, so the record
// keeps a short history. Bounded: this is memory, not a log.
inline constexpr usize kMaxDeaths = 16;

struct DeathEntry {
    i32         x = 0, y = 0;
    i8          z = 0;
    std::string regionId;
    u32         corpseSerial = 0;
    i32         recoveryAttempts = 0;
    i64         wallMs = 0;
};

// Which destinations this character has actually reached, and which it tried
// and failed. "Learned route knowledge" from the plan: interior routing is
// still unbuilt, so some destinations fail in a bounded way, and a character
// that remembers that stops walking at them every session.
inline constexpr usize kMaxRoutes = 64;

struct RouteMemory {
    std::string destination;       // the travel label, e.g. "service:banker"
    i32         successes = 0;
    i32         failures = 0;
    i32         consecutiveFailures = 0;
    i64         lastWallMs = 0;
    std::string lastFailure;
};

// Where the last session ended. "Log out somewhere safe" is a rule, and this
// is how the next session checks the previous one kept it.
struct LogoutPoint {
    bool        valid = false;
    i32         x = 0, y = 0;
    i8          z = 0;
    std::string placeId;
    bool        safe = false;      // the logging-out session's own judgement
    i64         wallMs = 0;
};

// --- the record --------------------------------------------------------------

struct CharacterRecord {
    // identity
    std::string name;
    std::string account;
    Aspiration  aspiration = Aspiration::Unknown;
    // M4.5: the archetype id from data/revolution_archetypes.tsv. Empty for
    // records made before the table existed (they keep `aspiration`).
    std::string archetype;
    // M4.6: what the JS bot has learned (danger map, market, known crafters,
    // open orders, ledger), as the script's own JSON. Opaque to C++: stored,
    // capped at kMaxScriptMemory, and handed back next session.
    std::string scriptMemory;
    i64         createdWallMs = 0;
    i32         sessions = 0;
    i64         lastLoginWallMs = 0;
    i64         lastSavedWallMs = 0;

    // targets -- what a FINISHED character looks like
    std::vector<rules::BuildSkill> targetBuild;
    i32 targetStr = 0, targetDex = 0, targetInt = 0;
    std::vector<EquipmentGoal> equipment;
    EconomyGoals economy;

    // where the life is up to
    Objective   objective;
    LogoutPoint lastLogout;
    std::vector<DeathEntry>  deaths;
    std::vector<RouteMemory> routes;

    // knowledge, snapshotted from the live objects in WALL-clock time
    std::vector<supply::Supplier>          suppliers;
    std::vector<travel::VisitRecord>       visits;
    std::vector<travel::ServiceSighting>   sightings;
    std::vector<travel::KnownRune>         runes;
    std::vector<travel::DangerNote>        dangers;
    travel::DeathRecord                    lastDeath;
    bool        homeSet = false;
    i32         homeX = 0, homeY = 0;
    i8          homeZ = 0;
    std::string homePlaceId;
};

// --- the first archetype -----------------------------------------------------
//
// The frontier Lumberjack / Swordsman of M4_LIFECYCLE_PLAN.md section 1.
CharacterRecord NewLumberjackSwordsman(const char* name, const char* account,
                                       i64 wallNowMs);

// --- every archetype (M4.5) ---------------------------------------------------
//
// data/revolution_archetypes.tsv is generated from scripts/js/lib/archetypes.js
// (the smoke test fails if the two drift). Each row: id, kind, style or trade,
// compendium ref, evidence class, skills as "id:tenths,...", STR, DEX, INT,
// home town. The record stores the archetype id; the build targets come from
// the row, so one table decides every character type.
struct ArchetypeRow {
    std::string id;
    std::string kind;        // fighter | crafter | gatherer
    std::string style;       // melee/ranged/mage/... or the trade
    std::string ref;         // Build Compendium entry
    std::string evidence;    // HISTORICAL_EXACT ... UNSOURCED
    std::vector<rules::BuildSkill> skills;
    i32 str = 0, dex = 0, intel = 0;
    std::string home;
};

struct ArchetypeTable {
    std::vector<ArchetypeRow> rows;
    const ArchetypeRow* Find(std::string_view id) const;
};

// Loads and VALIDATES every row against the Revolution profile. A row that
// breaks the 700/225 caps or names an inactive skill fails the whole load:
// a bad template would steer every character built from it.
bool LoadArchetypes(const std::string& path, ArchetypeTable* out, std::string* err);
bool ParseArchetypes(std::string_view text, ArchetypeTable* out, std::string* err);

CharacterRecord NewRecordFromArchetype(const ArchetypeRow& row, const char* name,
                                       const char* account, i64 wallNowMs);

// --- validation --------------------------------------------------------------
//
// A record whose TARGETS break the Revolution profile is refused at load, not
// quietly clamped: a target build over 700 would steer every training decision
// the character ever makes.
struct RecordCheck {
    bool        ok = false;
    std::string why;
};

RecordCheck ValidateRecord(const CharacterRecord& r,
                           const rules::Profile& p = rules::Revolution());

// --- clocks ------------------------------------------------------------------
//
// One pair of readings taken at the same instant. 0 means "never" on both
// sides and is preserved as 0, so an unset timestamp never turns into a date.
struct Clock {
    i64 steadyMs = 0;
    i64 wallMs = 0;

    i64 ToWall(i64 steady) const   { return steady ? steady - steadyMs + wallMs : 0; }
    i64 ToSteady(i64 wall) const   { return wall ? wall - wallMs + steadyMs : 0; }
};

Clock NowClock(i64 steadyNowMs);   // pairs the caller's steady reading with the wall clock

// --- live objects <-> record -------------------------------------------------

// Copy what the character has learned into the record. Death history is
// merged, not replaced: a new last-death is appended, an old one updated.
void Capture(CharacterRecord* r, const travel::PersonalKnowledge& k,
             const supply::Registry& reg, const Clock& c);

// Load the record's knowledge into freshly cleared live objects. Danger notes
// that expired while the character was logged out are dropped here.
void Apply(const CharacterRecord& r, travel::PersonalKnowledge* k,
           supply::Registry* reg, const Clock& c);

// Remember how a trip to `destination` went.
void NoteRoute(CharacterRecord* r, const char* destination, bool ok,
               const char* why, i64 wallMs);
const RouteMemory* FindRoute(const CharacterRecord& r, std::string_view destination);

// --- text format -------------------------------------------------------------

std::string Serialize(const CharacterRecord& r);

struct ParseResult {
    bool        ok = false;
    int         line = 0;          // 1-based line of the first error
    std::string error;
    int         unknownLines = 0;  // record kinds this build does not know
};

ParseResult Parse(std::string_view text, CharacterRecord* out);

// --- files -------------------------------------------------------------------

enum class LoadStatus : u8 {
    Ok = 0,
    NotFound,    // no life yet -- the caller may begin one
    Corrupt,     // a life exists and could not be read -- NEVER overwrite it
    Invalid,     // it parsed, but its targets break the profile
    Count,
};

const char* LoadStatusName(LoadStatus s);

struct LoadResult {
    LoadStatus  status = LoadStatus::NotFound;
    std::string detail;
};

LoadResult LoadFile(const std::string& path, CharacterRecord* out);

// Atomic: written to `<path>.tmp`, flushed, then renamed over `path`. A crash
// mid-save leaves the previous life intact.
bool SaveFile(const std::string& path, const CharacterRecord& r, std::string* err);

// A filesystem-safe file name for a character: letters and digits kept,
// everything else '_'. "Ahmet the Woodsman" -> "Ahmet_the_Woodsman.life".
std::string RecordFileName(std::string_view characterName);

}  // namespace uo::life
