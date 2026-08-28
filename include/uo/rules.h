#pragma once

// ---------------------------------------------------------------------------
// Revolution rules profile, as enforceable constraints (M3.6).
//
// WHY THIS EXISTS, AND WHY IT LIVES IN THE CLIENT
//
// The reconstruction's server is MORE PERMISSIVE than RevolutionUO was:
//
//     total skill      runtime 1000.0   Revolution 700.0
//     Resisting Spells runtime enabled  Revolution officially inactive
//     active skills    runtime 58       Revolution 38
//     reagents         runtime not required (ReagentsRequired=0)
//
// So a build the server accepts is NOT therefore a Revolution build, and
// authenticity cannot be delegated to Source-X. It has to be enforced where
// builds are decided -- here. Server-side restoration is a separate,
// deliberate act and is tracked as its own debt.
//
// Nothing in this file changes the server. It constrains what our characters
// aim for.
//
// Profile: revolution_2009_2010. See docs/REVOLUTION_RULESET_PROFILE.md.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::rules {

// Sphere skill ids for the nine skills RevolutionUO's own gameplay guide lists
// as inactive: "Herding, Remove Trap, Resisting Spells, Enticement,
// Peacemaking, Provocation, Sprit Speak, Forensic Evaluation, Taste
// Identification". Verified against runtime/scripts/skills/*.scp.
enum SkillId : int {
    kPeacemaking      = 9,
    kEnticement       = 15,
    kForensics        = 19,
    kHerding          = 20,
    kProvocation      = 22,
    kMagicResistance  = 26,   // "Resisting Spells"
    kSpiritSpeak      = 32,
    kTasteId          = 36,
    kRemoveTrap       = 48,
    kMagery           = 25,
    kPoisoning        = 30,

    // Production skills (M3.7). Ids read off the runtime's own file names --
    // runtime/scripts/skills/skill<N>_<name>.scp -- not guessed from a table.
    kAlchemy          = 0,
    kArmsLore         = 4,
    kBlacksmithing    = 7,
    kBowcraft         = 8,
    kCarpentry        = 11,
    kCartography      = 12,
    kCooking          = 13,
    kEvaluatingIntel  = 16,
    kFishing          = 18,
    kInscription      = 23,
    kLockpicking      = 24,
    kTailoring        = 34,
    kTaming           = 35,
    kTinkering        = 37,
    kLumberjacking    = 44,
    kMining           = 45,
    kMeditation       = 46,

    // Combat and support skills the M4 Slice 1 build plans against. Same
    // sourcing rule as the production ids above: read off the runtime's own
    // file names, runtime/scripts/skills/skill<N>_<name>.scp.
    kAnatomy          = 1,
    kHealing          = 17,
    kTactics          = 27,
    kSwordsmanship    = 40,

    // M5 archetypes. Same sourcing rule: read off the runtime's own file
    // names, runtime/scripts/skills/skill<N>_<name>.scp.
    kAnimalLore       = 2,
    kVeterinary       = 39,
};

// The shard's own name for a skill id, or "skill <n>" when we do not have a
// name for it. There used to be two of these -- one in the needs layer, one in
// the runner -- and both stopped at the five M4 skills, so every M5 log line
// about a mage read "skill 50.0 + skill 50.0". One table, everywhere.
const char* SkillName(int skillId);

struct Profile {
    std::string name = "revolution_2009_2010";

    // Budget. 700.0 total is the M3.5 finding: eleven forum builds across
    // 2008-2010, every one exactly 700, plus the official `.skilldusur`
    // command documenting a 670.0 skill-total floor.
    i32 totalSkillCapTenths = 7000;
    i32 perSkillCapTenths   = 1000;

    // STAT budget. 225 total is the M3.8 finding, and it arrived the same way
    // the skill cap did: nobody states it, so ten player builds state it for
    // them. Two unrelated threads, two classes, every build exactly 225 --
    // 90/100/35, 100/100/25, 90/90/45, 98/97/30, 85/100/40, 50/100/75,
    // 25/100/100 -- none above 100 in one stat and none below 25.
    //
    // DERIVED, not quoted. The profile previously read "build threads discuss
    // skills only and never post stats", which was false and is why the
    // question stayed open: the search had concluded the evidence did not
    // exist rather than that it had not been found.
    //
    // THE RUNTIME ALLOWS 300. Same shape as the taming divergence -- the server
    // is more permissive than Revolution, so this is enforced bot-side and the
    // gap is recorded as SERVER_AUTHENTICITY_DEBT rather than patched.
    i32 totalStatCap = 225;
    i32 perStatCap   = 100;
    // The lowest value seen in any published build. Not proven to be a floor,
    // recorded because every one of the ten respects it.
    i32 observedStatFloor = 25;

    // The official skill-lowering floor. Recorded because the 30-point gap
    // between it and the cap is unexplained and should not be quietly closed.
    i32 skillLowerFloorTenths = 6700;

