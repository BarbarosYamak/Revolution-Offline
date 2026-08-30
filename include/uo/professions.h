#pragma once

// ---------------------------------------------------------------------------
// M5.1 -- professions as DATA, not as branches.
//
// M4 proved one life with a hardcoded `FrontierLumberjackSwordsman()`. M5 needs
// several, and the brief is explicit about the failure mode to avoid:
//
//     do not scatter `if (miner) ... if (mage) ... if (tamer)` through
//     unrelated systems.
//
// So a profession is a RECORD describing what a character is trying to become,
// and every system reads that record rather than testing an archetype enum.
// Adding an archetype must not require editing the need model, the planner or
// the runner.
//
// THIS IS A GOAL PROFILE, NOT A TEMPLATE TO INSTANTIATE.
//
// The distinction is the whole project rule. Nothing here is granted: the
// starting pair is what the character ASKS the server for at creation, and
// every other number is a target it must earn. A profession that listed
// finished skills and handed them over would be exactly the uo-offline
// `Skills[x].Base = v` mistake this project rejects.
// ---------------------------------------------------------------------------

#include "uo/rules.h"
#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::prof {

// --- Revolution character creation ------------------------------------------
//
// THE RULE, as the project owner states it: a new character picks exactly TWO
// skills at 50.0 each, everything else 0.0, and splits about 50 points across
// STR/DEX/INT.
//
// MEASURED AGAINST THE RUNTIME, both fit inside what Source-X allows, so
// neither is blocked and neither needs a server change:
//
//   CChar::InitPlayer clamps creation to 50.0 per skill and 100.0 total
//     -> 50 + 50 = 100.0 is EXACTLY the ceiling. The rule is expressible.
//   CChar::InitPlayer clamps creation to 60 per stat and 80 total
//     -> and a new character spends ALL EIGHTY.
//
// CORRECTED 2026-08-29. This said 50 for a long time, attributed to the owner,
// and the owner has since corrected it to 80 -- the full ceiling. Every
// profession's split was scaled x1.6 to match; all seventeen were multiples of
// five, so each scales exactly and no stat exceeds the per-stat cap of 60.
//
// Worth recording because it went the wrong way once already: M4's original
// character asked for 80 (40/35/5), which was LEGAL AND RIGHT, and an earlier
// pass "corrected" it down to 50 in the belief that 50 was the Revolution
// rule. Every bot created between then and now was born a third weaker than a
// real player. Restored here.
inline constexpr i32 kRevolutionStartSkillEach   = 500;  // 50.0, in tenths
inline constexpr i32 kRevolutionStartSkillCount  = 2;
inline constexpr i32 kRevolutionStartStatTotal   = 80;

// Server-side creation ceilings, for comparison only. Never use these as
// targets -- they are what the engine tolerates, not what Revolution did.
inline constexpr i32 kServerCreateSkillEachMax = 500;   // 50.0
inline constexpr i32 kServerCreateSkillSumMax  = 1000;  // 100.0
inline constexpr i32 kServerCreateStatEachMax  = 60;
inline constexpr i32 kServerCreateStatSumMax   = 80;

// --- what a profession needs from the world ---------------------------------

// A tool the life cannot proceed without. `graphics` are the item ids as this
// shard's own itemdefs carry them -- never guessed from generic UO.
struct ToolNeed {
    std::string      name;        // "hatchet", "pickaxe"
    std::vector<u16> graphics;
    bool             mustBeWielded = false;  // Sphere skills that need SRC.WEAPON
};

struct ConsumableNeed {
    std::string      name;        // "bandage", "reagent", "food"
    std::vector<u16> graphics;
    i32              low = 0;     // below this, it becomes a need
    i32              restockTo = 0;
};

// What this life does for money, in preference order. A profession that can
// only gather is not the same as one that can gather and craft.
enum class Income : u8 {
    Gather = 0,     // chop, mine, fish -- take from the world
    Process,        // smelt, spin -- transform what was gathered
    Craft,          // make a finished good
    Hunt,           // loot from creatures
    Count,
};

const char* IncomeName(Income i);

// --- the profession record ---------------------------------------------------

