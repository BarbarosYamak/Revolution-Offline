#include "uo/professions.h"

#include <cstdio>

namespace uo::prof {

const char* IncomeName(Income i) {
    switch (i) {
        case Income::Gather:  return "gather";
        case Income::Process: return "process";
        case Income::Craft:   return "craft";
        case Income::Hunt:    return "hunt";
        case Income::Count:   break;
    }
    return "?";
}

const char* ProfViolationName(ProfViolation v) {
    switch (v) {
        case ProfViolation::None:                   return "none";
        case ProfViolation::NotTwoStartSkills:      return "not_two_start_skills";
        case ProfViolation::StartSkillNot50:        return "start_skill_not_50";
        case ProfViolation::StartStatsWrongTotal:   return "start_stats_wrong_total";
        case ProfViolation::StartStatOverServerMax: return "start_stat_over_server_max";
        case ProfViolation::SkillBudgetExceeded:    return "skill_budget_exceeded";
        case ProfViolation::PerSkillCap:            return "per_skill_cap";
        case ProfViolation::InactiveSkill:          return "inactive_skill";
        case ProfViolation::StatTotalExceeded:      return "stat_total_exceeded";
        case ProfViolation::StatPerCapExceeded:     return "stat_per_cap_exceeded";
        case ProfViolation::NotExactlyOnePrimary: return "not_exactly_one_primary";
        case ProfViolation::UtilityAtFullCap:     return "utility_at_full_cap";
        case ProfViolation::NoIncome:               return "no_income";
        case ProfViolation::Count:                  break;
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Item graphics, every one read off this shard's own itemdefs. Never guessed.
// ---------------------------------------------------------------------------
namespace {

constexpr u16 kHatchet[]  = {0x0F43, 0x0F44};   // i_hatchet, layer 2
constexpr u16 kPickaxe[]  = {0x0E85, 0x0E86};   // i_pickaxe, layer 1, ReqStr=50
constexpr u16 kBandage    = 0x0E21;
constexpr u16 kMortar     = 0x0E9B;             // i_mortar_pestle
constexpr u16 kBottle     = 0x0F0E;             // i_bottle_empty
constexpr u16 kSpellbook  = 0x0EFA;             // i_spellbook
constexpr u16 kTongs[]    = {0x0FBB, 0x0FBC};   // i_tongs
constexpr u16 kBread[]    = {0x103B, 0x09EB};

std::vector<u16> V(const u16* p, usize n) { return std::vector<u16>(p, p + n); }

ConsumableNeed Bandages() {
    ConsumableNeed c;
    c.name = "bandage";
    c.graphics = {kBandage};
    c.low = 8;
    c.restockTo = 30;
    return c;
}

ConsumableNeed Food() {
    ConsumableNeed c;
    c.name = "food";
    c.graphics = V(kBread, 2);
    c.low = 1;
    c.restockTo = 5;
    return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// THE CATALOGUE
//
// Every entry is a GOAL PROFILE. The two start skills are what the character
// asks the server for; every `targets` entry is something it must earn.
//
// The start pair is also chosen for the KIT it triggers, because on this shard
// the newbie template decides whether a life can begin at all
// (templates_special/sp_tm_newbie.scp):
//
//   LUMBERJACKING -> i_hatchet          SWORDSMANSHIP -> i_katana
//   MINING        -> i_pickaxe          BLACKSMITHING -> tongs, pickaxe,
//                                                        i_ingot_iron x50
//   HEALING       -> i_bandage x50      ALCHEMY       -> mortar, 4 bottles
//   MAGERY        -> spellbook, 2 scrolls (NO REAGENTS)
//   TAMING        -> nothing at all
// ---------------------------------------------------------------------------

const std::vector<Profession>& All() {
    static const std::vector<Profession> kAll = [] {
        std::vector<Profession> v;

        // --- 1. Frontier Lumberjack / Swordsman -------------------------------
        // The M4 character, restated under the Revolution creation rule. M4
        // asked for three skills (40/40/20) and 80 stat points; that was legal
        // but not the rule, and this is the corrected form.
        {
            Profession p;
            p.id = "lumberjack_swordsman";
            p.label = "Frontier Lumberjack / Swordsman";
            p.startSkillA = rules::kLumberjacking;
            p.startSkillB = rules::kSwordsmanship;
            // DEX-weighted: six characters died on a STR-heavy split and one
            // survived on a DEX-heavy one (M4 plan). Same lesson, 50 points.
            p.startStr = 25; p.startDex = 20; p.startInt = 5;
            p.targets = {
                {rules::kLumberjacking, 1000, 5, false, SkillRole::Primary},
                {rules::kSwordsmanship, 1000, 4, false, SkillRole::Secondary},
                {rules::kTactics,       1000, 3, true,  SkillRole::Secondary},
                {rules::kAnatomy,       1000, 2, true,  SkillRole::Secondary},
                {rules::kHealing,       1000, 2, true,  SkillRole::Secondary},
            };
            p.unresolvedTenths = 2000;   // 200.0 deliberately unspent
            p.targetStr = 100; p.targetDex = 100; p.targetInt = 25;
            p.income = {Income::Gather, Income::Hunt};
            p.gathers = "logs";
            p.produces = {"i_log"};
            p.tools = {{"hatchet", V(kHatchet, 2), true}};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.55;
            p.goldReserve = 300;         // one trainer fee held back
            v.push_back(std::move(p));
        }

        // --- 2. Miner / Smith --------------------------------------------------
        //
        // A REAL CONSTRAINT, recorded rather than designed around:
        //
        //   Mining requires a WIELDED tool -- skill45_mining.scp @PreStart reads
        //   SRC.WEAPON.USESCUR.
        //   i_shovel cannot be wielded: tiledata layer 0 (verified with
        //   uo_mul_dump: id 3897 quality=0).
        //   i_pickaxe CAN be wielded (layer 1) but carries ReqStr=50
        //   (items/weapons/i_weapons.scp:192).
        //
        // So under Revolution's 50-point creation pool a fresh miner CANNOT
        // MINE until STR reaches 50, and putting all 50 into STR at creation
        // would leave DEX 0 / INT 0.
        //
        // That is not a blocker, it is the shape of the life: the character
        // starts as a SMITH -- [NEWBIE BLACKSMITHING] hands over tongs and 50
        // iron ingots -- works those, grows STR through carrying and smithing,
        // and becomes a miner once it can lift the pickaxe. STR is the first
        // progression goal, not an assumption.
        {
            Profession p;
            p.id = "miner_smith";
            p.label = "Miner / Blacksmith";
            p.startSkillA = rules::kMining;
            p.startSkillB = rules::kBlacksmithing;
            p.startStr = 35; p.startDex = 10; p.startInt = 5;
            p.targets = {
                {rules::kBlacksmithing, 1000, 5, false, SkillRole::Primary},
                {rules::kMining,        1000, 4, false, SkillRole::Secondary},
                {rules::kTinkering,      500, 2, true,  SkillRole::Utility},
                {rules::kArmsLore,       500, 1, true,  SkillRole::Utility},
            };
            p.unresolvedTenths = 4000;
            p.targetStr = 100; p.targetDex = 45; p.targetInt = 80;
            p.income = {Income::Craft, Income::Process, Income::Gather};
            p.gathers = "ore";
            p.produces = {"i_ingot_iron", "i_spear_short"};
            // Ore comes out of the world, but the SPEAR does not: it is
            // 6 ingots plus one LOG (Production.cpp:107, script SKILLMAKE
            // Blacksmithing 45.3). That single log is the first real
            // producer-to-consumer link in the catalogue -- the smith cannot
            // make it without something a lumberjack pulls out of a tree.
            p.consumes = {"i_ore_iron", "i_log"};
            // The pickaxe is listed even though a new character cannot lift it:
            // the need model must SAY that, not hide it.
            p.tools = {{"pickaxe", V(kPickaxe, 2), true},
                       {"tongs",   V(kTongs, 2),   false}};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.35;      // a smith is not looking for a fight
            p.goldReserve = 500;
            v.push_back(std::move(p));
        }

        // --- 3. Pure Mage ------------------------------------------------------
        //
        // [NEWBIE MAGERY] hands over a spellbook with circles 1-4 and two
        // scrolls, and NO REAGENTS. So a mage's first economic act is buying
        // reagents -- which makes it the archetype that proves the M7 supplier
        // path, not merely the combat one.
        {
            Profession p;
            p.id = "mage";
            p.label = "Pure Mage";
            p.startSkillA = rules::kMagery;
            p.startSkillB = rules::kMeditation;
            p.startStr = 15; p.startDex = 10; p.startInt = 25;
            p.targets = {
                {rules::kMagery,          1000, 5, false, SkillRole::Primary},
                {rules::kMeditation,      1000, 4, false, SkillRole::Secondary},
                {rules::kEvaluatingIntel, 1000, 3, true,  SkillRole::Secondary},
                {rules::kInscription,      500, 1, true,  SkillRole::Utility},
            };
            p.unresolvedTenths = 3500;
            p.targetStr = 25; p.targetDex = 25; p.targetInt = 100;
            p.income = {Income::Craft, Income::Hunt};
            // What a mage actually MAKES. Blank scrolls are Carpentry 25.7
            // (Production.cpp:123) -- wood, not magic -- so listing them as
            // the mage's output was backwards. It makes SPELL scrolls out of
            // them.
            p.produces = {"i_scroll_poison", "i_scroll_recall"};
            // Reagents are bought, never gathered -- the ground-reagent spawns
            // exist but a mage buying from a mage shop is the era behaviour.
            // Blank scrolls are the mage's real bottleneck and it cannot make
            // them: no profession in this catalogue has Carpentry, so today
            // they come from a vendor. That is a KNOWN GAP, not a solved
            // chain -- see docs/M7.md.
            p.consumes = {"i_scroll_blank", "i_reag_black_pearl",
                          "i_reag_blood_moss", "i_reag_mandrake_root",
                          "i_reag_nightshade"};
            p.tools = {{"spellbook", {kSpellbook}, false}};
            p.consumables = {Food()};
            p.riskTolerance = 0.30;      // squishy; disengages early
            p.goldReserve = 800;         // reagents are a running cost
            v.push_back(std::move(p));
        }

        // --- 4. Alchemist ------------------------------------------------------
        // Produces exactly what M6 PvP consumes. [NEWBIE ALCHEMY] gives a
        // mortar and four bottles -- enough to start, not enough to trade.
        {
            Profession p;
            p.id = "alchemist";
            p.label = "Alchemist";
            p.startSkillA = rules::kAlchemy;
            p.startSkillB = rules::kMagery;
            p.startStr = 20; p.startDex = 10; p.startInt = 20;
            p.targets = {
                {rules::kAlchemy,     1000, 5, false, SkillRole::Primary},
                {rules::kMagery,       500, 3, false, SkillRole::Utility},
                {rules::kMeditation,   500, 2, true,  SkillRole::Utility},
            };
            p.unresolvedTenths = 4500;
            p.targetStr = 50; p.targetDex = 25; p.targetInt = 100;
            p.income = {Income::Craft};
            // Bottles are NOT bought: i_bottle_empty is Alchemy 25.0 with a
            // glassblowing tool (Production.cpp:178), so an alchemist makes
            // its own. It buys reagents and nothing else.
            p.produces = {"i_potion_refresh", "i_potion_cure"};
            p.consumes = {"i_reag_black_pearl", "i_reag_garlic",
                          "i_reag_ginseng"};
            p.tools = {{"mortar", {kMortar}, false}};
            p.consumables = {Food()};
            p.riskTolerance = 0.25;
            p.goldReserve = 600;
            v.push_back(std::move(p));
        }

        // --- 5. Tamer ----------------------------------------------------------
        //
        // [NEWBIE TAMING] is EMPTY -- the shard hands a tamer nothing at all.
        // Combined with M8 owning the taming ecosystem, this entry exists so
        // the catalogue can express the life and the M5 gate can report it
        // BLOCKED with evidence, rather than pretending it runs.
        {
            Profession p;
            p.id = "tamer";
            p.label = "Animal Tamer";
            p.startSkillA = rules::kTaming;
            p.startSkillB = rules::kAnimalLore;
            p.startStr = 20; p.startDex = 10; p.startInt = 20;
            p.targets = {
                {rules::kTaming,     1000, 5, false, SkillRole::Primary},
                {rules::kAnimalLore, 1000, 4, false, SkillRole::Secondary},
                {rules::kVeterinary, 1000, 3, true,  SkillRole::Secondary},
            };
            p.unresolvedTenths = 4000;
            p.targetStr = 80; p.targetDex = 45; p.targetInt = 100;
            p.income = {Income::Hunt};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.40;
            p.goldReserve = 400;
            v.push_back(std::move(p));
        }

        return v;
    }();
    return kAll;
}

const Profession* Find(const char* id) {
    if (!id) return nullptr;
    for (const Profession& p : All()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

ProfCheck Validate(const rules::Profile& rp, const Profession& p) {
    ProfCheck out;

    // --- Revolution creation rule -----------------------------------------
    if (p.startSkillA < 0 || p.startSkillB < 0 ||
        p.startSkillA == p.startSkillB) {
        out.violation = ProfViolation::NotTwoStartSkills;
        return out;
    }
    out.startSkillSum = kRevolutionStartSkillEach * kRevolutionStartSkillCount;
    if (out.startSkillSum > kServerCreateSkillSumMax) {
        // Cannot happen with the constants above; asserted so a future edit
        // that raises them fails here rather than at character creation.
        out.violation = ProfViolation::StartSkillNot50;
        return out;
    }
    for (int id : {p.startSkillA, p.startSkillB}) {
        if (!rules::SkillActive(id)) {
            out.violation = ProfViolation::InactiveSkill;
            out.skillId = id;
            return out;
        }
    }

    out.startStatSum = p.startStr + p.startDex + p.startInt;
    if (out.startStatSum != kRevolutionStartStatTotal) {
        out.violation = ProfViolation::StartStatsWrongTotal;
        return out;
    }
    if (p.startStr > kServerCreateStatEachMax ||
        p.startDex > kServerCreateStatEachMax ||
        p.startInt > kServerCreateStatEachMax) {
        out.violation = ProfViolation::StartStatOverServerMax;
        return out;
    }

    // --- the finished build, against the 700 / 225 caps --------------------
    for (const SkillTargetSpec& t : p.targets) {
        if (t.tenths > rp.perSkillCapTenths) {
            out.violation = ProfViolation::PerSkillCap;
            out.skillId = t.skillId;
            return out;
        }
        if (!rules::SkillActive(t.skillId)) {
            out.violation = ProfViolation::InactiveSkill;
            out.skillId = t.skillId;
            return out;
        }
        out.targetSkillSum += t.tenths;
    }
    out.targetSkillSum += p.unresolvedTenths;
    if (out.targetSkillSum > rp.totalSkillCapTenths) {
        out.violation = ProfViolation::SkillBudgetExceeded;
        return out;
    }

    out.targetStatSum = p.targetStr + p.targetDex + p.targetInt;
    if (p.targetStr > rp.perStatCap || p.targetDex > rp.perStatCap ||
        p.targetInt > rp.perStatCap) {
        out.violation = ProfViolation::StatPerCapExceeded;
        return out;
    }
    if (out.targetStatSum > rp.totalStatCap) {
        out.violation = ProfViolation::StatTotalExceeded;
        return out;
    }

    // A life with no way to earn is not a profession.
    // --- roles ------------------------------------------------------------
    //
    // Exactly one primary. It is what the paperdoll title reads, and a title
    // is the only thing another player can see about a character before
    // speaking to it -- two primaries means the title is arbitrary.
    int primaries = 0;
    for (const SkillTargetSpec& t : p.targets) {
        if (t.role == SkillRole::Primary) ++primaries;
        // A "utility" skill at the per-skill cap is not utility, it is a
        // second profession. The cap is what keeps a dexxer's Magery at
        // Recall-and-Cure and keeps Revolution's travel-magic rarity intact.
        if (t.role == SkillRole::Utility && t.tenths >= rp.perSkillCapTenths) {
            out.violation = ProfViolation::UtilityAtFullCap;
            out.skillId = t.skillId;
            return out;
        }
    }
    if (!p.targets.empty() && primaries != 1) {
        out.violation = ProfViolation::NotExactlyOnePrimary;
        return out;
    }

    if (p.income.empty()) {
        out.violation = ProfViolation::NoIncome;
        return out;
    }

    out.ok = true;
    return out;
}

const char* SkillRoleName(SkillRole r) {
    switch (r) {
        case SkillRole::Primary:   return "primary";
        case SkillRole::Secondary: return "secondary";
        case SkillRole::Utility:   return "utility";
    }
    return "?";
}

const char* TierName(Tier t) {
    switch (t) {
        case Tier::Novice:      return "Novice";
        case Tier::Apprentice:  return "Apprentice";
        case Tier::Journeyman:  return "Journeyman";
        case Tier::Adept:       return "Adept";
        case Tier::Expert:      return "Expert";
        case Tier::Master:      return "Master";
        case Tier::Grandmaster: return "Grandmaster";
    }
    return "?";
}

Tier TierFromSkillSum(i32 sumTenths, i32 capTenths) {
    if (capTenths <= 0) return Tier::Novice;
    if (sumTenths < 0) sumTenths = 0;
    // Fraction of the 700-point budget the character has actually earned.
    // A Revolution character starts at 100.0 of 700.0 -- 14% -- so Novice
    // deliberately covers everything up to a quarter of a finished build.
    const int pct = static_cast<int>((sumTenths * 100LL) / capTenths);
    if (pct >= 97) return Tier::Grandmaster;   // effectively a finished build
    if (pct >= 85) return Tier::Master;
    if (pct >= 70) return Tier::Expert;
    if (pct >= 55) return Tier::Adept;
    if (pct >= 40) return Tier::Journeyman;
    if (pct >= 25) return Tier::Apprentice;
    return Tier::Novice;
}

int PopulationSharePercent(Tier t) {
    // uo-offline's bell curve (BotSkillTier.cs), kept because the SHAPE is
    // right -- most players are mid-tier, Grandmasters are rare. There it is
    // a spawn table; here it is only something a fleet can compare itself to.
    switch (t) {
        case Tier::Novice:      return 15;
        case Tier::Apprentice:  return 20;
        case Tier::Journeyman:  return 20;
        case Tier::Adept:       return 20;
        case Tier::Expert:      return 15;
        case Tier::Master:      return 8;
        case Tier::Grandmaster: return 2;
    }
    return 0;
}

}  // namespace uo::prof
