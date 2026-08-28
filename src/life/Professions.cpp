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
constexpr u16 kFishingPole[] = {0x0DBF, 0x0DC0};
constexpr u16 kMortar     = 0x0E9B;             // i_mortar_pestle
constexpr u16 kBottle     = 0x0F0E;             // i_bottle_empty
constexpr u16 kSpellbook  = 0x0EFA;             // i_spellbook
constexpr u16 kTongs[]    = {0x0FBB, 0x0FBC};   // i_tongs
constexpr u16 kBread[]    = {0x103B, 0x09EB};

// -- M5.2: graphics for the eleven new archetypes, read off the runtime's
// own itemdefs the same way as the block above -- never guessed.
constexpr u16 kShovel      = 0x0F39;  // i_profession.scp:440  i_shovel
constexpr u16 kLockpick    = 0x14FB;  // i_profession.scp:1511 i_lockpick (qty 20 in the newbie kit)
constexpr u16 kSewingKit   = 0x0F9D;  // i_profession_tailor_tanner.scp:115
constexpr u16 kScissors    = 0x0F9E;  // i_profession_tailor_tanner.scp:128
constexpr u16 kSaw         = 0x1034;  // i_profession.scp:945   i_saw
constexpr u16 kTinkerTools = 0x1EBC;  // i_profession.scp:2649

// -- M5.2: skill ids the M3.6 SkillId enum (include/uo/rules.h) does not
// carry yet. Same sourcing rule as every id rules.h already has: read off
// the skill's own script filename under runtime/scripts/skills/ --
// skill<N>_<name>.scp. None of these appear in rules::InactiveSkills().
constexpr int kParrying     = 5;   // skill5_parrying.scp
constexpr int kArchery      = 31;  // skill31_archery.scp
constexpr int kMaceFighting = 41;  // skill41_macefighting.scp
constexpr int kFencing      = 42;  // skill42_fencing.scp
constexpr int kWrestling    = 43;  // skill43_wrestling.scp

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

