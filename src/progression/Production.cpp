#include "uo/production.h"
#include "uo/rules.h"

#include <cstring>

namespace uo::prod {

namespace {

using rules::kAlchemy;
using rules::kArmsLore;
using rules::kBlacksmithing;
using rules::kBowcraft;
using rules::kCarpentry;
using rules::kCooking;
using rules::kInscription;
using rules::kMagery;
using rules::kMining;
using rules::kTailoring;
using rules::kTinkering;

constexpr i32 kNoSkill = -1;

bool Same(const char* a, const char* b) {
    if (!a || !b) return false;
    return std::strcmp(a, b) == 0;
}

// ---------------------------------------------------------------------------
// THE GRAPH
//
// Every row cites where its numbers came from. `ENGINE` means Source-X C++ and
// is the strongest kind of fact here -- it is what the server will actually do.
// `SCRIPT` means an itemdef or skill menu in runtime/scripts. `REVOLUTION`
// means a dated entry in the shard's own archive.
//
// Rows are only present when the runtime can actually perform them. Chains
// RevolutionUO had and this runtime does not -- fishing nets, Shell, S.O.S.,
// spirit of nitre, special robes, trapped pouches, golems, special logs -- are
// deliberately ABSENT rather than stubbed, because a planner that finds a
// recipe will believe it. They are catalogued in
// docs/REVOLUTION_PRODUCTION_CHAINS.md as known gaps.
// ---------------------------------------------------------------------------
const std::vector<Recipe>& Table() {
    static const std::vector<Recipe> kRecipes = {

    // --- textile ------------------------------------------------------------
    // Shearing takes any bladed weapon, NOT scissors and NOT a crook, and
    // yields exactly one pile. The sheep becomes CREID_SHEEP_SHORN and regrows
    // in WoolGrowthTime (sphere.ini: 30 minutes).
    {"i_wool", 1, Provenance::AnimalHarvested, Station::None, Tool::Blade,
     kNoSkill, 0, kNoSkill, 0, {}, "ENGINE CClientTarg.cpp:1878; sphere.ini:399 WoolGrowthTime=30"},

    // One pile of wool yields THREE balls of yarn, and there is no skill check
    // whatsoever -- it is a plain item-use.
    {"i_yarn_ball", 3, Provenance::WorldProcessed, Station::SpinningWheel, Tool::None,
     kNoSkill, 0, kNoSkill, 0, {{"i_wool", 1}}, "ENGINE CClientTarg.cpp:2066"},

    // Cotton yields SIX spools of thread. Note i_thread's own itemdef claims
    // RESOURCES=1 i_flax_bundle, but flax has no TYPE (it is t_normal, not
    // IT_COTTON) so the engine will not spin it -- that line is decorative.
    {"i_thread", 6, Provenance::WorldProcessed, Station::SpinningWheel, Tool::None,
     kNoSkill, 0, kNoSkill, 0, {{"i_cotton", 1}}, "ENGINE CClientTarg.cpp:2075"},

    // The loom accumulates four units in m_itLoom and then emits one bolt.
    // IT_YARN and IT_THREAD share the case, so either weaves.
    {"i_cloth_bolt", 1, Provenance::WorldProcessed, Station::Loom, Tool::None,
     kNoSkill, 0, kNoSkill, 0, {{"i_yarn_ball", 4}}, "ENGINE CClientTarg.cpp:2230"},

    // Cutting the bolt yields its RESOURCES: fifty cloth.
    {"i_cloth", 50, Provenance::WorldProcessed, Station::None, Tool::Scissors,
     kNoSkill, 0, kNoSkill, 0, {{"i_cloth_bolt", 1}}, "ENGINE CItem.cpp:4287; SCRIPT i_cloth_bolt RESOURCES=50 i_cloth"},

    // Tailoring proper. The sewing kit is used ON cloth to open sm_tailor_cloth.
    {"i_sash", 1, Provenance::PlayerCrafted, Station::None, Tool::SewingKit,
     kTailoring, 45, kNoSkill, 0, {{"i_cloth", 4}, {"i_thread", 1}},
     "SCRIPT sm_cloth_misc; i_sash SKILLMAKE=Tailoring 4.5"},
    {"i_robe", 1, Provenance::PlayerCrafted, Station::None, Tool::SewingKit,
     kTailoring, 590, kNoSkill, 0, {{"i_cloth", 16}, {"i_thread", 1}},
     "SCRIPT i_robe SKILLMAKE=Tailoring 59.0"},

    // --- leather ------------------------------------------------------------
    {"i_hide", 12, Provenance::PvmDrop, Station::None, Tool::Blade,
     kNoSkill, 0, kNoSkill, 0, {}, "ENGINE CCharUse.cpp:49 Use_CarveCorpse; SCRIPT c_cow_brown RESOURCES=8 i_ribs_raw,12 i_hide"},
    {"i_hides_cut", 1, Provenance::WorldProcessed, Station::None, Tool::Scissors,
     kNoSkill, 0, kNoSkill, 0, {{"i_hide", 1}}, "ENGINE CClientTarg.cpp:2159 (IT_HIDE -> TDATA1)"},
    {"i_leather_tunic", 1, Provenance::PlayerCrafted, Station::None, Tool::SewingKit,
     kTailoring, 705, kArmsLore, 100, {{"i_hides_cut", 12}},
     "SCRIPT sm_leather_armor; SKILLMAKE=Tailoring 70.5,Armslore 10.0"},

    // --- mining and smithing ------------------------------------------------
    // Iron is 50.0 of ~99.9 in r_default_rock; a swing yields 1-3 and the tile
    // regenerates on a ten-hour timer.
    {"i_ore_iron", 1, Provenance::WorldGathered, Station::None, Tool::Pickaxe,
     kMining, 10, kNoSkill, 0, {}, "SCRIPT core/regiontypes.scp r_default_rock 50.0 mr_iron; regionresources.scp mr_iron SKILL=1.0,30.0"},

    // ONE ingot per ore, and the skill checked is MINING, not Blacksmithy. The
    // window comes from the ingot's TDATA1/TDATA2 (iron: 20.0/50.0), and a
    // failed smelt destroys up to half the ore.
    {"i_ingot_iron", 1, Provenance::WorldProcessed, Station::Forge, Tool::None,
     kMining, 200, kNoSkill, 0, {{"i_ore_iron", 1}},
     "ENGINE CCharSkill.cpp:1154 iResourceQty=1; TDATA1=20.0 TDATA2=50.0; failure loses up to half"},

    // The smith menu needs the hammer EQUIPPED in HAND1 and a forge within 3.
    {"i_dagger", 1, Provenance::PlayerCrafted, Station::Forge, Tool::SmithHammer,
     kBlacksmithing, 0, kNoSkill, 0, {{"i_ingot_iron", 4}},
     "ENGINE CClientUse.cpp:1273 LayerFind(LAYER_HAND1), :1282 IsItemTypeNear(IT_FORGE,3); SCRIPT i_dagger SKILLMAKE=Blacksmithing 0.0"},
    {"i_spear_short", 1, Provenance::PlayerCrafted, Station::Forge, Tool::SmithHammer,
     kBlacksmithing, 453, kNoSkill, 0, {{"i_ingot_iron", 6}, {"i_log", 1}},
     "SCRIPT i_spear_short SKILLMAKE=Blacksmithing 45.3; REVOLUTION forum 59111 band 70.1-100"},

    // --- wood ---------------------------------------------------------------
    {"i_log", 1, Provenance::WorldGathered, Station::None, Tool::Blade,
     rules::kLumberjacking, 10, kNoSkill, 0, {},
     "SCRIPT r_default_tree 40.0 mr_tree; mr_tree REAPAMOUNT=1,3 SKILL=1.0,80.0"},
    {"i_board", 1, Provenance::PlayerCrafted, Station::None, Tool::CarpentryTool,
     kCarpentry, 0, kNoSkill, 0, {{"i_log", 1}},
     "SCRIPT sm_carpentry; i_board SKILLMAKE=Carpentry 0.0"},
    {"i_parchment", 1, Provenance::PlayerCrafted, Station::None, Tool::CarpentryTool,
     kCarpentry, 257, kNoSkill, 0, {{"i_log", 1}},
     "SCRIPT sm_wood_misc; i_parchment SKILLMAKE=Carpentry 25.7"},
    // Blank scrolls are a CARPENTRY product on this runtime, not an Inscription
    // one. That is what makes the runebook's eight scrolls world-sourced.
    {"i_scroll_blank", 1, Provenance::PlayerCrafted, Station::None, Tool::CarpentryTool,
     kCarpentry, 257, kNoSkill, 0, {{"i_parchment", 1}},
     "SCRIPT sm_wood_misc; i_scroll_blank SKILLMAKE=Carpentry 25.7"},
    // The carpenter's one verified SELLABLE good below the endgame. Two of its
    // three numbers are UNKNOWN and are marked rather than invented:
    //   - our runtime carries NO [ITEMDEF i_club] and therefore no SKILLMAKE;
    //     i_club is a base Sphere item. The carpentry menu's default material
    //     tier is "Carpentry 0.0" (def_carpentry.scp:200), so 0 is the
    //     best-supported reading of the gate, NOT a measured one.
    //   - the log count is UNKNOWN. The magic variant i_club_ruin takes
    //     "RESOURCES=4 i_log, i_club" (i_magic_weapon.scp:2279), which is the
    //     ENCHANT cost, not the craft cost. 1 is a placeholder.
    // What IS verified: our own carpentry menu offers it
    // (def_carpentry.scp:41, category 1 "Weapons and Armor"), and the blunt
    // weaponsmith buys it at {10 15} (tm_vend.scp:1710).
    {"i_club", 1, Provenance::PlayerCrafted, Station::None, Tool::CarpentryTool,
     kCarpentry, 0, kNoSkill, 0, {{"i_log", 1}},
     "SCRIPT def_carpentry.scp:41 menu entry; SKILLMAKE and log count UNKNOWN; "
     "buyer tm_vend.scp:1710 VENDOR_B_WEAPONS_BLUNT {10 15}"},
    {"i_model_ship", 1, Provenance::PlayerCrafted, Station::None, Tool::CarpentryTool,
     kCarpentry, 950, kNoSkill, 0, {{"i_board", 10}},
     "SCRIPT i_model_ship SKILLMAKE=carpentry 95.0; REVOLUTION forum 59111 band 95-100"},

    // --- bowcraft -----------------------------------------------------------
    {"i_arrow_shaft", 1, Provenance::PlayerCrafted, Station::None, Tool::None,
     kBowcraft, 98, kNoSkill, 0, {{"i_log", 1}},
     "SCRIPT sm_bowcraft; i_arrow_shaft SKILLMAKE=9.8 Bowcraft; REVOLUTION forum 59111 band 0-100 Shaft"},
    {"i_arrow", 1, Provenance::PlayerMarket, Station::None, Tool::None,
     kBowcraft, 0, kNoSkill, 0, {{"i_arrow_shaft", 1}, {"i_feather", 1}},
     "SCRIPT i_arrow SKILLMAKE=Bowcraft 0.0; REVOLUTION cooperative category 08.11.2008"},
    {"i_bow", 1, Provenance::PlayerCrafted, Station::None, Tool::None,
     kBowcraft, 300, kNoSkill, 0, {{"i_log", 7}},
     "SCRIPT i_bow SKILLMAKE=Bowcraft 30.0"},

    // --- tinkering ----------------------------------------------------------
    // Tinkering is the craft the other crafts depend on: it makes the tailor's
    // kit, the scribe's pen, the alchemist's keg and the miner's pickaxe.
    {"i_gears", 1, Provenance::PlayerCrafted, Station::None, Tool::TinkerTools,
     kTinkering, 147, kNoSkill, 0, {{"i_ingot_iron", 1}},
     "SCRIPT sm_tinker; i_gears SKILLMAKE=Tinkering 14.7"},
    {"i_lockpick", 1, Provenance::PlayerCrafted, Station::None, Tool::TinkerTools,
     kTinkering, 485, kNoSkill, 0, {{"i_ingot_iron", 1}},
     "SCRIPT i_lockpick SKILLMAKE=Tinkering 48.5; REVOLUTION forum 59111 band 42.1-100"},
    {"i_tinker_tools", 1, Provenance::PlayerCrafted, Station::None, Tool::TinkerTools,
     kTinkering, 350, kNoSkill, 0, {{"i_ingot_iron", 4}},
     "SCRIPT i_tinker_tools SKILLMAKE=TINKERING 35.0"},
    {"i_pickaxe", 1, Provenance::PlayerCrafted, Station::None, Tool::TinkerTools,
     kTinkering, 400, kNoSkill, 0, {{"i_ingot_iron", 1}},
     "SCRIPT i_pickaxe SKILLMAKE=Tinkering 40.0"},
    {"i_barrel_tap", 1, Provenance::PlayerCrafted, Station::None, Tool::TinkerTools,
     kTinkering, 520, kNoSkill, 0, {{"i_ingot_iron", 1}}, "SCRIPT i_barrel_tap SKILLMAKE=Tinkering 52.0"},
    {"i_barrel_hoops", 1, Provenance::PlayerCrafted, Station::None, Tool::TinkerTools,
     kTinkering, 420, kNoSkill, 0, {{"i_ingot_iron", 4}}, "SCRIPT i_barrel_hoops SKILLMAKE=Tinkering 42.0"},
    // The keg is the PvP economy's container: 100 potions of ONE type.
    {"i_keg_potion", 1, Provenance::PlayerMarket, Station::None, Tool::TinkerTools,
     kTinkering, 650, kNoSkill, 0, {{"i_board", 8}, {"i_barrel_tap", 1}, {"i_barrel_hoops", 1}},
     "SCRIPT i_keg_potion SKILLMAKE=TINKERING 65.0, capacity 100; REVOLUTION cooperative 'potion kegleri (dolu olmalı)' 08.11.2008"},
    {"i_sewing_kit", 1, Provenance::PlayerCrafted, Station::None, Tool::TinkerTools,
     kTinkering, 188, kTailoring, 188,
     {{"i_sewing_needle", 1}, {"i_bowl_wood", 1}, {"i_yarn_ball", 1}, {"i_thread", 1}},
     "SCRIPT i_sewing_kit SKILLMAKE=Tinkering 18.8,t_tinker_tools,Tailoring 18.8"},
    {"i_scissors", 1, Provenance::PlayerCrafted, Station::None, Tool::TinkerTools,
     kTinkering, 145, kNoSkill, 0, {{"i_ingot_iron", 1}}, "SCRIPT i_scissors SKILLMAKE=Tinkering 14.5"},
    {"i_pen_and_ink", 1, Provenance::PlayerCrafted, Station::None, Tool::TinkerTools,
     kTinkering, 441, kNoSkill, 0, {{"i_feather", 1}, {"i_ink_well", 1}},
     "SCRIPT i_pen_and_ink SKILLMAKE=Tinkering 44.1 -- the scribe's tool is a TINKER product"},

    // --- alchemy ------------------------------------------------------------
    // The six bands below are RevolutionUO's own published Alchemy training
    // ladder, and every one of them lands on this runtime's SKILLMAKE exactly.
    // Six for six is the strongest single agreement found in M3.7.
    {"i_bottle_empty", 1, Provenance::PlayerCrafted, Station::None, Tool::None,
     kAlchemy, 250, kNoSkill, 0, {}, "SCRIPT i_bottle_empty SKILLMAKE=Alchemy 25.0,t_glassblowing_tool"},
    {"i_potion_heal", 1, Provenance::PlayerCrafted, Station::None, Tool::MortarPestle,
     kAlchemy, 151, kNoSkill, 0, {{"i_reag_ginseng", 3}, {"i_bottle_empty", 1}},
     "SCRIPT SKILLMAKE=ALCHEMY 15.1; REVOLUTION guide band 15.1-25.1 Heal"},
    // Poison, added 2026-08-29 on the owner's instruction ("Voris it can make
    // poison bottle and it can sell to npc"). Cheapest potion this life can
    // make -- ALCHEMY 15.1, the same band as Heal -- and the one an NPC will
    // take, so it is both the training sink and the first coin. Recipe read
    // from the shard: [ITEMDEF i_potion_Poison] RESOURCES=2 i_reag_nightshade,
    // 1 i_bottle_empty, SKILLMAKE=ALCHEMY 15.1, t_mortar.
    //
    // Note these potion defs use the DEFNAME as the ITEMDEF header rather than
    // a numeric id with a DEFNAME line, which is why a search for
    // "DEFNAME=i_potion_poison" finds nothing at all.
    {"i_potion_poison", 1, Provenance::PlayerCrafted, Station::None, Tool::MortarPestle,
     kAlchemy, 151, kNoSkill, 0, {{"i_reag_nightshade", 2}, {"i_bottle_empty", 1}},
     "SCRIPT i_potion_Poison SKILLMAKE=ALCHEMY 15.1,t_mortar; "
     "RESOURCES=2 i_reag_nightshade,1 i_bottle_empty"},
    {"i_potion_cure", 1, Provenance::PlayerCrafted, Station::None, Tool::MortarPestle,
     kAlchemy, 251, kNoSkill, 0, {{"i_reag_garlic", 3}, {"i_bottle_empty", 1}},
     "SCRIPT SKILLMAKE=ALCHEMY 25.1; REVOLUTION guide band 25.1-35.1 Cure"},
    {"i_potion_agilitygreat", 1, Provenance::PlayerCrafted, Station::None, Tool::MortarPestle,
     kAlchemy, 351, kNoSkill, 0, {{"i_reag_blood_moss", 3}, {"i_bottle_empty", 1}},
     "SCRIPT SKILLMAKE=ALCHEMY 35.1; REVOLUTION guide band 35.1-55.1 Gr. Agility"},
    {"i_potion_healgreat", 1, Provenance::PlayerCrafted, Station::None, Tool::MortarPestle,
     kAlchemy, 551, kNoSkill, 0, {{"i_reag_ginseng", 7}, {"i_bottle_empty", 1}},
     "SCRIPT SKILLMAKE=ALCHEMY 55.1; REVOLUTION guide band 55.1-65.1 Gr. Heal"},
    {"i_potion_curegreat", 1, Provenance::PlayerCrafted, Station::None, Tool::MortarPestle,
     kAlchemy, 651, kNoSkill, 0, {{"i_reag_garlic", 6}, {"i_bottle_empty", 1}},
     "SCRIPT SKILLMAKE=ALCHEMY 65.1; REVOLUTION guide band 65.1-90.1 Gr. Cure"},
    {"i_potion_poisondeadly", 1, Provenance::PlayerCrafted, Station::None, Tool::MortarPestle,
     kAlchemy, 901, kNoSkill, 0, {{"i_reag_nightshade", 8}, {"i_bottle_empty", 1}},
     "SCRIPT SKILLMAKE=ALCHEMY 90.1; REVOLUTION guide band 90.1-100 Deadly Poison"},

    // --- inscription --------------------------------------------------------
    // Revolution's Inscription ladder lands on these too: poison from 30,
    // recall from 40, resurrection at 80.0 exactly.
    {"i_scroll_poison", 1, Provenance::PlayerCrafted, Station::None, Tool::BlankScroll,
     kInscription, 300, kMagery, 200, {{"i_scroll_blank", 1}, {"i_reag_nightshade", 1}},
     "SCRIPT SKILLMAKE=Inscription 30.0,Magery 20.0; REVOLUTION forum 59111 band 0-60"},
    {"i_scroll_recall", 1, Provenance::PlayerCrafted, Station::None, Tool::BlankScroll,
     kInscription, 400, kMagery, 300,
     {{"i_scroll_blank", 1}, {"i_reag_black_pearl", 1}, {"i_reag_blood_moss", 1}, {"i_reag_mandrake_root", 1}},
     "SCRIPT SKILLMAKE=Inscription 40.0,Magery 30.0; REVOLUTION forum 59111 band 60-80"},
    // The Runebook's real wall. Gate Travel is 7th circle, the mage shop stocks
    // circles 1-4 only, so there is NO purchasable source -- a legitimate
    // runebook crafter needs Magery 60 or a PvM drop.
    {"i_scroll_gate_travel", 1, Provenance::PlayerCrafted, Station::None, Tool::BlankScroll,
     kInscription, 700, kMagery, 600,
     {{"i_scroll_blank", 1}, {"i_reag_black_pearl", 1}, {"i_reag_mandrake_root", 1}, {"i_reag_sulfur_ash", 1}},
     "SCRIPT SKILLMAKE=Inscription 70.0,Magery 60.0; no vendor sells 7th circle"},
    {"i_scroll_resurrection", 1, Provenance::PlayerCrafted, Station::None, Tool::BlankScroll,
     kInscription, 800, kMagery, 700,
     {{"i_scroll_blank", 1}, {"i_reag_blood_moss", 1}, {"i_reag_garlic", 1}, {"i_reag_ginseng", 1}},
     "SCRIPT SKILLMAKE=Inscription 80.0,Magery 70.0; REVOLUTION forum 59111 band 80-100"},

    // The runebook itself. NOTE: this recipe is currently UNREACHABLE -- the
    // item is in no skill menu, so the graph knows how to make it and the
    // runtime offers no door. That is the honest state and it is why
    // LEGITIMATE_RUNEBOOK_CRAFTING is still NOT PROVEN.
    {"i_spellbook_runebook", 1, Provenance::PlayerMarket, Station::None, Tool::PenAndInk,
     kInscription, 450, kNoSkill, 0,
     {{"i_scroll_blank", 8}, {"i_rune_marker", 1}, {"i_scroll_recall", 1}, {"i_scroll_gate_travel", 1}},
     "SCRIPT revolution/revolution_runebook.scp SKILLMAKE=Inscription 45.0,i_pen_and_ink; REVOLUTION cooperative 19.12.2008; NO MENU ENTRY"},

    // --- fishing ------------------------------------------------------------
    {"i_fish_big_1", 1, Provenance::WorldGathered, Station::None, Tool::FishingPole,
     rules::kFishing, 10, kNoSkill, 0, {},
     "SCRIPT r_default_water 60.0 nothing / 10.0 each of four fish; REAPAMOUNT=1,3"},
    // The M3 finding, as an edge: one fish becomes four steaks at a twelfth of
    // the weight, and only the cook buys them.
    // COOKED, which is what a vendor buys and what a person can eat. The raw
    // steak cannot simply be double-clicked onto a fire: Source-X answers a
    // double-click on IT_FOOD_RAW by EATING it (CCharUse.cpp:1860), so this
    // goes through the Cooking menu with a fire in reach, exactly as the
    // itemdef says -- SKILLMAKE=Cooking 0.0, t_cooking.
    {"i_fish_cut_cooked", 1, Provenance::PlayerCrafted, Station::Fire,
     Tool::None, kCooking, 0, kNoSkill, 0, {{"i_fish_cut_raw", 1}},
     "SCRIPT i_fish_cut_cooked RESOURCES=1 i_fish_cut_raw, "
     "SKILLMAKE=Cooking 0.0,t_cooking; ENGINE CCharUse.cpp:294 Use_Kindling "
     "-> Skill_UseQuick(SKILL_CAMPING) -> ITEMID_CAMPFIRE 0x0de3"},
    {"i_fish_cut_raw", 4, Provenance::WorldProcessed, Station::None, Tool::Blade,
     kNoSkill, 0, kNoSkill, 0, {{"i_fish_big_1", 1}},
     "ENGINE CClientTarg.cpp:1950 SetAmount(4 * GetAmount()); LIVE m3_cut1"},
    };
    return kRecipes;
}

// Items with no producing edge, whose class still has to be known.
struct LeafClass {
    const char* item;
    Provenance  provenance;
};

const LeafClass kLeaves[] = {
    {"i_cotton",             Provenance::WorldGathered},
    {"i_flax_bundle",        Provenance::WorldGathered},
    {"i_feather",            Provenance::Unknown},      // no gathering path on this runtime
    {"i_reag_black_pearl",   Provenance::Unknown},      // reagent sourcing: see the vendor matrix
    {"i_reag_blood_moss",    Provenance::Unknown},
    {"i_reag_garlic",        Provenance::Unknown},
    {"i_reag_ginseng",       Provenance::Unknown},
    {"i_reag_mandrake_root", Provenance::Unknown},
    {"i_reag_nightshade",    Provenance::Unknown},
    {"i_reag_spider_silk",   Provenance::Unknown},
    {"i_reag_sulfur_ash",    Provenance::Unknown},
    {"i_rune_marker",        Provenance::Unknown},
    {"i_ink_well",           Provenance::Unknown},
    {"i_sewing_needle",      Provenance::Unknown},
    {"i_bowl_wood",          Provenance::Unknown},
};

} // namespace

const char* ProvenanceName(Provenance p) {
    switch (p) {
        case Provenance::Unknown:         return "UNKNOWN";
        case Provenance::WorldGathered:   return "WORLD_GATHERED";
        case Provenance::AnimalHarvested: return "ANIMAL_HARVESTED";
        case Provenance::PvmDrop:         return "PVM_DROP";
        case Provenance::TreasureDrop:    return "TREASURE_DROP";
        case Provenance::WorldProcessed:  return "WORLD_PROCESSED";
        case Provenance::PlayerCrafted:   return "PLAYER_CRAFTED";
        case Provenance::PlayerMarket:    return "PLAYER_MARKET";
        case Provenance::NpcVerified:     return "NPC_VERIFIED";
        default:                          return "?";
    }
}

const char* StationName(Station s) {
    switch (s) {
        case Station::None:          return "none";
        case Station::Forge:         return "forge";
        case Station::Anvil:         return "anvil";
        case Station::SpinningWheel: return "spinning wheel";
        case Station::Loom:          return "loom";
        case Station::Fire:          return "campfire";
        default:                     return "?";
    }
}

bool StationNeedsDynamicItem(Station s) {
    // A forge or an anvil is found by CWorldMap::FindItemTypeNearby, which
    // scans dynamics, terrain AND statics -- so map art works.
    //
    // A spinning wheel or loom is TARGETED, and CClient::Event_Target resolves
    // the target with uid.ObjFind(). A static has no UID, so pItemTarg is
    // nullptr and the IT_WOOL / IT_YARN / IT_THREAD cases break out without
    // doing anything. Map art is inert.
    return s == Station::SpinningWheel || s == Station::Loom;
}

const char* ToolName(Tool t) {
    switch (t) {
        case Tool::None:          return "none";
        case Tool::Blade:         return "blade";
        case Tool::Pickaxe:       return "pickaxe";
        case Tool::Scissors:      return "scissors";
        case Tool::SewingKit:     return "sewing kit";
        case Tool::SmithHammer:   return "smith hammer";
        case Tool::TinkerTools:   return "tinker tools";
        case Tool::CarpentryTool: return "carpentry tool";
        case Tool::MortarPestle:  return "mortar and pestle";
        case Tool::PenAndInk:     return "pen and ink";
        case Tool::FishingPole:   return "fishing pole";
        case Tool::BlankScroll:   return "blank scroll";
        default:                  return "?";
    }
}

bool ToolMustBeEquipped(Tool t) {
    // Two separate mechanisms, both of which cost a live run before they were
    // understood:
    //
    // SMITH HAMMER -- CClientUse.cpp:1273 reads LayerFind(LAYER_HAND1). A
    //   hammer in the backpack is not a hammer.
    //
    // PICKAXE / SHOVEL -- the SCRIPT, not the engine. skill45_mining.scp:30
    //   guards @PreStart with `IF (<SRC.WEAPON.USESCUR> < 1)`, and SRC.WEAPON
    //   is the EQUIPPED weapon, so an unworn shovel reads as zero charges and
    //   the shard answers "The tool is out of charges."
    //   skill44_lumberjacking.scp:26 carries the identical check, so a chopping
    //   axe must be worn too -- note that SHEARING does not, because that path
    //   is OnTarg_Use_Item on the sheep and never enters a skill script.
    return t == Tool::SmithHammer || t == Tool::Pickaxe;
}

const char* BlockName(Block b) {
    switch (b) {
        case Block::None:           return "none";
        case Block::NoRecipe:       return "NO_RECIPE";
        case Block::MissingInput:   return "MISSING_INPUT";
        case Block::MissingSkill:   return "MISSING_SKILL";
        case Block::MissingTool:    return "MISSING_TOOL";
        case Block::ToolNotEquipped:return "TOOL_NOT_EQUIPPED";
        case Block::NoStation:      return "NO_STATION";
        default:                    return "?";
    }
}

const std::vector<Recipe>& KnownRecipes() { return Table(); }

const Recipe* FindRecipe(const char* item) {
    for (const Recipe& r : Table()) {
        if (Same(r.output, item)) return &r;
    }
    return nullptr;
}

Provenance ProvenanceOf(const char* item) {
    if (const Recipe* r = FindRecipe(item)) return r->provenance;
    for (const LeafClass& l : kLeaves) {
        if (Same(l.item, item)) return l.provenance;
    }
    return Provenance::Unknown;
}

bool IsRawResource(const char* item) { return FindRecipe(item) == nullptr; }

i32 Capability::Skill(i32 skillId) const {
    if (skillId < 0) return 0;
    if (static_cast<usize>(skillId) >= skillTenths.size()) return -1;
    return skillTenths[static_cast<usize>(skillId)];
}

bool Capability::HasTool(Tool t) const {
    if (t == Tool::None) return true;
    if (ToolMustBeEquipped(t)) {
        for (Tool e : toolsEquipped) {
            if (e == t) return true;
        }
        return false;
    }
    for (Tool c : toolsCarried) {
        if (c == t) return true;
    }
    for (Tool e : toolsEquipped) {
        if (e == t) return true;
    }
    return false;
}

bool Capability::CanReach(Station s) const {
    if (s == Station::None) return true;
    for (Station r : stationsReachable) {
        if (r == s) return true;
    }
    return false;
}

namespace {

i32 HeldQty(const std::vector<Ingredient>& have, const char* item) {
    i32 total = 0;
    for (const Ingredient& h : have) {
        if (Same(h.item, item)) total += h.qty;
    }
    return total;
}

} // namespace

std::vector<Requirement> MissingInputs(const char* item,
                                       const Capability& cap,
                                       const std::vector<Ingredient>& have) {
    std::vector<Requirement> out;
    const Recipe* r = FindRecipe(item);
    if (!r) {
        Requirement req;
        req.block = Block::NoRecipe;
        req.item  = item;
        out.push_back(req);
        return out;
    }

    // Skills first: a planner should learn before it shops.
    if (r->skillId >= 0 && cap.Skill(r->skillId) < r->skillTenths) {
        Requirement req;
        req.block       = Block::MissingSkill;
        req.skillId     = r->skillId;
        req.skillTenths = r->skillTenths;
        out.push_back(req);
    }
    if (r->skillId2 >= 0 && cap.Skill(r->skillId2) < r->skillTenths2) {
        Requirement req;
        req.block       = Block::MissingSkill;
        req.skillId     = r->skillId2;
        req.skillTenths = r->skillTenths2;
        out.push_back(req);
    }

    if (!cap.HasTool(r->tool)) {
        Requirement req;
        // Distinguish "you don't own one" from "it is in your pack, not your
        // hand" -- they need completely different fixes.
        bool carried = false;
        for (Tool c : cap.toolsCarried) {
            if (c == r->tool) carried = true;
        }
        req.block = (carried && ToolMustBeEquipped(r->tool)) ? Block::ToolNotEquipped
                                                            : Block::MissingTool;
        req.tool  = r->tool;
        out.push_back(req);
    }

    if (!cap.CanReach(r->station)) {
        Requirement req;
        req.block   = Block::NoStation;
        req.station = r->station;
        out.push_back(req);
    }

    for (const Ingredient& in : r->inputs) {
        if (!in.item || in.qty <= 0) continue;
        const i32 held = HeldQty(have, in.item);
        if (held < in.qty) {
            Requirement req;
            req.block = Block::MissingInput;
            req.item  = in.item;
            req.qty   = in.qty - held;
            out.push_back(req);
        }
    }
    return out;
}

bool CanSelfProduce(const char* item,
                    const Capability& cap,
                    const std::vector<Ingredient>& have) {
    return MissingInputs(item, cap, have).empty();
}

namespace {

bool Visit(const char* item,
           std::vector<const char*>& onStack,
           std::vector<const Recipe*>& order,
           bool* cycle) {
    for (const char* s : onStack) {
        if (Same(s, item)) {
            if (cycle) *cycle = true;
            return false;
        }
    }
    const Recipe* r = FindRecipe(item);
    if (!r) return true;   // a raw resource is a valid leaf, not a failure

    for (const Recipe* done : order) {
        if (Same(done->output, item)) return true;   // already scheduled
    }

    onStack.push_back(item);
    for (const Ingredient& in : r->inputs) {
        if (!in.item || in.qty <= 0) continue;
        if (!Visit(in.item, onStack, order, cycle)) {
            onStack.pop_back();
            return false;
        }
    }
    onStack.pop_back();
    order.push_back(r);
    return true;
}

} // namespace

std::vector<const Recipe*> ProductionOrder(const char* item, bool* cycle) {
    std::vector<const Recipe*> order;
    std::vector<const char*> onStack;
    if (cycle) *cycle = false;
    if (!Visit(item, onStack, order, cycle)) order.clear();
    return order;
}

namespace {

void AccumulateRaw(const char* item, i32 qty, std::vector<Ingredient>& out, int depth) {
    // A depth stop as well as the cycle check: the graph is authored data, and
    // a malformed edge should degrade rather than recurse forever.
    if (depth > 16) return;
    const Recipe* r = FindRecipe(item);
    if (!r) {
        for (Ingredient& o : out) {
            if (Same(o.item, item)) { o.qty += qty; return; }
        }
        out.push_back({item, qty});
        return;
    }
    // Ceiling-divide: half a smelt is not a smelt.
    const i32 per = r->outputQty > 0 ? r->outputQty : 1;
    const i32 runs = (qty + per - 1) / per;
    bool any = false;
    for (const Ingredient& in : r->inputs) {
        if (!in.item || in.qty <= 0) continue;
        any = true;
        AccumulateRaw(in.item, in.qty * runs, out, depth + 1);
    }
    if (!any) {
        // A recipe with no inputs is itself a gathering step -- wool, ore, a
        // log, a fish. It IS the raw resource.
        for (Ingredient& o : out) {
            if (Same(o.item, item)) { o.qty += qty; return; }
        }
        out.push_back({item, qty});
    }
}

} // namespace

std::vector<Ingredient> RawInputsFor(const char* item, i32 qty) {
    std::vector<Ingredient> out;
    bool cycle = false;
    ProductionOrder(item, &cycle);
    if (cycle) return out;
    AccumulateRaw(item, qty, out, 0);
    return out;
}

} // namespace uo::prod
