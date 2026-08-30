#include "life/Runner.h"

#include "Client.h"
#include "uo/log.h"
#include "uo/builders.h"
#include "uo/faucets.h"
#include "uo/market.h"
#include "uo/trade.h"
#include "uo/combat.h"
#include "uo/professions.h"
#include "uo/vendor_policy.h"
#include "uo/world_model.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>
#include <cstring>

namespace uo::life {

namespace {

// Item graphics, all read off the runtime's own itemdefs -- never guessed from
// generic UO tables. i_hatchet is [ITEMDEF 0f43] with DUPELIST 0f44 (the
// flipped graphic); i_log is 0x1BDD; i_bandage is [ITEMDEF 0e21].
constexpr u16 kHatchet[]  = {0x0F43, 0x0F44};
constexpr u16 kAxe[]      = {0x0F49, 0x0F4A};
constexpr u16 kLog        = 0x1BDD;
constexpr u16 kBandage    = 0x0E21;
constexpr u16 kKatana[]   = {0x13FE, 0x13FF};
// WHAT THIS SHARD ACTUALLY SELLS AS FOOD, read from its own itemdefs rather
// than from generic UO. The first three were all the old list had, and a
// baker's window is mostly none of them -- Voris stood in front of pizzas and
// pans of cookies with an empty stomach.
//
//   0x103B i_bread_loaf     0x1041 i_pie_baked    0x09E9 i_cake
//   0x09EA i_muffin         0x098C i_bread_french 0x160B i_pan_cookies
//   0x1040 i_pizza          0x160A i_lamb_leg     0x1608 i_chicken_leg
//   0x09B7 i_bird_cooked    0x09C9 i_ham          0x09EB/0x09F2 kept from
//                                                 the original list
constexpr u16 kFood[]     = {0x103B, 0x1041, 0x09E9, 0x09EA, 0x098C, 0x160B,
                             0x1040, 0x160A, 0x1608, 0x09B7, 0x09C9,
                             0x09EB, 0x09F2};
constexpr u16 kGoldCoin   = 0x0EED;             // i_gold
// i_tongs (0FBB/0FBC) and i_hammer_smith (013E3, dupe 013E4) are both
// TYPE=t_weapon_mace_smith, which is what the engine gates the menu on.
constexpr u16 kSmithToolGfx[] = {0x0FBB, 0x0FBC, 0x13E3, 0x13E4};
// BUT ONLY THE HAMMER CAN BE WIELDED, and the menu needs it in HAND1.
// i_tongs has no DAM and no SKILL in i_profession.scp -- it is not a weapon,
// so the server will not put it in a hand: it answered "You put the tongs in
// your pack" to every equip, at layer 0 and layer 1, with the hand empty.
// i_hammer_smith has DAM=13,15 SKILL=Macefighting and equips normally.
// "equip smith hammer maybe?" (project owner, 2026-08-29).
constexpr u16 kSmithHammerGfx[] = {0x13E3, 0x13E4};
// WHAT OPENS A CRAFT MENU, per tool.
//
// Most trades open their menu by double-clicking the TOOL, not the material:
// a mortar for alchemy, tinker tools, a sewing kit, a saw. Inscription is the
// exception the enum itself names -- Tool::BlankScroll, "Inscription's menu
// opens by double-clicking one" -- which is why DoCraft's default was written
// around the material and why alchemy then answered "You can't think of a way
// to use that item" as it double-clicked a reagent.
//
// Graphics come from Professions.cpp, which reads them off the shard's own
// itemdefs: mortar 0E9B, sewing kit 0F9D, saw 1034, tinker tools 1EBC.
struct ToolOpener { prod::Tool tool; u16 gfx[4]; usize n; };
const ToolOpener kToolOpeners[] = {
    {prod::Tool::MortarPestle,  {0x0E9B, 0, 0, 0},        1},
    {prod::Tool::SewingKit,     {0x0F9D, 0, 0, 0},        1},
    {prod::Tool::CarpentryTool, {0x1034, 0, 0, 0},        1},
    {prod::Tool::TinkerTools,   {0x1EBC, 0, 0, 0},        1},
    {prod::Tool::Scissors,      {0x0F9E, 0x0F9F, 0, 0},   2},
    {prod::Tool::SmithHammer,   {0x13E3, 0x13E4, 0, 0},   2},
};
const ToolOpener* OpenerFor(prod::Tool t) {
    for (const ToolOpener& o : kToolOpeners) if (o.tool == t) return &o;
    return nullptr;
}

// WEAPONS A FIGHTER MIGHT BE CARRYING. Every one of these is handed out by the
// shard's own newbie kits (sp_tm_newbie.scp: [NEWBIE FENCING] ITEMNEWBIE=
// i_kryss, [NEWBIE MACEFIGHTING] i_club, [NEWBIE SWORDSMANSHIP] and friends),
// and they are ITEMNEWBIE, so they survive death. Kaelen's own saved character
// carries i_kryss, i_dagger AND i_katana -- all three sitting in CONT=
// (his backpack) with no LAYER, which is to say in his bag while he punched a
// Spectre bare-handed for twenty seconds and called it a stalemate.
// Flip pairs included: a weapon on the wire is often the flipped id.
constexpr u16 kMeleeWeaponGfx[] = {
    0x1400, 0x1401,   // i_kryss
    0x0F51, 0x0F52,   // i_dagger
    0x13B3, 0x13B4,   // i_club
    0x13FE, 0x13FF,   // i_katana
    0x0F5E, 0x0F5F,   // i_sword_broad
    0x1440, 0x1441,   // i_sword_cutlass
    0x0F60, 0x0F61,   // i_sword_long
    0x143B, 0x143C,   // i_mace
    0x0F62, 0x0F63,   // i_spear
};
// Enough coin to walk into a shop with. A smith hammer is VALUE=50 and the
// dearest tool a life buys is well under this, so one withdrawal covers the
// errand rather than one trip to the box per item.
constexpr i32 kToolMoneyToCarry = 500;
// i_spellbook, ITEMDEF 0efa. A spell scroll's graphic is 0x1F2D + the spell
// number: Create Food is spell 2 at 0x1F2F, Heal is 4 at 0x1F31, Magic Arrow
// is 5 at 0x1F32 and Recall is 31 at 0x1F4C -- four independent points, all
// read from this shard's own itemdefs. Circles 1-8 are therefore spells 1-64
// at 0x1F2E..0x1F6D.
// A working book rather than a complete one: circles 7-8 are sold by nobody
// on this shard, so a target of 64 would nag forever. 24 is the first three
// circles, which ARE obtainable by shopping. Mirrors kSpellbookComfortable in
// Needs.cpp -- the need and the goal must agree or the goal finishes a book
// the need still wants filled, and the pair loops.
constexpr int kSpellbookComfortableRuntime = 24;
// Enough to be worth a trip. Prices come from the shop window, never from
// here; these are only "is it worth walking".
constexpr i32 kSpellbookMoney = 120;
constexpr i32 kScrollMoney    = 120;
constexpr i64 kNoSpellbookCooldownMs = 240000;   // four minutes
constexpr i32 kMaxSpellbookTrips = 3;
// Long enough that a character which cannot sell anything goes and does
// something else for a while -- hunts, gathers, crafts -- rather than asking
// again the moment the purse is still empty.
constexpr i64 kNothingToSellCooldownMs = 180000;   // three minutes
constexpr i32 kMaxTradeTrips = 3;
// "up to 50-100" (project owner). Sixty is a fighting stock: enough to see a
// character through several graveyard trips, short of hoarding wool it could
// have spun into something saleable.
constexpr i32 kBandagesWanted = 60;
constexpr i32 kMaxBandageTrips = 3;
constexpr i64 kNoBandageCooldownMs = 180000;
// When even the map is exhausted, rest a while before asking again.
constexpr i64 kExploredAllCooldownMs = 300000;
// A pair of scissors is a few dozen coins from any tailor. Worth a walk.
constexpr i32 kScissorsMoney = 60;
constexpr i32 kMaxToolTrips = 3;
constexpr i64 kNoOreCooldownMs = 120000;
constexpr i64 kNoPetCooldownMs = 180000;
constexpr i32 kMaxTameTrips = 3;

// WHAT A CREATURE IS LIKELY TO DO TO YOU, before it has done it.
//
// "britain graveyard has specific enemies so zombie skeletal mage etc etc"
// (project owner, 2026-08-29). The graveyard holds zombies, skeletal mages and
// spectres, and a new fencer picked the SPECTRE -- the hardest hitter of the
// three and a caster besides.
//
// The danger memory only learned by dying, so a character's first fight was
// always a coin toss. This is the prior it lacked, generated from the shard's
// own chardefs (data/revolution_creatures.tsv, 450 creatures): damage weighs
// heaviest because that is what kills a weak melee character, then armour
// because it decides whether the fight can be won at all, then hit points, and
// a flat penalty for anything that casts. The ranking it produces for the
// graveyard is the one a player would give:
//
//   Zombie 0.037 < Skeleton 0.038 < Ghoul 0.054 < Skeletal Mage 0.097
//                                               < Spectre 0.101
//
// It is only a PRIOR. Anything this character has actually learned about a
// creature overrides it, because being killed by something is better evidence
// than its stat block.
std::unordered_map<std::string, double>& SeededTaming();

std::unordered_map<std::string, double>& SeededCreatureDanger() {
    static std::unordered_map<std::string, double> table;
    return table;
}

void LoadSeededCreatureDanger(const std::string& dataDir) {
    std::unordered_map<std::string, double>& t = SeededCreatureDanger();
    if (!t.empty()) return;
    std::FILE* f = std::fopen((dataDir + "/revolution_creatures.tsv").c_str(), "rb");
    if (!f) return;
    char line[512];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }          // header
        std::string row(line);
        // defname 	 name 	 danger 	 ...
        const usize t1 = row.find('	');
        if (t1 == std::string::npos) continue;
        const usize t2 = row.find('	', t1 + 1);
        if (t2 == std::string::npos) continue;
        const usize t3 = row.find('	', t2 + 1);
        const std::string name = row.substr(t1 + 1, t2 - t1 - 1);
        const std::string dg =
            row.substr(t2 + 1, (t3 == std::string::npos ? row.size() : t3) - t2 - 1);
        if (name.empty()) continue;
        std::string key;
        for (char c : name)
            key.push_back(static_cast<char>(std::tolower(
                static_cast<unsigned char>(c))));
        t[key] = std::atof(dg.c_str());
        // ... and the taming requirement, which is the LAST column.
        const usize lastTab = row.find_last_of('	');
        if (lastTab != std::string::npos && lastTab > t2) {
            const double tam = std::atof(row.c_str() + lastTab + 1);
            if (tam >= 0.0) SeededTaming()[key] = tam;
        }
    }
    std::fclose(f);
}

// The TAMING requirement from the same table, or -1 for a creature that
// cannot be tamed at all. 109 of the 450 carry one.
std::unordered_map<std::string, double>& SeededTaming() {
    static std::unordered_map<std::string, double> table;
    return table;
}

double SeededTamingFor(const std::string& name) {
    const std::unordered_map<std::string, double>& t = SeededTaming();
    std::string key;
    for (char c : name)
        key.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(c))));
    const auto it = t.find(key);
    return it == t.end() ? -1.0 : it->second;
}

double SeededDangerFor(const std::string& name) {
    const std::unordered_map<std::string, double>& t = SeededCreatureDanger();
    std::string key;
    for (char c : name)
        key.push_back(static_cast<char>(std::tolower(
            static_cast<unsigned char>(c))));
    const auto it = t.find(key);
    return it == t.end() ? -1.0 : it->second;
}


constexpr i64 kNoToolCooldownMs = 180000;
// Spare gold, above the profession's reserve, that makes armour shopping
// sensible rather than reckless.
constexpr i32 kArmorMoney = 400;
constexpr i64 kGearCooldownMs = 240000;
// A rest, not a write-off. RetryableFailure means THIS door said no and
// another may not -- one silent shop is not evidence about a trade.
constexpr i64 kShortRestMs = 30000;
// How many times to answer "you can't reach that" by moving before
// concluding the forge itself is the problem. Three, matching the
// vendor chase: enough to get round a wall, not enough to spend a
// session on one anvil.
constexpr i32 kMaxSmeltReachFails = 3;
// A goal that used its whole time limit and finished nothing rests for two
// minutes. Long enough that something else certainly gets a turn, short enough
// that a genuinely long errand -- a walk across the map to a trainer -- can be
// resumed later in the same session.
constexpr i64 kExhaustedCooldownMs = 120000;
// HOW CLOSE IS CLOSE ENOUGH TO TRADE.
//
// One tile was too strict. Britain's shopkeepers wander a step at a time, so a
// character that insists on being adjacent re-walks every time the vendor
// shifts -- Ysolde logged "the 'mage' has moved to 1450,1618 (2 tiles) --
// walking back before buying" fifty-seven times in one session and bought
// nothing at all. Sphere refuses an out-of-reach purchase itself ("You can't
// reach the Vendor"), so the client does not need to be stricter than the
// server; it only needs to be close enough that the answer is usually yes.
constexpr i32 kVendorReach = 3;
// And a wandering vendor must not own the goal. After this many walk-backs,
// try the purchase from where we stand and let the server decide.
constexpr i32 kMaxVendorChases = 4;
// THE TAILORING CHAIN, END TO END. Every graphic read from this shard's own
// itemdefs and every mechanic from Source-X's source, not from generic UO:
//
//   scissors 0x0F9E on a woolly sheep (CHARDEF 0cf, body 0x00CF)
//        -> wool          CClientTarg.cpp:1878, case CREID_SHEEP
//   wool 0x0DF8 on a spinning wheel 0x1015
//        -> yarn/thread   CClientTarg.cpp:2053, case IT_WOOL
//   yarn 0x0E1D / thread 0x0FA0 on an upright loom 0x105F
//        -> cloth bolt    CClientTarg.cpp:2186, case IT_YARN/IT_THREAD
//   scissors on the bolt 0x0F95
//        -> cut cloth     CClientTarg.cpp:2147, ConvertBolttoCloth
//   scissors on cloth 0x175D
//        -> BANDAGES      CClientTarg.cpp:2151, IT_CLOTH -> ITEMID_BANDAGES1,
//                         one bandage per cloth
//
// The stations are real dynamic items and must be targeted by serial: the
// engine breaks out when pItemTarg is null, so targeting the ground beside a
// loom does nothing at all. There are 20 spinning wheels and 33 looms in the
// world -- in save/spherestatics.scp, NOT sphereworld.scp, because the M3.7
// decorator marks its placements attr_static and Source-X routes those to a
// different file.
// WHAT A CHARACTER MAY WEAR, AND HOW GOOD IT IS.
//
// "always try to wear better equipment based on your class" and "bots also
// always check for gear" (project owner, 2026-08-29).
//
// CLASS MATTERS ABSOLUTELY ON THIS SHARD, not as a penalty. revolutionuo.net's
// mining guide states that ore-smithed metal sets are Warrior-only and that
// "Bu setleri giyen karakterler buyu atamazlar" -- characters wearing them
// CANNOT CAST AT ALL (docs/REVOLUTION_EQUIPMENT_EVIDENCE.md, graded CONFIRMED).
// So metal on a caster is not a trade-off to weigh, it is the end of its
// profession. Leather is PLAUSIBLE for a caster and is allowed; cloth always.
//
// GENERATED from this shard's own itemdefs: every TYPE=t_armor,
// t_armor_leather or t_shield def carrying an ARMOR value, minus the gargish
// and elven pieces this Renaissance-era client has no art for. ARMOR and
// ReqStr are the shard's numbers, never guessed -- and ReqStr is why this
// table exists at all: a piece the character is too weak to wear is refused by
// the server, and asking repeatedly is one of the ways a goal burns a session.
constexpr ArmorPiece kArmorPieces[] = {
    {0x13BB, 25,  60, ArmorClass::Metal  },  // i_chainmail_coif
    {0x13BE, 25,  60, ArmorClass::Metal  },  // i_chainmail_leggings
    {0x13BF, 25,  60, ArmorClass::Metal  },  // i_chainmail_tunic
    {0x13C5, 13,  20, ArmorClass::Leather},  // i_leather_sleeves
    {0x13C6, 13,  20, ArmorClass::Leather},  // i_leather_gloves
    {0x13C7, 13,  20, ArmorClass::Leather},  // i_leather_gorget
    {0x13CB, 13,  20, ArmorClass::Leather},  // i_leather_leggings
    {0x13CC, 13,  25, ArmorClass::Leather},  // i_leather_tunic
    {0x13D4, 16,  25, ArmorClass::Leather},  // i_studded_sleeves
    {0x13D5, 16,  25, ArmorClass::Leather},  // i_studded_gloves
    {0x13D6, 16,  25, ArmorClass::Leather},  // i_studded_gorget
    {0x13DA, 16,  30, ArmorClass::Leather},  // i_studded_leggings
    {0x13DB, 16,  35, ArmorClass::Leather},  // i_studded_tunic
    {0x13EB, 22,  40, ArmorClass::Metal  },  // i_ringmail_gloves
    {0x13EC, 22,  40, ArmorClass::Metal  },  // i_ringmail_tunic
    {0x13EE, 22,  40, ArmorClass::Metal  },  // i_ringmail_sleeves
    {0x13F0, 22,  40, ArmorClass::Metal  },  // i_ringmail_leggings
    {0x1408, 30,  55, ArmorClass::Metal  },  // i_helm_closed
    {0x140A, 30,  45, ArmorClass::Metal  },  // i_helm_open
    {0x140C, 25,  40, ArmorClass::Metal  },  // i_bascinet
    {0x140E, 30,  55, ArmorClass::Metal  },  // i_helm_nose
    {0x1410, 30,  80, ArmorClass::Metal  },  // i_platemail_arms
    {0x1411, 30,  90, ArmorClass::Metal  },  // i_platemail_leggings
    {0x1412, 30,  80, ArmorClass::Metal  },  // i_platemail_helm
    {0x1413, 30,  45, ArmorClass::Metal  },  // i_platemail_gorget
    {0x1414, 30,  70, ArmorClass::Metal  },  // i_platemail_gloves
    {0x1415, 30,  95, ArmorClass::Metal  },  // i_platemail_chest
    {0x144E, 24,  55, ArmorClass::Metal  },  // i_bone_arms
    {0x144F, 24,  60, ArmorClass::Metal  },  // i_bone_chest
    {0x1450, 24,  55, ArmorClass::Metal  },  // i_bone_gloves
    {0x1451, 24,  20, ArmorClass::Metal  },  // i_bone_helmet
    {0x1452, 24,  55, ArmorClass::Metal  },  // i_bone_leggings
    {0x1B72, 10,  35, ArmorClass::Shield },  // i_shield_round_bronze
    {0x1B73,  7,  20, ArmorClass::Shield },  // i_shield_buckler
    {0x1B74, 12,  45, ArmorClass::Shield },  // i_shield_kite_metal
    {0x1B76, 15,  90, ArmorClass::Shield },  // i_shield_heater
    {0x1B78,  9,  20, ArmorClass::Shield },  // i_shield_kite_wood
    {0x1B7A,  8,  20, ArmorClass::Shield },  // i_shield_wood
    {0x1B7B,  9,  45, ArmorClass::Shield },  // i_shield_round_metal
    {0x1BC3, 16,  95, ArmorClass::Shield },  // i_shield_chaos
    {0x1BC4, 16,  95, ArmorClass::Shield },  // i_shield_order
    {0x1BC6, 16,  95, ArmorClass::Shield },  // i_shield_scale
    {0x1C00, 13,  20, ArmorClass::Leather},  // i_armor_female_shorts
    {0x1C02, 16,  35, ArmorClass::Leather},  // i_armor_female_studded
    {0x1C04, 30,  95, ArmorClass::Metal  },  // i_armor_female_plate
    {0x1C06, 13,  25, ArmorClass::Leather},  // i_armor_female_leather
    {0x1C08, 13,  25, ArmorClass::Leather},  // i_armor_female_skirt
    {0x1C0A, 13,  20, ArmorClass::Leather},  // i_armor_female_bustier
    {0x1C0C, 16,  35, ArmorClass::Leather},  // i_armor_female_bustier_studded
    {0x1DB9, 13,  20, ArmorClass::Leather},  // i_leather_cap
    {0x1F0B, 20,  30, ArmorClass::Metal  },  // i_helm_orc
    {0x236C, 25,  80, ArmorClass::Metal  },  // i_helm_kabuto
    {0x25E4, 15,  20, ArmorClass::Leather},  // i_armor_female_shorts_spiked
    {0x25E6, 18,  25, ArmorClass::Metal  },  // i_armor_female_harness_amazon
    {0x25E8, 20,  25, ArmorClass::Metal  },  // i_armor_female_harness_elite
    {0x2641, 28,  75, ArmorClass::Metal  },  // i_dragon_chest
    {0x2643, 28,  75, ArmorClass::Metal  },  // i_dragon_gloves
    {0x2645, 28,  75, ArmorClass::Metal  },  // i_dragon_helm
    {0x2647, 28,  75, ArmorClass::Metal  },  // i_dragon_leggings
    {0x264B, 30,  45, ArmorClass::Metal  },  // i_platemail_gorget2
    {0x2653, 30,  70, ArmorClass::Metal  },  // i_platemail_waraji_3d
    {0x2657, 28,  75, ArmorClass::Metal  },  // i_dragon_sleeves
    {0x2659, 30,  60, ArmorClass::Metal  },  // i_amazon_heavy
    {0x265B, 30,  60, ArmorClass::Metal  },  // i_amazon_medium
    {0x265D, 20,  60, ArmorClass::Metal  },  // i_amazon_light
    {0x2677, 13,  40, ArmorClass::Leather},  // i_gloves_kote1
    {0x2679, 13,  40, ArmorClass::Leather},  // i_gloves_kote2
    {0x2689, 30,  25, ArmorClass::Metal  },  // i_helm_winged
    {0x268B, 30,  40, ArmorClass::Metal  },  // i_hachimaki
    {0x268D, 30,  80, ArmorClass::Metal  },  // i_helm_kabuto_decorative
    {0x268F, 30,  80, ArmorClass::Metal  },  // i_kabuto_mempo
    {0x2691, 13,  20, ArmorClass::Leather},  // i_leather_cap2
    {0x269D, 13,  25, ArmorClass::Leather},  // i_cap_feathered
    {0x26B0, 13,  10, ArmorClass::Leather},  // i_leather_gloves_arcane
    {0x2774, 28,  50, ArmorClass::Metal  },  // i_chainmail_hatsuburi
    {0x2775, 30,  65, ArmorClass::Metal  },  // i_platemail_hatsuburi
    {0x2776, 13,  25, ArmorClass::Leather},  // i_leather_jingasa
    {0x2777, 30,  55, ArmorClass::Metal  },  // i_plate_jingasa_heavy
    {0x2778, 30,  80, ArmorClass::Metal  },  // i_platemail_kabuto_decorative
    {0x2779, 30,  50, ArmorClass::Metal  },  // i_platemail_mempo
    {0x277A, 13,  30, ArmorClass::Leather},  // i_leather_mempo
    {0x277B, 13,  40, ArmorClass::Leather},  // i_leather_do
    {0x277C, 20,  55, ArmorClass::Leather},  // i_studded_do
    {0x277D, 30,  85, ArmorClass::Metal  },  // i_platemail_do
    {0x277E, 13,  25, ArmorClass::Metal  },  // i_leather_hiro_sode
    {0x277F, 20,  30, ArmorClass::Leather},  // i_studded_hiro_sode
    {0x2780, 30,  75, ArmorClass::Metal  },  // i_platemail_hiro_sode
    {0x2781, 28,  55, ArmorClass::Metal  },  // i_plate_jingasa_light
    {0x2784, 13,  55, ArmorClass::Metal  },  // i_plate_jingasa_small
    {0x2785, 30,  70, ArmorClass::Metal  },  // i_helm_kabuto_battle
    {0x2786, 13,  20, ArmorClass::Leather},  // i_leather_suneate
    {0x2787, 20,  30, ArmorClass::Leather},  // i_studded_suneate
    {0x2788, 30,  80, ArmorClass::Metal  },  // i_platemail_suneate
    {0x2789, 30,  70, ArmorClass::Metal  },  // i_platemail_kabuto
    {0x278A, 13,  20, ArmorClass::Leather},  // i_leather_haidate
    {0x278B, 20,  30, ArmorClass::Leather},  // i_studded_haidate
    {0x278D, 30,  80, ArmorClass::Metal  },  // i_platemail_haidate
    {0x278E, 13,  10, ArmorClass::Leather},  // i_hood_ninja_leather
    {0x2790, 13,  10, ArmorClass::Leather},  // i_belt_ninja_leather
    {0x2791, 13,  10, ArmorClass::Leather},  // i_pants_ninja_leather
    {0x2792, 13,  10, ArmorClass::Leather},  // i_mitts_ninja_leather
    {0x2793, 13,  10, ArmorClass::Leather},  // i_jacket_ninja_leather
    {0x279D, 20,  30, ArmorClass::Leather},  // i_mempo_studded
    {0x2B06, 40,  70, ArmorClass::Metal  },  // i_legs_honor
    {0x2B08, 40,  65, ArmorClass::Metal  },  // i_breastplate_justice
    {0x2B0A, 40,  60, ArmorClass::Metal  },  // i_arms_compassion
    {0x2B0C, 40,  60, ArmorClass::Metal  },  // i_gauntlets_valor
    {0x2B0E, 40,  45, ArmorClass::Metal  },  // i_gorget_truth
    {0x2B10, 40,  25, ArmorClass::Metal  },  // i_helm_spirituality
    {0x2B12, 40,  10, ArmorClass::Metal  },  // i_solaretes_sacrifice
    {0x2B67, 30,  95, ArmorClass::Metal  },  // i_woodland_chest
    {0x2B69, 30,  45, ArmorClass::Metal  },  // i_woodland_gorget
    {0x2B6A, 30,  70, ArmorClass::Metal  },  // i_woodland_gauntlets
    {0x2B6B, 30,  90, ArmorClass::Metal  },  // i_woodland_leggings
    {0x2B6C, 30,  80, ArmorClass::Metal  },  // i_woodland_arms
    {0x2B6D, 30,  95, ArmorClass::Metal  },  // i_woodland_chest_female
    {0x2B6E, 30,  10, ArmorClass::Metal  },  // i_helm_circlet1
    {0x2B6F, 30,  10, ArmorClass::Metal  },  // i_helm_circlet2
    {0x2B70, 30,  10, ArmorClass::Metal  },  // i_helm_circlet3
    {0x2B71, 30,  25, ArmorClass::Metal  },  // i_helm_raven
    {0x2B72, 30,  25, ArmorClass::Metal  },  // i_helm_vulture
    {0x2B73, 30,  25, ArmorClass::Metal  },  // i_helm_winged_2
    {0x2B74, 20,  25, ArmorClass::Leather},  // i_hide_chest
    {0x2B75, 20,  15, ArmorClass::Leather},  // i_hide_gloves
    {0x2B76, 20,  15, ArmorClass::Leather},  // i_hide_gorget
    {0x2B77, 20,  20, ArmorClass::Leather},  // i_hide_arms
    {0x2B78, 20,  25, ArmorClass::Leather},  // i_hide_leggings
    {0x2B79, 20,  25, ArmorClass::Leather},  // i_hide_chest_female
    {0x317B, 13,  20, ArmorClass::Leather},  // i_leaf_chest
    {0x317C, 13,  10, ArmorClass::Leather},  // i_leaf_gloves
    {0x317D, 13,  10, ArmorClass::Leather},  // i_leaf_gorget
    {0x317E, 13,  15, ArmorClass::Leather},  // i_leaf_arms
    {0x317F, 13,  20, ArmorClass::Leather},  // i_leaf_leggings
    {0x3180, 13,  10, ArmorClass::Leather},  // i_leaf_tonlet
    {0x3181, 13,  20, ArmorClass::Leather},  // i_leaf_chest_female
    {0xA649, 20,  35, ArmorClass::Shield },  // i_shield_pirate
    {0xA7E2,  5,  10, ArmorClass::Leather},  // i_belt_demon
    {0xA831, 20,  35, ArmorClass::Shield },  // i_shield_hildebrandt
};

// i_pickaxe, layer 1, ReqStr=50 -- the same pair Professions.cpp names.
constexpr u16 kMinePickaxe[] = {0x0E85, 0x0E86};
// The four big fish this shard's water yields, and the blades that cut them.
// One whole fish becomes four cut steaks; the steaks are what a vendor buys
// and what a cook can turn into a meal.
constexpr u16 kWholeFish[] = {0x09CC, 0x09CD, 0x09CE, 0x09CF};
constexpr u16 kBlades[]    = {0x0F51, 0x0F52, 0x13F5, 0x13F6};  // dagger, knives
// i_kindling 0x0DE1 becomes ITEMID_CAMPFIRE 0x0DE3 when lit.
constexpr u16 kKindlingGraphic = 0x0DE1;
constexpr u16 kCampfireGraphic = 0x0DE3;
// The three t_cooking itemdefs -- rolling pin, fry pan, flour sifter. Any of
// them opens the cooking skillmenu (sm_legacy_cooking.scp: "Triggered by
// DClicking a i_fry_pan, rolling pin or flour sifter") and any satisfies
// SKILLMAKE=Cooking 0.0,t_cooking on i_fish_cut_cooked. The MATERIAL cannot
// open this menu: a raw steak is IT_MEAT_RAW and double-clicking it is
// answered by EATING it (Source-X CCharUse.cpp:1862 -> Use_Eat).
constexpr u16 kCookingToolGfx[] = {0x1043, 0x097F, 0x103E};

// SMELTING, taken from the shard and not from generic UO lore.
// runtime/scripts/types/type_ore.scp is the entire mechanic, on t_ore @dclick:
//   if <cont> != <src.findlayer.21>   -> the ore must be IN THE BACKPACK
//   elif <src.mining> >= <skillmake.1.value>-100
//   if <src.isneartype t_forge 2>     -> and within TWO TILES of a forge
//     serv.newitem <tdata1>           -> which for i_ore_iron is i_ingot_iron
//   else "You must be near a forge."  (cliloc 1044265)
// There is no crafting menu anywhere in that path, which is why DoCraft --
// which knows only how to walk menus -- could never have smelted, and why
// mining filled a pack with ore that nothing would ever turn into metal.
//
// i_ore_iron is ITEMDEF 019b7 with DUPELIST=019b8,019b9,019ba (the 1/2/3/4-ore
// piles), so all four count as ore. SKILLMAKE=mining 0.0 makes the skill gate
// `mining >= -10.0` -- always true, so any miner at all can smelt iron.
constexpr u16 kIronOre[] = {0x19B7, 0x19B8, 0x19B9, 0x19BA};
// Which graphics are forges lives in Client::NearestForge -- it reads the map
// statics, and the id set is derived there from TYPE=t_forge.
// <src.isneartype t_forge 2> reads like "two tiles is fine". It is not: at a
// Chebyshev distance of exactly 2 -- standing at 2533,572 with the forge at
// 2535,571 -- the shard answered "You must be near a forge to smelt" on every
// swing. Sphere measures that distance to the object's own footprint, so the
// only reliable rule is to stand ADJACENT and then click.
constexpr i32 kForgeReach = 1;
// i_fish_cut_raw 0x097A -- four to a whole fish, and food once cooked.
constexpr u16 kFishRawSteak = 0x097A;
// Mirrors kGoldWorthCarrying in Needs.cpp: the need and the goal must agree on
// how much coin a life keeps, or one will ask for a trip the other undoes.
// The runtime half of Needs.cpp's kMaxGoldCarried: what is DEPOSITED must
// agree with what the need thought was surplus, or the trip achieves nothing.
// How many turns of a never-failing self-use skill (Meditation) before the
// planner is given another chance to choose. Small: the point is that other
// needs get looked at, not that practice is discouraged.
// How long to let a craft resolve before deciding it failed silently. Longer
// than any single craft delay on this shard, short enough not to stall a
// batch.
constexpr i64 kCraftResolveMs = 8000;
constexpr i32 kSelfPracticeBeforeRethink = 6;
constexpr i32 kMaxGoldCarriedRt = 800;
constexpr i32 kGoldWorthCarryingRt = 500;
// Close enough to a mining place to call it a day at work.
// Distance to the EDGE of a mining area that still counts as being at work.
// Minoc's ore sits in resource areas of radius 20 scattered round the town, so
// a miner in Minoc is usually a short walk from one rather than standing on it.
constexpr i32 kAtOreDistance = 45;
// Close enough to actually swing: the pickaxe reaches two tiles, so anything
// beyond a few means walking in rather than hammering the ground.
constexpr i32 kMineReach = 6;
constexpr i32 kMaxMineTrips = 3;
// How far DoMine scans for genuine rock once travel says it has arrived.
// TravelToResource is satisfied at the resource area's RADIUS (Minoc's is
// r=20), so "arrived" can still be a full radius from the rock; the scan must
// out-reach that gap. 24 also covers the mine's real rects from the recorded
// centroid (a_minoc_mine_1_1 spans y474-504 around P=2558,499,
// maps/map0/map0_areas.scp:1954-1960).
constexpr i32 kMineScanRadius = 24;
// One mining attempt is 2-6 strokes (CCharSkill.cpp:1463) at DELAY=1.6s
// (skill45_mining.scp), so ~10s of silence is NORMAL. Poll the journal gently
// and only give up on a verdict well past the longest legitimate attempt.
constexpr i64 kMinePollMs = 1500;
constexpr i64 kMineResolveMs = 15000;
constexpr i32 kMaxBankShouts = 3;
// How far a spoken offer carries, in tiles.
constexpr int kTradeEarshot = 16;

// A SHOP IS A SHOP, WHEREVER YOU ARE STANDING.
//
// Every service trip passed the character's HOME CITY as a region hint, and
// that hint forces NearestPlaceWithServiceInRegion -- so a miner standing in
// Minoc who wanted a tinker was sent 440 tiles back to Vesper, because Vesper
// is where he happens to live. "it shouldnt go to vesper always, that is
// wrong" (project owner, 2026-08-29).
//
// Home is where a character LIVES. It is not a reason to walk past the shop in
// front of it. Returning nullptr makes the atlas pick the nearest place with
// the service, which is what a person does.
//
// Kept as a named function rather than deleting the argument, because there
// may yet be an errand where home genuinely matters -- a player market to be
// found at one's own bank, say -- and this is where that exception would go.
const char* HomeOrNearest(const std::string&) { return nullptr; }
constexpr i64 kNoBankCooldownMs = 120000;
// THE 36 ARMOUR PIECES AN ARMORER ACTUALLY STOCKS, generated from every
// SELL row in VENDOR_S_ARMORER_LEATHER / _RING / _CHAIN / _PLATE /
// _SHIELDS. Everything else with an ARMOR value is smith-crafted or
// looted, and shopping for it is a wasted trip.
constexpr u16 kSoldArmour[] = {
    0x13BB, 0x13BE, 0x13BF, 0x13C5, 0x13C6, 0x13C7, 0x13CB, 0x13CC,
    0x13EB, 0x13EC, 0x13EE, 0x13F0, 0x1408, 0x140A, 0x140C, 0x140E,
    0x1410, 0x1411, 0x1412, 0x1413, 0x1414, 0x1415, 0x1B72, 0x1B73,
    0x1B74, 0x1B76, 0x1B78, 0x1B7A, 0x1B7B, 0x1C00, 0x1C02, 0x1C06,
    0x1C08, 0x1C0A, 0x1C0C, 0x1DB9,
};

constexpr u16 kScissorsGraphic  = 0x0F9E;
constexpr u16 kWoolGraphic      = 0x0DF8;
constexpr u16 kYarnGraphic      = 0x0E1D;
constexpr u16 kThreadGraphic    = 0x0FA0;
constexpr u16 kClothBoltGraphic = 0x0F95;
constexpr u16 kClothGraphic     = 0x175D;
constexpr u16 kSpinWheelGraphic = 0x1015;
constexpr u16 kLoomGraphic      = 0x105F;
constexpr u16 kSheepBody        = 0x00CF;

// CLOTHING A FIGHTER CAN CUT UP FOR BANDAGES.
//
// Stock Source-X has always allowed this -- CClientTarg.cpp:2154, case
// IT_CLOTHING, yielding weight/WEIGHT_UNITS bandages -- but this shard's
// types/type_scissors.scp answered every type except cloth, bolt and hide with
// RETURN 1, which tells Sphere the use was handled and stops the engine's own
// behaviour from running. t_clothing was added to that script on 2026-08-29,
// so a looted shirt is a bandage supply again.
//
// This is the cheapest bandage there is: no sheep, no wheel, no loom, and the
// garment came off a corpse the character had to kill anyway.
//
// The list is GENERATED from this shard's itemdefs, not chosen: every
// TYPE=t_clothing def weighing 6.0 or more with VALUE at or under 60, minus
// anything named robe / elven / gargish. The weight floor keeps it to garments
// worth the gesture (yield is weight in tenths, so 6.0 is six bandages) and
// the value ceiling keeps a character from shredding something it could sell
// or wear -- the Revolution special robes above all, which are the one piece
// of clothing a mage genuinely needs.
constexpr u16 kCuttableClothing[] = {
    0x1516, 0x1517, 0x152E, 0x1531, 0x1537, 0x1539, 0x153B, 0x153D,  // skirts,
    0x170B, 0x170F, 0x1713, 0x1716, 0x1717, 0x1718, 0x1719, 0x171A,  // shirts,
    0x171B, 0x1EFD, 0x1EFF, 0x1F01, 0x1F7B, 0x1FA1, 0x1FFD, 0x25EA,  // pants,
    0x25F2, 0x2649, 0x264D, 0x264F, 0x2651, 0x2655, 0x265F, 0x2661,  // hats,
    0x2663, 0x2665, 0x2667, 0x266B, 0x266D, 0x2671, 0x2673, 0x267B,  // dresses,
    0x267D, 0x267F, 0x2681, 0x2782, 0x2783, 0x2794, 0x2796, 0x27A0,  // aprons
    0x27A1,
};

constexpr u16 kSpellbookGraphic = 0x0EFA;
constexpr u16 kFirstScrollGraphic = 0x1F2E;   // spell 1
constexpr u16 kLastScrollGraphic  = 0x1F6D;   // spell 64
// [SPELL 2] s_create_food -- targetless, 4 mana, MAGERY 10.0 to try.
// The practice spell: nothing to target wrongly, nobody to anger.
constexpr int kSpellCreateFood = 2;
constexpr i32 kCreateFoodMana  = 4;

// The word the NPC expects after "train". Sphere matches on the skill KEY from
// skills/skill<N>_<name>.scp, not on our own label.
const char* SkillKey(int id) {
    switch (id) {
        case rules::kLumberjacking:   return "Lumberjacking";
        case rules::kFishing:         return "Fishing";
        case rules::kCooking:         return "Cooking";
        case rules::kCarpentry:       return "Carpentry";
        case rules::kSwordsmanship:   return "Swordsmanship";
        case rules::kTactics:         return "Tactics";
        case rules::kAnatomy:         return "Anatomy";
        case rules::kHealing:         return "Healing";
        case rules::kMining:          return "Mining";
        case rules::kBlacksmithing:   return "Blacksmithing";
        case rules::kMagery:          return "Magery";
        case rules::kMeditation:      return "Meditation";
        case rules::kAlchemy:         return "Alchemy";
        case rules::kTinkering:       return "Tinkering";
        case rules::kArmsLore:        return "ArmsLore";
        case rules::kEvaluatingIntel: return "EvaluatingIntel";
        case rules::kInscription:     return "Inscription";
        case rules::kTaming:          return "Taming";
        case rules::kAnimalLore:      return "AnimalLore";
        case rules::kVeterinary:      return "Veterinary";
        default:                      return "";
    }
}

// Which trade teaches which skill. A guildmaster teaches to 50.0 where a plain
// tradesman stops at 30.0 (c_human_guildmasters.scp:23), so the guild is
// preferred wherever one exists. Pairs are (paperdoll-title substring, the
// world model's service) so both the mobile scan and the travel layer agree.
struct TrainerFor {
    int         skillId;
    const char* trade;
    wm::Service service;
};
const TrainerFor kTrainers[] = {
    // THREE SKILLS PROFESSIONS ASK FOR AND NOBODY COULD TEACH.
    //
    // Parrying (fencer, macer, archer), Tailoring (full_crafter) and
    // Cartography (treasure_hunter) are all marked viaTrainer in the
    // catalogue and had no row here. TrainerForSkill returned null,
    // trainerTrade_ stayed EMPTY, and the goal asked
    // NearestMobileWithTrade("") -- which matches nothing, ever. Live:
    // goal_failed=TRAIN_AT_NPC reason="no '' reachable after 3 trips",
    // eight times in one six-bot run (run_m7/f6_*). The comment below this
    // table has warned about exactly this failure since the fisher hit it.
    //
    // Trades chosen from who actually HAS the skill on this shard, not from
    // generic UO: c_guild_warrior carries PARRYING={75.0 98.0}, the tailor
    // guildmaster carries TAILORING, and the mapmaker carries
    // CARTOGRAPHY={50.0 75.0} (c_human_guildmasters.scp, c_vendor_human.scp).
    {rules::kParrying,        "armorer",     wm::Service::Blacksmith},
    {rules::kTailoring,       "tailor",      wm::Service::Tailor},
    {rules::kCartography,     "mapmaker",    wm::Service::Mapmaker},
    // "swordsman" WAS A WORD NOBODY ON THIS SHARD WEARS.
    //
    // These two rows were dead for exactly the same reason the three missing
    // rows above were dead -- the goal asked NearestMobileWithTrade() for a
    // title that matches nothing -- and they were harder to spot, because a
    // wrong name looks like a right one in the table. Zero NPC chardefs across
    // runtime/scripts/npcs/*.scp carry "the swordsman"; the count is 0, next to
    // 12 for "the weaponsmith" and 6 for "the armorer".
    //
    // The warrior guildmaster is the right teacher by the owner's rule -- every
    // skill-related guildmaster teaches to 30.0 -- and it is the wrong ANSWER
    // here, because it cannot be reached. There are 7 in the world and no PLACE
    // in the atlas carries a warrior service, so TravelToService has nowhere to
    // send anyone; the nearest routable PLACE to Britain's is 47 tiles away,
    // well past scan range. The craft guildmasters work only because they stand
    // inside the shop their service already points at -- that is how Tarath
    // found Jarman and bought Carpentry. The warrior guild has no shop.
    //
    // Weaponsmiths and armorers do stand in one. Both carry the whole combat
    // set -- SWORDSMANSHIP/FENCING/MACEFIGHTING {15.0 38.0}, PARRYING/TACTICS
    // {45.0 68.0} (c_vendor_human.scp) -- both map to Service::Blacksmith,
    // which has 33 PLACEs, and 68 of them are spawned in the world save.
    // Training caps at the NPC's own roll, so a low-rolled weaponsmith teaches
    // less than a guildmaster would. A trainer that is sometimes weaker beats
    // one that is always unreachable.
    //
    // Routing guildmasters properly is real work and belongs with the atlas
    // generator, which today reads only vendor SPAWNERS and never the world
    // save's WORLDCHARs -- noted in docs/M4_OPEN_LOOSE_ENDS.md rather than
    // bodged here.
    {rules::kSwordsmanship,   "weaponsmith", wm::Service::Blacksmith},
    {rules::kTactics,         "weaponsmith", wm::Service::Blacksmith},
    {rules::kAnatomy,         "healer",      wm::Service::Healer},
    {rules::kHealing,         "healer",      wm::Service::Healer},
    {rules::kBlacksmithing,   "blacksmith",  wm::Service::Blacksmith},
    {rules::kMining,          "blacksmith",  wm::Service::Blacksmith},
    {rules::kTinkering,       "tinker",      wm::Service::Tinker},
    {rules::kArmsLore,        "blacksmith",  wm::Service::Blacksmith},
    {rules::kMagery,          "mage",        wm::Service::Mage},
    {rules::kMeditation,      "mage",        wm::Service::Mage},
    {rules::kEvaluatingIntel, "mage",        wm::Service::Mage},
    {rules::kInscription,     "scribe",      wm::Service::Scribe},
    {rules::kAlchemy,         "alchemist",   wm::Service::Alchemist},
    {rules::kTaming,          "animal",      wm::Service::Stablemaster},
    {rules::kAnimalLore,      "animal",      wm::Service::Stablemaster},
    {rules::kVeterinary,      "animal",      wm::Service::Stablemaster},
    {rules::kLumberjacking,   "carpenter",   wm::Service::Carpenter},
    // The fisher's own two. Without these NextSkillToBuy picks Fishing,
    // TrainerForSkill returns null, trainerTrade_ stays empty, and the goal
    // asks NearestMobileWithTrade("") -- burning every trip and failing.
    {rules::kFishing,         "fisher",      wm::Service::Fisherman},
    {rules::kCooking,         "cook",        wm::Service::Cook},
    {rules::kCarpentry,       "carpenter",   wm::Service::Carpenter},
};

const TrainerFor* TrainerForSkill(int id) {
    for (const TrainerFor& t : kTrainers) {
        if (t.skillId == id) return &t;
    }
    return nullptr;
}

// A buyer trade, as the world model names its destination. The item->trade
// half of this lives in uo::market, because it is shard vendor data that the
// need layer also has to ask about; only the trade->place mapping is here,
// where the world model is in scope.
wm::Service ServiceForTrade(const char* trade) {
    struct Row { const char* trade; wm::Service service; };
    static const Row kRows[] = {
        {"carpenter",   wm::Service::Carpenter},
        {"provisioner", wm::Service::Provisioner},
        {"tinker",      wm::Service::Tinker},
        {"bowyer",      wm::Service::Bowyer},
        {"blacksmith",  wm::Service::Blacksmith},
        {"jeweler",     wm::Service::Jeweler},
        {"tailor",      wm::Service::Tailor},
        {"scribe",      wm::Service::Scribe},
        {"alchemist",   wm::Service::Alchemist},
        {"mage",        wm::Service::Mage},
        {"fisher",      wm::Service::Fisherman},
        {"cook",        wm::Service::Cook},
        {"weaponsmith", wm::Service::Blacksmith},
    };
    for (const Row& r : kRows) {
        if (std::strcmp(r.trade, trade) == 0) return r.service;
    }
    return wm::Service::GeneralVendor;
}

// What a life gathers, as the ATLAS names it. One table, so "where is my
// work" is answered from the world model for every profession instead of
// TreeCount standing in for all of them.
//
// The atlas already files every one of these: atlasgen reads the mine and dock
// AREADEF names and measures foliage density off the client's own statics, so
// twenty docks come through tagged resources=fishing and the mines likewise.
// Nothing needed adding to the data -- only to the code that had been asking
// it one question on behalf of every character.
struct GatherKind {
    const char*      gathers;   // Profession::gathers
    wm::ResourceKind kind;
};
const GatherKind kGatherKinds[] = {
    {"logs", wm::ResourceKind::Lumber},
    {"ore",  wm::ResourceKind::Mining},
    {"fish", wm::ResourceKind::Fishing},
};

wm::ResourceKind ResourceKindFor(const std::string& gathers) {
    for (const GatherKind& g : kGatherKinds) {
        if (gathers == g.gathers) return g.kind;
    }
    return wm::ResourceKind::None;
}

// The two hand layers. Which one an item lands on is decided by THIS SHARD'S
// tiledata, not by generic UO: the newbie katana wears on layer 1 and the
// hatchet wears on layer 2, so a bot that hardcodes "the weapon layer" gets
// `worn on a different layer` from the server and never arms its own axe.
// Equips therefore ask for layer 0 -- "wherever this belongs" -- and the hands
// are READ as a pair.
constexpr u8 kLayerHand1 = 0x01;
constexpr u8 kLayerHand2 = 0x02;
constexpr u8 kLayerServerChooses = 0x00;

i32 CountAny(Client& c, const u16* list, usize n) {
    i32 total = 0;
    for (usize i = 0; i < n; ++i) total += static_cast<i32>(c.BackpackItemCount(list[i]));
    return total;
}

u32 FindAny(Client& c, const u16* list, usize n) {
    for (usize i = 0; i < n; ++i) {
        const u32 s = c.FindBackpackItemByGraphic(list[i]);
        if (s) return s;
    }
    return 0;
}

// IS THE OPEN SHOP WINDOW THE ONE WE ARE STANDING IN FRONT OF?
//
// It very often is not. A vendor offer persists after the goal that opened it
// ends, so the next errand inherits somebody else's stock. Ysolde walked from
// a baker to a mage, searched the BAKER's window for blank scrolls, and
// concluded "this 'mage' does not stock i_scroll_blank" -- 746 times, at 60ms
// intervals. The shop was real, the scroll was on its list
// (VENDOR_S_MAGE_SHOP: SELL=i_scroll_blank,250), and the bot was reading the
// wrong shop.
//
// Client::VendorOfferFrom() has always said whose offer it is. Nothing asked.
bool OfferBelongsTo(const Client& c, u32 vendor) {
    return vendor != 0 && c.VendorOfferFrom() == vendor && !c.VendorOffer().empty();
}

bool GraphicIsAny(u16 graphic, const u16* list, usize n) {
    for (usize i = 0; i < n; ++i) {
        if (graphic == list[i]) return true;
    }
    return false;
}

// Serial of the axe currently in a hand, or 0. The newbie kit arms a katana
// (from the Swordsmanship request), so "a hand is full" is NOT "the axe is in
// hand" -- and swinging a katana at a tree earns nothing but "The tool is out
// of charges", forever, silently.
u32 AxeSerialInHand(Client& c) {
    for (u8 layer : {kLayerHand1, kLayerHand2}) {
        const u16 g = c.EquippedGraphicAt(layer);
        if (GraphicIsAny(g, kHatchet, 2) || GraphicIsAny(g, kAxe, 2)) {
            return c.EquippedAtLayer(layer);
        }
    }
    return 0;
}

bool AxeInHand(Client& c) { return AxeSerialInHand(c) != 0; }

bool HandsBusy(Client& c) {
    return c.EquippedAtLayer(kLayerHand1) != 0 || c.EquippedAtLayer(kLayerHand2) != 0;
}

i32 TileDist(i32 ax, i32 ay, i32 bx, i32 by) {
    return std::max(ax > bx ? ax - bx : bx - ax, ay > by ? ay - by : by - ay);
}

}  // namespace

Runner::Runner() = default;
Runner::~Runner() = default;

void Runner::LogLine(const char* fmt, ...) const {
    if (!cfg_.verbose) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // One prefix for the whole autonomous layer, so a run can be read with
    // `findstr [life]` and nothing else. Deliberately NOT a packet trace: the
    // M4 brief asks for choices to be explainable without one.
    LogInfo("[life] %s\n", buf);
}

bool Runner::Configure(const RunnerConfig& cfg, std::string* err) {
    cfg_ = cfg;
    store_ = Store(cfg.dataRoot);

    const std::string id = MakeIdentityId(cfg.accountName, cfg.characterName);

    std::string loadErr;
    if (store_.Load(id, &state_, &loadErr)) {
        LogLine("loaded persistent state for %s (%zu places, %zu suppliers, "
                "%zu events, %zu prior sessions)",
                state_.identity.characterName.c_str(),
                state_.memory.Places().size(), state_.memory.Suppliers().size(),
                state_.memory.Events().size(), state_.sessions.size());
    } else if (!loadErr.empty()) {
        // A corrupt file is a hard stop. Silently starting a fresh life on top
        // of an unreadable one would destroy exactly the history M4 exists to
        // keep, and would look like a successful run.
        if (err) *err = loadErr;
        return false;
    } else {
        state_ = PersistentState{};
        state_.identity.identityId    = id;
        state_.identity.accountName   = cfg.accountName;
        state_.identity.characterName = cfg.characterName;
        const prof::Profession* chosen =
            cfg.professionId.empty() ? nullptr : prof::Find(cfg.professionId.c_str());
        if (!chosen) {
            if (err) {
                *err = "unknown profession '" + cfg.professionId +
                       "' -- see uo::prof::All()";
            }
            return false;
        }
        const prof::ProfCheck pc = prof::Validate(rules::Revolution(), *chosen);
        if (!pc.ok) {
            if (err) {
                *err = "profession '" + chosen->id + "' is not a legal life: " +
                       prof::ProfViolationName(pc.violation);
            }
            return false;
        }
        state_.plan = PlanFromProfession(*chosen);
        LogLine("no prior state for %s: a new %s", id.c_str(),
                chosen->label.c_str());
        LogLine("creation request: %s 50.0 + %s 50.0, stats %d/%d/%d = %d",
                rules::SkillName(chosen->startSkillA), rules::SkillName(chosen->startSkillB),
                chosen->startStr, chosen->startDex, chosen->startInt,
                chosen->startStr + chosen->startDex + chosen->startInt);
    }

    // Which life is asking. Resolved from the plan's family so it works the
    // same for a fresh character and for one reloaded from disk. A plan family
    // with no catalogue entry -- the M4 lumberjack, saved before the catalogue
    // existed -- leaves this null and keeps the old lumberjack needs.
    needCfg_.profession = prof::Find(state_.plan.family.c_str());
    if (!needCfg_.profession) {
        // THE M4 CHARACTER IS NOT A DIFFERENT LIFE, ONLY AN OLDER SPELLING.
        //
        // FrontierLumberjackSwordsman() (Identity.cpp:121) predates the M5
        // catalogue and writes family "frontier_lumberjack_swordsman"; the
        // catalogue registers "lumberjack_swordsman" (Professions.cpp:149).
        // Tarath was created under the old plan, so every reload since has
        // fallen into the branch below and run with a NULL profession -- and
        // a null profession silently disables far more than the comment
        // implies. DoTrainCombat short-circuits on it before WantsToHunt is
        // ever evaluated (Runner.cpp, `!needCfg_.profession`), so the shard's
        // most-run character could never go hunting, and the plan was never
        // rebuilt so it had no viaTrainer flags either. It logged one line
        // about "the original lumberjack needs" and looked fine.
        //
        // Alias the old spelling rather than renaming it: the M4 plan is what
        // that character was actually created with, and rewriting history in
        // Identity.cpp would change what the saved file means.
        if (state_.plan.family == "frontier_lumberjack_swordsman") {
            needCfg_.profession = prof::Find("lumberjack_swordsman");
            if (needCfg_.profession) {
                LogLine("needs: plan family '%s' is the M4 spelling of '%s' "
                        "-- reading the catalogue entry",
                        state_.plan.family.c_str(), needCfg_.profession->id.c_str());
            }
        }
    }
    if (needCfg_.profession) {
        // THE CATALOGUE IS THE INTENTION; the save file only records which
        // life this is. Re-deriving here is not tidiness -- the saved plan
        // carries skills and targets but not the per-target `viaTrainer` and
        // `priority` fields, so a reloaded character silently had nothing it
        // was willing to buy from a trainer, and simply never trained. It
        // logged no error: NextSkillToBuy just returned -1 forever.
        //
        // Anything the character has actually EARNED lives on the server or
        // in Memory, never in the plan, so nothing is lost by rebuilding it.
        state_.plan = PlanFromProfession(*needCfg_.profession);
        LogLine("needs: reading '%s' from the profession catalogue",
                needCfg_.profession->id.c_str());
    } else {
        LogLine("needs: plan family '%s' is not in the catalogue -- using the "
                "original lumberjack needs", state_.plan.family.c_str());
    }

    // Pick a home, once. Deterministic from the identity id rather than random,
    // so the same character always gets the same home even if the state file is
    // lost -- and so a fleet spreads across the map instead of every member
    // rolling the same first entry.
    if (state_.homeCity.empty() && needCfg_.profession) {
        if (needCfg_.profession->homeCities.empty()) {
            // No list at all: Britain, never the index-0 fallback of Yew.
            state_.homeCity = "Britain";
            LogLine("home: %s has no home cities in the catalogue -- Britain",
                    state_.identity.characterName.c_str());
        }
    }
    if (state_.homeCity.empty() && needCfg_.profession &&
        !needCfg_.profession->homeCities.empty()) {
        usize h = 0;
        for (char c : state_.identity.identityId) {
            h = h * 131 + static_cast<unsigned char>(c);
        }
        const std::vector<std::string>& homes = needCfg_.profession->homeCities;
        // A LIFE WHOSE WORK IS GEOGRAPHIC LIVES WHERE THE WORK IS.
        //
        // "miners home city then minoc" and "lumberjacks too" (project owner,
        // 2026-08-29). The hash exists to spread a fleet across the map, which
        // is right for a trade you can practise anywhere -- a scribe, a mage,
        // an alchemist. It is wrong for one you cannot: Minoc is FIRST in the
        // miner's list precisely because that is where the ore is, and Corwyn
        // still rolled Vesper and spent his sessions walking back to it.
        //
        // So a profession that GATHERS something takes the first entry, which
        // the catalogue already orders by where that work actually happens.
        // Everyone else still spreads out.
        const bool workIsPlaceBound = !needCfg_.profession->gathers.empty();
        state_.homeCity = workIsPlaceBound ? homes.front()
                                           : homes[h % homes.size()];
        LogLine("home: %s lives in %s", state_.identity.characterName.c_str(),
                state_.homeCity.c_str());
    }

    // Whatever the source -- a fresh plan or one reloaded from disk -- it has
    // to be a legal Revolution build before the character acts on it.
    const PlanCheck check = ValidatePlan(rules::Revolution(), state_.plan);
    if (!check.ok) {
        if (err) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "build plan is not a legal Revolution build: %s (skill %d)",
                          PlanViolationName(check.violation), check.skillId);
            *err = buf;
        }
        return false;
    }
    LogLine("build plan '%s': %.1f resolved + %.1f unresolved = %.1f/%.1f, "
            "stats %d/%d/%d = %d",
            state_.plan.family.c_str(), check.resolvedTenths / 10.0,
            state_.plan.unresolvedTenths / 10.0, check.plannedTotalTenths / 10.0,
            rules::Revolution().totalSkillCapTenths / 10.0,
            state_.plan.targetStr, state_.plan.targetDex, state_.plan.targetInt,
            check.statTotal);

    // The goal that came back from disk is an INTENTION. Its clock and
    // counters are transient and are rebuilt at reconciliation.
    planner_.Mutable() = state_.goal;
    configured_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Observation -- source of truth C, rebuilt from scratch every tick.
// ---------------------------------------------------------------------------

Observation Runner::Observe(Client& client, i64 nowMs) const {
    Observation obs;
    obs.nowMs = nowMs;
    obs.inWorld = client.IsInWorld();
    if (!obs.inWorld) return obs;

    obs.dead    = client.IsDead();
    obs.mounted = client.PlayerIsMounted();
    obs.warMode = client.WarModeOn();

    obs.x = client.PlayerX();
    obs.y = client.PlayerY();
    obs.z = client.PlayerZ();

    obs.hp    = client.PlayerHp();
    obs.hpMax = client.PlayerHpMax();
    obs.mana  = client.PlayerMana();

    obs.str   = client.PlayerStr();
    obs.dex   = client.PlayerDex();
    obs.intel = client.PlayerInt();

    for (const SkillTarget& t : state_.plan.skills) {
        const i32 base = client.PlayerSkillBase(static_cast<u16>(t.skillId));
        if (base >= 0) obs.skills.push_back({t.skillId, base});
    }

    obs.gold      = client.PlayerGold();
    obs.goldOnHand = static_cast<i32>(client.BackpackItemCount(kGoldCoin));
    obs.coinWanted = coinWanted_;
    obs.weight    = client.PlayerWeight();
    obs.maxWeight = client.PlayerMaxWeight();
    if (obs.maxWeight <= 0) {
        // The server only sends maxWeight when its status packet is flagged >= 5
        // (Client.cpp OnStats), and this shard sends less -- so it is ALWAYS 0
        // here and WeightFraction() was permanently 0. The character therefore
        // never noticed it was full, never banked, and simply overflowed:
        // "You put the logs at your feet. It is too heavy.."
        //
        // Derive it from the engine's own formula instead of guessing.
        // CResourceCalc.cpp:24 -- 40 + STR * 3.5, plus a flat +60 for humans
        // with the Strong Back racial. Tenths of stones there; PlayerWeight()
        // is in whole stones, so this is the stone figure.
        obs.maxWeight = 40 + (obs.str * 35) / 10;
    }
    // The server's own words are the definitive signal, and they arrive whether
    // or not it ever told us a capacity.
    obs.overloaded = client.JournalSaidSince("it is too heavy", overloadWatchMs_);

    obs.bandages = static_cast<i32>(client.BackpackItemCount(kBandage));
    obs.logs     = static_cast<i32>(client.BackpackItemCount(kLog));
    obs.food     = CountAny(client, kFood, sizeof(kFood) / sizeof(kFood[0]));
    // HUNGER AS THE SERVER SAYS IT. "You are <level>" over the eight levels
    // in core/messages.scp:470-477. A player reads this line; so do we.
    // Watermarked from session start rather than a rolling mark: hunger is
    // a STATE, and the last thing it said is still true until it says
    // otherwise.
    obs.starving = client.JournalSaidSince("you are starving", sessionStartJournalMs_);
    obs.hungry   = obs.starving ||
                   client.JournalSaidSince("you are very hungry", sessionStartJournalMs_) ||
                   client.JournalSaidSince("you are hungry", sessionStartJournalMs_);

    obs.axeInPack = FindAny(client, kHatchet, 2) != 0 || FindAny(client, kAxe, 2) != 0;
    obs.weaponEquipped = HandsBusy(client);
    // Read the worn graphic rather than inferring from a full hand. The first
    // live run swung the newbie katana at a tree for two minutes because a
    // filled weapon hand was taken to mean "the axe is out".
    obs.axeEquipped = AxeInHand(client);

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    obs.marketQuiet = obs.nowMs < marketQuietUntilMs_;
    obs.hostilesNear = static_cast<i32>(hostiles.size());
    const u32 warTarget = client.WarWatchdog().TargetSerial();
    i32 adjacent = 0;
    for (const Client::HostileHit& h : hostiles) {
        if (TileDist(h.x, h.y, obs.x, obs.y) <= 1) ++adjacent;
    }
    // A FIGHT, not a crowd. Adjacency alone is not being attacked: Session B
    // opened combat with a COW that happened to be standing next to it, and
    // then spent twenty seconds failing to dent it. The war watchdog's target
    // is the client's own record that a fight is actually happening, so that
    // is the gate; adjacent hostiles only scale the pressure once it is.
    obs.underAttack = warTarget != 0;
    obs.attackersOnMe = obs.underAttack ? std::max(1, adjacent) : 0;

    const travel::DeathRecord& death = client.Knowledge().LastDeath();
    obs.corpseKnown = death.valid && death.corpseSerial != 0;
    obs.corpseX = death.x;
    obs.corpseY = death.y;
    obs.corpseRecoveryAttempts = death.recoveryAttempts;

    // Arrival is a claim about the TILE. `TreeCount` asks the shard's own
    // statics whether there is anything here to chop, which travel success
    // does not answer (see docs/UOOFFLINE_BEHAVIOR_AUDIT.md section 3.6).
    // AM I WHERE MY WORK IS -- for THIS life. TreeCount answered on behalf of
    // every profession, so a miner at its vein, a mage in its tower and a
    // fisher on a dock were all told they were "not at work" and sent hiking
    // toward the nearest forest looking for logs they had no use for.
    const std::string gathers =
        needCfg_.profession ? needCfg_.profession->gathers : std::string("logs");
    if (gathers == "fish") {
        Client::WaterHit w;
        obs.atWorkSite = client.NearestWater(obs.x, obs.y, 4, &w);
    } else if (gathers.empty()) {
        // A life that gathers nothing is never away from its work: a mage or
        // an alchemist works wherever it happens to be standing.
        obs.atWorkSite = true;
    } else if (gathers == "ore") {
        // A MINER IS AT WORK WHERE THE ORE IS. This fell through to counting
        // TREES -- the comment above says a miner at its vein was the very
        // case this block existed to fix, and ore was still never handled. So
        // a miner standing in Minoc, the mining town, was judged by how many
        // trees happened to be around him. Corwyn trained in Minoc and walked
        // back to Vesper without a single swing.
        const i32 d = client.DistanceToResource(wm::ResourceKind::Mining);
        obs.atWorkSite = d >= 0 && d <= kAtOreDistance;
    } else {
        obs.atWorkSite = client.TreeCount(obs.x, obs.y, cfg_.searchRadius) > 0;
    }
    // NO SKILL ADVANCES INSIDE A REGION_FLAG_SAFE AREA (Source-X
    // Skill_Experience; docs/REVOLUTION_GAMEPLAY_TRUTH.md 3.2 point 1).
    // Twenty-five of them on map 0, and the ones that matter are the
    // SHRINES -- quiet, safe-looking, and exactly where a bot would
    // otherwise choose to stand and meditate for an hour to no effect.
    {
        const wm::Region* here = client.CurrentRegion();
        obs.inNoGainRegion = here && here->flags.safe;
    }
    obs.treeAdjacent = client.TreeCount(obs.x, obs.y, 2) > 0;
    obs.atBank = client.BankContainer() != 0 &&
                 client.ContainerKnown(client.BankContainer());

    // READ THE BOX while it is open, and KEEP what it said.
    //
    // A character that does not remember its own bank cannot sell what it
    // banked -- it has no reason to walk to a box it does not know holds
    // anything, so everything it ever gathered leaves the economy for good.
    // This is not omniscience: it is remembering your own container, which is
    // the most ordinary thing a player does.
    obs.bankOpen = obs.atBank;
    if (obs.atBank) {
        const u32 box = client.BankContainer();
        std::vector<market::Stock> fresh;
        const usize n = client.ContainerItemCount(box);
        for (usize i = 0; i < n; ++i) {
            u32 serial = 0; u16 gfx = 0, amount = 0;
            if (!client.ContainerItemAt(box, i, &serial, &gfx, &amount)) continue;
            const char* name = econ::ItemNameForGraphic(gfx);
            if (!name) continue;          // nothing we have a name for
            if (amount == 0) amount = 1;
            bool merged = false;
            for (market::Stock& k : fresh) {
                if (k.item == name) { k.qty += amount; merged = true; break; }
            }
            if (!merged) fresh.push_back({name, static_cast<i32>(amount)});
        }
        obs.bank = std::move(fresh);
    } else {
        // Away from the box: what the character REMEMBERS is what it has.
        obs.bank = state_.bank;
    }

    // The pack, as the M7 economy layer wants it: quantities keyed by itemdef
    // defname. Built from what THIS life produces and consumes, so the loop is
    // over a handful of names rather than every item on the shard. One name can
    // have several graphics -- iron ingots are 0x1BEF/0x1BF0/0x1BF1 by stack
    // size -- so a caller that checked only the first would miss most of a pack.
    if (needCfg_.profession) {
        auto countInto = [&](const std::string& item) {
            for (const market::Stock& have : obs.pack) {
                if (have.item == item) return;   // already counted
            }
            i32 n = 0;
            for (u16 g : econ::GraphicsForItem(item.c_str())) {
                n += static_cast<i32>(client.BackpackItemCount(g));
            }
            obs.pack.push_back({item, n});
        };
        for (const std::string& it : needCfg_.profession->produces) countInto(it);
        for (const std::string& it : needCfg_.profession->consumes) countInto(it);
    }

    // Which of this plan's trainable skills have already been refused. Read
    // from memory, so one wasted walk teaches the character for good -- and
    // so the answer survives a logout.
    for (usize i = 0; i < state_.plan.skills.size(); ++i) {
        if (i >= state_.plan.viaTrainer.size() || !state_.plan.viaTrainer[i]) continue;
        const int id = state_.plan.skills[i].skillId;
        const TrainerFor* tf = TrainerForSkill(id);
        // Either the trade is exhausted (several NPCs of it have said no), or
        // one of them has said the character is already past teaching -- which
        // no other NPC can undo, so it counts on its own.
        if (state_.memory.TrainerSaysMaxed(id) ||
            (tf && state_.memory.TrainerRefused(id, tf->trade))) {
            obs.trainerRefusedSkills.push_back(id);
        }
    }

    // Which of this life's own tools it is actually holding. Checked in the
    // pack AND in both hands: several Sphere gathering skills read SRC.WEAPON,
    // so "carried" and "wielded" are different questions.
    if (needCfg_.profession) {
        for (const prof::ToolNeed& t : needCfg_.profession->tools) {
            for (u16 g : t.graphics) {
                if (client.FindBackpackItemByGraphic(g) ||
                    client.EquippedGraphicAt(kLayerHand1) == g ||
                    client.EquippedGraphicAt(kLayerHand2) == g) {
                    obs.toolsHeld.push_back(t.name);
                    break;
                }
            }
        }
    }

    // What this life wants to BUY next. A generic tradesman teaches to 30.0
    // (sphere.ini NPCTrainPercent=30 of a GM's 100.0); a guildmaster overrides
    // to 50.0. The ceiling passed here is the lower one, so the character
    // never pays for a skill it has already grown past a plain trainer.
    // Which skill this life should PRACTISE -- do the thing that raises it.
    // Distinct from wantTrainSkill, which is a skill to BUY from a guildmaster
    // and stops at 30.0. Practice is how a skill reaches 100.
    obs.wantPracticeSkill = -1;
    for (const SkillTarget& t : state_.plan.skills) {
        if (obs.SkillTenths(t.skillId) >= t.tenths) continue;
        if (t.skillId != rules::kMeditation &&
            t.skillId != rules::kMagery) continue;   // see DoPracticeSkill
        obs.wantPracticeSkill = t.skillId;
        break;
    }

    // THE BOOK, AND WHAT IS IN IT.
    //
    // i_spellbook is ITEMDEF 0efa on this shard. The count is what the client
    // has been told is inside it, which is only populated after the book has
    // been opened once -- so 0 here means "no book, or a book we have not
    // looked in yet", and the goal opens it rather than assuming it is empty.
    // A BOOK IN THE HAND IS STILL A BOOK. This looked in the backpack only,
    // while the tools scan a few lines above checks the same graphic in the
    // pack AND both hands -- so every "needs considered" line reported
    // held=[spellbook,] while spellbookSerial stayed 0. DoFillSpellbook trusts
    // this value, so Ilyandra spent a whole life believing she had no book:
    // "opening the book" never fired once in 1,300 log lines, she tried to BUY
    // a second spellbook 32 times, and Magery could never be practised
    // ("Create Food is not in this character's spellbook") because the book she
    // was wearing was invisible to the only code that reads it.
    obs.spellbookSerial = client.FindBackpackItemByGraphic(kSpellbookGraphic);
    if (!obs.spellbookSerial) {
        for (u8 layer : {kLayerHand1, kLayerHand2}) {
            if (client.EquippedGraphicAt(layer) == kSpellbookGraphic) {
                obs.spellbookSerial = client.EquippedAtLayer(layer);
                break;
            }
        }
    }
    obs.spellsKnown =
        obs.spellbookSerial
            ? static_cast<int>(client.ContainerItemCount(obs.spellbookSerial))
            : 0;

    obs.wantTrainSkill = NextSkillToBuy(state_.plan, obs, 300);
    if (obs.wantTrainSkill >= 0) {
        for (const SkillTarget& t : state_.plan.skills) {
            if (t.skillId == obs.wantTrainSkill) { obs.wantTrainTarget = t.tenths; break; }
        }
    }

    return obs;
}

// ---------------------------------------------------------------------------
// Learning. Only from things actually observed.
// ---------------------------------------------------------------------------

// Take the axe in hand, unequipping whatever is there first.
//
// A hand already holding something cannot take a second weapon: Sphere answers
// the lift-and-wear with "You put the hatchet in your pack" and the character
// keeps swinging the newbie katana at trees. So this is two actions, and the
// first live run is the reason it is not one.
//
// Returns true while the arming is still in progress (the caller should wait),
// false when the axe is in hand or there is no axe to arm.
bool Runner::ArmAxe(Client& client, const Observation& obs) {
    if (AxeInHand(client)) return false;
    if (client.ActionBusy()) return true;

    const u32 hatchet = FindAny(client, kHatchet, 2);
    const u32 axe = hatchet ? hatchet : FindAny(client, kAxe, 2);
    if (!axe) return false;   // nothing to arm; the caller decides what that means

    // Clear BOTH hands. Which one the axe wants is the shard's decision, so
    // the only reliable way to make room is to empty the pair.
    for (u8 layer : {kLayerHand1, kLayerHand2}) {
        const u32 worn = client.EquippedAtLayer(layer);
        if (!worn) continue;
        LogLine("arming the axe: taking off 0x%04X from layer %u first",
                client.EquippedGraphicAt(layer), static_cast<unsigned>(layer));
        client.ActionUnequip(worn);
        nextActionMs_ = obs.nowMs + 1400;
        return true;
    }

    // Layer 0 = "wherever this belongs". Naming a layer here is how the first
    // live run earned `worn on a different layer` and never armed its axe.
    LogLine("arming the axe");
    client.ActionEquip(axe, kLayerServerChooses);
    nextActionMs_ = obs.nowMs + 1600;
    return true;
}

// ---------------------------------------------------------------------------
// Holding the build to its caps.
//
// A build plan that is never enforced is a wish. Revolution's caps are 700.0
// skill and 225 stat, and both are reached by ACCUMULATION -- so a character
// that never locks anything eventually spends its budget on whatever it
// happened to use most, not on what it planned.
//
// The policy is deliberately narrow, because the plan is:
//   * a PLANNED skill at or past its target  -> LOCK  (stop spending budget)
//   * a PLANNED skill below its target        -> UP    (keep training it)
//   * an UNPLANNED skill                      -> left alone. The 200 unresolved
//     points are unspent ON PURPOSE, and setting them DOWN would quietly
//     decide the rest of the build.
//   * a stat at or past its target            -> LOCK
//   * a stat below its target                 -> UP
//
// Nothing here raises a skill or a stat. It moves the arrow beside it, which
// is what a player does, and the server remains free to refuse.
void Runner::MaintainBuildLocks(Client& client, const Observation& obs) {
    if (obs.nowMs < nextLockCheckMs_) return;
    nextLockCheckMs_ = obs.nowMs + 30000;

    const rules::Profile& p = rules::Revolution();

    // --- DO NOT LOCK EARLY. -----------------------------------------------
    //
    // A lock is an END-OF-BUILD instrument. Its only job is to stop a finished
    // part of the build from eating budget the unfinished parts still need,
    // and that budget is only scarce near the cap. A young character has
    // hundreds of points of headroom, so locking anything then just freezes
    // growth for no gain -- and a locked stat cannot fall either, which is
    // exactly the redistribution a build near the cap depends on.
    //
    // So: nothing is locked until the character is actually approaching the
    // cap. Below the gate, everything trains up and no lock packet is sent at
    // all. The gates are the last 100 skill points and the last 25 stat
    // points -- the region where the caps start to bind.
    const i32 skillSum = obs.SkillSumTenths();
    const i32 statSum  = obs.str + obs.dex + obs.intel;
    const bool skillsNearCap = skillSum >= p.totalSkillCapTenths - 1000;   // 600.0 of 700.0
    const bool statsNearCap  = statSum  >= p.totalStatCap - 25;            // 200 of 225

    if (!lockGateLogged_ && (skillsNearCap || statsNearCap)) {
        lockGateLogged_ = true;
        LogLine("build: approaching the caps (skills %.1f/%.1f, stats %d/%d) -- "
                "lock management starts now",
                skillSum / 10.0, p.totalSkillCapTenths / 10.0, statSum,
                p.totalStatCap);
    }

    if (skillsNearCap) {
        for (const SkillTarget& t : state_.plan.skills) {
            const i32 have = client.PlayerSkillBase(static_cast<u16>(t.skillId));
            if (have < 0) continue;   // the server has not told us yet
            const u8 want = (have >= t.tenths) ? build::kLockLocked : build::kLockUp;
            const i32 now = client.PlayerSkillLock(static_cast<u16>(t.skillId));
            if (now == static_cast<i32>(want)) continue;
            LogLine("build: %s at %.1f/%.1f -> %s", rules::SkillName(t.skillId),
                    have / 10.0, t.tenths / 10.0,
                    want == build::kLockLocked ? "LOCK" : "train up");
            client.ActionSetSkillLock(static_cast<u16>(t.skillId), want);
        }
    }

    if (statsNearCap) {
        // The client is never told a stat's lock state, so each transition is
        // sent once rather than reconciled against the server.
        const struct { u8 code; i32 have; i32 target; const char* name; } kStats[3] = {
            {0, obs.str,   state_.plan.targetStr, "STR"},
            {1, obs.dex,   state_.plan.targetDex, "DEX"},
            {2, obs.intel, state_.plan.targetInt, "INT"},
        };
        for (const auto& s : kStats) {
            const u8 want = (s.have >= s.target) ? build::kLockLocked : build::kLockUp;
            if (statLockSent_[s.code] == want + 1) continue;
            statLockSent_[s.code] = static_cast<u8>(want + 1);
            LogLine("build: %s at %d/%d -> %s", s.name, s.have, s.target,
                    want == build::kLockLocked ? "LOCK" : "train up");
            client.ActionSetStatLock(s.code, want);
        }
    }
}

// ---------------------------------------------------------------------------
// A SMALL HINT, and no more.
//
// The M4 brief's Phase 15 rule: "Seed only what the character would reasonably
// know at creation ... Everything else should be learned."
//
// A player who rolls a lumberjack knows that Yew has woods. They do NOT know
// which tree still holds wood, nor where the good stands are -- that is earned
// by swinging an axe. So this seeds the few NAMED forests nearest home, marked
// as hints, and nothing else. No global map, no yields, no confidence.
//
// Runs once per life: after the first session the character has its own
// experience and the hints only matter as fallback leads.
void Runner::SeedCommonKnowledge(Client& client, i64 nowMs) {
    if (state_.memory.HasEvent("common_knowledge_seeded")) return;
    if (!client.WorldKnowledgeReady()) return;

    // SEED WHAT THIS LIFE GATHERS. Every character was seeded with forest
    // hints regardless of profession -- a miner learned three woods it would
    // never chop and no mine at all, and a fisher no dock.
    const std::string gathers =
        needCfg_.profession ? needCfg_.profession->gathers : std::string("logs");
    if (gathers.empty()) return;      // a mage gathers nothing; seed nothing
    const wm::ResourceKind kind = ResourceKindFor(gathers);
    if (kind == wm::ResourceKind::None) return;

    std::vector<const wm::Place*> forests;
    client.ResourcePlacesNear(kind, client.PlayerX(),
                              client.PlayerY(), forests);
    if (forests.empty()) return;

    int seeded = 0;
    for (const wm::Place* p : forests) {
        if (seeded >= kSeedHints) break;
        state_.memory.HintResource(gathers.c_str(), p->name.c_str(),
                                   p->position.x,
                                   p->position.y, p->position.z, nowMs);
        LogLine("common knowledge: %s at %d,%d (%d tiles away)", p->name.c_str(),
                p->position.x, p->position.y,
                TileDist(p->position.x, p->position.y, client.PlayerX(),
                         client.PlayerY()));
        ++seeded;
    }
    state_.memory.NoteEvent("common_knowledge_seeded", gathers.c_str(),
                            "", client.PlayerX(), client.PlayerY(), nowMs);
    LogLine("seeded %d %s hint(s) of %zu the atlas knows -- everything else "
            "is earned", seeded, gathers.c_str(), forests.size());
}

void Runner::LearnFromObservation(Client& client, const Observation& obs) {
    // Keep what the open box said. Observe() is const by design -- it is the
    // ephemeral half of the truth split -- so the remembering happens here,
    // which is the function whose whole job is "what did I learn".
    if (obs.bankOpen) {
        state_.bank = obs.bank;
        state_.bankSeenMs = obs.nowMs;
    }
    // NOTHING is written here. Standing where trees are visible is not
    // knowledge worth keeping: doing so gave the character 64 imaginary
    // "stands" after one session, which it then preferred over asking the
    // atlas, and it spent four sessions working scrub 210 tiles short of the
    // real Yew woods. A stand is recorded only where a chop YIELDED (see
    // DoGatherLogs), and leads come from HintResource.
    if (obs.atBank) {
        bankTrips_ = 0;
        // Pack emptied: stop reacting to the overflow message that got us here.
        // (The `+ 1` that used to be here is now inside JournalNowMs, which
        // returns an exclusive mark for every caller rather than only this one.)
        overloadWatchMs_ = client.JournalNowMs();
        // (The place itself was recorded when the box was opened, from the
        // banker's own position -- see above.)
        if (!state_.memory.HasEvent("bank_learned")) {
            state_.memory.NoteEvent("bank_learned", "opened a bank box", "bank",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("memory_learned=PLACE kind=bank at %d,%d", obs.x, obs.y);
        }
    }

    // A healer we have actually seen. The world model records the sighting;
    // this copies it into the character's own memory so it survives logout.
    const travel::ServiceSighting* healer =
        client.Knowledge().RecentService(wm::Service::Healer, obs.nowMs,
                                         60 * 60 * 1000);
    if (healer) {
        state_.memory.NotePlace("healer", healer->title.c_str(), healer->x,
                                healer->y, healer->z, obs.nowMs);
    }

    state_.memory.ExpireDanger(obs.nowMs);
}

// ---------------------------------------------------------------------------
// Unreachable-foe memory (audit section 3.7). Lives on the RUNNER, not on the
// goal, so abandoning a fight and coming back does not forget it.
// ---------------------------------------------------------------------------

// A destination we already travelled to this session and found treeless. The
// memory failure count reorders things eventually, but only a hard skip stops
// the same no-op trip repeating within one session.
bool Runner::IsDeadTarget(i32 x, i32 y) const {
    for (const auto& d : deadTargets_) {
        if (TileDist(d.first, d.second, x, y) <= 8) return true;
    }
    return false;
}

bool Runner::IsUnreachable(u32 serial, i64 nowMs) const {
    for (const auto& e : unreachable_) {
        if (e.first == serial && nowMs < e.second) return true;
    }
    return false;
}

void Runner::MarkUnreachable(u32 serial, i64 nowMs) {
    const i64 until = nowMs + 30 * 1000;
    for (auto& e : unreachable_) {
        if (e.first == serial) { e.second = until; return; }
    }
    unreachable_.emplace_back(serial, until);
    if (unreachable_.size() > 32) unreachable_.erase(unreachable_.begin());
}

// ---------------------------------------------------------------------------
// Checkpoint
// ---------------------------------------------------------------------------

bool Runner::Checkpoint(Client& client, i64 nowMs, const char* why) {
    if (!configured_) return false;
    state_.checkpointMs = nowMs;
    state_.goal = planner_.Current();
    if (client.IsInWorld()) {
        state_.lastKnownGold = client.PlayerGold();
        state_.lastKnownStr  = client.PlayerStr();
        state_.lastKnownDex  = client.PlayerDex();
        state_.lastKnownInt  = client.PlayerInt();
        state_.lastKnownX    = client.PlayerX();
        state_.lastKnownY    = client.PlayerY();
        state_.lastKnownDead = client.IsDead();
        state_.lastKnownSkills.clear();
        for (const SkillTarget& t : state_.plan.skills) {
            const i32 base = client.PlayerSkillBase(static_cast<u16>(t.skillId));
            if (base >= 0) state_.lastKnownSkills.push_back({t.skillId, base});
        }
    }
    std::string err;
    if (!store_.Save(state_, &err)) {
        LogError("[life] checkpoint failed (%s): %s\n", why, err.c_str());
        return false;
    }
    lastCheckpointMs_ = nowMs;
    LogLine("checkpoint (%s) -> %s", why,
            store_.PathFor(state_.identity.identityId).c_str());
    return true;
}

void Runner::EndSession(const char* why) {
    if (phase_ == Phase::WindDown || phase_ == Phase::LoggingOut ||
        phase_ == Phase::Done) {
        return;
    }
    LogLine("session_end_requested reason=\"%s\"", why ? why : "");
    phase_ = Phase::WindDown;
    windDownStartedMs_ = lastTickMs_;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void Runner::Tick(Client& client, i64 nowMs) {
    if (!configured_ || finished_) return;
    lastTickMs_ = nowMs;

    switch (phase_) {
        case Phase::AwaitWorld: {
            if (!client.IsInWorld()) return;
            if (sessionStartMs_ == 0) {
                sessionStartMs_ = nowMs;
                sessionStartJournalMs_ = client.JournalNowMs();
                // The server has to tell us the skills before anything can be
                // reconciled against them.
                client.ActionRequestSkills();
                LogLine("in world; asked the server for the skill list");
                nextActionMs_ = nowMs + 2500;
                return;
            }
            if (nowMs < nextActionMs_) return;
            phase_ = Phase::Reconcile;
            return;
        }

        case Phase::Reconcile: {
            const Observation obs = Observe(client, nowMs);
            SeedCommonKnowledge(client, nowMs);
            const ReconcileReport rep = Reconcile(&state_, obs);

            LogLine("reconciliation: %s, %d field(s) differed",
                    rep.firstEverLogin ? "first ever login" : "resuming a life",
                    rep.driftFields);
            for (const ReconcileLine& l : rep.lines) {
                LogLine("  %-12s persisted=%-10s server=%-10s -> %s",
                        l.field.c_str(), l.persisted.c_str(), l.server.c_str(),
                        l.result.c_str());
            }
            if (rep.goalDropped) {
                LogLine("  restored objective DROPPED: %s", rep.goalDropReason.c_str());
            } else if (state_.goal.active) {
                LogLine("  restored objective KEPT: %s (progress %d)",
                        GoalKindName(state_.goal.kind), state_.goal.progress);
            }
            planner_.Mutable() = state_.goal;

            // Transient observations from the previous life are gone by
            // construction -- they were never persisted -- but the runtime
            // caches that DO exist in this process are cleared explicitly so
            // nothing carries over between sessions in one host.
            unreachable_.clear();
            chopTargetValid_ = false;
            chopCursorPending_ = false;
            travelInFlight_ = false;

            session_ = SessionSummary{};
            session_.startedMs = nowMs;
            session_.goldStart = obs.gold;
            session_.skillTenthsStart = obs.SkillSumTenths();
            logsAtSessionStart_ = obs.logs;

            state_.identity.sessions++;

            // Survival on, and left on. M3.9.1 proved this path live: a
            // character disengaged at ~32%, bandaged, and survived.
            client.SetSurvivalEnabled(true);
            client.EnsurePeaceMode();

            Checkpoint(client, nowMs, "login reconciled");
            phase_ = Phase::Live;
            return;
        }

        case Phase::Live: {
            if (!client.IsInWorld()) return;
            const Observation obs = Observe(client, nowMs);
            LearnFromObservation(client, obs);
            MaintainBuildLocks(client, obs);

            // --- session limits -------------------------------------------
            const i64 elapsed = nowMs - sessionStartMs_;
            if (cfg_.sessionLimitMs > 0 && elapsed >= cfg_.sessionLimitMs) {
                // Deferral rules from the audit (section 3.13): never end a
                // session on top of a corpse run, and never while dead.
                if (!obs.dead && planner_.Current().kind != GoalKind::RecoverCorpse) {
                    EndSession("session time limit reached");
                    return;
                }
            }
            if (cfg_.goalLimit > 0 && session_.goalsCompleted >= cfg_.goalLimit &&
                !obs.dead) {
                EndSession("goal limit reached");
                return;
            }

            // --- decide ----------------------------------------------------
            const std::vector<Need> needs =
                AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
            std::string why;
            const GoalKind previous = planner_.Current().kind;
            const bool wasActive = planner_.Current().active;
            if (planner_.Select(needs, obs, state_.memory, nowMs, &why)) {
                session_.goalsAttempted++;
                {
                    const int gi = static_cast<int>(planner_.Current().kind);
                    if (gi >= 0 && gi < static_cast<int>(GoalKind::Count))
                        session_.goalPicks[gi]++;
                }
                if (wasActive) {
                    LogLine("goal_changed=%s from=%s reason=\"%s\"",
                            GoalKindName(planner_.Current().kind),
                            GoalKindName(previous), why.c_str());
                } else {
                    LogLine("goal=%s reason=\"%s\"",
                            GoalKindName(planner_.Current().kind), why.c_str());
                }
                LogGoalChange(obs, why);
                // A new goal starts from a clean transient slate -- but only
                // if it is genuinely a NEW goal.
                //
                // Re-picking the SAME kind must not wipe the journey already
                // under way. Corran walked from Vesper to the Minoc tinker;
                // partway there TRAIN_AT_NPC hit its 300-second limit and was
                // re-picked as TRAIN_AT_NPC, which cleared travelInFlight_. He
                // then ARRIVED beside the tinker with the flag false, took the
                // "start another trip" branch instead of the "arrived, look
                // around" one, and set off for Britain 856 tiles away 61
                // milliseconds after getting there. He never scanned Minoc at
                // all -- one scan in the whole session, back in Vesper.
                const bool sameErrand = wasActive && previous == planner_.Current().kind;
                if (!sameErrand) {
                    chopTargetValid_ = false;
                    chopCursorPending_ = false;
                    travelInFlight_ = false;
                    travelAttempts_ = 0;
                }
                // Per-errand counters belong to the errand. vendorChases_
                // bounds how long a wandering shopkeeper may be followed, and
                // a fresh goal deserves a fresh allowance -- otherwise one
                // restless vendor early in a session silences every purchase
                // made after it.
                vendorChases_ = 0;
                logsAtGoalStart_ = obs.logs;
            }

            // --- act -------------------------------------------------------
            // Point the trainer machinery at whatever the plan wants next.
            // Doing it here, from data, is what keeps `if (miner) ...` out of
            // the goal bodies.
            if (obs.wantTrainSkill >= 0) {
                if (const TrainerFor* tf = TrainerForSkill(obs.wantTrainSkill)) {
                    if (trainerTrade_ != tf->trade) {
                        trainerTrade_ = tf->trade;
                        trainerService_ = tf->service;
                        trainTrips_ = 0;
                        trainAsked_ = false;
                    }
                }
            }

            RunGoal(client, obs);

            // --- checkpoint ------------------------------------------------
            if (cfg_.checkpointIntervalMs > 0 &&
                nowMs - lastCheckpointMs_ >= cfg_.checkpointIntervalMs) {
                Checkpoint(client, nowMs, "periodic");
            }
            return;
        }

        case Phase::WindDown: {
            if (!client.IsInWorld()) { phase_ = Phase::Done; finished_ = true; return; }
            // Logging out somewhere hostile is how this project lost three
            // characters -- Source-X does not drop a combat-flagged connection
            // immediately, and one died in the gap AFTER logout_ack. So the
            // wind-down walks to a known bank first and only then logs out.
            client.EnsurePeaceMode();

            // The deadline is checked BEFORE the travel guard, and it ABORTS
            // the trip. Session B put this the other way round and a trip that
            // never arrived held the wind-down open for fourteen minutes --
            // the timeout was unreachable while travel was busy, which is not
            // a bound at all.
            const bool outOfTime = nowMs - windDownStartedMs_ > 2 * 60 * 1000;
            if (outOfTime && client.TravelBusy()) {
                LogLine("wind-down: the trip has run past its deadline; abandoning "
                        "it and logging out where I stand");
                client.TravelAbort("wind-down deadline");
                travelInFlight_ = false;
                return;
            }
            if (client.TravelBusy()) return;

            const KnownPlace* bank = state_.memory.NearestPlace(
                "bank", client.PlayerX(), client.PlayerY());
            const bool safeHere = client.BankContainer() != 0 ||
                                  windDownArrived_ ||
                                  (bank && TileDist(bank->x, bank->y, client.PlayerX(),
                                                    client.PlayerY()) <= 6);

            if (travelInFlight_) {
                travelInFlight_ = false;
                if (client.TravelSucceeded()) {
                    // Arriving IS the safe state. Requiring an open bank box
                    // here made the first live run re-plan the same
                    // already-arrived trip every tick for four minutes.
                    windDownArrived_ = true;
                    state_.memory.NotePlace("safe", "logout point", client.PlayerX(),
                                            client.PlayerY(), client.PlayerZ(), nowMs);
                    LogLine("wind-down: arrived somewhere safe at %d,%d",
                            client.PlayerX(), client.PlayerY());
                } else {
                    LogLine("wind-down: the trip did not arrive (%s)",
                            client.TravelFailureText());
                }
                return;
            }

            // Bounded, and it has to be: the alternative to "log out here" is
            // never logging out, and a character that never ends its session
            // never proves anything about resuming one.
            if (!safeHere && !outOfTime && windDownTrips_ < 2) {
                windDownTrips_++;
                if (bank) {
                    LogLine("wind-down: travelling to a known bank at %d,%d before "
                            "logout (attempt %d)", bank->x, bank->y, windDownTrips_);
                    travelInFlight_ =
                        client.TravelToPoint(bank->x, bank->y, 3, "logout_safe");
                } else {
                    LogLine("wind-down: no bank learned yet; asking the world for one "
                            "(attempt %d)", windDownTrips_);
                    travelInFlight_ = client.TravelToService(wm::Service::Banker, nullptr);
                }
                if (!travelInFlight_) {
                    LogLine("wind-down: could not start the trip (%s); logging out here",
                            client.TravelFailureText());
                }
                return;
            }
            if (!safeHere) {
                LogLine("wind-down: giving up on reaching a safe spot after %d "
                        "attempt(s); logging out at %d,%d and recording it",
                        windDownTrips_, client.PlayerX(), client.PlayerY());
                state_.memory.NoteEvent("logout_unsafe",
                                        "could not reach a known-safe spot", "",
                                        client.PlayerX(), client.PlayerY(), nowMs);
            }

            session_.endedMs = nowMs;
            session_.goldEnd = client.PlayerGold();
            {
                i32 sum = 0;
                for (const SkillTarget& t : state_.plan.skills) {
                    const i32 b = client.PlayerSkillBase(static_cast<u16>(t.skillId));
                    if (b >= 0) sum += b;
                }
                session_.skillTenthsEnd = sum;
            }
            session_.logsGathered =
                logsAtSessionStart_ >= 0
                    ? std::max(0, static_cast<i32>(client.BackpackItemCount(kLog)) -
                                      logsAtSessionStart_)
                    : 0;
            session_.placesLearned = static_cast<i32>(state_.memory.Places().size());
            session_.suppliersLearned = static_cast<i32>(state_.memory.Suppliers().size());
            session_.cleanLogout = true;

            state_.identity.totalPlayTimeMs += (nowMs - sessionStartMs_);
            state_.sessions.push_back(session_);
            if (state_.sessions.size() > kMaxSessions) {
                state_.sessions.erase(state_.sessions.begin());
            }

            LogLine("session_summary duration=%llds goals=%d/%d gold=%d->%d "
                    "skills=%.1f->%.1f logs=+%d kills=%d deaths=%d places=%d "
                    "suppliers=%d",
                    static_cast<long long>((nowMs - sessionStartMs_) / 1000),
                    session_.goalsCompleted, session_.goalsAttempted,
                    session_.goldStart, session_.goldEnd,
                    session_.skillTenthsStart / 10.0, session_.skillTenthsEnd / 10.0,
                    session_.logsGathered, session_.kills, session_.deaths,
                    session_.placesLearned, session_.suppliersLearned);

            // HOW THE DAY WAS SPENT, as one greppable line.
            //
            // R1's exit proof is "at least four goal families, none above half
            // the picks", and that has to be checkable without reading fifty
            // thousand lines by eye. Printing the shape of the day is also the
            // only way the monotony ever became visible: p0gate10 looked like
            // a healthy session until its goals were counted and turned out to
            // be CRAFT / BUY_SUPPLIES / EARN_GOLD in a ring and nothing else.
            {
                // Counted by FAMILY, not by goal kind. A crafter alternating
                // BUY_SUPPLIES / CRAFT / EARN_GOLD scores three "kinds" and
                // is still doing one thing all day; the bar has to measure
                // what R1 actually asks for.
                i32 total = 0, top = 0;
                i32 famCount[static_cast<int>(GoalFamily::Count)] = {};
                for (int i = 0; i < static_cast<int>(GoalKind::Count); ++i) {
                    const i32 n = session_.goalPicks[i];
                    if (n <= 0) continue;
                    total += n;
                    famCount[static_cast<int>(FamilyOf(static_cast<GoalKind>(i)))] += n;
                }
                i32 families = 0;
                for (int f = 0; f < static_cast<int>(GoalFamily::Count); ++f) {
                    if (famCount[f] <= 0) continue;
                    ++families;
                    if (famCount[f] > top) top = famCount[f];
                }
                std::string hist;
                for (int f = 0; f < static_cast<int>(GoalFamily::Count); ++f) {
                    if (famCount[f] <= 0) continue;
                    if (!hist.empty()) hist += " ";
                    char fc[64];
                    std::snprintf(fc, sizeof(fc), "%s=%d(%.0f%%)",
                                  GoalFamilyName(static_cast<GoalFamily>(f)),
                                  famCount[f],
                                  total ? (100.0 * famCount[f] / total) : 0.0);
                    hist += fc;
                }
                hist += " |";
                for (int i = 0; i < static_cast<int>(GoalKind::Count); ++i) {
                    const i32 n = session_.goalPicks[i];
                    if (n <= 0) continue;
                    hist += " ";
                    char cell[64];
                    std::snprintf(cell, sizeof(cell), "%s=%d(%.0f%%)",
                                  GoalKindName(static_cast<GoalKind>(i)), n,
                                  total ? (100.0 * n / total) : 0.0);
                    hist += cell;
                }
                const double topFrac = total ? (static_cast<double>(top) / total) : 1.0;
                LogLine("session_goals families=%d picks=%d top=%.0f%% varied=%d | %s",
                        families, total, topFrac * 100.0,
                        (families >= 4 && topFrac <= 0.50) ? 1 : 0,
                        hist.empty() ? "(none)" : hist.c_str());
            }

            Checkpoint(client, nowMs, "clean logout");
            LogLine("logging out");
            client.ActionLogout();
            phase_ = Phase::LoggingOut;
            return;
        }

        case Phase::LoggingOut:
            if (!client.IsInWorld()) { phase_ = Phase::Done; finished_ = true; }
            return;

        case Phase::Done:
            finished_ = true;
            return;
    }
}

namespace {
std::string Fmt2(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}
}  // namespace

void Runner::LogGoalChange(const Observation& obs, const std::string& why) {
    // Every need considered, not just the winner and the blocked ones. "Why
    // did it not do X" is only answerable if X appears somewhere.
    {
        const std::vector<Need> all =
            AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
        std::string line;
        for (const Need& n : all) {
            line += Fmt2("%s(%s%s %.2f) ", NeedKindName(n.kind), n.what.c_str(),
                         n.blocked ? " BLOCKED" : "", n.urgency);
        }
        std::string held;
        for (const std::string& t : obs.toolsHeld) { held += t; held += ","; }
        // SAY WHAT THE CHARACTER WANTS TO BUY, even when the answer is
        // "nothing". NeedSkillTraining is absent from this line whenever
        // NextSkillToBuy returns -1, and an ABSENT need is indistinguishable
        // from a need that was never asked about: run_m5/pair2 and p0gate1
        // both show a scribe with 317 gold and Meditation at 21.9 -- squarely
        // inside the 30.0 trainer ceiling -- producing no training need at
        // all, and the log gave no way to tell whether the planner had
        // decided against it or never considered it. "Why didn't it do X" has
        // to be answerable from the log.
        LogLine("needs considered: %s | tools defined=%zu held=[%s] "
                "want_train=%s (target %.1f)",
                line.empty() ? "(none)" : line.c_str(),
                needCfg_.profession ? needCfg_.profession->tools.size() : 0,
                held.c_str(),
                obs.wantTrainSkill >= 0 ? rules::SkillName(obs.wantTrainSkill)
                                        : "nothing",
                obs.wantTrainTarget / 10.0);
    }

    const std::vector<Need> needs =
        AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
    const std::vector<ScoredGoal> scored = planner_.Score(needs, obs, state_.memory);
    for (const ScoredGoal& g : scored) {
        if (g.kind != planner_.Current().kind) continue;
        LogLine("  score=%.1f", g.score);
        for (const std::string& r : g.reasons) LogLine("  reason: %s", r.c_str());
        break;
    }
    // Blocked goals are reported at every change, so "why didn't it do X"
    // never needs a packet trace to answer.
    for (const ScoredGoal& g : scored) {
        if (g.feasible) continue;
        LogLine("  BLOCKED_NEED %s: %s", GoalKindName(g.kind), g.blockedWhy.c_str());
    }
    (void)why;
}

// ---------------------------------------------------------------------------
// Goal execution
// ---------------------------------------------------------------------------

void Runner::RunGoal(Client& client, const Observation& obs) {
    if (obs.nowMs < nextActionMs_) return;

    // Bounded failure, checked before the body runs so a wedged goal cannot
    // keep acting after it has been abandoned.
    std::string exhaustedWhy;
    if (planner_.Exhausted(obs.nowMs, &exhaustedWhy)) {
        // AND REST IT. Running the full time limit without finishing is the
        // strongest possible evidence that this errand is not working right
        // now, and it was the one failure path with no cooldown -- so the very
        // next Select() saw the same unchanged need and started the identical
        // goal again with a fresh five minutes. Brannoc spent an entire
        // session on it:
        //
        //   goal_changed=TRAIN_AT_NPC from=TRAIN_AT_NPC
        //       reason="previous goal abandoned: ran 300s without finishing"
        //
        // five times over, walking Vesper to Minoc to Magincia looking for a
        // tinker, and finishing the session with goals=0/5, gold unchanged and
        // not one tenth of skill gained.
        //
        // Note this is NOT the spin the backstop catches. That one ends
        // instantly and repeatedly; this one never ends at all until the timer
        // kills it. Opposite symptoms, same cost -- one goal owning every
        // decision a character makes.
        const GoalKind spent = planner_.Current().kind;
        LogLine("goal_failed=%s reason=\"%s\"", GoalKindName(spent),
                exhaustedWhy.c_str());
        session_.goalsFailed++;
        planner_.Finish(false, exhaustedWhy.c_str(), obs.nowMs);
        planner_.Cooldown(spent, obs.nowMs + kExhaustedCooldownMs);
        return;
    }

    bool done = false;
    switch (planner_.Current().kind) {
        case GoalKind::Survive:               done = DoSurvive(client, obs); break;
        case GoalKind::Heal:                  done = DoHeal(client, obs); break;
        case GoalKind::RecoverCorpse:         done = DoRecoverCorpse(client, obs); break;
        case GoalKind::GetTool:               done = DoGetTool(client, obs); break;
        case GoalKind::ReplaceEquipment:      done = DoReplaceEquipment(client, obs); break;
        case GoalKind::Bank:                  done = DoBank(client, obs); break;
        case GoalKind::GatherLogs:            done = DoGatherLogs(client, obs); break;
        case GoalKind::TrainCombat:           done = DoTrainCombat(client, obs); break;
        case GoalKind::EarnGold:              done = DoEarnGold(client, obs); break;
        case GoalKind::TravelToRequiredPlace: done = DoTravel(client, obs); break;
        case GoalKind::TrainAtNpc:            done = DoTrainAtNpc(client, obs); break;
        case GoalKind::TradeWithPlayer:       done = DoTradeWithPlayer(client, obs); break;
        case GoalKind::Fish:                  done = DoFish(client, obs); break;
        case GoalKind::BuySupplies:           done = DoBuySupplies(client, obs); break;
        case GoalKind::Craft:                 done = DoCraft(client, obs); break;
        case GoalKind::GetFood:               done = DoGetFood(client, obs); break;
        case GoalKind::PracticeSkill:         done = DoPracticeSkill(client, obs); break;
        case GoalKind::FillSpellbook:         done = DoFillSpellbook(client, obs); break;
        case GoalKind::MakeBandages:         done = DoMakeBandages(client, obs); break;
        case GoalKind::Explore:              done = DoExplore(client, obs); break;
        case GoalKind::Mine:                 done = DoMine(client, obs); break;
        case GoalKind::Smelt:                done = DoSmelt(client, obs); break;
        case GoalKind::TameAnimal:           done = DoTameAnimal(client, obs); break;
        case GoalKind::UpgradeGear:          done = DoUpgradeGear(client, obs); break;
        case GoalKind::IdleBriefly:           done = DoIdle(client, obs); break;
        case GoalKind::Count:                 break;
    }

    if (done) {
        LogLine("goal_completed=%s progress=%d",
                GoalKindName(planner_.Current().kind), planner_.Current().progress);
        session_.goalsCompleted++;
        // Count it toward satiation so the same errand does not own the whole
        // session. A life is train, earn, sell, hunt and company in turn, not
        // one loop repeated -- and goods that found no buyer simply going into
        // the bank is a fine end to an errand, not a failure to retry.
        planner_.NoteRan(planner_.Current().kind, obs.nowMs);
        planner_.Finish(true, nullptr, obs.nowMs);
        // The anti-spin backstop, said out loud. A goal cooled off for
        // repeatedly succeeding at nothing is a BUG REPORT, not routine
        // pacing, and it must not be silent -- the three that got through so
        // far were each found by noticing a goal count in the thousands.
        const GoalKind spun = planner_.TakeSpinDetected();
        if (spun != GoalKind::Count) {
            LogLine("goal_spinning=%s reason=\"completed %d times in a row "
                    "with progress 0 -- cooled off for a minute; this is a "
                    "bug in that goal, not pacing\"",
                    GoalKindName(spun), 5);
        }
        Checkpoint(client, obs.nowMs, "goal completed");
    }
}

// --- survival --------------------------------------------------------------
//
// SurvivalTick already owns potion / bandage / disengage, proven live in
// M3.9.1. This goal adds the two things a tick-level policy cannot decide:
// whether to fight back at all, and where to go when the answer is no.

bool Runner::DoSurvive(Client& client, const Observation& obs) {
    if (obs.dead) {
        // ASK ONCE AND WAIT. Resurrection is driven by the world -- a healer
        // walking over, a shrine -- so its deadline is fifteen minutes
        // (kResurrectTimeoutMs). Re-announcing the ghost every three seconds
        // superseded the outstanding request every single time:
        //
        //   resurrect invalid_state took=3056ms superseded
        //   [ACTION] resurrect start
        //
        // for as long as the character stayed dead (run_m5/r1warrior.console
        // .txt, the first death this project has ever recorded). Same
        // retry-inside-its-own-deadline fault as the bank, the vendor and the
        // trainer -- and it survived undetected precisely because nothing had
        // ever died to exercise it. "Never fired" and "broken" look identical.
        // AND GO AND FIND A HEALER, because waiting does not work.
        //
        // ActionResurrectAccept only ANSWERS an offer the server has already
        // made. A ghost standing in a field is never offered anything, so the
        // previous version waited out a fifteen-minute deadline, failed the
        // goal, was re-picked and waited again -- for the whole session.
        // Kaelen died in a graveyard and LOGGED IN STILL DEAD on the next run
        // (run_m5/r2a.console.txt: "needs considered: StayAlive(resurrection
        // 1.00)" and nothing else, forever). That is the ghost trap the
        // project already knew about from the other end: a character that dies
        // somewhere hostile silently fails every later session.
        //
        // WHAT KILLED US, recorded once. DeathRecord carries where and when
        // but not who (PersonalKnowledge.h:44-52), so the only witness is the
        // foe we were last fighting. Death is the strongest evidence a
        // creature type can give about itself, and without this the warrior
        // would walk back to the same lich next session having learned only
        // that the graveyard is dangerous -- which it already knew.
        if (!deathBlamed_) {
            deathBlamed_ = true;
            if (!currentFoeName_.empty()) {
                LogLine("dead: blaming '%s' -- it is what we were fighting",
                        currentFoeName_.c_str());
                state_.memory.NoteCreatureOutcome(currentFoeName_.c_str(),
                                                  kCreatureEvidenceDeath,
                                                  obs.nowMs);
            }
        }

        // A player walks to a healer. So does this.
        if (client.ActionBusy()) return false;
        client.ActionResurrectAccept();

        const u32 healer = client.NearestMobileWithTrade("healer");
        if (healer) {
            i32 hx = 0, hy = 0; i8 hz = 0;
            if (client.MobilePosition(healer, &hx, &hy, &hz) &&
                TileDist(obs.x, obs.y, hx, hy) > 1 && !client.TravelBusy()) {
                LogLine("dead: a healer is here -- getting close enough to be "
                        "raised");
                travelInFlight_ = client.TravelToEntity(healer, 1);
            }
            nextActionMs_ = obs.nowMs + 4000;
            return false;
        }
        if (!client.TravelBusy() && !travelInFlight_) {
            if (++ghostTrips_ > kMaxGhostTrips) {
                LogLine("dead: %d trips and no healer found; still a ghost",
                        ghostTrips_ - 1);
                ghostTrips_ = 0;
                nextActionMs_ = obs.nowMs + 30000;
                return false;
            }
            LogLine("dead: walking to a healer (trip %d)", ghostTrips_);
            travelInFlight_ =
                client.TravelToService(wm::Service::Healer, HomeOrNearest(state_.homeCity));
        }
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // Alive again: the next death is a new death, and a new verdict.
    deathBlamed_ = false;

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    if (hostiles.empty()) {
        currentFoe_ = 0;
        client.EnsurePeaceMode();
        return true;   // the danger passed
    }

    // Remember where this went badly -- ONCE PER FIGHT, not once per tick.
    // Per-tick notes are how one twenty-minute stalemate compounded a single
    // wolf into heat 499.89 and made the whole forest look lethal.
    if (obs.nowMs - lastDangerNoteMs_ > 60000) {
        lastDangerNoteMs_ = obs.nowMs;
        state_.memory.NoteDanger(obs.x, obs.y, 14, hostiles.front().name.c_str(),
                                 0.5, obs.nowMs);
    }

    double bailAt = needCfg_.fleeHpFraction;
    const i32 extra = obs.attackersOnMe - 1;
    if (extra > 0) bailAt = std::min(0.90, bailAt + 0.08 * std::min(3, extra));

    if (obs.HpFraction() < bailAt) {
        LogLine("interrupt=FLEE reason=\"HP %.0f%%; %d attacker(s); bail at %.0f%%\"",
                obs.HpFraction() * 100.0, obs.attackersOnMe, bailAt * 100.0);
        client.EnsurePeaceMode();
        state_.memory.NoteDanger(obs.x, obs.y, 18, hostiles.front().name.c_str(), 1.5,
                                 obs.nowMs);
        // AND WHAT IT WAS, not just where it happened. A place cannot un-scare
        // you, but a creature type can prove itself safe or dangerous, and
        // "learn which graveyard mobs are safe and which are dangerous" is the
        // owner's warrior loop. Fleeing at low health from THIS thing is the
        // strongest evidence short of dying to it.
        state_.memory.NoteCreatureOutcome(hostiles.front().name.c_str(),
                                          kCreatureEvidenceNearDeathFlee,
                                          obs.nowMs);
        if (!state_.memory.HasEvent("first_near_death")) {
            state_.memory.NoteEvent("first_near_death", hostiles.front().name.c_str(),
                                    "", obs.x, obs.y, obs.nowMs);
        }
        // Retreat toward somewhere known-safe rather than a random direction.
        const KnownPlace* bank = state_.memory.NearestPlace("bank", obs.x, obs.y);
        if (bank && !client.TravelBusy()) {
            client.TravelToPoint(bank->x, bank->y, 3, "flee_to_bank");
        }
        nextActionMs_ = obs.nowMs + 2000;
        planner_.NoteAttempt(obs.nowMs);
        return false;
    }

    // Fight back at whatever is actually on us. Never pick a NEW fight here:
    // this goal exists because something already started one.
    const Client::HostileHit* target = nullptr;
    for (const Client::HostileHit& h : hostiles) {
        if (IsUnreachable(h.serial, obs.nowMs)) continue;
        if (!target || TileDist(h.x, h.y, obs.x, obs.y) <
                           TileDist(target->x, target->y, obs.x, obs.y)) {
            target = &h;
        }
    }
    if (!target) {
        client.EnsurePeaceMode();
        return true;
    }

    const i32 dist = TileDist(target->x, target->y, obs.x, obs.y);

    // DID THE LAST ONE DIE?
    //
    // Nothing in this file ever recorded WINNING a fight:
    // kCreatureEvidenceCheapKill and kCreatureEvidenceCostlyKill were declared
    // and never used, so a creature's danger could only ever go UP -- fleeing
    // added 1.0, dying added 2.0, and killing something added nothing at all.
    // A character that beat a zombie ten times learned exactly as much about
    // zombies as one that had never seen one.
    //
    // The foe vanishing from the mobile list while we were fighting it is the
    // kill: Sphere removes the mobile and drops a corpse. Cheap or costly is
    // decided by the health we finished on, which is the thing that actually
    // matters when choosing the next fight.
    if (currentFoe_ != 0 && currentFoe_ != target->serial &&
        !currentFoeName_.empty()) {
        bool stillThere = false;
        for (const Client::HostileHit& h : hostiles)
            stillThere = stillThere || (h.serial == currentFoe_);
        if (!stillThere) {
            const bool cheap = obs.HpFraction() >= 0.75;
            LogLine("hunt: killed '%s' -- finished at %.0f%% health, recording "
                    "it as a %s win", currentFoeName_.c_str(),
                    obs.HpFraction() * 100.0, cheap ? "cheap" : "costly");
            state_.memory.NoteCreatureOutcome(
                currentFoeName_.c_str(),
                cheap ? kCreatureEvidenceCheapKill : kCreatureEvidenceCostlyKill,
                obs.nowMs);
            if (!state_.memory.HasEvent("first_kill")) {
                state_.memory.NoteEvent("first_kill", currentFoeName_.c_str(),
                                        "", obs.x, obs.y, obs.nowMs);
            }
            session_.kills++;
        }
    }

    // TAKE THE WEAPON OUT OF THE BAG FIRST.
    //
    // Nothing in the life layer ever wielded one. The fighting professions
    // define no tools at all (fencer, macer and archer all have empty
    // p.tools), so no goal ever asked for a weapon in hand -- and the shard
    // hands every fighter one at creation, which then sits in the backpack for
    // the character's whole life. The result reads as a combat problem and is
    // not one: "20s of fighting and Spectre is still at the same health; this
    // is a stalemate", swung with bare fists.
    if (!client.ActionBusy()) {
        bool armed = false;
        for (usize i = 0; i < sizeof(kMeleeWeaponGfx) / sizeof(u16); ++i) {
            const u16 g = kMeleeWeaponGfx[i];
            if (client.EquippedGraphicAt(kLayerHand1) == g ||
                client.EquippedGraphicAt(kLayerHand2) == g) {
                armed = true;
                break;
            }
        }
        if (!armed) {
            const u32 inPack = FindAny(client, kMeleeWeaponGfx,
                                       sizeof(kMeleeWeaponGfx) / sizeof(u16));
            if (inPack) {
                LogLine("combat: drawing a weapon before swinging -- it was in "
                        "the pack");
                client.ActionEquip(inPack, kLayerHand1);
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
        }
    }

    if (currentFoe_ != target->serial) {
        currentFoe_ = target->serial;
        // The NAME as well as the serial. A serial dies with the corpse; the
        // name is what a per-creature verdict is keyed on, and it is the only
        // thing left to blame once we are a ghost.
        currentFoeName_ = target->name;
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
        fightStartedMs_ = obs.nowMs;
        // ASK FOR ITS HEALTH, or the whole fight is judged blind.
        //
        // The stalemate test below reads target->hpCur, and NOTHING in the
        // life layer ever filled it: SendStatusRequest -- the 0x34 status
        // query that makes a server send a mobile's health -- was called only
        // from the JS scenario bindings. So hpCur stayed at its -1 default,
        // foeHp was always -1.0, `noDent` was UNCONDITIONALLY TRUE, and every
        // autonomous fight disengaged at 21 seconds as a "stalemate" however
        // well it was going:
        //   interrupt=DISENGAGE reason="21s of fighting and Zombie is still at
        //   unknown health; this is a stalemate"
        // It then marked the foe unreachable and noted it as dangerous, so the
        // character taught itself to avoid the very monsters it was beating --
        // and that verdict persisted across sessions. This is why no bot has
        // ever recorded a confirmed kill.
        client.RequestMobileStatus(target->serial);
        foeHpAtStart_ = target->hpCur >= 0 && target->hpMax > 0
                            ? static_cast<double>(target->hpCur) / target->hpMax
                            : -1.0;
        foeHpAskedMs_ = obs.nowMs;
        LogLine("engaging %s (noto %d) at %d,%d",
                target->name.empty() ? "a hostile" : target->name.c_str(),
                target->noto, target->x, target->y);
    }

    // CANNOT DENT IT. A fight neither side can win is the worst outcome
    // available: Session A spent twenty of its thirty-one minutes in one, and
    // the goal-level timeout only restarted it every five minutes because
    // something was still attacking. So the fight itself is bounded on the one
    // signal a client actually has -- the foe's health bar.
    // KEEP ASKING. One query at the start only ever yields the opening value;
    // the stalemate test needs to see the bar MOVE, so re-ask while swinging.
    if (obs.nowMs - foeHpAskedMs_ > 3000) {
        client.RequestMobileStatus(target->serial);
        foeHpAskedMs_ = obs.nowMs;
        // The first reply is also the first honest opening reading -- before
        // it, foeHpAtStart_ could only ever have been -1.
        if (foeHpAtStart_ < 0.0 && target->hpCur >= 0 && target->hpMax > 0) {
            foeHpAtStart_ =
                static_cast<double>(target->hpCur) / target->hpMax;
        }
    }

    if (obs.nowMs - fightStartedMs_ > kFightAssessMs) {
        const double foeHp = target->hpCur >= 0 && target->hpMax > 0
                                 ? static_cast<double>(target->hpCur) / target->hpMax
                                 : -1.0;
        const bool noDent = foeHp < 0.0 || foeHpAtStart_ < 0.0 ||
                            (foeHpAtStart_ - foeHp) < 0.05;
        if (noDent) {
            LogLine("interrupt=DISENGAGE reason=\"%llds of fighting and %s is "
                    "still at %s health; this is a stalemate\"",
                    static_cast<long long>((obs.nowMs - fightStartedMs_) / 1000),
                    target->name.empty() ? "it" : target->name.c_str(),
                    foeHp >= 0.0 ? "the same" : "unknown");
            MarkUnreachable(target->serial, obs.nowMs);
            state_.memory.NoteDanger(obs.x, obs.y, 16,
                                     target->name.empty() ? "a stalemate foe"
                                                          : target->name.c_str(),
                                     1.0, obs.nowMs);
            currentFoe_ = 0;
            client.EnsurePeaceMode();
            // Walk away, or the same foe is simply re-engaged next tick.
            const KnownResourceSource* stand =
                state_.memory.BestResource("logs", obs.x, obs.y, obs.nowMs);
            if (stand && !client.TravelBusy()) {
                client.TravelToPoint(stand->x, stand->y, 4, "leave_stalemate");
            }
            return true;
        }
        // It IS taking damage -- reset the window and keep fighting.
        fightStartedMs_ = obs.nowMs;
        foeHpAtStart_ = foeHp;
    }

    // BOUNDED CHASE. A wounded animal runs, and a lumberjack that follows it
    // across the countryside has stopped being a lumberjack -- the first live
    // run spent four of its five minutes chasing one fleeing mobile and never
    // returned to the trees. Progress means getting CLOSER; when there is none
    // for a while, the foe is written off and work resumes.
    if (dist <= 1) {
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
    } else if (dist < chaseBestDist_) {
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
    } else if (obs.nowMs - chaseProgressMs_ > kChaseGiveUpMs) {
        LogLine("interrupt=DISENGAGE reason=\"cannot close on %s in %llds "
                "(best %d tiles); it is not worth the chase\"",
                target->name.empty() ? "it" : target->name.c_str(),
                static_cast<long long>(kChaseGiveUpMs / 1000), chaseBestDist_);
        MarkUnreachable(target->serial, obs.nowMs);
        currentFoe_ = 0;
        client.EnsurePeaceMode();
        return true;
    }

    if (!client.WarModeOn()) client.EnterWarMode();
    client.ActionAttack(target->serial);
    if (dist > 1 && !client.GotoBusy()) client.ActionGotoMobile(target->serial, 1);
    nextActionMs_ = obs.nowMs + 1200;
    return false;
}

bool Runner::DoHeal(Client& client, const Observation& obs) {
    if (obs.HpFraction() >= 0.95) return true;
    if (obs.bandages <= 0) {
        LogLine("goal_failed=HEAL reason=\"no bandages carried\"");
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    // SurvivalTick owns the actual bandage timing (it knows the ~3s skill
    // delay and will not restart a running heal, which is the bug that made
    // uo-offline's first bandage loop heal nothing at all). Here we only make
    // sure nothing else is competing for the body.
    if (client.WarModeOn() && obs.hostilesNear == 0) client.EnsurePeaceMode();
    nextActionMs_ = obs.nowMs + 2000;
    planner_.NoteProgress();
    return false;
}

// --- corpse ----------------------------------------------------------------

bool Runner::DoRecoverCorpse(Client& client, const Observation& obs) {
    if (obs.dead) {
        // Same guard as DoSurvive: one outstanding resurrection request, not
        // one every three seconds against a fifteen-minute deadline. Both
        // goals can be the one running while the character is a ghost, so
        // both had the fault.
        if (client.ActionBusy()) return false;
        client.ActionResurrectAccept();
        nextActionMs_ = obs.nowMs + 10000;
        return false;
    }
    if (!obs.corpseKnown) return true;   // nothing to recover

    const travel::DeathRecord& death = client.Knowledge().LastDeath();
    if (TileDist(death.x, death.y, obs.x, obs.y) > 2) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            LogLine("corpse run: heading to %d,%d (attempt %d)", death.x, death.y,
                    death.recoveryAttempts + 1);
            travelInFlight_ = client.TravelToLastCorpse();
            if (!travelInFlight_) {
                LogLine("corpse run: no route (%s)", client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 5000;
            }
            return false;
        }
        travelInFlight_ = false;
        if (!client.TravelSucceeded()) {
            client.Knowledge().NoteCorpseRecoveryAttempt();
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    // Standing on it. Open, then take everything the container reports.
    if (!client.ContainerKnown(death.corpseSerial)) {
        if (client.ActionBusy()) return false;
        client.ActionOpenContainer(death.corpseSerial);
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    const usize count = client.ContainerItemCount(death.corpseSerial);
    if (count == 0) {
        LogLine("corpse recovered or empty at %d,%d", death.x, death.y);
        state_.memory.NoteEvent("corpse_recovered", "", "", death.x, death.y,
                                obs.nowMs);
        client.Knowledge().ClearDeath();
        return true;
    }
    if (client.ActionBusy()) return false;
    u32 serial = 0;
    u16 graphic = 0, amount = 0;
    if (client.ContainerItemAt(death.corpseSerial, 0, &serial, &graphic, &amount)) {
        client.TakeFromContainer(serial, amount ? amount : 1);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 900;
    }
    return false;
}

// --- tools and equipment ---------------------------------------------------

// Which trade sells a given tool, and where the world model files them.
// Read off this shard's own vendor templates rather than guessed, the same
// discipline the trainer and buyer tables follow.
struct ToolVendor {
    const char* tool;      // the profession's own name for it
    const char* trade;     // paperdoll-title substring
    wm::Service service;
};
const ToolVendor kToolVendors[] = {
    // NEITHER OF THESE WAS SOLD WHERE IT SAID. VENDOR_S_BLACKSMITH's entire
    // stock is i_tongs and i_store_ingot (tm_vend.scp) -- no hatchet, no
    // pickaxe. So a lumberjack or a miner who lost a tool walked to a smithy,
    // opened a shop that could not contain the thing, and left; Edrik logged
    // NeedTool(hatchet 0.90) unsatisfied all session. The same "goal addressed
    // to nobody" shape as the missing tongs row below.
    //
    // Who actually sells them, from tm_vend.scp:
    //   i_hatchet   VENDOR_S_WEAPONS_BLADED only
    //   i_pickaxe   VENDOR_S_WEAPONS_BLADED, VENDOR_S_TINKER
    //
    // The pickaxe goes to the tinker: 9 tinker shops are in the atlas, and a
    // miner already visits one for a shovel and tinker tools.
    //
    // The hatchet has one seller and no Service of its own -- the atlas files
    // "Papua weaponsmith" under `blacksmith`, so Blacksmith is the right place
    // to WALK to, while the trade string must say "weaponsmith" because it is
    // matched as a paperdoll-title substring and "weaponsmith" does not
    // contain "blacksmith". 40 c_weaponsmith_* stand in sphereworld.scp.
    {"hatchet",      "weaponsmith", wm::Service::Blacksmith},
    {"pickaxe",      "tinker",      wm::Service::Tinker},
    {"fishing pole", "fisherman",   wm::Service::Fisherman},
    {"mortar",       "alchemist",   wm::Service::Alchemist},
    {"spellbook",    "mage",        wm::Service::Mage},
    // FOUR TOOLS THE CATALOGUE ASKS FOR AND NOBODY SOLD.
    //
    // Exactly the shape of the missing kTrainers rows: a profession names a
    // tool, VendorForTool returns null, and the goal fails
    // REFUSE_NO_KNOWN_BUYER without ever walking to a shop. Bruin, a full
    // crafter, logged "no known supplier of a tongs" 107 times in one session
    // -- BLOCKED_NEED GET_TOOL and BLOCKED_NEED CRAFT together, so he could
    // neither equip himself nor make anything, and spent 85% of his picks
    // idling with 0 gold.
    //
    // Trades read from this shard's own vendor templates rather than guessed:
    //   i_tongs        VENDOR_S_BLACKSMITH, VENDOR_S_TINKER
    //   i_shovel       VENDOR_S_TINKER
    //   i_sewing_kit   VENDOR_S_TAILOR, VENDOR_S_TINKER
    //   i_tinker_tools VENDOR_S_TINKER
    // Where two sell it, the one whose trade the tool belongs to is named --
    // a smith's tongs from a smith -- since that is also who stands in the
    // shop the rest of that craft's errands already visit.
    {"tongs",        "blacksmith",  wm::Service::Blacksmith},
    // The wieldable half of the smith kit, and it is sold WHERE THE SMITH
    // ALREADY IS. Reading only tm_vend.scp's SELL rows said otherwise --
    // VENDOR_S_BLACKSMITH lists just i_tongs and i_store_ingot -- but a
    // vendor's stock is not only that list: c_blacksmith and c_blacksmith_f
    // in c_vendor_human.scp both carry
    //     ITEM={ i_hammer_sledge 1 i_hammer_smith 1 }
    // in their own CHARDEF. "blacksmith has it as well, it doesnt need to go
    // far away" (project owner, 2026-08-29) -- and a smelting or smithing
    // errand is standing in a smithy already, so this costs no walk at all.
    // (c_armorer and c_weaponsmith_blade carry it too, but Blacksmith travel
    // deliberately steps past armouries.)
    {"smith hammer", "blacksmith",  wm::Service::Blacksmith},
    {"shovel",       "tinker",      wm::Service::Tinker},
    {"sewing kit",   "tailor",      wm::Service::Tailor},
    {"tinker tools", "tinker",      wm::Service::Tinker},
    // And two more the cross-check caught: every name in any profession's
    // p.tools must appear here, and "saw" and "scissors" did not.
    //   i_saw       VENDOR_S_CARPENTER, VENDOR_S_TINKER
    //   i_scissors  VENDOR_S_TAILOR, VENDOR_S_TINKER, VENDOR_S_WEAVER
    {"saw",          "carpenter",   wm::Service::Carpenter},
    {"scissors",     "tailor",      wm::Service::Tailor},
    // The fisher's cooking tool. Sold by the BAKER: the stock-Sphere
    // Scripts-X tm_vend.scp carries SELL=i_rolling_pin,{1 6} in
    // VENDOR_S_BAKER_TEMPLATE, a row the TNS shop-list swap dropped and the
    // runtime file restores. A c_baker stands at Britain's bakery
    // (sphereworld.scp P=1448,1618,20), an easy walk from the dock.
    {"rolling pin",  "baker",       wm::Service::Baker},
};

const ToolVendor* VendorForTool(const std::string& tool) {
    for (const ToolVendor& t : kToolVendors) {
        if (tool == t.tool) return &t;
    }
    return nullptr;
}

bool Runner::DoGetTool(Client& client, const Observation& obs) {
    // WHICH TOOL IS MISSING, from the catalogue rather than from a hardcoded
    // hatchet. This body was written for the lumberjack and never generalised
    // -- the fourth place in this file where that was true, after the needs,
    // the sell path and the bank. A fisher standing next to a lake was being
    // sent to a blacksmith to buy an axe it had no use for.
    std::string toolName;
    std::vector<u16> toolGfx;
    if (needCfg_.profession) {
        for (const prof::ToolNeed& t : needCfg_.profession->tools) {
            bool have = false;
            for (u16 g : t.graphics) {
                if (client.FindBackpackItemByGraphic(g) ||
                    client.EquippedGraphicAt(kLayerHand1) == g ||
                    client.EquippedGraphicAt(kLayerHand2) == g) {
                    have = true;
                    break;
                }
            }
            if (have) continue;
            toolName = t.name;
            toolGfx = t.graphics;
            break;
        }
        if (toolName.empty()) return true;   // every tool this life needs
    } else {
        // A life predating the catalogue keeps the original behaviour exactly.
        if (obs.axeInPack || obs.axeEquipped) return true;
        toolName = "hatchet";
        toolGfx.assign(kHatchet, kHatchet + 2);
        toolGfx.push_back(kAxe[0]);
        toolGfx.push_back(kAxe[1]);
    }

    const ToolVendor* tv = VendorForTool(toolName);
    if (!tv) {
        LogLine("goal_failed=GET_TOOL reason=\"%s\" tool=%s",
                faucet::RefusalName(faucet::Refusal::NoKnownBuyer),
                toolName.c_str());
        // STAND DOWN. No cooldown here meant the goal failed and was
        // re-picked on the very next tick: Bruin logged 2,058 GET_TOOL goals
        // in ten minutes at sixty-millisecond intervals (run_m7/f6_Bruin).
        // GET_TOOL sits in the Emergency family, which is exempt from
        // satiation by design -- nobody should get bored of needing an axe --
        // so a cooldown is the ONLY brake it has, and it had none.
        planner_.Cooldown(GoalKind::GetTool, obs.nowMs + kNoToolCooldownMs);
        planner_.Finish(false, "no trade known to sell it", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    const KnownSupplier* known = state_.memory.BestSupplier(toolName.c_str());

    // A tool purchase is legal under the vendor policy -- a tool is not a
    // resource, and buying one shortcuts no production chain. Verify that
    // here rather than assuming it, because the policy is the thing that
    // keeps the shard's player economy alive.
    const econ::VendorRuling ruling =
        econ::CanUseNPCVendorForGraphic(toolGfx.empty() ? 0 : toolGfx[0]);
    if (!ruling.allowed) {
        LogLine("goal_failed=GET_TOOL reason=\"%s\" tool=%s class=%s",
                faucet::RefusalName(faucet::Refusal::RevolutionAuthenticityUnknown),
                toolName.c_str(), econ::VendorClassName(ruling.klass));
        state_.memory.NoteEvent("policy_refused", toolName.c_str(),
                                econ::VendorClassName(ruling.klass),
                                obs.x, obs.y, obs.nowMs);
        // Same stand-down. A policy refusal is a settled answer, not a
        // temporary one -- re-asking it sixty times a second changes nothing.
        planner_.Cooldown(GoalKind::GetTool, obs.nowMs + kNoToolCooldownMs);
        planner_.Finish(false, "the vendor policy refuses this tool", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // COIN BEFORE THE SHOP TRIP, not after arriving at it. Fetching it later
    // put two destinations in play at once: the coin errand started walking to
    // the bank, the tool goal re-issued its walk to the smithy on the next
    // tick, and the character announced "looking for a blacksmith" every two
    // and a half seconds without ever arriving anywhere.
    if (FetchCoinForPurchase(client, obs, kToolMoneyToCarry)) return false;

    if (client.TravelBusy()) return false;

    const u32 vendor = client.VendorOfferFrom();
    if (vendor == 0) {
        if (!travelInFlight_) {
            if (known) {
                LogLine("get_tool: returning to a remembered supplier '%s' at %d,%d",
                        known->name.c_str(), known->x, known->y);
                travelInFlight_ = client.TravelToPoint(known->x, known->y, 2, "supplier");
            } else {
                toolTitlesAskedMs_ = 0;
                LogLine("get_tool: no remembered supplier; looking for a %s to "
                        "sell a %s", tv->trade, toolName.c_str());
                travelInFlight_ =
                    client.TravelToService(tv->service, HomeOrNearest(state_.homeCity));
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=GET_TOOL reason=\"%s\" (%s)",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 15000;
            }
            return false;
        }
        // LEARN WHO IS STANDING HERE BEFORE DECIDING NOBODY IS.
        //
        // NOTE THE ORDER. travelInFlight_ is cleared AFTER this, not before:
        // clearing it first and then returning early to wait for titles threw
        // away the fact that the character had arrived, so the next tick saw
        // "not travelling" and set off again -- a walk of nought tiles,
        // restarted every two and a half seconds, with two blacksmiths named
        // Olin and Curtis standing in the room.
        //
        // NearestShopkeeperWithTrade matches on the PAPERDOLL TITLE and skips
        // every mobile whose title has not been fetched yet -- and titles only
        // arrive after ActionScanMobiles double-clicks them. This path never
        // called it, so the character arrived, saw a cache full of untitled
        // mobiles, and concluded the trade was absent. At The Forgery that
        // meant "arrived but no blacksmith is here" three times over with
        // c_blacksmith standing at 2474,565 and 2467,567 -- three and four
        // tiles away.
        if (!toolTitlesAskedMs_ || obs.nowMs - toolTitlesAskedMs_ > 20000) {
            client.ActionScanMobiles();
            toolTitlesAskedMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + 2500;   // let the replies land
            return false;
        }
        travelInFlight_ = false;
        // Arrived (or gave up). Ask whoever is here to show their wares.
        const u32 keeper = client.NearestShopkeeperWithTrade(tv->trade, tv->service);
        if (!keeper) {
            // BOUND THE TRIPS. This was the last travelling goal without a
            // limit, and it cost a whole session: Edrik logged "arrived but no
            // blacksmith is here" TWO HUNDRED AND FIFTY times, 51 GET_TOOL
            // picks at 100% of a full crafter's day, and never went anywhere
            // else. Documented in M4_OPEN_LOOSE_ENDS as waste-not-a-hang, on
            // the grounds the 300s limit caps it -- which was true and still
            // meant one goal owning the character.
            //
            // Three arrivals with nobody there is enough to conclude this
            // trade is not where the atlas says, cool off and let another
            // family have the day.
            if (++toolTrips_ > kMaxToolTrips) {
                LogLine("goal_failed=GET_TOOL reason=\"%d arrivals and no '%s' "
                        "was there to sell a %s\"", toolTrips_ - 1, tv->trade,
                        toolName.c_str());
                planner_.Cooldown(GoalKind::GetTool,
                                  obs.nowMs + kNoToolCooldownMs);
                planner_.Finish(false, "no shopkeeper of that trade", obs.nowMs);
                toolTrips_ = 0;
                return false;
            }
            LogLine("get_tool: arrived but no %s is here (trip %d of %d)",
                    tv->trade, toolTrips_, kMaxToolTrips);
            state_.memory.NoteEvent("vendor_not_observed", toolName.c_str(),
                                    tv->trade, obs.x, obs.y, obs.nowMs);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        toolTrips_ = 0;
        // WALK UP FIRST. Sphere routes a vendor keyword to whoever is nearest
        // in earshot, not to the name spoken -- the lesson a carpenter taught
        // when Joshua the architect answered instead.
        i32 vx = 0, vy = 0; i8 vz = 0;
        if (client.MobilePosition(keeper, &vx, &vy, &vz)) {
            const i32 d = TileDist(obs.x, obs.y, vx, vy);
            const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
            if (d > 1 || dz > 3) {
                travelInFlight_ = client.TravelToEntity(keeper, 1);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        client.ActionVendorOpen(keeper);
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // A shop window is open. A supplier exists only once we have READ its
    // stock and seen the item -- never because a place was tagged with a
    // profession (supplier.h, and three journeys that ended at a guildmaster).
    for (const Client::VendorItem& v : client.VendorOffer()) {
        bool match = false;
        for (u16 g : toolGfx) { if (v.graphic == g) { match = true; break; } }
        if (!match) continue;

        KnownSupplier s;
        s.need = toolName;
        s.name = v.name;
        s.sourceType = "npc_vendor";
        s.serial = vendor;
        s.x = obs.x; s.y = obs.y; s.z = obs.z;
        s.observedQuantity = v.amount;
        s.observedPricePerUnit = static_cast<i32>(v.price);
        s.lastVerifiedMs = obs.nowMs;
        s.policyAllows = true;
        state_.memory.NoteSupplier(s);
        // What a thing COSTS is a price observation like any other, and the
        // character has just read it off an open window with its own eyes.
        market::PriceObservation po;
        po.item = toolName;
        po.pricePerUnit = static_cast<i32>(v.price);
        po.source = market::PriceSource::NpcVendorSells;
        po.who = v.name;
        po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
        state_.prices.Note(po);
        if (!state_.memory.HasEvent("supplier_learned")) {
            state_.memory.NoteEvent("supplier_learned", v.name.c_str(), "", obs.x,
                                    obs.y, obs.nowMs);
        }
        LogLine("memory_learned=SUPPLIER need=%s name=\"%s\" price=%u qty=%u",
                toolName.c_str(), v.name.c_str(), v.price, v.amount);

        if (obs.gold < static_cast<i32>(v.price)) {
            LogLine("goal_blocked=GET_TOOL reason=\"%s\" %s costs %u, carrying %d",
                    faucet::RefusalName(faucet::Refusal::EconomicRouteBlocked),
                    toolName.c_str(), v.price, obs.gold);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 10000;
            return false;
        }
        toolGoldBefore_ = obs.gold;
        client.ActionVendorBuy(vendor, v.serial, 1);
        state_.ledger.Note(market::GoldFlow::DestroyedVendorPurchase,
                           static_cast<i32>(v.price), toolName.c_str(),
                           obs.nowMs);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    LogLine("goal_blocked=GET_TOOL reason=\"%s\" this %zu-item list has no %s",
            faucet::RefusalName(faucet::Refusal::VendorNotObserved),
            client.VendorOffer().size(), toolName.c_str());
    planner_.NoteAttempt(obs.nowMs);
    nextActionMs_ = obs.nowMs + 8000;
    return false;
}

bool Runner::DoReplaceEquipment(Client& client, const Observation& obs) {
    // The cheapest fix first: something usable is already in the pack. The axe
    // is preferred -- it is this build's weapon AND its tool, so arming it
    // solves both needs at once.
    if (!obs.weaponEquipped) {
        if (ArmAxe(client, obs)) { planner_.NoteProgress(); return false; }
        const u32 sword = FindAny(client, kKatana, 2);
        if (!AxeInHand(client) && sword) {
            if (client.ActionBusy()) return false;
            LogLine("arming: no axe carried, equipping the sword instead");
            client.ActionEquip(sword, kLayerServerChooses);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
    }
    if (obs.weaponEquipped && obs.bandages >= needCfg_.bandageLow) return true;

    if (obs.bandages < needCfg_.bandageLow) {
        const econ::VendorRuling ruling = econ::CanUseNPCVendorForGraphic(kBandage);
        if (!ruling.allowed) {
            // FINISH the goal rather than retrying every 30 seconds forever.
            // The policy verdict will not change within a session, so a retry
            // is not a retry -- it is a character standing still. The need
            // itself is now reported blocked in AssessNeeds, so this is the
            // belt to that braces.
            LogLine("goal_failed=REPLACE_EQUIPMENT reason=\"the vendor policy "
                    "grades a bandage %s, and no player supplier is known\"",
                    econ::VendorClassName(ruling.klass));
            state_.memory.NoteEvent("policy_refused", "i_bandage",
                                    econ::VendorClassName(ruling.klass),
                                    obs.x, obs.y, obs.nowMs);
            planner_.Finish(false, "no legitimate source of bandages",
                            obs.nowMs);
            return false;
        }
        if (client.TravelBusy()) return false;

        // --- THE ERRAND OWNS THE HANDSHAKE FROM HERE ---------------------
        //
        // Everything between "I want bandages" and "the server took the gold"
        // used to live inline, and every step of it was learned the hard way
        // in this one function: ask who is here, do not address the
        // guildmaster, walk into reach, wait past the action's own deadline,
        // never ask for more than the shelf holds. life::VendorErrand holds
        // that sequence once, so the next buyer inherits it instead of
        // rediscovering it. What stays HERE is what is genuinely this goal's
        // business: the vendor POLICY above, how many bandages are wanted,
        // and what to remember afterwards.
        if (!bandageErrand_.Running()) {
            life::VendorErrandSpec spec;
            spec.Sell("healer", wm::Service::Healer);
            spec.graphic = kBandage;
            spec.qty = std::min<i32>(needCfg_.bandageFull - obs.bandages, 20);
            spec.what = "clean bandages";
            spec.goldFloor = 0;   // bandages ARE the emergency reserve
            bandageErrand_.Begin(spec);
        }
        const life::VendorErrandResult r = bandageErrand_.Tick(client, obs);
        if (!r.why.empty()) LogLine("bandages: %s", r.why.c_str());
        if (r.wake == life::Wake::AfterDelay && r.delayMs > 0)
            nextActionMs_ = obs.nowMs + r.delayMs;

        if (r.status == life::ActivityStatus::Success) {
            // Learn the shop. The errand deliberately knows nothing about
            // memory -- it buys; remembering where from is a life's business.
            for (const Client::VendorItem& v : client.VendorOffer()) {
                if (v.graphic != kBandage) continue;
                KnownSupplier s;
                s.need = "bandage";
                s.name = v.name;
                s.sourceType = "npc_vendor";
                s.serial = r.keeper;
                s.x = obs.x; s.y = obs.y; s.z = obs.z;
                s.observedQuantity = v.amount;
                s.observedPricePerUnit = static_cast<i32>(v.price);
                s.lastVerifiedMs = obs.nowMs;
                s.policyAllows = true;
                state_.memory.NoteSupplier(s);
                LogLine("memory_learned=SUPPLIER need=bandage name=\"%s\"",
                        v.name.c_str());
                break;
            }
            planner_.NoteProgress();
            return false;
        }
        // EVERY TERMINAL STATE ENDS THE GOAL, not just Failed.
        //
        // The errand stops running the moment it reaches any terminal state,
        // so a caller that only recognises Failed would let a finished errand
        // be Begin()-ed again on the very next tick -- which is the spin this
        // whole layer exists to end, reintroduced at the seam. NoProgress and
        // RetryableFailure are the two that would have slipped through.
        if (life::IsTerminal(r.status)) {
            LogLine("goal_failed=REPLACE_EQUIPMENT status=%s reason=\"%s\"",
                    life::ActivityStatusName(r.status), r.why.c_str());
            // A shop that would not open is not the same as bandages being
            // unobtainable: rest briefly and let another town be tried.
            const i64 rest = (r.status == life::ActivityStatus::RetryableFailure)
                                 ? kShortRestMs : kGearCooldownMs;
            planner_.Cooldown(GoalKind::ReplaceEquipment, obs.nowMs + rest);
            planner_.Finish(false, "no bandages bought", obs.nowMs);
            return false;
        }
        planner_.NoteAttempt(obs.nowMs);
        return false;
    }
    return true;
}

// --- banking ---------------------------------------------------------------

bool Runner::DoBank(Client& client, const Observation& obs) {
    if (coinWanted_ > 0 && obs.goldOnHand >= coinWanted_) {
        LogLine("bank: %d gold is in the pack now -- the purchase can go ahead",
                obs.goldOnHand);
        coinWanted_ = 0;
        coinLiftFails_ = 0;
    }
    const u32 box = client.BankContainer();
    // ONLY the serial is needed to deposit. Requiring ContainerKnown -- that
    // the box's CONTENTS have arrived -- was wrong twice over: an EMPTY bank
    // box sends no 0x3C at all, so the flag never flipped, and the character
    // re-opened the bank every 2.5 seconds forever without ever putting
    // anything in it. You do not need to know what is in a container to put
    // something into it.
    if (box) {
        // The box is open, so whoever we asked did answer. Forgive every
        // banker we had written off: the next visit starts clean.
        bankerAsked_ = 0;
        bankerCounted_ = 0;
        bankOpenTries_ = 0;
        bankerSilent_.clear();
        if (client.ActionBusy()) return false;

        // TAKE OUT BEFORE PUTTING IN. A purchase waiting on coin is the reason
        // this trip was made at all, and depositing first would empty the pack
        // it is trying to fill.
        if (coinWanted_ > obs.goldOnHand) {
            // STAND STILL TO LIFT. The box opened while the character was
            // still walking to the banker; he then stepped through a door to
            // 2502,548, and every lift came back "cannot lift that" -- the
            // open container does not survive being walked away from.
            if (client.TravelBusy()) return false;
            static const u16 kCoin[] = {kGoldCoin};
            const u32 stack = client.FindContainerItemByGraphic(box, kCoin, 1);
            if (stack && coinLiftFails_ >= 2) {
                // The reference has gone stale. Ask for the box again rather
                // than dragging at a serial the server no longer honours.
                LogLine("bank: the box will not give up its coin -- reopening");
                client.ForgetBankContainer();
                coinLiftFails_ = 0;
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            if (stack) {
                const i32 want = (coinWanted_ - obs.goldOnHand) + 200;
                LogLine("bank: withdrawing %d gold -- %d is wanted for a "
                        "purchase and %d is carried",
                        want, coinWanted_, obs.goldOnHand);
                client.ActionMoveItem(stack, static_cast<u16>(want),
                                      client.BackpackSerial());
                ++coinLiftFails_;   // cleared below the moment coin arrives
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 3000;
                return false;
            }
            LogLine("bank: %d gold wanted but the open box shows no coin",
                    coinWanted_);
            coinWanted_ = 0;
        }

        // Deposit whatever THIS LIFE produces, not just logs.
        //
        // This used to be hardcoded to kLog, which is the same lumberjack
        // assumption that made a mage want a hatchet. A smith carrying fifty
        // iron ingots reached the bank and deposited nothing, stayed at
        // 165/162 stones, and crawled through hundreds of fatigue rejects for
        // the rest of its life. Five of twenty bots were immobilised by it.
        //
        // One item per tick: each move is a separate action the server may
        // refuse, and batching them hides which one failed.
        // DO NOT BANK WHAT THIS LIFE IS ABOUT TO SELL.
        //
        // The need model already refuses to schedule a deposit for sellable
        // output (Needs.cpp, SellableInstead), but a BANK objective restored
        // from a previous session bypasses that reasoning entirely: Bryn
        // reached the bank carrying 15 fish, deposited all 15, and EARN_GOLD
        // pulled the same 15 straight back out two seconds later. A player
        // does not put its stock in the box on the way to the shop.
        //
        // Weight is the exception the need model already makes, and it is the
        // real reason to bank: a load too heavy to carry to a buyer has to go
        // somewhere.
        // GOLD GOES IN WHATEVER THE PACK WEIGHS.
        //
        // Everything below is gated on the load being heavy, which is the
        // right rule for STOCK -- a smith does not put its ingots in the box
        // on the way to the shop. It is the wrong rule for coin: gold is
        // barely any weight at all, so the gate never opened for it, and
        // Corwyn stood at an open bank box carrying 9,842 gold and deposited
        // nothing. "corwyn didnt put money on bank" (project owner).
        //
        // Coin is a RISK problem, not a weight problem, and this shard has
        // full loot on death. What stays is the profession's own reserve plus
        // working change; the rest goes in before anything else is considered.
        {
            const u32 coin = client.FindBackpackItemByGraphic(kGoldCoin);
            const i32 keep =
                std::min(needCfg_.profession ? needCfg_.profession->goldReserve
                                             : 0,
                         kMaxGoldCarriedRt) +
                kGoldWorthCarryingRt;
            // WHAT IS CARRIED, NOT WHAT IS OWNED. obs.gold is the status-bar
            // figure and counts the bank box, so this asked to deposit 8,785
            // coins one second after withdrawing 700 -- undoing the errand
            // that made the trip.
            const i32 spare = obs.goldOnHand - keep;
            if (coin && spare > 0 && !client.ActionBusy()) {
                LogLine("bank: depositing %d gold, keeping %d for this life's "
                        "own errands", spare, keep);
                client.ActionMoveItem(coin, static_cast<u16>(spare), box);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
        }

        const bool loadDemandsIt =
            obs.WeightFraction() >= needCfg_.bankWeightFrac;
        if (needCfg_.profession && (loadDemandsIt ||
                                    needCfg_.profession->produces.empty())) {
            for (const std::string& made : needCfg_.profession->produces) {
                const std::vector<u16> gfx = econ::GraphicsForItem(made.c_str());
                u32 serial = 0;
                i32 amount = 0;
                for (u16 g : gfx) {
                    const u32 found = client.FindBackpackItemByGraphic(g);
                    if (!found) continue;
                    serial = found;
                    amount = static_cast<i32>(client.BackpackItemCount(g));
                    break;
                }
                if (!serial || amount <= 0) continue;
                // A DEPOSIT THAT NEVER LANDS MUST NOT BE RETRIED FOREVER.
                //
                // The bank serial outlives the visit: Bryn stood on the
                // Britain dock, seventy tiles from any banker, and pushed the
                // same fifteen fish at a box it could not reach once a second
                // for the rest of the session -- every attempt answered
                // "item landed in a different container", none of them
                // counted, and nothing else could run.
                if (bankDepositItem_ == made) {
                    if (++bankDepositTries_ > kMaxBankDepositTries) {
                        LogLine("bank: %d attempts to deposit %s all landed "
                                "elsewhere -- this box is not really open",
                                bankDepositTries_, made.c_str());
                        client.ForgetBankContainer();
                        bankDepositTries_ = 0;
                        bankDepositItem_.clear();
                        planner_.NoteAttempt(obs.nowMs);
                        nextActionMs_ = obs.nowMs + 3000;
                        return false;
                    }
                } else {
                    bankDepositItem_ = made;
                    bankDepositTries_ = 1;
                }
                LogLine("banking %d %s", amount, made.c_str());
                client.ActionMoveItem(serial, static_cast<u16>(amount), box);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
        }
        // AND THE INPUTS, when the load demands it. DoBank could only ever
        // deposit what a life PRODUCES, so a scribe carrying two hundred and
        // thirty blank scrolls at 97% of its carry limit reached the bank,
        // found nothing it was allowed to put down, completed with progress 0
        // and was re-picked -- five thousand one hundred and sixty-nine times
        // in twenty minutes. Stock is still weight.
        //
        // Keep a working batch and box the rest, so the next errand can
        // actually be walked to.
        if (loadDemandsIt && needCfg_.profession) {
            const i32 keep = needCfg_.craftBatch * 2;
            // WHAT THIS LIFE CONSUMES, from the RECIPES rather than from the
            // hand-written list. The scribe has no `consumes` at all -- its
            // inputs were only ever implied by what it makes -- so a list-only
            // version of this deposited nothing and the bank goal still span.
            // The recipe graph already knows, and it cannot fall out of step
            // with itself.
            std::vector<std::string> inputs = needCfg_.profession->consumes;
            for (const std::string& made : needCfg_.profession->produces) {
                const prod::Recipe* r = prod::FindRecipe(made.c_str());
                if (!r) continue;
                for (const prod::Ingredient& in : r->inputs) {
                    if (!in.item) continue;
                    bool seen = false;
                    for (const std::string& have : inputs) {
                        if (have == in.item) { seen = true; break; }
                    }
                    if (!seen) inputs.emplace_back(in.item);
                }
            }
            for (const std::string& input : inputs) {
                const std::vector<u16> gfx = econ::GraphicsForItem(input.c_str());
                u32 serial = 0;
                i32 amount = 0;
                for (u16 g : gfx) {
                    const u32 found = client.FindBackpackItemByGraphic(g);
                    if (!found) continue;
                    serial = found;
                    amount = static_cast<i32>(client.BackpackItemCount(g));
                    break;
                }
                if (!serial || amount <= keep) continue;
                const i32 put = amount - keep;
                LogLine("banking %d spare %s (keeping %d to work with)", put,
                        input.c_str(), keep);
                client.ActionMoveItem(serial, static_cast<u16>(put), box);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
        }

        const u32 logs = loadDemandsIt || !needCfg_.profession
                             ? client.FindBackpackItemByGraphic(kLog)
                             : 0;
        if (logs) {
            const u16 amount = static_cast<u16>(client.BackpackItemCount(kLog));
            LogLine("banking %u logs", amount);
            client.ActionMoveItem(logs, amount, box);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        // AND THE DEAD WEIGHT -- what this life has no name for at all.
        //
        // The need and the action have to agree or the goal cannot terminate.
        // NeedBank is scored from CARRIED WEIGHT; every branch above deposits
        // only what the profession produces, consumes or makes from. When the
        // load is none of those, the need stays at 0.72 and the goal completes
        // having done nothing, forever.
        //
        // Ysolde is the case that proves it. A scribe with STR 10 has a carry
        // limit of 75 stones, and her STARTING KIT alone is 73 of them: two
        // chainmail coifs she cannot wear usefully, two books, a candle, three
        // cast scrolls (runtime/save/spherechars.scp, serial 04001425d). Not
        // one of those is a scroll she wrote or a reagent she buys, so she was
        // full of things she could neither use nor put down.
        //
        // A player empties that into the box. So: anything in the pack that is
        // not gold, not a tool this life declares, not a consumable it stocks,
        // not what it makes and not what it makes it FROM is dead weight, and
        // it goes in -- one per tick, each named in the log so the decision is
        // auditable. Bounded by loadDemandsIt, the same guard the inputs
        // branch uses: below the weight line a character keeps its oddments.
        if (loadDemandsIt && client.BackpackContentsKnown()) {
            // GOLD IS NO LONGER KEPT WHOLESALE. It used to be listed here
            // beside logs as something never deposited, which meant a
            // character banked its goods and walked away still carrying every
            // coin it owned -- into a shard with full loot on death. The
            // surplus goes in the box below; only what the life actually needs
            // stays in the pack.
            std::vector<u16> keepGfx{kLog};
            auto keepAll = [&keepGfx](const std::vector<u16>& g) {
                keepGfx.insert(keepGfx.end(), g.begin(), g.end());
            };
            auto keepNamed = [&keepGfx](const std::string& item) {
                const std::vector<u16> g = econ::GraphicsForItem(item.c_str());
                keepGfx.insert(keepGfx.end(), g.begin(), g.end());
            };
            if (needCfg_.profession) {
                for (const prof::ToolNeed& t : needCfg_.profession->tools)
                    keepAll(t.graphics);
                for (const prof::ConsumableNeed& c : needCfg_.profession->consumables)
                    keepAll(c.graphics);
                for (const std::string& s : needCfg_.profession->consumes)
                    keepNamed(s);
                for (const std::string& made : needCfg_.profession->produces) {
                    keepNamed(made);
                    const prod::Recipe* r = prod::FindRecipe(made.c_str());
                    if (!r) continue;
                    for (const prod::Ingredient& in : r->inputs)
                        if (in.item) keepNamed(in.item);
                }
            }
            const u32 pack = client.BackpackSerial();
            const usize n = client.ContainerItemCount(pack);
            for (usize i = 0; i < n; ++i) {
                u32 serial = 0; u16 gfx = 0, amount = 0;
                if (!client.ContainerItemAt(pack, i, &serial, &gfx, &amount)) continue;
                if (!serial) continue;
                bool named = false;
                for (u16 k : keepGfx) { if (k == gfx) { named = true; break; } }
                if (named) continue;

                // GOLD IS DEPOSITED IN PART, NOT WHOLESALE.
                //
                // It used to be kept entirely, so a character banked its goods
                // and walked out carrying every coin it owned -- on a shard
                // with full loot. Dropping it from the keep list without this
                // would be the opposite mistake: banking the lot and leaving
                // the life unable to buy bread. What stays is the profession's
                // own reserve plus working change; the rest goes in.
                u16 moving = amount ? amount : 1;
                if (gfx == kGoldCoin) {
                    const i32 keep =
                        (needCfg_.profession ? needCfg_.profession->goldReserve
                                             : 0) + kGoldWorthCarryingRt;
                    const i32 spare = static_cast<i32>(moving) - keep;
                    if (spare <= 0) continue;      // all of it is needed
                    moving = static_cast<u16>(spare);
                    LogLine("banking %d gold, keeping %d for this life's own "
                            "errands -- coin in the pack is coin at risk",
                            spare, keep);
                    client.ActionMoveItem(serial, moving, box);
                    planner_.NoteProgress();
                    nextActionMs_ = obs.nowMs + 1500;
                    return false;
                }

                LogLine("banking dead weight: 0x%04X x%u -- this life has no "
                        "use for it and the pack is at %.0f%%",
                        gfx, moving, obs.WeightFraction() * 100.0);
                client.ActionMoveItem(serial, moving, box);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
        }

        // (Recorded at box-open from the banker's own position, not here:
        // where the character stands after the last deposit is not the bank.)
        if (!state_.memory.HasEvent("first_bank_deposit") && planner_.Current().progress > 0) {
            state_.memory.NoteEvent("first_bank_deposit", "logs", "bank", obs.x,
                                    obs.y, obs.nowMs);
        }

        // A VISIT THAT DEPOSITED NOTHING IS NOT A COMPLETED BANK GOAL.
        //
        // Reporting success here is what produced the churn: Finish(true)
        // clears `active`, the next Select() sees NeedBank still at 0.72 --
        // because nothing left the pack -- and starts BANK again 60 ms later.
        // pair2 did that for five straight minutes, fsyncing state.json on
        // every lap. Say what actually happened and stand down.
        if (planner_.Current().progress == 0) {
            LogLine("bank: the box is open and there is nothing in the pack "
                    "this life may put down (weight %d/%d) -- standing down "
                    "for %llds rather than re-deciding",
                    obs.weight, obs.maxWeight,
                    static_cast<long long>(kBankCooldownMs / 1000));
            planner_.Cooldown(GoalKind::Bank, obs.nowMs + kBankCooldownMs);
            planner_.Finish(false, "nothing to deposit", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        return true;
    }

    if (client.TravelBusy()) return false;

    // WAIT FOR THE ASK BEFORE ASKING AGAIN.
    //
    // open_bank's deadline is 6 s (kBankTimeoutMs) and this used to re-issue
    // every 2.5 s, so every attempt was killed by the NEXT one before it could
    // either succeed or time out. The whole of run_m5/pair3 is sixty-three
    // repetitions of
    //
    //   [action] open_bank superseded by open_bank
    //   [ACTION_RESULT] open_bank invalid_state (2520ms) superseded
    //
    // and not one resolved result in twenty minutes. An action already in
    // flight is not a reason to start another one.
    if (client.ActionBusy()) return false;

    // AN ASK THAT PRODUCED NO BOX IS A FAILED ASK, and it has to be counted
    // here: the action layer can report "superseded" or "timeout", but only
    // this goal knows that the box it wanted still is not open.
    if (bankerAsked_) {
        // COUNT PER BANKER, NOT PER GOAL. Britain's bankers wander, so the
        // nearest one changes between asks: p0gate1 asked Hyman, Hyman, then
        // Lyndon, and wrote LYNDON off "3 times" when he had been asked once.
        // A tally that blames whoever stood closest at the third failure is
        // not evidence about anybody.
        if (bankerCounted_ != bankerAsked_) {
            bankerCounted_ = bankerAsked_;
            bankOpenTries_ = 0;
        }
        if (++bankOpenTries_ >= kMaxBankOpenTries) {
            LogLine("bank: asked 0x%08X for the box %d times and got nothing "
                    "back -- finding a different banker",
                    bankerAsked_, bankOpenTries_);
            bankerSilent_.push_back(bankerAsked_);
            bankOpenTries_ = 0;
            bankerCounted_ = 0;
        }
        bankerAsked_ = 0;
    }

    u32 banker = client.NearestMobileWithTrade("banker", bankerSilent_);
    if (!banker) {
        // STAND IN THE BANK AND SAY "BANK". "you dont need find banker vendor
        // directly, go near bank and say bank" (project owner, 2026-08-29) --
        // which is exactly what a player does.
        //
        // Addressing a banker BY NAME was itself a fix, for a real problem:
        // a bare keyword is UNNAMED and Source-X answers those by picking the
        // closest NPC, and Britain's bankers wander. But requiring the name
        // means requiring the PAPERDOLL TITLE, and if that has not arrived the
        // character stands inside a bank unable to see a banker at all --
        // three trips and "no banker in reach", which is how Corwyn ended a
        // session carrying 9,842 gold.
        //
        // So: name one when we can see one, and otherwise just say the word.
        // Standing in the bank is what makes "closest NPC" the right NPC.
        // ...but only while actually STANDING IN ONE.
        //
        // The first version accepted "a bank is remembered somewhere", which
        // is not the same thing at all: Corwyn said "bank" aloud four times
        // beside a GUILDMASTER, who is not a banker and never answers.
        // Shouting the word is only sensible where the closest NPC is likely
        // to be a banker, and that is inside the bank.
        // ASK WHO IS HERE FIRST. The trade match needs a paperdoll title and
        // titles only arrive after ActionScanMobiles clicks for them, so
        // "no banker recognised" is often just "nobody has been asked yet".
        if (!bankTitlesAskedMs_ || obs.nowMs - bankTitlesAskedMs_ > 20000) {
            client.ActionScanMobiles();
            bankTitlesAskedMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }

        const KnownPlace* bankHere = state_.memory.NearestPlace("bank", obs.x, obs.y);
        const bool standingInABank =
            obs.atBank || client.BankContainer() != 0 ||
            (bankHere && TileDist(obs.x, obs.y, bankHere->x, bankHere->y) <= 6);
        if (standingInABank && ++bankShouts_ <= kMaxBankShouts) {
            LogLine("bank: no banker recognised here -- saying 'bank' aloud "
                    "(%d of %d), which is what a player does",
                    bankShouts_, kMaxBankShouts);
            client.ActionOpenBank(0, "bank");
            nextActionMs_ = obs.nowMs + kBankAskGapMs;
            return false;
        }
        if (bankShouts_ > kMaxBankShouts) {
            LogLine("goal_failed=BANK reason=\"said 'bank' %d times where the "
                    "box should be and nobody answered\"", kMaxBankShouts);
            planner_.Cooldown(GoalKind::Bank, obs.nowMs + kNoBankCooldownMs);
            planner_.Finish(false, "nobody answered", obs.nowMs);
            bankShouts_ = 0;
            return false;
        }
    }
    if (banker) {
        // CLOSE ENOUGH TO BE HEARD. "he cant open the bank he needs to be
        // closer to banker" (project owner, 2026-08-29). The remembered bank
        // place is a spot in the room, not the teller: Corwyn stood at
        // 2502,552 while Jarvinia was at 2499,549 and the other banker at
        // 2502,547 -- three and five tiles -- and every "bank" he said drew no
        // reply at all, from a banker who greets other bots cheerfully.
        i32 kx = 0, ky = 0; i8 kz = 0;
        if (client.MobilePosition(banker, &kx, &ky, &kz)) {
            const i32 d = TileDist(obs.x, obs.y, kx, ky);
            if (d > 2) {
                if (client.TravelBusy()) return false;
                LogLine("bank: the banker is %d tiles off -- stepping up to be "
                        "heard", d);
                travelInFlight_ = client.TravelToPoint(kx, ky, 2, "banker");
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        client.ActionOpenBank(banker);
        // REMEMBER WHERE THE BANKER STANDS, not where we happened to be when
        // the last item went into the box. Recording the player's position at
        // deposit time put Bryn's "bank" on the Britain dock, seventy tiles
        // from Hyman, and every later trip walked confidently to a spot with
        // no banker in it -- three round trips in one minute before the trip
        // counter gave up. A remembered place is only useful if it is the
        // thing, not a place the thing was once near.
        i32 bx = 0, by = 0; i8 bz = 0;
        if (client.MobilePosition(banker, &bx, &by, &bz)) {
            state_.memory.NotePlace("bank", "bank", bx, by, bz, obs.nowMs);
        }
        bankerAsked_ = banker;
        bankOpenedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + kBankAskGapMs;
        // ASKING IS NOT PROGRESS. NoteProgress() here reset the attempt
        // counter on every retry, so Exhausted() never fired and the planner
        // believed a goal that had done nothing for twenty minutes was
        // working. An ask is an attempt; the box opening is the progress.
        planner_.NoteAttempt(obs.nowMs);
        return false;
    }
    if (!bankerSilent_.empty()) {
        // Every banker within sight has now been asked and none opened a box.
        // Walking to another bank is the honest next move, but not on this
        // goal and not this second.
        LogLine("bank: %d banker(s) in reach and not one opened a box -- "
                "standing down for %llds",
                static_cast<int>(bankerSilent_.size()),
                static_cast<long long>(kBankCooldownMs / 1000));
        state_.memory.NoteEvent("bank_no_answer", "no banker opened a box", "",
                                obs.x, obs.y, obs.nowMs);
        bankerSilent_.clear();
        bankOpenTries_ = 0;
        bankTrips_ = 0;
        planner_.Cooldown(GoalKind::Bank, obs.nowMs + kBankCooldownMs);
        planner_.Finish(false, "no banker answered", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }
    if (!travelInFlight_) {
        // BOUNDED. A trip that "arrives" without putting a banker in reach
        // completes instantly, and without a counter this alternates
        // start/clear forever -- the same no-op travel loop that pinned
        // GATHER_LOGS, logged eight times a second.
        if (++bankTrips_ > kMaxBankTrips) {
            LogLine("goal_failed=BANK reason=\"%d trips and still no banker in "
                    "reach; the pack stays full\"", bankTrips_ - 1);
            state_.memory.NoteEvent("bank_unreachable",
                                    "could not reach a banker", "", obs.x, obs.y,
                                    obs.nowMs);
            planner_.Finish(false, "no banker reachable", obs.nowMs);
            bankTrips_ = 0;
            nextActionMs_ = obs.nowMs + 30000;
            return false;
        }
        const KnownPlace* known = state_.memory.NearestPlace("bank", obs.x, obs.y);
        // A REMEMBERED PLACE THAT KEEPS BEING WRONG IS NOT A MEMORY.
        //
        // Bryn walked to a "bank" on the Britain dock, found nobody, walked
        // back, and did it again -- three round trips a minute, and the trip
        // counter reset every time the goal was re-picked, so it never ran
        // out. Two failed arrivals at the same spot is enough: unlearn it and
        // ask the world model instead. The place that replaces it is recorded
        // from the BANKER's own position when a box actually opens.
        if (known && bankTrips_ > 2) {
            LogLine("bank: two trips to %d,%d found no banker -- forgetting "
                    "that place", known->x, known->y);
            state_.memory.ForgetPlace("bank", known->x, known->y);
            known = state_.memory.NearestPlace("bank", obs.x, obs.y);
        }
        if (known) {
            LogLine("bank: returning to a remembered bank at %d,%d (trip %d)",
                    known->x, known->y, bankTrips_);
            travelInFlight_ = client.TravelToPoint(known->x, known->y, 2, "bank");
        } else {
            LogLine("bank: no bank learned yet; asking the world model for one "
                    "(trip %d)", bankTrips_);
            travelInFlight_ = client.TravelToService(wm::Service::Banker, nullptr);
        }
        if (!travelInFlight_) {
            LogLine("goal_blocked=BANK reason=\"%s\"", client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 10000;
        }
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    travelInFlight_ = false;
    if (!client.TravelSucceeded()) {
        LogLine("bank: the trip did not arrive (%s)", client.TravelFailureText());
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    // Arrived. ASK WHO IS HERE before concluding there is no banker:
    // NearestMobileWithTrade matches on the paperdoll title, and a title only
    // arrives after a 0x98 name request. Without this the character stands
    // next to a banker and reports none in reach.
    LogLine("bank: arrived at %d,%d -- asking who is here", client.PlayerX(),
            client.PlayerY());
    client.ActionScanMobiles();
    nextActionMs_ = obs.nowMs + 2500;
    return false;
}

// --- the work --------------------------------------------------------------

bool Runner::DoGatherLogs(Client& client, const Observation& obs) {
    if (!obs.axeInPack && !obs.axeEquipped) {
        LogLine("goal_failed=GATHER_LOGS reason=\"no axe\"");
        planner_.Finish(false, "no axe", obs.nowMs);
        return false;
    }
    if (obs.WeightFraction() >= 0.95) {
        LogLine("gather: pack full at %.0f%%", obs.WeightFraction() * 100.0);
        return true;
    }

    // Arm the axe. `skill44_lumberjacking.scp` requires SRC.WEAPON, so the axe
    // has to be IN HAND, not merely carried -- and it must be the AXE, not
    // whatever the newbie kit armed. This is also the weapon the character
    // fights with, which is the whole point of the era Lumberjack template.
    if (!obs.axeEquipped) {
        // Arming is progress, not a failed attempt -- counting it against the
        // attempt budget would exhaust the goal before the first swing.
        if (ArmAxe(client, obs)) return false;
        if (!AxeInHand(client)) {
            LogLine("goal_failed=GATHER_LOGS reason=\"no axe to arm\"");
            planner_.Finish(false, "no axe to arm", obs.nowMs);
            return false;
        }
    }

    // --- am I actually where the work is? ---------------------------------
    //
    // `areaExhausted_` overrides the census: TreeCount can still see trunks
    // here while NearestTree has none left to offer, because every one of them
    // has already been worked this visit. Believing the census in that state
    // is what kept the character standing in a clearing it had finished.
    if (!obs.atWorkSite || areaExhausted_) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            // Earned knowledge first, then common knowledge, then go looking.
            // The order matters: preferring a merely-remembered spot over a
            // named forest is what kept this character in the scrub.
            const KnownResourceSource* proven =
                state_.memory.BestProvenResource("logs", obs.x, obs.y, obs.nowMs);
            if (proven && IsDeadTarget(proven->x, proven->y)) proven = nullptr;
            const KnownResourceSource* hint =
                proven ? nullptr
                       : state_.memory.BestHint("logs", obs.x, obs.y, obs.nowMs);
            if (hint && IsDeadTarget(hint->x, hint->y)) hint = nullptr;
            if (proven) {
                LogLine("gather: back to a stand that has paid out before at "
                        "%d,%d (%d successes, %d failures)",
                        proven->x, proven->y, proven->successes, proven->failures);
                lastHintX_ = proven->x;
                lastHintY_ = proven->y;
                travelInFlight_ =
                    client.TravelToPoint(proven->x, proven->y, 4, "proven_stand");
            } else if (hint) {
                LogLine("gather: nothing proven yet -- trying %s at %d,%d "
                        "(a lead, %d disappointment(s) so far)",
                        hint->label.empty() ? "a known forest" : hint->label.c_str(),
                        hint->x, hint->y, hint->failures);
                lastHintX_ = hint->x;
                lastHintY_ = hint->y;
                travelInFlight_ =
                    client.TravelToPoint(hint->x, hint->y, 6, "forest_hint");
            } else {
                LogLine("gather: no stand and no lead left; asking the world for lumber");
                travelInFlight_ = client.TravelToResource(wm::ResourceKind::Lumber);
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=GATHER_LOGS reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 10000;
            }
            return false;
        }
        travelInFlight_ = false;
        // A completed journey is a new place, so the exhaustion verdict about
        // the OLD one no longer applies.
        areaExhausted_ = false;
        // ARRIVAL IS A CLAIM ABOUT THE TILE. A journey that reports success
        // and leaves us six tiles short of the trees is a failure here, and
        // saying so is what keeps it out of the "worked fine" column.
        if (client.TreeCount(client.PlayerX(), client.PlayerY(), cfg_.searchRadius) == 0) {
            LogLine("gather: trip reported %s but there are no trees within %d tiles",
                    client.TravelSucceeded() ? "success" : "failure", cfg_.searchRadius);
            // Charge the DESTINATION WE AIMED AT, and refuse to aim there
            // again this session. Charging wherever we happen to stand let a
            // no-op trip -- one that "arrived" without moving -- pile failures
            // onto a stand with three real successes, several times a second.
            if (lastHintX_ != 0 || lastHintY_ != 0) {
                state_.memory.NoteResource("logs", lastHintX_, lastHintY_,
                                           client.PlayerZ(), false, obs.nowMs);
                deadTargets_.emplace_back(lastHintX_, lastHintY_);
                if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
                lastHintX_ = lastHintY_ = 0;
            }
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 3000;
        }
        return false;
    }

    // --- pick a tree and stand next to it ---------------------------------
    if (!chopTargetValid_) {
        Client::TreeHit tree;
        // Ask for the nearest tree we have NOT already worked. The exclusion
        // is the whole point: without it every widening radius hands back the
        // same tree, the area reads as exhausted after ONE tree, and the
        // character loops on it -- 231 swings at a single trunk in one live
        // session.
        const bool found =
            client.NearestTree(obs.x, obs.y, cfg_.searchRadius, &tree, &visitedTrees_);
        if (!found) {
            LogLine("gather: every tree within %d tiles is worked out -> "
                    "this area is done for now", cfg_.searchRadius);
            // Charge the FAILURE TO THE LEAD that sent us here, so the next
            // trip picks a different named forest instead of walking back to
            // the same dry one. Without this the hint list never reorders and
            // the character loops on its nearest disappointment.
            if (lastHintX_ != 0 || lastHintY_ != 0) {
                state_.memory.NoteResource("logs", lastHintX_, lastHintY_, obs.z,
                                           false, obs.nowMs);
                lastHintX_ = lastHintY_ = 0;
            }
            visitedTrees_.clear();
            // DO NOT complete the goal here. "This area is done" is a reason
            // to GO SOMEWHERE ELSE, not a reason to hand control back -- and
            // handing it back put the character in a 2.5-second loop: the
            // planner re-picked GATHER_LOGS (still the top need), the
            // character was still standing in the same worked-out clearing,
            // and it said the same sentence again. Forty completions with
            // progress=0 in under two minutes, live.
            //
            // This is also the M4 Session L churn -- 22 goals attempted, 1
            // completed -- finally visible.
            deadTargets_.emplace_back(obs.x, obs.y);
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            areaExhausted_ = true;
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
        chopX_ = tree.x; chopY_ = tree.y; chopZ_ = tree.z;
        chopGraphic_ = tree.graphic;
        chopTargetValid_ = true;
        chopCursorPending_ = false;
        swingsOnTree_ = 0;
    }

    // Adjacency is read from the LIVE position, not from the tick's snapshot:
    // the walk finishes between ticks, and swinging from the stale reading is
    // what earned "You can't reach this." eight times per tree in the first
    // live run. A tree is surrounded by eight cells and some of them are other
    // trees, so try them nearest-first rather than always stepping east.
    // RANGE=2 in skill44_lumberjacking.scp -- the shard's own number. Standing
    // adjacent is not required, and demanding it wastes walks.
    if (TileDist(chopX_, chopY_, client.PlayerX(), client.PlayerY()) > 2) {
        if (client.GotoBusy()) return false;
        if (approachCell_ >= 8) {
            LogLine("cannot stand next to the tree at %d,%d -> moving on",
                    chopX_, chopY_);
            visitedTrees_.emplace_back(chopX_, chopY_);
            chopTargetValid_ = false;
            approachCell_ = 0;
            planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        static const int kdx[8] = {1, -1, 0,  0, 1,  1, -1, -1};
        static const int kdy[8] = {0,  0, 1, -1, 1, -1,  1, -1};
        // Order the eight cells by how close they are to where we stand now,
        // then walk to the next untried one.
        int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        const i32 px = client.PlayerX(), py = client.PlayerY();
        std::stable_sort(order, order + 8, [&](int a, int b) {
            return TileDist(chopX_ + kdx[a], chopY_ + kdy[a], px, py) <
                   TileDist(chopX_ + kdx[b], chopY_ + kdy[b], px, py);
        });
        const int pick = order[approachCell_++];
        client.ActionGoto(chopX_ + kdx[pick], chopY_ + kdy[pick]);
        nextActionMs_ = obs.nowMs + 1200;
        return false;
    }
    approachCell_ = 0;

    // --- swing --------------------------------------------------------------
    if (client.ActionBusy()) return false;

    if (chopCursorPending_) {
        if (client.TargetActive()) {
            client.ActionTargetStatic(chopX_, chopY_, chopZ_, chopGraphic_);
            chopCursorPending_ = false;
            lastChopMs_ = obs.nowMs;
            chopSwungJournalMs_ = client.JournalNowMs();
            // LET THE CHOP FINISH. skill44_lumberjacking.scp is DELAY=1.6 and
            // Source-X rolls rand(5)+2 strokes per attempt (CCharSkill.cpp),
            // so one chop runs 3.2 to 9.6 seconds. Re-using the axe before it
            // resolves fires @Abort -- "You decide not to chop wood for now."
            // A live session threw away 191 of 240 swings that way, an 80%
            // loss that looked like bad luck rather than a bug.
            //
            // The wait is cut short the moment the pack gains a log, so a
            // fast success is not paid for twice (see the yield check below)
            // -- and equally by any of Sphere's DEFINITIVE answers, checked
            // on a short poll below.
            nextActionMs_ = obs.nowMs + kChopPollMs;
            return false;
        }
        if (obs.nowMs - lastChopMs_ > 6000) {
            chopCursorPending_ = false;
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    // Did the last swing produce anything? The honest signal a client has is
    // the pack: logs went up, or they did not.
    //
    // BOUNDED PER TREE. The first live run swung at one tree for two minutes
    // straight because the "nothing came out" branch was gated on a timer that
    // the next swing kept resetting. Count swings instead of watching a clock:
    // a counter cannot be reset by the thing it is counting.
    // --- has the swing RESOLVED? -------------------------------------------
    //
    // A flat ten-second sleep after every swing was most of the loop's wall
    // clock, and most of it was spent waiting for an answer the server had
    // already given. "There is nothing here to chop" arrives on the FIRST
    // stroke, and with 60% of trees barren by the shard's own resource table
    // (regionresources.scp: 60.0 mr_nothing against 40.0 mr_tree) that is the
    // common case, not the exception. A live run managed seven swings in three
    // minutes and gathered nothing.
    //
    // So the ten seconds is now a CEILING, not a delay: poll briefly and stop
    // the moment Sphere says something conclusive.
    if (!chopCursorPending_ && lastChopMs_ != 0 &&
        obs.nowMs - lastChopMs_ < kChopResolveMs && obs.logs <= logsSeen_) {
        static const char* kResolved[] = {
            "there is nothing here to chop",       // barren: move on NOW
            "but fail to produce any useable wood",// attempt resolved, no yield
            "you decide not to chop wood for now", // @Abort
            "that is too far away",
            "you can't reach this",
        };
        bool done = false;
        for (const char* line : kResolved) {
            if (client.JournalSaidSince(line, chopSwungJournalMs_)) {
                done = true;
                break;
            }
        }
        if (!done) {
            nextActionMs_ = obs.nowMs + kChopPollMs;
            return false;
        }
        // Resolved with no wood. Fall through: the swing counter below decides
        // whether to try this tree once more or move to the next one.
        lastChopMs_ = 0;
    }

    if (obs.logs > logsSeen_) {
        // The chop resolved early and paid out -- stop waiting out the stroke
        // window and swing again.
        nextActionMs_ = obs.nowMs;
        logsSeen_ = obs.logs;
        swingsOnTree_ = 0;
        planner_.NoteProgress();
        state_.memory.NoteResource("logs", chopX_, chopY_, chopZ_, true, obs.nowMs);
        if (!state_.memory.HasEvent("first_logs")) {
            state_.memory.NoteEvent("first_logs", "first logs gathered", "forest",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("first logs gathered at %d,%d (pack now holds %d)", obs.x, obs.y,
                    obs.logs);
        }
    } else if (swingsOnTree_ >= kMaxSwingsPerTree) {
        // A barren tree is NOT a failure. `regionresources.scp` gives
        // `r_default_tree` 60.0 mr_nothing against 40.0 mr_tree, so three trees
        // in five hold no wood at all and Source-X says so on the first swing
        // ("There is nothing here to chop", CCharSkill.cpp:1682). Counting
        // that against the goal's attempt budget ended GATHER_LOGS every few
        // trees in the live run -- the character was working correctly and
        // being told it had failed.
        LogLine("tree at %d,%d holds no wood -> next tree (%d tried this stand)",
                chopX_, chopY_, static_cast<int>(visitedTrees_.size()) + 1);
        // DO NOT MarkStump here. It rewrites the tree's graphic in OUR OWN
        // statics overlay, so the next TreeCount() sees nothing -- the bot
        // blinds itself, concludes it is not at a work site, "travels" zero
        // tiles to where it already stands, and loops, charging a failure onto
        // a genuinely productive stand every time round. `visitedTrees_`
        // already stops us re-picking this tree, and it does it without
        // lying to the census.
        state_.memory.NoteResource("logs", chopX_, chopY_, chopZ_, false, obs.nowMs);
        visitedTrees_.emplace_back(chopX_, chopY_);
        if (visitedTrees_.size() > 64) visitedTrees_.erase(visitedTrees_.begin());
        chopTargetValid_ = false;
        swingsOnTree_ = 0;
        return false;
    }

    const u32 axe = AxeSerialInHand(client);
    if (!axe) {
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    client.ActionUseObject(axe);
    chopCursorPending_ = true;
    swingsOnTree_++;
    lastChopMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 800;
    return false;
}


bool Runner::DoTrainCombat(Client& client, const Observation& obs) {
    // Something is already here: finish that fight. This is how every
    // character trains, hunter or not.
    // A FIGHT IN PROGRESS IS DEFENCE. A fight to pick is not.
    //
    // This used to hand EVERY sighting to DoSurvive, which answers "what is
    // most likely to kill me" and attacks the nearest reachable thing. That is
    // right when something is already swinging and wrong when choosing whom to
    // start on -- and it meant the whole M6 layer (Classify, ChooseTarget,
    // ChoosePrey and 54 unit tests) was called by nothing at all.
    if (obs.attackersOnMe > 0) return DoSurvive(client, obs);

    if (obs.hostilesNear > 0 && !client.ActionBusy()) {
        std::vector<Client::HostileHit> seen;
        client.ScanHostiles(12, seen);
        if (!seen.empty()) {
            std::vector<combat::Candidate> cands;
            cands.reserve(seen.size());
            for (const Client::HostileHit& h : seen) {
                combat::Candidate c;
                c.serial = h.serial;
                c.name   = h.name;
                // The notoriety BYTE and the Noto enum agree value for value --
                // 1 blue, 2 green, 3/4 gray, 5 orange, 6 red, 7 yellow
                // (Client.h:644, combat.h:28-37) -- so this is a mapping,
                // not a guess.
                c.noto   = static_cast<combat::Noto>(h.noto);
                c.dist   = TileDist(h.x, h.y, obs.x, obs.y);
                c.hpCur  = h.hpCur;
                c.hpMax  = h.hpMax;
                c.warMode = h.warMode;
                cands.push_back(std::move(c));
            }
            combat::Stance me;
            // REGION_FLAG_GUARDED, straight from the atlas -- there is no
            // Observation field for it and inventing one would just cache a
            // fact the world model already answers.
            {
                const wm::Region* here = client.CurrentRegion();
                me.inGuardedRegion = here && here->flags.guarded;
            }
            me.attackersOnMe   = obs.attackersOnMe;

            // What this life has LEARNED about these creatures, so a lich it
            // died to last session is not "weak and alone" today.
            const Memory& mem = state_.memory;
            const i64 now = obs.nowMs;
            // Learned first, seeded second. Experience beats a stat block.
            LoadSeededCreatureDanger(client.DataDir());
            const combat::CreatureDangerLookup danger =
                [&mem, now](const std::string& n) {
                    const double learned = mem.CreatureDanger(n.c_str(), now);
                    if (learned > 0.0) return learned;
                    const double seeded = SeededDangerFor(n);
                    return seeded >= 0.0 ? seeded : 0.0;
                };

            const combat::EngagePolicy policy;
            const int prey = combat::ChoosePrey(cands, me, combat::RevolutionCrimeRules(),
                                                policy, obs.HpFraction(), danger);
            if (prey >= 0) {
                const combat::Candidate& c = cands[static_cast<usize>(prey)];
                const combat::Classification v =
                    combat::Classify(c, me, combat::RevolutionCrimeRules(), policy,
                                     obs.HpFraction());
                // PRINT THE VERDICT. R2's whole point is that a kill is
                // exercised THROUGH the legality layer, not around it, and a
                // verdict nobody logs is a verdict nobody can check.
                LogLine("hunt: picked '%s' at %d tiles -- verdict=%s threat=%.2f "
                        "learned_danger=%.2f (%s)",
                        c.name.c_str(), c.dist, combat::LegalityName(v.legality),
                        v.threat, mem.CreatureDanger(c.name.c_str(), obs.nowMs),
                        v.reason.c_str());
                client.ActionAttack(c.serial);
                currentFoe_ = c.serial;
                currentFoeName_ = c.name;
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            LogLine("hunt: %zu hostile(s) in sight and none worth starting on",
                    seen.size());
        }
        return DoSurvive(client, obs);
    }
    if (obs.hostilesNear > 0) return DoSurvive(client, obs);

    // NOTHING HERE. Until now that was the end of it -- "return true" -- and
    // it is why M6 has never once been exercised live: the layer that decides
    // what may legally be attacked was never given anything to decide about.
    // A fighter with no fight in reach should go and find one.
    if (!needCfg_.profession || !WantsToHunt(*needCfg_.profession)) return true;

    // Not while hurt, and not while loaded: the goal scorer already docks
    // both, but arriving at a graveyard at half health is a death rather than
    // a lesson, and that is a decision this goal should make for itself.
    if (obs.hp * 100 < obs.hpMax * 80) {
        LogLine("hunt: %d/%d health -- not going looking for a fight",
                obs.hp, obs.hpMax);
        return true;
    }
    if (obs.WeightFraction() >= 0.7) {
        LogLine("hunt: carrying too much to fight (%.0f%%)",
                obs.WeightFraction() * 100.0);
        return true;
    }

    if (client.TravelBusy()) return false;
    if (!travelInFlight_) {
        if (++huntTrips_ > kMaxHuntTrips) {
            LogLine("goal_failed=TRAIN_COMBAT reason=\"no hunting ground "
                    "reachable after %d trips\"", huntTrips_);
            planner_.Finish(false, "no hunting ground reachable", obs.nowMs);
            huntTrips_ = 0;
            nextActionMs_ = obs.nowMs + 60000;
            return false;
        }
        LogLine("hunt: no fight in reach -- going to the nearest graveyard "
                "(trip %d)", huntTrips_);
        travelInFlight_ = client.TravelToPlaceCategory(wm::PlaceCategory::Graveyard);
        if (!travelInFlight_) {
            LogLine("goal_blocked=TRAIN_COMBAT reason=\"%s\"",
                    client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
        }
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    travelInFlight_ = false;
    huntTrips_ = 0;
    // Arrived. Ask what is here; the targeting layer judges legality, and a
    // graveyard's dead are the one thing on this shard that is always lawful
    // to swing at.
    LogLine("hunt: arrived at %d,%d -- looking for something to fight",
            client.PlayerX(), client.PlayerY());
    client.ActionScanMobiles();
    if (!state_.memory.HasEvent("first_hunt")) {
        state_.memory.NoteEvent("first_hunt", "went hunting", "graveyard",
                                obs.x, obs.y, obs.nowMs);
    }
    nextActionMs_ = obs.nowMs + 3000;
    return false;
}

bool Runner::DoEarnGold(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) {
        // A life that predates the catalogue (the M4 lumberjack) has no
        // `produces` list, so there is nothing this goal can honestly sell.
        // Also a stand-down rather than a completion, for the same reason:
        // an uncatalogued life would otherwise spin here identically.
        LogLine("earn_gold: '%s' is not in the catalogue -- nothing to sell",
                state_.plan.family.c_str());
        planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kNothingToSellCooldownMs);
        planner_.Finish(false, "not in the catalogue", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // --- did the last sale actually pay? ----------------------------------
    //
    // The purse is the proof, not the fact that a packet was sent. Sphere
    // answers a refused sale with silence, and a sale that "worked" without
    // gold arriving is the same silent failure that made a working trainer
    // purchase read as a failure earlier in M5.
    if (sellSent_) {
        if (sellGoldBefore_ >= 0 && obs.gold > sellGoldBefore_) {
            const i32 paid = obs.gold - sellGoldBefore_;
            const i32 each = sellWanted_ > 0 ? paid / sellWanted_ : paid;
            LogLine("earn_gold: sold %d %s for %d gold (%d each) to a '%s'",
                    sellWanted_, sellItem_.c_str(), paid, each,
                    sellTrade_.c_str());

            // What it was worth, as OBSERVED. This is the only kind of price
            // this project lets a character know.
            market::PriceObservation po;
            po.item = sellItem_;
            po.pricePerUnit = each;
            po.source = market::PriceSource::NpcVendorBuys;
            po.who = sellTrade_;
            po.x = obs.x; po.y = obs.y;
            po.whenMs = obs.nowMs;
            state_.prices.Note(po);

            // Selling to an NPC CREATES gold. Recording it as a source is what
            // makes the anti-arbitrage invariant checkable afterwards.
            state_.ledger.Note(market::GoldFlow::CreatedVendor, paid,
                               sellItem_.c_str(), obs.nowMs);

            KnownSupplier sup;
            sup.need = std::string("buyer:") + sellItem_;
            sup.name = sellTrade_;
            sup.sourceType = "npc_vendor";
            sup.x = obs.x; sup.y = obs.y; sup.z = obs.z;
            sup.observedPricePerUnit = each;
            sup.lastVerifiedMs = obs.nowMs;
            sup.policyAllows = true;
            state_.memory.NoteSupplier(sup);

            state_.memory.NoteEvent("sold_to_vendor", sellItem_.c_str(),
                                    sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
            planner_.NoteProgress();
            sellSent_ = false;
            sellAsked_ = false;
            sellTrips_ = 0;
            sellLotCap_ = 0;   // this buyer could pay; stop rationing
            Checkpoint(client, obs.nowMs, "sold to a vendor");
            return true;
        }
        if (obs.nowMs - sellAskedMs_ > 12000) {
            LogLine("earn_gold: offered %d %s and the purse did not move "
                    "(still %d) -- this buyer did not take them",
                    sellWanted_, sellItem_.c_str(), obs.gold);
            state_.memory.NoteEvent("sale_refused", sellItem_.c_str(),
                                    sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
            sellSent_ = false;
            sellAsked_ = false;
            // A VENDOR'S PURSE IS FINITE. OFFER FEWER BEFORE GIVING UP.
            //
            // Alenne bought 5 poison scrolls for 125 gold and then refused 11
            // of the same scroll at the same quoted 25 each -- 275 gold she no
            // longer had. The offer is still LISTED, so nothing about the shop
            // says no; only the silent purse does. The old code went straight
            // to the next trade, found poison scrolls have exactly one buyer
            // trade, failed the goal, and was re-picked ten seconds later to
            // offer the identical 11 again. Gold sat at 135 for the rest of
            // the run while eleven saleable scrolls sat in the pack
            // (run_m5/p0gate3).
            //
            // A player offers half. Halve until the lot is empty, and only
            // then decide this buyer is no use -- the same "a refusal is
            // information, act on it" rule as the banker and trainer paths.
            if (sellLotCap_ <= 0) sellLotCap_ = sellWanted_;
            sellLotCap_ /= 2;
            if (sellLotCap_ > 0) {
                LogLine("earn_gold: trying a smaller lot -- %d %s this time "
                        "(a vendor's own purse runs out)",
                        sellLotCap_, sellItem_.c_str());
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
            sellLotCap_ = 0;
            ++sellBuyerIndex_;      // try the next trade that buys this
            sellTrade_.clear();
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- what is there to sell? -------------------------------------------
    // The threshold bends when the purse is empty: see PolicyForPurse.
    const market::TradePolicy tp = market::PolicyForPurse(obs.goldOnHand);
    const std::vector<market::Offer> offers =
        market::Surplus(*me, obs.pack, tp);
    if (offers.empty()) {
        // THE STOCK MAY BE IN THE BOX. The need layer scores this errand from
        // pack AND bank on purpose -- goods in the bank are still this
        // character's stock, "it just has to go and fetch them, which is a
        // step in the errand" (Needs.cpp) -- but this goal counted the pack
        // alone. A fisher with fish in the bank therefore scored NeedGold at
        // 0.45, won the scoring, entered here, found the pack empty, completed
        // with progress 0, and was re-picked sixty milliseconds later. It did
        // that for the whole session and never fished once, because the errand
        // that outranked fishing could never finish.
        //
        // Fetching the stock IS the errand, so do that rather than refuse.
        std::vector<market::Stock> holdings = obs.pack;
        for (const market::Stock& b : obs.bank) {
            bool merged = false;
            for (market::Stock& h : holdings) {
                if (h.item == b.item) { h.qty += b.qty; merged = true; break; }
            }
            if (!merged) holdings.push_back(b);
        }
        const std::vector<market::Offer> banked =
            market::Surplus(*me, holdings, tp);
        if (banked.empty()) {
            // AND THIS IS A FAILURE, NOT A COMPLETION.
            //
            // The comment above describes this exact bug for the fisher whose
            // stock was in the bank -- "completed with progress 0, and was
            // re-picked sixty milliseconds later" -- and fixed only that
            // branch. The terminal branch still returned true, so a character
            // with genuinely nothing to sell reported success, freed the
            // planner, and was handed the same errand again immediately.
            //
            // Kaelen did it 13,111 times in one session: died, lost everything
            // to full loot, woke with no gold and no goods, and spent 25
            // minutes completing EARN_GOLD at 60ms intervals --
            //
            //   goal_completed=EARN_GOLD progress=0
            //   goal=EARN_GOLD reason="no goal was running"
            //
            // -- while a graveyard full of things worth killing sat outside.
            // A goal that cannot act must stand down and let another have the
            // turn, exactly as GET_FOOD and GET_TOOL learned to.
            LogLine("earn_gold: nothing spare to sell (neither the pack nor "
                    "the bank holds a surplus of what this life makes) -- "
                    "standing down so something that CAN earn gets a turn");
            planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kNothingToSellCooldownMs);
            planner_.Finish(false, "nothing to sell", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }

        // Only chase stock a buyer would actually take; a bank full of
        // player-market goods is not a reason to walk to the bank.
        const market::Offer* fetch = nullptr;
        for (const market::Offer& o : banked) {
            if (market::QtyOf(obs.bank, o.item) <= 0) continue;
            if (market::MaySellToNpc(*me, o.item.c_str(), state_.ledger).allowed) {
                fetch = &o;
                break;
            }
        }
        if (!fetch) {
            LogLine("earn_gold: the bank holds a surplus but no NPC route for "
                    "it -- that is the player market's job, not this goal's");
            return true;
        }

        if (client.BankContainer() == 0) {
            // ARRIVING IS NOT ENOUGH -- the box has to be OPENED, by asking a
            // banker for it. Travelling and then re-testing "am I at the bank"
            // loops forever the moment the trip completes instantly because
            // the character is already standing there, which is exactly what
            // it did: eight identical "going to fetch it" lines in twelve
            // seconds, never once opening the box. Same shape as the no-op
            // travel loop that pinned GATHER_LOGS.
            if (client.TravelBusy()) return false;
            const u32 banker = client.NearestMobileWithTrade("banker");
            if (banker) {
                LogLine("earn_gold: the stock is in the bank (%d %s) -- opening "
                        "the box", market::QtyOf(obs.bank, fetch->item),
                        fetch->item.c_str());
                client.ActionOpenBank(banker);
                i32 bx = 0, by = 0; i8 bz = 0;
                if (client.MobilePosition(banker, &bx, &by, &bz)) {
                    state_.memory.NotePlace("bank", "bank", bx, by, bz,
                                            obs.nowMs);
                }
                bankOpenedMs_ = obs.nowMs;
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            if (!travelInFlight_) {
                if (++bankTrips_ > kMaxBankTrips) {
                    LogLine("goal_failed=EARN_GOLD reason=\"%d trips and still "
                            "no banker in reach\"", bankTrips_);
                    planner_.Finish(false, "no banker reachable", obs.nowMs);
                    bankTrips_ = 0;
                    nextActionMs_ = obs.nowMs + 30000;
                    return false;
                }
                const KnownPlace* known = state_.memory.NearestPlace("bank", obs.x, obs.y);
                LogLine("earn_gold: the stock is in the bank (%d %s) -- going "
                        "to fetch it (trip %d)",
                        market::QtyOf(obs.bank, fetch->item),
                        fetch->item.c_str(), bankTrips_);
                travelInFlight_ =
                    known ? client.TravelToPoint(known->x, known->y, 2, "bank")
                          : client.TravelToService(wm::Service::Banker,
                                                   HomeOrNearest(state_.homeCity));
                if (!travelInFlight_) {
                    LogLine("goal_blocked=EARN_GOLD reason=\"%s\" (%s)",
                            faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                            client.TravelFailureText());
                    planner_.NoteAttempt(obs.nowMs);
                    nextActionMs_ = obs.nowMs + 15000;
                }
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            travelInFlight_ = false;
            // Arrived. A title only exists after a name request, so ask who is
            // here before concluding no banker is.
            client.ActionScanMobiles();
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        bankTrips_ = 0;

        const std::vector<u16> gfx = econ::GraphicsForItem(fetch->item.c_str());
        const u32 serial = client.FindContainerItemByGraphic(
            client.BankContainer(), gfx.data(), gfx.size());
        if (!serial) {
            LogLine("earn_gold: the bank ledger says %s but the open box does "
                    "not show it -- the ledger is stale", fetch->item.c_str());
            return true;
        }
        const i32 take = market::QtyOf(obs.bank, fetch->item);
        LogLine("earn_gold: withdrawing %d %s to sell", take,
                fetch->item.c_str());
        client.TakeFromContainer(serial, static_cast<u16>(take));
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Take the first offer this life may legitimately sell.
    const market::Offer* chosen = nullptr;
    for (const market::Offer& o : offers) {
        const market::SellRuling r =
            market::MaySellToNpc(*me, o.item.c_str(), state_.ledger);
        if (!r.allowed) {
            LogLine("earn_gold: will NOT sell %d %s -- %s", o.qty,
                    o.item.c_str(), r.reason);
            state_.memory.NoteEvent("sale_refused_policy", o.item.c_str(),
                                    r.reason, obs.x, obs.y, obs.nowMs);
            continue;
        }
        chosen = &o;
        break;
    }
    if (!chosen) {
        LogLine("earn_gold: everything spare is barred from an NPC sale; "
                "banking instead");
        return true;
    }

    if (sellItem_ != chosen->item) {
        sellItem_ = chosen->item;
        sellBuyerIndex_ = 0;
        sellTrade_.clear();
        sellTrips_ = 0;
        sellLotCap_ = 0;
    }
    sellWanted_ = chosen->qty;

    // --- who buys it? ------------------------------------------------------
    const std::vector<const market::NpcBuyer*> buyers =
        market::NpcBuyersFor(sellItem_.c_str());
    if (buyers.empty()) {
        // A real answer, not a failure. The character stays resource-rich and
        // wealth-poor, which is a legitimate state on this shard.
        LogLine("earn_gold: no NPC trade on this shard buys %s; banking it "
                "instead", sellItem_.c_str());
        state_.memory.NoteEvent("no_buyer", sellItem_.c_str(), "",
                                obs.x, obs.y, obs.nowMs);
        return true;
    }
    if (sellBuyerIndex_ >= buyers.size()) {
        LogLine("goal_failed=EARN_GOLD reason=\"tried all %zu trades that buy "
                "%s\" -- standing down for %llds", buyers.size(),
                sellItem_.c_str(),
                static_cast<long long>(kNoBuyerCooldownMs / 1000));
        // STAND DOWN, do not re-decide. Failing here without a cooldown put
        // EARN_GOLD straight back at the top of the list and the whole walk
        // began again 2.6 seconds later: run_m5/p0gate3 logged this same line
        // every few seconds with gold pinned at 135 and eleven saleable
        // scrolls in the pack. The buyers have not changed in that time; the
        // vendor's purse needs a restock cycle, and something else can be
        // done meanwhile.
        planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kNoBuyerCooldownMs);
        planner_.Finish(false, "no buyer took the goods", obs.nowMs);
        sellBuyerIndex_ = 0;
        sellLotCap_ = 0;
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }
    const market::NpcBuyer* buyer = buyers[sellBuyerIndex_];
    if (sellTrade_ != buyer->trade) {
        sellTrade_ = buyer->trade;
        sellService_ = ServiceForTrade(buyer->trade);
        sellAsked_ = false;
    }

    if (client.TravelBusy()) return false;

    // --- get to one ---------------------------------------------------------
    const u32 vendor = client.NearestShopkeeperWithTrade(sellTrade_.c_str(),
                                                        sellService_);
    if (!vendor) {
        if (sellTrips_ >= kMaxSellTrips) {
            LogLine("earn_gold: no '%s' reachable after %d trips; trying the "
                    "next trade that buys %s", sellTrade_.c_str(), sellTrips_,
                    sellItem_.c_str());
            ++sellBuyerIndex_;
            sellTrade_.clear();
            sellTrips_ = 0;
            return false;
        }
        if (!travelInFlight_) {
            ++sellTrips_;
            const KnownSupplier* known = state_.memory.BestSupplier(
                (std::string("buyer:") + sellItem_).c_str());
            if (known && known->name == sellTrade_) {
                LogLine("earn_gold: back to a buyer we have used before, "
                        "'%s' at %d,%d", known->name.c_str(), known->x, known->y);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 2, "buyer");
            } else {
                LogLine("earn_gold: looking for a '%s' to buy %d %s (trip %d)",
                        sellTrade_.c_str(), sellWanted_, sellItem_.c_str(),
                        sellTrips_);
                travelInFlight_ = client.TravelToService(sellService_, HomeOrNearest(state_.homeCity));
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=EARN_GOLD reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        LogLine("earn_gold: arrived at %d,%d -- asking who is here",
                client.PlayerX(), client.PlayerY());
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // --- stand next to the one we mean to deal with -------------------------
    //
    // Sphere routes a vendor keyword to whoever is NEAREST in earshot, not to
    // the name spoken. The first live sale said "Weston sell" three tiles from
    // Weston the carpenter, and JOSHUA THE ARCHITECT answered -- with "You
    // have nothing I'm interested in", because architects do not buy logs.
    // Walking up first is what makes the intended vendor the nearest listener.
    if (sellVendorSerial_ != vendor) {
        sellVendorSerial_ = vendor;
        sellApproached_ = false;
    }
    if (!sellApproached_) {
        i32 vx = 0, vy = 0; i8 vz = 0;
        if (client.MobilePosition(vendor, &vx, &vy, &vz)) {
            const i32 d = TileDist(obs.x, obs.y, vx, vy);
            const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
            if (d > 1 || dz > 3) {
                LogLine("earn_gold: the '%s' is %d tiles and %d z away -- "
                        "walking up before speaking, or the nearest other "
                        "vendor answers instead", sellTrade_.c_str(), d, dz);
                // WALK TO THE MOBILE, NOT TO ITS FOOTPRINT.
                //
                // TravelToPoint zeroes travelEntitySerial_ and passes no Z at
                // all (ClientTravel.cpp:183-186), so A* is free to finish on
                // whichever floor of that column it reaches first. In a
                // multi-storey Britain mage shop that is the wrong storey:
                // "the 'mage' is 1 tiles and 40 z away", arrived by every 2D
                // measure and out of speech range by the server's, so the buy
                // list never came and the character could not sell a thing for
                // a whole session (run_m5/p0gate4). A UO storey is about 20 z
                // and the same-floor tolerance is 12, so this is never a near
                // miss -- it is a different room.
                //
                // TravelToEntity keeps the serial, re-aims at the live
                // position as it closes, and pins the goal Z on the final leg
                // (ClientTravel.cpp:644-651) -- which is exactly what chasing
                // a wandering NPC needs.
                travelInFlight_ = client.TravelToEntity(vendor, 1);
                sellApproached_ = true;   // one approach, then talk regardless
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        sellApproached_ = true;
    }

    // --- ask what it will take ---------------------------------------------
    if (!sellAsked_) {
        LogLine("earn_gold: asking the '%s' what it buys", sellTrade_.c_str());
        client.ActionVendorSellOpen(vendor);
        sellAsked_ = true;
        sellAskedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    if (client.VendorSellFrom() != vendor) {
        if (obs.nowMs - sellAskedMs_ > 10000) {
            LogLine("earn_gold: the '%s' never showed a buy list",
                    sellTrade_.c_str());
            ++sellBuyerIndex_;
            sellTrade_.clear();
            sellAsked_ = false;
            sellVendorSerial_ = 0;
            sellApproached_ = false;
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- sell, matching by GRAPHIC -----------------------------------------
    //
    // The 0x9E list carries the serials of OUR OWN items, so this is a join
    // against the pack rather than against the vendor's stock.
    const std::vector<u16> mine = econ::GraphicsForItem(sellItem_.c_str());
    for (const Client::VendorItem& v : client.VendorSellOffer()) {
        bool match = false;
        for (u16 g : mine) { if (v.graphic == g) { match = true; break; } }
        if (!match) continue;

        // THE WHOLE LOT, NOT ONE PIECE. "yes dont sell one buy one" (project
        // owner, 2026-08-29). A dagger does not stack, so the vendor's buy
        // list holds sixteen separate entries of amount 1 -- and
        // min(sellWanted_, v.amount) is therefore always 1. Corwyn sold a
        // dagger, walked back to the forge, made another, and returned.
        //
        // The 0x9F packet has always carried an item COUNT; only the caller
        // was passing one. So gather every entry of this item the vendor will
        // take, up to what this life wants to be rid of, and sell them in a
        // single transaction.
        i32 remaining = sellWanted_;
        if (sellLotCap_ > 0) remaining = std::min<i32>(remaining, sellLotCap_);
        if (remaining <= 0) continue;

        std::vector<std::pair<u32, u16>> lot;
        i32 lotQty = 0;
        for (const Client::VendorItem& w : client.VendorSellOffer()) {
            if (lotQty >= remaining) break;
            bool same = false;
            for (u16 g : mine) { if (w.graphic == g) { same = true; break; } }
            if (!same || w.amount <= 0) continue;
            const i32 take =
                std::min<i32>(remaining - lotQty, static_cast<i32>(w.amount));
            lot.emplace_back(w.serial, static_cast<u16>(take));
            lotQty += take;
        }
        if (lot.empty()) continue;

        LogLine("earn_gold: '%s' offers %u gold each for %s; selling %d in "
                "%u lot(s)", sellTrade_.c_str(), v.price, sellItem_.c_str(),
                lotQty, static_cast<unsigned>(lot.size()));
        sellWanted_ = lotQty;
        sellGoldBefore_ = obs.gold;
        sellAskedMs_ = obs.nowMs;
        client.ActionVendorSellMany(vendor, lot);
        sellSent_ = true;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // NOTHING IT MAKES -- BUT MAYBE SOMETHING IT FOUND.
    //
    // Loot is income for a warrior and a mage, and the sell path could not see
    // it: market::Surplus only ever considers `produces`, which means WHAT I
    // MAKE. Loot is WHAT I FOUND, and the model had no word for it, so a mage
    // with a pack of graveyard drops had nothing the economy recognised.
    //
    // The fix needs no item table and no guessing. The 0x9E list IS the
    // server's own answer: it enumerates OUR items this vendor will buy, with
    // its prices. So offer whatever is in that list that this life has no use
    // for. A graphic table would have been the wrong foundation anyway --
    // ItemNameForGraphic maps 63 graphics, nearly all crafting materials and
    // not one weapon or piece of armour.
    for (const Client::VendorItem& v : client.VendorSellOffer()) {
        if (v.amount <= 0 || v.price == 0) continue;
        if (LifeNeedsGraphic(v.graphic)) continue;   // tool, stock, or input

        i32 qty = static_cast<i32>(v.amount);
        if (sellLotCap_ > 0) qty = std::min<i32>(qty, sellLotCap_);
        if (qty <= 0) continue;

        LogLine("earn_gold: selling %d looted 0x%04X at %u each to a '%s' "
                "(this life has no use for it)",
                qty, v.graphic, v.price, sellTrade_.c_str());
        sellWanted_ = qty;
        sellGoldBefore_ = obs.gold;
        sellAskedMs_ = obs.nowMs;
        client.ActionVendorSell(vendor, v.serial, static_cast<u16>(qty));
        sellSent_ = true;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    LogLine("earn_gold: this '%s' does not take %s after all, nor anything "
            "spare we are carrying; trying the next trade",
            sellTrade_.c_str(), sellItem_.c_str());
    state_.memory.NoteEvent("buyer_list_lacks_item", sellItem_.c_str(),
                            sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
    ++sellBuyerIndex_;
    sellTrade_.clear();
    sellAsked_ = false;
    return false;
}


// ---------------------------------------------------------------------------
// TRAIN_AT_NPC -- buy a skill the way a player does.
//
//   travel to a trade NPC -> ask who is here -> say "train <skill>"
//   -> READ THE PRICE THE NPC QUOTES -> hand over exactly that in gold
//   -> verify against the server's own skill number
//
// The price is never computed. Source-X answers with
// "For %d gold I will train you in all I know of %s" (defmessages.tbl
// NPC_TRAINER_PRICE) and the bot reads that line. If the shard retunes
// NPCTrainCost the bot follows without being told, and a refusal
// ("I know nothing about that", "You already know as much as I can teach")
// is read as a refusal rather than timed out.
//
// Nothing here sets a skill. The gold leaves the pack by an ordinary
// lift-and-drop onto the trainer, and the proof of training is the server's
// skill value afterwards.
// ---------------------------------------------------------------------------
bool Runner::DoTrainAtNpc(Client& client, const Observation& obs) {
    const int skillId = obs.wantTrainSkill;
    if (skillId < 0) return true;

    const i32 have = client.PlayerSkillBase(static_cast<u16>(skillId));

    // --- did the gold we handed over actually buy anything? ---------------
    //
    // The proof is the SERVER'S skill number, not our own bookkeeping. Asking
    // for a fresh skill list is what a player's client does anyway.
    if (trainPaid_) {
        if (have > trainSkillBefore_) {
            LogLine("training: %s %.1f -> %.1f, bought from a trainer",
                    rules::SkillName(skillId), trainSkillBefore_ / 10.0, have / 10.0);
            state_.memory.NoteEvent("skill_trained", rules::SkillName(skillId),
                                    trainerTrade_.c_str(), obs.x, obs.y,
                                    obs.nowMs);
            // Remember WHERE, so the next skill this life buys does not start
            // the search from nothing. Recorded only because we dealt with it.
            KnownSupplier sup;
            sup.need = std::string("trainer:") + trainerTrade_;
            sup.name = trainerTrade_;
            sup.sourceType = "npc_trainer";
            sup.x = obs.x; sup.y = obs.y; sup.z = obs.z;
            sup.observedPricePerUnit = trainQuoted_;
            sup.lastVerifiedMs = obs.nowMs;
            sup.policyAllows = true;
            state_.memory.NoteSupplier(sup);
            // The other half of the verdict: this trade DOES teach this skill,
            // and here is what it charged. Worth as much as the refusal.
            TrainerVerdict v;
            v.skillId  = skillId;
            v.trade    = trainerTrade_;
            v.npcSerial = trainerSerial_;
            v.taught   = true;
            v.atTenths = have;
            v.quoted   = trainQuoted_;
            v.why      = "taught";
            v.whenMs   = obs.nowMs;
            state_.memory.NoteTrainerVerdict(v);
            trainPaid_ = false;
            trainAsked_ = false;
            trainSkillsAsked_ = false;
            trainPackRefreshed_ = false;
            trainPayAttempts_ = 0;
            trainTrips_ = 0;
            Checkpoint(client, obs.nowMs, "skill bought from a trainer");
            return true;
        }
        // The server does not push the new number, so ASK for it -- once,
        // promptly. The first version only asked after a ten-second timeout
        // had already declared failure, so a purchase that actually worked
        // (11.8 -> 21.1 for 93 gold, live) was recorded as "has not moved".
        if (!trainSkillsAsked_ && obs.nowMs - trainPaidMs_ > 1500) {
            client.ActionRequestSkills();
            trainSkillsAsked_ = true;
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        if (obs.nowMs - trainPaidMs_ > 15000) {
            LogLine("training: paid %d for %s but the server still reports "
                    "%.1f after 15s", trainQuoted_, rules::SkillName(skillId),
                    have / 10.0);
            state_.memory.NoteEvent("training_unverified",
                                    rules::SkillName(skillId),
                                    trainerTrade_.c_str(), obs.x, obs.y,
                                    obs.nowMs);
            planner_.NoteAttempt(obs.nowMs);
            trainPaid_ = false;
            trainAsked_ = false;
            trainSkillsAsked_ = false;
            // Ask for the pack again: the most likely reason a give did
            // nothing is that the serial it named no longer exists.
            trainPackRefreshed_ = false;
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    if (have >= obs.wantTrainTarget) {
        LogLine("training: %s is already at %.1f -- nothing to buy",
                rules::SkillName(skillId), have / 10.0);
        return true;
    }

    if (client.TravelBusy()) return false;

    // --- get to a trainer --------------------------------------------------
    // Skip anyone of this trade who has already been asked three times and
    // never said a word. Alenne the mage stood one tile away and answered
    // nothing about Meditation across two sessions and seven asks, while Alek
    // -- also "the mage", in a different Britain shop -- quoted 184 gold for
    // the same skill on the first ask. Why one answers and the other does not
    // is UNKNOWN; what a player does about it is not, and is what this does.
    // AND SKIP ANYONE WHO ALREADY REFUSED. Silence and refusal are different
    // answers with the same consequence for THIS NPC -- and, because the
    // ceiling is that NPC's own skill and not the trade's, the next one may
    // still teach it. Seeded from memory rather than kept in the transient
    // list, so a refusal survives the logout that earned it.
    for (u32 refused :
         state_.memory.TrainersWhoRefused(skillId, trainerTrade_.c_str())) {
        bool known = false;
        for (u32 k : trainerSilent_) { if (k == refused) { known = true; break; } }
        if (!known) trainerSilent_.push_back(refused);
    }
    // THE GUILDMASTER FIRST. Both the guildmaster and the shopkeeper of a
    // trade answer NearestMobileWithTrade -- it truncates "mage guildmaster"
    // to "mage" -- so it returns whoever is nearer, and in Britain's mage shop
    // that is always the shopkeeper. Alenne, two tiles away, refused Ysolde
    // Meditation at 21.9 on every visit while a mage guildmaster stood five
    // tiles off at 1490,1549 able to teach it (run_m5/p0gate7). Ask the one
    // whose job it is; fall back to the trade only when no guildmaster is in
    // reach, because an ordinary tradesman can still teach a low skill.
    u32 trainer = client.NearestGuildmasterForTrade(trainerTrade_.c_str(),
                                                    trainerSilent_);
    if (trainer && trainer != trainerSerial_) {
        LogLine("training: a '%s guildmaster' is here -- asking them rather "
                "than the shopkeeper", trainerTrade_.c_str());
    }
    if (!trainer) {
        trainer = client.NearestMobileWithTrade(trainerTrade_.c_str(),
                                                trainerSilent_);
    }
    if (!trainer) {
        if (trainTrips_ >= kMaxTrainTrips) {
            LogLine("goal_failed=TRAIN_AT_NPC reason=\"no '%s' reachable after "
                    "%d trips\"", trainerTrade_.c_str(), trainTrips_);
            state_.memory.NoteEvent("trainer_unreachable", trainerTrade_.c_str(),
                                    "", obs.x, obs.y, obs.nowMs);
            // STAND DOWN. Without this the goal was re-picked 60 ms later and
            // the whole three-trip count began again -- and every one of those
            // "trips" was a no-op, because the remembered trainer place is the
            // tile the character is already standing on: run_m5/p0gate9 logs
            // travel_start and ARRIVED at the same coordinate with legs=0,
            // three times, then goal_failed, then immediately goal=TRAIN_AT_NPC
            // again. Nothing about the world changes in two seconds.
            planner_.Cooldown(GoalKind::TrainAtNpc,
                              obs.nowMs + kNoTrainerCooldownMs);
            planner_.Finish(false, "no trainer reachable", obs.nowMs);
            trainTrips_ = 0;
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        if (!travelInFlight_) {
            ++trainTrips_;
            const KnownSupplier* known = state_.memory.BestSupplier(
                (std::string("trainer:") + trainerTrade_).c_str());
            if (known) {
                // STANDING ON THE SPOT AND SEEING NOBODY IS THE DISPROOF.
                //
                // A remembered supplier is a POSITION, not a mobile, and the
                // NPC that earned it can be gone -- despawned, re-rolled by a
                // spawner, or just wandered off. The old code walked back
                // regardless, which meant that when the trainer was missing it
                // issued a travel to the tile it was ALREADY on. Live:
                //
                //   arrived at (2629,2099,10)
                //   training: back to a trainer we have used before, 'carpenter' at 2629,2099
                //   training: back to a trainer we have used before, 'carpenter' at 2629,2099
                //   goal_failed=TRAIN_AT_NPC reason="no 'carpenter' reachable after 3 trips"
                //
                // Two of the three trips were spent travelling nowhere, two
                // seconds apart, and the goal then blamed the world. This is
                // the same shape ForgetPlace was written for -- belief that
                // survives being disproved is not memory, it is a loop.
                const i32 dToKnown = TileDist(obs.x, obs.y, known->x, known->y);
                if (dToKnown <= 3) {
                    LogLine("training: no '%s' where we remembered one at %d,%d "
                            "-- forgetting it and looking properly",
                            trainerTrade_.c_str(), known->x, known->y);
                    state_.memory.ForgetSupplier(
                        (std::string("trainer:") + trainerTrade_).c_str(),
                        known->x, known->y);
                    // Do not spend a trip on a lesson. Rescan from here first:
                    // the trade may be a few tiles off rather than absent.
                    --trainTrips_;
                    client.ActionScanMobiles();
                    nextActionMs_ = obs.nowMs + 2000;
                    return false;
                }
                LogLine("training: back to a trainer we have used before, "
                        "'%s' at %d,%d", known->name.c_str(), known->x, known->y);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 2, "trainer");
            } else {
                LogLine("training: looking for a '%s' to teach %s (trip %d)",
                        trainerTrade_.c_str(), rules::SkillName(skillId), trainTrips_);
                travelInFlight_ = client.TravelToServiceSkipping(
                    trainerService_, HomeOrNearest(state_.homeCity), trainerSilent_,
                    &trainerShopsTried_);
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=TRAIN_AT_NPC reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        LogLine("training: arrived at %d,%d -- asking who is here",
                client.PlayerX(), client.PlayerY());
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // --- stand where a player would stand ---------------------------------
    //
    // Speech is heard by position, not by intent. The first live attempt at a
    // castle scribe stood 3 tiles away in 2D but SEVEN z below her -- another
    // floor -- and got a greeting but no training reply, three times, silently.
    // Close the distance in three dimensions before talking.
    if (trainerSerial_ != trainer) {
        // A DIFFERENT NPC, so the conversation starts over. Leaving
        // trainAsked_ set meant the bot skipped re-asking and then judged the
        // silence of an NPC it had never spoken to, reporting "the trainer
        // never answered" about a conversation that never happened.
        trainerSerial_ = trainer;
        trainerApproached_ = false;
        trainApproaches_ = 0;
        trainSilentAsks_ = 0;
        trainAsked_ = false;
    }
    if (!trainerApproached_) {
        i32 tx = 0, ty = 0; i8 tz = 0;
        if (client.MobilePosition(trainer, &tx, &ty, &tz)) {
            const i32 d = TileDist(obs.x, obs.y, tx, ty);
            const i32 dz = (obs.z > tz) ? (obs.z - tz) : (tz - obs.z);
            if (d > 2 || dz > 3) {
                LogLine("training: '%s' is %d tiles and %d z away -- walking to "
                        "them before speaking (approach %d of %d)",
                        trainerTrade_.c_str(), d, dz, trainApproaches_ + 1,
                        kMaxTrainApproaches);
                travelInFlight_ = client.TravelToEntity(trainer, 1);
                // ONE APPROACH WAS NOT ENOUGH, and the cost of that was a
                // whole session. Ysolde stood 7 tiles and 2 z from Alenne,
                // gave up closing after a single attempt, and then asked about
                // Meditation seven times from outside the shop. Every ask went
                // unheard, every timeout read as "the trainer never answered",
                // and the character concluded a perfectly good trainer was
                // useless. The very next session it walked all the way in,
                // asked once, and was quoted 219 gold.
                //
                // Speech is heard by POSITION. Keep closing until we are
                // actually in earshot, and only then talk regardless.
                if (++trainApproaches_ >= kMaxTrainApproaches) {
                    trainerApproached_ = true;
                }
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            trainApproaches_ = 0;
        }
        trainerApproached_ = true;
    }

    // --- ask, then read what the NPC actually says -------------------------
    if (!trainAsked_) {
        // TWO marks, on two clocks, because they answer different questions:
        // the journal mark says "read replies after this point", the tick mark
        // says "how long have I waited". Using the journal clock for both made
        // a 12-second window expire in 2.5 seconds.
        trainAskedMs_ = client.JournalNowMs();
        trainAskedTickMs_ = obs.nowMs;
        trainAsked_ = true;
        LogLine("training: asking the trainer about %s", rules::SkillName(skillId));
        client.ActionNpcTrain(trainer, SkillKey(skillId));
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // Refusals first -- each is a real answer, not a timeout.
    struct Refusal { const char* text; const char* why; };
    static const Refusal kRefusals[] = {
        {"i know nothing about",        "this NPC does not teach it"},
        {"you know more about",         "the character already exceeds the trainer"},
        {"you already know as much",    "the trainer has nothing left to give"},
        {"i would never train",         "the trainer refuses this character"},
        {"there is nothing that i can", "the trainer has nothing to teach"},
    };
    for (const Refusal& r : kRefusals) {
        if (!client.JournalSaidSince(r.text, trainAskedMs_)) continue;
        LogLine("training: %s refused to teach %s at %.1f -- %s",
                trainerTrade_.c_str(), rules::SkillName(skillId), have / 10.0,
                r.why);
        // A durable verdict, not a log line. The previous version wrote an
        // event nothing ever read and then reset the trip counter, so the
        // character re-selected the goal and asked the same NPC again roughly
        // every two seconds for the rest of the session -- 30+ times in the
        // first live run, with the NPC patiently refusing each time.
        TrainerVerdict v;
        v.skillId  = skillId;
        v.trade    = trainerTrade_;
        v.npcSerial = trainerSerial_;
        v.taught   = false;
        v.atTenths = have;
        v.why      = r.why;
        v.whenMs   = obs.nowMs;
        state_.memory.NoteTrainerVerdict(v);
        state_.memory.NoteEvent("trainer_refused", r.why, trainerTrade_.c_str(),
                                obs.x, obs.y, obs.nowMs);
        planner_.Finish(false, r.why, obs.nowMs);
        trainAsked_ = false;
        trainTrips_ = 0;
        Checkpoint(client, obs.nowMs, "trainer refusal");
        return false;
    }

    const i32 quoted = client.JournalNumberSince("i will train you", trainAskedMs_);

    // PAY WHOEVER ACTUALLY QUOTED.
    //
    // The price is read out of the journal by TEXT, with no regard for who
    // said it, and the fee was then handed to the NPC we had addressed. Those
    // are not always the same person:
    //
    //   [TRAIN] ask 0x00009096 say='Rhyssa train Tinkering'
    //   Pembroke: For 101 gold I will train you in all I know of Tinkering
    //   training: paying the quoted 101 gold ... (purse 9801)
    //   training: paid 101 for Tinkering but the server still reports 19.9
    //
    // Two tinkers stand together in Minoc; Sphere answered with the nearer
    // one. The gold went to Rhyssa, who had offered nothing, and Pembroke --
    // who had -- was never paid. The skill did not move, the purse did not
    // move, and the character went round again.
    //
    // So the payee is the SPEAKER of the quote when one can be identified.
    u32 payee = trainer;
    if (quoted > 0) {
        std::vector<Client::Heard> said;
        client.JournalHeardSince(trainAskedMs_, said);
        for (const Client::Heard& h : said) {
            std::string low;
            for (char c : h.text)
                low.push_back(static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c))));
            if (low.find("i will train you") == std::string::npos) continue;
            if (!h.speaker || h.speaker == trainer) break;
            LogLine("training: '%s' answered instead of the one asked -- "
                    "paying the trainer who actually quoted",
                    h.name.empty() ? "someone else" : h.name.c_str());
            payee = h.speaker;
            break;
        }
    }
    if (quoted <= 0) {
        if (obs.nowMs - trainAskedTickMs_ > 12000) {
            ++trainSilentAsks_;
            LogLine("training: no quote and no refusal after 12s (ask %d of %d)",
                    trainSilentAsks_, kMaxSilentAsks);
            planner_.NoteAttempt(obs.nowMs);
            trainAsked_ = false;
            // Silence is most often distance. Try to close it again before
            // repeating the same words from the same spot.
            trainerApproached_ = false;
            if (trainSilentAsks_ >= kMaxSilentAsks) {
                // Give up on this NPC for now. Deliberately NOT written as a
                // trainer verdict: a verdict is what an NPC SAID, and this one
                // said nothing. Recording silence as a refusal would teach the
                // character something the world never told it.
                LogLine("goal_failed=TRAIN_AT_NPC reason=\"'%s' never answered "
                        "about %s\"", trainerTrade_.c_str(),
                        rules::SkillName(skillId));
                state_.memory.NoteEvent("trainer_silent",
                                        rules::SkillName(skillId),
                                        trainerTrade_.c_str(), obs.x, obs.y,
                                        obs.nowMs);
                planner_.Cooldown(GoalKind::TrainAtNpc,
                                  obs.nowMs + kNoTrainerCooldownMs);
                planner_.Finish(false, "the trainer never answered", obs.nowMs);
                // Do not walk back to this same silent NPC next time. Held for
                // the session only, and not written to memory: silence is not
                // something the world told the character, so it is not a
                // belief -- it is just somewhere already tried today.
                bool listed = false;
                for (u32 sk : trainerSilent_) {
                    if (sk == trainerSerial_) { listed = true; break; }
                }
                if (!listed && trainerSerial_) {
                    trainerSilent_.push_back(trainerSerial_);
                }
                trainSilentAsks_ = 0;
                trainerSerial_ = 0;
                trainerApproached_ = false;
            }
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    if (obs.gold < quoted) {
        LogLine("BLOCKED_NEED %s: the trainer wants %d gold and the purse holds "
                "%d -- going back to work", rules::SkillName(skillId), quoted, obs.gold);
        state_.memory.NoteEvent("trainer_quote", rules::SkillName(skillId),
                                trainerTrade_.c_str(), obs.x, obs.y, obs.nowMs);
        planner_.Finish(false, "cannot afford the quoted fee", obs.nowMs);
        trainAsked_ = false;
        return false;
    }

    // --- pay exactly what was quoted ---------------------------------------
    //
    // Ask for the pack's contents FIRST. Sphere splits a gold stack to make
    // change, which retires the old serial, and a give addressed to a retired
    // serial is a silent no-op: no gold moves, the NPC says nothing, and
    // nothing anywhere reports an error. That is exactly what happened on the
    // second purchase of the first successful live run -- the first 108gp
    // give did nothing and only the retry landed.
    if (!trainPackRefreshed_) {
        client.ActionOpenBackpack();
        trainPackRefreshed_ = true;
        nextActionMs_ = obs.nowMs + 1200;
        return false;
    }
    if (trainPayAttempts_ >= kMaxPayAttempts) {
        LogLine("goal_failed=TRAIN_AT_NPC reason=\"%d attempts to hand over %d "
                "gold all failed\"", trainPayAttempts_, quoted);
        planner_.Finish(false, "could not hand over the fee", obs.nowMs);
        trainAsked_ = false;
        trainPackRefreshed_ = false;
        trainPayAttempts_ = 0;
        return false;
    }
    // The fee has to be IN THE PACK. obs.gold counts the bank box on this
    // shard, so "can afford" and "can hand over" are different questions.
    if (FetchCoinForPurchase(client, obs, quoted)) return false;
    const u32 gold = client.FindBackpackItemByGraphic(kGoldCoin);
    if (!gold) {
        LogLine("training: quoted %d but no gold stack found in the pack", quoted);
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    LogLine("training: paying the quoted %d gold for %s (purse %d)",
            quoted, rules::SkillName(skillId), obs.gold);
    trainSkillBefore_ = have;
    trainQuoted_ = quoted;
    // The TICK clock, not the journal clock. These are different clocks and
    // mixing them made the ten-second verification window expire in 8.7s.
    trainPaidMs_ = obs.nowMs;
    trainSkillsAsked_ = false;
    ++trainPayAttempts_;
    client.ActionNpcGive(payee, gold, static_cast<u16>(quoted));
    trainPaid_ = true;
    nextActionMs_ = obs.nowMs + 4000;
    return false;
}

// ---------------------------------------------------------------------------
// TRADE_WITH_PLAYER -- the only goal that needs somebody else to exist.
//
// Both halves run in the same body, because a character is whichever one the
// situation makes it: it announces what it has spare, and it answers what it
// hears. A fleet of twenty is mostly listeners at any moment.
//
//   stand where players gather (the home bank)
//   -> announce "WTS 20 i_log 2gp", occasionally, not every tick
//   -> hear "WTB i_log" -> walk to the speaker -> open the trade window
//   -> put the goods in -> accept -> verify against the PACK, not the packet
//
// The listener half is symmetrical: hear a WTS, decide with ConsiderOffer,
// answer with WTB, and wait to be traded with.
// ---------------------------------------------------------------------------
bool Runner::DoTradeWithPlayer(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    // --- a trade is already open: drive it to a conclusion ------------------
    const trade::TradeState& tr = client.Trade();
    if (tr.Active()) {
        return DriveOpenTrade(client, obs);
    }
    if (tr.CurrentPhase() == trade::Phase::Completed) {
        LogLine("trade: window closed complete with %s", tradePartnerName_.c_str());
        // The PACK is the proof. A completed window means the server moved
        // the goods; believing the packet without checking is how a "sale"
        // that moved nothing gets recorded as income.
        const i32 now = market::QtyOf(obs.pack, tradeItem_);
        if (tradeSellingQty_ > 0 && now < tradePackBefore_) {
            const i32 moved = tradePackBefore_ - now;
            const i32 paid = obs.gold - tradeGoldBefore_;
            LogLine("trade: gave %d %s to %s for %d gold", moved,
                    tradeItem_.c_str(), tradePartnerName_.c_str(), paid);
            if (paid > 0) {
                market::PriceObservation po;
                po.item = tradeItem_;
                po.pricePerUnit = paid / moved;
                po.source = market::PriceSource::PlayerTraded;
                po.who = tradePartnerName_;
                po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
                state_.prices.Note(po);
                state_.ledger.Note(market::GoldFlow::TransferPlayerTrade, paid,
                                   tradeItem_.c_str(), obs.nowMs);
            }
            state_.memory.NoteEvent("traded_with_player", tradeItem_.c_str(),
                                    tradePartnerName_.c_str(), obs.x, obs.y,
                                    obs.nowMs);
            planner_.NoteProgress();
        } else if (tradeSellingQty_ == 0 && now > tradePackBefore_) {
            const i32 got = now - tradePackBefore_;
            const i32 spent = tradeGoldBefore_ - obs.gold;
            LogLine("trade: got %d %s from %s for %d gold", got,
                    tradeItem_.c_str(), tradePartnerName_.c_str(), spent);
            if (spent > 0) {
                market::PriceObservation po;
                po.item = tradeItem_;
                po.pricePerUnit = spent / got;
                po.source = market::PriceSource::PlayerTraded;
                po.who = tradePartnerName_;
                po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
                state_.prices.Note(po);
                state_.ledger.Note(market::GoldFlow::TransferPlayerTradeOut, spent,
                                   tradeItem_.c_str(), obs.nowMs);
            }
            planner_.NoteProgress();
        } else {
            LogLine("trade: window completed but nothing moved");
        }
        ResetTradeState();
        Checkpoint(client, obs.nowMs, "traded with a player");
        return true;
    }
    if (tr.CurrentPhase() == trade::Phase::Cancelled) {
        LogLine("trade: %s cancelled (%s)", tradePartnerName_.c_str(),
                trade::CloseReasonName(tr.Reason()));
        state_.memory.NoteEvent("trade_cancelled", tradeItem_.c_str(),
                                tradePartnerName_.c_str(), obs.x, obs.y,
                                obs.nowMs);
        ResetTradeState();
        return false;
    }

    // --- listen ------------------------------------------------------------
    //
    // Done BEFORE announcing, so a character that can answer somebody else's
    // offer does that rather than adding its own to the noise.
    std::vector<Client::Heard> heard;
    client.JournalHeardSince(tradeHeardMs_, heard);
    if (!heard.empty()) tradeHeardMs_ = heard.back().timeMs;

    for (const Client::Heard& h : heard) {
        // Somebody answered OUR offer.
        std::string wanted;
        if (!tradeOffer_.item.empty() &&
            market::ParseBuyReply(h.text, &wanted) && wanted == tradeOffer_.item) {
            LogLine("trade: %s wants our %s", h.name.c_str(), wanted.c_str());
            tradePartner_ = h.speaker;
            tradePartnerName_ = h.name;
            tradeItem_ = tradeOffer_.item;
            tradeSellingQty_ = tradeOffer_.qty;
            return false;   // next tick walks over and opens the window
        }
        // Somebody is selling something we need.
        market::TradeIntent offer;
        if (!market::ParseSellOffer(h.text, &offer)) continue;
        const market::BuyDecision d =
            market::ConsiderOffer(*me, obs.pack, obs.gold, tradePolicy_, offer);
        LogLine("trade: heard '%s' from %s -> %s (%s)", h.text.c_str(),
                h.name.c_str(), d.accept ? "want it" : "no", d.reason);
        if (!d.accept) continue;
        // Say so out loud. The seller is listening for exactly this, and
        // saying it is also what makes the deal visible to a human watching.
        client.ActionSay(market::FormatBuyReply(offer.item).c_str());
        tradePartner_ = h.speaker;
        tradePartnerName_ = h.name;
        tradeItem_ = offer.item;
        tradeSellingQty_ = 0;            // we are the BUYER
        tradeWantQty_ = d.qty;
        tradeOfferPrice_ = offer.pricePerUnit;
        return false;
    }

    // --- a partner is named: go and open the window -------------------------
    if (tradePartner_ != 0) {
        if (client.TravelBusy()) return false;
        i32 px = 0, py = 0; i8 pz = 0;
        if (!client.MobilePosition(tradePartner_, &px, &py, &pz)) {
            LogLine("trade: lost sight of %s", tradePartnerName_.c_str());
            ResetTradeState();
            return false;
        }
        if (TileDist(obs.x, obs.y, px, py) > 2) {
            if (!travelInFlight_) {
                travelInFlight_ = client.TravelToEntity(tradePartner_, 1);
                nextActionMs_ = obs.nowMs + 1500;
            } else {
                travelInFlight_ = false;
            }
            return false;
        }
        // Only the SELLER opens the window, so both sides do not race to open
        // one and cancel each other. Sphere opens a trade by dropping an item
        // on the partner, which the buyer has nothing to do with yet.
        if (tradeSellingQty_ > 0) {
            const std::vector<u16> gfx = econ::GraphicsForItem(tradeItem_.c_str());
            u32 serial = 0;
            for (u16 g : gfx) {
                serial = client.FindBackpackItemByGraphic(g);
                if (serial) break;
            }
            if (!serial) {
                LogLine("trade: no %s in the pack after all", tradeItem_.c_str());
                ResetTradeState();
                return false;
            }
            tradePackBefore_ = market::QtyOf(obs.pack, tradeItem_);
            tradeGoldBefore_ = obs.gold;
            LogLine("trade: opening a window with %s for %d %s",
                    tradePartnerName_.c_str(), tradeSellingQty_,
                    tradeItem_.c_str());
            client.ActionTradeStart(tradePartner_, serial);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        // Buyer: stand there and wait for the seller to open it.
        tradePackBefore_ = market::QtyOf(obs.pack, tradeItem_);
        tradeGoldBefore_ = obs.gold;
        if (obs.nowMs - tradeAnnouncedMs_ > 20000) {
            LogLine("trade: %s never opened a window", tradePartnerName_.c_str());
            ResetTradeState();
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- collect the stock before selling it --------------------------------
    //
    // Everything this character ever gathered is in the bank, because banking
    // is what it does when a pack fills. Announcing goods that are in a box on
    // the other side of town is an offer it cannot honour, so the withdrawal
    // is part of the errand.
    if (obs.atBank) {
        market::TradeIntent want;
        std::vector<market::Stock> holdings = obs.pack;
        for (const market::Stock& b : obs.bank) {
            bool merged = false;
            for (market::Stock& h : holdings) {
                if (h.item == b.item) { h.qty += b.qty; merged = true; break; }
            }
            if (!merged) holdings.push_back(b);
        }
        if (market::ChooseSellOffer(*me, holdings, state_.prices, tradePolicy_,
                                    &want)) {
            const i32 inPack = market::QtyOf(obs.pack, want.item);
            const i32 inBank = market::QtyOf(obs.bank, want.item);
            if (inPack < want.qty && inBank > 0) {
                const std::vector<u16> gfx = econ::GraphicsForItem(want.item.c_str());
                const u32 serial = client.FindContainerItemByGraphic(
                    client.BankContainer(), gfx.data(), gfx.size());
                if (serial) {
                    const i32 take = std::min(inBank, want.qty - inPack);
                    LogLine("trade: withdrawing %d %s from the bank to sell",
                            take, want.item.c_str());
                    client.TakeFromContainer(serial, static_cast<u16>(take));
                    nextActionMs_ = obs.nowMs + 2000;
                    return false;
                }
            }
        }
    }

    // --- nothing heard: stand where players are and announce ----------------
    market::TradeIntent offer;
    if (!market::ChooseSellOffer(*me, obs.pack, state_.prices, tradePolicy_,
                                 &offer)) {
        // Nothing worth announcing -- most often because this character has
        // never seen a price for what it carries and refuses to invent one.
        // SAME DEAD END, SAME COOLDOWN. Returning plain success here let the
        // need score identically on the very next tick and the goal was
        // re-picked sixteen times a second -- a lumberjack logged
        // goal=TRADE_WITH_PLAYER eight times in half a second and did nothing
        // else all session. An errand that cannot even be started is the
        // market being unavailable, not a goal that succeeded.
        LogLine("trade: nothing to announce (no observed price for what is spare)");
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        return true;
    }
    tradeOffer_ = offer;

    // A market needs a marketplace. The bank is where players actually stand,
    // and going there is also what makes the fleet visibly congregate instead
    // of shouting into an empty forest.
    if (!obs.atBank && client.BankContainer() == 0) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            // BOUND THE TRIPS. This walk was unbounded, and the flag below
            // clears on the very next tick, so a character that never arrives
            // re-issues the journey every two seconds until the goal's 300s
            // limit kills it -- and is then handed the same errand again.
            // Brannoc logged "taking 30 i_ingot_iron to the Vesper market" 145
            // times in one session and reached no market, while training,
            // eating and crafting all waited their turn behind it. Every other
            // travelling goal already counts its trips; this one did not.
            if (++tradeTrips_ > kMaxTradeTrips) {
                LogLine("goal_failed=TRADE_WITH_PLAYER reason=\"no market "
                        "reached after %d trips\"", tradeTrips_ - 1);
                planner_.Cooldown(GoalKind::TradeWithPlayer,
                                  obs.nowMs + kMarketQuietMs);
                planner_.Finish(false, "no market reachable", obs.nowMs);
                tradeTrips_ = 0;
                nextActionMs_ = obs.nowMs + 5000;
                return false;
            }
            LogLine("trade: taking %d %s to the %s market (trip %d)",
                    offer.qty, offer.item.c_str(),
                    state_.homeCity.empty() ? "nearest" : state_.homeCity.c_str(),
                    tradeTrips_);
            travelInFlight_ =
                client.TravelToService(wm::Service::Banker, nullptr);
            if (!travelInFlight_) {
                LogLine("goal_blocked=TRADE_WITH_PLAYER reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        return false;
    }

    // NOBODY TO SELL TO IS NOT A REASON TO SHOUT. "dont try to sell with WTS
    // if no one around" (project owner, 2026-08-29).
    //
    // The offer is a SPOKEN one -- it only works if a player hears it -- so
    // announcing to an empty bank is noise, and it is noise the character
    // repeats on a timer while a real errand waits. A player checks who is
    // there first.
    //
    // Other BOTS count: they are the market. NPCs do not, which is why this
    // asks for players rather than for mobiles.
    if (client.PlayersNearby(kTradeEarshot) == 0) {
        LogLine("trade: nobody within %d tiles to hear an offer -- not "
                "shouting into an empty room", kTradeEarshot);
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
        planner_.Finish(false, "no audience", obs.nowMs);
        return false;
    }

    // AND NOT AT THE SAME PEOPLE WHO ALREADY IGNORED IT. The earshot test is
    // working correctly -- the "players" standing in the Minoc bank are the
    // owner's own observer characters, "Observer, Apprentice Archer" and "The
    // Eminent Owner Observer", which are real player bodies with no " the " in
    // their titles. They are an audience that never buys, so a character kept
    // shouting WTS at them and Jarvinia answered "Um... um?" each time.
    //
    // A player asks once and waits. Announcing again is only sensible when
    // somebody NEW is in the room.
    const u32 audience = client.AudienceFingerprint(kTradeEarshot);
    if (audience == tradeAudienceIgnored_) {
        LogLine("trade: the same people who ignored the last offer are still "
                "here -- not repeating it");
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
        planner_.Finish(false, "audience already declined", obs.nowMs);
        return false;
    }

    if (obs.nowMs - tradeAnnouncedMs_ >= kAnnounceIntervalMs) {
        const std::string line = market::FormatSellOffer(offer);
        LogLine("trade: announcing '%s'", line.c_str());
        client.ActionSay(line.c_str());
        tradeAnnouncedMs_ = obs.nowMs;
        ++tradeAnnounceCount_;
    }
    if (tradeAnnounceCount_ >= kMaxAnnounces) {
        LogLine("trade: nobody answered %d offers of %s -- back to work",
                tradeAnnounceCount_, offer.item.c_str());
        tradeAudienceIgnored_ = client.AudienceFingerprint(kTradeEarshot);
        state_.memory.NoteEvent("no_player_buyer", offer.item.c_str(), "",
                                obs.x, obs.y, obs.nowMs);
        tradeAnnounceCount_ = 0;
        // AND STOP SCHEDULING IT for a while. Finishing the goal was not
        // enough: the need scored the same on the very next tick, the errand
        // was re-picked, and a lumberjack spent whole sessions announcing logs
        // to an empty Yew while its own training and hunting needs -- which it
        // could actually have finished -- sat underneath it.
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        planner_.Finish(false, "nobody wanted it", obs.nowMs);
        return false;
    }
    nextActionMs_ = obs.nowMs + 2000;
    return false;
}

// Put the goods (or the gold) in the window, then accept. Kept separate
// because it is the half that runs on BOTH sides of the same deal.
bool Runner::DriveOpenTrade(Client& client, const Observation& obs) {
    const trade::TradeState& tr = client.Trade();

    if (!tradeOffered_) {
        if (tradeSellingQty_ > 0) {
            const std::vector<u16> gfx = econ::GraphicsForItem(tradeItem_.c_str());
            for (u16 g : gfx) {
                const u32 serial = client.FindBackpackItemByGraphic(g);
                if (!serial) continue;
                client.ActionTradeOffer(serial,
                                        static_cast<u16>(tradeSellingQty_));
                break;
            }
        } else {
            const i32 owed = tradeWantQty_ * tradeOfferPrice_;
            const u32 gold = client.FindBackpackItemByGraphic(kGoldCoin);
            if (gold && owed > 0) {
                client.ActionTradeOffer(gold, static_cast<u16>(owed));
                LogLine("trade: offering %d gold for %d %s", owed,
                        tradeWantQty_, tradeItem_.c_str());
            }
        }
        tradeOffered_ = true;
        tradeOpenedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Accept once the partner has put something in. Accepting an EMPTY window
    // is how a character gives its goods away for nothing.
    if (tr.CurrentPhase() == trade::Phase::Open && !tr.TheirOffer().empty() &&
        !tr.CheckSent()) {
        LogLine("trade: partner offered %zu line(s); accepting",
                tr.TheirOffer().size());
        client.ActionTradeAccept(true);
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    if (obs.nowMs - tradeOpenedMs_ > kTradeGiveUpMs) {
        LogLine("trade: %s put nothing in after %llds -- cancelling",
                tradePartnerName_.c_str(),
                static_cast<long long>(kTradeGiveUpMs / 1000));
        client.ActionTradeCancel();
        state_.memory.NoteEvent("trade_timeout", tradeItem_.c_str(),
                                tradePartnerName_.c_str(), obs.x, obs.y,
                                obs.nowMs);
        ResetTradeState();
        return false;
    }
    nextActionMs_ = obs.nowMs + 1000;
    return false;
}

void Runner::ResetTradeState() {
    tradePartner_ = 0;
    tradePartnerName_.clear();
    tradeItem_.clear();
    tradeOffer_ = market::TradeIntent{};
    tradeSellingQty_ = 0;
    tradeWantQty_ = 0;
    tradeOfferPrice_ = 0;
    tradeOffered_ = false;
    tradePackBefore_ = 0;
    tradeGoldBefore_ = 0;
    tradeAnnounceCount_ = 0;
    travelInFlight_ = false;
}

// ---------------------------------------------------------------------------
// FISH -- the one gold faucet a character can reach on day one.
//
// Every number here is the shard's own, read out of
// runtime/scripts/skills/skill18_fishing.scp rather than assumed:
//
//   DELAY=8.0     one cast takes eight seconds
//   RANGE=4       water four tiles away is reachable
//   FLAGS=skf_gather
//   @PreStart     refuses outright while mounted
//
// The answers Sphere gives are equally its own, and they are the only honest
// signal that a cast resolved:
//
//   "You fish a while, but fail to catch anything."
//   "You pull your line back in and stop fishing."
//
// The same lesson the axe taught applies: a flat sleep after every cast spends
// most of the loop waiting for an answer the server already gave, so the eight
// seconds is a CEILING and the goal polls for a verdict.
// ---------------------------------------------------------------------------
// Every kind of fish the sea yields, counted as one catch. The graphic table
// listed only i_fish_big_1 for a long while, so a character could pull a fish
// out of the water and its own pack counter would report nothing -- the catch
// was real, the blindness was ours (VendorPolicy.cpp kGraphics).
static i32 FishInPack(const std::vector<market::Stock>& pack) {
    static const char* kKinds[] = {"i_fish_big_1", "i_fish_big_2",
                                   "i_fish_big_3", "i_fish_big_4",
                                   "i_fish_small"};
    i32 n = 0;
    for (const char* k : kKinds) n += market::QtyOf(pack, k);
    return n;
}

// ---------------------------------------------------------------------------
// CRAFTING -- the half of the economy a gatherer never needed.
//
// A crafter's day is two errands. BUY_SUPPLIES fetches what it cannot make;
// CRAFT makes what it can sell. They are separate goals because they fail
// differently, and "I cannot craft" when the truth is "nobody has sold me
// nightshade yet" is a bot lying about its own state.
// ---------------------------------------------------------------------------

// Who sells a craft input, as the paperdoll names them. Read off this shard's
// own vendor templates, never guessed: the mage shop carries both halves of
// the Inscription chain -- SELL=i_scroll_blank,{10 15} and every Magery
// reagent (templates/tm_vend.scp:633-656) -- which is why a scribe's whole
// shopping trip is one stop.
const char* SupplierTradeFor(const std::string& item) {
    if (item.rfind("i_reag_", 0) == 0) return "mage";
    if (item == "i_scroll_blank")      return "mage";
    if (item == "i_bottle_empty")      return "alchemist";
    if (item == "i_feather")           return "provisioner";
    // KINDLING, which is what a campfire is made of and therefore what
    // cooking needs. Marla caught fish, cut them into steaks and then SOLD
    // the steaks raw at 2 gold because she could not cook: NeedCraft never
    // appeared in her list at all, since the recipe wanted a fire and she had
    // nothing to light. Cooked steaks are worth 6 (i_fish_cut_cooked
    // VALUE=6), so the missing gap was threefold value on every fish.
    //
    // The provisioner stocks it -- her own vendor window showed "kindling
    // gfx=0x0DE1 qty=36 price=1" while she stood there buying bread.
    if (item == "i_kindling")          return "provisioner";
    return nullptr;
}

// How to reach one output through the shard's legacy craft menus. Two levels
// at most, and both strings are matched as case-insensitive substrings.
//
// Inscription is nested -- the blank scroll opens "Spell Circles" and the
// spell lives one level down (sm_legacy_inscription.scp:12-31, 93-118).
// Bowcraft is flat, its options named "<name> (<resmake>)"
// (sm_legacy_bowcraft.scp:13-33). Nothing here is inferred from generic UO.
struct CraftMenuPath {
    const char* item;
    const char* step1;
    const char* step2;   // nullptr for a flat menu
    const char* step3;   // blacksmithing nests one level deeper than the rest
};
const CraftMenuPath kCraftMenus[] = {
    {"i_scroll_poison",      "Spell Circle 3", "poison",  nullptr},
    {"i_scroll_recall",      "Spell Circle 4", "recall",  nullptr},
    {"i_bow",                "bow",            nullptr,   nullptr},
    {"i_crossbow",           "crossbow",       nullptr,   nullptr},
    {"i_arrow_shaft",        "arrow_shaft",    nullptr,   nullptr},
    // BLACKSMITHING. Corwyn reached 58 ingots and then stopped dead on
    // "no menu path known for i_dagger" -- the table had no smith entry at
    // all, so the whole mine -> smelt -> smith -> sell chain ended one step
    // from the end.
    //
    // Three levels, from sm_legacy_blacksmithing.scp:
    //   ON=i_sword_viking Weapons          -> ON=i_sword_viking Swords & Blades
    //   -> ON=i_dagger <name> (<resmake>)
    // where <name> is the itemdef's NAME. If this shard serves the newer
    // def_blacksmithing gump instead, its categories are clilocs ("Bladed",
    // 1011081) -- the failure branch below prints what the menu ACTUALLY
    // offered, which is how to settle it without guessing twice.
    {"i_dagger",             "Weapons",        "Swords & Blades", "dagger"},
    // ALCHEMY IS A FLAT MENU -- sm_legacy_alchemy.scp has no categories, just
    // "ON=i_potion_Poison <name> (<resmake>)" straight off the mortar. But the
    // names are suffixes of one another and the menu lists them in this order:
    //     Lesser Poison / Poison / Greater Poison / Deadly Poison
    // so a plain substring search for "poison" finds LESSER poison first and
    // quietly brews the wrong thing. A leading '^' means match the START of
    // the option instead, which only the plain "Poison" satisfies.
    {"i_potion_poisonless",  "^Lesser Poison", nullptr,   nullptr},
    {"i_potion_poison",      "^Poison",        nullptr,   nullptr},
    {"i_potion_poisongreat", "^Greater Poison",nullptr,   nullptr},
    {"i_potion_poisondeadly","^Deadly Poison", nullptr,   nullptr},
    {"i_spear_short",        "Weapons",        "Spears and Forks", "short spear"},
    // COOKING. Two levels, from sm_legacy_cooking.scp (this shard runs the
    // legacy menu: crafting_settings.scp has scp.NewCrafting_Cooking=0):
    //   ON=i_ribs_cooked Barbecue -> ON=i_fish_cut_cooked <name> (<resmake>)
    // where <name> resolves off tiledata for 0x097B, "fish steak" -- the
    // itemdef carries no NAME= of its own. Matching is case-insensitive
    // substring, and no other Barbecue entry contains it.
    {"i_fish_cut_cooked",    "Barbecue",       "fish steak",      nullptr},
};

const CraftMenuPath* CraftMenuFor(const std::string& item) {
    for (const CraftMenuPath& m : kCraftMenus) {
        if (item == m.item) return &m;
    }
    return nullptr;
}

bool Runner::DoBuySupplies(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    // SETTLE THE PREVIOUS ASK FIRST, from the gold the server actually took.
    // The ledger is the economy's own books; it must record purchases that
    // HAPPENED. Noting the flow at request time counted one on every
    // superseded retry against a vendor that had walked out of reach.
    if (!pendingBuyItem_.empty() && !client.ActionBusy()) {
        const i32 spent = pendingBuyGoldBefore_ - obs.gold;
        if (spent > 0) {
            state_.ledger.Note(market::GoldFlow::DestroyedVendorPurchase, spent,
                               pendingBuyItem_.c_str(), obs.nowMs);
            LogLine("supplies: the server took %d gold for %s (purse %d -> %d)",
                    spent, pendingBuyItem_.c_str(), pendingBuyGoldBefore_,
                    obs.gold);
            planner_.NoteProgress();   // THIS is progress: goods changed hands
        } else {
            LogLine("supplies: asked to buy %s and the purse did not move -- "
                    "nothing was bought", pendingBuyItem_.c_str());
        }
        pendingBuyItem_.clear();
        pendingBuyGoldBefore_ = 0;
    }

    const CraftIntent intent = ChooseCraft(*me, obs, needCfg_.craftBatch);
    if (!intent.item || intent.missing.empty()) {
        LogLine("supplies: nothing short after all");
        supplyItem_.clear();
        return true;
    }

    const prod::Ingredient want = intent.missing.front();
    if (supplyItem_ != want.item) {
        supplyItem_ = want.item;
        supplyTrips_ = 0;
        const char* trade = SupplierTradeFor(supplyItem_);
        supplyTrade_ = trade ? trade : "";
        // The service the trade word maps to, so a shopkeeper wearing a
        // different title for the same job is still recognised.
        supplyService_ = ServiceForTrade(supplyTrade_.c_str());
    }

    if (supplyTrade_.empty()) {
        LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" item=%s",
                faucet::RefusalName(faucet::Refusal::NoKnownBuyer),
                supplyItem_.c_str());
        planner_.Finish(false, "no trade known to sell it", obs.nowMs);
        return false;
    }

    // THE POLICY DECIDES, not the shop. An NPC that technically stocks a thing
    // is not thereby a legitimate source for it -- that is the whole point of
    // the vendor matrix, and buying a player-market good from a vendor would
    // cut a real player out of the economy this project exists to simulate.
    const econ::VendorRuling ruling = econ::CanUseNPCVendorFor(supplyItem_.c_str());
    if (!ruling.allowed) {
        LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" item=%s class=%s (%s)",
                faucet::RefusalName(faucet::Refusal::RevolutionAuthenticityUnknown),
                supplyItem_.c_str(), econ::VendorClassName(ruling.klass),
                ruling.reason ? ruling.reason : "");
        state_.memory.NoteEvent("policy_refused", supplyItem_.c_str(),
                                econ::VendorClassName(ruling.klass), obs.x,
                                obs.y, obs.nowMs);
        planner_.Finish(false, "the vendor policy refuses this input", obs.nowMs);
        return false;
    }

    if (client.TravelBusy()) return false;

    const u32 vendor = client.VendorOfferFrom();
    if (vendor == 0) {
        const u32 keeper = client.NearestShopkeeperWithTrade(supplyTrade_.c_str(),
                                                             supplyService_);
        if (keeper) {
            i32 vx = 0, vy = 0; i8 vz = 0;
            if (client.MobilePosition(keeper, &vx, &vy, &vz)) {
                const i32 d = TileDist(obs.x, obs.y, vx, vy);
                const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
                if (d > 1 || dz > 3) {
                    travelInFlight_ = client.TravelToEntity(keeper, 1);
                    nextActionMs_ = obs.nowMs + 2000;
                    return false;
                }
            }
            client.ActionVendorOpen(keeper);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        if (!travelInFlight_) {
            if (++supplyTrips_ > kMaxSupplyTrips) {
                LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" no '%s' found "
                        "after %d trips",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        supplyTrade_.c_str(), supplyTrips_);
                planner_.Finish(false, "no supplier reachable", obs.nowMs);
                supplyTrips_ = 0;
                nextActionMs_ = obs.nowMs + 30000;
                return false;
            }
            LogLine("supplies: looking for a '%s' to sell %d %s (trip %d)",
                    supplyTrade_.c_str(), want.qty, supplyItem_.c_str(),
                    supplyTrips_);
            travelInFlight_ = client.TravelToService(
                ServiceForTrade(supplyTrade_.c_str()), HomeOrNearest(state_.homeCity));
            if (!travelInFlight_) {
                LogLine("goal_blocked=BUY_SUPPLIES reason=\"%s\" (%s)",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        LogLine("supplies: arrived at %d,%d -- asking who is here",
                client.PlayerX(), client.PlayerY());
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // AN OPEN SHOP WINDOW IS NOT A VENDOR STILL IN REACH.
    //
    // The approach check above only runs while no offer is open, so once the
    // window came up the character stopped watching where the shopkeeper went
    // -- and Britain's shopkeepers walk. In run_m5/p0gate1 Ysolde bought once,
    // Nightshade wandered off, and every buy after that was answered "You
    // can't reach the Vendor" while the goal re-issued it every 2.5 seconds
    // for the rest of the session. Re-measure before each purchase and walk
    // back if the shop has moved.
    {
        i32 vx = 0, vy = 0; i8 vz = 0;
        if (client.MobilePosition(vendor, &vx, &vy, &vz)) {
            const i32 d = TileDist(obs.x, obs.y, vx, vy);
            const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
            if ((d > kVendorReach || dz > 3) &&
                ++vendorChases_ <= kMaxVendorChases) {
                LogLine("supplies: the '%s' has moved to %d,%d (%d tiles) -- "
                        "walking back before buying (chase %d of %d)",
                        supplyTrade_.c_str(), vx, vy, d, vendorChases_,
                        kMaxVendorChases);
                travelInFlight_ = client.TravelToEntity(vendor, 1);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            if (d > kVendorReach && vendorChases_ > kMaxVendorChases) {
                // Chasing has failed; ask anyway and let Sphere answer. A
                // refusal is information and ends the goal honestly, where
                // another lap ends nothing.
                LogLine("supplies: the '%s' keeps moving (%d tiles after %d "
                        "chases) -- asking from here and letting the server "
                        "decide", supplyTrade_.c_str(), d, kMaxVendorChases);
            }
        }
    }

    // ONE BUY IN FLIGHT AT A TIME. kVendorTimeoutMs is 8 s and this used to
    // re-issue every 2.5 s, so each attempt was superseded before it could
    // resolve -- the identical defect the bank ask had.
    if (client.ActionBusy()) return false;

    // A shop window is open: find the input in it and buy the shortfall.
    //
    // WHOSE window, though. An offer outlives the goal that opened it, so this
    // loop used to search whatever shop was last visited. Ysolde walked from a
    // baker to a mage and searched the BAKER's window for blank scrolls,
    // failing 746 times with "this 'mage' does not stock i_scroll_blank" while
    // standing in a mage shop that sells them.
    if (!OfferBelongsTo(client, vendor)) {
        LogLine("supplies: the open shop window is not this vendor's -- "
                "asking '%s' for their own list", supplyTrade_.c_str());
        client.ActionVendorOpen(vendor);
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }
    const std::vector<u16> gfx = econ::GraphicsForItem(supplyItem_.c_str());
    for (const Client::VendorItem& v : client.VendorOffer()) {
        bool match = false;
        for (u16 g : gfx) { if (v.graphic == g) { match = true; break; } }
        if (!match) continue;

        const i32 unit = static_cast<i32>(v.price);
        i32 take = want.qty;
        // AND NEVER MORE THAN THE SHELF HOLDS. Sphere refuses the WHOLE order
        // when the quantity exceeds stock, so one over-ask buys nothing at
        // all rather than buying what is there -- the defect that stalled the
        // bandage errand (a healer with 19, asked for 20). Same guard here,
        // where it had not bitten yet only because the batch sizes are small.
        if (v.amount > 0 && take > static_cast<i32>(v.amount))
            take = static_cast<i32>(v.amount);
        // WORKING CAPITAL IS NOT THE DEATH RESERVE.
        //
        // goldReserve is what a life keeps back to replace a tool after it
        // dies. Charging the working stock against it deadlocked a scribe
        // outright: blank scrolls cost 3 gold, the purse held 781, the
        // reserve was 900, and the character stood in the mage shop
        // refusing to buy three scrolls -- every fifteen seconds, for the
        // whole session. A reserve that forbids the only activity which
        // refills it is not caution, it is a trap.
        //
        // So inputs are bought out of working capital: everything above a
        // small hard floor, and never more than a quarter of the purse in one
        // trip, so a bad price cannot empty a character either.
        constexpr i32 kHardFloor = 100;      // never end a trip broke
        const i32 above = obs.gold - kHardFloor;

        // BUY THE BATCH IN ONE GO. The quarter-purse cap was applied on every
        // pass, so a life bought three bottles, then two, then one, shrinking
        // as its own purse shrank -- three vendor trips for one errand:
        //   the server took 36 gold for i_bottle_empty (purse 180 -> 144)
        //   the server took 24 gold for i_bottle_empty (purse 144 -> 120)
        //   the server took 12 gold for i_bottle_empty (purse 120 -> 108)
        // "buying should be bulk as well not one buy one" (project owner,
        // 2026-08-30).
        //
        // The quarter rule exists so an UNKNOWN price cannot empty a purse in
        // one go, and that reasoning only holds while the price is unknown.
        // Here the vendor has already quoted `unit`, so the exposure is known
        // exactly -- the floor is the protection that matters, and it still
        // stands. With a quoted price a life may spend everything above it.
        const bool priceIsKnown = unit > 0;
        const i32 spendable =
            priceIsKnown ? above
                         : ((above < obs.gold / 4) ? above : obs.gold / 4);
        if (unit > 0 && take * unit > spendable) take = spendable / unit;
        if (take <= 0) {
            // SAY WHAT IS BEING WAITED FOR, AND STAND DOWN SO IT CAN HAPPEN.
            //
            // This used to log "blocked" and retry every fifteen seconds
            // forever without ever finishing, so the planner was never asked
            // again -- and the thing that would have unblocked it was sitting
            // in the same pack. Voris stood outside the alchemist with 108
            // gold, unable to afford a 12 gold bottle (the floor of 100 plus
            // the quarter-purse rule leaves 8 spendable), while carrying five
            // poison potions worth about a hundred. "it should see what is it
            // waiting?" (project owner, 2026-08-30).
            //
            // What it is waiting for is GOLD, so name that, and finish so
            // EARN_GOLD gets a turn. If there is genuinely nothing to sell the
            // planner will come back here and the cooldown paces the retry.
            const std::vector<market::Offer> couldSell =
                needCfg_.profession
                    ? market::Surplus(*needCfg_.profession, obs.pack,
                                      market::PolicyForPurse(obs.goldOnHand))
                    : std::vector<market::Offer>{};
            i32 sellable = 0;
            for (const market::Offer& o : couldSell) sellable += o.qty;
            LogLine("supplies: %s costs %d each and only %d of %d gold is "
                    "spendable -- waiting on GOLD, and there %s %d thing(s) "
                    "in the pack to sell; standing down so that can happen",
                    supplyItem_.c_str(), unit, spendable, obs.gold,
                    sellable == 1 ? "is" : "are", sellable);
            planner_.NoteAttempt(obs.nowMs);
            planner_.Cooldown(GoalKind::BuySupplies, obs.nowMs + 45000);
            planner_.Finish(false, "cannot afford the inputs yet", obs.nowMs);
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        }

        // COIN IN THE PACK, NOT IN THE BOX. obs.gold is the status-bar total
        // and counts the bank on this shard, so "spendable" above can be a
        // comfortable number while the purse is empty -- and the vendor is the
        // one who notices:
        //   supplies: buying 5 i_scroll_blank at 6 each from 'blank scrolls'
        //   Shunnar: Begging thy pardon, but thou canst not afford that.
        //   supplies: asked to buy i_scroll_blank and the purse did not move
        // Ysolde repeated that 42 times in one run holding 409 gold, all of it
        // banked. GET_TOOL and the trainer fee already fetch their coin first;
        // this path was simply never wired to do the same.
        if (FetchCoinForPurchase(client, obs, take * unit)) return false;

        market::PriceObservation po;
        po.item = supplyItem_;
        po.pricePerUnit = unit;
        po.source = market::PriceSource::NpcVendorSells;
        po.who = v.name;
        po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
        state_.prices.Note(po);

        LogLine("supplies: buying %d %s at %d each from '%s'", take,
                supplyItem_.c_str(), unit, v.name.c_str());
        client.ActionVendorBuy(vendor, v.serial, static_cast<u16>(take));
        // THE LEDGER RECORDS WHAT THE SERVER DID, not what we asked for.
        // Noting the flow here counted a purchase on every one of those
        // superseded retries, so the economy's own books recorded gold
        // destroyed that never left the purse. What is remembered instead is
        // the ASK; the flow is noted on the next tick, from the gold the
        // server actually took (see `pendingBuy_` below).
        pendingBuyItem_ = supplyItem_;
        pendingBuyGoldBefore_ = obs.gold;
        // BUYING IS AN ATTEMPT, NOT PROGRESS. NoteProgress() cleared the
        // failure ladder on every retry, so a goal that bought nothing for
        // twenty minutes never ran out of attempts.
        planner_.NoteAttempt(obs.nowMs);
        supplyItem_.clear();
        // Longer than kVendorTimeoutMs (8 s, Client.cpp): an ask re-issued
        // inside its own deadline supersedes itself and never resolves.
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }

    LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" this '%s' does not stock %s",
            faucet::RefusalName(faucet::Refusal::VendorNotObserved),
            supplyTrade_.c_str(), supplyItem_.c_str());
    state_.memory.NoteEvent("vendor_lacks", supplyItem_.c_str(),
                            supplyTrade_.c_str(), obs.x, obs.y, obs.nowMs);
    // STAND DOWN, do not spin. Finish(false) alone re-picks on the next tick,
    // and when a stale shop window made this branch reachable from anywhere it
    // failed sixteen times a SECOND (v_Marla, 23:57:44). The window bug is
    // fixed at the source -- 0x3B now clears the offer -- but a shop that
    // truly lacks the item deserves the same brake GET_TOOL has: the stock
    // will be no different two ticks from now.
    planner_.Cooldown(GoalKind::BuySupplies, obs.nowMs + 60000);
    planner_.Finish(false, "this vendor does not stock it", obs.nowMs);
    return false;
}

// ---------------------------------------------------------------------------
// SMELTING. The missing link in "mine smelt smith sell".
//
// "it didnt smelt iron ore" (project owner, 2026-08-29). Corwyn reached the
// Minoc mine, swung a pickaxe, filled his pack with Iron Ore -- and stopped
// there, because no goal in the life could turn ore into metal. Downstream
// everything then failed for the right reason and the wrong cause: EARN_GOLD
// refused to sell ore because a raw material is a player-market good, and
// CRAFT was short of the ingots that were sitting in his pack as ore.
// ---------------------------------------------------------------------------
// COIN IN HAND BEFORE A PURCHASE.
//
// "nobody carry gold on them unless they need to buy something -- always put
// additional items to bank, so they can get it when they need it" (project
// owner). The first half was implemented and the second half was not: a
// character banked everything and then stood in front of a shop with an empty
// purse. "even though you say here carry 1000 gp on him he is not carry 1000
// gp" -- the 1000 in that log is the THRESHOLD, not what is carried.
//
// Two symptoms, one cause: Olin quoted 196 gold for Arms Lore and the payment
// step answered "no gold stack found in the pack", and a smith hammer could
// not be bought for the same reason.
//
// Returns true when it has taken over the tick (walking to the bank, opening
// it, or lifting coin out); the caller should return false and try again.
bool Runner::FetchCoinForPurchase(Client& client, const Observation& obs,
                                  i32 needed) {
    if (needed <= 0) return false;
    if (obs.goldOnHand >= needed) { coinWanted_ = 0; return false; }
    // Nothing banked either -- poverty, not logistics. The caller's own
    // "cannot afford" path is the honest answer.
    if (obs.gold < needed) { coinWanted_ = 0; return false; }

    // DO NOT OPEN THE BANK HERE. An earlier version of this walked to the box
    // and shouted "bank" itself, and it was wrong in four separate ways at
    // once -- it tested obs.atBank (which means the box is already OPEN), it
    // passed banker serial 0, it asked before any paperdoll title had been
    // fetched, and it re-issued inside open_bank's own 3s deadline so every
    // attempt superseded the last. The visible result was a character standing
    // at the bank saying "bank" over and over. ("corwyn spamming bank")
    //
    // There is already a goal that opens the box properly, with a skip list
    // for bankers that do not answer: BANK. And there is already a withdrawal
    // that works -- EARN_GOLD lifts stock out of the box the same way. So this
    // only does the part neither of them does: name the sum wanted, so NeedBank
    // fires, and lift the coin once the box is open.
    // "we withdraw stuff from bank before -- why it is hard for this account"
    // (project owner, 2026-08-29). It was not hard; it was duplicated.
    coinWanted_ = needed;

    const u32 box = client.BankContainer();
    if (box) {
        static const u16 kCoin[] = {kGoldCoin};
        const u32 stack = client.FindContainerItemByGraphic(box, kCoin, 1);
        if (stack && !client.ActionBusy()) {
            const i32 want = (needed - obs.goldOnHand) + 200;
            LogLine("bank: withdrawing %d gold for a purchase (need %d, "
                    "carrying %d)", want, needed, obs.goldOnHand);
            client.ActionMoveItem(stack, static_cast<u16>(want),
                                  client.BackpackSerial());
            nextActionMs_ = obs.nowMs + 3000;
            return true;
        }
    }

    // Box shut: let the BANK goal have the tick. Reporting "not now" rather
    // than steering keeps one goal in charge of one errand.
    // AND STAND DOWN LONG ENOUGH FOR THE BANK TRIP TO HAPPEN. Finishing alone
    // was not enough: NeedTool scores 0.90 against NeedBank's 0.80, so the
    // buying goal won the very next tick, stood down again, and BANK never got
    // a turn -- "500 gold needed and 0 carried" every three seconds while the
    // character stood still.
    LogLine("%s: %d gold needed and %d carried -- standing down so the bank "
            "goal can fetch it",
            GoalKindName(planner_.Current().kind), needed, obs.goldOnHand);
    planner_.Cooldown(planner_.Current().kind, obs.nowMs + 45000);
    planner_.Finish(false, "needs coin from the bank", obs.nowMs);
    nextActionMs_ = obs.nowMs + 1000;
    return true;
}

bool Runner::DoSmelt(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    const u32 ore = FindAny(client, kIronOre, 4);
    if (!ore) {
        LogLine("smelt: no ore in the pack to melt");
        smeltStartedMs_ = 0;
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // DID THE LAST DOUBLE-CLICK LAND? The pack is the only honest witness.
    // Both outcomes are clilocs -- 1044270 on success, craft_smelt_fail on a
    // failed roll -- and 0xC1 is an explicit no-op in this client, so there is
    // nothing to read in the journal. Counting metal is the truth. This is the
    // same reasoning DoCraft states for inscription.
    const i32 metal = market::QtyOf(obs.pack, "i_ingot_iron");
    if (smeltStartedMs_ != 0 && metal > smeltIngotsBefore_) {
        LogLine("smelt: +%d ingots (%d in the pack)", metal - smeltIngotsBefore_,
                metal);
        planner_.NoteProgress();
        if (!state_.memory.HasEvent("first_smelt")) {
            state_.memory.NoteEvent("first_smelt", "i_ingot_iron", "", obs.x,
                                    obs.y, obs.nowMs);
        }
    }
    smeltIngotsBefore_ = metal;

    // The one smelt message that IS plain ASCII, and so the one the journal
    // can actually see: everything else on this path is a cliloc and 0xC1 is a
    // no-op here. If it appears, the character is not close enough -- say so
    // rather than swinging again from the same spot.
    if (smeltStartedMs_ != 0 &&
        client.JournalSaidSince("must be near a forge", smeltStartedMs_)) {
        // GIVE UP ON A FORGE THAT CANNOT BE STOOD NEXT TO. Some are behind a
        // counter or against a wall, so no walkable tile is ever adjacent --
        // the lone forge beside the Minoc armorer is a candidate. Rather than
        // swing at it forever, strike it off and let NearestForge offer the
        // next one; in Minoc that means The Forgery, which has six of them.
        if (++smeltRefusals_ >= 3) {
            LogLine("smelt: the forge at %d,%d refuses from every tile reached "
                    "-- looking for another", smeltForgeX_, smeltForgeY_);
            deadForges_.emplace_back(smeltForgeX_, smeltForgeY_);
            if (deadForges_.size() > 16) deadForges_.erase(deadForges_.begin());
            smeltRefusals_ = 0;
            travelInFlight_ = false;
        } else {
            LogLine("smelt: refused -- not close enough to the forge yet (%d)",
                    smeltRefusals_);
        }
    }

    // A FORGE WITHIN TWO TILES, or go and find one.
    //
    // FROM THE MAP, NOT FROM THE ITEM LIST. sphereworld.scp and
    // spherestatics.scp contain ZERO forges between them -- every forge here
    // is original UO map content in statics0.mul, so the server never sends
    // one as an item and the first version of this, which asked
    // FindWorldItemByGraphic, stood inside a smithy reporting "no forge within
    // 2 tiles" three times and gave up.
    // AS FAR AS THE SERVER SENDS ITEMS, then walk the last few tiles. There
    // is no point asking for more: a forge outside the item-send radius is not
    // in the item list at all, so a bigger number would only look thorough.
    //
    // This is usually enough on its own. A forge stands beside the Minoc
    // armorer at 2535,571, and another INSIDE the Minoc mine at 2561,501 --
    // four tiles from the rock Corwyn actually swings at -- so a miner very
    // often smelts without going anywhere.
    Client::TreeHit forgeTile;
    const bool sawForge =
        client.NearestForge(obs.x, obs.y, 20, &forgeTile, &deadForges_);
    const i32 forgeDist =
        sawForge ? TileDist(obs.x, obs.y, forgeTile.x, forgeTile.y) : 0;

    if (sawForge && forgeDist > kForgeReach) {
        if (client.TravelBusy()) return false;

        // A FORGE YOU CANNOT GET NEXT TO IS NOT A FORGE YOU CAN USE. Many
        // stand against a wall or behind a counter with no walkable tile
        // adjacent -- the lone one beside the Minoc armorer is exactly that.
        // Walking "to" it then succeeds at two tiles forever, and because the
        // character never gets close enough to CLICK, the refusal message that
        // would otherwise retire the forge never arrives. So count approaches
        // as well as refusals.
        if (forgeTile.x == smeltForgeX_ && forgeTile.y == smeltForgeY_) {
            if (++smeltApproaches_ >= 4) {
                LogLine("smelt: cannot get within %d tile of the forge at %d,%d "
                        "after %d tries -- looking for another",
                        kForgeReach, forgeTile.x, forgeTile.y, smeltApproaches_);
                deadForges_.emplace_back(forgeTile.x, forgeTile.y);
                if (deadForges_.size() > 16)
                    deadForges_.erase(deadForges_.begin());
                smeltApproaches_ = 0;
                travelInFlight_ = false;
                // The trip counter is what decides to walk somewhere new, and
                // the skip list above is what makes "somewhere new" mean a
                // different building rather than this one again.
                smeltTrips_ = 0;
                nextActionMs_ = obs.nowMs + 500;
                return false;
            }
        } else {
            smeltForgeX_ = forgeTile.x;
            smeltForgeY_ = forgeTile.y;
            smeltApproaches_ = 1;
        }

        // WALK TO A TILE BESIDE THE FORGE, NEVER TO THE FORGE ITSELF. A forge
        // is solid: asking the planner for its own tile made it search for the
        // best part of a second and then give up --
        //   "no path to (2468,557) avoiding 0 block(s) (search 976432.8us)"
        //   "start (2472,550,5) exits: open=8" -- not enclosed; unreachable.
        // Naming a real standing tile turns that into an ordinary short walk.
        i32 standX = 0, standY = 0;
        bool haveStand = false;
        static const int kdx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
        static const int kdy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
        for (int i = 0; i < 8 && !haveStand; ++i) {
            const i32 tx = forgeTile.x + kdx[i], ty = forgeTile.y + kdy[i];
            if (!client.TileIsWalkable(tx, ty, forgeTile.z)) continue;
            standX = tx; standY = ty; haveStand = true;
        }
        if (!haveStand) {
            // Nothing to stand on next to it -- that is the whole story for
            // the lone forge beside the Minoc armorer. No point approaching
            // four times to learn it.
            LogLine("smelt: no walkable tile beside the forge at %d,%d -- "
                    "striking it off", forgeTile.x, forgeTile.y);
            deadForges_.emplace_back(forgeTile.x, forgeTile.y);
            if (deadForges_.size() > 16) deadForges_.erase(deadForges_.begin());
            smeltApproaches_ = 0;
            travelInFlight_ = false;
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }

        LogLine("smelt: forge at %d,%d is %d tiles off -- standing at %d,%d",
                forgeTile.x, forgeTile.y, forgeDist, standX, standY);
        travelInFlight_ = client.TravelToPoint(standX, standY, 0, "forge");
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // DO NOT SWING WHILE STILL WALKING. The first version issued the
    // double-click in the same tick it started the last step, so the click
    // went out from two tiles away and the shard refused it -- four times in
    // a row, each one looking like a smelt that simply did nothing.
    if (client.TravelBusy()) return false;

    if (!sawForge) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            // Forges stand in smithies, and the atlas files smithies -- and
            // the weaponsmiths beside them -- under `blacksmith`.
            // ADVANCE TO THE NEXT SMITHY, do not keep arriving at the same
            // one. The atlas files two Minoc buildings as `blacksmith`:
            // minoc_armorer at 2533,572, whose single forge stands where no
            // adjacent tile can be reached, and minoc_blackshmith at 2471,564
            // inside The Forgery, which has SIX. Travel quite reasonably picks
            // the nearer, so without a skip list the character walks to the
            // armorer forever and never sees the real smithy.
            // "why dont you go minoc_blacksmith ... the real smithy"
            // (project owner, 2026-08-29, asked twice).
            LogLine("smelt: carrying %d ore with no forge in reach -- walking "
                    "to a smithy (%d already tried)",
                    market::QtyOf(obs.pack, "i_ore_iron"),
                    static_cast<int>(smeltSkipPlaces_.size()));
            travelInFlight_ = client.TravelToServiceSkipping(
                wm::Service::Blacksmith, HomeOrNearest(state_.homeCity), {},
                &smeltSkipPlaces_);
            if (!travelInFlight_) {
                LogLine("goal_blocked=SMELT reason=\"%s\" (%s)",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                planner_.Cooldown(GoalKind::Smelt, obs.nowMs + 60000);
                planner_.Finish(false, "no forge reachable", obs.nowMs);
                return false;
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        // Arrived, but the forge is not within the shard's two tiles. Say so
        // plainly rather than clicking into the void -- a smithy whose forge
        // cannot be stood next to is worth knowing about.
        travelInFlight_ = false;
        if (++smeltTrips_ >= 3) {
            LogLine("smelt: %d trips to a smithy and no forge within %d tiles "
                    "-- giving up for now", smeltTrips_, kForgeReach);
            smeltTrips_ = 0;
            planner_.Cooldown(GoalKind::Smelt, obs.nowMs + 120000);
            planner_.Finish(false, "arrived but no forge in reach", obs.nowMs);
            return false;
        }
        LogLine("smelt: at the smithy but no forge within %d tiles (trip %d)",
                kForgeReach, smeltTrips_);
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // A FORGE THAT WORKS ENDS THE SEARCH. Without this the skip list only
    // ever grew: once Minoc's smithies were on it, "the nearest blacksmith not
    // yet tried" became Vesper, and the character walked out of its own city
    // to smelt. ("why corwyn in vesper?")
    smeltTrips_ = 0;
    smeltSkipPlaces_.clear();
    travelInFlight_ = false;
    smeltForgeX_ = forgeTile.x;
    smeltForgeY_ = forgeTile.y;
    smeltApproaches_ = 0;

    // CLICK THE FORGE, THEN THE ORE -- not the ore on its own.
    //
    // "Also forge works I double clicked forge then ore" (project owner,
    // 2026-08-29), and the scripts agree. types_forge.scp:
    //     [TYPEDEF t_forge]
    //     ON=@DCLICK
    //        TARGETF f_craft_blacksmith_smelt_targ
    // so the forge arms a target cursor, and that cursor is then given the
    // ore. f_craft_blacksmith_smelt_targ (crafting_functions.scp) checks the
    // ore is in the pack, checks ISNEARTYPE t_forge 3, and for a t_ore target
    // delegates to the ore's own @dclick.
    //
    // The first version clicked the ore directly. That is the OTHER route --
    // type_ore.scp's @dclick -- and it is the one that answered "You must be
    // near a forge to smelt" from two tiles away, over and over.
    if (smeltCursorPending_) {
        if (client.TargetActive()) {
            LogLine("smelt: giving the forge's cursor the ore (%d ore, %d "
                    "ingots so far)", market::QtyOf(obs.pack, "i_ore_iron"),
                    metal);
            client.ActionTargetObject(ore);
            smeltCursorPending_ = false;
            smeltReachFails_ = 0;   // the forge answered; the spot is good
            smeltStartedMs_ = obs.nowMs;
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        // The cursor never came up. Do not sit waiting on it forever.
        if (obs.nowMs - smeltClickedMs_ > 6000) smeltCursorPending_ = false;
        return false;
    }

    // THE SERVER SAID NO, AND IT MEANT IT.
    //
    // "You can't reach that." is a definitive refusal, and this goal used to
    // discard it and click again: 311 identical "opening the forge at
    // 2561,501" lines in one session (run_m7/r1b_Corwyn.console.txt), the bot
    // standing at 2560,500 -- ONE DIAGONAL TILE away -- and every click
    // refused in under 20 ms. It is the third path this week to re-issue an
    // action the server had already answered, after the vendor open and the
    // vendor buy, which is what the shared interaction layer exists to end.
    //
    // A diagonal is not always reachable on this shard: a forge is a multi-
    // tile static and the tile the atlas names is not necessarily the one that
    // can be touched. So a refusal means MOVE, not repeat -- walk onto the
    // forge's own entity and try from there. After a few of those the forge
    // itself is the problem, not the standing spot, and the errand stands
    // down so a different forge (or a different goal) gets the turn.
    if (client.ActionKind() == act::Kind::UseObject &&
        client.ActionResult() == act::Result::Rejected &&
        client.CurrentAction().subject == client.LastForgeSerial()) {
        if (++smeltReachFails_ > kMaxSmeltReachFails) {
            LogLine("goal_failed=SMELT reason=\"the forge at %d,%d refused "
                    "every approach after %d tries\"",
                    forgeTile.x, forgeTile.y, smeltReachFails_ - 1);
            deadTargets_.emplace_back(forgeTile.x, forgeTile.y);
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            smeltReachFails_ = 0;
            planner_.Cooldown(GoalKind::Smelt, obs.nowMs + kGearCooldownMs);
            planner_.Finish(false, "the forge cannot be reached", obs.nowMs);
            return false;
        }
        LogLine("smelt: \"you can't reach that\" from %d,%d -- walking onto "
                "the forge itself (try %d of %d)", obs.x, obs.y,
                smeltReachFails_, kMaxSmeltReachFails);
        travelInFlight_ = client.TravelToEntity(client.LastForgeSerial(), 0);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    LogLine("smelt: opening the forge at %d,%d", forgeTile.x, forgeTile.y);
    client.ActionUseObject(client.LastForgeSerial());
    smeltCursorPending_ = true;
    smeltClickedMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 1500;
    return false;
}

bool Runner::DoCraft(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    const CraftIntent intent = ChooseCraft(*me, obs, 1);
    if (!intent.item) {
        LogLine("craft: nothing this life can make and sell (%s)", intent.why);
        return true;
    }
    if (!intent.missing.empty()) {
        // A BURNING FIRE IS KINDLING ALREADY SPENT. The production graph
        // charges the cooked steak one kindling because that is what a fire
        // costs -- but the server consumes kindling at LIGHTING time
        // (Use_Kindling turns the piece itself into the campfire), not per
        // steak. So a character whose last kindling is currently burning
        // three tiles away is not short of anything: refusing to cook beside
        // its own lit fire would send it shopping while the fire went out.
        const bool onlyKindling =
            intent.missing.size() == 1 &&
            std::strcmp(intent.missing.front().item, "i_kindling") == 0;
        const prod::Recipe* fireCheck = prod::FindRecipe(intent.item);
        const bool fireBurning =
            fireCheck && fireCheck->station == prod::Station::Fire &&
            client.FindWorldItemByGraphic(kCampfireGraphic, 3) != 0;
        if (!(onlyKindling && fireBurning)) {
            LogLine("goal_blocked=CRAFT reason=\"%s\" %s short of %d x %s",
                    faucet::RefusalName(faucet::Refusal::RequiredForProduction),
                    intent.item, intent.missing.front().qty,
                    intent.missing.front().item);
            planner_.Finish(false, "inputs are short", obs.nowMs);
            return false;
        }
    }

    // A FIRE IS A STATION YOU CARRY. "nessa needs to cut the whole fish with
    // dagger to have raw fish then cook it with kindling and camping skill"
    // (project owner, 2026-08-29).
    //
    // Cooking's source is t_cooking, and the way to have one on a shore is to
    // light kindling: double-clicking it runs Skill_UseQuick(SKILL_CAMPING)
    // and on success turns the kindling itself into ITEMID_CAMPFIRE 0x0DE3
    // (Source-X CCharUse.cpp:294-300). Unlike a forge, this is a station the
    // character makes on the spot -- which is exactly why a fisher can cook
    // where it fished.
    //
    // Note the raw steak cannot simply be double-clicked onto the fire:
    // Source-X answers a double-click on IT_FOOD_RAW by EATING it
    // (CCharUse.cpp:1860), so the cooking itself goes through the menu with a
    // fire in reach.
    if (const prod::Recipe* r = prod::FindRecipe(intent.item)) {
        if (r->station == prod::Station::Fire &&
            !client.FindWorldItemByGraphic(kCampfireGraphic, 3)) {
            if (client.ActionBusy()) return false;
            // STAND STILL FIRST. The goal can begin while a previous goal's
            // travel is still carrying the character -- the first live run
            // dropped one kindling at (646,822), walked five tiles on the
            // leftover leg to the Yew banker, found nothing "on the ground"
            // within reach, and dropped the second piece too. Both lay in the
            // street; the pack read empty; the craft blocked.
            if (client.TravelBusy()) return false;
            // ON THE GROUND FIRST. Use_Kindling opens with
            //   if ( !pKindling->IsTopLevel() ) -> DEFMSG_ITEMUSE_KINDLING_CONT
            // (Source-X CCharUse.cpp:288) -- kindling double-clicked in the
            // backpack is refused before Camping is even rolled. So the
            // gesture is two actions: drop a piece at the feet, then light
            // the piece on the ground. A failed Camping roll leaves it lying
            // there, which is why the ground is checked before the pack --
            // relight what is already down rather than dropping another.
            if (const u32 ground =
                    client.FindWorldItemByGraphic(kKindlingGraphic, 3)) {
                LogLine("craft: lighting the kindling on the ground for a "
                        "campfire to cook %s on", intent.item);
                client.ActionUseObject(ground);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 3500;
                return false;
            }
            const u32 kindling = client.FindBackpackItemByGraphic(kKindlingGraphic);
            if (!kindling) {
                LogLine("goal_blocked=CRAFT reason=\"%s\" %s needs a fire and "
                        "there is no kindling to light one",
                        faucet::RefusalName(faucet::Refusal::RequiredForProduction),
                        intent.item);
                planner_.Finish(false, "no kindling for a fire", obs.nowMs);
                return false;
            }
            LogLine("craft: putting one kindling on the ground -- "
                    "Use_Kindling refuses it inside a container");
            client.ActionDropGround(kindling, 1, obs.x, obs.y, obs.z);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
    }

    // A FORGE RECIPE NEEDS THE FORGE, AND THE HAMMER IN HAND.
    //
    // Production.cpp already recorded both, from the engine:
    //   CClientUse.cpp:1273 LayerFind(LAYER_HAND1)   -- tool EQUIPPED, not carried
    //   CClientUse.cpp:1282 IsItemTypeNear(IT_FORGE,3)
    // and nothing acted on either. So Corwyn stood wherever he happened to be,
    // double-clicked an ingot 14 times, and the menu never opened -- "craft:
    // making i_dagger -- using a i_ingot_iron to open the menu", once every
    // four seconds until the run ended. "double click smith hammer maybe"
    // (project owner, 2026-08-29), which is exactly what the engine wants.
    if (const prod::Recipe* fr = prod::FindRecipe(intent.item)) {
        if (fr->station == prod::Station::Forge) {
            Client::TreeHit forgeTile;
            // NOT `near` -- MSVC still defines that as a legacy keyword macro.
            const bool haveForge = client.NearestForge(obs.x, obs.y, 20,
                                                       &forgeTile, &deadForges_);
            const i32 d = haveForge
                              ? TileDist(obs.x, obs.y, forgeTile.x, forgeTile.y)
                              : 999;
            if (!haveForge || d > 2) {
                if (client.TravelBusy()) return false;
                LogLine("craft: %s needs a forge -- %s", intent.item,
                        haveForge ? "walking to the one in sight"
                                  : "going to find a smithy");
                if (haveForge) {
                    i32 sx = 0, sy = 0; bool ok = false;
                    static const int ddx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                    static const int ddy[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                    for (int i = 0; i < 8 && !ok; ++i) {
                        const i32 tx = forgeTile.x + ddx[i];
                        const i32 ty = forgeTile.y + ddy[i];
                        if (!client.TileIsWalkable(tx, ty, forgeTile.z)) continue;
                        sx = tx; sy = ty; ok = true;
                    }
                    if (ok) {
                        travelInFlight_ = client.TravelToPoint(sx, sy, 0, "forge");
                        nextActionMs_ = obs.nowMs + 2000;
                        return false;
                    }
                    deadForges_.emplace_back(forgeTile.x, forgeTile.y);
                }
                travelInFlight_ = client.TravelToServiceSkipping(
                    wm::Service::Blacksmith, HomeOrNearest(state_.homeCity), {},
                    &smeltSkipPlaces_);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        // THE TOOL MUST BE IN HAND1, not merely in the pack.
        if (fr->tool == prod::Tool::SmithHammer) {
            bool held = false;
            for (usize i = 0; i < 2; ++i) {
                if (client.EquippedGraphicAt(kLayerHand1) == kSmithHammerGfx[i]) {
                    held = true; break;
                }
            }
            if (!held) {
                // DO NOT RE-ISSUE INSIDE THE ACTION'S OWN DEADLINE. Without
                // this the equip superseded itself every two seconds --
                // "equip invalid_state took=2091ms superseded" -- forever.
                if (client.ActionBusy()) return false;
                const u32 inPack = FindAny(client, kSmithHammerGfx, 2);
                if (!inPack) {
                    // Tongs will not do here, however much the catalogue likes
                    // them: GET_TOOL has to fetch an actual hammer, and
                    // VENDOR_S_TINKER sells one.
                    LogLine("goal_blocked=CRAFT reason=\"%s\" no smith HAMMER "
                            "to open the forge menu with (tongs cannot be "
                            "wielded)",
                            faucet::RefusalName(faucet::Refusal::MissingTool));
                    planner_.Finish(false, "no smith hammer", obs.nowMs);
                    return false;
                }
                // EMPTY THE HAND FIRST. Naming the layer was still not
                // enough: a miner_smith carries a pickaxe AND tongs, mining
                // leaves the pickaxe wielded, and the server will not put a
                // second thing in an occupied hand -- it answered "You put the
                // tongs in your pack" to every attempt, at layer 0 and at
                // layer 1 alike. Mining re-equips its own pickaxe when it next
                // needs it, so putting it away here costs nothing.
                const u32 hand1 = client.EquippedAtLayer(kLayerHand1);
                const u32 hand2 = client.EquippedAtLayer(kLayerHand2);
                const u32 inTheWay = hand1 ? hand1 : hand2;
                if (inTheWay && inTheWay != inPack) {
                    LogLine("craft: putting away what is in hand to free "
                            "HAND1 for the smith tool");
                    client.ActionUnequip(inTheWay);
                    nextActionMs_ = obs.nowMs + 3000;
                    return false;
                }

                // NAME THE LAYER. Passing kLayerServerChooses (0) does not
                // equip anything. The engine looks in HAND1
                // (CClientUse.cpp:1273), so say HAND1.
                LogLine("craft: taking the smith tool into HAND1 -- the menu "
                        "reads LAYER_HAND1");
                client.ActionEquip(inPack, kLayerHand1);
                nextActionMs_ = obs.nowMs + 4000;
                return false;
            }
        }
    }

    if (craftItem_ != intent.item) {
        makeLastIssued_ = false;   // a different item needs its own first make
        craftItem_ = intent.item;
        craftHadBefore_ = market::QtyOf(obs.pack, craftItem_);
        craftMade_ = 0;
        craftMenuStep_ = 0;
    }

    // Did the last attempt land? The pack is the only honest witness -- Sphere
    // answers a failed inscription with "the scroll is ruined", which consumes
    // the input and produces nothing, so counting attempts would count wrong.
    const i32 now = market::QtyOf(obs.pack, craftItem_);
    if (now > craftHadBefore_) {
        craftMade_ += now - craftHadBefore_;
        craftHadBefore_ = now;
        craftMenuStep_ = 0;
        craftAwaitingMs_ = 0;          // the thing we were waiting for arrived
        planner_.NoteProgress();
        LogLine("craft: made one %s (%d this sitting)", craftItem_.c_str(),
                craftMade_);
        if (!state_.memory.HasEvent("first_craft")) {
            state_.memory.NoteEvent("first_craft", craftItem_.c_str(), "",
                                    obs.x, obs.y, obs.nowMs);
        }
        // KEEP GOING WHILE THE MATERIAL LASTS. "craft till you are out of iron
        // on your bag" (project owner, 2026-08-29). Stopping at craftBatch
        // left a smith standing at the forge with fifty-odd ingots still in
        // the pack, walking off to sell four daggers and coming back.
        //
        // The stock is the honest limit: when the inputs no longer cover one
        // more, ChooseCraft reports it missing and the goal ends on its own.
        // The batch still applies to trades whose material is bought rather
        // than dug -- a scribe should not spend its whole purse on scrolls.
        bool moreToUse = false;
        if (const prod::Recipe* rr = prod::FindRecipe(craftItem_.c_str())) {
            // Fire recipes carry on for the same reason forge ones do: the
            // stock in the pack is the honest limit. Kindling is special --
            // the server spends it at lighting time, so while the campfire
            // burns the pack owes no more of it (the same carve-out the
            // blocked check above makes).
            // ANY TRADE, not just the ones at a station. This was limited to
            // Forge and then Fire, so a smith and a cook batched their work
            // while an alchemist brewed ONE potion, walked off to sell it and
            // came back. "also do like blacksmith craft a lot then sell"
            // (project owner, 2026-08-30).
            {
                moreToUse = true;
                for (const prod::Ingredient& in : rr->inputs) {
                    if (!in.item) break;
                    if (rr->station == prod::Station::Fire &&
                        std::strcmp(in.item, "i_kindling") == 0 &&
                        client.FindWorldItemByGraphic(kCampfireGraphic, 3)) {
                        continue;
                    }
                    if (market::QtyOf(obs.pack, in.item) < in.qty) {
                        moreToUse = false;
                        break;
                    }
                }
            }
        }
        if (moreToUse && obs.WeightFraction() < 0.90) {
            // REPEAT WITH .makelast RATHER THAN RE-WALKING THE MENU.
            //
            // revolution_makelast.scp (a PLEVEL 1 command, so it is invoked by
            // speech with sphere.ini's CommandPrefix ".") repeats the last
            // COMPLETED craft: crafting_events.scp's @skillmakeitem stores the
            // baseid in CTAG.revo.makelast.item, for the legacy menus as well
            // as the modern gump. So the first item still goes through the
            // menu -- that is what sets the tag -- and the rest of the batch
            // is one command instead of four dialog round-trips each.
            //
            // The server re-checks CANMAKE every repetition, so skill,
            // materials, tool and station are all still enforced; it stops by
            // itself with "Make Last stopped: you can no longer craft ..." the
            // moment the stock runs out. It also cancels on war mode, attack,
            // spellcast, death and logout -- all of which are things this
            // character would want to stop crafting for anyway.
            //
            // Gathering is untouched: mining and fishing are TARGETED skills,
            // not menu crafts, and have no last-item to repeat. "most of
            // professions can use makelast ... except mining or fishing since
            // they require target and not craft" (project owner, 2026-08-30).
            if (!makeLastIssued_) {
                i32 canMake = 0;
                if (const prod::Recipe* rr =
                        prod::FindRecipe(craftItem_.c_str())) {
                    canMake = 500;
                    for (const prod::Ingredient& in : rr->inputs) {
                        if (!in.item || in.qty <= 0) continue;
                        if (rr->station == prod::Station::Fire &&
                            std::strcmp(in.item, "i_kindling") == 0 &&
                            client.FindWorldItemByGraphic(kCampfireGraphic, 3))
                            continue;   // the fire is already lit
                        const i32 have = market::QtyOf(obs.pack, in.item);
                        const i32 fits = have / in.qty;
                        if (fits < canMake) canMake = fits;
                    }
                }
                if (canMake > 500) canMake = 500;
                if (canMake > 1) {
                    char cmd[64];
                    std::snprintf(cmd, sizeof(cmd), ".makelast %d",
                                  static_cast<int>(canMake));
                    LogLine("craft: %d %s made -- repeating the other %d with "
                            "'%s' instead of walking the menu again",
                            craftMade_, craftItem_.c_str(),
                            static_cast<int>(canMake), cmd);
                    client.ActionSay(cmd);
                    makeLastIssued_ = true;
                    nextActionMs_ = obs.nowMs + 3000;
                    return false;
                }
            }
            LogLine("craft: %d %s made and the material is not finished -- "
                    "carrying on", craftMade_, craftItem_.c_str());
        } else if (craftMade_ >= needCfg_.craftBatch || !moreToUse) {
            LogLine("craft: %d %s made -- %s", craftMade_, craftItem_.c_str(),
                    moreToUse ? "enough for a trip to a buyer"
                              : "the material is spent");
            craftItem_.clear();
            return true;
        }
    }

    const CraftMenuPath* path = CraftMenuFor(craftItem_);
    if (!path) {
        LogLine("goal_failed=CRAFT reason=\"%s\" no menu path known for %s",
                faucet::RefusalName(faucet::Refusal::MissingRecipe),
                craftItem_.c_str());
        planner_.Finish(false, "no craft menu path known", obs.nowMs);
        return false;
    }

    if (client.ActionBusy()) return false;

    // WAITING ON THE LAST ONE. The pack is the witness: either the count
    // rises (handled above, which clears this) or the craft failed and the
    // timeout releases us. Either way, do not start a second craft on top of
    // the first.
    if (craftAwaitingMs_ != 0) {
        if (obs.nowMs - craftAwaitingMs_ < kCraftResolveMs) {
            if (client.ActionBusy()) return false;
            nextActionMs_ = obs.nowMs + 700;
            return false;
        }
        LogLine("craft: no result from the last %s in %llds -- trying again",
                craftItem_.c_str(),
                static_cast<long long>(kCraftResolveMs / 1000));
        craftAwaitingMs_ = 0;
    }

    // --- walk the menu -----------------------------------------------------
    if (client.CraftMenuOpen()) {
        // READ THE MENU, DO NOT COUNT STEPS.
        //
        // The first version tracked which level it thought it was on. One
        // failed answer reset that counter to zero while the SUBMENU was
        // still open, so it then hunted for "Spell Circle 3" inside the list
        // that was already offering "poison" -- and said "the craft menu does
        // not offer it" sixteen times a second while printing the very option
        // it wanted. The menu itself says which level it is; ask it.
        // DEEPEST FIRST. The menu itself says which level it is on, so ask it
        // from the bottom up; anything else mistakes a submenu for the top.
        // A step beginning with '^' is anchored at the start of the option.
        auto has = [&client](const char* step) -> bool {
            if (!step) return false;
            return step[0] == '^' ? client.DialogHasPrefix(step + 1)
                                  : client.DialogHasOption(step);
        };
        auto choose = [&client](const char* step) -> bool {
            return step[0] == '^' ? client.ChooseDialogByPrefix(step + 1)
                                  : client.ChooseDialogByName(step);
        };
        const char* want = nullptr;
        if (path->step3 && has(path->step3)) {
            want = path->step3;
        } else if (path->step2 && has(path->step2)) {
            want = path->step2;          // already in the submenu
        } else if (has(path->step1)) {
            want = path->step1;          // the top menu, or a flat one
        }
        if (!want) {
            LogLine("goal_failed=CRAFT reason=\"%s\" this menu offers none of "
                    "'%s' / '%s' / '%s'",
                    faucet::RefusalName(faucet::Refusal::MissingRecipe),
                    path->step1, path->step2 ? path->step2 : "(flat)",
                    path->step3 ? path->step3 : "(flat)");
            for (const std::string& o : client.CraftableNow()) {
                LogLine("craft:   offered: %s", o.c_str());
            }
            planner_.Finish(false, "the craft menu does not offer it", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        if (!choose(want)) {
            // DialogHasOption just said it was there, so this is a send
            // failure rather than a missing option. Let it settle and re-read.
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        LogLine("craft: chose '%s'", want);
        // LET THE CRAFT FINISH BEFORE TOUCHING ANYTHING ELSE.
        //
        // Two seconds was shorter than the skill itself, so the next menu was
        // opened while the previous item was still being made:
        //   02:19:30.091 craft: chose '^Poison'
        //   02:19:32.137 craft: making ... open the menu
        //   02:19:32.615 System: You put the Poison in your pack.
        // -- the re-open landed half a second BEFORE the potion existed.
        // Sphere answers an interrupted craft with @SkillAbort, so this was
        // racing the server for no gain. "you are not waiting to finish one
        // poison" (project owner, 2026-08-30).
        //
        // craftAwaitingMs_ makes the wait explicit rather than a guessed
        // delay: the pack is watched until the count rises, and the timeout
        // below is only a floor under a craft that failed silently.
        craftAwaitingMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 1000;
        return false;
    }

    // --- open it -----------------------------------------------------------
    //
    // The menu opens by USING the thing the craft is made from -- a blank
    // scroll for Inscription, a log for Bowcraft. The recipe's own first
    // input is that thing, so nothing here has to be hardcoded per trade.
    const prod::Recipe* r = prod::FindRecipe(craftItem_.c_str());
    if (!r || !r->inputs[0].item) {
        LogLine("goal_failed=CRAFT reason=\"%s\" %s has no recipe",
                faucet::RefusalName(faucet::Refusal::MissingRecipe),
                craftItem_.c_str());
        planner_.Finish(false, "no recipe", obs.nowMs);
        return false;
    }
    // WHAT OPENS THE MENU. For most trades it is the material -- a blank
    // scroll, a log. For blacksmithing it is the TOOL: t_weapon_mace_smith is
    // a hardcoded engine type (defs_types_hardcoded.scp) whose double-click
    // opens the smith menu, and an ingot's double-click opens nothing at all.
    // ...except where the material EATS the double-click. For a fire recipe
    // the opener is the t_cooking tool: using the raw steak itself would be
    // answered by Use_Eat (CCharUse.cpp:1862) -- the character would swallow
    // its own stock one click at a time and no menu would ever come.
    // THE TOOL OPENS THE MENU wherever the trade has one -- a mortar for
    // alchemy, tinker tools, a sewing kit, a saw -- and only Inscription
    // (Tool::BlankScroll) and the toolless recipes open from the material.
    // Special-casing the smith hammer alone left alchemy double-clicking a
    // reagent and being told "You can't think of a way to use that item".
    const ToolOpener* opener_tool = OpenerFor(r->tool);
    const std::vector<u16> openGfx =
        r->station == prod::Station::Fire
            ? std::vector<u16>(kCookingToolGfx, kCookingToolGfx + 3)
            : (opener_tool
                   ? std::vector<u16>(opener_tool->gfx,
                                      opener_tool->gfx + opener_tool->n)
                   : econ::GraphicsForItem(r->inputs[0].item));
    u32 opener = 0;
    for (u16 g : openGfx) {
        opener = client.FindBackpackItemByGraphic(g);
        if (opener) break;
    }
    if (!opener) {
        LogLine("goal_blocked=CRAFT reason=\"%s\" nothing in the pack to open "
                "the %s menu with (%s)",
                faucet::RefusalName(faucet::Refusal::MissingTool),
                craftItem_.c_str(), r->inputs[0].item);
        planner_.Finish(false, "no material to start from", obs.nowMs);
        return false;
    }

    // BLACKSMITHING TAKES THREE ACTIONS, NOT ONE.
    //
    // "how to craft is double click hammer then select ingot then select what
    // do you want to craft" (project owner, 2026-08-29). The hammer arms a
    // TARGET cursor; the cursor is given an ingot; only THEN does the menu
    // appear. This code used the hammer and sat waiting for a menu that was
    // never going to come on its own -- the same shape as the smelt bug, where
    // the forge arms a cursor for the ore.
    //
    // Inscription and bowcraft are genuinely one action (use the material),
    // so the middle step is asked for only where the tool opens a target.
    if (r->tool == prod::Tool::SmithHammer) {
        if (craftCursorPending_) {
            if (client.TargetActive()) {
                const std::vector<u16> matGfx =
                    econ::GraphicsForItem(r->inputs[0].item);
                u32 mat = 0;
                for (u16 g : matGfx) {
                    mat = client.FindBackpackItemByGraphic(g);
                    if (mat) break;
                }
                if (!mat) {
                    LogLine("goal_blocked=CRAFT reason=\"%s\" the smith cursor "
                            "is up but there is no %s to give it",
                            faucet::RefusalName(
                                faucet::Refusal::RequiredForProduction),
                            r->inputs[0].item);
                    craftCursorPending_ = false;
                    planner_.Finish(false, "no ingots to target", obs.nowMs);
                    return false;
                }
                LogLine("craft: giving the smith cursor an %s to open the menu",
                        r->inputs[0].item);
                client.ActionTargetObject(mat);
                craftCursorPending_ = false;
                craftStartedMs_ = obs.nowMs;
                craftMenuStep_ = 0;
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            if (obs.nowMs - craftClickedMs_ > 6000) craftCursorPending_ = false;
            return false;
        }
        LogLine("craft: making %s -- double-clicking the smith hammer",
                craftItem_.c_str());
        client.ActionUseObject(opener);
        craftCursorPending_ = true;
        craftClickedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    LogLine("craft: making %s -- using %s to open the menu",
            craftItem_.c_str(),
            r->station == prod::Station::Fire
                ? "a cooking tool"
                : (opener_tool ? "its own tool" : r->inputs[0].item));
    client.ActionUseObject(opener);
    craftStartedMs_ = obs.nowMs;
    craftMenuStep_ = 0;
    nextActionMs_ = obs.nowMs + 2000;
    return false;
}

bool Runner::DoFish(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    // CUT THE FISH UP. "nessa fishing at same spot constantly -- does he ever
    // cut fishes into raw fish?" (project owner, 2026-08-29). She did not: she
    // fished until "pack full" twenty times in a session and stopped, carrying
    // whole fish that are heavy, unsellable in that form and inedible.
    //
    // One whole fish yields FOUR cut steaks (Production.cpp i_fish_cut_raw,
    // Tool::Blade, no station and no skill), and the gesture is the same
    // use-one-thing-on-another as the bandage chain -- a dagger, which every
    // starter kit now carries as ITEMNEWBIE.
    //
    // Done BEFORE the pack-full check on purpose: cutting is what makes room,
    // so a full pack is a reason to cut rather than a reason to stop.
    if (const u32 blade = FindAny(client, kBlades,
                                  sizeof(kBlades) / sizeof(kBlades[0]))) {
        for (u16 g : kWholeFish) {
            const u32 whole = client.FindBackpackItemByGraphic(g);
            if (!whole) continue;
            LogLine("fish: cutting a whole fish (0x%04X) into steaks -- four "
                    "each, and lighter", g);
            client.ActionUseItemOn(blade, whole);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
    }

    if (obs.WeightFraction() >= 0.95) {
        LogLine("fish: pack full at %.0f%%", obs.WeightFraction() * 100.0);
        return true;
    }

    // The pole has to be IN HAND, the same way the axe does: skf_gather reads
    // the character's weapon, and a pole in the pack is not a pole in hand.
    // TAKE THE SERIAL FROM THE LAYER IT WAS FOUND ON.
    //
    // The first version noticed the pole was equipped and then asked the PACK
    // for its serial -- which is 0, because it is not in the pack -- and fell
    // back to hand1 regardless of which hand held it. i_fishing_pole is
    // TWOHANDS=Y so the kit arms it automatically, and the result was a fisher
    // standing beside water reporting REFUSE_MISSING_TOOL while holding one.
    //
    // That also produced a wrong conclusion on the way: the pack dump showed
    // no pole, and [NEWBIE FISHING] was very nearly recorded as BLOCKED_RUNTIME
    // when the kit had worked correctly all along.
    const std::vector<u16> poleGfx = econ::GraphicsForItem("i_fishing_pole");
    u32 pole = 0;
    for (u16 g : poleGfx) {
        if (client.EquippedGraphicAt(kLayerHand1) == g) {
            pole = client.EquippedAtLayer(kLayerHand1);
            break;
        }
        if (client.EquippedGraphicAt(kLayerHand2) == g) {
            pole = client.EquippedAtLayer(kLayerHand2);
            break;
        }
    }
    if (!pole) {
        for (u16 g : poleGfx) {
            const u32 inPack = client.FindBackpackItemByGraphic(g);
            if (!inPack) continue;
            LogLine("fish: arming the pole");
            client.ActionEquip(inPack, kLayerServerChooses);
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        // Say WHAT WAS THERE. "no fishing pole" is not a diagnosis when the
        // question is whether the kit delivered one, whether it is on a layer
        // this code does not read, or whether its graphic is not the one the
        // item table names.
        char seen[192];
        int n = 0;
        seen[0] = 0;
        const u32 pack = client.BackpackSerial();
        const usize count = client.ContainerItemCount(pack);
        for (usize i = 0; i < count && n < 160; ++i) {
            u32 sr = 0; u16 gfx = 0, amt = 0;
            if (!client.ContainerItemAt(pack, i, &sr, &gfx, &amt)) continue;
            n += std::snprintf(seen + n, sizeof(seen) - n, "%s0x%04X",
                               n ? "," : "", gfx);
        }
        LogLine("goal_failed=FISH reason=\"%s\" wanted=0x0DBF/0x0DC0 pack=[%s]",
                faucet::RefusalName(faucet::Refusal::MissingTool), seen);
        planner_.Finish(false, "no fishing pole", obs.nowMs);
        return false;
    }

    // --- has the last cast resolved? --------------------------------------
    // Gated on the target reply having been SENT: fishCastMs_ is also set
    // when the pole is double-clicked, and ungated this branch swallowed the
    // whole 9-second ceiling between cursor and reply doing nothing -- the
    // cast1 live run armed the cursor at :24.6 and answered it at :33.7.
    // The ceiling is NOT one stroke. DELAY=8.0 is per @Stroke, and a round
    // can run more than one before Sphere speaks: the cast3 live run held a
    // line at (1465,1751) for a full 9 seconds with no verdict at all, and
    // @Success (skill18_fishing.scp:37) packs the fish SILENTLY -- so the
    // only signs of a good round are the fish appearing in the pack or,
    // eventually, @Fail's sentence. Twenty-five seconds is three strokes of
    // headroom; on expiry the recast below aborts the stale round cleanly.
    const i64 kFishRoundCeilingMs = 25000;
    if (!fishCursorPending_ && fishCastMs_ != 0 &&
        obs.nowMs - fishCastMs_ < kFishRoundCeilingMs) {
        // Two kinds of answer. A cast that HELD and came up empty means the
        // water is good and the roll said mr_nothing (regionresources.scp:
        // RESOURCES=60.0 mr_nothing); cast there again. A REFUSAL is Sphere
        // saying this tile can never pay -- "There are no fish here." is
        // DEFMSG FISHING_2 (core/messages.scp:158) and came back instantly
        // when the cast1 run targeted near-shore water at (1466,1752) -- so
        // the tile goes on the dead list and the sweep moves on.
        // "You pull your line back in and stop fishing." is DELIBERATELY not
        // a resolution. It is @Abort (skill18_fishing.scp:43-44) -- the echo
        // of this character's own recast cancelling the previous round -- and
        // it arrives moments AFTER the new round's journal mark. Treating it
        // as a verdict resolved every new cast instantly, which recast again,
        // which aborted again: the cast3 run cancelled its own line twice a
        // second for a minute straight.
        static const char* kNoCatch[] = {
            "but fail to catch anything",       // @Fail: resolved, no fish
        };
        static const char* kRefusedHere[] = {
            "there are no fish here",           // no fish resource at the tile
            "try fishing elsewhere",            // FISHING_1, same verdict
            "try fishing in water",             // not water at all
            "can't fish from where you are standing",
            "you can't fish while riding",      // @PreStart refusal
            "cannot fish so close to yourself", // adjacent water is refused
            "target cannot be seen",
            "that is too far away",
        };
        bool done = false;
        for (const char* line : kNoCatch) {
            if (client.JournalSaidSince(line, fishCastJournalMs_)) {
                done = true;
                break;
            }
        }
        if (!done) {
            for (const char* line : kRefusedHere) {
                if (!client.JournalSaidSince(line, fishCastJournalMs_))
                    continue;
                LogLine("fish: refused at %d,%d (\"%s\") -- marking it dead",
                        fishX_, fishY_, line);
                deadTargets_.emplace_back(fishX_, fishY_);
                if (deadTargets_.size() > 32)
                    deadTargets_.erase(deadTargets_.begin());
                done = true;
                break;
            }
        }
        // ALL four fish, not just the first: the region rolls mr_fish1-4
        // (core/regionresources.scp:64-90) and each REAPs its own item, so a
        // counter watching only i_fish_big_1 misses three catches in four --
        // and @Success is silent, so the pack count is the only proof.
        const i32 caught = FishInPack(obs.pack);
        // ...and Sphere announces a catch OUT LOUD as well: "You pull out a
        // fish!" (hardcoded, not in skill18_fishing.scp). In the cast4 live
        // run that sentence arrived at :07:12 and the pack counter never
        // moved -- the round resolved only when the 25 s ceiling expired --
        // so the server's own receipt is trusted directly and the pack count
        // stays as a second witness.
        const bool saidCaught =
            client.JournalSaidSince("you pull out a fish", fishCastJournalMs_);
        if (caught > fishSeen_ || saidCaught) {
            LogLine("fish: caught one at %d,%d (%s)", fishX_, fishY_,
                    saidCaught ? "journal" : "pack count");
            fishSeen_ = caught;
            planner_.NoteProgress();
            state_.memory.NoteResource("fish", fishX_, fishY_, obs.z, true,
                                       obs.nowMs);
            if (!state_.memory.HasEvent("first_fish")) {
                state_.memory.NoteEvent("first_fish", "first fish caught",
                                        "water", obs.x, obs.y, obs.nowMs);
            }
            done = true;
        }
        if (!done) {
            nextActionMs_ = obs.nowMs + kFishPollMs;
            return false;
        }
        fishCastMs_ = 0;
    }

    // --- find water -------------------------------------------------------
    //
    // RANGE=4 is the shard's own number, so a spot further than that is not a
    // spot at all. Searching a wider radius and then walking is the difference
    // between fishing and standing hopefully near a lake.
    // GO TO A DOCK FIRST, and only then look for water.
    //
    // The order used to be the other way round, and it was the whole problem:
    // NearestFishingSpot searches 24 tiles and there is water within 24 tiles
    // of almost anywhere near Britain, so the dock trip only ran when that
    // search FAILED -- which it never did. The character chased the nearest
    // pond from wherever it stood, the route stopped short, it picked again
    // from the new position, and it drifted across the map without once
    // reaching a dock. There is no "Docks ARRIVED" line in any of those runs.
    if (!fishAtDock_) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            ++fishTrips_;
            if (fishTrips_ > kMaxFishTrips) {
                LogLine("goal_failed=FISH reason=\"%s\" no dock reachable "
                        "after %d trips",
                        faucet::RefusalName(faucet::Refusal::EconomicRouteBlocked),
                        fishTrips_);
                planner_.Finish(false, "no dock reachable", obs.nowMs);
                fishTrips_ = 0;
                return false;
            }
            const KnownResourceSource* known =
                state_.memory.BestProvenResource("fish", obs.x, obs.y, obs.nowMs);
            if (known) {
                LogLine("fish: back to a shore that has paid out at %d,%d",
                        known->x, known->y);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 4, "fishing spot");
            } else {
                LogLine("fish: going to a dock (trip %d)", fishTrips_);
                travelInFlight_ =
                    client.TravelToResource(wm::ResourceKind::Fishing);
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=FISH reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        // ARRIVAL IS A CLAIM ABOUT THE TILE, and the proof is water nearby.
        Client::FishingSpot probe;
        if (!client.NearestFishingSpot(client.PlayerX(), client.PlayerY(), 12,
                                       &probe)) {
            LogLine("fish: trip reported %s but there is no shore within 12 "
                    "tiles of %d,%d",
                    client.TravelSucceeded() ? "success" : "failure",
                    client.PlayerX(), client.PlayerY());
            deadTargets_.emplace_back(client.PlayerX(), client.PlayerY());
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        LogLine("fish: at the water's edge (%d,%d), shore %d,%d",
                client.PlayerX(), client.PlayerY(), probe.standX, probe.standY);
        fishAtDock_ = true;
        fishTrips_ = 0;
        state_.memory.NoteResource("fish", client.PlayerX(), client.PlayerY(),
                                   client.PlayerZ(), true, obs.nowMs);
        return false;
    }

    // AT A DOCK: cast at whatever water is already in range.
    //
    // Every previous version picked a "stand tile" and then tried to walk to
    // it, and every one of them failed differently -- and the post-mortem
    // found ONE cause under all of them: the client could not see the water
    // it was standing next to. Around Britain's docks the sea is wet STATICS
    // over impassable dry-by-tiledata land (see WaterHit in Client.h), so the
    // land-only water search reported "no water 2-4 tiles" while castable
    // water sat 2 tiles away, and every walk was aimed at either a wet tile
    // (an unwalkable A* goal by definition) or a "dry" tile under a water
    // static (walkable:false -- "goal not walkable"). With both water forms
    // visible, standing at the edge is usually already enough.
    //
    // 2 to 4 tiles: RANGE=4 is the maximum from skill18_fishing.scp, and the
    // minimum is the server's own "You cannot fish so close to yourself."

    // A shore hop is in flight. Its deadline is checked BEFORE the
    // GotoBusy early-return -- checked after, a hung walk means the ceiling
    // can never fire.
    if (fishTargetSet_) {
        if (obs.nowMs > fishWalkMs_) {
            LogLine("fish: shore hop to %d,%d timed out at %d,%d",
                    fishTargetX_, fishTargetY_, obs.x, obs.y);
            fishTargetSet_ = false;
            deadTargets_.emplace_back(fishTargetX_, fishTargetY_);
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            fishAtDock_ = false;   // re-approach through the travel path
            planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        if (client.GotoBusy()) return false;
        // The walk resolved -- arrived, or A* stopped as close as it could.
        // Either way the next tick asks the only question arrival ever
        // poses, "is there castable water from HERE", against a FRESH
        // observation; scanning this tick's snapshot after a walk is the
        // stale-position bug the chop goal already paid for. The tile is
        // marked tried first so a hop that resolved somewhere useless is
        // never picked again.
        LogLine("fish: shore hop done at %d,%d (wanted %d,%d)",
                client.PlayerX(), client.PlayerY(), fishTargetX_, fishTargetY_);
        deadTargets_.emplace_back(fishTargetX_, fishTargetY_);
        if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
        fishTargetSet_ = false;
        nextActionMs_ = obs.nowMs + 300;
        return false;
    }
    // When Sphere has already refused SEVERAL waters around this stand, the
    // whole near band is fishless and probing the rest of the ring one cast
    // at a time is just slower agreement. Refusals are structural, not luck:
    // an empty roll answers "you fish a while, but fail to catch anything"
    // (RESOURCES=60.0 mr_nothing, core/regionresources.scp:93), while "There
    // are no fish here." is about the TILE. Three of those within casting
    // range is enough evidence to move along the shore instead. Only refused
    // WATER counts -- the dead list also holds failed stand tiles, and those
    // say nothing about fish.
    // EXACT-tile matching for cast bookkeeping. IsDeadTarget deliberately
    // matches within EIGHT tiles (Runner.cpp:766) because it judges travel
    // destinations -- "this clearing is treeless" -- and that radius is
    // right for those. Applied to per-tile casts it is catastrophic: one
    // "There are no fish here." at (1466,1752) blackened the whole dock,
    // and the very next scan reported "no water 2-4 tiles ... and no shore
    // to hop to" from a stand with live water three tiles away (cast2 run).
    auto deadExact = [&](i32 x, i32 y) -> bool {
        for (const auto& d : deadTargets_)
            if (d.first == x && d.second == y) return true;
        return false;
    };

    int refusedNear = 0;
    for (const auto& d : deadTargets_) {
        if (std::max(std::abs(d.first - obs.x), std::abs(d.second - obs.y)) > 4)
            continue;
        Client::WaterHit dw;
        if (client.NearestWater(d.first, d.second, 0, &dw)) ++refusedNear;
    }

    Client::WaterHit water;
    bool haveTarget = false;
    for (int r = 2; r <= 4 && !haveTarget && refusedNear < 3; ++r) {
        for (i32 dy = -r; dy <= r && !haveTarget; ++dy) {
            for (i32 dx = -r; dx <= r && !haveTarget; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                Client::WaterHit w;
                if (!client.NearestWater(obs.x + dx, obs.y + dy, 0, &w)) continue;
                if (deadExact(w.x, w.y)) continue;
                water = w;
                haveTarget = true;
            }
        }
    }
    if (!haveTarget) {
        // No castable water from where we stand. That makes this a SHORT
        // WALK problem, not a search problem: NearestFishingSpot now vets
        // its stand tile with the pathfinder's own walkability query, so a
        // spot it returns is a goal ActionGoto's A* will accept -- the
        // missing property that killed every earlier walk. Commit to the
        // tile ONCE: re-picking from the current position every tick is
        // what made attempt 2's target drift from 9 tiles out to 20.
        // The dead list rides along so refused water is not re-nominated:
        // without it the sweep down the pier proposes the same "no fish
        // here" tiles from every new stand.
        Client::FishingSpot spot;
        if (!client.GotoBusy() &&
            client.NearestFishingSpot(client.PlayerX(), client.PlayerY(), 12,
                                      &spot, &deadTargets_) &&
            !deadExact(spot.standX, spot.standY) &&
            !(spot.standX == client.PlayerX() &&
              spot.standY == client.PlayerY())) {
            LogLine("fish: shore hop %d,%d -> %d,%d (water %d,%d)",
                    client.PlayerX(), client.PlayerY(),
                    spot.standX, spot.standY, spot.waterX, spot.waterY);
            client.ActionGoto(spot.standX, spot.standY);
            fishTargetX_ = spot.standX;
            fishTargetY_ = spot.standY;
            fishTargetSet_ = true;
            // A ceiling, not a wait: the hop is at most 12 tiles of route,
            // and 15 s is roomy even at a walk. Timed-out hops are marked
            // dead above, so this cannot retry the same tile forever, and
            // the planner's own attempt cap bounds the whole goal.
            fishWalkMs_ = obs.nowMs + 15000;
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
        // No walkable shore to hop to either: this place is a dud.
        LogLine("fish: no water 2-4 tiles from %d,%d and no shore to hop to "
                "-- finding another dock", obs.x, obs.y);
        fishAtDock_ = false;
        deadTargets_.emplace_back(obs.x, obs.y);
        if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    fishTrips_ = 0;

    // --- cast --------------------------------------------------------------
    if (client.ActionBusy()) return false;

    if (fishCursorPending_) {
        if (client.TargetActive()) {
            // Answer the cursor the way a classic client answers a click on
            // the water the player SEES. Coastline water is a static and a
            // static reply carries its graphic; open sea is wet land and
            // gets a ground reply. Sphere types both as fishable t_water
            // (items/i_ground_tiles.scp:733 [ITEMDEF 01796] TYPE=T_WATER,
            // DUPELIST through 017b2), so the difference is fidelity, not
            // permission -- and water.z is the surface actually targeted:
            // the static's own z (-5 at the Britain dock), not the land
            // buried under it (-15).
            if (water.graphic != 0) {
                client.ActionTargetStatic(water.x, water.y, water.z,
                                          water.graphic);
            } else {
                client.ActionTargetGround(water.x, water.y, water.z);
            }
            fishCursorPending_ = false;
            fishCastMs_ = obs.nowMs;
            fishCastJournalMs_ = client.JournalNowMs();
            fishX_ = water.x; fishY_ = water.y;
            fishSeen_ = FishInPack(obs.pack);
            nextActionMs_ = obs.nowMs + kFishPollMs;
            return false;
        }
        if (obs.nowMs - fishCastMs_ > 6000) {
            fishCursorPending_ = false;
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    client.ActionUseObject(pole);
    fishCursorPending_ = true;
    fishCastMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 800;
    return false;
}

bool Runner::DoTravel(Client& client, const Observation& obs) {
    if (obs.atWorkSite || obs.atBank) return true;
    // WHERE THIS LIFE'S WORK IS. This body walked every character toward a
    // forest -- a miner, a mage and a fisher included -- because the need it
    // answers had no profession gate and the destination was Lumber.
    const std::string gathers =
        needCfg_.profession ? needCfg_.profession->gathers : std::string("logs");
    if (gathers.empty()) return true;   // nothing to travel to; work is here
    if (client.TravelBusy()) return false;
    if (travelAttempts_ >= 3) {
        LogLine("goal_failed=TRAVEL_TO_REQUIRED_PLACE reason=\"three trips did not arrive\"");
        planner_.Finish(false, "three trips did not arrive", obs.nowMs);
        return false;
    }
    if (!travelInFlight_) {
        travelAttempts_++;
        const KnownResourceSource* stand =
            state_.memory.BestResource(gathers.c_str(), obs.x, obs.y,
                                       obs.nowMs);
        if (stand) {
            travelInFlight_ =
                client.TravelToPoint(stand->x, stand->y, 4, gathers.c_str());
        } else {
            travelInFlight_ = client.TravelToResource(ResourceKindFor(gathers));
        }
        if (!travelInFlight_) {
            LogLine("travel: could not start (%s)", client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 8000;
        }
        return false;
    }
    travelInFlight_ = false;
    if (client.TravelSucceeded()) {
        planner_.NoteProgress();
        return true;
    }
    LogLine("travel: did not arrive (%s)", client.TravelFailureText());
    planner_.NoteAttempt(obs.nowMs);
    return false;
}

// PRACTISE THE SKILL BY DOING IT.
//
// The half of progression that is not buying tenths from a guildmaster. A
// guildmaster sells up to 30.0 and stops; everything above that is the hours a
// character puts in. Meditation is the honest first case: it is raised purely
// by using it, needs no target, no reagents and no foe, and this life already
// wants it.
//
// DELIBERATELY NARROW. Magery and Evaluating Intelligence are raised by
// casting, which needs a spell choice, reagents and a legal target, and
// getting that wrong would have a mage burning its scribe stock on practice
// casts. Fighting skills already have TRAIN_COMBAT. Everything else --
// Inscription, Blacksmithing, Lumberjacking -- is raised by the work the life
// already does, and must NOT get an errand of its own: a scribe writes scrolls
// to sell, not to practise.
// EAT SOMETHING.
//
// The simplest need in the model, and it had NO GOAL AT ALL until now:
// NeedFood was assessed and printed every tick -- 27 times in one twenty
// minute session -- and appeared in no entry of the goal table, so it fell
// into a void every time. That is why M4's hunger row reads BUILT / NEVER
// FIRED. It was never reachable.
//
// Two halves, in the order a person would do them: eat what you are carrying,
// and if you are carrying none, go and buy some.
// DOES THIS LIFE HAVE A USE FOR THIS GRAPHIC?
//
// The one predicate behind both halves of "everything else is spare": what the
// dead-weight bank pass puts down, and what the loot pass sells. Gold, the
// tools this profession declares, the consumables it stocks, what it makes,
// and what it makes those from -- everything else is spare.
bool Runner::LifeNeedsGraphic(u16 gfx) const {
    if (gfx == kGoldCoin) return true;
    const prof::Profession* me = needCfg_.profession;
    if (!me) return false;
    for (const prof::ToolNeed& t : me->tools)
        for (u16 g : t.graphics) if (g == gfx) return true;
    for (const prof::ConsumableNeed& c : me->consumables)
        for (u16 g : c.graphics) if (g == gfx) return true;
    auto named = [&](const std::string& item) {
        for (u16 g : econ::GraphicsForItem(item.c_str())) if (g == gfx) return true;
        return false;
    };
    for (const std::string& it : me->consumes) if (named(it)) return true;
    for (const std::string& made : me->produces) {
        if (named(made)) return true;
        const prod::Recipe* r = prod::FindRecipe(made.c_str());
        if (!r) continue;
        for (const prod::Ingredient& in : r->inputs)
            if (in.item && named(in.item)) return true;
    }
    return false;
}

bool Runner::DoGetFood(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    // WHEN IS SUPPER OVER?
    //
    // This goal had no completion at all -- not one `return true` in the whole
    // body -- so it could never finish. Brannoc ate ONE HUNDRED AND SIXTY
    // times in a single session and the goal simply kept running until the
    // planner's 300-second limit killed it, whereupon the same unchanged
    // hunger picked it straight back:
    //
    //   session_goals families=1 picks=6 top=100% | GET_FOOD=6(100%)
    //   session_summary goals=0/6 gold=837->789
    //
    // The gold moving is the proof it was working -- he really did buy bread
    // twelve times and eat it -- and the goal still reported nothing, took the
    // whole session, and let no other family have a turn. Voris did the same.
    //
    // Fed, with something in the pack for later, is done.
    if (!obs.hungry && !obs.starving && obs.food >= needCfg_.foodLow) {
        LogLine("food: fed, and carrying %d for later "
                "-- this errand is done", obs.food);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // AND EAT ONLY WHEN HUNGRY. "if they are full they dont need to eat"
    // (project owner, 2026-08-29). The eat branch fired on carrying food
    // rather than on needing it, so a fed character chewed through its whole
    // pack -- 160 mouthfuls in one session -- and then had to go and buy more.
    // Food costs gold; a full stomach wastes it.
    const u32 food = (obs.hungry || obs.starving)
                         ? FindAny(client, kFood, sizeof(kFood) / sizeof(kFood[0]))
                         : 0;
    if (food) {
        LogLine("food: eating (hungry=%d starving=%d, carrying %d)",
                obs.hungry ? 1 : 0, obs.starving ? 1 : 0, obs.food);
        // A double-click is how a player eats. The server decides whether it
        // helped; the next tick's journal says so.
        client.ActionUseObject(food);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // A FISHER MAKES ITS OWN SUPPER TOO.
    //
    // "Marla shouldnt buy food she can make food" (project owner,
    // 2026-08-29). A life that catches fish has dinner in its pack already:
    // one whole fish cuts into four steaks and a campfire cooks them, and
    // i_fish_cut_cooked is t_food. Walking to a baker to buy bread with the
    // river behind you is not a shopping trip, it is a failure to look in
    // your own backpack.
    //
    // The cut and the cook belong to the fishing and crafting goals, which
    // already know how; what this does is stop the FOOD errand spending gold
    // when the makings are carried.
    if (needCfg_.profession && needCfg_.profession->gathers == "fish") {
        const bool haveMakings =
            FindAny(client, kWholeFish,
                    sizeof(kWholeFish) / sizeof(kWholeFish[0])) != 0 ||
            client.FindBackpackItemByGraphic(kFishRawSteak) != 0;
        if (haveMakings) {
            LogLine("food: carrying the catch -- cutting and cooking it rather "
                    "than buying bread");
            planner_.Cooldown(GoalKind::GetFood, obs.nowMs + kNoFoodCooldownMs);
            planner_.Finish(false, "will cook the catch instead", obs.nowMs);
            return false;
        }
    }

    // A MAGE MAKES ITS OWN SUPPER.
    //
    // "food was not a problem at all normally in Revolution UO -- mages make
    // their own food with the spell, you can collect from farms throughout the
    // world, fishers can sell fish steaks to people" (project owner,
    // 2026-08-29). Treating hunger as a shopping errand was the mistake under
    // all of this: it made a solved problem into a goal that could eat a whole
    // session.
    //
    // [SPELL 2] s_create_food is spellflag_playeronly with no target flag at
    // all (spells_magery.scp:36) -- four mana, MAGERY 10.0 to attempt, cast at
    // nobody. Anyone who can cast it should, before walking anywhere: it costs
    // no gold, needs no shop, and works while broke, which is exactly the
    // predicament the stand-down below was written for. It also raises Magery,
    // so the errand pays for itself.
    //
    // Mana is the renewable resource. Spending 4 of it on dinner is free in a
    // way that 30 gold is not.
    // HAVING THE SKILL IS NOT HAVING THE SPELL.
    //
    // First live outing of the line above: Voris, an alchemist carrying Magery
    // 50.0 and no spellbook worth the name, asked for Create Food every six
    // seconds for the whole session and was told every time --
    //
    //   [ACTION] cast_spell id=2 target=0x00000000 mana=32
    //   System: The spell is not in your spellbook.
    //
    // -- with mana sitting at 32 the entire time, which is the tell: a cast
    // that costs nothing never happened. The skill check passed and the
    // capability check did not exist. Ask once, believe the answer, and go
    // shopping like anyone else.
    if (noCreateFoodSpell_) {
        // fall through to buying
    } else if (obs.SkillTenths(rules::kMagery) >= 100) {
        if (createFoodMark_ != 0 &&
            client.JournalSaidSince("not in your spellbook", createFoodMark_)) {
            noCreateFoodSpell_ = true;
            LogLine("food: Create Food is not in this character's spellbook "
                    "(magery %.1f) -- buying food instead for the rest of the "
                    "session", obs.SkillTenths(rules::kMagery) / 10.0);
        } else if (obs.mana >= kCreateFoodMana) {
            LogLine("food: casting Create Food rather than shopping "
                    "(magery %.1f, mana %d)",
                    obs.SkillTenths(rules::kMagery) / 10.0, obs.mana);
            createFoodMark_ = client.JournalNowMs();
            client.ActionCastSpell(kSpellCreateFood);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 6000;
            return false;
        }
        // Out of mana but able to cast: waiting for mana beats walking to a
        // shop, and beats standing the goal down while broke.
        else if (obs.gold < kFoodMoney) {
            LogLine("food: %d mana is short of the %d Create Food needs -- "
                    "resting for it rather than shopping with %d gold",
                    obs.mana, kCreateFoodMana, obs.gold);
            nextActionMs_ = obs.nowMs + 15000;
            return false;
        }
    }

    // STILL TO DO, and deliberately not faked here: crops on the world's farms
    // are a second free source, and fish steaks bought from a FISHER are the
    // third -- which is the same errand as R4's first player-to-player trade,
    // since the fisher selling them will be another bot. Both belong with the
    // market layer rather than bolted into this goal.

    // NOTHING TO EAT AND NOTHING TO BUY WITH.
    //
    // A goal that cannot possibly succeed must stand down, or it eats the
    // session. This one did exactly that on its first live outing: Kaelen died,
    // lost everything to full loot, and woke with no food and no gold -- so
    // GET_FOOD failed, was re-picked, and took the WHOLE 25 minutes:
    //
    //   session_goals families=1 picks=5 top=100% varied=0 | upkeep=5(100%)
    //   session_summary goals=0/5 gold=0->0
    //
    // Being hungry with an empty purse is a real predicament and the honest
    // response is to go and earn something, not to keep walking to a shop.
    if (obs.gold < kFoodMoney) {
        LogLine("food: hungry with %d gold -- nothing to eat and nothing to buy "
                "with; standing down to go and earn", obs.gold);
        planner_.Cooldown(GoalKind::GetFood, obs.nowMs + kNoFoodCooldownMs);
        planner_.Finish(false, "no food and no money", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // Buying food is an ordinary provisioner errand, and it is NOT a craft
    // input -- so it does not belong in DoBuySupplies, which is about the
    // things a profession makes other things from.
    if (client.TravelBusy()) return false;

    // A PROVISIONER ON THIS SHARD CANNOT SELL FOOD.
    //
    // This goal asked one anyway, all session, and never ate. The shop window
    // opened every time -- 24 items, all of them backpacks, lockpicks, bottles
    // and board games -- because TNS's VENDOR_S_PROVISIONER has its four food
    // lines COMMENTED OUT (tm_vend.scp:1276-1279):
    //
    //   //SELL=i_bread_loaf,{5 38}
    //   //SELL=i_lamb_leg,{5 38}
    //   //SELL=i_chicken_leg,{5 38}
    //   //SELL=i_bird_cooked,{5 38}
    //
    // So this was never a protocol failure or a pathing failure. The errand was
    // addressed to a shop that structurally does not stock the goods. Voris and
    // Ysolde spent a whole 25-minute session on it and picked no other goal:
    //   session_goals families=1 picks=4 top=100% | GET_FOOD=4(100%)
    //
    // The BAKER carries it -- SELL=i_bread_loaf,{55 140}, plus pies, muffins
    // and cakes -- and i_bread_loaf is ITEMDEF 0103b, which is already the
    // first entry in kFood above, so the eating side needed no change at all.
    // Ask the baker first and keep the provisioner only as a fallback: it costs
    // nothing when a baker is near, and a town without one still gets a try.
    //
    // The runtime vendor lists are TNS's, kept deliberately. Uncommenting those
    // four lines would have been the smaller diff and the wrong one -- it edits
    // the shard's economy to suit the bot instead of teaching the bot where
    // food is sold.
    // --- THE ERRAND OWNS THE HANDSHAKE FROM HERE -------------------------
    //
    // Baker first, provisioner as the fallback -- the seller list, not two
    // hand-written branches and a trip-parity trick. Everything else this
    // block used to do (walk up before speaking, scan when the keeper is not
    // in the cache, wait past the vendor deadline) is the same sequence the
    // bandage errand needed, and is now written once.
    //
    // graphic = 0 means "the caller chooses from the offer", because food is
    // not one item: kFood lists bread, lamb, chicken and cooked bird, and any
    // of them ends the hunger. The errand opens the shop and hands back the
    // window; picking the row stays here, where the eating rules live.
    if (!foodErrand_.Running()) {
        life::VendorErrandSpec spec;
        spec.Sell("baker", wm::Service::Baker);
        spec.Sell("provisioner", wm::Service::Provisioner);
        spec.graphic = 0;
        spec.what = "something to eat";
        spec.maxTrips = kMaxFoodTrips;
        foodErrand_.Begin(spec);
    }
    const life::VendorErrandResult r = foodErrand_.Tick(client, obs);
    if (!r.why.empty()) LogLine("food: %s", r.why.c_str());
    if (r.wake == life::Wake::AfterDelay && r.delayMs > 0)
        nextActionMs_ = obs.nowMs + r.delayMs;

    // Any terminal state ends it -- see the note in DoReplaceEquipment.
    if (life::IsTerminal(r.status) && r.status != life::ActivityStatus::Success) {
        LogLine("goal_failed=GET_FOOD status=%s reason=\"%s\"",
                life::ActivityStatusName(r.status), r.why.c_str());
        const i64 rest = (r.status == life::ActivityStatus::RetryableFailure)
                             ? kShortRestMs : kNoFoodCooldownMs;
        planner_.Cooldown(GoalKind::GetFood, obs.nowMs + rest);
        planner_.Finish(false, "no food seller reachable", obs.nowMs);
        return false;
    }
    if (!r.offerOpen) {
        planner_.NoteAttempt(obs.nowMs);
        return false;
    }

    // The shop is open and it is OURS. Buy the first edible row we can
    // afford -- two of them, because walking back for the second loaf is the
    // errand nobody wants to run twice.
    for (const Client::VendorItem& v : client.VendorOffer()) {
        if (!GraphicIsAny(v.graphic, kFood, sizeof(kFood) / sizeof(kFood[0])))
            continue;
        if (static_cast<i32>(v.price) * 2 > obs.gold) continue;
        LogLine("food: buying %s at %d gold", v.name.c_str(),
                static_cast<i32>(v.price));
        client.ActionVendorBuy(r.keeper, v.serial, 2);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }
    LogLine("goal_failed=GET_FOOD reason=\"this shop has nothing edible this "
            "character can afford\"");
    planner_.Cooldown(GoalKind::GetFood, obs.nowMs + kNoFoodCooldownMs);
    planner_.Finish(false, "nothing edible for sale", obs.nowMs);
    foodErrand_.Cancel();
    return false;
}

// Is this spell already in the book? The book is a container of items whose
// graphic is 0x1F2D + the spell number, so a scroll's own graphic answers it.
bool Runner::BookHasGraphic(Client& client, u32 book, u16 graphic) const {
    if (!book) return false;
    const usize n = client.ContainerItemCount(book);
    for (usize i = 0; i < n; ++i) {
        u32 serial = 0; u16 g = 0, amount = 0;
        if (!client.ContainerItemAt(book, i, &serial, &g, &amount)) continue;
        if (g == graphic) return true;
    }
    return false;
}

// Walk to a scroll seller, open it, and buy one thing.
//
// `graphic` of 0 means "any spell scroll". `skipKnown` refuses scrolls the
// book already holds, which is the difference between the two sellers on this
// shard and the whole point of preferring one:
//
//   a MAGE shop stocks random_first_circle .. random_fourth_circle, so what
//   arrives is not chosen and a duplicate is quite likely
//   a SCRIBE stocks 44 NAMED scrolls, so a specific missing spell can be asked
//   for and nothing is wasted on one already owned
//
// "mage should also give priority to buy new spells not on the book" (project
// owner) -- which is only possible at the scribe, so that is where this goes
// first. Returns false while still working.
// `owner` is the goal this purchase belongs to. It was hardcoded to
// FillSpellbook, so when the gear and scissors paths reused this helper their
// failures were logged as FILL_SPELLBOOK, cooled FILL_SPELLBOOK down, and
// shared its trip counter -- which is why a fencer produced 1,932 identical
// "buying armour" lines and a goal_failed=FILL_SPELLBOOK "no 'armorer'
// reachable". Three errands wearing one name.
bool Runner::BuyScrollFrom(Client& client, const Observation& obs,
                           const char* trade, wm::Service svc, u16 graphic,
                           bool skipKnown, u16 qty, const char* what,
                           GoalKind owner) {
    if (client.TravelBusy()) return false;

    // THE TRIP COUNTER BELONGS TO THE ERRAND, not to this helper. Three goals
    // share it, and without this a gear trip spent the spellbook's allowance
    // and vice versa -- the fencer's 1,932 identical "buying armour" lines
    // came out of exactly that confusion.
    if (buyTripsOwner_ != owner) {
        buyTripsOwner_ = owner;
        spellbookTrips_ = 0;
    }
    const u32 keeper = client.NearestShopkeeperWithTrade(trade, svc);
    if (!keeper) {
        if (++spellbookTrips_ > kMaxSpellbookTrips) {
            spellbookTrips_ = 0;
            // A SELLER WE CANNOT REACH IS NOT THE END OF THE ERRAND.
            //
            // The scribe is preferred because only a scribe lets us choose the
            // spell, but plenty of towns have none. Falling back to the mage
            // shop -- a random scroll rather than a chosen one -- beats
            // failing the whole goal and standing down for four minutes.
            if (!scribeExhausted_ && std::strcmp(trade, "scribe") == 0) {
                LogLine("spellbook: no scribe reachable after 3 trips -- "
                        "falling back to a mage shop, where the scroll is "
                        "random rather than chosen");
                scribeExhausted_ = true;
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            LogLine("goal_failed=%s reason=\"no '%s' reachable after 3 trips\"",
                    GoalKindName(owner), trade);
            planner_.Cooldown(owner, obs.nowMs + kNoSpellbookCooldownMs);
            planner_.Finish(false, "no seller reachable", obs.nowMs);
            return false;
        }
        LogLine("%s: looking for a '%s' to sell %s (trip %d)",
                GoalKindName(owner), trade, what, spellbookTrips_);
        travelInFlight_ = client.TravelToService(svc, HomeOrNearest(state_.homeCity));
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Walk up before speaking -- the lesson the food goal had to learn twice.
    i32 vx = 0, vy = 0; i8 vz = 0;
    if (!client.MobilePosition(keeper, &vx, &vy, &vz)) {
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    const i32 d = TileDist(obs.x, obs.y, vx, vy);
    if (d > kVendorReach) {
        LogLine("%s: the %s is %d tiles away -- walking up",
                GoalKindName(owner), trade, d);
        travelInFlight_ = client.TravelToEntity(keeper, 1);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    if (!OfferBelongsTo(client, keeper)) {
        LogLine("%s: asking the %s to show %s", GoalKindName(owner), trade, what);
        client.ActionVendorOpen(keeper);
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }

    int skipped = 0;
    for (const Client::VendorItem& v : client.VendorOffer()) {
        const bool match =
            graphic ? (v.graphic == graphic)
                    : (v.graphic >= kFirstScrollGraphic &&
                       v.graphic <= kLastScrollGraphic);
        if (!match) continue;
        // DO NOT BUY A SPELL THIS CHARACTER ALREADY HAS. Gold spent on a
        // duplicate buys nothing at all -- the book refuses it and the scroll
        // is wasted.
        if (skipKnown && BookHasGraphic(client, obs.spellbookSerial, v.graphic)) {
            ++skipped;
            continue;
        }
        if (static_cast<i32>(v.price) > obs.gold) continue;
        LogLine("spellbook: buying %s ('%s', 0x%04X, spell %d) at %d gold "
                "-- %d of this shop's scrolls were already in the book",
                what, v.name.c_str(), v.graphic,
                static_cast<int>(v.graphic) - 0x1F2D,
                static_cast<i32>(v.price), skipped);
        client.ActionVendorBuy(keeper, v.serial, qty);
        // An ask, not progress -- same reason as BUY_SUPPLIES: counting a
        // purchase before the server takes the gold clears the failure ladder
        // on every retry.
        planner_.NoteAttempt(obs.nowMs);
        // A bought scroll must be re-read into the book, so force a re-open.
        spellbookOpened_ = false;
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }

    if (!scribeExhausted_ && std::strcmp(trade, "scribe") == 0) {
        LogLine("spellbook: this scribe has nothing the book lacks (%d of its "
                "scrolls are already known) -- trying a mage shop", skipped);
        scribeExhausted_ = true;
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    LogLine("goal_failed=%s reason=\"this '%s' does not stock %s "
            "(%d already known)\"", GoalKindName(owner), trade, what, skipped);
    planner_.Cooldown(owner, obs.nowMs + kNoSpellbookCooldownMs);
    planner_.Finish(false, "seller has none", obs.nowMs);
    return false;
}

// WHICH SPELL THIS CHARACTER CAN ACTUALLY PRACTISE WITH.
//
// Not a constant, because the starter book is not what the code assumed. The
// [NEWBIE MAGERY] template hands over a spellbook with MORE1=0382a8c38, and
// that bitmask decodes to exactly twelve spells --
//
//   Heal, Magic Arrow, Night Sight, Cure, Harm, Strength, Fireball, Poison,
//   Teleport, Fire Field, Greater Heal, Lightning
//
// -- which does NOT include Create Food. So the hardcoded practice spell was
// missing from the book of every freshly created character on this shard, not
// only from Voris's. "The spell is not in your spellbook", forever.
//
// The book is read rather than guessed: a spellbook's contents arrive as
// container items whose graphic is 0x1F2D + the spell number, so opening it
// says precisely what this character owns.
//
// The candidates are ordered by how safe they are to cast at oneself, which is
// the owner's rule for skill practice -- "we used to use all of the skills on
// ourselves with no damage to ourselves". Every one below is
// spellflag_good + spellflag_playeronly in spells_magery.scp: none can hurt the
// caster, none makes a criminal of them, and none needs a foe.
int Runner::PickPracticeSpell(Client& client, const Observation& obs) const {
    if (obs.spellbookSerial == 0) return -1;

    // Night Sight first: it always succeeds on a healthy character, where Heal
    // on someone at full health does nothing. Create Food last -- it is the
    // one that started all this, and it is still fine when present.
    // ALL TWELVE of circles 1-4 that are safe to cast at oneself, derived from
    // spells_magery.scp by excluding everything carrying spellflag_harm or
    // spellflag_damage and every field spell. The first version of this list
    // named only four, and Ysolde -- whose book holds fourteen spells -- then
    // logged "nothing safe to cast at myself is in this book" seventy times in
    // one session. A book is not obliged to contain the four spells this code
    // happened to think of.
    //
    // Ordered by how reliably each one does something when cast on a healthy
    // character standing still: Night Sight and the stat buffs always take,
    // where Heal on someone at full health does nothing at all.
    static const int kSelfSafe[] = {
         6,   // Night Sight
         7,   // Reactive Armor
        16,   // Strength
         9,   // Agility
        10,   // Cunning
        15,   // Protection
        26,   // Arch Protection
        11,   // Cure
        25,   // Arch Cure
         4,   // Heal
        29,   // Greater Heal
         2,   // Create Food
    };

    const usize n = client.ContainerItemCount(obs.spellbookSerial);
    for (int want : kSelfSafe) {
        const u16 wantGfx = static_cast<u16>(0x1F2D + want);
        for (usize i = 0; i < n; ++i) {
            u32 serial = 0; u16 gfx = 0, amount = 0;
            if (!client.ContainerItemAt(obs.spellbookSerial, i, &serial, &gfx,
                                        &amount))
                continue;
            if (gfx == wantGfx) return want;
        }
    }
    // SAY WHAT IS ACTUALLY IN THERE. "Nothing safe to cast" is a claim about
    // the book, and a claim about the book should be checkable from the log
    // rather than taken on trust -- especially since this is also the only
    // place that proves the book's contents can be read at all.
    std::string had;
    for (usize i = 0; i < n && i < 32; ++i) {
        u32 serial = 0; u16 gfx = 0, amount = 0;
        if (!client.ContainerItemAt(obs.spellbookSerial, i, &serial, &gfx,
                                    &amount))
            continue;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s%d", had.empty() ? "" : ",",
                      static_cast<int>(gfx) - 0x1F2D);
        had += buf;
    }
    LogLine("practice: the book holds %d item(s), spells [%s] -- none of them "
            "is one of the twelve safe to cast at oneself",
            static_cast<int>(n), had.c_str());
    return -1;
}

// ---------------------------------------------------------------------------
// GEAR.
//
// "always try to wear better equipment based on your class", "bots also always
// check for gear", "Kaelen needs to buy some armor" (project owner,
// 2026-08-29).
//
// Two halves. WEAR what is already carried if it beats what is worn -- loot
// arrives in the pack and sat there forever, because nothing ever looked. And
// BUY a piece for an empty slot when the purse is clear of the reserve.
//
// The class rule is not a preference. On this shard a metal set stops a
// caster casting entirely, so ArmorFor refuses metal to anyone with Magery
// rather than scoring it lower.
const ArmorPiece* ArmorFor(u16 graphic) {
    for (const ArmorPiece& a : kArmorPieces)
        if (a.graphic == graphic) return &a;
    return nullptr;
}

bool Runner::MayWear(const ArmorPiece& a, const Observation& obs) const {
    // THE PROFESSION ANSWERS FIRST, because it knows before login.
    //
    // "mage wears only mage equipment" (project owner). Read off the
    // catalogue's `wears`/`maysShield` (M5, professions.h) rather than
    // re-derived here. This closes a gap the Magery test never covered: a
    // tailor has no Magery and plenty of STR, so nothing stopped it putting on
    // a platemail gorget it had looted -- it was allowed to wear anything it
    // could lift.
    if (const prof::Profession* pr = needCfg_.profession) {
        const bool metal = (a.cls == ArmorClass::Metal);
        const bool leather = (a.cls == ArmorClass::Leather);
        if (a.cls == ArmorClass::Shield) {
            if (!pr->maysShield) return false;
        } else if (metal && pr->wears != prof::Profession::Wear::Metal) {
            return false;
        } else if (leather && pr->wears == prof::Profession::Wear::Cloth) {
            return false;
        }
    }

    // The Magery test below is the answer ONLY for a life with no profession.
    // A profession that says Metal must not then be talked out of it by its
    // own utility Magery -- a swordsman who learned Recall is still a
    // swordsman, and the earlier version of this function would have kept him
    // in cloth forever.
    if (needCfg_.profession) {
        if (obs.str <= 0) return false;      // unknown STR is not "strong enough"
        return obs.str >= static_cast<i32>(a.reqStr);
    }

    // NOT KNOWING IS NOT THE SAME AS ZERO.
    //
    // Thessaly is an Apprentice Mage with Magery 50.0, and she was found
    // WEARING PLATEMAIL GAUNTLETS -- ten pairs in her pack and one on her
    // paperdoll. The rule below is right and it read her Magery as 0, because
    // the skill list had not arrived from the server yet: obs.skills was empty
    // and SkillTenths returns 0 for a skill it cannot find. An empty skill
    // list is silence, not evidence of a warrior.
    //
    // On this shard the mistake is unrecoverable while worn -- a metal set
    // stops a caster casting at all -- so the safe reading of silence is to
    // refuse. A character that genuinely has no Magery loses nothing but a few
    // seconds, until its skills arrive.
    const bool skillsKnown = !obs.skills.empty();
    const bool mayBeCaster =
        !skillsKnown || obs.SkillTenths(rules::kMagery) > 0;
    if (mayBeCaster && a.cls == ArmorClass::Metal)
        return false;                       // metal ends a caster's casting
    if (mayBeCaster && a.cls == ArmorClass::Shield)
        return false;                       // a shield hand is a spell hand
    // Strength is read the same way: unknown is not "strong enough".
    if (obs.str <= 0) return false;
    return obs.str >= static_cast<i32>(a.reqStr);
}

bool Runner::DoUpgradeGear(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;
    // AND NOT WHILE ALREADY WALKING TO THE SHOP.
    //
    // Without this the goal re-decided every tick during the journey, printed
    // "no piece for an empty slot" again and re-issued the trip: Nessa logged
    // it 849 times in one session and spent it running between a lake and a
    // tailor. The third goal to spin this way by re-entering mid-travel.
    if (client.TravelBusy()) return false;

    // --- WEAR WHAT IS ALREADY CARRIED ------------------------------------
    //
    // Every armour graphic this shard defines, checked against the pack. The
    // layer comes from tiledata, so the comparison is against the piece
    // actually in that slot rather than a guess about what a slot holds.
    for (const ArmorPiece& a : kArmorPieces) {
        if (!MayWear(a, obs)) continue;
        const u32 have = client.FindBackpackItemByGraphic(a.graphic);
        if (!have) continue;
        const u8 layer = client.ItemEquipLayer(a.graphic);
        if (!layer) continue;
        const u16 wornGfx = client.EquippedGraphicAt(layer);
        const ArmorPiece* worn = wornGfx ? ArmorFor(wornGfx) : nullptr;
        const int wornArmor = worn ? worn->armor : 0;
        if (wornGfx && wornArmor >= a.armor) continue;   // no better
        LogLine("gear: wearing 0x%04X (armor %d, needs str %d, have %d) over "
                "0x%04X (armor %d)", a.graphic, a.armor, a.reqStr, obs.str,
                wornGfx, wornArmor);
        client.ActionEquip(have, layer);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }


    // --- WHAT THIS LIFE WILL NOT WEAR ------------------------------------
    //
    // M7's disposal order, applied to the pack. Until now the wear pass simply
    // skipped a piece the class refuses and said nothing, so a mage carried
    // looted platemail around for a whole session with no record of why it was
    // never worn and no decision about where it should go.
    //
    // market::DisposeOfGear owns the order -- wear, then offer to players,
    // then sell to an NPC only where the Gold Faucet Registry establishes that
    // route. For armour it does not (monster_loot_resale is UNKNOWN), so the
    // answer today is the bank box, and the BANK goal already carries pack
    // weight to a bank. What this adds is the RECORD: one line per item naming
    // the step the order reached and the reason it stopped there.
    if (needCfg_.profession && !dispositionLogged_) {
        for (const ArmorPiece& a : kArmorPieces) {
            if (MayWear(a, obs)) continue;              // the wear pass has it
            if (!client.FindBackpackItemByGraphic(a.graphic)) continue;
            const char* name = econ::ItemNameForGraphic(a.graphic);
            const market::DisposalRuling r = market::DisposeOfGear(
                *needCfg_.profession, name ? name : "", false,
                /*playersDeclined=*/false, state_.ledger);
            LogLine("gear: carrying 0x%04X (%s) this life will not wear -- %s: %s",
                    a.graphic, name ? name : "unmapped item",
                    market::DisposalName(r.what), r.reason ? r.reason : "");
        }
        dispositionLogged_ = true;
    }

    // --- BUY A PIECE FOR AN EMPTY SLOT -----------------------------------
    //
    // Only above the reserve: armour is worth having and is never worth being
    // unable to eat for. The best affordable legal piece is chosen, which for
    // a caster means the best LEATHER, and for a fighter the best its
    // strength allows.
    // A CRAFTER DOES NOT GO ARMOUR SHOPPING. "for crafter upgrade gear just
    // wear normal clothing for now" (project owner, 2026-08-29).
    //
    // A life that does not pick fights has little use for armour and every use
    // for its gold: a tailor buying a leather tunic is spending the money that
    // buys its lessons and its cloth. It still WEARS anything better that it
    // loots -- that part ran above and costs nothing -- but it does not shop.
    if (needCfg_.profession && !WantsToHunt(*needCfg_.profession)) {
        LogLine("gear: nothing carried is an upgrade, and this life does not "
                "fight -- ordinary clothes will do, so no armour shopping");
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    const i32 reserve =
        needCfg_.profession ? needCfg_.profession->goldReserve : 0;
    if (obs.gold <= reserve + kArmorMoney) {
        LogLine("gear: nothing carried is an upgrade, and %d gold is not clear "
                "of the %d reserve -- earning first", obs.gold, reserve);
        planner_.Cooldown(GoalKind::UpgradeGear, obs.nowMs + kGearCooldownMs);
        planner_.Finish(false, "no upgrade and no spare money", obs.nowMs);
        return false;
    }

    // BUY ONLY WHAT IS ACTUALLY FOR SALE -- and the list is longer than I first
    // thought.
    //
    // I claimed metal armour was sold by nobody. That was WRONG, and the owner
    // caught it: "armorers has more to sell at vendors". There are FIVE
    // armorer templates -- VENDOR_S_ARMORER_LEATHER, _RING, _CHAIN, _PLATE and
    // _SHIELDS -- carrying 36 distinct pieces between them, ringmail through
    // platemail and helms and shields. My check had matched
    // "i_(plate|chain|ring)_", and the defnames are i_platemail_,
    // i_chainmail_, i_ringmail_, so it found nothing and I concluded from that
    // nothing existed. Absence of a grep hit is not absence of the thing.
    //
    // kSoldArmour is generated from those templates: every SELL row in any
    // ARMORER list, resolved to its graphic. Anything outside it is
    // smith-crafted or looted and must not be shopped for.
    const ArmorPiece* want = nullptr;
    for (const ArmorPiece& a : kArmorPieces) {
        if (!GraphicIsAny(a.graphic, kSoldArmour,
                          sizeof(kSoldArmour) / sizeof(kSoldArmour[0])))
            continue;                                  // nobody stocks it
        if (!MayWear(a, obs)) continue;
        const u8 layer = client.ItemEquipLayer(a.graphic);
        if (!layer) continue;
        if (client.EquippedGraphicAt(layer)) continue;   // slot already filled
        // ALREADY BOUGHT ONE, AND IT IS STILL IN THE PACK.
        //
        // The wear pass at the top of this goal runs FIRST and wears anything
        // carried that fits. So a piece still sitting in the pack when we get
        // here is one this character could not put on -- and buying a second
        // copy cannot change that. Nothing checked, so the empty slot was
        // read as "buy one" on every single visit:
        //
        //   11,645 "no piece for an empty slot ... buying" lines across the
        //   recorded runs -- 5,140 on Cassia alone, 2,914 Nessa, 2,475
        //   Maribel -- and Corwyn's backpack holding SIX i_shield_heater
        //   (reqStr 90) bought by a character with STR 56, none ever worn.
        //
        // The M7 disposal order is what such a piece is for now: it will be
        // offered to players and otherwise banked, rather than restocked.
        if (client.FindBackpackItemByGraphic(a.graphic)) {
            LogLine("gear: already carrying 0x%04X and it is still not worn "
                    "-- not buying another", a.graphic);
            continue;
        }
        if (!want || a.armor > want->armor) want = &a;
    }
    if (!want) {
        // AND REST, rather than reporting success and being re-picked. There
        // is nothing to buy and nothing has changed, so an immediate second
        // look asks the same question of the same pack -- the "goal that
        // achieved nothing" family again. UPGRADE_GEAR completed with
        // progress=0 and was re-picked 60 ms later in run_m7/v_Corwyn; the
        // cooldown is what makes the answer stick until something moves.
        LogLine("gear: every slot this class may fill is filled");
        planner_.Cooldown(GoalKind::UpgradeGear, obs.nowMs + kGearCooldownMs);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // The armorer's own list is leather (VENDOR_S_ARMORER_LEATHER) and the
    // tailor carries some too, which is the fallback for a town without one.
    const bool haveArmorer = client.NearestShopkeeperWithTrade("armorer") != 0;
    LogLine("gear: no piece for an empty slot in the pack -- buying 0x%04X "
            "(armor %d, needs str %d) from a %s",
            want->graphic, want->armor, want->reqStr,
            haveArmorer ? "armorer" : "tailor");
    if (haveArmorer) {
        BuyScrollFrom(client, obs, "armorer", wm::Service::Blacksmith,
                      want->graphic, false, 1, "a piece of armour",
                      GoalKind::UpgradeGear);
    } else {
        BuyScrollFrom(client, obs, "tailor", wm::Service::Tailor,
                      want->graphic, false, 1, "a piece of armour",
                      GoalKind::UpgradeGear);
    }
    return false;
}

// ---------------------------------------------------------------------------
// MINING.
//
// A miner had no goal at all. GatherLogs wants an axe and a tree, Fish wants a
// pole and water, and ore had neither -- so Corran carried a pickaxe for a
// whole session, picked TRAIN_AT_NPC three times and mined nothing. "add
// mining goal so corran can mine" (project owner, 2026-08-29).
//
// The mechanics are the shard's, from skills/skill45_mining.scp:
//   FLAGS=skf_gather, RANGE=2, PROMPT_MSG="Where would you like to mine?"
//   ON=@PreStart refuses while FINDLAYER.layer_horse -- no mining mounted
//   ON=@PreStart reads SRC.WEAPON.USESCUR -- the pickaxe must be WIELDED,
//     not merely carried, and it wears out with use
//
// WHICH TILE IS ROCK IS THE SERVER'S JUDGEMENT, AND WE NOW MIRROR IT INSTEAD
// OF PROBING FOR IT. Swing-and-learn sounded humble but was wrong at both
// ends: the "unwalkable == rock" pre-filter nominated the RIVER beside the
// Minoc bridge (water is unwalkable too), and the ring-of-8 fallback swung at
// roads mid-journey -- 78 identical "Try mining elsewhere" refusals at
// (2540,503) in one run. The engine's actual gate is knowable from source:
// CheckNaturalResource(pt, IT_ROCK) demands the struck tile ITSELF be
// rock-typed land or a t_rock static (Source-X CWorldMap.cpp:52,721,781-785),
// where "rock-typed" is the shard's own [TYPEDEF t_rock] tables -- so
// Client::RockAt reads the same muls against the same tables, and refusals
// are kept only as per-tile memory (a vein rolled mr_nothing stays barren for
// REGEN=10h, core/regionresources.scp:40-42).
bool Runner::DoMine(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    if (obs.WeightFraction() >= 0.95) {
        LogLine("mine: pack full at %.0f%%", obs.WeightFraction() * 100.0);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // THE PICKAXE MUST BE IN HAND. skill45_mining reads SRC.WEAPON, so one
    // sitting in the backpack mines nothing and explains nothing.
    u32 pick = 0;
    for (int i = 0; i < 2 && !pick; ++i) {
        if (client.EquippedGraphicAt(kLayerHand1) == kMinePickaxe[i])
            pick = client.EquippedAtLayer(kLayerHand1);
        else if (client.EquippedGraphicAt(kLayerHand2) == kMinePickaxe[i])
            pick = client.EquippedAtLayer(kLayerHand2);
    }
    if (!pick) {
        const u32 inPack = FindAny(client, kMinePickaxe, 2);
        if (!inPack) {
            LogLine("goal_failed=MINE reason=\"no pickaxe carried\"");
            planner_.Cooldown(GoalKind::Mine, obs.nowMs + kNoOreCooldownMs);
            planner_.Finish(false, "no pickaxe", obs.nowMs);
            return false;
        }
        LogLine("mine: taking the pickaxe in hand -- mining reads SRC.WEAPON");
        client.ActionEquip(inPack, kLayerServerChooses);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Answer the cursor the skill arms -- like a classic client click on what
    // the player SEES. A cave floor is a STATIC and a static reply carries
    // its graphic; mountainside is rock land and gets a ground reply. Both
    // carry the ROCK'S OWN z (NearestMiningSpot filled it), not the
    // character's: the face of a mountain is well above the boots of the
    // miner striking it, and the engine range check is against m_Act_p as
    // sent (CCharSkill.cpp:1424-1441).
    if (mineCursorPending_) {
        if (client.TargetActive()) {
            if (mineGraphic_ != 0) {
                client.ActionTargetStatic(mineX_, mineY_, mineZ_,
                                          mineGraphic_);
            } else {
                client.ActionTargetGround(mineX_, mineY_, mineZ_);
            }
            mineCursorPending_ = false;
            // The attempt is timed from the answer, not the double-click:
            // the strokes start now.
            mineSwungMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + kMinePollMs;
            return false;
        }
        if (obs.nowMs - mineSwungMs_ > 6000) mineCursorPending_ = false;
        return false;
    }

    // A SWING IS IN FLIGHT: WAIT FOR THE VERDICT, DO NOT SWING OVER IT.
    // Mining is multi-stroke: SKTRIG_START rolls 2-6 strokes
    // (CCharSkill.cpp:1463) at DELAY=1.6s each (skill45_mining.scp), so an
    // attempt legitimately takes up to ~10s of silence before ANY journal
    // line. The old 2.5s re-swing restarted the skill mid-strokes every
    // single time -- an attempt never once ran to completion, which is why
    // whole sessions produced swings and no ore.
    if (mineSwungMs_ != 0) {
        // Verdicts about the TILE: dead-list it and move the aim.
        static const char* kBadTile[] = {
            "try mining elsewhere",           // MINING_1: not rock, or the
                                              //   vein rolled mr_nothing
            "there is nothing here to mine",  // MINING_2: vein exhausted
            "there is no ore here to mine",   // MINING_3
            "try mining in rock",             // MINING_4
            "that is too far away",           // positioning slip; re-aim
            // MINING_LOS (CCharSkill.cpp:1442-1444): the last gate before
            // the resource roll. Seen live at the Minoc mine mouth: the
            // nearest rock by ring order was the cliff at z=34 over the z=0
            // path, and its top is not visible from its foot. Dead-list it
            // and the z-aware picker finds the cave floor instead.
            "no line of sight to that location",
        };
        bool resolved = false;
        for (const char* line : kBadTile) {
            if (client.JournalSaidSince(line, mineJournalMs_)) {
                LogLine("mine: %d,%d refused (\"%s\") -- marking it dead",
                        mineX_, mineY_, line);
                deadTargets_.emplace_back(mineX_, mineY_);
                if (deadTargets_.size() > 32)
                    deadTargets_.erase(deadTargets_.begin());
                mineRoam_ = true;   // this vein is done; wander before rescanning
                resolved = true;
                break;
            }
        }
        // @Fail is a verdict about the SKILL ROLL, not the tile ("You loosen
        // some rocks but fail to find any useable ore.",
        // skill45_mining.scp:43). The tile stays live -- dead-listing it here
        // is how a low-skill miner talks himself out of a perfectly good
        // vein. Swing at it again.
        if (!resolved &&
            client.JournalSaidSince("fail to find any useable ore",
                                    mineJournalMs_)) {
            LogLine("mine: failed the roll at %d,%d -- that is how gains "
                    "happen; striking again", mineX_, mineY_);
            resolved = true;
        }
        if (!resolved && client.JournalSaidSince("You put", mineJournalMs_)) {
            LogLine("mine: ORE at %d,%d", mineX_, mineY_);
            state_.memory.NoteResource("ore", mineX_, mineY_, mineZ_, true,
                                       obs.nowMs);
            planner_.NoteProgress();
            resolved = true;
        }
        if (!resolved) {
            if (obs.nowMs - mineSwungMs_ < kMineResolveMs) {
                nextActionMs_ = obs.nowMs + kMinePollMs;
                return false;   // still stroking -- leave the skill alone
            }
            LogLine("mine: no verdict from %d,%d in %ds -- moving on",
                    mineX_, mineY_, (int)(kMineResolveMs / 1000));
        }
        mineSwungMs_ = 0;
    }

    // NEVER PICK TARGETS MID-JOURNEY. The bridge screenshots came from
    // exactly this: travel to the mine was still walking its legs when the
    // old code scanned from wherever the character happened to be and stopped
    // him mid-span to swing at the river. En route, there is nothing for this
    // goal to do but wait.
    if (client.TravelBusy() || client.GotoBusy()) return false;

    // GO TO THE ROCK FIRST. "first he needs to go to mining area" (project
    // owner, 2026-08-29).
    //
    // Being at WORK is a district; being able to MINE is a tile. atWorkSite
    // accepts 45 tiles from a resource area's edge, which is the right test
    // for "is this a mining town" and far too loose for "can I swing here":
    // Corwyn stood in Minoc hitting ordinary ground and was told "Try mining
    // elsewhere" every time. Walk into the area, then swing.
    {
        const i32 d = client.DistanceToResource(wm::ResourceKind::Mining);
        if (d > kMineReach) {
            if (++mineTrips_ > kMaxMineTrips) {
                LogLine("goal_failed=MINE reason=\"could not reach a mining "
                        "area in %d trips\"", kMaxMineTrips);
                planner_.Cooldown(GoalKind::Mine, obs.nowMs + kNoOreCooldownMs);
                planner_.Finish(false, "no mining area reachable", obs.nowMs);
                mineTrips_ = 0;
                return false;
            }
            LogLine("mine: the ore is %d tiles off -- walking into the mining "
                    "area first (trip %d)", d, mineTrips_);
            client.TravelToResource(wm::ResourceKind::Mining);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        mineTrips_ = 0;
    }

    // FIND GENUINE ROCK, walk to its side if need be, and strike it.
    //
    // The resource area's recorded position is a region CENTROID and a
    // centroid can be anything -- Corwyn's was a wooden bridge over water, and
    // the owner sent a screenshot of him standing on it swinging at planks.
    // Being "in the mining area" is not being at the rock, and "unwalkable" is
    // not rock (the river was unwalkable too). NearestMiningSpot mirrors the
    // server's own rock test (see Client::RockAt) and carries the dead list so
    // a refused tile is never nominated twice -- the previous build re-picked
    // (2540,503) 78 times because the primary path never consulted it.
    // ROAM THE MINE, DON'T CAMP ITS MOUTH. "there is more space in the mine
    // dont only mine at the entrance" (project owner, 2026-08-29). Scanning
    // nearest-first from the character's boots always re-nominates the rock
    // beside the last vein, so a miner would chew along the entrance one tile
    // at a time. When a vein dies, jitter the scan origin up to 8 tiles (the
    // low bits of the clock are noise enough for a stroll, and the dead-list
    // already stops him returning to worked-out ground); the walk to the new
    // stand tile is the wander itself. A jitter that lands where no rock is
    // found falls back to scanning from where he stands.
    i32 scanX = obs.x, scanY = obs.y;
    if (mineRoam_) {
        scanX += (i32)(obs.nowMs % 17) - 8;
        scanY += (i32)((obs.nowMs / 17) % 17) - 8;
        mineRoam_ = false;
    }
    Client::MiningSpot spot;
    if (!client.NearestMiningSpot(scanX, scanY, obs.z, kMineScanRadius, &spot,
                                  &deadTargets_) &&
        !client.NearestMiningSpot(obs.x, obs.y, obs.z, kMineScanRadius, &spot,
                                  &deadTargets_)) {
        LogLine("mine: no mineable rock within %d tiles of %d,%d -- moving on",
                kMineScanRadius, obs.x, obs.y);
        deadTargets_.clear();
        planner_.Cooldown(GoalKind::Mine, obs.nowMs + kNoOreCooldownMs);
        planner_.Finish(false, "no rock in reach", obs.nowMs);
        return false;
    }

    // STRIKE ONLY WHEN ACTUALLY BESIDE IT. The engine wants the target at
    // least 1 and at most RANGE=2 tiles off (CCharSkill.cpp:1432-1441,
    // skill45_mining.scp RANGE=2); further out, walk to the vetted stand tile
    // and let the NEXT tick re-measure from wherever the walk actually ended.
    const i32 toRock = TileDist(obs.x, obs.y, spot.rockX, spot.rockY);
    if (toRock < 1 || toRock > 2) {
        LogLine("mine: the rock is at %d,%d, %d tiles off -- standing at "
                "%d,%d to reach it", spot.rockX, spot.rockY, toRock,
                spot.standX, spot.standY);
        client.ActionGoto(spot.standX, spot.standY);
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    mineX_ = spot.rockX;
    mineY_ = spot.rockY;
    mineZ_ = spot.rockZ;
    mineGraphic_ = spot.rockGraphic;
    LogLine("mine: striking the rock at %d,%d,%d (%s, %d tiles)",
            mineX_, mineY_, (int)mineZ_,
            mineGraphic_ ? "cave floor" : "rock face", toRock);
    mineJournalMs_ = client.JournalNowMs();
    mineSwungMs_ = obs.nowMs;
    mineCursorPending_ = true;
    // USE THE PICKAXE, DO NOT INVOKE THE SKILL.
    //
    // ActionUseSkill(kMining) is answered by Sphere with "There is no such
    // skill. Please tell support you saw this" -- thirty times in one
    // session, which is what "swinging" amounted to. A gathering skill is
    // not requested by id; it is what the TOOL does. Double-clicking the
    // pickaxe is what arms the "Where would you like to mine?" cursor that
    // skill45_mining.scp declares, and DoFish has always done exactly this
    // with the pole.
    client.ActionUseObject(pick);
    nextActionMs_ = obs.nowMs + 2500;
    return false;
}

// ---------------------------------------------------------------------------
// TAMING.
//
// "add taming goal to cassia" (project owner, 2026-08-29). A tamer had no goal
// either: Cassia spent a whole session doing nothing but EXPLORE, because
// every other need she had was blocked and taming was not something she could
// want.
//
// skills/skill35_taming.scp: SKILL 35, PROMPT_MSG="Tame which animal?", so it
// targets a MOBILE and ActionUseSkill answers the cursor itself.
//
// WHAT to tame is read from the shard rather than guessed:
// data/revolution_creatures.tsv carries each chardef's TAMING requirement, and
// 109 of the 450 creatures are tamable at all. The hardest one this character
// can actually manage is the right pet -- a Rat needs 0.9, a Sheep 11.1 -- and
// anything above its skill is refused all day for nothing.
//
// ScanMobiles, not ScanHostiles: the latter excludes blue and green BY DESIGN,
// and a sheep is innocent. Innocent is exactly what a tamer wants.
bool Runner::DoTameAnimal(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;
    LoadSeededCreatureDanger(client.DataDir());

    const double mySkill = obs.SkillTenths(rules::kTaming) / 10.0;

    std::vector<Client::HostileHit> nearby;
    client.ScanMobiles(12, nearby);

    u32 best = 0;
    double bestReq = -1.0;
    std::string bestName;
    for (const Client::HostileHit& m : nearby) {
        if (m.name.empty()) continue;
        const double req = SeededTamingFor(m.name);
        if (req < 0.0) continue;            // not a tamable creature at all
        if (req > mySkill) continue;        // beyond this character today
        if (best == 0 || req > bestReq) {   // the best it can actually manage
            best = m.serial; bestReq = req; bestName = m.name;
        }
    }

    if (!best) {
        // GO WHERE THE ANIMALS ARE. Standing down was wrong: Cassia lives in
        // Britain, a city, and cities have almost nothing tamable in them. She
        // logged "nothing in sight" once and then spent the session BLOCKED on
        // a three-minute cooldown, twelve times over, while sheep grazed
        // outside the walls.
        //
        // A miner travels to ore and a fisher to water; a tamer travels to
        // livestock. The pastures are the ones read out of the world save for
        // the bandage chain -- 246 sheep on map 0, the three real flocks being
        // the farmland north-east of Yew.
        if (client.TravelBusy()) return false;
        if (++tameTrips_ <= kMaxTameTrips) {
            static const struct { i32 x, y; } kPastures[] = {
                {572, 1098}, {669, 943}, {669, 1175},
            };
            const int which = (tameTrips_ - 1) % 3;
            LogLine("tame: nothing tamable here (Taming %.1f) -- walking out to "
                    "the pasture at %d,%d (trip %d)", mySkill,
                    kPastures[which].x, kPastures[which].y, tameTrips_);
            travelInFlight_ = client.TravelToPoint(kPastures[which].x,
                                                   kPastures[which].y, 8,
                                                   "pasture");
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        LogLine("goal_failed=TAME_ANIMAL reason=\"nothing tamable after %d "
                "trips to the pastures (Taming %.1f)\"", tameTrips_ - 1, mySkill);
        planner_.Cooldown(GoalKind::TameAnimal, obs.nowMs + kNoPetCooldownMs);
        planner_.Finish(false, "nothing tamable in reach", obs.nowMs);
        tameTrips_ = 0;
        return false;
    }
    tameTrips_ = 0;

    i32 tx = 0, ty = 0; i8 tz = 0;
    if (client.MobilePosition(best, &tx, &ty, &tz)) {
        const i32 d = TileDist(obs.x, obs.y, tx, ty);
        if (d > 2) {
            LogLine("tame: '%s' is %d tiles away -- walking up",
                    bestName.c_str(), d);
            travelInFlight_ = client.TravelToEntity(best, 2);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
    }

    LogLine("tame: trying '%s' (needs Taming %.1f, have %.1f)",
            bestName.c_str(), bestReq, mySkill);
    client.ActionUseSkill(rules::kTaming, best);
    planner_.NoteAttempt(obs.nowMs);
    // DELAY=2.0 and taming usually takes several attempts.
    nextActionMs_ = obs.nowMs + 6000;
    return false;
}

// ---------------------------------------------------------------------------
// EXPLORING.
//
// "bots shouldnt be idle unless its state specifically" (project owner). This
// is what a character does instead of standing still, and it is not filler.
//
// Nearly every blocked need in this project is blocked for want of knowing
// WHERE something is: "no known supplier of a tongs", "carrying its own output
// with nobody known to buy it", "no 'tinker' reachable". Bruin finished a
// 25-minute session with session_summary places=1 -- he had seen one location
// all day, which is precisely why he knew no supplier for any of the three
// tools he was short of, and why he idled through 85% of his picks.
//
// So the fallback goes and looks at an unvisited shop, and reads the paperdolls
// of whoever is standing in it. That is how NearestMobileWithTrade and the
// supplier memory get anything to work with.
bool Runner::DoExplore(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    // Arrived somewhere: LOOK. A place walked to and not looked at teaches
    // nothing, and the scan is the entire point of the errand.
    if (travelInFlight_ && !client.TravelBusy()) {
        travelInFlight_ = false;
        LogLine("explore: arrived at %d,%d -- reading who is here", obs.x, obs.y);
        client.ActionScanMobiles();
        // RECORD IT BY ID, which is what TravelToUnexploredPlace matches
        // against. Storing an empty name would leave the place forever
        // unvisited and send the character back to it on the next tick.
        state_.memory.NotePlace("explored", exploreTarget_.c_str(), obs.x,
                                obs.y, obs.z, obs.nowMs);
        exploreTarget_.clear();
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 3000;
        return true;   // one place per outing; the next tick re-decides
    }
    if (client.TravelBusy()) return false;

    // Somewhere with a service, that this character has not been to. The
    // places it already knows come from its own memory, so two characters
    // explore differently and a character never re-walks its own ground.
    std::vector<std::string> seen;
    for (const KnownPlace& p : state_.memory.Places()) {
        if (!p.name.empty()) seen.push_back(p.name);
    }
    if (!client.TravelToUnexploredPlace(seen, &exploreTarget_)) {
        LogLine("explore: nowhere new to go (%s) -- standing down",
                client.TravelFailureText());
        planner_.Cooldown(GoalKind::Explore, obs.nowMs + kExploredAllCooldownMs);
        planner_.Finish(false, "nowhere unexplored", obs.nowMs);
        return false;
    }
    travelInFlight_ = true;
    LogLine("explore: nothing else to do, so going to '%s' -- somewhere new "
            "(%zu place(s) known so far)", exploreTarget_.c_str(), seen.size());
    nextActionMs_ = obs.nowMs + 2500;
    return false;
}

// ---------------------------------------------------------------------------
// MAKING BANDAGES.
//
// "for warrior it should create its own bandage on up to 50-100, we know the
// crafting bandages" and "wool can be obtained from sheeps" (project owner,
// 2026-08-29).
//
// This is the answer to the deadlock that cost Kaelen a session: hungry, so no
// HP regeneration; wounded, so under the hunting bar; no bandages, so HEAL was
// blocked; and no gold, so he could not buy any. Bandages ARE sold -- healers
// and vets stock them -- but a fighter with an empty purse cannot use a shop,
// and a sheep costs nothing.
//
// The chain is five gestures and they are all the same gesture: use one thing
// on another. See the constants above for the engine citation behind each.
// Every stage is skipped if its output is already in the pack, so a character
// who loots cloth walks straight to the last step.
bool Runner::DoMakeBandages(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    if (obs.bandages >= kBandagesWanted) {
        LogLine("bandages: %d is enough to fight on", obs.bandages);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // Scissors do every step, so without them there is no chain at all. They
    // are in every starter kit as ITEMNEWBIE and so survive death, which is
    // the point -- this goal exists for characters who have just lost
    // everything else.
    const u32 scissors = client.FindBackpackItemByGraphic(kScissorsGraphic);
    if (!scissors) {
        // GO AND BUY A PAIR. "fencer we added scissor no? if he has none he
        // should go buy one" (owner, 2026-08-29) -- and he is right that
        // giving up was the wrong answer.
        //
        // Scissors are in every starter kit as ITEMNEWBIE now, but a character
        // created before that change has none and can never get any, which is
        // exactly Kaelen: MAKE_BANDAGES fired three times and failed three
        // times on "no scissors" while he idled through the rest of the
        // session. A tailor sells them (VENDOR_S_TAILOR, and the tinker too),
        // and they are cheap.
        //
        // If the purse cannot even manage that, THEN stand down -- but say so
        // as a money problem, which is the thing that can actually change.
        if (obs.gold >= kScissorsMoney) {
            BuyScrollFrom(client, obs, "tailor", wm::Service::Tailor,
                          kScissorsGraphic, false, 1, "a pair of scissors",
                          GoalKind::MakeBandages);
            return false;
        }
        LogLine("goal_failed=MAKE_BANDAGES reason=\"no scissors and only %d "
                "gold to buy a pair with\"", obs.gold);
        planner_.Cooldown(GoalKind::MakeBandages, obs.nowMs + kNoBandageCooldownMs);
        planner_.Finish(false, "no scissors and no money", obs.nowMs);
        return false;
    }

    // 0. LOOTED CLOTHING -> BANDAGES. The cheapest of the lot: no sheep, no
    //    wheel, no loom, and the garment came off something the character had
    //    to kill anyway. A shirt yields 8, a surcoat 14.
    //
    //    Only what is in the PACK. FindBackpackItemByGraphic never returns a
    //    worn item, so a character cannot cut the clothes off its own back --
    //    and the engine would refuse anyway, since CanUse(item, true) requires
    //    CanMoveItem.
    if (const u32 rag = FindAny(client, kCuttableClothing,
                                sizeof(kCuttableClothing) /
                                    sizeof(kCuttableClothing[0]))) {
        LogLine("bandages: cutting up looted clothing (%d bandages so far, "
                "want %d)", obs.bandages, kBandagesWanted);
        client.ActionUseItemOn(scissors, rag);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // 1. CLOTH -> BANDAGES. One bandage per cloth, so this is the step that
    //    actually pays and it runs before anything else.
    if (const u32 cloth = client.FindBackpackItemByGraphic(kClothGraphic)) {
        LogLine("bandages: cutting cloth (%d bandages so far, want %d)",
                obs.bandages, kBandagesWanted);
        client.ActionUseItemOn(scissors, cloth);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // 2. BOLT -> CLOTH.
    if (const u32 bolt = client.FindBackpackItemByGraphic(kClothBoltGraphic)) {
        LogLine("bandages: cutting a bolt of cloth into cloth");
        client.ActionUseItemOn(scissors, bolt);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // 3. YARN OR THREAD -> LOOM -> BOLT.
    u32 spun = client.FindBackpackItemByGraphic(kYarnGraphic);
    if (!spun) spun = client.FindBackpackItemByGraphic(kThreadGraphic);
    if (spun) {
        const u32 loom = client.FindWorldItemByGraphic(kLoomGraphic, 10);
        if (!loom) {
            LogLine("bandages: carrying yarn but no loom in sight -- going to "
                    "a tailor, where the looms are");
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToService(
                    wm::Service::Tailor, HomeOrNearest(state_.homeCity));
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        LogLine("bandages: weaving yarn into cloth at a loom");
        client.ActionUseItemOn(spun, loom);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // 4. WOOL -> SPINNING WHEEL -> YARN.
    if (const u32 wool = client.FindBackpackItemByGraphic(kWoolGraphic)) {
        const u32 wheel = client.FindWorldItemByGraphic(kSpinWheelGraphic, 10);
        if (!wheel) {
            LogLine("bandages: carrying wool but no spinning wheel in sight -- "
                    "going to a tailor");
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToService(
                    wm::Service::Tailor, HomeOrNearest(state_.homeCity));
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        LogLine("bandages: spinning wool into yarn at a wheel");
        client.ActionUseItemOn(wool, wheel);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // 5. A SHEEP -> WOOL. The only free step, and the start of everything.
    const u32 sheep = client.NearestMobileWithBody(kSheepBody, 12);
    if (sheep) {
        i32 sx = 0, sy = 0; i8 sz = 0;
        if (client.MobilePosition(sheep, &sx, &sy, &sz)) {
            const i32 d = TileDist(obs.x, obs.y, sx, sy);
            if (d > 1) {
                LogLine("bandages: a sheep %d tiles away -- walking up to shear "
                        "it", d);
                travelInFlight_ = client.TravelToEntity(sheep, 1);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        LogLine("bandages: shearing a sheep for wool");
        client.ActionUseItemOn(scissors, sheep);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // NO SHEEP IN SIGHT. Go where they are.
    //
    // The pastures are read from the world save rather than assumed: of 246
    // sheep on map 0, the three real flocks are at 572,1098 (15), 669,943 (13)
    // and 669,1175 (11), which is the farmland north-east of Yew. The rest are
    // ones and twos wandering. The owner's recollection was Jhelom and
    // Britain; the save says Yew, and the save is what the bot has to walk to.
    if (client.TravelBusy()) return false;
    if (++bandageTrips_ > kMaxBandageTrips) {
        LogLine("goal_failed=MAKE_BANDAGES reason=\"no sheep found after %d "
                "trips to the pastures\"", bandageTrips_ - 1);
        planner_.Cooldown(GoalKind::MakeBandages, obs.nowMs + kNoBandageCooldownMs);
        planner_.Finish(false, "no sheep reachable", obs.nowMs);
        bandageTrips_ = 0;
        return false;
    }
    static const struct { i32 x, y; } kPastures[] = {
        {572, 1098}, {669, 943}, {669, 1175},
    };
    const int which = (bandageTrips_ - 1) % 3;
    LogLine("bandages: no sheep in sight -- walking to the pasture at %d,%d "
            "(trip %d)", kPastures[which].x, kPastures[which].y, bandageTrips_);
    travelInFlight_ =
        client.TravelToPoint(kPastures[which].x, kPastures[which].y, 6, "pasture");
    nextActionMs_ = obs.nowMs + 2500;
    return false;
}

// ---------------------------------------------------------------------------
// FILLING THE BOOK.
//
// "we need to add that mages tries to fill their book, make it full spell
// book" (project owner, 2026-08-29). A mage's spellbook is equipment, and this
// shard demonstrated why it is not optional: Voris carried Magery 50.0 and
// asked for Create Food twenty-six times in one session, being told every time
// "The spell is not in your spellbook".
//
// WHERE SCROLLS COME FROM ON THIS SHARD -- read from its own vendor and loot
// tables, written up in docs/REVOLUTION_GAMEPLAY_TRUTH.md 3.5:
//
//   circles 1-4   any mage shop, but as random_first_circle .. fourth, so the
//                 spell that arrives is NOT chosen
//   circle 5, part of 6, and Resurrection   a scribe, by name
//   circles 7-8   nobody sells them: dungeon chests and monster loot only
//
// Two consequences this goal is built around. A mage cannot buy its way to a
// full book, so the goal aims at a working kit rather than completeness. And
// because purchases are random, the honest loop is buy-then-look, not
// pick-then-buy -- which is also how a player fills a book on this shard.
//
// The book is READ, never assumed: opening it and counting what the server
// sends back is the only truthful source for what this character can cast.
bool Runner::DoFillSpellbook(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    // NO BOOK IS A DIFFERENT PROBLEM FROM AN EMPTY ONE.
    if (obs.spellbookSerial == 0) {
        if (obs.gold < kSpellbookMoney) {
            LogLine("spellbook: no book and %d gold -- a book costs about %d, "
                    "so go and earn first", obs.gold, kSpellbookMoney);
            planner_.Cooldown(GoalKind::FillSpellbook,
                              obs.nowMs + kNoSpellbookCooldownMs);
            planner_.Finish(false, "no book and no money", obs.nowMs);
            return false;
        }
        // A spellbook itself: the mage shop sells them (SELL=i_spellbook),
        // and there is nothing to choose, so no reason to prefer a scribe.
        BuyScrollFrom(client, obs, "mage", wm::Service::Mage, kSpellbookGraphic,
                      false, 1, "a spellbook", GoalKind::FillSpellbook);
        return false;
    }

    // DID THE LAST SCROLL GO IN?
    //
    // Dropping a scroll on the book is not the same as the book taking it. A
    // spell the book ALREADY HOLDS is refused, and the server puts the scroll
    // straight back in the backpack -- which arrives as
    //   move_item server_failure ("item landed in a different container")
    // because the 0x25 names the pack, not the book. Nothing remembered that,
    // so the very next tick found the same scroll and offered it again:
    //   "spellbook: adding scroll 0x1F40 to the book (14 spells so far)"
    // repeated every five seconds for two minutes and forty seconds of a
    // fourteen-minute life, the count never once moving off 14. The engine's
    // own backstop eventually called it -- "goal_spinning=PRACTICE_SKILL ...
    // completed 5 times in a row with progress 0" -- which bounded the waste
    // without stopping it.
    //
    // The book's own count is the honest witness: if it did not rise, that
    // graphic is one this book will not take, so stop offering it.
    if (scrollOfferedGraphic_ != 0) {
        if (obs.spellsKnown > spellsBeforeOffer_) {
            planner_.NoteProgress();      // a REAL add, unlike the old
                                          // unconditional call below
        } else {
            LogLine("spellbook: the book would not take scroll 0x%04X (still "
                    "%d spells) -- it already knows that one; not offering it "
                    "again", scrollOfferedGraphic_, obs.spellsKnown);
            bool known = false;
            for (u16 r : scrollBookRefused_)
                if (r == scrollOfferedGraphic_) { known = true; break; }
            if (!known) scrollBookRefused_.push_back(scrollOfferedGraphic_);
        }
        scrollOfferedGraphic_ = 0;
    }

    // LOOK IN IT BEFORE BUYING ANYTHING.
    //
    // ContainerItemCount is only meaningful once the server has sent the
    // contents, which happens when the book is opened. Treating an unopened
    // book as an empty one would send the character shopping for spells it
    // already owns.
    if (!spellbookOpened_) {
        LogLine("spellbook: opening the book to see what is already in it");
        client.ActionUseObject(obs.spellbookSerial);
        spellbookOpened_ = true;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    if (obs.spellsKnown >= kSpellbookComfortableRuntime) {
        LogLine("spellbook: %d spells is a working book -- the rest are scribe "
                "and dungeon work, not shopping", obs.spellsKnown);
        planner_.Finish(true, "book is serviceable", obs.nowMs);
        return true;
    }

    // A SCROLL IN THE PACK BELONGS IN THE BOOK.
    //
    // Looted scrolls arrive here too, which is the only route to circles 7-8,
    // so this runs before any purchase.
    for (u16 g = kFirstScrollGraphic; g <= kLastScrollGraphic; ++g) {
        bool refused = false;
        for (u16 r : scrollBookRefused_) if (r == g) { refused = true; break; }
        if (refused) continue;            // this book already knows that spell
        const u32 scroll = client.FindBackpackItemByGraphic(g);
        if (!scroll) continue;
        LogLine("spellbook: adding scroll 0x%04X to the book (%d spells so far)",
                g, obs.spellsKnown);
        client.ActionMoveItem(scroll, 1, obs.spellbookSerial);
        // Progress is claimed ABOVE, next tick, and only if the count rose.
        // Claiming it here said the goal was working while it achieved
        // nothing, which is exactly what the anti-spin backstop exists to
        // catch.
        scrollOfferedGraphic_ = g;
        spellsBeforeOffer_ = obs.spellsKnown;
        // Re-read the book after the drop rather than assuming it took.
        spellbookOpened_ = false;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // NOTHING TO ADD, SO BUY. Circles 1-4 only, and random at that.
    if (obs.gold < kScrollMoney) {
        LogLine("spellbook: %d spells and %d gold -- too poor to buy scrolls, "
                "standing down to earn", obs.spellsKnown, obs.gold);
        planner_.Cooldown(GoalKind::FillSpellbook,
                          obs.nowMs + kNoSpellbookCooldownMs);
        planner_.Finish(false, "no money for scrolls", obs.nowMs);
        return false;
    }
    // THE SCRIBE FIRST, BECAUSE ONLY THE SCRIBE LETS US CHOOSE.
    //
    // A mage shop stocks random_first_circle .. random_fourth_circle, so it
    // sells a lottery ticket: the spell that arrives is not chosen and may
    // well be one the book already holds. A scribe stocks 44 named scrolls, so
    // a spell the character actually lacks can be asked for by name.
    //
    // "buy new spells not on the book" is therefore a scribe errand, and this
    // shard has 19 of them standing in mage shops
    // (revolution/revolution_scribe_shops.scp) -- the same building, so
    // preferring one costs no extra walking.
    //
    // The mage shop stays as the fallback for a town with no scribe: a random
    // scroll is worth more than no scroll.
    if (scribeExhausted_) {
        BuyScrollFrom(client, obs, "mage", wm::Service::Mage, 0, true, 1,
                      "a scroll", GoalKind::FillSpellbook);
    } else {
        BuyScrollFrom(client, obs, "scribe", wm::Service::Scribe, 0, true, 1,
                      "a spell this book lacks", GoalKind::FillSpellbook);
    }
    return false;
}

bool Runner::DoPracticeSkill(Client& client, const Observation& obs) {
    const int skillId = obs.wantPracticeSkill;
    if (skillId < 0) return true;
    if (client.ActionBusy()) return false;

    const i32 have = obs.SkillTenths(skillId);

    // MAGERY IS RAISED BY CASTING, WITH OR WITHOUT A FOE.
    //
    // Owner's rule, and the spell table agrees: [SPELL 2] s_create_food is
    // FLAGS=spellflag_playeronly with no targ flag at all
    // (runtime/scripts/spells/spells_magery.scp:36), so it is cast at nobody.
    // Four mana, MAGERY 10.0 to attempt. It is the honest practice spell: no
    // target to pick wrong, no harm flag to make a criminal of the caster, and
    // what it produces is FOOD -- which this character also needs and has no
    // other way to get.
    //
    // Deliberately not a combat spell. Practising Magery must not be a way to
    // start fights the life did not choose.
    if (skillId == rules::kMagery) {
        // And the same capability check the food goal needed. Practising by
        // casting a spell the character does not own burns the session at one
        // refusal every six seconds, with mana never moving.
        if (noCreateFoodSpell_ ||
            (createFoodMark_ != 0 &&
             client.JournalSaidSince("not in your spellbook", createFoodMark_))) {
            noCreateFoodSpell_ = true;
            LogLine("practice: Create Food is not in this character's spellbook "
                    "-- Magery cannot be practised this way");
            planner_.Finish(false, "no create food spell", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        if (obs.mana < kCreateFoodMana) {
            LogLine("practice: %d mana is not enough to cast (need %d) -- "
                    "resting instead", obs.mana, kCreateFoodMana);
            nextActionMs_ = obs.nowMs + 15000;
            return true;   // meditation or time will bring it back
        }
        // CAST SOMETHING THIS CHARACTER ACTUALLY HAS. The book must be opened
        // once before it can be read; an unopened book is not an empty one.
        if (obs.spellbookSerial == 0) {
            LogLine("practice: no spellbook carried -- Magery cannot be "
                    "practised until FILL_SPELLBOOK has bought one");
            planner_.Finish(false, "no spellbook", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        if (!spellbookOpened_) {
            LogLine("practice: opening the spellbook to see what can be cast");
            client.ActionUseObject(obs.spellbookSerial);
            spellbookOpened_ = true;
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        const int spell = PickPracticeSpell(client, obs);
        if (spell < 0) {
            LogLine("practice: nothing safe to cast at myself is in this book "
                    "-- Magery cannot be practised until it holds one");
            planner_.Finish(false, "no self-safe spell in book", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        // At oneself. Every candidate is spellflag_good and playeronly, so
        // this can neither hurt the caster nor make a criminal of them.
        LogLine("practice: casting spell %d at myself to raise Magery "
                "(%.1f, mana %d)", spell, have / 10.0, obs.mana);
        createFoodMark_ = client.JournalNowMs();
        client.ActionCastSpell(spell, client.PlayerSerial());
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 6000;
        return false;
    }

    // A SELF-USE SKILL NEVER FAILS, SO IT MUST BE BOUNDED.
    //
    // Meditation always answers "You are at peace", so this goal claimed
    // progress on every single tick, never completed, and was then restored
    // next session as a KEPT objective -- "restored objective KEPT:
    // PRACTICE_SKILL (progress 18)". The planner never got another look in.
    // Voris spent a whole life meditating while holding 2 poison potions, 170
    // nightshade and a mortar, and the reason no goal change ever appeared in
    // the log is that no goal change ever happened.
    //
    // Stand down after a stretch of it and let the planner re-decide. If
    // meditating really is the best thing available it wins again immediately;
    // if there is stock to sell or a batch to brew, that now gets its turn.
    // For an alchemist in particular the training IS the crafting -- Alchemy
    // is PracticeBy::Working -- so an unbounded meditation was crowding out
    // the very activity that raises the skill it lives by.
    if (++selfPracticeRuns_ >= kSelfPracticeBeforeRethink) {
        LogLine("practice: %d turns of %s -- standing down so the planner can "
                "look at the rest of this life",
                selfPracticeRuns_, rules::SkillName(skillId));
        selfPracticeRuns_ = 0;
        planner_.Cooldown(GoalKind::PracticeSkill, obs.nowMs + 60000);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    LogLine("practice: using %s to raise it (%.1f)", rules::SkillName(skillId),
            have / 10.0);
    client.ActionUseSkill(skillId);
    planner_.NoteProgress();
    // Meditation runs for a while and the server decides when it ends. Long
    // enough that this is not a spin, short enough to notice an interruption.
    nextActionMs_ = obs.nowMs + 12000;
    return false;
}

bool Runner::DoIdle(Client& client, const Observation& obs) {
    (void)client;
    // A bounded no-op. It exists so a tick with nothing to do SAYS so rather
    // than spinning, and so the planner is never in a "no goal" state.
    nextActionMs_ = obs.nowMs + 5000;
    if (obs.nowMs - planner_.Current().startedAtMs > 15000) return true;
    return false;
}

}  // namespace uo::life