    // NPC teaching, proven live in M3.5 (Arms Lore 2.6 -> 21.7 for 191 gold).
    i32 teachingPercentOfTrainer = 30;
    i32 teachingAbsoluteMaxTenths = 420;
    i32 teachingGoldPerTenth = 1;
    // The trainer does not give change. Measured: 250 handed over against a
    // 191 quote, 250 taken.
    bool teacherKeepsChange = true;

    // Poisoned weapons. See the note on the four questions below.
    i32 poisonedWeaponMaxMageryTenths = 400;

    // Stat cap is deliberately ABSENT. No archive source states one, and a
    // guess would be worse than the gap.
};

const Profile& Revolution();

// The nine skills Revolution did not run.
const std::vector<int>& InactiveSkills();
bool SkillActive(int skillId);

// --- build validation -------------------------------------------------------

struct BuildSkill {
    int skillId = 0;
    i32 tenths = 0;
};

enum class Violation : u8 {
    None = 0,
    OverTotal,
    OverPerSkill,
    InactiveSkill,
    Negative,
    Count,
};

const char* ViolationName(Violation v);

struct BuildCheck {
    bool      ok = false;
    Violation violation = Violation::None;
    int       skillId = 0;      // the offending skill, when relevant
    i32       totalTenths = 0;
    usize     skillCount = 0;
};

// "7x" is 700 POINTS, not seven skills: a documented 2008 build spends its 700
// across nine. The skill COUNT is never checked here, only the sum.
BuildCheck ValidateBuild(const Profile& p, const std::vector<BuildSkill>& build);

// --- poisoning --------------------------------------------------------------
//
// M3.5's Build Compendium v2 stated this as "a poisoner may not exceed Magery
// 40.0", which was WRONG and would have outlawed the best-attested build
// family on the shard -- Magery 100 / Poisoning 100 warlocks appear repeatedly
// in the forum record.
//
// The official guide's actual wording (revolutionuo.net/oyun_rehberi):
//
//   "Warriorların silah sürmesi için bu skill yeterlidir. Poison şişesinin
//    seviyesine göre silah sürülebilmektedir. Büyücü yeteneği 40.0 ın
//    üstündeki savaşçılar zehirli silahı KULLANAMAZLAR."
//
//   "This skill is sufficient for warriors to apply poison to weapons.
//    Weapons can be poisoned according to the poison bottle's level. Warriors
//    with Magery above 40.0 CANNOT USE poisoned weapons."
//
// and, separately: "Magery yeteneği ile yaptığınız Poison büyüsünün gücünü
// arttırabilirsiniz" -- Poisoning increases the power of the Poison SPELL.
//
// So the restriction is on WIELDING a poisoned weapon, and nothing else. Four
// separate questions, four separate answers.

bool CanTrainPoisoning(const Profile& p, i32 mageryTenths);
bool CanCastPoisonSpell(const Profile& p, i32 mageryTenths);
bool CanApplyPoisonToWeapon(const Profile& p, i32 poisoningTenths);
bool CanUsePoisonedWeapon(const Profile& p, i32 mageryTenths);

// --- teaching ---------------------------------------------------------------

// Highest value this trainer will teach: 30% of its own skill, capped at 42.0.
i32 TeachingMaxTenths(const Profile& p, i32 trainerSkillTenths);

// What the trainer will quote to go from `fromTenths` to its ceiling. Returns
// 0 when there is nothing to buy.
i32 TeachingQuote(const Profile& p, i32 fromTenths, i32 trainerSkillTenths);

// Whether paying `gold` for that quote is the right move -- i.e. exactly the
// quote, because the trainer keeps the change.
bool PaymentIsExact(const Profile& p, i32 quote, i32 gold);

// --- should this character pay a teacher? -----------------------------------
//
// Teaching is a CHOICE, not a step. It buys the cheapest, dullest part of a
// skill -- the part where gains are fastest anyway -- so the question is
// whether the gold is better spent here than on reagents, tools or stock.

struct TrainerSituation {
    i32  currentTenths = 0;
    i32  targetTenths = 0;        // what the build wants eventually
    i32  trainerSkillTenths = 0;  // 0 = no trainer found
    i32  gold = 0;
    i32  goldReserve = 0;         // untouchable: bandages, reagents, a rune
    i32  travelSeconds = 0;       // how far the trainer is
    bool skillIsActive = true;    // a Revolution-inactive skill is never trained
    // What the build budget can still afford, in tenths. Teaching that would
    // push the character past 700 total is not a bargain.
    i32  buildHeadroomTenths = 10000;
};

enum class TrainerVerdict : u8 {
    Pay = 0,
    NoTrainer,
    NothingToTeach,   // the student is already at or past the trainer's ceiling
    CannotAfford,
    SkillInactive,
    ExceedsBuildBudget,
    Count,
};

const char* TrainerVerdictName(TrainerVerdict v);

struct TrainerDecision {
    TrainerVerdict verdict = TrainerVerdict::NoTrainer;
    i32 buyToTenths = 0;   // the value the trainer would raise us to
    i32 pointsBought = 0;
    i32 quote = 0;         // gold to hand over -- EXACTLY this, no more
};

TrainerDecision DecideTraining(const Profile& p, const TrainerSituation& s);

} // namespace uo::rules