// A skill this profession wants, and why. `priority` orders training when
// several are short; `viaTrainer` marks the ones worth paying an NPC for
// rather than grinding from zero; `role` says what the skill is FOR.
//
// What a skill is FOR in a build. uo-offline's BotSkillTemplate separates
// these three and gives each one a reason for its number; a flat priority
// integer orders training and explains nothing.
//
//   Primary   defines the character -- and, on this shard, its paperdoll
//             title, which is the only thing another player actually sees.
//   Secondary the rest of the working build, trained alongside the primary.
//   Utility   deliberately capped. A dexxer's Magery is for Recall and Cure
//             and must never become a second profession; letting it drift to
//             GM would also break Revolution's travel-magic rarity, where
//             Recall opens at 26+ and Gate at 90+.
enum class SkillRole : u8 { Primary = 0, Secondary, Utility };

const char* SkillRoleName(SkillRole r);

struct SkillTargetSpec {
    int       skillId = -1;
    i32       tenths = 0;
    int       priority = 0;      // higher trains first
    bool      viaTrainer = false;
    SkillRole role = SkillRole::Secondary;
};

struct Profession {
    std::string id;             // "miner_smith", stable, used as a key
    std::string label;          // human-readable

    // CREATION. Exactly two, 50.0 each -- validated, not assumed.
    // -1, not 0, is "unset": skill id 0 is ALCHEMY, a real skill, and using 0
    // as a sentinel silently rejected the alchemist archetype.
    int startSkillA = -1;
    int startSkillB = -1;
    // THE THIRD CREATION SLOT, which this shard sets to exactly 0.0.
    //
    // Source-X randomises EVERY skill at creation to a value in [0, 19.9)
    // (CChar.cpp:1768-1772 with sphere.ini MaxBaseSkill=200) and then writes
    // the three requested skills over the top (CChar.cpp:1803-1808). So the
    // ONLY skill a new character can hold at literal 0.0 is whichever one
    // occupies this slot -- which is why every bot so far has Remove Trap at
    // 0.0 and nothing else.
    //
    // Naming a skill the build actually intends to learn turns that inert
    // slot into an honest starting point: the character begins knowing
    // nothing of it and must buy or grind every tenth. It stays 0 points, so
    // Revolution's rule -- exactly two skills at 50.0 -- is untouched.
    //
    // CONSTRAINT: Source-X applies [NEWBIE <skill>] for every requested slot
    // WITHOUT checking its value (CChar.cpp:2116-2144), so a skill named here
    // must grant nothing, or the character is handed a kit it did not earn.
    //
    // "Grants nothing" is about the section's CONTENTS, not its existence --
    // this used to say Meditation was the only candidate, which was wrong and
    // needlessly narrow. Reading
    // runtime/scripts/templates_special/sp_tm_newbie.scp, two kinds qualify:
    //
    //   no section at all -- Meditation, Stealth, Remove Trap, Focus,
    //     Spellweaving, Mysticism, Imbuing (the lookup simply fails)
    //   a section whose body is literally "//empty" -- Evaluating Intelligence
    //     (:376), Tinkering (:545), Forensics, Magic Resistance, Necromancy,
    //     Taming, Taste ID
    //
    // Either way the pack is untouched. Anything else pays a real kit: naming
    // Inscription here would hand over two blank scrolls and a book (:417).
    int startZeroSkill = -1;
    // The stat split this life wants at creation. Must total
    // kRevolutionStartStatTotal.
    i32 startStr = 0, startDex = 0, startInt = 0;

    // THE LONG GAME. Targets the character must earn, inside the 700 budget.
    std::vector<SkillTargetSpec> targets;
    i32 unresolvedTenths = 0;   // budget deliberately left unspent

    i32 targetStr = 0, targetDex = 0, targetInt = 0;

    std::vector<Income>         income;
    std::vector<ToolNeed>       tools;
    std::vector<ConsumableNeed> consumables;

    // Resource this life gathers, as the world model names it ("logs", "ore").
    // Empty for a profession that buys its inputs instead.
    std::string gathers;

    // What it makes, as itemdef defnames. Drives the production-chain link and
    // the M7 producer/consumer split.
    std::vector<std::string> produces;
    // What it must obtain from someone else. THIS is what creates economic
    // interdependence rather than every bot doing every stage.
    std::vector<std::string> consumes;