ConsumableNeed Lockpicks() {
    ConsumableNeed c;
    c.name = "lockpicks";
    c.graphics = {kLockpick};
    c.low = 5;
    c.restockTo = 20;   // [NEWBIE LOCKPICKING] hands over exactly 20
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

        // --- 1. Lumberjack / Carpenter ----------------------------------------
        // The M4 character, restated twice: once under the Revolution creation
        // rule (M4 asked for three skills at 40/40/20 and 80 stat points --
        // legal, but not the rule), and once under what this life is actually
        // FOR.
        //
        // Project owner, 2026-08-28, describing the Revolution loop:
        //
        //   "you farm wood skill up your lumber jack then you can craft as
        //    carpenter and sell stuff to other players"
        //   "one character, lumberjack carpenter same guy crafter"
        //
        // That matters because logs cannot be sold to an NPC on this shard, so
        // without a crafting skill this life has NO income at all -- it can
        // only pile up a resource nobody will buy. Carpentry is what turns the
        // wood into something a player wants, and it comes out of the 200.0
        // this build had deliberately left unspent.
        //
        // The ID IS DELIBERATELY UNCHANGED. It is the persistence key: a live
        // character's state.json names it, and renaming would orphan Brannoc
        // into "not in the catalogue" and silently revert it to the M4 needs.
        {
            Profession p;
            p.id = "lumberjack_swordsman";
            p.label = "Lumberjack / Carpenter";
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
                // The half that makes it a living. Carpentry 25.7 is where
                // blank scrolls start (Production.cpp:123), which is also the
                // input the mage cannot make for itself.
                {rules::kCarpentry,     1000, 4, true,  SkillRole::Secondary},
            };
            p.unresolvedTenths = 1000;   // 100.0 still deliberately unspent
            p.targetStr = 100; p.targetDex = 100; p.targetInt = 25;
            p.income = {Income::Craft, Income::Gather, Income::Hunt};
            p.gathers = "logs";
            // Logs stay in `produces` even though they are the character's own
            // input: a smith needs one per spear, so a log is a real trade
            // good between PLAYERS. The reserve rule handles the rest -- it
            // sees i_log feeding i_board and holds a working stock back.
            // i_club is what actually pays: logs, boards and blank scrolls
            // are all MATERIALS and go to players, while a club is a finished
            // weapon the blunt weaponsmith buys. Without it this life crafts
            // all day and has no income at all.
            p.produces = {"i_log", "i_board", "i_scroll_blank", "i_club"};
            p.tools = {{"hatchet", V(kHatchet, 2), true}};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.55;
            p.goldReserve = 300;         // one trainer fee held back
                        // Yew is the forest. Britain and Skara Brae have woods within reach.
            p.homeCities = {"Yew", "Britain", "Skara Brae"};
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
                        // Minoc is the mining town -- the mountain is why it is there. Vesper
            // works the same range from the north.
            p.homeCities = {"Minoc", "Britain", "Vesper"};
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
                        // Moonglow is the mage city; Britain has the largest mage shop.
            p.homeCities = {"Moonglow", "Britain", "Nujel'm"};
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
                        // Reagents and a mortar; Britain has the deepest reagent supply.
            p.homeCities = {"Britain", "Vesper", "Moonglow"};
            v.push_back(std::move(p));
        }

        // --- 5. Fisher ---------------------------------------------------------
        //
        // THE ONLY LIFE IN THE CATALOGUE THAT CAN EARN ON DAY ONE.
        //
        // Fishing is the strongest row in the Gold Faucet Registry: Revolution
        // documented fish as sellable to NPC vendors, this shard's
        // VENDOR_B_FISHER carries the BUY rows (tm_vend.scp:1022-1027, {4 24}),
        // and [NEWBIE FISHING] hands over a pole. Nothing else in the
        // catalogue has all three.
        //
        // It exists precisely because the registry refuses smith, carpentry,
        // tailoring, tinkering and alchemy output as faucets. Without a life
        // that can legitimately earn, "a bot funds its own training" is not
        // demonstrable at all.
        {
            Profession p;
            p.id = "fisher";
            p.label = "Fisher";
            // Fishing is the PRIMARY and leads, which is what the paperdoll
            // title reads. Tested both orders live: the kit outcome is the
            // same either way, so the ordering is a build decision rather
            // than a workaround. See docs/M7.md on the missing pole.
            p.startSkillA = rules::kFishing;
            p.startSkillB = rules::kCooking;
            // Fishing pulls against STR for the catch and the carry; cooking
            // needs nothing. A fisher is not a fighter and does not pretend.
            p.startStr = 25; p.startDex = 15; p.startInt = 10;
            p.targets = {
                {rules::kFishing,   1000, 5, true,  SkillRole::Primary},
                {rules::kCooking,    500, 3, true,  SkillRole::Utility},
            };
            p.unresolvedTenths = 5500;
            p.targetStr = 80; p.targetDex = 50; p.targetInt = 45;
            p.income = {Income::Gather, Income::Craft};
            p.gathers = "fish";
            // Cooked fish is worth about 1gp more than raw, so the cooking
            // half is a real if small margin rather than decoration.
            // All four kinds the sea yields, or three quarters of a catch is
            // invisible to the economy layer.
            p.produces = {"i_fish_big_1", "i_fish_big_2", "i_fish_big_3",
                          "i_fish_big_4", "i_fish_small",
                          "i_fish_cut_raw", "i_fish_cut_cooked"};
            p.tools = {{"fishing pole", V(kFishingPole, 2), true}};
            p.consumables = {Food()};
            p.riskTolerance = 0.20;      // stands on a dock; avoids everything
            p.goldReserve = 150;         // a replacement pole
            v.push_back(std::move(p));
        }

        // --- 6. Tamer ----------------------------------------------------------
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
                        // Skara Brae is the ranger town, if a tamer ever becomes playable.
            p.homeCities = {"Britain", "Skara Brae"};
            v.push_back(std::move(p));
        }

        // --- 7. Fencer -----------------------------------------------------
        //
        // The PW-03 "Fencing Poison Warrior" family (compendium v1, section
        // 6): FENC 100 / TACT 100 / ANAT 100 core, plus "HEAL high", "POI
        // high" and "PARRY or second combat/utility" -- HISTORICAL_FAMILY,
        // no single posted total. The split below follows PW-01's exact
        // all-100 shape (the one HISTORICAL_EXACT sibling in the same
        // section) with Swordsmanship and Archery swapped for Fencing, and
        // 100.0 left deliberately open rather than invented.
        //
        // [NEWBIE FENCING] hands over i_kryss; [NEWBIE TACTICS] hands over
        // i_katana (sp_tm_newbie.scp) -- an odd pairing on paper, a spare
        // blade to sell or dual-carry in practice.
        {
            Profession p;
            p.id = "fencer";
            p.label = "Fencer";
            p.startSkillA = kFencing;
            p.startSkillB = rules::kTactics;
            p.startStr = 20; p.startDex = 25; p.startInt = 5;
            p.targets = {
                {kFencing,             1000, 6, false, SkillRole::Primary},
                {rules::kTactics,      1000, 5, true,  SkillRole::Secondary},
                {rules::kAnatomy,      1000, 4, true,  SkillRole::Secondary},
                {rules::kHealing,      1000, 3, true,  SkillRole::Secondary},
                {rules::kPoisoning,    1000, 2, false, SkillRole::Secondary},
                {kParrying,            1000, 1, true,  SkillRole::Secondary},
            };
            p.unresolvedTenths = 1000;   // the PW family's open seventh slot
            p.targetStr = 100; p.targetDex = 100; p.targetInt = 25;
            // No NPC faucet pays a Fencer for anything (Faucets.cpp has none
            // for weapon combat); monster_gold is the one row open to
            // "anybody who can survive the fight".
            p.income = {Income::Hunt};
            // Bought, not made: deadly poison is the alchemist's product
            // (Production.cpp i_potion_poisondeadly, ALCHEMY 90.1), exactly
            // what this build's Poisoning 100.0 needs applied to a weapon.
            p.consumes = {"i_potion_poisondeadly"};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.75;      // duelist; picks the fight
            p.goldReserve = 350;
            // UNKNOWN: no Revolution-specific evidence places Fencers in a
            // particular city. Britain and Trinsic are the generic UO
            // warrior hubs, not a Revolution-sourced fact.
            p.homeCities = {"Britain", "Trinsic"};
            v.push_back(std::move(p));
        }

        // --- 8. Macer --------------------------------------------------------
        //
        // PW-04 "Mace Armor-Break Warrior" (compendium v1, section 6):
        // MACE 100 / TACT 100 / ANAT 100 core, "HEAL", "PARRY", and
        // "optional POI/ARCH/utility" -- HISTORICAL_FAMILY. Poisoning is
        // demoted to Utility here rather than a full 100.0: the family's own
        // wording treats it as optional, unlike the Fencer's PW-03 family
        // where "POI high" reads as core.
        //
        // [NEWBIE MACEFIGHTING] hands over i_club -- the same defname the
        // lumberjack/carpenter's Carpentry line produces (Production.cpp
        // i_club, def_carpentry.scp:41), so a Macer's first weapon purchase
        // is literally a carpenter's product.
        {
            Profession p;
            p.id = "macer";
            p.label = "Macer";
            p.startSkillA = kMaceFighting;
            p.startSkillB = rules::kAnatomy;
            p.startStr = 30; p.startDex = 15; p.startInt = 5;
            p.targets = {
                {kMaceFighting,        1000, 6, false, SkillRole::Primary},
                {rules::kTactics,      1000, 5, true,  SkillRole::Secondary},
                {rules::kAnatomy,      1000, 4, true,  SkillRole::Secondary},
                {rules::kHealing,      1000, 3, true,  SkillRole::Secondary},
                {kParrying,            1000, 2, true,  SkillRole::Secondary},
                {rules::kPoisoning,     500, 1, false, SkillRole::Utility},
            };
            p.unresolvedTenths = 1500;
            p.targetStr = 100; p.targetDex = 75; p.targetInt = 50;
            p.income = {Income::Hunt};
            // Armor/stamina pressure is a slow fight, and a slow fight is
            // won on Cure potions, not poison -- the alchemist's product
            // again, a different one than the Fencer buys.
            p.consumes = {"i_potion_cure"};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.70;
            p.goldReserve = 350;
            // UNKNOWN, same caveat as the Fencer: no Revolution-specific
            // source names a Macer's city. Vesper sits by Minoc's smiths,
            // who sell the war hammers/mauls this build eventually wants.
            p.homeCities = {"Vesper", "Britain"};
            v.push_back(std::move(p));
        }

        // --- 9. Archer ---------------------------------------------------------
        //
        // CR-07 "Lumberjack + Bowyer" (compendium v1, section 10) describes
        // the loop this build lives inside: "harvest logs -> special logs ->
        // craft bows/arrows -> sell to Archers." This entry is the CUSTOMER
        // at the end of that chain who also fletches its own ammunition --
        // [NEWBIE BOWCRAFT] hands over 14 boards, 5 feathers and 5 arrow
        // shafts (sp_tm_newbie.scp), enough to start supplying itself rather
        // than buying every arrow.
        //
        // It is also the SECOND profession in the catalogue with a real NPC
        // gold faucet, after fishing: Revolution's own Bowcraft guidance
        // says crafted bows could be sold to NPC vendors, and Faucets.cpp
        // carries both rows as Policy::Allow (bow_to_bowyer,
        // crossbow_to_bowyer; VENDOR_B_BOWYER tm_vend.scp:1444).
        {
            Profession p;
            p.id = "archer";
            p.label = "Archer";
            p.startSkillA = kArchery;
            p.startSkillB = rules::kBowcraft;
            p.startStr = 15; p.startDex = 30; p.startInt = 5;
            p.targets = {
                {kArchery,             1000, 5, false, SkillRole::Primary},
                {rules::kBowcraft,     1000, 4, false, SkillRole::Secondary},
                {rules::kTactics,      1000, 3, true,  SkillRole::Secondary},
                {rules::kAnatomy,       500, 2, true,  SkillRole::Utility},
                {rules::kHealing,       500, 1, true,  SkillRole::Utility},
            };
            p.unresolvedTenths = 3000;
            p.targetStr = 70; p.targetDex = 100; p.targetInt = 55;
            // Craft leads: the bow/crossbow sale is a real NPC faucet,
            // unlike almost everything else this catalogue can make.
            p.income = {Income::Craft, Income::Hunt};
            // Logs are bought from a lumberjack rather than chopped, unlike
            // every other wood-using life in the catalogue -- an Archer that
            // also felled its own trees would not need the CR-07 loop.
            p.gathers = "";
            p.produces = {"i_arrow_shaft", "i_arrow", "i_bow"};
            p.consumes = {"i_log", "i_feather"};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.50;      // kites at range, disengages if closed on
            p.goldReserve = 300;
            // Yew is the forest CR-07 names as the special-log source; Skara
            // Brae and Trinsic sit within bow range of it.
            p.homeCities = {"Yew", "Skara Brae", "Trinsic"};
            v.push_back(std::move(p));
        }

        // --- 10. Warlock ---------------------------------------------------
        //
        // WL-01, "2007 Balanced Sword Warlock" (compendium v1, section 5),
        // HISTORICAL_EXACT and the only Warlock allocation in the section
        // that already sums to exactly 700:
        //
        //   MAGERY 100, SW 100, TACT 100, EVAL 80, POI 80, HEAL 80, ANAT 80,
        //   MEDI 80
        //
        // https://www.revolutionuo.net/forum/index.php/topic%2C23720.0.html
        //
        // Seven other Warlock variants are on record (WL-02..WL-08) trading
        // poison for mana, Fencing/Mace/Archery for Sword, or duel focus for
        // group support -- the family is real but not singular, and WL-01 is
        // the one with a clean, complete, sourced total to build against.
        //
        // [NEWBIE MAGERY] hands over a spellbook (circles 1-4, NO REAGENTS)
        // and [NEWBIE SWORDSMANSHIP] a katana -- the same kit as the mage
        // and the lumberjack respectively, because this build is genuinely
        // both.
        {
            Profession p;
            p.id = "warlock";
            p.label = "Warlock";
            p.startSkillA = rules::kMagery;
            p.startSkillB = rules::kSwordsmanship;
            p.startStr = 20; p.startDex = 15; p.startInt = 15;
            p.targets = {
                {rules::kMagery,          1000, 8, false, SkillRole::Primary},
                {rules::kSwordsmanship,   1000, 7, false, SkillRole::Secondary},
                {rules::kTactics,         1000, 6, true,  SkillRole::Secondary},
                {rules::kEvaluatingIntel,  800, 5, true,  SkillRole::Secondary},
                {rules::kPoisoning,        800, 4, false, SkillRole::Secondary},
                {rules::kHealing,          800, 3, true,  SkillRole::Secondary},
                {rules::kAnatomy,          800, 2, true,  SkillRole::Secondary},
                {rules::kMeditation,       800, 1, true,  SkillRole::Secondary},
            };
            p.unresolvedTenths = 0;      // WL-01 spends the entire 700, forum-exact
            p.targetStr = 90; p.targetDex = 90; p.targetInt = 45;
            p.income = {Income::Hunt};
            // A hybrid still needs the mage's reagents, just fewer of them --
            // this build never touches Inscription, so it buys finished
            // scrolls rather than blank ones.
            p.consumes = {"i_reag_black_pearl", "i_reag_nightshade"};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.65;      // duel-oriented, per the WL-01/WL-03 family
            p.goldReserve = 700;         // reagents AND weapon upkeep
            p.homeCities = {"Britain", "Trinsic"};
            v.push_back(std::move(p));
        }

        // --- 11. PK / Head Hunter --------------------------------------------
        //
        // PK-02, "Pure Warrior Head Hunter" (compendium v1, section 14):
        // "strong Warrior allocation: combat, Tactics, Anatomy, Healing,
        // Poisoning, Parry/second combat... Lower reagent dependency; heavy
        // potion/bandage/weapon dependency." HISTORICAL_FAMILY / no single
        // posted total, so the shape below is deliberately LEANER than the
        // Fencer/Macer/Warlock builds -- a Head Hunter's whole point is
        // opportunistic kills, not a maximal build, and Wrestling stands in
        // for PK-02's "second combat" so it can still fight disarmed.
        //
        // Revolution ran an actual Head Hunter gold system
        // (revolutionuo.net/head_hunters) -- Faucets.cpp's own
        // head_hunter_bounty row records it as HistoryEvidence::Confirmed
        // but RuntimeEvidence::Unverified / Policy::BlockedRuntime, so
        // `income` below states the ONLY route this build can use today:
        // monster loot. The bounty itself is a documented gap, not invented
        // income.
        {
            Profession p;
            p.id = "pk";
            p.label = "Player Killer";
            p.startSkillA = rules::kSwordsmanship;
            p.startSkillB = rules::kPoisoning;
            p.startStr = 20; p.startDex = 25; p.startInt = 5;
            p.targets = {
                {rules::kSwordsmanship, 1000, 5, false, SkillRole::Primary},
                {rules::kTactics,       1000, 4, true,  SkillRole::Secondary},
                {rules::kPoisoning,     1000, 3, false, SkillRole::Secondary},
                {rules::kHealing,        700, 2, true,  SkillRole::Secondary},
                {kWrestling,             300, 1, false, SkillRole::Utility},
            };
            p.unresolvedTenths = 3000;
            p.targetStr = 60; p.targetDex = 100; p.targetInt = 65;
            p.income = {Income::Hunt};
            // Recall is the escape valve after a kill; deadly poison is the
            // finisher. Both are bought, never made -- this build has no
            // Magery or Alchemy of its own.
            p.consumes = {"i_scroll_recall", "i_potion_poisondeadly"};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.85;      // the highest in the catalogue
            p.goldReserve = 500;
            // UNKNOWN: no Revolution source names a murderer's home city.
            // Buccaneer's Den is generic UO outlaw lore, not a Revolution
            // fact.
            p.homeCities = {"Buccaneer's Den", "Britain"};
            v.push_back(std::move(p));
        }

        // --- 12. Treasure Hunter -----------------------------------------------
        //
        // TH-01, "Balanced Treasure Mage" (compendium v1, section 8),
        // HISTORICAL_EXACT and already exactly 700:
        //
        //   LOCK 100, CARTO 100, MAGERY 100, EVAL 100, MEDI 100, POI 80,
        //   HEAL 60, ANAT 60
        //
        // https://www.revolutionuo.net/forum/index.php?topic=66975.0
        //
        // TH-04 (same section) separately notes "approximately 30 Mining was
        // required to open/dig treasure in that period" -- UNKNOWN against
        // this runtime, not verified or modeled here, so Mining is not a
        // target below. The shovel tool is carried regardless: digging up a
        // chest needs one whatever the skill gate turns out to be.
        //
        // [NEWBIE LOCKPICKING] hands over 20 lockpicks, [NEWBIE CARTOGRAPHY]
        // 4 blank maps and a sextant (sp_tm_newbie.scp) -- a kit that is
        // already a treasure hunter's toolkit, nothing improvised.
        {
            Profession p;
            p.id = "treasure_hunter";
            p.label = "Treasure Hunter";
            p.startSkillA = rules::kLockpicking;
            p.startSkillB = rules::kCartography;
            p.startStr = 15; p.startDex = 20; p.startInt = 15;
            p.targets = {
                {rules::kLockpicking,     1000, 8, false, SkillRole::Primary},
                {rules::kCartography,     1000, 7, true,  SkillRole::Secondary},
                {rules::kMagery,          1000, 6, false, SkillRole::Secondary},
                {rules::kEvaluatingIntel, 1000, 5, true,  SkillRole::Secondary},
                {rules::kMeditation,      1000, 4, true,  SkillRole::Secondary},
                {rules::kPoisoning,        800, 3, false, SkillRole::Secondary},
                {rules::kHealing,          600, 2, true,  SkillRole::Secondary},
                {rules::kAnatomy,          600, 1, true,  SkillRole::Secondary},
            };
            p.unresolvedTenths = 0;      // TH-01 spends the entire 700, forum-exact
            p.targetStr = 85; p.targetDex = 100; p.targetInt = 40;
            // Faucets.cpp's own treasure_gold row is Confirmed history but
            // Policy::BlockedRuntime -- nothing here has opened a chest on
            // this runtime yet. Guardian loot is what actually pays today.
            p.income = {Income::Hunt};
            p.gathers = "";
            p.consumes = {"i_reag_black_pearl", "i_reag_mandrake_root"};
            p.tools = {{"shovel",    {kShovel},    false},   // wield requirement UNKNOWN
                       {"spellbook", {kSpellbook}, false}};
            p.consumables = {Lockpicks(), Bandages(), Food()};
            p.riskTolerance = 0.45;      // handles guardians, disengages once looted
            p.goldReserve = 600;
            // UNKNOWN: no city evidence for treasure hunters specifically.
            // Britain is the general market; Vesper is the general travel hub.
            p.homeCities = {"Britain", "Vesper"};
            v.push_back(std::move(p));
        }

        // --- 13. Mage Blacksmith -----------------------------------------------
        //
        // No compendium section names this pairing outright, so it is
        // REVOLUTION_DERIVED rather than a posted allocation: it is the
        // professions.h SkillRole::Utility idea, applied to a smith instead
        // of a dexxer. That header's own example is "a dexxer's Magery is
        // for Recall and Cure and must never become a second profession" --
        // here BOTH Mining and Magery are held at that deliberately-capped
        // level, and Blacksmithing alone is the real calling.
        //
        // Contrast with `miner_smith`: that build starts Mining+Blacksmithing
        // and trains Mining to a full 100.0 Secondary. This one starts
        // Blacksmithing+Magery -- [NEWBIE BLACKSMITHING] hands over tongs, a
        // pickaxe and 50 ingots, so it can smith from minute one -- and only
        // grows into Mining later, at a permanently reduced, capped level,
        // because self-sufficiency, not ore volume, is the point of carrying
        // it at all.
        {
            Profession p;
            p.id = "mage_blacksmith";
            p.label = "Mage Blacksmith";
            p.startSkillA = rules::kBlacksmithing;
            p.startSkillB = rules::kMagery;
            p.startStr = 30; p.startDex = 10; p.startInt = 10;
            p.targets = {
                {rules::kBlacksmithing, 1000, 3, false, SkillRole::Primary},
                {rules::kMining,         500, 2, false, SkillRole::Utility},
                {rules::kMagery,         500, 1, false, SkillRole::Utility},
            };
            p.unresolvedTenths = 5000;
            p.targetStr = 100; p.targetDex = 50; p.targetInt = 60;
            // Same faucet gap as miner_smith: Faucets.cpp's
            // smith_output_to_vendor is RefuseAuthenticity, so smithed goods
            // go to the player market only.
            p.income = {Income::Craft};
            p.gathers = "ore";
            p.produces = {"i_ingot_iron", "i_dagger", "i_spear_short"};
            // A small reagent basket -- only what Recall and Cure need, not
            // a working mage's full set.
            p.consumes = {"i_ore_iron", "i_log", "i_reag_black_pearl",
                          "i_reag_blood_moss", "i_reag_mandrake_root"};
            p.tools = {{"pickaxe", V(kPickaxe, 2), true},
                       {"tongs",   V(kTongs, 2),   false},
                       {"spellbook", {kSpellbook}, false}};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.40;
            p.goldReserve = 550;
            p.homeCities = {"Minoc", "Moonglow", "Britain"};
            v.push_back(std::move(p));
        }

        // --- 14. Full Crafter ----------------------------------------------
        //
        // EC-02, "Multi-Craft Merchant" (compendium v1, section 15):
        // "Could combine several: Mining, BS, Alchemy, Tailoring, Tinkering,
        // Inscription... Not all at GM simultaneously if cap prevents it.
        // Character chooses a realistic subset." REVOLUTION_DERIVED -- the
        // family is real, the specific subset below is this catalogue's
        // choice: Carpentry and Blacksmithing as the two real trades, with
        // Tinkering, Tailoring and Alchemy carried at working-but-capped
        // levels rather than invented as further GMs.
        //
        // The point of this life is BREADTH, not depth: unlike every other
        // craft entry in this catalogue, it does not gather anything --
        // `gathers` is deliberately empty. [NEWBIE CARPENTRY] and
        // [NEWBIE BLACKSMITHING] together hand over 10 boards, a saw, tongs,
        // a pickaxe and 50 iron ingots on day one (sp_tm_newbie.scp), so it
        // can start converting bought logs and ore into finished goods
        // immediately rather than harvesting its own.
        {
            Profession p;
            p.id = "full_crafter";
            p.label = "Full Crafter";
            p.startSkillA = rules::kCarpentry;
            p.startSkillB = rules::kBlacksmithing;
            p.startStr = 25; p.startDex = 15; p.startInt = 10;
            p.targets = {
                {rules::kCarpentry,     1000, 5, false, SkillRole::Primary},
                {rules::kBlacksmithing, 1000, 4, false, SkillRole::Secondary},
                {rules::kTinkering,      700, 3, true,  SkillRole::Secondary},
                {rules::kTailoring,      600, 2, true,  SkillRole::Utility},
                {rules::kAlchemy,        400, 1, true,  SkillRole::Utility},
            };
            p.unresolvedTenths = 3300;
            p.targetStr = 100; p.targetDex = 60; p.targetInt = 65;
            // Every one of these trades is refused as an NPC faucet
            // (Faucets.cpp: carpentry_output_to_vendor, smith_output_to_vendor,
            // alchemy_output_to_vendor are all Refuse*) -- the starkest
            // player-market-only life in the catalogue.
            p.income = {Income::Craft, Income::Process};
            p.gathers = "";
            p.produces = {"i_board", "i_dagger", "i_spear_short", "i_club",
                          "i_bottle_empty", "i_potion_cure", "i_sash"};
            p.consumes = {"i_log", "i_ore_iron", "i_reag_garlic"};
            p.tools = {{"tongs",  V(kTongs, 2), false},
                       {"saw",    {kSaw},       false},
                       {"mortar", {kMortar},    false}};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.35;
            p.goldReserve = 450;
            p.homeCities = {"Britain", "Minoc", "Yew"};
            v.push_back(std::move(p));
        }

        // --- 15. Tailor ----------------------------------------------------
        //
        // CR-04, "Special Robe Tailor" (compendium v1, section 10),
        // HISTORICAL_FAMILY: "TAILOR high, often MAGERY-oriented character,
        // economic/travel support." PM-03 (section 4) independently confirms
        // the pairing from the mage side: "A player explicitly described
        // using Tailoring as the side skill and crafting a large share of
        // the shard's special robes." Neither thread posts a full 700
        // split, so the numbers below are REVOLUTION_DERIVED from that
        // shape.
        //
        // The ArmsLore target is not decorative: Production.cpp's own
        // i_leather_tunic recipe requires Tailoring 70.5 AND ArmsLore 10.0
        // (SKILLMAKE=Tailoring 70.5,Armslore 10.0), so this is the one
        // profession in the catalogue that trains ArmsLore for a real
        // production reason rather than as combat flavor. It is held
        // exactly at the 50.0 creation already grants -- well above the
        // recipe's 10.0 floor -- rather than trained further.
        //
        // [NEWBIE TAILORING] hands over a cloth bolt and a sewing kit --
        // enough to cut cloth and start sewing immediately. [NEWBIE
        // ARMSLORE] hands over one random weapon (kryss/katana/club), a
        // spare rather than a tool.
        {
            Profession p;
            p.id = "tailor";
            p.label = "Tailor";
            p.startSkillA = rules::kTailoring;
            p.startSkillB = rules::kArmsLore;
            p.startStr = 15; p.startDex = 15; p.startInt = 20;
            p.targets = {
                {rules::kTailoring, 1000, 4, false, SkillRole::Primary},
                {rules::kArmsLore,   500, 3, false, SkillRole::Utility},
                // Tinkering is capped Utility because Production.cpp's own
                // comment names it as what "makes the tailor's kit" -- just
                // enough to eventually replace a broken sewing kit, not a
                // second trade.
                {rules::kTinkering,  500, 2, true,  SkillRole::Utility},
                {rules::kMagery,     250, 1, false, SkillRole::Utility},
            };
            p.unresolvedTenths = 4750;
            p.targetStr = 40; p.targetDex = 60; p.targetInt = 100;
            // Faucets.cpp refuses generic tailor buyback outright
            // (tailor_output_to_vendor, Policy::RefusePlayerMarket): cloth
            // and robes are player-market goods on Revolution, full stop.
            p.income = {Income::Craft};
            p.gathers = "wool";
            p.produces = {"i_cloth_bolt", "i_sash", "i_robe",
                          "i_leather_tunic"};
            // Hides are a hunter's product, not something this life gathers
            // itself -- the leather-tunic half of its output depends on
            // someone else's kill.
            p.consumes = {"i_hides_cut"};
            p.tools = {{"sewing kit", {kSewingKit}, false},
                       {"scissors",   {kScissors},  false}};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.15;      // the most peaceful life in the catalogue
            p.goldReserve = 400;
            // UNKNOWN: no Revolution source names a tailor's city; Britain
            // and Trinsic are the generic UO tailoring hubs.
            p.homeCities = {"Britain", "Trinsic"};
            v.push_back(std::move(p));
        }

        // --- 16. Merchant / Tinker -----------------------------------------
        //
        // EC-03, "Player Vendor Operator" (compendium v1, section 15): "Any
        // production/gather build can specialize in stocking a player
        // vendor... Revolution's Vendor Cooperative makes this especially
        // relevant." That describes a BEHAVIOR any life can adopt, not a
        // skill pair, so this entry earns the "Merchant" aspiration the way
        // Production.cpp's own comment frames Tinkering: "the craft the
        // other crafts depend on: it makes the tailor's kit, the scribe's
        // pen, the alchemist's keg and the miner's pickaxe." A Tinker who
        // supplies every OTHER profession's tools is the closest thing to a
        // market maker this catalogue's evidence actually supports.
        //
        // [NEWBIE TINKERING] is EMPTY (sp_tm_newbie.scp) and [NEWBIE MINING]
        // hands over only a pickaxe -- the same STR-50-to-wield gate as
        // `miner_smith` (skill45_mining.scp @PreStart, ReqStr=50), but this
        // life has no tongs and no ingots either. Its real first act is a
        // shopping trip: VENDOR_S_TINKER sells i_tinker_tools outright
        // (tm_vend.scp:910, SELL={4 34}), so the empty kit is a real
        // bootstrap cost, not a blocker -- gold buys the fix.
        {
            Profession p;
            p.id = "merchant_tinker";
            p.label = "Merchant / Tinker";
            p.startSkillA = rules::kTinkering;
            p.startSkillB = rules::kMining;
            p.startStr = 30; p.startDex = 10; p.startInt = 10;
            p.targets = {
                {rules::kTinkering, 1000, 2, false, SkillRole::Primary},
                // Held exactly at the 50.0 creation grants: this life buys
                // its ingots from a miner/smith rather than becoming one --
                // see `gathers`.
                {rules::kMining,     500, 1, false, SkillRole::Utility},
            };
            p.unresolvedTenths = 5500;
            p.targetStr = 100; p.targetDex = 50; p.targetInt = 75;
            // Faucets.cpp refuses NPC buyback for tinker output outright
            // (tinker_output_to_vendor, Policy::RefusePlayerMarket) -- the
            // most player-market-dependent income in the whole catalogue.
            p.income = {Income::Craft};
            // The defining trait: it gathers NOTHING. Every raw input is
            // bought from someone who did.
            p.gathers = "";
            p.produces = {"i_gears", "i_lockpick", "i_tinker_tools",
                          "i_pickaxe", "i_scissors", "i_sewing_kit",
                          "i_pen_and_ink", "i_barrel_tap", "i_barrel_hoops",
                          "i_keg_potion"};
            p.consumes = {"i_ingot_iron"};
            p.tools = {{"tinker tools", {kTinkerTools}, false},
                       {"pickaxe",      V(kPickaxe, 2), true}};
            p.consumables = {Bandages(), Food()};
            p.riskTolerance = 0.30;
            p.goldReserve = 250;         // spends rather than hoards; that IS the business
            p.homeCities = {"Britain", "Minoc"};
            v.push_back(std::move(p));
        }

        // --- 17. Scribe ------------------------------------------------------
        //
        // CR-05, "Inscriber / Runebook Maker" (compendium v1, section 10):
        // "INS high, MAGERY required, travel/economy support." The `mage`
        // entry above already carries Inscription, but only as a capped
        // Utility hobby (500 tenths) -- it never reaches the skill levels
        // Production.cpp's own scroll ladder requires past Recall
        // (Inscription 70.0/Magery 60.0 for Gate Travel, 80.0/70.0 for
        // Resurrection). This entry inverts the emphasis so those tiers are
        // actually reachable, which is what makes it a different life and
        // not the mage restated.
        //
        // Blank scrolls stay a CARPENTRY product on this runtime, not an
        // Inscription one (Production.cpp: i_scroll_blank, SKILLMAKE=
        // Carpentry 25.7) -- so this life buys its own raw material from the
        // lumberjack/carpenter, the same cross-profession link the mage
        // entry already documents as a known gap.
        //
        // [NEWBIE INSCRIPTION] hands over 2 blank scrolls and a book;
        // [NEWBIE MAGERY] the same no-reagent spellbook the mage and the
        // warlock both start with.
        {
            Profession p;
            p.id = "scribe";
            p.label = "Scribe";
            p.startSkillA = rules::kInscription;
            p.startSkillB = rules::kMagery;
            p.startStr = 10; p.startDex = 10; p.startInt = 30;
            p.targets = {
                {rules::kInscription, 1000, 3, false, SkillRole::Primary},
                // Pushed to 100.0, not capped like the mage's own Magery
                // dabbling -- Gate Travel and Resurrection scrolls are
                // unreachable below Magery 60.0/70.0, and reaching them is
                // this build's entire reason to exist.
                {rules::kMagery,      1000, 2, false, SkillRole::Secondary},
                {rules::kMeditation,   400, 1, true,  SkillRole::Utility},
            };
            p.unresolvedTenths = 4600;
            p.targetStr = 30; p.targetDex = 20; p.targetInt = 100;
            // scroll_to_mage_shop / scroll_recall_to_mage_shop are the only
            // two rows Faucets.cpp allows (LIVE PROVEN for the first). Gate
            // Travel and Resurrection scrolls are not in the registry at
            // all -- UNKNOWN whether an NPC buys them; treat that half of
            // `produces` as player-market only until audited.
            p.income = {Income::Craft};
            p.gathers = "";
            p.produces = {"i_scroll_poison", "i_scroll_recall",
                          "i_scroll_gate_travel", "i_scroll_resurrection"};
            p.consumes = {"i_scroll_blank", "i_reag_nightshade",
                          "i_reag_black_pearl", "i_reag_blood_moss",
                          "i_reag_mandrake_root", "i_reag_garlic",
                          "i_reag_ginseng", "i_reag_sulfur_ash"};
            p.tools = {{"spellbook", {kSpellbook}, false}};
            p.consumables = {Food()};
            p.riskTolerance = 0.20;
            p.goldReserve = 900;         // the widest reagent basket in the catalogue
            p.homeCities = {"Moonglow", "Britain"};
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
