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
// Every heal potion is ID=i_bottle_yellow; the client cannot tell which.
constexpr u16 kYellowPotion = 0x0F0C;
constexpr u16 kBandage    = 0x0E21;
constexpr u16 kFishingPole[] = {0x0DBF, 0x0DC0};
// THE COOKING TOOL IS A TYPE TOO. i_fish_cut_cooked demands SKILLMAKE=Cooking
// 0.0,t_cooking, and the three t_cooking itemdefs are i_fry_pan (0x097F),
// i_flour_sifter (0x103E) and i_rolling_pin (0x1043). Any of them satisfies
// the shard; the rolling pin is the one an NPC sells (the stock-Sphere baker
// row, restored to the runtime tm_vend.scp), so it leads the list the same
// way the buyable half of the smith kit does.
constexpr u16 kCookingTool[] = {0x1043, 0x097F, 0x103E};
constexpr u16 kMortar     = 0x0E9B;             // i_mortar_pestle
constexpr u16 kBottle     = 0x0F0E;             // i_bottle_empty
constexpr u16 kSpellbook  = 0x0EFA;             // i_spellbook
// THE SMITH TOOL IS A TYPE, NOT AN ITEM. Blacksmithing is gated on
// TYPE=t_weapon_mace_smith, and i_tongs (0FBB/0FBC) and i_hammer_smith
// (013E3, with 013E4 as its DUPEITEM flip) both carry it -- so either one in
// hand satisfies the shard. Listing only the tongs meant a character holding
// a perfectly good smith hammer still reported itself toolless and went
// shopping. "you need smith hammer for blacksmith ... I think tinker sells
// it" (project owner, 2026-08-29) -- and VENDOR_S_TINKER does sell all of
// i_tongs, i_hammer, i_hammer_smith and i_hammer_sledge.
constexpr u16 kSmithTool[] = {0x0FBB, 0x0FBC, 0x13E3, 0x13E4};
// The half of that set a character can actually WIELD. The blacksmith menu
// opens from LAYER_HAND1 and i_tongs is not a weapon -- no DAM, no SKILL -- so
// the server refuses to put it in a hand. A smith needs the hammer itself.
constexpr u16 kSmithHammer[] = {0x13E3, 0x13E4};
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
// (Now in rules.h with every other skill id -- see the note there.)
using rules::kParrying;
using rules::kArchery;
using rules::kMaceFighting;
using rules::kFencing;
using rules::kWrestling;

std::vector<u16> V(const u16* p, usize n) { return std::vector<u16>(p, p + n); }

ConsumableNeed Bandages() {
    ConsumableNeed c;
    c.name = "bandage";
    c.graphics = {kBandage};
    c.low = 8;
    c.restockTo = 30;
    return c;
}

// HEAL POTIONS -- a fighting life's other way of staying alive.
//
// The DrinkPotion tactic has existed and been unit-tested since M3.9.1
// (CombatPolicy.cpp: healPotions > 0 -> Tactic::DrinkPotion), and it has never
// once fired, because NO PROFESSION EVER ASKED FOR POTIONS. Only Bandages()
// and Food() were ever declared, so healPotions was permanently zero. That is
// the fifth "built but unreachable" found on 2026-08-29 alone.
//
// All three strengths share ID=i_bottle_yellow and TYPE=t_potion
// (items/i_provisions_potions.scp:282-304), so the CLIENT CANNOT TELL a heal
// potion from any other yellow bottle by graphic. The count is therefore
// optimistic and DrinkPotion is attempted rather than assumed to work -- which
// is exactly why SurvivalTick attempts it.
//
// AND IT MUST BE BOUGHT FROM A PLAYER. Faucets.cpp records this as CONFIRMED
// Revolution history: on 18.08.2009 potions stopped being sold by NPCs,
// deliberately, and demand was large -- a 2008 warrior describes carrying
// 150 Deadly Poison, 80 Heal and 80 Cure in kegs. So a warrior wanting heal
// potions is not a shopping errand, it is the alchemist's customer, and this
// is what makes the fighter/alchemist pair real rather than theoretical.
// A CRAFTER'S ONLY WAY TO HEAL ITSELF.
//
// "you are crafter you dont have heal skill so buy healing potion 3-4"
// (project owner, 2026-08-30). A bandage is worth what your Healing skill
// says it is worth, and a miner-smith has none -- so the Bandages() every
// crafter carried were close to decoration. A potion asks nothing of the
// drinker.
//
// Smaller than the warrior's eight: this is for surviving the walk to a
// healer, not for standing in a fight.
//
// And it REPLACES Bandages() for these lives rather than joining it -- "so
// crafter do not buy bandages" (project owner). Buying a consumable your
// skills cannot use is gold spent on nothing, and it was crowding out the
// thing that would actually have worked.
ConsumableNeed CrafterHealPotions() {
    ConsumableNeed c;
    c.name = "heal potion";
    c.graphics = {kYellowPotion};
    c.low = 2;
    c.restockTo = 4;
    return c;
}