    // How readily this life picks a fight it did not start. A miner deep in a
    // cave and a swordsman on the road should not behave alike.
    double riskTolerance = 0.5;   // 0 = flee everything, 1 = stand and fight

    // Gold this life keeps back rather than spending -- the reserve that pays
    // for a replacement tool after a death.
    // SAVINGS, not pocket money. This is what a life wants to have TO ITS
    // NAME before it counts itself comfortable -- a scribe wants 5000, a
    // lumberjack-swordsman 10000. It is NOT how much to walk around with:
    // death here is full loot, and the owner's rule is "nobody carry gold on
    // them unless they need to buy something". What is carried is capped at
    // kMaxGoldCarried below, and the rest belongs in the box; a purchase
    // withdraws what it needs when it needs it.
    i32 goldReserve = 0;

    // WHERE THIS LIFE LIVES, best first, as the atlas names the region.
    //
    // Without a home every character asks the travel layer for the NEAREST
    // provider of a service, so twenty bots spawning on the same tile converge
    // on the same shop -- seven of them queued at the Yew banker in the first
    // fleet run. A shard where every trade is in one place does not read as
    // populated, and it is not how UO's map works: miners live at Minoc
    // because the mountain is there, lumberjacks at Yew because the forest is.
    //
    // uo-offline's BotHomeCities has the same shape (audit A.4) -- roll a home,
    // bias destination picks toward it, and REGULARS emerge. This ties the
    // roll to the profession, because the geography is not arbitrary.
    std::vector<std::string> homeCities;
};

// --- the catalogue -----------------------------------------------------------

// Every profession this build knows. Data only; adding one is a table entry.
const std::vector<Profession>& All();
const Profession* Find(const char* id);

enum class ProfViolation : u8 {
    None = 0,
    NotTwoStartSkills,      // Revolution starts exactly two
    StartSkillNot50,
    StartStatsWrongTotal,   // must be kRevolutionStartStatTotal
    StartStatOverServerMax,
    SkillBudgetExceeded,    // targets + unresolved > 700.0
    PerSkillCap,
    InactiveSkill,
    StatTotalExceeded,      // targets > 225
    StatPerCapExceeded,
    NoIncome,
    NotExactlyOnePrimary,   // the primary is what the paperdoll title reads
    UtilityAtFullCap,       // a "utility" skill at 100.0 is a second job
    Count,
};

const char* ProfViolationName(ProfViolation v);

struct ProfCheck {
    bool          ok = false;
    ProfViolation violation = ProfViolation::None;
    int           skillId = 0;
    i32           startSkillSum = 0;
    i32           startStatSum = 0;
    i32           targetSkillSum = 0;   // targets + unresolved
    i32           targetStatSum = 0;
};

// Validates a profession against BOTH rulesets: Revolution's creation rule and
// the 700/225 finished-build caps. A profession that fails is a bug in the
// table, not a runtime condition -- every entry is checked by the unit tests.
ProfCheck Validate(const rules::Profile& p, const Profession& prof);

// ---------------------------------------------------------------------------
// How far along a character is. OBSERVED, never assigned.
//
// uo-offline rolls a tier at creation and grants the matching skills. Here it
// is computed from the skill total the SERVER reports, so a bot cannot be
// spawned a Grandmaster -- it can only become one. The bands are ours; the
// axis (class = the set of skills, tier = the level they sit at) is the idea
// worth keeping, because without it there is no way to say "a Journeyman
// smith" as distinct from "a smith who started yesterday".
enum class Tier : u8 {
    Novice = 0, Apprentice, Journeyman, Adept, Expert, Master, Grandmaster,
};

const char* TierName(Tier t);

// From the character's total skill, in tenths, as the server reports it.
Tier TierFromSkillSum(i32 sumTenths, i32 capTenths);

// The share of a healthy POPULATION expected at each tier, in percent. This is
// a yardstick the fleet can measure itself against -- most characters are
// mid-tier and Grandmasters are rare -- and it is explicitly NOT a spawn
// table: nothing may create a character at a tier.
int PopulationSharePercent(Tier t);

}  // namespace uo::prof