ConsumableNeed HealPotions() {
    ConsumableNeed c;
    c.name = "heal potion";
    c.graphics = {kYellowPotion};
    // Deliberately modest. DERIVED, not documented: no archive source states
    // what a Revolution warrior carried on an ordinary graveyard trip, and the
    // keg numbers above are a large battle, not a Tuesday.
    c.low = 2;
    c.restockTo = 8;
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
            // Fights as: melee -- a swordsman closes and stays there.
            p.combatStrategy = life::CombatStrategyId::Melee;
            // Wears: Metal. a swordsman in the open takes the heaviest armour its STR allows, and a shield hand is free
            p.wears = Profession::Wear::Metal;
            p.maysShield = true;
            p.label = "Lumberjack / Carpenter";
            p.startSkillA = rules::kLumberjacking;
            p.startSkillB = rules::kSwordsmanship;
            // DEX-weighted: six characters died on a STR-heavy split and one
            // survived on a DEX-heavy one (M4 plan). Same lesson, 50 points.
            p.startStr = 50; p.startDex = 25; p.startInt = 5;
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
            // i_parchment belongs HERE, not in consumes. A blank scroll is
            // made from parchment and parchment is made from a log
            // (Production.cpp: i_parchment <- {i_log 1}, Carpentry 25.7) --
            // both by this life's own hands, from the wood it cuts itself. It
            // still has to be DECLARED, because obs.pack counts only what a
            // profession produces or consumes and an unlisted item reads as
            // zero; but declaring it as consumed would have made the
            // deliberately self-sufficient lumberjack look dependent on
            // somebody else, which it is not.
            p.produces = {"i_log", "i_board", "i_parchment", "i_scroll_blank",
                          "i_club"};
            p.tools = {{"hatchet", V(kHatchet, 2), true}};
            p.consumables = {Bandages(), HealPotions(), Food()};
            p.riskTolerance = 0.55;
            // "tarath it should have 10K + to buy bandages" (owner,
            // 2026-08-29). 300 was one trainer fee and nothing else, so this
            // life spent down to nothing and then could not equip itself for
            // the fights that pay for everything. A fighter's reserve is its
            // ability to walk back into a graveyard.
            p.goldReserve = 10000;
            // NOT YEW. "we dont use yew as starting or hometown" (project
            // owner, 2026-08-30). Yew is the obvious forest, but the shard's
            // Yew bank at 652,820 is not reachable to the pathfinder, so a
            // life based there loses whole errands to "no path" -- and a
            // gatherer takes homeCities.front() as its home, so Yew first
            // meant Yew always. Britain and Skara Brae both have woods within
            // reach and banks that work.
            p.homeCities = {"Britain", "Skara Brae"};
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
            // Fights as: avoidcombat -- no weapon skill in the build at all.
            p.combatStrategy = life::CombatStrategyId::AvoidCombat;
            // Wears: Metal. it makes the plate; it is not going to refuse to wear it
            p.wears = Profession::Wear::Metal;
            p.maysShield = true;
            // Tinkering starts at literally 0.0: its [NEWBIE] section is empty
            // (sp_tm_newbie.scp:545), so the slot costs nothing.
            p.startZeroSkill = rules::kTinkering;
            p.label = "Miner / Blacksmith";
            p.startSkillA = rules::kMining;
            p.startSkillB = rules::kBlacksmithing;
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
            p.targets = {
                // Secondary by ROLE -- a build may declare exactly one
                // Primary and Mining is it -- but ahead of everything else by
                // PRIORITY, which is the field that actually orders what gets
                // trained next.
                {rules::kBlacksmithing, 1000, 5, false, SkillRole::Secondary},
                {rules::kMining,        1000, 4, false, SkillRole::Primary  },
                {rules::kTinkering,      500, 2, true,  SkillRole::Utility},
                {rules::kArmsLore,       500, 1, true,  SkillRole::Utility},
            };
            p.unresolvedTenths = 4000;
            p.targetStr = 100; p.targetDex = 45; p.targetInt = 80;
            p.income = {Income::Craft, Income::Process, Income::Gather};
            p.gathers = "ore";
            // "go mine then forge then make dagger and sell them" (owner,
            // 2026-08-29). The dagger is what makes this life's loop close:
            // ingots are a player-market good with no NPC buyer and short
            // spears are refused on authenticity, so before this the character
            // mined, smithed, and then stood holding goods nobody would take
            // -- BLOCKED_NEED EARN_GOLD "carrying its own output with nobody
            // known to buy it", every tick of a session.
            //
            // i_dagger is SKILLMAKE=Blacksmithing 0.0, so it is makeable from
            // the first minute, and VENDOR_B_WEAPONS_BLADED buys it.
            // i_cutlass is the SWORDSMAN'S order (S4). The defname comes from
            // this shard's own blacksmithing menu -- def_blacksmithing.scp:173
            // blacksmithing_category_6_4 "i_cutlass" -- and its ITEMDEF is
            // TYPE=t_weapon_sword SKILL=Swordsmanship, 8 ingots,
            // SKILLMAKE=Blacksmithing 24.3 (i_weapons.scp:1221-1240). The
            // dagger above is NOT a sword: it is TYPE=t_weapon_fence
            // SKILL=Fencing (:496-508), so nothing the smith made before this
            // was anything a lumberjack_swordsman could use.
            // APPENDED, not inserted: produces.front() is load-bearing in
            // tests/m7_market.cpp:172.
            p.produces = {"i_dagger", "i_ingot_iron", "i_spear_short",
                          "i_cutlass"};
            // Ore comes out of the world, but the SPEAR does not: it is
            // 6 ingots plus one LOG (Production.cpp:107, script SKILLMAKE
            // Blacksmithing 45.3). That single log is the first real
            // producer-to-consumer link in the catalogue -- the smith cannot
            // make it without something a lumberjack pulls out of a tree.
            p.consumes = {"i_ore_iron", "i_log"};
            // The pickaxe is listed even though a new character cannot lift it:
            // the need model must SAY that, not hide it.
            p.tools = {{"pickaxe", V(kPickaxe, 2), true},
                       {"smith hammer", V(kSmithHammer, 2), false}};
            p.consumables = {CrafterHealPotions(), Food()};
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
            // Fights as: mage -- keeps the distance, watches its mana.
            p.combatStrategy = life::CombatStrategyId::Mage;
            // Wears: Cloth. metal ends casting on this shard and a shield hand is a spell hand
            p.wears = Profession::Wear::Cloth;
            p.maysShield = false;
            // Evaluating Intelligence has an empty [NEWBIE] section
            // (sp_tm_newbie.scp:376), so a mage may start it at literally 0.0.
            p.startZeroSkill = rules::kEvaluatingIntel;
            p.label = "Pure Mage";
            p.startSkillA = rules::kMagery;
            p.startSkillB = rules::kMeditation;
            // HIGH INT, and enough STR/DEX not to be useless. A pure mage
            // lives on its mana pool; STR is what stops it dying to one hit
            // and carrying nothing, DEX is what lets it walk away.
            // Every new life starts at the practical 50 STR baseline.  STR is
            // slow to earn and gates carrying, armour and basic survival;
            // this mage spends the remaining creation points on Intelligence.
            p.startStr = 50; p.startDex = 5; p.startInt = 25;
            p.targets = {
                // THREE SKILLS. No melee plan, no crafting plan, no hybrid
                // thinking (owner's pure-mage spec, 2026-08-29). Inscription
                // used to sit here at 50.0 and it was scribe thinking wearing
                // a mage's robe: it gave the build a crafting income and a
                // reason to stand in a shop, which is a different life.
                {rules::kMagery,          1000, 5, false, SkillRole::Primary},
                {rules::kMeditation,      1000, 4, false, SkillRole::Secondary},
                {rules::kEvaluatingIntel, 1000, 3, true,  SkillRole::Secondary},
            };
            // 300.0 resolved across three skills leaves 400.0 UNSPENT, and
            // that is deliberate rather than an oversight: a pure mage is
            // three skills, and the rest of the budget stays open until
            // there is evidence about what Revolution mages actually took
            // with it. Unresolved is a first-class value in this project.
            p.unresolvedTenths = 4000;
            p.targetStr = 25; p.targetDex = 25; p.targetInt = 100;
            // LOOT, not crafting. The day-one economy is starting reagents
            // -> graveyard kills -> loot -> sell -> gold -> more reagents,
            // and never gold -> reagents first, because a new mage has
            // reagents and no gold.
            p.income = {Income::Hunt};
            // What a mage actually MAKES. Blank scrolls are Carpentry 25.7
            // (Production.cpp:123) -- wood, not magic -- so listing them as
            // the mage's output was backwards. It makes SPELL scrolls out of
            // them.
            // NOTHING. A pure mage makes nothing; it kills things and sells
            // what they drop.
            //
            // THIS EXPOSES A REAL GAP, recorded rather than papered over:
            // the sell path asks market::Surplus, which only ever considers
            // `produces`. LOOT IS NOT PRODUCED, so a mage with a pack full
            // of graveyard drops has nothing the economy layer recognises
            // as sellable. The same gap blocks a warrior selling the gear
            // it loots. See docs/M6.md, "Loot is income and the model has
            // no word for it".
            p.produces = {};
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
            // A HEAL IT CAN ACTUALLY REACH. This read {Food()} alone, and a
            // pure mage has no Healing skill in `targets`, no bandages and no
            // potions -- while Runner::DoHeal hardwires see.canCastHeal = false
            // (Runner.cpp, "UNKNOWN: this does not prove *Heal* is in the
            // spellbook"), so this life had NO self-heal of any kind. Casting is
            // R2's work; until then the potion is the honest stopgap, and it is
            // the same fix the crafters already carry. HealPotions() rather than
            // CrafterHealPotions() because this life HUNTS for its income (the
            // graveyard, p.income = Hunt) -- eight is the fighting number, four
            // is the walk-to-a-healer number. Of the six Mage-strategy families
            // only this one lacked a heal: warlock and treasure_hunter carry
            // Healing skill AND bandages, alchemist / mage_blacksmith / scribe
            // already carry CrafterHealPotions(). (audit 2026-08-30, finding 2.)
            p.consumables = {HealPotions(), Food()};
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
            // Fights as: mage -- casts what little it has rather than trading blows.
            p.combatStrategy = life::CombatStrategyId::Mage;
            // Wears: Cloth. a brewer that also casts; robes only
            p.wears = Profession::Wear::Cloth;
            p.maysShield = false;
            // Meditation begins at LITERALLY 0.0, not at the 0.0-19.9 the
            // server rolls for every other skill. It has no [NEWBIE] section
            // at all, so claiming the third creation slot for it costs the
            // character nothing it did not earn -- and gives it a skill it
            // must buy or grind up from nothing.
            p.startZeroSkill = rules::kMeditation;
            p.label = "Alchemist";
            p.startSkillA = rules::kAlchemy;
            p.startSkillB = rules::kMagery;
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
            p.targets = {
                {rules::kAlchemy,     1000, 5, false, SkillRole::Primary},
                {rules::kMagery,       500, 3, false, SkillRole::Utility},
                {rules::kMeditation,   500, 2, true,  SkillRole::Utility},
            };
            p.unresolvedTenths = 4500;
            p.targetStr = 50; p.targetDex = 25; p.targetInt = 100;
            // THE TRAINING IS THE PRODUCTION. An alchemist does not grind
            // Alchemy and then separately make stock to sell -- every practice
            // batch comes out of the mortar as real potions. So the loop is:
            // train, keep what this life needs, sell the excess to PLAYERS,
            // and spend that on the next reagents and bottles.
            //
            // Revolution made the player half explicit rather than incidental:
            // on 18.08.2009 Night Sight potions stopped being sold by NPCs,
            // and a 2008 warrior describes carrying about 150 Deadly Poison,
            // 80 Heal and 80 Cure in kegs into a large battle. Every PvPer,
            // PvMer, tamer, mage and warlock is a standing customer. TNS
            // scripts an NPC buyback instead (VENDOR_B_ALCHEMIST); that is
            // TNS's model, not this one -- see the faucet registry row.
            p.income = {Income::Craft};
            // Bottles are NOT bought: i_bottle_empty is Alchemy 25.0 with a
            // glassblowing tool (Production.cpp:178), so an alchemist makes
            // its own. It buys reagents and nothing else.
            // HEAL AND GREATER HEAL, which are what a fighting life actually
            // buys. Both recipes already existed in the production graph and
            // the alchemist simply never claimed them:
            //   i_potion_heal      ALCHEMY 15.1, 3 ginseng + 1 empty bottle
            //   i_potion_healgreat ALCHEMY 55.1, 7 ginseng + 1 empty bottle
            // (Production.cpp:196,205; runtime i_provisions_potions.scp:293).
            //
            // 15.1 matters: a brand-new alchemist can make heal potions almost
            // at once, so the alchemist -> warrior chain is viable on day one
            // rather than after a long grind. And Revolution CONFIRMED potions
            // as a player-market good -- NPCs stopped selling them on
            // 18.08.2009 -- so this is a producer whose only customers are
            // other characters, which is what R4 needs.
            // Poison added 2026-08-29 on the owner's instruction: it is what
            // this life can make and sell to an NPC, and Poisoning is a skill
            // the fighters buy the product of. See the flagged conflict in
            // Faucets.cpp poison_potion_to_alchemist.
            // STRONGEST FIRST, so the ladder is climbed rather than ignored.
            // ChooseCraft takes the first entry whose skill is met AND whose
            // inputs are all present, so ORDER is the ladder: an alchemist at
            // 90.1 makes Deadly Poison, at 55.1 Greater, at 15.1 plain, and a
            // brand-new one still has Lesser at 0. Listing plain Poison first
            // meant a master alchemist brewed the beginner's potion forever.
            // "use the ladder" (project owner, 2026-08-30).
            p.produces = {"i_potion_poisondeadly", "i_potion_poisongreat",
                          "i_potion_poison", "i_potion_poisonless",
                          "i_potion_heal", "i_potion_healgreat",
                          "i_potion_refresh", "i_potion_cure"};
            // EVERY REAGENT ITS RECIPES ACTUALLY NAME. i_reag_nightshade was
            // missing, and it is the one poison is made of
            // (Production.cpp: i_potion_poison <- {i_reag_nightshade 2,
            // i_bottle_empty 1}; the greater poison wants 8).
            //
            // This list is not documentation: Runner.cpp builds obs.pack by
            // counting ONLY what a profession `produces` or `consumes`, so an
            // unlisted input reads as ZERO however many are in the backpack.
            // Voris bought nightshade ten at a time, three times over, and was
            // still told "i_potion_poison needs 10 x i_reag_nightshade" until
            // his purse was empty -- the same shape as i_kindling, which read
            // as zero for every fisher until it was listed.
            p.consumes = {"i_reag_black_pearl", "i_reag_garlic",
                          "i_reag_ginseng", "i_reag_nightshade",
                          "i_bottle_empty"};
            p.tools = {{"mortar", {kMortar}, false}};
            p.consumables = {CrafterHealPotions(), Food()};
            p.riskTolerance = 0.25;
            p.goldReserve = 600;
                        // Reagents and a mortar; Britain has the deepest reagent supply.
            // Confirmed by the owner 2026-08-29: "alchemist moonglow britain jhelom
            // magicinia all good". An alchemist gathers nothing, so it keeps the
            // hashed spread across these four rather than being pinned to one.
            p.homeCities = {"Moonglow", "Britain", "Jhelom", "Magincia"};
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
            // Fights as: avoidcombat -- a crafter's answer to a fight is to leave.
            p.combatStrategy = life::CombatStrategyId::AvoidCombat;
            // Wears: Cloth. "for crafter upgrade gear just wear normal clothing for now" (owner)
            p.wears = Profession::Wear::Cloth;
            p.maysShield = false;
            p.label = "Fisher";
            // Fishing is the PRIMARY and leads, which is what the paperdoll
            // title reads. Tested both orders live: the kit outcome is the
            // same either way, so the ordering is a build decision rather
            // than a workaround. See docs/M7.md on the missing pole.
            p.startSkillA = rules::kFishing;
            p.startSkillB = rules::kCooking;
            // Fishing pulls against STR for the catch and the carry; cooking
            // needs nothing. A fisher is not a fighter and does not pretend.
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
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
            // The pole catches; the rolling pin cooks. i_fish_cut_cooked is
            // SKILLMAKE=Cooking 0.0,t_cooking, so without a t_cooking item in
            // the pack the profession's whole cooking half -- and the 5gp the
            // cook pays for a cooked steak against 2gp raw -- is unreachable.
            // Any of the three t_cooking graphics satisfies the shard; the
            // GET_TOOL errand buys the rolling pin from a baker.
            p.tools = {{"fishing pole", V(kFishingPole, 2), true},
                       {"rolling pin",  V(kCookingTool, 3), false}};
            // Kindling is bought, never made: the fire Skill_Cooking demands
            // is lit from it on the dock. Named here so the pack counter
            // counts it -- obs.pack is built from produces + consumes, and an
            // uncounted input reads as forever missing.
            p.consumes = {"i_kindling"};
            p.consumables = {CrafterHealPotions(), Food()};
            // A FISHER HAD NO HOME AT ALL. homeCities was simply absent, so
            // Runner::Start skipped the whole selection block, homeCity stayed
            // empty, and character creation fell back to start location 0 --
            // Yew, which is inland. "why fisher started in yew come on"
            // (project owner, 2026-08-29). Quite.
            //
            // Chosen from where the fishing actually is, not from taste: the
            // atlas's 17 fishing PLACEs cluster on Skara Brae and Jhelom, with
            // docks at Vesper and Nujel'm. All four are start cities the shard
            // offers (map0_starts.scp), so a fisher is now born on the coast.
            //
            // It gathers, so it takes the FIRST of these rather than hashing.
            p.homeCities = {"Skara Brae", "Jhelom", "Vesper", "Britain"};

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
            // Fights as: tamer -- the pet fights; the tamer commands and heals it.
            p.combatStrategy = life::CombatStrategyId::Tamer;
            // Wears: Leather. UNKNOWN what Revolution's tamers wore; leather is the light-armour reading and it keeps casting available
            p.wears = Profession::Wear::Leather;
            p.maysShield = false;
            p.label = "Animal Tamer";
            p.startSkillA = rules::kTaming;
            p.startSkillB = rules::kAnimalLore;
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
            p.targets = {
                {rules::kTaming,     1000, 5, false, SkillRole::Primary},
                {rules::kAnimalLore, 1000, 4, false, SkillRole::Secondary},
                {rules::kVeterinary, 1000, 3, true,  SkillRole::Secondary},
            };
            p.unresolvedTenths = 4000;
            p.targetStr = 80; p.targetDex = 45; p.targetInt = 100;
            p.income = {Income::Hunt};
            p.consumables = {Bandages(), HealPotions(), Food()};
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
        // [NEWBIE FENCING] hands over i_kryss.  Tactics deliberately has no
        // newbie weapon: a character's combat school, not its supporting
        // skill, decides the weapon it begins with.
        {
            Profession p;
            p.id = "fencer";
            // Fights as: melee -- one-handed and in your face.
            p.combatStrategy = life::CombatStrategyId::Melee;
            // Wears: Metal. a dexxer, and fencing is one-handed
            p.wears = Profession::Wear::Metal;
            p.maysShield = true;
            p.label = "Fencer";
            p.startSkillA = kFencing;
            p.startSkillB = rules::kTactics;
            // A fencer is a warrior first.  The old 32/40/8 dexxer split
            // could not wear the ordinary early warrior kit, forcing the
            // life to postpone armour until stat gains arrived.  Spend the
            // same legal 80 creation points as 50/25/5: enough STR for the
            // starter weapon and real armour, with practical DEX remaining.
            p.startStr = 50; p.startDex = 25; p.startInt = 5;
            p.targets = {
                {kFencing,             1000, 6, false, SkillRole::Primary},
                {rules::kTactics,      1000, 5, false, SkillRole::Secondary},
                {rules::kAnatomy,      1000, 4, false, SkillRole::Secondary},
                {rules::kHealing,      1000, 3, false, SkillRole::Secondary},
                {rules::kPoisoning,    1000, 2, false, SkillRole::Secondary},
                {kParrying,            1000, 1, false, SkillRole::Secondary},
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
            p.consumables = {Bandages(), HealPotions(), Food()};
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
            // Fights as: melee -- the same, with a shield.
            p.combatStrategy = life::CombatStrategyId::Melee;
            // Wears: Metal. a dexxer, and the shield is part of the school
            p.wears = Profession::Wear::Metal;
            p.maysShield = true;
            p.label = "Macer";
            p.startSkillA = kMaceFighting;
            p.startSkillB = rules::kAnatomy;
            p.startStr = 50; p.startDex = 25; p.startInt = 5;
            p.targets = {
                {kMaceFighting,        1000, 6, false, SkillRole::Primary},
                {rules::kTactics,      1000, 5, false, SkillRole::Secondary},
                {rules::kAnatomy,      1000, 4, false, SkillRole::Secondary},
                {rules::kHealing,      1000, 3, false, SkillRole::Secondary},
                {kParrying,            1000, 2, false, SkillRole::Secondary},
                {rules::kPoisoning,     500, 1, false, SkillRole::Utility},
            };
            p.unresolvedTenths = 1500;
            p.targetStr = 100; p.targetDex = 75; p.targetInt = 50;
            p.income = {Income::Hunt};
            // Armor/stamina pressure is a slow fight, and a slow fight is
            // won on Cure potions, not poison -- the alchemist's product
            // again, a different one than the Fencer buys.
            p.consumes = {"i_potion_cure"};
            p.consumables = {Bandages(), HealPotions(), Food()};
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
            // Fights as: ranged -- the bow needs both hands, so distance IS the tactic.
            p.combatStrategy = life::CombatStrategyId::Ranged;
            // Wears: Leather. the bow needs BOTH hands, so never a shield. Leather rather than plate is the classic archer reading and is UNKNOWN for Revolution specifically
            p.wears = Profession::Wear::Leather;
            p.maysShield = false;
            p.label = "Archer";
            p.startSkillA = kArchery;
            p.startSkillB = rules::kBowcraft;
            p.startStr = 50; p.startDex = 25; p.startInt = 5;
            p.targets = {
                {kArchery,             1000, 5, false, SkillRole::Primary},
                {rules::kBowcraft,     1000, 4, false, SkillRole::Secondary},
                {rules::kTactics,      1000, 3, false, SkillRole::Secondary},
                {rules::kAnatomy,       500, 2, false, SkillRole::Utility},
                {rules::kHealing,       500, 1, false, SkillRole::Utility},
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
            p.consumables = {Bandages(), HealPotions(), Food()};
            p.riskTolerance = 0.50;      // kites at range, disengages if closed on
            p.goldReserve = 300;
            // Yew is the forest CR-07 names as the special-log source; Skara
            // Brae and Trinsic sit within bow range of it.
            // Yew removed on the owner's instruction, 2026-08-30.
            p.homeCities = {"Skara Brae", "Trinsic", "Britain"};
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
            // Fights as: mage -- Magery is the primary; the sword is the fallback.
            p.combatStrategy = life::CombatStrategyId::Mage;
            // Wears: Cloth. casts; same rule as the mage
            p.wears = Profession::Wear::Cloth;
            p.maysShield = false;
            // Meditation begins at LITERALLY 0.0, not at the 0.0-19.9 the
            // server rolls for every other skill. It has no [NEWBIE] section
            // at all, so claiming the third creation slot for it costs the
            // character nothing it did not earn -- and gives it a skill it
            // must buy or grind up from nothing.
            p.startZeroSkill = rules::kMeditation;
            p.label = "Warlock";
            p.startSkillA = rules::kMagery;
            p.startSkillB = rules::kSwordsmanship;
            // The hybrid still needs a real body on day one; its remaining
            // points preserve a useful mana pool rather than weak STR.
            p.startStr = 50; p.startDex = 10; p.startInt = 20;
            p.targets = {
                {rules::kMagery,          1000, 8, false, SkillRole::Primary},
                {rules::kSwordsmanship,   1000, 7, false, SkillRole::Secondary},
                {rules::kTactics,         1000, 6, false, SkillRole::Secondary},
                {rules::kEvaluatingIntel,  800, 5, true,  SkillRole::Secondary},
                {rules::kPoisoning,        800, 4, false, SkillRole::Secondary},
                {rules::kHealing,          800, 3, false, SkillRole::Secondary},
                {rules::kAnatomy,          800, 2, false, SkillRole::Secondary},
                {rules::kMeditation,       800, 1, true,  SkillRole::Secondary},
            };
            p.unresolvedTenths = 0;      // WL-01 spends the entire 700, forum-exact
            p.targetStr = 90; p.targetDex = 90; p.targetInt = 45;
            p.income = {Income::Hunt};
            // A hybrid still needs the mage's reagents, just fewer of them --
            // this build never touches Inscription, so it buys finished
            // scrolls rather than blank ones.
            p.consumes = {"i_reag_black_pearl", "i_reag_nightshade"};
            p.consumables = {Bandages(), HealPotions(), Food()};
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
            // Fights as: melee -- closes, and means it.
            p.combatStrategy = life::CombatStrategyId::Melee;
            // Wears: Metal. kills players for a living and wears everything it can
            p.wears = Profession::Wear::Metal;
            p.maysShield = true;
            p.label = "Player Killer";
            p.startSkillA = rules::kSwordsmanship;
            p.startSkillB = rules::kPoisoning;
            // Same melee-warrior baseline as a fencer.  A PK who cannot wear
            // its early armour is not ready to leave town.
            p.startStr = 50; p.startDex = 25; p.startInt = 5;
            p.targets = {
                {rules::kSwordsmanship, 1000, 5, false, SkillRole::Primary},
                {rules::kTactics,       1000, 4, false, SkillRole::Secondary},
                {rules::kPoisoning,     1000, 3, false, SkillRole::Secondary},
                {rules::kHealing,        700, 2, false, SkillRole::Secondary},
                {kWrestling,             300, 1, false, SkillRole::Utility},
            };
            p.unresolvedTenths = 3000;
            p.targetStr = 60; p.targetDex = 100; p.targetInt = 65;
            p.income = {Income::Hunt};
            // Recall is the escape valve after a kill; deadly poison is the
            // finisher. Both are bought, never made -- this build has no
            // Magery or Alchemy of its own.
            p.consumes = {"i_scroll_recall", "i_potion_poisondeadly"};
            p.consumables = {Bandages(), HealPotions(), Food()};
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
            // Fights as: mage -- travels and survives by casting.
            p.combatStrategy = life::CombatStrategyId::Mage;
            // Wears: Leather. travels by Recall and casts to survive, so nothing metal
            p.wears = Profession::Wear::Leather;
            p.maysShield = false;
            // Meditation begins at LITERALLY 0.0, not at the 0.0-19.9 the
            // server rolls for every other skill. It has no [NEWBIE] section
            // at all, so claiming the third creation slot for it costs the
            // character nothing it did not earn -- and gives it a skill it
            // must buy or grind up from nothing.
            p.startZeroSkill = rules::kMeditation;
            p.label = "Treasure Hunter";
            p.startSkillA = rules::kLockpicking;
            p.startSkillB = rules::kCartography;
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
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
            // Fights as: mage -- the smithing half does not change how it fights.
            p.combatStrategy = life::CombatStrategyId::Mage;
            // Wears: Cloth. the smithing half does not change the fact that it casts
            p.wears = Profession::Wear::Cloth;
            p.maysShield = false;
            p.label = "Mage Blacksmith";
            p.startSkillA = rules::kBlacksmithing;
            p.startSkillB = rules::kMagery;
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
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
                       {"smith hammer", V(kSmithHammer, 2), false},
                       {"spellbook", {kSpellbook}, false}};
            p.consumables = {CrafterHealPotions(), Food()};
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
            // Fights as: avoidcombat -- tools, not weapons.
            p.combatStrategy = life::CombatStrategyId::AvoidCombat;
            // Wears: Cloth. "for crafter upgrade gear just wear normal clothing for now" (owner)
            p.wears = Profession::Wear::Cloth;
            p.maysShield = false;
            // As miner_smith: Tinkering grants no kit, so it can start at 0.0.
            p.startZeroSkill = rules::kTinkering;
            p.label = "Full Crafter";
            // RESOURCE SKILLS FIRST, not production skills. Owner's rule,
            // 2026-08-29: "a crafter with 50 Blacksmithy and no ore is
            // useless. A crafter with 50 Mining can immediately create his
            // own input stream."
            //
            // This started as Carpentry + Blacksmithing -- both PRODUCTION --
            // while listing i_ore_iron and i_log among the things it CONSUMES,
            // i.e. a crafter created unable to obtain either of its own
            // inputs. Mining and Lumberjacking are the two skills that turn
            // labour into materials, and everything else the build wants is
            // earned from there.
            //
            // UNKNOWN, deliberately not invented: the canonical Revolution 7x
            // crafter build. Mining / Blacksmithy / Lumberjacking / Carpentry /
            // Tailoring / Tinkering plus Alchemy or Inscription is the SHAPE,
            // and the exact seven needs old build evidence before it is
            // written down as fact.
            // MINING AND BLACKSMITHING FIRST, on the owner's instruction
            // 2026-08-29: "lets make bruin focus mining and blacksmithing
            // first".
            //
            // This also settles the tool problem at its source rather than
            // patching around it. Creation skills decide which [NEWBIE ...]
            // blocks a character is handed, and [NEWBIE BLACKSMITHING] gives
            // ITEMNEWBIE tongs, an ITEMNEWBIE pickaxe and 50 iron ingots --
            // which is exactly the tongs this build's p.tools asks for and
            // could not afford. With Lumberjacking as the second skill it got
            // a hatchet instead and was declared short of tongs from its first
            // minute.
            //
            // Mining still leads: it is the resource skill, and the rule for
            // this build is unchanged -- "a crafter with 50 Blacksmithy and no
            // ore is useless".
            p.startSkillA = rules::kMining;
            p.startSkillB = rules::kBlacksmithing;
            // STR 50 IS NOT A PREFERENCE, IT IS THE PICKAXE.
            //
            // i_pickaxe carries ReqStr=50 (items/weapons/i_weapons.scp:193)
            // and Source-X REFUSES the equip below it -- CanEquipStr,
            // CCharStatus.cpp:296-309, answering "Not strong enough to
            // equip". Mining needs a wielded tool, so at STR 40 this life
            // could not perform its own startSkillA on the day it was
            // created. That fault was introduced the same day Mining became
            // its creation skill, and it is exactly the trap the miner_smith
            // comment block already warned about.
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
            p.targets = {
                // The resource half of the build comes FIRST in priority --
                // ore and logs are what every other skill here consumes, and a
                // day spent raising Mining is a day the whole build gets more
                // capable, not a detour from crafting.
                {rules::kMining,        1000, 6, false, SkillRole::Primary},
                // Promoted above Lumberjacking and Carpentry: ore that is dug
                // and never smithed is just weight, and this pair is what the
                // character starts able to do.
                {rules::kBlacksmithing, 1000, 5, false, SkillRole::Secondary},
                {rules::kLumberjacking,  800, 4, false, SkillRole::Secondary},
                {rules::kCarpentry,     1000, 4, false, SkillRole::Secondary},
                {rules::kTinkering,      700, 3, true,  SkillRole::Secondary},
                {rules::kTailoring,      600, 2, true,  SkillRole::Utility},
                {rules::kAlchemy,        400, 1, true,  SkillRole::Utility},
            };
            // 550.0 resolved across seven skills, 150.0 left open. The
            // owner's shape is Mining / Blacksmithy / Lumberjacking /
            // Carpentry / Tailoring / Tinkering plus Alchemy or
            // Inscription -- which at seven skills is 100.0 each and
            // exactly the 700 budget. That symmetry is suggestive and is
            // NOT written down as fact: the canonical Revolution 7x build
            // needs old build evidence, so the remainder stays unresolved.
            p.unresolvedTenths = 1500;
            p.targetStr = 100; p.targetDex = 60; p.targetInt = 65;
            // Every one of these trades is refused as an NPC faucet
            // (Faucets.cpp: carpentry_output_to_vendor, smith_output_to_vendor,
            // alchemy_output_to_vendor are all Refuse*) -- the starkest
            // player-market-only life in the catalogue.
            p.income = {Income::Craft, Income::Process};
            // ORE FIRST. `gathers` is a single resource and this life needs
            // two, which is a real limit of the model rather than a choice:
            // a full crafter mines AND chops. Ore is named because smithing
            // is the deeper chain and the ingot stockpile is the day-one
            // objective; the wood side needs the model to grow a second
            // slot before it can be expressed honestly.
            p.gathers = "ore";
            p.produces = {"i_board", "i_dagger", "i_spear_short", "i_club",
                          "i_bottle_empty", "i_potion_cure", "i_sash"};
            // NOT ore and logs -- this life GATHERS those. What it must get
            // from someone else is the reagent side, and only once it has
            // the gold and supply access for Alchemy at all, which the
            // owner places on day two rather than day one.
            // AND THE MATERIALS ITS OWN RECIPES NAME. i_log and i_ingot_iron
            // were missing: this life GATHERS both, and the comment above
            // reasoned from that to leaving them out. But `consumes` is not a
            // shopping list -- Runner.cpp builds obs.pack from `produces` plus
            // `consumes` and nothing else, so an unlisted material reads as
            // ZERO whether it was bought, mined or chopped. A full crafter
            // could never have smithed: i_dagger and i_spear_short both want
            // i_ingot_iron, and i_board wants i_log.
            p.consumes = {"i_reag_garlic", "i_log", "i_ingot_iron",
                          "i_cloth", "i_thread"};
            // THE TOOLS THIS LIFE NEEDS TODAY COME FIRST.
            //
            // This listed only production tools -- tongs, saw, mortar -- while
            // the creation skills are Mining and Lumberjacking, so the newbie
            // kit hands over a pickaxe and a hatchet and the catalogue then
            // declares three DIFFERENT tools missing. Bruin spawned unable to
            // do the resource work he was designed around, and with 0 gold he
            // could not buy his way out: GET_TOOL blocked, CRAFT blocked,
            // EARN_GOLD with nothing to sell, and 85% of a session idling.
            //
            // The owner's own rule for this build says which way round it
            // goes -- "a crafter with 50 Blacksmithy and no ore is useless" --
            // so the gathering tools lead. The production tools stay, because
            // he does want them, but they are no longer the only thing he is
            // told he lacks while standing next to a forest with an axe.
            p.tools = {{"pickaxe", V(kPickaxe, 2), true},
                       {"hatchet", V(kHatchet, 2), true},
                       {"smith hammer", V(kSmithHammer, 2), false},
                       {"saw",     {kSaw},         false},
                       {"mortar",  {kMortar},      false}};
            p.consumables = {CrafterHealPotions(), Food()};
            p.riskTolerance = 0.35;
            p.goldReserve = 450;
            // Minoc first: this build's creation skills are Mining and Blacksmithing
            // and it gathers ore, so it lives where the ore is. It was left on
            // Britain when the build changed earlier today.
            // Yew removed on the owner's instruction, 2026-08-30.
            p.homeCities = {"Minoc", "Britain"};
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
            // Fights as: avoidcombat -- tools, not weapons.
            p.combatStrategy = life::CombatStrategyId::AvoidCombat;
            // Wears: Cloth. a crafter in ordinary clothes
            p.wears = Profession::Wear::Cloth;
            p.maysShield = false;
            // As miner_smith: Tinkering grants no kit, so it can start at 0.0.
            p.startZeroSkill = rules::kTinkering;
            p.label = "Tailor";
            p.startSkillA = rules::kTailoring;
            p.startSkillB = rules::kArmsLore;
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
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
            // AND EVERY MATERIAL ITS RECIPES NAME. obs.pack is built from
            // `produces` + `consumes` alone, so an undeclared input counts as
            // ZERO however many are carried -- a tailor could never have made
            // a robe or a sash, because the cloth and thread in her pack were
            // invisible to the code that checks for them.
            p.consumes = {"i_hides_cut", "i_yarn_ball", "i_cloth", "i_thread"};
            p.tools = {{"sewing kit", {kSewingKit}, false},
                       {"scissors",   {kScissors},  false}};
            p.consumables = {CrafterHealPotions(), Food()};
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
            // Fights as: avoidcombat -- tools, not weapons.
            p.combatStrategy = life::CombatStrategyId::AvoidCombat;
            // Wears: Cloth. a crafter in ordinary clothes
            p.wears = Profession::Wear::Cloth;
            p.maysShield = false;
            p.label = "Merchant / Tinker";
            p.startSkillA = rules::kTinkering;
            p.startSkillB = rules::kMining;
            // 48 was two points short of the pickaxe it declares as a
            // WIELDED tool (ReqStr=50). Mining is its second creation
            // skill, so it too was born unable to do it.
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
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
            // Same reason as the tailor's list above: a tinker's own recipes
            // name boards, bowls, needles, thread, yarn, feathers and ink, and
            // anything left out of this list reads as zero in the pack.
            p.consumes = {"i_ingot_iron", "i_board", "i_bowl_wood",
                          "i_feather", "i_ink_well", "i_sewing_needle",
                          "i_thread", "i_yarn_ball"};
            p.tools = {{"tinker tools", {kTinkerTools}, false},
                       {"pickaxe",      V(kPickaxe, 2), true}};
            p.consumables = {CrafterHealPotions(), Food()};
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
        // Blank scrolls are a CARPENTRY product on this runtime, not an
        // Inscription one (Production.cpp: i_scroll_blank, SKILLMAKE=
        // Carpentry 25.7).
        //
        // CORRECTED 2026-08-29: that used to end "so this life buys its own
        // raw material from the lumberjack/carpenter". It does not, and it
        // never did at runtime. SupplierTradeFor() sends blank scrolls to the
        // MAGE SHOP, which is where Ysolde actually buys them -- in the same
        // stop as her reagents, confirmed live: "supplies: the server took 30
        // gold for i_scroll_blank (purse 194 -> 164)" from Alenne the mage,
        // whose buy list carries blank scrolls at 6gp beside all eight
        // reagents. Who CRAFTS a thing and who SELLS it are different
        // questions; this comment answered the first and asserted the second.
        //
        // [NEWBIE INSCRIPTION] hands over 2 blank scrolls and a book;
        // [NEWBIE MAGERY] the same no-reagent spellbook the mage and the
        // warlock both start with.
        {
            Profession p;
            p.id = "scribe";
            // Fights as: mage -- casts what it scribes.
            p.combatStrategy = life::CombatStrategyId::Mage;
            // Wears: Cloth. casts, and sells what it scribes
            p.wears = Profession::Wear::Cloth;
            p.maysShield = false;
            // Meditation begins at LITERALLY 0.0, not at the 0.0-19.9 the
            // server rolls for every other skill. It has no [NEWBIE] section
            // at all, so claiming the third creation slot for it costs the
            // character nothing it did not earn -- and gives it a skill it
            // must buy or grind up from nothing.
            p.startZeroSkill = rules::kMeditation;
            p.label = "Scribe";
            p.startSkillA = rules::kInscription;
            p.startSkillB = rules::kMagery;
            p.startStr = 50; p.startDex = 20; p.startInt = 10;
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
            p.consumables = {CrafterHealPotions(), Food()};
            p.riskTolerance = 0.20;
            // A TRAINING FUND, NOT JUST A REAGENT FLOAT.
            //
            // "Ysolde should keep at least 2-3K gold so that he can train some
            // mage or train scribe again" (project owner, 2026-08-29). 900 was
            // sized for the widest reagent basket in the catalogue and nothing
            // else, so every copper above it went on supplies and the purse
            // never reached a lesson.
            //
            // A guildmaster teaches to 30.0 for 300 (sphere.ini NPCTrainCost=1
            // per tenth), and this life wants Inscription and Magery both --
            // so a few lessons plus a reagent basket is the shape of the
            // number. 2500 keeps a full basket AND several lessons in hand.
            // Raised again 2026-08-29: "ysolde first should make money at least
            // keep 5K gold". Lessons are the point -- a guildmaster teaches to
            // 30.0 for about 300 -- and she wants Inscription and Magery both,
            // so a five-thousand floor is several lessons plus a full reagent
            // basket rather than one lesson and an empty purse.
            p.goldReserve = 5000;
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
