#include "life/Runner.h"

#include "Client.h"
#include "uo/log.h"
#include "uo/builders.h"
#include "uo/faucets.h"
#include "uo/activities/acquire.h"
#include "uo/activities/gather.h"
#include "uo/activities/craft_confirm.h"
#include "uo/activities/tame.h"
#include "uo/activities/train.h"
#include "uo/activities/train_confirm.h"
#include "uo/interaction/progress.h"
#include "uo/market.h"
#include "uo/newbie_knowledge.h"
#include "uo/trade.h"
#include "uo/combat.h"
#include "uo/professions.h"
#include "uo/sphere_rules.h"
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
// Every heal potion is the same yellow bottle -- HealLess, Heal and HealGreat
// are all ID=i_bottle_yellow -- so this counts strength-blind, which is what
// a player squinting at their own pack does too.
constexpr u16 kHealPotion = 0x0F0C;

// SHIRT, TROUSERS, SHOES. Not armour and not decoration -- what a person
// wears so as not to be standing in a city in their underwear. Plain and
// cheap on purpose: a tailor's shirt is 2-24 gold and a cobbler's shoes 2-6
// (tm_vend.scp VENDOR_S_TAILOR, VENDOR_S_COBBLER).
struct ClothingPiece {
    u16         graphic;   // what to BUY when the slot is empty
    const char* item;      // defname, for the buy request
    const char* what;      // for the log
    // WHO SELLS IT FIRST. A cobbler does not stock trousers, and the first
    // live run walked to one for a pair: the seller list is tried in order,
    // so a single shared order sends every garment to the wrong counter twice
    // out of three times. Cloth to the tailor, footwear to the cobbler.
    const char* firstSeller;
    // ANYTHING THAT FILLS THE SAME SLOT COUNTS AS FILLING IT. Naming one
    // graphic per slot made the bot shop for what it was already carrying:
    // "you have shhort pants on your bag" (project owner, 2026-08-30) while
    // it walked to a cobbler for long ones. A player wears the trousers they
    // own. Zero-terminated.
    u16 alsoWorn[8];
};
constexpr ClothingPiece kBasicClothing[] = {
    {0x1517, "i_shirt_plain", "shirt",    "tailor",
     {0x1517, 0x1EFD, 0x25EA, 0, 0, 0, 0, 0}},         // plain, fancy, checkered
    {0x1539, "i_pants_long",  "trousers", "tailor",
     {0x1539, 0x152E, 0x279B, 0x1537, 0, 0, 0, 0}},    // long, short, tattsuke, kilt
    {0x170F, "i_shoes_plain", "shoes",    "cobbler",
     {0x170F, 0x170B, 0x1711, 0x170D, 0x2307, 0x2796, 0x2797, 0}},
                                                               // shoes, boots, sandals, fur, tabi
};

// Is this slot already dealt with -- worn, or in the pack ready to wear?
// Returns the pack serial when there is something to put on, 0 otherwise, and
// sets `worn` when the slot is already filled.
u32 ClothingOnHand(Client& client, const ClothingPiece& p, bool* worn) {
    *worn = false;
    for (const u16 g : p.alsoWorn) {
        if (!g) break;
        const u8 layer = client.ItemEquipLayer(g);
        if (!layer) continue;
        if (client.EquippedGraphicAt(layer)) { *worn = true; return 0; }
    }
    for (const u16 g : p.alsoWorn) {
        if (!g) break;
        if (const u32 have = client.FindBackpackItemByGraphic(g)) return have;
    }
    return 0;
}
constexpr u16 kKatana[]   = {0x13FE, 0x13FF};
// The other three weapon-school basics (kryss/club/bow) live as
// life::SchoolWeapon rows in uo/life.h / life/Identity.cpp, not here --
// see SchoolWeaponFor, used by DoReplaceEquipment below.
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
//
// 0x097B i_fish_cut_cooked is a fisher's own supper and was missing, so a
// fisher STARVED with dinner in its pack: the eat branch found nothing it
// recognised, fell through to the "cut and cook the catch" stand-down, and
// stood the errand down again every few minutes without ever eating (live
// 2026-09-02: run_gates/g_Dorvar.console.txt:584 "You are starving" with
// i_fish_cut_cooked carried, and lines 276/1313/2367/3455 standing down).
// It is TYPE=t_food on this shard
// (runtime/scripts/items/i_profession_cook_barkeep_baker.scp:113-115).
constexpr u16 kFood[]     = {0x103B, 0x1041, 0x09E9, 0x09EA, 0x098C, 0x160B,
                             0x1040, 0x160A, 0x1608, 0x09B7, 0x09C9,
                             0x09EB, 0x09F2, 0x097B};
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
// A book with no self-castable spell in it is FILL_SPELLBOOK's problem, and
// that errand needs a walk to a scribe and back before the answer changes.
// Long enough that PRACTICE_SKILL cannot keep taking the turn away from it.
constexpr i64 kNoSelfSafeSpellCooldownMs = 240000;   // four minutes
// One practice cast per six seconds -- the cadence the goal has always used,
// named here because the reagent target is derived FROM it (uo::spell::
// ExpectedPracticeCasts) rather than from a number picked by hand.
constexpr i64 kPracticeCastPeriodMs = 6000;
// An empty reagent pouch is BUY_SUPPLIES' problem and needs a walk to a mage
// shop and back before the answer changes -- the same reasoning, and the same
// figure, as the no-spell case above. Without it PRACTICE_SKILL takes the turn
// straight back and the errand that would fix it never runs.
constexpr i64 kNoReagentCooldownMs = 240000;   // four minutes
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
// How long a STAGNANT verdict (DecideRest: every errand blocked far too
// long) cools the goal it hands off from, and the TravelToRequiredPlace
// Wander sibling alongside it. Five minutes -- long enough that the same
// tick does not immediately re-report the fault, short enough that a
// genuinely freed-up errand is not left idle for the rest of the session.
constexpr i64 kStagnantCooldownMs = 5 * 60 * 1000;
// A pair of scissors is a few dozen coins from any tailor. Worth a walk.
constexpr i32 kScissorsMoney = 60;
constexpr i32 kMaxToolTrips = 3;
constexpr i64 kNoOreCooldownMs = 120000;
constexpr i64 kNoPetCooldownMs = 180000;
constexpr i32 kMaxTameTrips = 3;
// How long after the 0x98 name query the handler waits before it is allowed to
// call a spot empty. The name replies themselves time out in 500 ms
// (kMobilesNamesTimeoutMs, Client.cpp); the rest is settle for animals walking
// back into range -- a grazing flock drifts.
constexpr i64 kTameSettleMs = 2000;
// Sphere rolls 2-5 strokes per tame and @Fail is ordinary, so several tries are
// normal; this bounds the ones that never take.
constexpr i32 kMaxTameAttempts = 8;
// How many animals may refuse this character before the goal stands down.
// One refusal is an animal, not a verdict (a sheep that is already someone's
// pet says nothing about the next sheep); three is a spot that is not worth
// more of the session -- the same escalate-after-three rule the miner and
// the vendor chase use.
constexpr i32 kMaxTameRefusals = 3;

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


// --- WHERE THE SHEEP ARE ----------------------------------------------------
//
// Derived from the shard's own world save, never reasoned about: the owner's
// standing rule is that spawn locations come from the save / atlas, and this
// project has broken it four times (scenario-constants-from-evidence).
// tools/pasturegen.py reads every `[WORLDCHAR c_sheep*]` out of
// runtime/save/sphereworld.scp, clusters them at 24 tiles, drops clusters
// under four animals, and writes data/revolution_pastures.tsv.
//
// Regenerate it after any spawn edit. Like the atlas, it goes stale, and a
// stale pasture table sends a tailor on a long walk to an empty field.
struct Pasture {
    i32 x = 0, y = 0;
    i32 count = 0;
    i32 radius = 0;
};

std::vector<Pasture>& Pastures() {
    static std::vector<Pasture> table;
    return table;
}

void LoadPastures(const std::string& dataDir) {
    std::vector<Pasture>& t = Pastures();
    if (!t.empty()) return;
    std::FILE* f =
        std::fopen((dataDir + "/revolution_pastures.tsv").c_str(), "rb");
    if (!f) return;
    char line[256];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }              // header
        Pasture p;
        int mapId = 0;
        // x \t y \t map \t count \t radius \t label
        if (std::sscanf(line, "%d\t%d\t%d\t%d\t%d", &p.x, &p.y, &mapId,
                        &p.count, &p.radius) != 5)
            continue;
        if (mapId != 0) continue;   // the bots only ever play map 0
        t.push_back(p);
    }
    std::fclose(f);
}

// --- WHERE THE TAMABLE ANIMALS ARE -----------------------------------------
//
// Same rule as the pastures, wider question. tools/tamablegen.py takes every
// WORLDCHAR in the save whose chardef carries a TAMING requirement
// (data/revolution_creatures.tsv, 109 of 450), clusters it per species at 15
// tiles, and writes data/revolution_tamables.tsv with the requirement on the
// row. The pasture table stays where it is -- it is the WOOL chain's data,
// sheep only, all of it in Yew, which is not a legal home city.
struct TameCluster {
    i32 x = 0, y = 0;
    i32 count = 0;
    i32 radius = 0;
    double req = 0.0;               // the chardef's TAMING requirement
    std::string label;              // "Cow x4", for the log line
};

std::vector<TameCluster>& Tamables() {
    static std::vector<TameCluster> table;
    return table;
}

void LoadTamables(const std::string& dataDir) {
    std::vector<TameCluster>& t = Tamables();
    if (!t.empty()) return;
    std::FILE* f =
        std::fopen((dataDir + "/revolution_tamables.tsv").c_str(), "rb");
    if (!f) return;
    char line[512];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }              // header
        // x \t y \t map \t count \t radius \t label \t defname \t taming_req
        std::vector<std::string> col;
        const std::string row(line);
        usize start = 0;
        while (start <= row.size()) {
            const usize tab = row.find('\t', start);
            if (tab == std::string::npos) {
                std::string last = row.substr(start);
                while (!last.empty() &&
                       (last.back() == '\n' || last.back() == '\r'))
                    last.pop_back();
                col.push_back(last);
                break;
            }
            col.push_back(row.substr(start, tab - start));
            start = tab + 1;
        }
        if (col.size() < 8) continue;
        if (std::atoi(col[2].c_str()) != 0) continue;   // bots play map 0 only
        TameCluster c;
        c.x = std::atoi(col[0].c_str());
        c.y = std::atoi(col[1].c_str());
        c.count = std::atoi(col[3].c_str());
        c.radius = std::atoi(col[4].c_str());
        c.label = col[5];
        c.req = std::atof(col[7].c_str());
        t.push_back(c);
    }
    std::fclose(f);
}

// A BLADED ITEM, WHICH IS NOT THE SCISSORS.
//
// Source-X shears a sheep from `case IT_WEAPON_SWORD / _AXE / _FENCE /
// _MACE_SHARP / IT_CARPENTRY_CHOP` (CClientTarg.cpp:1866-1900): the sheep is
// a CHARACTER, so pItemTarg is null, and the IT_SCISSORS case at :2135 is
// reached only with an ITEM target -- it falls straight through to
// "Scissors cannot be used on that to produce anything". This shard's
// types/type_scissors.scp hooks @TargOn_Item only, so it does not change
// that either. Scissors cut the BOLT; a blade takes the wool.
//
// The list is this shard's own itemdefs, base graphic and DUPELIST flip, for
// the bladed things a bot plausibly carries: the starter dagger, the two
// knives, the two axes a gatherer owns, and the four school weapons.
constexpr u16 kBladedGraphics[] = {
    0x0F51, 0x0F52,   // i_dagger        (t_weapon_fence)
    0x13F6, 0x13F7,   // i_knife_butcher (t_weapon_fence)
    0x0EC4, 0x0EC5,   // i_knife_skinning(t_weapon_fence)
    0x0F43, 0x0F44,   // i_hatchet       (t_weapon_axe)
    0x0F49, 0x0F4A,   // i_axe           (t_weapon_axe)
    0x13FE, 0x13FF,   // i_katana        (t_weapon_sword)
    0x1400, 0x1401,   // i_kryss         (t_weapon_fence)
    0x1440, 0x1441,   // i_cutlass       (t_weapon_sword)
    0x13B5, 0x13B6,   // i_scimitar      (t_weapon_sword)
};

// MAKE_CLOTH's own bounds. Three empty gestures, matching every other
// escalate-after-three counter here (kMaxSmeltReachFails, the vendor chase);
// four pasture trips, one per row the save-derived table holds; and a rest
// long enough that a life which cannot get wool does something else for a
// while rather than re-walking to Yew on the next tick.
constexpr i32 kMaxEmptyClothSteps = 3;
constexpr i32 kMaxClothTrips = 4;
constexpr i64 kNoClothCooldownMs = 300000;
// Four yarn make one bolt: the loom's own message table is five entries and it
// yields at ARRAY_COUNT-1 (CClientTarg.cpp:2230-2245).
constexpr i32 kYarnPerBolt = 4;
// THE SHARD'S TAILOR WORKSHOP, by the atlas's own PLACE id
// (data/revolution_atlas.txt:2116, britain_tailor_2 at 1467,1686). The wheels
// and looms placed by the M3.7 decorator stand here; TravelToService(Tailor)
// remains the fallback for a life that is nowhere near Britain, because any
// tailor shop has the stations.
constexpr const char* kTailorWorkshopPlace = "britain_tailor_2";

constexpr i64 kNoToolCooldownMs = 180000;
// Spare gold, above the profession's reserve, that makes armour shopping
// sensible rather than reckless.
constexpr i32 kArmorMoney = 400;
constexpr i64 kGearCooldownMs = 240000;
// A rest, not a write-off. RetryableFailure means THIS door said no and
// another may not -- one silent shop is not evidence about a trade.
constexpr i64 kShortRestMs = 30000;
// TRAIN_COMBAT stand-downs (early-hunting-grounds, 2026-08-31): a goal that
// ends its tick having decided NOT to fight or NOT to travel must still
// Finish(false) with an explicit Cooldown -- Finish(false) alone only stops
// counting as success, Score() still reads the goal as feasible a moment
// later (see goal-that-did-nothing-must-stand-down). kHuntStandDownMs is for
// a condition this life expects to clear on its own shortly (too hurt, too
// loaded); kNoHuntingGroundCooldownMs is for the structural one (the atlas
// has nothing reachable), so a life does not re-walk the same failed trip on
// the very next tick.
constexpr i64 kHuntStandDownMs = kShortRestMs;
constexpr i64 kNoHuntingGroundCooldownMs = 180000;
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
//
// TWO IS THE SERVER'S NUMBER, AND THREE WAS ONE TOO MANY. Source-X answers a
// buy packet with CChar::CanTouch(vendor) (receive.cpp:752-756), and CanTouch
// refuses on `iDist > 2` -- CCharStatus.cpp:1423, where iDist is
// CPointMap::GetDist, the same Chebyshev metric as TileDist here. So a client
// that considered 3 "close enough" was guaranteed to be told no at exactly
// that distance. Aurelius proved it on the wire: he opened Kenton's shop from
// (1591,1657) with Kenton at (1588,1655) -- max(3,2) = 3 -- and the buy came
// back "You can't reach the Vendor" 1ms later, twice
// (run_gates/g_Aurelius.console.txt:411,468-470,482). This is a mirror of the
// server's own rule, not a tuning knob -- so take it from the one place that
// mirrors server rules rather than restating the number here.
constexpr i32 kVendorReach = sphere::kTouchDist;
// And a wandering vendor must not own the goal. After this many walk-backs,
// try the purchase from where we stand and let the server decide.
constexpr i32 kMaxVendorChases = 4;
// AND A TOOL THE SHARD WILL NOT LET US WIELD MUST NOT OWN THE GOAL EITHER.
// Three asks is enough to tell a slow server from a refusal; after that the
// character banks and does something else, and the gear cooldown paces the
// next attempt. (audit 2026-08-30, finding 4.)
constexpr int kMaxToolWearTries = 3;
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
// How many whole fish are worth putting the pole down for. One cast yields one
// fish, so this is "cut roughly once every four casts" -- see DoFish.
constexpr i32 kFishCutBatch = 4;
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
// How many times to swing at one recipe with nothing appearing before
// admitting the batch is going nowhere. Voris repeated it for a whole
// session; three is enough to ride out a slow shard and few enough to hand
// the turn on.
constexpr i32 kMaxCraftAttempts = 3;
// And how long to leave it alone afterwards -- long enough that the planner
// picks something else, short enough that a transient problem is retried.
constexpr i64 kCraftStuckCooldownMs = 120000;
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
// How far away a remembered productive spot may be and still be worth
// scanning from instead of from the boots. Comfortably more than the scan
// radius -- the point is to reach INSIDE the mine from its mouth -- but not
// so far that a miner walks to another county for it.
constexpr i32 kMineKnownSpotWithin = 60;
// Consecutive server refusals at the mouth (no nearby memory) before DoMine
// stops re-jittering the doorway and walks deeper into the cave instead.
// Three, same escalation point as everywhere else in this codebase.
constexpr i32 kMineRefusalsBeforeAdvance = 3;
// How many deeper-advances a single goal attempt will make before falling
// back to the ordinary give-up path. Bounds the walk so a genuinely
// rock-less cave still fails honestly rather than pacing forever.
constexpr i32 kMaxMineAdvances = 3;
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

// --- SCROLL GRAPHICS AND SPELL NUMBERS ARE NOT THE SAME LADDER --------------
//
// The 64 magery scroll ITEMDEFs run in the CLIENT's spellbook-page order,
// which puts Reactive Armor first in circle 1; the server's SPELL_TYPE table
// puts it seventh. Every other entry agrees, so the whole mapping is
// "graphic - 0x1F2D, plus one from Weaken onward", with Reactive Armor as the
// single exception.
//
// Evidence, server/Scripts-X/items/i_magic_magery.scp read against
// server/Scripts-X/spells/spells_magery.scp:
//   01f2d i_scroll_reactive_armor    s_reactive_armor    [SPELL 7]
//   01f2e i_scroll_clumsy            s_clumsy            [SPELL 1]
//   01f2f i_scroll_create_food       s_create_food       [SPELL 2]
//   01f30 i_scroll_feeblemind        s_feeblemind        [SPELL 3]
//   01f31 i_scroll_heal              s_heal              [SPELL 4]
//   01f32 i_scroll_magic_arrow       s_magic_arrow       [SPELL 5]
//   01f33 i_scroll_night_sight       s_night_sight       [SPELL 6]
//   01f34 i_scroll_weaken            s_weaken            [SPELL 8]
//   ...   (contiguous from here)
//   01f6c i_scroll_summon_elem_water s_summon_elem_water [SPELL 64]
//
// The old range started at 0x1F2E, so a looted Reactive Armor scroll was
// invisible to the "a scroll in the pack belongs in the book" pass, and ended
// at 0x1F6D, which is not a scroll at all.
constexpr u16 kFirstScrollGraphic = 0x1F2D;   // i_scroll_reactive_armor
constexpr u16 kLastScrollGraphic  = 0x1F6C;   // i_scroll_summon_elem_water

// Which spell does this scroll teach? 0 when the graphic is not a scroll.
int SpellForScrollGraphic(u16 graphic) {
    if (graphic < kFirstScrollGraphic || graphic > kLastScrollGraphic) return 0;
    const int idx = static_cast<int>(graphic) - static_cast<int>(kFirstScrollGraphic);
    if (idx == 0) return 7;      // Reactive Armor, first in the art
    if (idx <= 6) return idx;    // Clumsy .. Night Sight
    return idx + 1;              // Weaken (8) onward
}
// [SPELL 2] s_create_food -- targetless, 4 mana, MAGERY 10.0 to try.
// The practice spell: nothing to target wrongly, nobody to anger.
constexpr int kSpellCreateFood = 2;
constexpr i32 kCreateFoodMana  = 4;

// The word the NPC expects after "train". Sphere matches on the skill KEY from
// skills/skill<N>_<name>.scp, not on our own label.
const char* SkillKey(int id) {
    switch (id) {
        // Keep these exact runtime KEY values in sync with
        // runtime/scripts/skills/skill<N>_<name>.scp.  A missing row is not a
        // harmless label issue: it turns "<name> train Parrying" into the
        // generic "<name> train", which merely prints the trainer's list and
        // never produces a price or a lesson.
        case rules::kParrying:        return "Parrying";
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
        case rules::kTailoring:       return "Tailoring";
        case rules::kCartography:     return "Cartography";
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
        // THE WEAVER, who buys cotton (tm_vend.scp:896) and stands in the
        // tailor shop. Added with the NPC price floor, which is the first
        // thing to name that trade as a buyer -- without a row here it fell
        // through to GeneralVendor and the travel leg had nowhere to aim.
        // Not a guess: AtlasGenMain.cpp:579 and ClientTravel.cpp:1554 already
        // file a weaver under Service::Tailor, so this is the third copy of
        // one fact rather than a new one.
        {"weaver",      wm::Service::Tailor},
        // THE HIDE BUYERS, added with the 2026-09-02 restore of the material
        // BUY rows in tm_vend.scp. VENDOR_B_TANNER (:480-482) and
        // VENDOR_B_COBBLER (:342-344) are the live counters for i_hide and
        // i_hides_cut; without these two rows a hide row in kNpcBuyers would
        // fall through to GeneralVendor and the travel leg would aim nowhere.
        // Not new facts: ClientTravel.cpp:1555/1565 and AtlasGenMain.cpp:592/
        // :600 already file a cobbler under Tailor and a tanner under Tanner.
        {"tanner",      wm::Service::Tanner},
        {"cobbler",     wm::Service::Tailor},
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

// SOMETHING BLADED, IN THE PACK OR IN A HAND.
//
// The pack-only variant would miss the one case that matters most: a fighter
// walking to a pasture is carrying its katana IN ITS HAND, and
// FindBackpackItemByGraphic never returns a worn item. Source-X does not care
// which -- the shear branch is reached from OnTarg_Use_Item on the item's
// TYPE, and a wielded weapon is used the same way.
u32 FindBlade(Client& c) {
    for (u16 g : kBladedGraphics) {
        const u32 s = c.FindItemByGraphic(g, /*includeEquipped=*/true);
        if (s) return s;
    }
    return 0;
}

// DoSmelt's ore picker (S1, docs/CRAFTER_RUN_2026_08_30.md #20). Plain FindAny
// over kIronOre returns whichever ore graphic it hits first, and ore is one
// graphic for every metal -- a coloured vein (valorite, shadow, ...) and
// plain iron are indistinguishable without the hue. Smelting a rare ore as if
// it were iron is a real loss, so this prefers hue 0 (plain iron) when both
// are in the pack and only reaches for a coloured one when there is no iron
// left to melt instead. `hueOut` reports what was actually picked either way,
// so DoSmelt can log it rather than smelt silently.
u32 FindIronOrePreferPlain(Client& c, u16* hueOut) {
    const u32 pack = c.BackpackSerial();
    const usize n = c.ContainerItemCount(pack);
    u32 fallback = 0;
    u16 fallbackHue = 0;
    for (usize i = 0; i < n; ++i) {
        u32 serial = 0; u16 gfx = 0, amount = 0, hue = 0;
        if (!c.ContainerItemAt(pack, i, &serial, &gfx, &amount, &hue)) continue;
        bool isOre = false;
        for (u16 g : kIronOre) {
            if (g == gfx) { isOre = true; break; }
        }
        if (!isOre) continue;
        if (hue == 0) {
            if (hueOut) *hueOut = hue;
            return serial;
        }
        if (!fallback) { fallback = serial; fallbackHue = hue; }
    }
    if (fallback && hueOut) *hueOut = fallbackHue;
    return fallback;
}

// --- HUE-RESOLVED CONTAINER LOOKUPS (S1) ----------------------------------
//
// Every one of these exists because a GRAPHIC is not an identity on this
// shard. Ore is one graphic for all sixteen metals and the iron ingot is one
// graphic for thirteen, so FindContainerItemByGraphic / BackpackItemCount ask
// a question that has no single right answer: they will happily hand back a
// valorite stack when asked for iron, or add it to the iron total. Where a
// quantity is already computed by NAME (obs.pack and obs.bank are, since S1)
// and the SERIAL is still found by graphic, the two disagree -- and the gap
// between them is a wrong item moved, sold or melted.
//
// So: find and count over the same hue-resolved name, always.

// Every item in `container` whose hue-resolved defname is `item`: the first
// one's serial, and the TOTAL amount across all of them (which is what the
// graphic-keyed BackpackItemCount used to return, minus the other metals).
u32 FindContainerItemByName(Client& c, u32 container, const char* item,
                            i32* amountOut) {
    if (amountOut) *amountOut = 0;
    if (!item || !container) return 0;
    u32 first = 0;
    i32 total = 0;
    const usize n = c.ContainerItemCount(container);
    for (usize i = 0; i < n; ++i) {
        u32 serial = 0; u16 gfx = 0, amount = 0, hue = 0;
        if (!c.ContainerItemAt(container, i, &serial, &gfx, &amount, &hue))
            continue;
        if (!serial) continue;
        const char* name = econ::ItemNameForGraphicAndHue(gfx, hue);
        if (!name || std::strcmp(name, item) != 0) continue;
        if (!first) first = serial;
        total += amount ? amount : 1;
    }
    if (amountOut) *amountOut = total;
    return first;
}

u32 FindBackpackItemByName(Client& c, const char* item, i32* amountOut) {
    return FindContainerItemByName(c, c.BackpackSerial(), item, amountOut);
}

// The hue-resolved defname of one of OUR items, found by serial in the pack.
// The vendor's 0x9E sell list carries our own serials but no hue, so this is
// how a sell offer is joined back to what the item actually is.
const char* PackItemNameBySerial(Client& c, u32 serial) {
    if (!serial) return nullptr;
    const u32 pack = c.BackpackSerial();
    const usize n = c.ContainerItemCount(pack);
    for (usize i = 0; i < n; ++i) {
        u32 s = 0; u16 gfx = 0, amount = 0, hue = 0;
        if (!c.ContainerItemAt(pack, i, &s, &gfx, &amount, &hue)) continue;
        if (s == serial) return econ::ItemNameForGraphicAndHue(gfx, hue);
    }
    return nullptr;
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
    // The rotation memory lives on this Runner and is read by the need model,
    // so the two never disagree about what this life is making today.
    needCfg_.craftFocus = &craftFocus_;
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
    obs.healPotions = static_cast<i32>(client.BackpackItemCount(kHealPotion));
    // Counted here rather than in the goal, because a need that cannot see
    // the condition cannot select the goal that fixes it.
    for (const ClothingPiece& p : kBasicClothing) {
        // No tiledata, no layer, no opinion. Without it there is no way to
        // ask what is worn, and guessing would report a dressed character
        // naked.
        if (!client.ItemEquipLayer(p.graphic)) continue;
        bool worn = false;
        if (ClothingOnHand(client, p, &worn) || worn) continue;
        ++obs.clothingMissing;
    }
    // A skirt is optional wardrobe, but if a female character already owns one
    // in her backpack it must participate in the "dress from the pack" need.
    // Otherwise an otherwise clothed character never enters DoReplaceEquipment
    // and WearBasicClothing never gets a chance to equip the skirt layer.
    if (client.PlayerIsFemale()) {
        constexpr u16 kSkirts[] = {0x1516, 0x1531, 0x1537}; // long, short, kilt
        for (u16 graphic : kSkirts) {
            const u8 layer = client.ItemEquipLayer(graphic);
            if (layer && !client.EquippedGraphicAt(layer) &&
                client.FindBackpackItemByGraphic(graphic)) {
                ++obs.clothingMissing;
                break;
            }
        }
    }
    obs.logs     = static_cast<i32>(client.BackpackItemCount(kLog));
    obs.food     = CountAny(client, kFood, sizeof(kFood) / sizeof(kFood[0]));
    // HUNGER AS THE SERVER LAST SAID IT. "You are <level>" over the eight
    // levels in core/messages.scp:470-477, AND the food_full_N line every
    // successful meal produces -- both are statements about the same
    // STAT_FOOD (act::HungerStatements carries the table and the arithmetic).
    //
    // THE LAST STATEMENT WINS, and that is the whole point. The old read was
    // "did it ever say hungry since session start", but the shard says "You
    // are hungry" exactly ONCE, at login, and never repeats it. So a character
    // that logged in hungry stayed hungry to the client forever: it ate, the
    // server answered "You are nearly stuffed, but manage to eat the food",
    // and the flag did not move -- so it ate the rest of its pack, walked to
    // the provisioner, bought more, ate that, and did it again for the whole
    // session (2026-09-02 wave: g_Halain.console.txt:39 is the only hunger
    // status line in the run, followed by 351 `food: eating` lines).
    {
        usize rows = 0;
        const act::HungerStatement* table = act::HungerStatements(&rows);
        i64 newestMs = -1;
        int level = -1;
        for (usize i = 0; i < rows; ++i) {
            const i64 saidMs =
                client.JournalLastSaidMs(table[i].text, sessionStartJournalMs_);
            // Strict >: the table is ordered most-specific-first, so on the
            // one line that matches two phrases the specific one keeps it.
            if (saidMs > newestMs) {
                newestMs = saidMs;
                level = static_cast<int>(table[i].level);
            }
        }
        obs.starving = (level >= 0 && level <= act::kHungerLevelStarving);
        obs.hungry   = (level >= 0 && level <= act::kHungerLevelHungry);
    }

    obs.axeInPack = FindAny(client, kHatchet, 2) != 0 || FindAny(client, kAxe, 2) != 0;
    obs.weaponEquipped = HandsBusy(client);
    // Read the worn graphic rather than inferring from a full hand. The first
    // live run swung the newbie katana at a tree for two minutes because a
    // filled weapon hand was taken to mean "the axe is out".
    obs.axeEquipped = AxeInHand(client);

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    obs.marketQuiet = obs.nowMs < marketQuietUntilMs_;
    // WHICH items the player market came back empty on, not merely THAT it
    // did. marketQuietUntilMs_ above is a ten-minute damper on the trade goal
    // as a whole; a need that has to decide "may I walk to Yew for this one
    // material" needs the item, and the memory has been carrying it all
    // along.
    obs.noSellerFor.clear();
    for (const LifeEvent& e : state_.memory.Events()) {
        if (e.kind != "no_player_seller") continue;
        if (e.detail.empty()) continue;
        if (obs.nowMs - e.atMs > kPlayerWindowMemoryMs) continue;
        if (!obs.NoSellerFor(e.detail)) obs.noSellerFor.push_back(e.detail);
    }
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
    // The death location is enough to begin a recovery run.  The corpse serial
    // is a transient world-object id and is normally learned only after we
    // return to the death tile, so requiring it here made a logout turn every
    // real corpse into "nowhere to go back to".
    obs.corpseKnown = death.valid;
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
    // AND WHETHER THIS IS THE MARKET. Geometry only -- the same test the trade
    // handler uses to decide it has arrived -- because the planner needs to
    // know a market trip has been PAID FOR before it lets an ordinary errand
    // walk the character away again. See Observation::atMarket.
    obs.atMarket = AtMarketBank(client);

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
            u32 serial = 0; u16 gfx = 0, amount = 0, hue = 0;
            if (!client.ContainerItemAt(box, i, &serial, &gfx, &amount, &hue)) continue;
            // Hue first, graphic fallback (S1): ore and the iron ingot are
            // one graphic for every metal, so a coloured deposit in the bank
            // must be read by its hue or it merges into the plain-iron count.
            const char* name = econ::ItemNameForGraphicAndHue(gfx, hue);
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
    // defname. Built from what THIS life produces and consumes, so the final
    // list is a handful of names rather than every item on the shard.
    //
    // NOT graphic-only anymore (S1, docs/CRAFTER_RUN_2026_08_30.md #20). One
    // name can have several graphics -- iron ingots are 0x1BEF/0x1BF0/0x1BF1
    // by stack size -- but the reverse is also true and is the trap the old
    // per-name GraphicsForItem() sum fell into: ore is ONE graphic for every
    // metal, and so is the iron ingot for its twelve special colours, so
    // summing BackpackItemCount() over "every graphic i_ore_iron uses" also
    // counted every coloured vein in the pack as plain iron. A single
    // hue-resolved pass over the actual pack contents, mirroring the bank
    // box above, is the only way to keep them apart.
    if (needCfg_.profession) {
        std::unordered_map<std::string, i32> packByName;
        {
            const u32 packBox = client.BackpackSerial();
            const usize pn = client.ContainerItemCount(packBox);
            for (usize i = 0; i < pn; ++i) {
                u32 serial = 0; u16 gfx = 0, amount = 0, hue = 0;
                if (!client.ContainerItemAt(packBox, i, &serial, &gfx, &amount, &hue))
                    continue;
                const char* name = econ::ItemNameForGraphicAndHue(gfx, hue);
                if (!name) continue;          // nothing we have a name for
                packByName[name] += (amount ? amount : 1);
            }
        }
        auto countInto = [&](const std::string& item) {
            for (const market::Stock& have : obs.pack) {
                if (have.item == item) return;   // already counted
            }
            const auto it = packByName.find(item);
            obs.pack.push_back({item, it != packByName.end() ? it->second : 0});
        };
        for (const std::string& it : needCfg_.profession->produces) countInto(it);
        for (const std::string& it : needCfg_.profession->consumes) countInto(it);

        // AND EVERY METAL THE PACK ACTUALLY HOLDS, listed or not.
        //
        // The two loops above only fill names this profession `produces` or
        // `consumes` -- "i_ore_iron" and "i_ingot_iron" for a mining smith.
        // The hue pass, though, now names a coloured vein HONESTLY, so
        // i_ore_rusty / i_ingot_bronze resolve to names that are in neither
        // list and were simply DROPPED from obs.pack. Before S1 they were at
        // least counted (wrongly) as iron; after it they vanished, which is
        // the worse failure: the gather goal cannot see its own haul, the
        // smelt goal has nothing to melt and the bank goal nothing to put
        // away, all while the pack is full.
        //
        // This is not a rare tail. r_default_rock -- the region type every
        // ordinary rock on the map uses -- weights, out of ~100 parts
        // (runtime/scripts/core/regiontypes.scp:19-37):
        //     50.0 mr_iron, 10.0 mr_nothing, 8.0 mr_rusty,
        //      6.0 mr_old_copper, 6.0 mr_dull_copper, 5.0 mr_bronze, ...
        // and every one of those four coloured ores carries SKILL=1.0,30.0,
        // the same band as mr_iron (core/regionresources.scp:259-285). So a
        // 50-skill miner is inside the band for all of them and about a
        // quarter of what it digs up is not iron at all.
        //
        // obs.bank needs no equivalent: its pass above is unfiltered and
        // already keeps every name the hue lookup resolves.
        //
        // AND EVERY REAGENT, for the same reason and a sharper one. A spell
        // consumes what the SPELL lists, not what the profession's `consumes`
        // happens to name: the mage entry names four reagents
        // (Professions.cpp) while Night Sight -- the first spell practice
        // reaches for -- costs spider silk and sulfurous ash, neither of them
        // on that list. Counting only the listed four would report a pouch of
        // 250 ash as EMPTY, and the restock errand would then buy ash forever.
        for (const auto& kv : packByName) {
            if (kv.first.compare(0, 6, "i_ore_") == 0 ||
                kv.first.compare(0, 8, "i_ingot_") == 0 ||
                kv.first.compare(0, 7, "i_reag_") == 0)
                countInto(kv.first);
        }
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

    // The reagent shopping list PRACTICE_SKILL left behind, if any, minus
    // anything the pack has since acquired. Kept here rather than in the goal
    // so it survives a goal change: the whole point is that a DIFFERENT goal
    // (BUY_SUPPLIES) does the fetching.
    // Observe is const, so the list itself is pruned where it is USED
    // (DoBuySupplies); what the need model sees is only the still-missing part.
    // The Magery table is data (data/revolution_spells.tsv, exported from the
    // shard's own spells_magery.scp by tools/spellgen.py). Load it here, once,
    // because BOTH the practice goal and the food errand ask DefForSpell what
    // a cast costs and neither may run before the table exists.
    spell::LoadSpellTable(client.DataDir());
    obs.practiceReagentsShort.clear();
    for (const std::string& r : reagentWants_) {
        if (market::QtyOf(obs.pack, r) <= 0)
            obs.practiceReagentsShort.push_back(r);
    }
    obs.practiceReagentQty = reagentWantQty_;

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
// WHAT EVERY NEW PLAYER ALREADY KNOWS (docs/LIFE_GATE_WAVE1.md theme 1).
//
// The M4 brief's Phase 15 rule still holds: "Seed only what the character
// would reasonably know at creation ... Everything else should be learned."
// It used to mean only "a lumberjack knows Yew has woods" -- but wave-1's
// evidence was two failures of the SAME shape, not one:
//
//   Vorar (lumberjack): GATHER_LOGS spun on "no known source of that
//   resource" with an empty places/resources memory.
//   Draver / Lyra (smith / scribe): the very first BANK goal failed "no
//   banker in sight" at 00:32-00:33 -- DoBank only ever scans mobiles
//   already in view, and nothing had ever told either of them where a
//   counter was.
//
// So this now delegates to the pure uo::life::SeedNewbieKnowledge
// (newbie_knowledge.h, unit-tested directly against the shipped atlas in
// tests/newbie_knowledge.cpp), which seeds the home bank and healer and
// provisioner alongside the resource lead this always seeded. This wrapper's
// only job is what a live session alone can supply: the atlas itself, the
// moment "world knowledge is ready", and the once-per-life guard.
//
// ANCHORED ON state_.homeCity, NOT ON WHEREVER THE CHARACTER IS STANDING.
// The shard's own chargen spawn point (map0_starts.scp) is not driven by
// Profession::homeCities, so the two can differ -- and what a lumberjack
// knows on day one is "Yew has woods", not "wherever I happened to spawn has
// woods nearby".
//
// A DIFFERENT EVENT NAME ON PURPOSE. A character already carrying the older
// "common_knowledge_seeded" mark from a prior run is not "already seeded" by
// today's fuller definition -- it never got a bank, healer or provisioner --
// so re-seeding under a new name picks it up on its next load rather than
// silently skipping it forever.
void Runner::SeedNewbieKnowledge(Client& client, i64 nowMs) {
    // Older lives carry the first, resource-only seed marker.  That marker
    // predates common_knowledge_bank, so treating it as complete leaves a
    // miner with a BANK goal but no counter it can route to.  The seed is
    // idempotent; only skip when this life has the complete current version.
    if (state_.memory.HasEvent("newbie_knowledge_seeded") &&
        state_.memory.BestPlace("common_knowledge_bank")) return;
    if (!client.WorldKnowledgeReady()) return;

    const world_atlas::Atlas* atlas = client.WorldAtlas();
    if (!atlas) return;

    const usize placesBefore = state_.memory.Places().size();
    const usize resourcesBefore = state_.memory.Resources().size();

    life::SeedNewbieKnowledge(state_, needCfg_.profession, state_.homeCity,
                              *atlas, nowMs);

    state_.memory.NoteEvent("newbie_knowledge_seeded", state_.homeCity.c_str(),
                            "", client.PlayerX(), client.PlayerY(), nowMs);
    LogLine("newbie knowledge: %zu place(s) and %zu resource hint(s) seeded "
            "near home (%s) -- everything else is earned",
            state_.memory.Places().size() - placesBefore,
            state_.memory.Resources().size() - resourcesBefore,
            state_.homeCity.empty() ? "nowhere yet" : state_.homeCity.c_str());
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
    // The end-of-session verdict is also the only one a run gets if it never
    // reaches a clean WindDown -- a crash, a disconnect, an operator kill.
    // Checkpoint fires far more often than that (periodic + several action
    // sites), so this is gated to once per kHistogramIntervalMs rather than
    // reprinting on every save; same "session_goals" prefix as the WindDown
    // call, so one grep catches both (S2.8).
    if (nowMs - lastHistogramMs_ >= kHistogramIntervalMs) {
        LogGoalHistogram();
        lastHistogramMs_ = nowMs;
    }
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
                // RestTick's blockedForMs is obs.nowMs - lastRealErrandMs_;
                // left at its 0 sentinel, the very first idle tick of a
                // session reads as "blocked" since session start (S2_WIRING
                // _PLAN.md S2.2), a false Stagnant before any real errand has
                // even had a chance to run. Stamping it here gives it the
                // same origin as the session clock it is measured against.
                lastRealErrandMs_ = nowMs;
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
            Observation obs = Observe(client, nowMs);
            // Rehydrate a pending corpse run after a reconnect.  Object
            // serials are deliberately not persisted, but the recorded death
            // tile is sufficient to travel back and discover the current
            // corpse object there.
            // A reconnect may happen after resurrection but before the corpse
            // run.  The fresh client has no session-local DeathRecord in that
            // case, yet the durable corpse_pending event is still authoritative.
            if (!client.Knowledge().LastDeath().valid) {
                for (auto it = state_.memory.Events().rbegin();
                     it != state_.memory.Events().rend(); ++it) {
                    if (it->kind == "corpse_recovered" || it->kind == "corpse_abandoned")
                        break;
                    if (it->kind == "corpse_pending") {
                        client.Knowledge().NoteDeath(it->x, it->y, 0, "", nowMs);
                        obs = Observe(client, nowMs);
                        LogLine("corpse run: restored pending death at %d,%d", it->x, it->y);
                        break;
                    }
                }
            }
            SeedNewbieKnowledge(client, nowMs);
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
            // Fresh gate for this session's histogram, so the "login
            // reconciled" Checkpoint two lines down does not immediately
            // fire LogGoalHistogram on an all-zero goalPicks[].
            lastHistogramMs_ = nowMs;

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

            // WHEN DID THIS LIFE COME BACK? The robe the server hands out at a
            // resurrection is only identifiable by the moment it appears (see
            // CutResurrectionRobe), so the dead->alive transition has to be
            // noticed as it happens rather than inferred later from a robe
            // that might be anyone's.
            if (wasDead_ && !obs.dead) resurrectedAtMs_ = nowMs;
            wasDead_ = obs.dead;

            LearnFromObservation(client, obs);
            MaintainBuildLocks(client, obs);

            // THE REAGENT LIST IS SETTLED WHERE IT CAN BE SEEN, not only where
            // it is spent. BUY_SUPPLIES prunes it too, but once the last
            // reagent is bought its own need disappears and it is never
            // entered again -- so the last entry would never come off and
            // PRACTICE_SKILL would sit out its whole stand-down holding a full
            // pouch (run_gates/g_Aurelius.console.txt:717-731).
            if (!reagentWants_.empty() && obs.practiceReagentsShort.empty()) {
                LogLine("practice: the pouch is stocked again -- practising is "
                        "back on the table");
                reagentWants_.clear();
                planner_.ClearCooldown(GoalKind::PracticeSkill);
            }

            // --- session limits -------------------------------------------
            const i64 elapsed = nowMs - sessionStartMs_;
            if (cfg_.sessionLimitMs > 0 && elapsed >= cfg_.sessionLimitMs) {
                // Deferral rules from the audit (section 3.13): never end a
                // session on top of a corpse run, and never while dead.
                //
                // BUT A DEFERRAL WITHOUT A BOUND IS NOT A DEADLINE. A corpse
                // run stuck in a retry loop held both conditions true
                // forever, and Hector was still connected 5 minutes past a
                // 30-minute window (artifacts/wave_2026-09-02_verdict.md).
                // The grace buys the corpse run a chance to finish; after
                // that THE CLOCK WINS -- the in-flight goal and trip are
                // cancelled and the ordinary wind-down/logout path runs, the
                // same one a player uses. No process kill.
                const bool defer =
                    obs.dead || planner_.Current().kind == GoalKind::RecoverCorpse;
                if (!defer) {
                    EndSession("session time limit reached");
                    return;
                }
                if (elapsed >= cfg_.sessionLimitMs + kSessionOverrunGraceMs) {
                    LogLine("session_overrun goal=%s dead=%d over=%llds -- the "
                            "clock wins",
                            GoalKindName(planner_.Current().kind),
                            obs.dead ? 1 : 0,
                            static_cast<long long>(
                                (elapsed - cfg_.sessionLimitMs) / 1000));
                    if (client.TravelBusy())
                        client.TravelAbort("session time limit reached");
                    travelInFlight_ = false;
                    if (planner_.Current().active)
                        planner_.Finish(false, "session time limit reached", nowMs);
                    EndSession("session time limit reached (grace spent)");
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
                // S2.2: DecideRest's `blockedForMs` needs to know when a REAL
                // errand -- anything outside the Wander family -- was last
                // picked. One line at one site (S2_WIRING_PLAN.md S2.2).
                if (FamilyOf(planner_.Current().kind) != GoalFamily::Wander) {
                    lastRealErrandMs_ = obs.nowMs;
                }
                if (wasActive) {
                    // SELF-SUPERSESSION: "goal_changed=X from=X" -- the
                    // planner cleared the goal (Exhausted, or a completion)
                    // and re-picked the identical kind. Greppable before
                    // S2.8; now totalled for the session_goals verdict line.
                    if (previous == planner_.Current().kind) {
                        session_.selfSupersessions++;
                    }
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
                    // Each DoXxx handler's lastXxxPlan_ exists only so
                    // LogPlan fires on a plan transition, not every tick
                    // (S2_WIRING_PLAN.md S2.0). Left across a goal change, a
                    // plan whose name happens to match the last one logged
                    // this session -- e.g. plan=disengage picked up again
                    // several goals later -- reads as "no change" and never
                    // logs, even though it is a brand new goal's first tick.
                    // Reset every one of them to its sentinel here, with the
                    // rest of the transient slate this guard already wipes
                    // (review finding 6).
                    lastCombatMove_ = life::CombatMove::Wait;
                    lastHealPlan_ = HealStep::None;
                    lastRestPlan_ = static_cast<RestStep>(0xFF);
                    lastRecoveryPlan_ = static_cast<RecoveryStep>(0xFF);
                    lastTrainPlan_ = TrainStep::Done;
                    lastCraftPlan_ = static_cast<CraftStep>(0xFF);
                    lastBandageAcquirePlan_ = AcquireStep::Done;
                    lastPotionAcquirePlan_ = AcquireStep::Done;
                    lastGarmentAcquirePlan_ = AcquireStep::Done;
                    lastToolAcquirePlanByItem_.clear();
                }
                // Per-errand counters belong to the errand. vendorChases_
                // bounds how long a wandering shopkeeper may be followed, and
                // a fresh goal deserves a fresh allowance -- otherwise one
                // restless vendor early in a session silences every purchase
                // made after it.
                vendorChases_ = 0;
                logsAtGoalStart_ = obs.logs;
            }
            // Select itself can end a goal -- an attempts-exhausted one goes
            // through Finish, so it can trip the noop-spin backstop without
            // ever reaching the completion path below.
            LogSpinIfDetected();

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
            // LOGGING OUT LATE IS CHEAP. LOGGING OUT IN THE WILD IS FATAL.
            //
            // Source-X leaves the body standing in the world after the
            // disconnect, so whatever is wandering past finishes the job:
            // Corwyn was killed by a Gazer a minute after one logout and by a
            // Wudgh in the same minute as the next, losing his tools, his
            // ingots and six shields. Four wild logouts in a row, each one
            // starting the next ghost walk.
            //
            // So the abort is bounded twice over rather than once. The plain
            // deadline still applies when the character is nowhere near
            // safety -- an unreachable target must not hold the session open
            // for fourteen minutes, which is what happened before it existed.
            // But when a safe spot is CLOSE and the trip is still moving, the
            // last stretch is worth another minute; giving up thirty tiles
            // out is how a 3-minute session ends in open country.
            // IS IT STILL WALKING, or is it stuck? That is the question the
            // original deadline was really asking. A trip that cannot arrive
            // must not hold the session open -- but one that is visibly
            // covering ground deserves to finish, and the old fixed bound
            // could not tell the two apart. It cut Corwyn off mid-stride in
            // Britain with the bank in sight.
            const i64 windDownMs = nowMs - windDownStartedMs_;
            if (client.PlayerX() != windDownLastX_ ||
                client.PlayerY() != windDownLastY_) {
                windDownLastX_ = client.PlayerX();
                windDownLastY_ = client.PlayerY();
                windDownMovedMs_ = nowMs;
            }
            const bool stillMoving =
                windDownMovedMs_ != 0 &&
                nowMs - windDownMovedMs_ < kWindDownStalledMs;
            const i64 budgetMs =
                stillMoving ? kWindDownGraceMs : kWindDownBudgetMs;

            const bool outOfTime = windDownMs > budgetMs;
            if (outOfTime && client.TravelBusy()) {
                LogLine("wind-down: the trip has run past its deadline (%llds, "
                        "%s); abandoning it and logging out where I stand",
                        static_cast<long long>(windDownMs / 1000),
                        stillMoving ? "still moving, but far too long"
                                    : "not moving");
                client.TravelAbort("wind-down deadline");
                travelInFlight_ = false;
                return;
            }
            if (client.TravelBusy()) return;

            const KnownPlace* bank = state_.memory.NearestPlace(
                "bank", client.PlayerX(), client.PlayerY());
            // A REACHED GUARDED SPOT IS SAFE ALREADY -- do not walk PAST it
            // looking for a bank. Without this a character standing in the
            // middle of a guarded town, but more than 6 tiles from any bank
            // this life has personally learned, read as unsafe and set off
            // on "asking the world for one" (below), which is a fresh
            // cross-world walk exactly like the one that just stranded
            // Dorvar: the abort at the top of this block only fires once a
            // trip is ALREADY committed and stalled, so the fix is to not
            // start an avoidable one in the first place when where we are
            // standing already answers to a guard.
            const wm::Region* hereRegion = client.CurrentRegion();
            const bool safeHere = client.BankContainer() != 0 ||
                                  windDownArrived_ ||
                                  (hereRegion && hereRegion->flags.guarded) ||
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

                // THE NEAREST SAFE PLACE, NOT THE NEAREST *REMEMBERED* ONE.
                //
                // NearestPlace already sorts by distance -- but only over what
                // this character has personally learned, and Corwyn had learned
                // nothing but Minoc. So standing in Britain, twenty tiles from
                // a bank, "nearest" meant Minoc: 1,752 tiles away.
                //
                // That produced a self-sustaining death loop. Four logouts in
                // the wild, every one of them in the Britain region --
                // (1422,1555), (1330,1978), (1618,1442), (1701,1367) -- and
                // Source-X leaves the body standing in the world after the
                // disconnect, so each one was killed where it stopped:
                //
                //   16:17 disconnected   16:18 'Corwyn' was killed by N'Gazer'
                //   16:25 disconnected   16:25 'Corwyn' was killed by N'Wudgh'
                //
                // He then woke as a ghost, walked to the Britain healer,
                // resurrected, was ordered home to Minoc, ran out of clock in
                // open country, and did it all again. Full loot took his tools,
                // his ingots and six heater shields on the way round.
                //
                // "That should be nearest, or near work place" (project owner,
                // 2026-08-30). The atlas knows every city's bank, so when the
                // remembered one is a journey, ask the world for a closer one.
                // Britain was a perfectly good place to log out; going home was
                // the whole mistake.
                const i32 known = bank
                                      ? TileDist(bank->x, bank->y, client.PlayerX(),
                                                 client.PlayerY())
                                      : -1;
                if (bank && known <= kWindDownPreferKnownWithin) {
                    LogLine("wind-down: travelling to a known bank at %d,%d, "
                            "%d tiles off (attempt %d)",
                            bank->x, bank->y, known, windDownTrips_);
                    travelInFlight_ =
                        client.TravelToPoint(bank->x, bank->y, 3, "logout_safe");
                } else {
                    if (bank)
                        LogLine("wind-down: the nearest bank this life has "
                                "learned is %d tiles away -- asking the world "
                                "for a closer one (attempt %d)",
                                known, windDownTrips_);
                    else
                        LogLine("wind-down: no bank learned yet; asking the "
                                "world for one (attempt %d)", windDownTrips_);
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

            // HOW THE DAY WAS SPENT, as one greppable line. Extracted to
            // LogGoalHistogram (S2.8) so a crash or a killed session -- not
            // just a clean logout -- can still leave a verdict; see the
            // gated call inside Checkpoint. Unconditional and stamped here:
            // the clean-logout verdict always prints, and the periodic
            // Checkpoint call two lines down must not immediately repeat it.
            LogGoalHistogram();
            lastHistogramMs_ = nowMs;

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

// HOW THE DAY WAS SPENT, as one greppable line.
//
// R1's exit proof is "at least four goal families, none above half the
// picks", and that has to be checkable without reading fifty thousand
// lines by eye. Printing the shape of the day is also the only way the
// monotony ever became visible: p0gate10 looked like a healthy session
// until its goals were counted and turned out to be CRAFT / BUY_SUPPLIES /
// EARN_GOLD in a ring and nothing else.
//
// S2.8: extracted out of the WindDown case so it is also reachable from
// Checkpoint (gated, kHistogramIntervalMs) -- a crash, a disconnect, or a
// session killed by the operator used to print no verdict at all. The
// arithmetic itself (families/picks/top/topFrac/varied) is
// uo::life::SummariseGoalPicks (Goals.cpp), a pure function reachable by
// ctest; this method is formatting only.
void Runner::LogGoalHistogram() const {
    const GoalHistogram h = SummariseGoalPicks(session_.goalPicks);

    // Counted by FAMILY, not by goal kind, for the summary numbers above --
    // but the breakdown text below still wants per-family and per-kind
    // counts, which is display detail, not the tested arithmetic.
    i32 famCount[static_cast<int>(GoalFamily::Count)] = {};
    for (int i = 0; i < static_cast<int>(GoalKind::Count); ++i) {
        const i32 n = session_.goalPicks[i];
        if (n <= 0) continue;
        famCount[static_cast<int>(FamilyOf(static_cast<GoalKind>(i)))] += n;
    }
    std::string hist;
    for (int f = 0; f < static_cast<int>(GoalFamily::Count); ++f) {
        if (famCount[f] <= 0) continue;
        if (!hist.empty()) hist += " ";
        char fc[64];
        std::snprintf(fc, sizeof(fc), "%s=%d(%.0f%%)",
                      GoalFamilyName(static_cast<GoalFamily>(f)), famCount[f],
                      h.picks ? (100.0 * famCount[f] / h.picks) : 0.0);
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
                      h.picks ? (100.0 * n / h.picks) : 0.0);
        hist += cell;
    }
    LogLine("session_goals families=%d picks=%d top=%.0f%% varied=%d "
            "self_superseded=%d | %s",
            h.families, h.picks, h.topFrac * 100.0, h.varied ? 1 : 0,
            session_.selfSupersessions,
            hist.empty() ? "(none)" : hist.c_str());
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

// The one place a plan's step is logged. Callers emit this once per plan
// change (a `lastPlan*_` member compared against the new step), not once per
// tick -- per-tick emission is what produced the 311-line forge spam this
// slice exists to end.
void Runner::LogSpinIfDetected() {
    const GoalKind spun = planner_.TakeSpinDetected();
    if (spun == GoalKind::Count) return;
    LogLine("goal_spinning=%s reason=\"completed %d times in a row with "
            "progress 0 -- cooled off for a minute; this is a bug in that "
            "goal, not pacing\"",
            GoalKindName(spun), 5);
}

void Runner::LogPlan(const char* kind, const char* reason) const {
    LogLine("plan=%s reason=\"%s\"", kind, reason);
}

// AN ERRAND'S REASON, ON CHANGE OR ONCE A MINUTE -- never once per tick.
//
// The errands answer with a reason every tick on purpose: an unexplained
// stand-down is the defect that whole layer exists to end. But printing every
// one of them prints the TICK RATE, not the errand. Measured: 214 "potions:"
// lines in run_r4/w_Bruin.console.txt, 209 of them the identical "an action
// is already in flight" while a single 8-second vendor ask was outstanding.
//
// Same sentinel rule LogPlan already uses for plan steps, over text instead of
// an enum, and per tag so one errand's chatter cannot hide another's.
void Runner::LogErrandReason(const char* tag, const char* reason,
                             i64 nowMs) const {
    if (!tag || !reason || !reason[0]) return;
    ErrandLogSentinel& seen = errandLogSeen_[tag];
    if (seen.atMs != 0 && seen.reason == reason &&
        nowMs - seen.atMs < kErrandReasonRepeatMs)
        return;
    seen.reason = reason;
    seen.atMs = nowMs;
    LogLine("%s: %s", tag, reason);
}

// The ONLY legal way a plan hands the turn to another goal (S2_WIRING_PLAN.md
// S2.0). `to` is advisory only -- it is logged, never dispatched; the
// receiving goal is chosen by Planner::Select on the next tick from whatever
// need AssessNeeds already produces. The cooldown is load-bearing: without it
// Planner::Score treats `from` as still feasible and it can simply win again.
bool Runner::HandOff(GoalKind from, GoalKind to, i64 restMs, const char* why,
                     i64 nowMs) {
    planner_.Cooldown(from, nowMs + restMs);
    planner_.Finish(false, why, nowMs);
    LogLine("handoff=%s->%s reason=\"%s\"", GoalKindName(from), GoalKindName(to),
            why);
    nextActionMs_ = nowMs + 2000;
    return false;
}

bool Runner::VetoTripOverSessionBudget(Client& client, const Observation& obs,
                                       GoalKind goal, const char* goalName,
                                       i64 cooldownMs) {
    if (cfg_.sessionLimitMs <= 0) return true;   // no session clock to run out
    const i32 tiles = client.TravelLastPlannedTiles();
    // 0 means no plan has landed yet (this trip's TravelPlanRoute runs on a
    // later Client tick than the TravelToXxx() call that started it) or the
    // plan failed outright -- either way there is nothing here yet to judge,
    // and the ordinary travel-failure/replan machinery will be heard from on
    // its own. Vetoing on a stale zero would only ever wave a real trip
    // through, never wrongly block one, so that side is safe to skip too.
    if (tiles <= 0) return true;
    const i64 remainingMs = cfg_.sessionLimitMs - (obs.nowMs - sessionStartMs_);
    if (TripFitsSessionBudget(remainingMs, tiles, kWindDownBudgetMs)) return true;

    LogLine("goal_blocked=%s reason=\"not enough session left for the trip\" "
            "tiles=%d left=%llds need=%llds", goalName, tiles,
            static_cast<long long>(remainingMs / 1000),
            static_cast<long long>(
                (EstimateTripTimeMs(tiles) + kWindDownBudgetMs) / 1000));
    client.TravelAbort("not enough session left for the trip");
    travelInFlight_ = false;
    planner_.Cooldown(goal, obs.nowMs + cooldownMs);
    planner_.Finish(false, "not enough session left for the trip", obs.nowMs);
    return false;
}

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
        case GoalKind::MakeCloth:            done = DoMakeCloth(client, obs); break;
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
        LogSpinIfDetected();
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
            // A live corpse serial cannot survive a reconnect, but this
            // location can.  Save it immediately: dying must not make loot
            // recovery depend on keeping the original client process alive.
            state_.memory.NoteEvent("corpse_pending", "recover after resurrection", "",
                                    obs.x, obs.y, obs.nowMs);
            Checkpoint(client, obs.nowMs, "death location recorded");
            if (!currentFoeName_.empty()) {
                LogLine("dead: blaming '%s' -- it is what we were fighting",
                        currentFoeName_.c_str());
                state_.memory.NoteCreatureOutcome(currentFoeName_.c_str(),
                                                  kCreatureEvidenceDeath,
                                                  obs.nowMs);
            }
        }

        // A player walks to a healer. So does this.  A resurrection reply is
        // valid only after the healer has offered it; sending one while still
        // travelling creates a busy action and strands the ghost in place.
        if (travelInFlight_ && !client.TravelBusy()) {
            travelInFlight_ = false;
            LogLine("dead: arrived at healer destination -- scanning nearby healers");
            client.ActionScanMobiles();
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        if (client.ActionBusy()) return false;

        const u32 healer = client.NearestMobileWithTrade("healer");
        if (healer) {
            i32 hx = 0, hy = 0; i8 hz = 0;
            // Healers commonly stand behind a counter or in a small room.
            // The interaction range is wider than adjacency, so path to an
            // accessible tile in that range instead of attempting to occupy
            // the NPC's sealed tile through the wall.
            constexpr i32 kHealerReach = 4;
            if (client.MobilePosition(healer, &hx, &hy, &hz) &&
                TileDist(obs.x, obs.y, hx, hy) > kHealerReach && !client.TravelBusy()) {
                LogLine("dead: a healer is here -- getting close enough to be "
                        "raised");
                travelInFlight_ = client.TravelToEntity(healer, kHealerReach);
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

    // AvoidCombat only (S2_WIRING_PLAN.md S2.6). A null profession is a
    // pre-catalogue life -- combatStrategy defaults to AvoidCombat
    // (professions.h:235), so gating on the enum alone would silently turn
    // every legacy character pacifist. Every other strategy falls through to
    // the body below byte-for-byte; Melee/Ranged/Mage/Tamer tuning is R2's.
    bool avoidCombatDisengage = false;
    if (needCfg_.profession &&
        needCfg_.profession->combatStrategy == life::CombatStrategyId::AvoidCombat) {
        life::CombatSight see;
        see.hp = obs.hp;
        see.hpMax = obs.hpMax;
        see.mana = obs.mana;
        // UNKNOWN: no Observation field and no status flag for this on the
        // shard (the same gap Runner.cpp records for weight). Left 0.
        see.manaMax = 0;
        // Neither the hostile nor the target-selection loop has run yet at
        // this point in the function -- write what we have, the nearest
        // hostile, same source NoteDanger above just used.
        see.foeDistance =
            TileDist(hostiles.front().x, hostiles.front().y, obs.x, obs.y);
        see.foeHpFraction = hostiles.front().hpCur >= 0 && hostiles.front().hpMax > 0
                                ? static_cast<double>(hostiles.front().hpCur) /
                                      hostiles.front().hpMax
                                : -1.0;
        see.attackersOnMe = obs.attackersOnMe;
        see.bandages = obs.bandages;
        // UNKNOWN: obs.hasPet answers ownership, not health. Left at
        // defaults (petAlive=false, petHpFraction=-1.0).
        {
            bool armedNow = false;
            for (usize i = 0; i < sizeof(kMeleeWeaponGfx) / sizeof(u16); ++i) {
                const u16 g = kMeleeWeaponGfx[i];
                if (client.EquippedGraphicAt(kLayerHand1) == g ||
                    client.EquippedGraphicAt(kLayerHand2) == g) {
                    armedNow = true;
                    break;
                }
            }
            see.armed = armedNow;
        }

        life::CombatTuning tune;
        tune.fleeHpFraction = needCfg_.fleeHpFraction;
        tune.healHpFraction = needCfg_.healHpFraction;
        // UNKNOWN: neither field exists on NeedConfig or a personality
        // record. Left at the struct defaults (preferredRange=6,
        // riskTolerance=0.5).

        const life::CombatDecision d = life::DecideCombat(
            needCfg_.profession->combatStrategy, see, tune);
        if (d.move != lastCombatMove_) {
            LogPlan(life::CombatMoveName(d.move), d.reason);
            lastCombatMove_ = d.move;
        }

        // AvoidCombat always decides Disengage (CombatStrategy.cpp:71-73),
        // before ShouldBreakOff is even consulted -- so this is the only
        // reachable arm in S2. Every other CombatMove is unreachable here.
        // Only peace mode happens right here: returning true on the spot
        // (the earlier version) skipped the bailAt block below entirely,
        // which meant an AvoidCombat life fled with none of the FLEE
        // path's creature-outcome evidence, first_near_death event, or
        // rate-limited danger note -- a second, thinner retreat instead of
        // the proven one (S2_WIRING_PLAN.md review finding 5).
        // avoidCombatDisengage forces that block to run below regardless
        // of HP, since this life never reaches the fight-back code after
        // it either way.
        if (d.move == life::CombatMove::Disengage) {
            client.EnsurePeaceMode();
            avoidCombatDisengage = true;
        }
    }

    double bailAt = needCfg_.fleeHpFraction;
    const i32 extra = obs.attackersOnMe - 1;
    if (extra > 0) bailAt = std::min(0.90, bailAt + 0.08 * std::min(3, extra));

    // A fresh warrior pulls ONE opponent.  Two actual attackers is not a
    // tougher version of the same lesson: it is the boundary where the bot
    // breaks contact immediately, even while healthy, so it can heal and
    // choose a quieter part of the hunting ground.
    if (avoidCombatDisengage || obs.attackersOnMe >= 2 ||
        obs.HpFraction() < bailAt) {
        LogLine("interrupt=FLEE reason=\"HP %.0f%%; %d attacker(s); bail at %.0f%%\"",
                obs.HpFraction() * 100.0, obs.attackersOnMe, bailAt * 100.0);
        client.EnsurePeaceMode();
        // Once per fight, not once per tick -- same guard as the note
        // above (S2_WIRING_PLAN.md review finding 4). This is now also
        // where the AvoidCombat arm's danger note lands, since it always
        // falls through into this block.
        if (obs.nowMs - lastDangerNoteMs_ > 60000) {
            lastDangerNoteMs_ = obs.nowMs;
            state_.memory.NoteDanger(obs.x, obs.y, 18, hostiles.front().name.c_str(),
                                     1.5, obs.nowMs);
        }
        // AND WHAT IT WAS, not just where it happened. A place cannot un-scare
        // you, but a creature type can prove itself safe or dangerous, and
        // "learn which graveyard mobs are safe and which are dangerous" is the
        // owner's warrior loop. Fleeing at low health from THIS thing is the
        // strongest evidence short of dying to it.
        state_.memory.NoteCreatureOutcome(hostiles.front().name.c_str(),
                                          kCreatureEvidenceNearDeathFlee,
                                          obs.nowMs);
        // These are the creatures we just decided not to fight.  Without a
        // temporary exclusion, the next SURVIVE tick re-selected one as soon
        // as the attacker count flickered from two to one and cancelled the
        // retreat by attacking again.
        for (const Client::HostileHit& h : hostiles)
            MarkUnreachable(h.serial, obs.nowMs);
        if (!state_.memory.HasEvent("first_near_death")) {
            state_.memory.NoteEvent("first_near_death", hostiles.front().name.c_str(),
                                    "", obs.x, obs.y, obs.nowMs);
        }
        // Retreat toward somewhere known-safe rather than a random direction.
        const KnownPlace* bank = state_.memory.NearestPlace("bank", obs.x, obs.y);
        if (bank && !client.TravelBusy()) {
            client.TravelToPoint(bank->x, bank->y, 3, "flee_to_bank");
        } else if (!client.TravelBusy()) {
            // A new character may not have personally recorded a bank yet;
            // the world atlas still knows where guarded bankers are.
            client.TravelToService(wm::Service::Banker,
                                   HomeOrNearest(state_.homeCity));
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
    // Attack is a target-selection command, not a weapon swing.  Once the
    // server has accepted it, it owns the normal melee cadence.  Reissuing it
    // each 1.2-second life tick was resetting that cadence before the first
    // hit could resolve, leaving a healthy Zombie and a dead newcomer.
    constexpr i64 kAttackReassertMs = 6000;
    if (lastAttackOrderTarget_ != target->serial ||
        obs.nowMs - lastAttackOrderMs_ >= kAttackReassertMs) {
        client.ActionAttack(target->serial);
        lastAttackOrderTarget_ = target->serial;
        lastAttackOrderMs_ = obs.nowMs;
    }
    if (dist > 1 && !client.GotoBusy()) client.ActionGotoMobile(target->serial, 1);
    nextActionMs_ = obs.nowMs + 1200;
    return false;
}

bool Runner::DoHeal(Client& client, const Observation& obs) {
    // See docs/S2_WIRING_PLAN.md S2.1 for the field-source table this mirrors.
    HealSight see;
    see.hp = obs.hp;
    see.hpMax = obs.hpMax;
    see.mana = obs.mana;
    see.bandages = obs.bandages;
    see.healPotions = obs.healPotions;
    // UNKNOWN: this does not prove *Heal* is in the spellbook -- that needs
    // BookHasGraphic with the book open, not just a skill/spellbook check.
    // Left false for this slice; a crafter has no Magery at all (the R4 pair
    // are miner_smith / lumberjack_swordsman), so nothing here loses by it.
    see.canCastHeal = false;
    // obs.gold is the BANK total on this shard, not the pack (obs.goldOnHand
    // is that) -- "can this be fixed with money" is the bank question, not
    // "can I hand it over right now".
    see.gold = obs.gold;
    // The same four graphics DoMakeBandages walks, in the same order.
    see.hasBandageMaterial =
        FindAny(client, kCuttableClothing,
                sizeof(kCuttableClothing) / sizeof(kCuttableClothing[0])) !=
            0 ||
        client.FindBackpackItemByGraphic(kClothGraphic) != 0 ||
        client.FindBackpackItemByGraphic(kClothBoltGraphic) != 0 ||
        client.FindBackpackItemByGraphic(kWoolGraphic) != 0;
    see.hungry = obs.hungry;
    // Under attack right now, not merely near a hostile -- a cow standing
    // next to the character is not a fight.
    see.inDanger = obs.underAttack;

    // A bandage on an untrained healer only produces the shard's "barely
    // help" one-point result.  A warrior must not consume its emergency kit
    // that way: obtain a healing potion first, then resume healing/training.
    constexpr i32 kUsableHealingTenths = 300;
    if (obs.SkillTenths(rules::kHealing) < kUsableHealingTenths &&
        see.healPotions == 0 && obs.gold >= 200) {
        return HandOff(GoalKind::Heal, GoalKind::ReplaceEquipment, 60000,
                       "Healing is untrained; buying a potion instead of wasting bandages",
                       obs.nowMs);
    }

    HealTuning tune;
    tune.healHpFraction = needCfg_.healHpFraction;
    // UNKNOWN until an observation exists; the struct default of 2 stands in
    // for it until then.
    if (const market::PriceObservation* p = state_.prices.Latest(
            "bandage", market::PriceSource::NpcVendorSells)) {
        tune.bandagePrice = p->pricePerUnit;
    }
    // UNKNOWN as a field: needCfg_.goldFloor (100) is the nearest honest
    // number, but the bandage errand deliberately spends the character's
    // last coin on purpose (see the reserve comment near Runner.cpp:3314) --
    // so this is left at the struct default of 0 rather than block the poor
    // branch on a number that contradicts existing behaviour.

    const HealPlan p = DecideHeal(see, tune);
    if (p.step != lastHealPlan_) {
        LogPlan(HealStepName(p.step), p.reason);
        lastHealPlan_ = p.step;
    }

    switch (p.step) {
        case HealStep::None:
            return true;

        case HealStep::Bandage:
            // SurvivalTick owns the actual bandage timing (it knows the ~3s
            // skill delay and will not restart a running heal, which is the
            // bug that made uo-offline's first bandage loop heal nothing at
            // all) -- but only at <=60% HP, out of contact (CombatPolicy's
            // kPotionPercent). DecideHeal fires Bandage anywhere below
            // healHpFraction (80%), so 61-79% was nobody's: SurvivalTick
            // would not act (pct > 60) and this arm only delegated, which
            // meant a HP band where the character silently never healed.
            // Below 60% we still only make sure nothing else is competing
            // for the body and let SurvivalTick do the actual bandaging;
            // above it, apply the bandage ourselves with the same client
            // call SurvivalTick uses.
            if (client.WarModeOn() && obs.hostilesNear == 0)
                client.EnsurePeaceMode();
            if (obs.HpFraction() <= 0.60) {
                nextActionMs_ = obs.nowMs + 2000;
                planner_.NoteAttempt(obs.nowMs);
                return false;
            }
            if (client.ActionBusy()) {
                nextActionMs_ = obs.nowMs + 2000;
                planner_.NoteAttempt(obs.nowMs);
                return false;
            }
            {
                const u32 bandage = client.FindBackpackItemByGraphic(kBandage);
                if (bandage) {
                    client.ActionUseBandage(bandage, client.PlayerSerial());
                    nextActionMs_ = obs.nowMs + 4000;
                    planner_.NoteProgress();
                } else {
                    nextActionMs_ = obs.nowMs + 2000;
                    planner_.NoteAttempt(obs.nowMs);
                }
            }
            return false;

        case HealStep::DrinkPotion: {
            // SurvivalTick already drinks autonomously at <=60% HP once out
            // of contact -- below that line it owns the tick, and a second
            // click here would race it. DoHeal only acts above 60%, and only
            // once SurvivalTick's own click (if any) is not still in flight.
            // The comparison is done in the same integer percent
            // combat::HealthPercent uses (not obs.HpFraction()'s double), so
            // the two never disagree about which side of 60% a tick is on.
            const bool aboveSurvivalLine =
                obs.hpMax > 0 && (obs.hp * 100) / obs.hpMax > 60;
            const u32 potion = client.FindBackpackItemByGraphic(kHealPotion);
            if (aboveSurvivalLine && !client.ActionBusy() && potion != 0) {
                client.ActionUseObject(potion);
                nextActionMs_ = obs.nowMs + 2500;
                planner_.NoteProgress();
            } else {
                nextActionMs_ = obs.nowMs + 2000;
                planner_.NoteAttempt(obs.nowMs);
            }
            return false;
        }

        case HealStep::CastHeal:
            // Unreachable while canCastHeal is hardwired false above. Casting
            // a spell id is a new mechanic this slice does not add.
            nextActionMs_ = obs.nowMs + 3000;
            return false;

        case HealStep::BuySupplies:
            // NOT GoalKind::BuySupplies -- DoBuySupplies shops for craft
            // inputs only. The bandage/potion buyer is ReplaceEquipment.
            return HandOff(GoalKind::Heal, GoalKind::ReplaceEquipment, 60000,
                           "nothing to heal with; going shopping", obs.nowMs);

        case HealStep::MakeBandages:
            return HandOff(GoalKind::Heal, GoalKind::MakeBandages, 60000,
                           "too poor to buy; cutting cloth", obs.nowMs);

        case HealStep::Rest:
            // No NoteProgress -- resting is not progress; five of these trip
            // the anti-spin backstop, which is correct here.
            nextActionMs_ = obs.nowMs + 5000;
            return false;

        case HealStep::Stuck:
            LogLine("goal_stuck=HEAL reason=\"%s\"", p.reason);
            return HandOff(GoalKind::Heal, GoalKind::GetFood, 120000, p.reason,
                           obs.nowMs);
    }
    return false;
}

// --- corpse ----------------------------------------------------------------

bool Runner::DoRecoverCorpse(Client& client, const Observation& obs) {
    const travel::DeathRecord& death = client.Knowledge().LastDeath();

    // Corpse serials are intentionally session-local.  Once we are back at
    // the recorded tile, the normal world-item stream makes the corpse visible
    // again; bind that fresh serial before attempting to open it.
    if (death.valid && death.corpseSerial == 0 &&
        TileDist(death.x, death.y, obs.x, obs.y) <= 8) {
        const u32 corpse = client.FindWorldItemByGraphic(0x2006, 8);
        if (corpse) {
            client.Knowledge().NoteCorpse(corpse, death.x, death.y, 0);
            LogLine("corpse run: found own corpse 0x%08X at the death site", corpse);
        }
    }

    RecoverySight see;
    see.dead = obs.dead;
    see.corpseKnown = obs.corpseKnown;
    see.corpseDistance = TileDist(death.x, death.y, obs.x, obs.y);
    // This character's OWN memory of the corpse's place, not of here --
    // somewhere it died three times is dangerous to it specifically.
    see.dangerHeatAtCorpse = state_.memory.DangerHeatAt(death.x, death.y, obs.nowMs);
    see.hpFraction = obs.HpFraction();
    see.attemptsSoFar = obs.corpseRecoveryAttempts;
    // Unknown is not empty: only claim corpseEmpty once the container has
    // actually been opened and counted (mirrors the ContainerKnown gate the
    // handler always kept before opening).
    see.corpseEmpty = death.corpseSerial != 0 &&
                       client.ContainerKnown(death.corpseSerial) &&
                       client.ContainerItemCount(death.corpseSerial) == 0;
    // A DEATH RECORD OUTLIVES A CORPSE (sphere.ini CorpsePlayerDecay=7 min).
    // Count the decisions spent standing on the tile with nothing bound so
    // DecideRecovery can call it gone instead of opening serial 0 forever.
    see.corpseVisible = death.corpseSerial != 0;
    if (see.corpseVisible || see.corpseDistance > 2) corpseProbesAtSite_ = 0;
    else if (!obs.dead) ++corpseProbesAtSite_;
    see.probesAtSite = corpseProbesAtSite_;
    // UNKNOWN (S2_WIRING_PLAN.md S2.3): no cheap "loose gear in the pack"
    // read exists without duplicating MayWear's loop (Runner.cpp:8440-ish).
    // Left false -- DoUpgradeGear/DoReplaceEquipment re-dress on their own
    // goal, which is what happens today; ReEquip below is unreachable.
    see.gearInPack = false;

    // RecoveryTuning: riskTolerance, minHpToReturn and maxAttempts all left
    // at their struct defaults. riskTolerance is UNKNOWN -- no per-character
    // personality field exists yet (spec S3 defers it).
    RecoveryTuning tune;

    const RecoveryPlan plan = DecideRecovery(see, tune);
    if (plan.step != lastRecoveryPlan_) {
        LogPlan(RecoveryStepName(plan.step), plan.reason);
        lastRecoveryPlan_ = plan.step;
    }

    switch (plan.step) {
        case RecoveryStep::SeekResurrection:
            // Same guard as DoSurvive: one outstanding resurrection request,
            // not one every three seconds against a fifteen-minute deadline.
            // Both goals can be the one running while the character is a
            // ghost, so both had the fault.
            if (client.ActionBusy()) return false;
            client.ActionResurrectAccept();
            nextActionMs_ = obs.nowMs + 10000;
            return false;

        case RecoveryStep::Recover:
            // Mandatory: without this cooldown RecoverCorpse (950) outscores
            // Heal (700) forever and the character never heals up to walk
            // back -- the exact death loop this handler exists to prevent.
            return HandOff(GoalKind::RecoverCorpse, GoalKind::Heal, 60000,
                           plan.reason, obs.nowMs);

        case RecoveryStep::TravelToCorpse:
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

        case RecoveryStep::Loot:
            // Standing on it. Open, then take everything the container
            // reports.
            // Never address a request to nobody: serial 0 is answered with
            // "invalid_state / null serial" as fast as it is asked, which is
            // a busy loop, not an attempt.  CorpseGone above ends it.
            if (death.corpseSerial == 0) {
                nextActionMs_ = obs.nowMs + 1000;
                return false;
            }
            if (!client.ContainerKnown(death.corpseSerial)) {
                if (client.ActionBusy()) return false;
                client.ActionOpenContainer(death.corpseSerial);
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
            if (client.ActionBusy()) return false;
            {
                u32 serial = 0;
                u16 graphic = 0, amount = 0;
                if (client.ContainerItemAt(death.corpseSerial, 0, &serial, &graphic,
                                           &amount)) {
                    client.TakeFromContainer(serial, amount ? amount : 1);
                    planner_.NoteProgress();
                    nextActionMs_ = obs.nowMs + 900;
                }
            }
            return false;

        case RecoveryStep::ReEquip:
            // Unreachable while gearInPack is left false above (S2.3 scope).
            return HandOff(GoalKind::RecoverCorpse, GoalKind::ReplaceEquipment,
                           30000, plan.reason, obs.nowMs);

        case RecoveryStep::CorpseGone:
            // A REAL FAILURE with a reason, not a decision: the loot is gone.
            // That is Revolution death -- no shortcuts, no retry. Clear the
            // record so the need dies with it, cool the goal down so a stale
            // 950-point urgency cannot outscore everything else again, and
            // go get dressed.
            {
            const i32 deathX = death.x;
            const i32 deathY = death.y;
            client.Knowledge().ClearDeath();
            corpseProbesAtSite_ = 0;
            state_.memory.NoteEvent("corpse_lost", plan.reason, "", deathX,
                                    deathY, obs.nowMs);
            LogLine("goal_failed=RECOVER_CORPSE reason=\"%s\"", plan.reason);
            return HandOff(GoalKind::RecoverCorpse, GoalKind::ReplaceEquipment,
                           600000, plan.reason, obs.nowMs);
            }

        case RecoveryStep::Abandon:
            // A completed decision, not a failure -- never Finish(false).
            {
            const i32 deathX = death.x;
            const i32 deathY = death.y;
            client.Knowledge().ClearDeath();
            state_.memory.NoteEvent("corpse_abandoned", plan.reason, "", deathX,
                                    deathY, obs.nowMs);
            return true;
            }

        case RecoveryStep::Done:
            {
            const i32 deathX = death.x;
            const i32 deathY = death.y;
            client.Knowledge().ClearDeath();
            state_.memory.NoteEvent("corpse_recovered", "", "", deathX, deathY,
                                    obs.nowMs);
            return true;
            }
    }
    return true;
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
            // HELD/WORN, ACROSS EVERY GRAPHIC THE TOOL WEARS, and counting
            // the HANDS as well as the pack -- the same question obs.toolsHeld
            // asks a few hundred lines above, answered the same way. Three
            // regressions lived in the narrower version this replaces:
            //
            //  * the layer came from t.graphics[0] ALONE, so a tool whose
            //    first listed graphic has no itemdef layer reported layer 0
            //    for the whole entry;
            //  * `held` counted the PACK only, so a tool already in the hand
            //    read held=0 -- and a flip-graphic tool (kPickaxe and kHatchet
            //    are two graphics each) also read worn != graphics[0], because
            //    the wielded half is the OTHER graphic. Together that is
            //    DecideAcquire's "none held, and it may be bought": the
            //    character walks to a smith and buys a second pickaxe while
            //    swinging the first;
            //  * mustWear was `layer != 0` -- i.e. "anything equippable must
            //    be equipped" -- which ignored ToolNeed::mustBeWielded, the
            //    field the catalogue sets for exactly this and which no code
            //    in src/ read at all. A saw or a sewing kit works from the
            //    pack; only the SRC.WEAPON skills need the hand.
            // (audit 2026-08-30, finding 4.)
            u8 layer = 0;
            for (u16 g : t.graphics) {
                layer = client.ItemEquipLayer(g);
                if (layer) break;
            }
            const u16 canonical = t.graphics.empty() ? 0 : t.graphics[0];
            i32  held = 0;
            u32  have = 0;
            bool wielded = false;
            for (u16 g : t.graphics) {
                if (!g) continue;
                held += static_cast<i32>(client.BackpackItemCount(g));
                if (!have) have = client.FindBackpackItemByGraphic(g);
                if (client.EquippedGraphicAt(kLayerHand1) == g ||
                    client.EquippedGraphicAt(kLayerHand2) == g ||
                    (layer && client.EquippedGraphicAt(layer) == g))
                    wielded = true;
            }
            // A TOOL IN THE HAND IS A TOOL HELD. Reported as the canonical
            // graphic so DecideAcquire's `worn == req.graphic` test sees the
            // tool it asked about rather than whichever flip-frame the server
            // put on the paperdoll -- the same shape the garment scan in
            // DoReplaceEquipment uses.
            if (wielded) ++held;
            const u16 worn = wielded ? canonical : 0;

            life::AcquireRequest req;
            req.graphic = canonical;
            req.item = t.name.c_str();
            req.desiredTotal = 1;
            req.layer = layer;
            req.mustWear = t.mustBeWielded;
            // A profession that names a tool can use it -- unlike armour,
            // nothing here gates which tools this life may wield.
            req.wearable = true;

            const life::AcquirePlan plan = life::DecideAcquire(req, held, worn);
            // Keyed by THIS tool's own name -- see Runner.h's comment on
            // lastToolAcquirePlanByItem_ for the alternating-log bug a single
            // shared sentinel produced here.
            AcquireStep& lastStep = lastToolAcquirePlanByItem_[t.name];
            if (plan.step != lastStep) {
                LogPlan(life::AcquireStepName(plan.step), plan.reason);
                lastStep = plan.step;
            }
            // THE PAPERDOLL IS THE VERIFICATION, AND IT ARRIVES NEXT TICK.
            // An equip is an ask with a server answer; "I sent the packet" is
            // not "it is in my hand" (acquire.h, rule 2). The old code called
            // NoteProgress() the instant it sent ActionEquip, so a shard that
            // silently refused the wield reset the failure ladder every 1.2
            // seconds and the planner's backstop -- the thing that ends a goal
            // doing nothing -- could never fire.
            int& wearTries = toolWearAttemptsByItem_[t.name];
            if (plan.step == life::AcquireStep::Done) {
                // Done AFTER we asked means EquippedGraphicAt now sees it.
                // THAT is the progress, and it is the only place that says so.
                if (wearTries > 0) {
                    LogLine("tool: the %s is in hand now", t.name.c_str());
                    wearTries = 0;
                    planner_.NoteProgress();
                }
                continue;
            }

            if (plan.step == life::AcquireStep::Wear) {
                if (client.ActionBusy()) return false;
                if (wearTries >= kMaxToolWearTries) {
                    LogLine("goal_blocked=GET_TOOL reason=\"the %s is in the "
                            "pack but %d equip attempts left the hand empty\"",
                            t.name.c_str(), wearTries);
                    state_.memory.NoteEvent("wield_refused", t.name.c_str(), "",
                                            obs.x, obs.y, obs.nowMs);
                    wearTries = 0;
                    return HandOff(GoalKind::GetTool, GoalKind::Bank,
                                   kGearCooldownMs,
                                   "the shard refuses to wield it", obs.nowMs);
                }
                ++wearTries;
                LogLine("tool: putting the %s in hand (attempt %d of %d)",
                        t.name.c_str(), wearTries, kMaxToolWearTries);
                client.ActionEquip(have, layer);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 1200;
                return false;
            }
            if (plan.step == life::AcquireStep::Refuse) {
                // Unreachable while every profession tool is `wearable=true`
                // above -- kept so the switch stays exhaustive if that
                // changes (an STR-gated tool, say).
                LogLine("goal_blocked=GET_TOOL reason=\"%s\"", plan.reason);
                state_.memory.NoteEvent("policy_refused", t.name.c_str(), "",
                                        obs.x, obs.y, obs.nowMs);
                return HandOff(GoalKind::GetTool, GoalKind::Bank,
                               kGearCooldownMs, plan.reason, obs.nowMs);
            }
            // Buy: genuinely missing. Fall through to the shop errand below.
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
        return HandOff(GoalKind::GetTool, GoalKind::IdleBriefly,
                       kNoToolCooldownMs, "no trade known to sell it",
                       obs.nowMs);
    }

    const KnownSupplier* known = state_.memory.BestSupplier(toolName.c_str());

    // A tool purchase is legal under the vendor policy -- a tool is not a
    // resource, and buying one shortcuts no production chain. Verify that
    // here rather than assuming it, because the policy is the thing that
    // keeps the shard's player economy alive.
    const econ::VendorRuling ruling =
        econ::CanBuyFromNPCGraphic(toolGfx.empty() ? 0 : toolGfx[0]);
    if (!ruling.allowed) {
        LogLine("goal_failed=GET_TOOL reason=\"%s\" tool=%s class=%s",
                faucet::RefusalName(faucet::Refusal::RevolutionAuthenticityUnknown),
                toolName.c_str(), econ::VendorClassName(ruling.klass));
        state_.memory.NoteEvent("policy_refused", toolName.c_str(),
                                econ::VendorClassName(ruling.klass),
                                obs.x, obs.y, obs.nowMs);
        // Same stand-down. A policy refusal is a settled answer, not a
        // temporary one -- re-asking it sixty times a second changes nothing.
        return HandOff(GoalKind::GetTool, GoalKind::IdleBriefly,
                       kNoToolCooldownMs, "the vendor policy refuses this tool",
                       obs.nowMs);
    }

    // COIN BEFORE THE SHOP TRIP, not after arriving at it. Fetching it later
    // put two destinations in play at once: the coin errand started walking to
    // the bank, the tool goal re-issued its walk to the smithy on the next
    // tick, and the character announced "looking for a blacksmith" every two
    // and a half seconds without ever arriving anywhere.
    if (FetchCoinForPurchase(client, obs, kToolMoneyToCarry)) return false;

    if (client.TravelBusy()) {
        VetoTripOverSessionBudget(client, obs, GoalKind::GetTool, "GET_TOOL",
                                  kNoToolCooldownMs);
        return false;
    }

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

// SIXTEEN FREE BANDAGES, OFF YOUR OWN BACK.
//
// "unequip the resurrection robe and cut into bandages with scissors" and
// "not any robe only resurrection robe" (project owner, 2026-08-30).
//
// The shard side already works: types/type_scissors.scp cuts t_clothing, and
// a robe's 16.1 weight yields sixteen bandages. It also refuses to cut WORN
// clothing -- the engine calls CanUse(pItemTarg, true) -- which is exactly why
// the robe has to come off first.
//
// WHICH ROBE. Source-X hands out ITEMID_ROBE on LAYER_ROBE at a resurrection
// (CCharSpell.cpp:509-514) and names it "Resurrection Robe". The client reads
// names from tiledata rather than per item, so the name is not available here
// -- but the MOMENT is. Death on this shard is full loot, so in the minutes
// after coming back the only robe a character can possibly be wearing is the
// one the server just conjured. A mage's own robe went to the corpse with
// everything else. Outside that window this does nothing at all, which is the
// point: nobody's real robe gets cut up.
bool Runner::CutResurrectionRobe(Client& client, const Observation& obs) {
    constexpr u16 kResRobe = 0x1F03;      // ITEMID_ROBE
    constexpr u8  kLayerRobe = 22;        // LAYER_ROBE, "robe over all"
    constexpr u16 kScissors[] = {0x0F9E, 0x0DFC};
    // Long enough to walk out of the healer's and find the scissors; far too
    // short to catch a robe bought later in the session.
    constexpr i64 kResRobeWindowMs = 5 * 60 * 1000;

    if (obs.dead || client.ActionBusy()) return false;

    // WHOSE ROBE IS IT? Two ways to be sure, because the robe outlives the
    // session that earned it.
    //
    // 1. This life does not wear cloth. A miner-smith's kit is metal and
    //    leather; a robe on LAYER_ROBE is not part of it and never was, so
    //    whatever put it there, it is spare cloth. This is the case that
    //    matters in practice -- Corwyn resurrected in one process and was
    //    still wearing the robe three sessions later, because a transition
    //    that happened before this program started cannot be observed by it.
    //
    // 2. This life DOES wear cloth (a mage), so a robe might genuinely be
    //    its own -- and only the minutes right after a resurrection are
    //    safe, since full loot means its real robe went to the corpse.
    const bool wearsCloth = needCfg_.profession &&
                            needCfg_.profession->wears == prof::Profession::Wear::Cloth;
    const bool freshlyRaised =
        resurrectedAtMs_ && obs.nowMs - resurrectedAtMs_ <= kResRobeWindowMs;
    if (wearsCloth && !freshlyRaised) return false;

    // Still wearing it: take it off. The server will not let scissors touch
    // worn cloth.
    if (client.EquippedGraphicAt(kLayerRobe) == kResRobe) {
        const u32 worn = client.EquippedAtLayer(kLayerRobe);
        if (worn) {
            LogLine("robe: taking off the resurrection robe -- it is worth "
                    "sixteen bandages");
            client.ActionUnequip(worn);
            nextActionMs_ = obs.nowMs + 1500;
            return true;
        }
    }

    const u32 robe = client.FindBackpackItemByGraphic(kResRobe);
    if (!robe) return false;

    u32 scissors = 0;
    for (u16 g : kScissors) {
        scissors = client.FindBackpackItemByGraphic(g);
        if (scissors) break;
    }
    if (!scissors) {
        // Not a failure worth stalling on -- the robe keeps. Said once per
        // resurrection rather than every tick.
        if (!state_.memory.HasEvent("no_scissors_for_robe")) {
            LogLine("robe: the resurrection robe is in the pack but there are "
                    "no scissors to cut it with");
            state_.memory.NoteEvent("no_scissors_for_robe", "i_scissors", "",
                                    obs.x, obs.y, obs.nowMs);
        }
        return false;
    }

    LogLine("robe: cutting the resurrection robe into bandages");
    client.ActionUseItemOn(scissors, robe);
    nextActionMs_ = obs.nowMs + 2000;
    return true;
}

// SHIRT, TROUSERS, SHOES -- in that order, and out of the pack before the shop.
//
// "nice you did it but now wear your shirt short shoes etc" and "if you have
// your clothing on your bag wear them, you can buy missing parts" (project
// owner, 2026-08-30). Cutting up the resurrection robe leaves a character
// standing in Britain in its underwear, which is not what a player looks
// like.
//
// Wearing costs nothing and needs nobody, so it always comes first; the shop
// is only for what is genuinely absent. The layer comes from tiledata rather
// than a hand-written table, exactly as the armour path does.
bool Runner::WearBasicClothing(Client& client, const Observation& obs) {
    if (obs.dead || client.ActionBusy()) return false;
    for (const ClothingPiece& p : kBasicClothing) {
        bool worn = false;
        const u32 have = ClothingOnHand(client, p, &worn);
        if (worn || !have) continue;
        LogLine("clothes: putting on the %s that was already in the pack",
                p.what);
        // Let the server pick the layer: the pack piece may be any of the
        // slot's variants, not the one this row is named after.
        client.ActionEquip(have, kLayerServerChooses);
        nextActionMs_ = obs.nowMs + 1200;
        return true;
    }
    // Skirts use their own paperdoll layer, not the trousers layer above.
    // Consequently a long skirt sitting in a woman's pack was never seen by
    // the basic-clothing loop and remained invisible even though it could be
    // worn over the starting trousers.  Wear an owned skirt; do not force a
    // shopping trip merely to satisfy a cosmetic choice.
    if (client.PlayerIsFemale()) {
        constexpr u16 kSkirts[] = {0x1516, 0x1531, 0x1537}; // long, short, kilt
        for (u16 graphic : kSkirts) {
            const u8 layer = client.ItemEquipLayer(graphic);
            if (!layer || client.EquippedGraphicAt(layer)) continue;
            const u32 have = client.FindBackpackItemByGraphic(graphic);
            if (!have) continue;
            LogLine("clothes: putting on the skirt that was already in the pack");
            client.ActionEquip(have, kLayerServerChooses);
            nextActionMs_ = obs.nowMs + 1200;
            return true;
        }
    }
    return false;
}

// The weapon-school basic (katana/kryss/club/bow) for a WantsToHunt fighter
// with empty hands -- SchoolWeapon and SchoolWeaponFor live in uo/life.h /
// life/Identity.cpp, pure and Client-free, next to WantsToHunt itself (same
// weapon-skill target, same threshold, so the two never disagree). See there
// for the per-weapon citations.

bool Runner::DoReplaceEquipment(Client& client, const Observation& obs) {
    // FREE BANDAGES BEFORE BOUGHT ONES.
    if (CutResurrectionRobe(client, obs)) {
        planner_.NoteProgress();
        return false;
    }
    // AND DRESS FROM THE PACK BEFORE WALKING TO A SHOP.
    if (WearBasicClothing(client, obs)) {
        planner_.NoteProgress();
        return false;
    }

    // The cheapest fix first: something usable is already in the pack. The axe
    // is preferred -- it is this build's weapon AND its tool, so arming it
    // solves both needs at once.
    if (!obs.weaponEquipped) {
        // WAITING IS NOT PROGRESS.
        //
        // ArmAxe returns true for TWO different things: "I issued an
        // unequip/equip" and "an action is already in flight, come back"
        // (Runner.cpp:1640). Crediting both counted the TICK RATE as work.
        // Measured, wave 2 2026-09-01: Xerxes cut his resurrection robe at
        // 18:08:57.081, the use_item_on stayed in flight until it timed out
        // at 18:09:12.099, and REPLACE_EQUIPMENT completed reporting
        // progress=243 -- one per ~60ms tick of that fifteen-second wait,
        // having armed nothing (run_gates/g_Xerxes.console.txt:72-105).
        // Illyria did the same behind her cast_spell timeouts (progress=101,
        // 110, 70; g_Illyria.console.txt:152,331,461).
        //
        // Worse than the wrong number: NoteProgress() also clears
        // goal_.attempts (Goals.cpp:659), so the failure ladder was reset on
        // every tick and the goal could never run out of tries.
        const bool waiting = client.ActionBusy();
        if (ArmAxe(client, obs)) {
            if (!waiting) planner_.NoteProgress();
            return false;
        }
        const SchoolWeapon* desiredSchool = needCfg_.profession
                                                ? SchoolWeaponFor(*needCfg_.profession)
                                                : nullptr;
        const u32 sword = FindAny(client, kKatana, 2);
        if ((!desiredSchool || desiredSchool->skill == rules::kSwordsmanship) &&
            !AxeInHand(client) && sword) {
            if (client.ActionBusy()) return false;
            LogLine("arming: no axe carried, equipping the sword instead");
            client.ActionEquip(sword, kLayerServerChooses);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }

        // THE BUILD'S OWN SCHOOL WEAPON, ARMED FROM THE PACK OR BOUGHT.
        //
        // Reached only when the axe and the generic katana fallback above
        // both found nothing to ARM. That katana fallback only ever equips
        // one already sitting in the pack -- it never buys -- so a
        // swordsman with none carried falls through to here too, same as a
        // fencer, macefighter or archer. This is the gap the fix closes: the
        // FindAny just below is one more (harmless, redundant) look for a
        // swordsman, and the Buy path below it is the part that was missing
        // for every school, swordsman included.
        if (const SchoolWeapon* school = desiredSchool) {
            const u32 have = FindAny(client, school->graphics, 2);
            if (have) {
                if (client.ActionBusy()) return false;
                LogLine("arming: a %s is in the pack -- equipping it",
                        school->item);
                client.ActionEquip(have, kLayerServerChooses);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }

            // Buy it, or -- decided the same way bandages/garment/potions
            // above decide -- Done (nothing to do) or Refuse. `held` is
            // always 0 here: if it were not, the FindAny check just above
            // would already have armed it and returned.
            life::AcquireRequest req;
            req.graphic = school->graphics[0];
            req.item = school->item;
            req.desiredTotal = 1;
            req.mustWear = false;   // arming is handled above, from the pack
            req.wearable = true;
            req.minimumGoldReserve = 100;
            const life::AcquirePlan plan = life::DecideAcquire(req, 0, 0);

            if (plan.step == life::AcquireStep::Buy || weaponBuy_.Running()) {
                // VENDOR POLICY, ASKED BEFORE THE WALK. Every basic school
                // weapon here is a live smithing/carpentry/bowcraft recipe
                // (SKILLMAKE on each ITEMDEF) -- exactly the "a player craft
                // produces this" class M3.7 exists to keep off an NPC's
                // counter. i_katana/i_kryss/i_club/i_bow carry no
                // VendorPolicy row at all (Unknown, fails safe); i_dagger,
                // the one bladed weapon that IS graded, is PlayerCrafted.
                // Both refuse. This is deliberately NOT loosened here --
                // see the fix report -- so the refusal is asked and obeyed
                // the same way the bandage errand already asks it, rather
                // than silently skipped.
                const econ::VendorRuling ruling =
                    econ::CanBuyFromNPCGraphic(school->graphics[0]);
                if (!ruling.allowed) {
                    LogLine("goal_blocked=REPLACE_EQUIPMENT reason=\"the "
                            "vendor policy grades a %s %s, and no player "
                            "supplier is known\"", school->item,
                            econ::VendorClassName(ruling.klass));
                    state_.memory.NoteEvent("policy_refused", school->defname,
                                            econ::VendorClassName(ruling.klass),
                                            obs.x, obs.y, obs.nowMs);
                    planner_.Finish(false, "no legitimate source of a weapon",
                                    obs.nowMs);
                    return false;
                }
                if (client.TravelBusy()) return false;
                if (!weaponBuy_.Running()) {
                    life::BuyRequest breq;
                    breq.graphic = school->graphics[0];
                    breq.item = school->item;
                    breq.desiredTotal = 1;
                    breq.minimumGoldReserve = 100;
                    breq.Sell("weaponsmith", wm::Service::Blacksmith);
                    if (school->bowyerFallback)
                        breq.Sell("bowyer", wm::Service::Bowyer);
                    weaponBuy_.Begin(breq);
                }
                const life::ActivityTickResult wr =
                    weaponBuy_.Tick(client, obs);
                LogErrandReason("weapon", wr.reason, obs.nowMs);
                if (wr.wake == life::Wake::AfterDelay && wr.delayMs > 0)
                    nextActionMs_ = obs.nowMs + wr.delayMs;
                if (!life::IsTerminal(wr.status)) {
                    if (wr.acted) planner_.NoteAttempt(obs.nowMs);
                    return false;
                }
                if (wr.status == life::ActivityStatus::Success) {
                    planner_.NoteProgress();
                    return false;   // armed on the next pass, see FindAny above
                }
                LogLine("weapon: no %s bought (%s)", school->item, wr.reason);
                const i64 rest =
                    (wr.status == life::ActivityStatus::RetryableFailure)
                        ? kShortRestMs : kGearCooldownMs;
                return HandOff(GoalKind::ReplaceEquipment, GoalKind::Bank,
                               rest, "no weapon bought", obs.nowMs);
            }
        }
    }
    // --- BANDAGES, THE MISSING GARMENT, HEAL POTIONS -----------------------
    //
    // One AcquireRequest per item, decided by DecideAcquire instead of three
    // copies of "empty slot, more wanted" (S2_WIRING_PLAN.md S2.7). ALL
    // THREE plans feed the early-out below; dropping one reproduces the
    // regression this shape exists to prevent -- fifteen picks of
    // REPLACE_EQUIPMENT in one session, every one of them goal_completed
    // progress=0, because the old early-out asked about bandages only while
    // the need that selected the goal said "potions=0 low=2 gold=8993".

    // 1. Bandages -- BUT ONLY FOR A LIFE THAT DECLARES THEM.
    //
    // THE ERRAND MUST ASK WHAT THE NEED ASKS. AssessNeeds gates its bandage
    // clause on WantsConsumable(cfg, "bandage") (Needs.cpp) because a
    // crafting life's catalogue entry deliberately drops Bandages() in favour
    // of CrafterHealPotions() -- "you are crafter you dont have heal skill so
    // buy healing potion 3-4" / "so crafter do not buy bandages" (project
    // owner, 2026-08-30). This errand asked nothing, so it built the request
    // unconditionally and a miner_smith bought THIRTY bandages its zero
    // Healing could never make work. Worse, the Buy branch below returns on
    // every path, so the heal-potion branch behind it -- the one thing that
    // WOULD have kept the crafter alive -- was unreachable for exactly the
    // lives that needed it. (audit 2026-08-30, finding 1.)
    //
    // A family with no bandages in `consumables` treats this plan as Done,
    // which is the truth: it is not short of something it does not carry.
    const bool wantsBandages = life::WantsConsumable(needCfg_, "bandage");
    life::AcquirePlan bandagePlan;   // default Done -- vacuously satisfied
    // `low` triggers a restock attempt; it is not a requirement that one NPC
    // must fill the pack all the way to `restockTo`. Once a partial purchase
    // crosses the safety floor, move on to the other missing kit.
    if (wantsBandages && obs.bandages < needCfg_.bandageLow) {
        life::AcquireRequest bandageReq;
        bandageReq.graphic = kBandage;
        bandageReq.item = "bandages";
        bandageReq.desiredTotal = needCfg_.bandageFull;
        bandageReq.mustWear = false;
        bandageReq.wearable = true;
        // goldFloor stays ZERO here deliberately: bandages ARE the emergency
        // reserve. A character that will not spend its last coin on the thing
        // that keeps it alive has misunderstood what the reserve is for.
        bandageReq.minimumGoldReserve = 0;
        bandageReq.Sell("healer", wm::Service::Healer);
        bandagePlan = life::DecideAcquire(bandageReq, obs.bandages, 0);
    }
    if (bandagePlan.step != lastBandageAcquirePlan_) {
        LogPlan(life::AcquireStepName(bandagePlan.step), bandagePlan.reason);
        lastBandageAcquirePlan_ = bandagePlan.step;
    }

    // 2. The missing garment. Only what the pack could not supply, one piece
    // per visit: the errand re-runs, and a shirt bought this trip is worn by
    // WearBasicClothing at the top of the next one before anything else is
    // considered. Same scan order (shirt, trousers, shoes) as before.
    const ClothingPiece* garment = nullptr;
    life::AcquirePlan garmentPlan;   // default Done -- vacuously satisfied
    for (const ClothingPiece& p : kBasicClothing) {
        const u8 layer = client.ItemEquipLayer(p.graphic);
        if (!layer) continue;
        bool worn = false;
        const u32 have = ClothingOnHand(client, p, &worn);
        life::AcquireRequest req;
        req.graphic = p.graphic;
        req.item = p.what;
        req.desiredTotal = 1;
        req.layer = layer;
        req.mustWear = true;
        req.wearable = true;
        const u16 wornGraphic = worn ? p.graphic : 0;
        const life::AcquirePlan plan =
            life::DecideAcquire(req, have ? 1 : 0, wornGraphic);
        if (plan.step == life::AcquireStep::Done) continue;
        garment = &p;
        garmentPlan = plan;
        break;
    }
    if (garment && (garmentPlan.step != lastGarmentAcquirePlan_ ||
                    garment->what != lastGarmentAcquireItem_)) {
        LogPlan(life::AcquireStepName(garmentPlan.step), garmentPlan.reason);
        lastGarmentAcquirePlan_ = garmentPlan.step;
        lastGarmentAcquireItem_ = garment->what;
    }

    // 3. Heal potions. HealPotions() has sat in the profession catalogue
    // since M5 and no code path ever filled it: warriors declared a need for
    // eight and carried none. "you are crafter you dont have heal skill so
    // buy healing potion 3-4" and "you can buy from same place you buy
    // healer" (project owner, 2026-08-30). The healer sells them -- the same
    // counter the bandage errand above already walks to; the alchemist is
    // the fallback (tm_vend's ALCHEMIST list carries i_potion_heal at {3 18}).
    const prof::ConsumableNeed* potions = nullptr;
    if (needCfg_.profession) {
        for (const prof::ConsumableNeed& c : needCfg_.profession->consumables) {
            if (c.name == "heal potion") { potions = &c; break; }
        }
    }
    life::AcquirePlan potionPlan;   // default Done -- vacuously satisfied
    u16 potionGfx = 0;
    if (potions && !potions->graphics.empty()) {
        // The graphic comes from the need itself rather than a second copy
        // of the constant: one table, one truth.
        potionGfx = potions->graphics.front();
        const i32 held = static_cast<i32>(client.BackpackItemCount(potionGfx));
        if (held < potions->low) {
            life::AcquireRequest req;
            req.graphic = potionGfx;
            req.item = "heal potion";
            req.desiredTotal = potions->restockTo;
            req.mustWear = false;
            req.wearable = true;
            // Unlike bandages this is not the last-coin emergency: a character
            // that spends its final gold on potions cannot buy the ore that
            // earns the next lot.
            req.minimumGoldReserve = 50;
            req.Sell("healer", wm::Service::Healer);
            req.Sell("alchemist", wm::Service::Alchemist);
            potionPlan = life::DecideAcquire(req, held, 0);
        }
    }
    if (potionPlan.step != lastPotionAcquirePlan_) {
        LogPlan(life::AcquireStepName(potionPlan.step), potionPlan.reason);
        lastPotionAcquirePlan_ = potionPlan.step;
    }

    // ALL THREE DONE. Every plan in this check, on purpose -- dropping one is
    // the exact regression named above.
    if (obs.weaponEquipped && bandagePlan.step == life::AcquireStep::Done &&
        garmentPlan.step == life::AcquireStep::Done &&
        potionPlan.step == life::AcquireStep::Done)
        return true;

    // Refuse: unreachable today -- bandages and potions are never `mustWear`,
    // and every garment above is `wearable=true`. Kept so the switch stays
    // exhaustive once armour (MayWear-gated) joins this errand.
    if (bandagePlan.step == life::AcquireStep::Refuse) {
        LogLine("goal_blocked=REPLACE_EQUIPMENT reason=\"%s\"", bandagePlan.reason);
        state_.memory.NoteEvent("policy_refused", "i_bandage", "", obs.x, obs.y,
                                obs.nowMs);
        return HandOff(GoalKind::ReplaceEquipment, GoalKind::Bank,
                       kGearCooldownMs, bandagePlan.reason, obs.nowMs);
    }
    if (garment && garmentPlan.step == life::AcquireStep::Refuse) {
        LogLine("goal_blocked=REPLACE_EQUIPMENT reason=\"%s\"", garmentPlan.reason);
        state_.memory.NoteEvent("policy_refused", garment->item, "", obs.x,
                                obs.y, obs.nowMs);
        return HandOff(GoalKind::ReplaceEquipment, GoalKind::Bank,
                       kGearCooldownMs, garmentPlan.reason, obs.nowMs);
    }
    if (potionPlan.step == life::AcquireStep::Refuse) {
        LogLine("goal_blocked=REPLACE_EQUIPMENT reason=\"%s\"", potionPlan.reason);
        state_.memory.NoteEvent("policy_refused", "i_potion_heal", "", obs.x,
                                obs.y, obs.nowMs);
        return HandOff(GoalKind::ReplaceEquipment, GoalKind::Bank,
                       kGearCooldownMs, potionPlan.reason, obs.nowMs);
    }

    // Buy: bandages. The `bandageBuy_.Running()` half of the guard keeps an
    // in-flight purchase ticking to a terminal status even on a tick where
    // the pack has already crossed back above the decision's own threshold.
    //
    // IN CATALOGUE ORDER, FIRST ACTIONABLE WINS: bandages, then the garment,
    // then heal potions. Each branch runs only when ITS OWN plan says Buy, so
    // a life that wants no bandages falls straight through to the potion
    // branch instead of being stopped by a request it never made.
    if (wantsBandages &&
        (bandagePlan.step == life::AcquireStep::Buy || bandageBuy_.Running())) {
        const econ::VendorRuling ruling = econ::CanBuyFromNPCGraphic(kBandage);
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

        // --- ONE ACTIVITY, EVERY PURCHASE --------------------------------
        //
        // This goal no longer knows how buying works. It states WHAT it wants
        // to end up holding and who might sell it; BuyActivity does the sums
        // (shortfall, reserve, ceiling) and VendorErrand does the handshake
        // (find a shopkeeper who is not a guildmaster, ask who is present,
        // walk into reach, open, clamp to stock, verify the pack moved).
        //
        // Note the request is a TOTAL, not a quantity to buy. That phrasing
        // is what makes "I already have thirty" expressible at all -- the
        // version that said "buy twenty" is the one that bought six heater
        // shields because a slot was still empty.
        if (!bandageBuy_.Running()) {
            life::BuyRequest req;
            req.graphic = kBandage;
            req.item = "clean bandages";
            req.desiredTotal = needCfg_.bandageFull;
            req.minimumGoldReserve = 0;
            req.Sell("healer", wm::Service::Healer);
            bandageBuy_.Begin(req);
        }
        const life::ActivityTickResult r = bandageBuy_.Tick(client, obs);
        LogErrandReason("bandages", r.reason, obs.nowMs);
        if (r.wake == life::Wake::AfterDelay && r.delayMs > 0)
            nextActionMs_ = obs.nowMs + r.delayMs;

        if (!life::IsTerminal(r.status)) {
        // AN ASK IS AN ATTEMPT; A WAIT IS NOT.
        //
        // This counted every non-terminal poll, so the ~60ms tick rate --
        // not the errand -- decided when the goal ran out of tries. Bruin's
        // potion errand issued ONE vendor ask, which needs its full 8s
        // deadline, and the five "an action is already in flight" polls
        // behind it spent the whole budget in 300ms
        // (run_r4/w_Bruin.console.txt:317-323). REPLACE_EQUIPMENT was
        // re-picked 39 times while that single ask was still outstanding.
            if (r.acted) planner_.NoteAttempt(obs.nowMs);
            return false;
        }

        if (r.status == life::ActivityStatus::Success) {
            // Learn the shop. The activity deliberately does not write
            // memory -- remembering where the bandages came from is a life's
            // business, not a purchase's.
            for (const Client::VendorItem& v : client.VendorOffer()) {
                if (v.graphic != kBandage) continue;
                KnownSupplier s;
                s.need = "bandage";
                s.name = v.name;
                s.sourceType = "npc_vendor";
                s.serial = bandageBuy_.Keeper();
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

        // Every other terminal status ends the goal. The activity has stopped
        // running, so a caller that recognised only Failed would Begin() a
        // fresh one next tick -- the spin this whole layer exists to end,
        // reintroduced at the seam.
        LogLine("goal_failed=REPLACE_EQUIPMENT status=%s reason=\"%s\"",
                life::ActivityStatusName(r.status), r.reason);
        const i64 rest = (r.status == life::ActivityStatus::RetryableFailure)
                             ? kShortRestMs : kGearCooldownMs;
        return HandOff(GoalKind::ReplaceEquipment, GoalKind::MakeBandages, rest,
                       "no bandages bought", obs.nowMs);
    }

    // A hunter's first trip must be for survival gear, not a civilian
    // wardrobe.  Hector had no legal armour, yet the generic equipment goal
    // chose missing trousers and sent him from Minoc to Vesper's tailor before
    // he could safely start the graveyard loop.  Free clothing in the pack is
    // still worn above; defer only a PURCHASE until the fighter has at least
    // one legal armour piece.
    const bool hunterNeedsArmor =
        needCfg_.profession && WantsToHunt(*needCfg_.profession) &&
        !HasBasicArmor(client, obs);

    // Buy: the missing garment.
    if (!hunterNeedsArmor && garment &&
        (garmentPlan.step == life::AcquireStep::Buy ||
                    clothingBuy_.Running())) {
        if (client.TravelBusy()) return false;
        if (!clothingBuy_.Running()) {
            LogLine("clothes: no %s on the body or in the pack -- buying one",
                    garment->what);
            life::BuyRequest req;
            req.graphic = garment->graphic;
            req.item = garment->item;
            req.desiredTotal = 1;
            req.minimumGoldReserve = 20;
            // A cobbler for the shoes, a tailor for the cloth -- and the
            // provisioner as the catch-all, because a small town has one of
            // those when it has neither of the others.
            req.Sell(garment->firstSeller, wm::Service::Tailor);
            req.Sell("tailor", wm::Service::Tailor);
            req.Sell("provisioner", wm::Service::Provisioner);
            clothingBuy_.Begin(req);
        }
        const life::ActivityTickResult cr = clothingBuy_.Tick(client, obs);
        LogErrandReason("clothes", cr.reason, obs.nowMs);
        if (cr.wake == life::Wake::AfterDelay && cr.delayMs > 0)
            nextActionMs_ = obs.nowMs + cr.delayMs;
        if (!life::IsTerminal(cr.status)) {
            // An ask is an attempt; a wait is not. See the note above.
            if (cr.acted) planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        if (cr.status == life::ActivityStatus::Success) {
            planner_.NoteProgress();
            return false;   // worn on the next pass
        }
        LogLine("clothes: no %s bought (%s)", garment->what, cr.reason);
    }

    // Buy: heal potions.
    if (potions && !potions->graphics.empty() &&
        (potionPlan.step == life::AcquireStep::Buy || potionBuy_.Running())) {
        if (client.TravelBusy()) return false;
        if (!potionBuy_.Running()) {
            const i32 held =
                static_cast<i32>(client.BackpackItemCount(potionGfx));
            LogLine("potions: carrying %d heal potion(s), below %d -- "
                    "buying up to %d", held, potions->low,
                    potions->restockTo);
            life::BuyRequest req;
            req.graphic = potionGfx;
            req.item = "heal potion";
            req.desiredTotal = potions->restockTo;
            req.minimumGoldReserve = 50;
            req.Sell("healer", wm::Service::Healer);
            req.Sell("alchemist", wm::Service::Alchemist);
            potionBuy_.Begin(req);
        }
        const life::ActivityTickResult pr = potionBuy_.Tick(client, obs);
        LogErrandReason("potions", pr.reason, obs.nowMs);
        if (pr.wake == life::Wake::AfterDelay && pr.delayMs > 0)
            nextActionMs_ = obs.nowMs + pr.delayMs;

        if (!life::IsTerminal(pr.status)) {
            // An ask is an attempt; a wait is not. See the note above.
            if (pr.acted) planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        if (pr.status == life::ActivityStatus::Success) {
            planner_.NoteProgress();
            return false;
        }
        // A shop that would not sell is not a reason to keep the goal
        // spinning; the next errand can have the turn.
        LogLine("potions: none bought (%s)", pr.reason);
    }

    // FALLING OFF THE END IS NOT SUCCESS.
    //
    // The ONE genuine completion of this goal is the all-plans-Done early-out
    // above. Everything that reaches here got past it -- so something on the
    // list is still missing and this pass did not fix it: the shop would not
    // sell, the purse was empty, or a plan was in a state no branch acts on.
    // Reporting `goal_completed` for that is the exact defect the anti-spin
    // backstop exists to catch, and it caught it: Aelia completed
    // REPLACE_EQUIPMENT fifteen times with progress=0 on 2026-09-01, every
    // one of them this line, with 0 gold and a healer quoting 30
    // (g_Aelia.console.txt; g_Illyria.console.txt:150-152 shows the same shape
    // -- "0 gold with a floor of 50 cannot buy one heal potion at 30" and then
    // goal_completed).
    //
    // So stand down properly: say why, cool the goal off, and finish FAILED so
    // the planner hands the turn to something that can act. Same rule
    // DoEarnGold already follows for "nothing spare to sell".
    LogLine("equipment: nothing on the list could be replaced this pass -- "
            "standing down so something that CAN act gets a turn");
    planner_.Cooldown(GoalKind::ReplaceEquipment, obs.nowMs + kGearCooldownMs);
    planner_.Finish(false, "nothing on the equipment list could be replaced",
                    obs.nowMs);
    return false;
}

// --- banking ---------------------------------------------------------------

void Runner::IssueBankItemMove(Client& client, const Observation& obs,
                               u32 serial, u16 amount, u32 box) {
    client.ActionMoveItem(serial, amount, box);
    bankItemMovePending_ = true;
    nextActionMs_ = obs.nowMs + 1500;
}

bool Runner::SettleBankItemMove(Client& client, const Observation& obs) {
    if (!bankItemMovePending_ || client.ActionBusy()) return false;
    bankItemMovePending_ = false;

    const act::Result r = client.ActionResult();
    if (r == act::Result::Success) {
        bankItemMoveFails_ = 0;
        planner_.NoteProgress();
        return false;
    }

    // A DEPOSIT THAT DID NOT LAND IS NOT PROGRESS, AND THE THIRD ONE IS THE
    // LAST. Source-X refuses every drop into a bank box unless the character
    // is standing on the exact tile the box was opened from
    // (CClientEvent.cpp:448-467) and bounces the item back with a plain 0x25,
    // so a character that walked one step deposits nothing, forever, in
    // silence. Letting the box go forces the next tick to walk to a banker
    // and open a fresh one, which re-stamps that tile.
    ++bankItemMoveFails_;
    client.ForgetBankContainer();
    planner_.NoteAttempt(obs.nowMs);
    if (bankItemMoveFails_ >= kMaxBankItemMoveFails) {
        LogLine("bank: %d deposits in a row did not land (last: %s) -- "
                "standing the bank goal down instead of asking again",
                bankItemMoveFails_, act::ResultName(r));
        bankItemMoveFails_ = 0;
        nextActionMs_ = obs.nowMs + 20000;
    } else {
        LogLine("bank: the deposit did not land (%s, %d of %d) -- opening the "
                "box again before trying",
                act::ResultName(r), bankItemMoveFails_,
                kMaxBankItemMoveFails);
        nextActionMs_ = obs.nowMs + 2500;
    }
    return true;
}

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
        // The box is open, so whoever we asked did answer. Forgiving the
        // bankers we had written off is BankErrand's own business now
        // (NpcRotation::NoteAnswered), which is why nothing is cleared here.
        bankErrand_.Cancel();
        if (client.ActionBusy()) return false;

        // Read the LAST deposit before asking for another one.
        if (SettleBankItemMove(client, obs)) return false;

        // STAND STILL TO USE THE BOX. The bank box only answers from the tile
        // it was opened on (Source-X CClientEvent.cpp:448-467 for drops,
        // CCharStatus.cpp:1063-1069 for lifts); a deposit issued mid-stride is
        // a guaranteed silent bounce. The coin withdrawal below already knew
        // this the hard way -- every branch needs it.
        if (client.TravelBusy()) return false;
        if (!client.BankOpenTileHeld()) {
            LogLine("bank: the box was opened at (%d,%d) and we are at "
                    "(%d,%d) -- opening it again from here",
                    client.BankOpenX(), client.BankOpenY(), obs.x, obs.y);
            client.ForgetBankContainer();
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        }

        // Keep one working smithing batch but bank the rest.  The generic
        // keep-list below protects all declared crafting inputs; for miner
        // smiths that previously meant *every* ingot stayed in the pack even
        // after NeedBank had correctly identified it as surplus.
        if (needCfg_.profession && needCfg_.profession->gathers == "ore") {
            constexpr i32 kCarryMetalBatch = 20;
            i32 ingots = 0;
            const u32 ingot = FindBackpackItemByName(client, "i_ingot_iron", &ingots);
            if (ingot && ingots > kCarryMetalBatch) {
                const u16 moving = static_cast<u16>(ingots - kCarryMetalBatch);
                LogLine("banking %u iron ingots, keeping %d as the smithing batch",
                        moving, kCarryMetalBatch);
                IssueBankItemMove(client, obs, ingot, moving, box);
                return false;
            }
        }

        // SETTLE THE GOLD DEPOSIT ASKED FOR LAST TICK, from what actually
        // left the pack -- never from having merely issued the drag. The
        // deposit below used to call NoteProgress() the instant it ISSUED
        // the move, not once it LANDED, which hid a real failure mode:
        // Lyra's box kept answering "item landed in 0x4000C91D, not the
        // destination 0x4000C944" -- the exact "this box is not really
        // open" case the item-deposit loop already recovers from below --
        // fifteen times a minute, each one credited as progress. That both
        // defeated the "progress==0 -> stand down" guard further down this
        // function and meant NeedBank never went quiet, so BANK kept
        // outscoring FILL_SPELLBOOK every ~20 s for the rest of the session
        // (docs/LIFE_GATE_WAVE1.md theme 3, run_gates/g_Lyra.console.txt
        // 00:40:09-00:42:21).
        if (bankGoldDepositPending_) {
            bankGoldDepositPending_ = false;
            const DepositOutcome out = SettleDeposit(
                pendingGoldDepositBefore_, obs.goldOnHand,
                bankGoldDepositTries_, kMaxBankDepositTries);
            if (out.progressed) {
                planner_.NoteProgress();
            } else if (out.giveUp) {
                LogLine("bank: %d attempts to deposit gold all landed "
                        "elsewhere -- this box is not really open",
                        bankGoldDepositTries_);
                client.ForgetBankContainer();
                bankGoldDepositTries_ = 0;
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 3000;
                return false;
            } else {
                LogLine("bank: gold did not leave the pack (attempt %d of "
                        "%d) -- asking the box again",
                        bankGoldDepositTries_, kMaxBankDepositTries);
            }
        }

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
                // An ASK, not yet progress -- settled at the top of this
                // block on the tick after it lands (or does not).
                pendingGoldDepositBefore_ = obs.goldOnHand;
                bankGoldDepositPending_ = true;
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
        }

        const bool loadDemandsIt =
            obs.WeightFraction() >= needCfg_.bankWeightFrac;
        if (needCfg_.profession && (loadDemandsIt ||
                                    needCfg_.profession->produces.empty())) {
            for (const std::string& made : needCfg_.profession->produces) {
                // FIND AND COUNT THE SAME THING (S1). This used to take the
                // serial from FindBackpackItemByGraphic -- ONE stack, of
                // whatever hue happened to come first -- and the amount from
                // BackpackItemCount, which sums EVERY hue of that graphic.
                // With ore and the iron ingot shared by a dozen metals that
                // is a request to move N of a stack that is not the one being
                // counted: "banking 60 i_ingot_iron" pointing at valorite.
                i32 amount = 0;
                const u32 serial =
                    FindBackpackItemByName(client, made.c_str(), &amount);
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
                IssueBankItemMove(client, obs, serial,
                                  static_cast<u16>(amount), box);
                return false;
            }
        }
        // STOCK NOBODY WILL BUY YET, WHATEVER THE PACK WEIGHS.
        //
        // "until they have orders they keep other ingots in the bank"
        // (project owner, 2026-08-30). Every branch around this one is gated
        // on `loadDemandsIt` -- carried weight -- and seventeen iron ingots
        // are not heavy. So Corwyn carried them through three sessions while
        // EARN_GOLD logged, every single time:
        //
        //   BLOCKED_NEED EARN_GOLD: carrying its own output with nobody known
        //   to buy it (17 x i_ingot_iron spare, and no buyer known)
        //
        // Unsellable is not worthless. It is stock waiting for an order, and
        // stock waits in the box. NeedBank scores this same condition (see
        // Needs.cpp, "put unsold stock away") so the need and the action
        // agree -- without that pairing the goal completes having done
        // nothing and is picked again immediately. The two buyer checks below
        // are the whole test, and they are the same two the need uses.
        //
        // A WORKING BATCH STAYS. Ingots are both what a smith makes and what
        // it makes FROM; banking every one would leave it standing at a forge
        // with nothing to hammer.
        if (needCfg_.profession) {
            const i32 keepToWorkWith = needCfg_.craftBatch * 2;
            for (const std::string& made : needCfg_.profession->produces) {
                if (market::MaySellToNpc(*needCfg_.profession, made.c_str(),
                                         state_.ledger).allowed)
                    continue;   // it has an NPC route; selling beats storing
                if (state_.memory.BestSupplier(
                        (std::string("buyer:") + made).c_str()))
                    continue;   // a player buyer is known; that is a sale

                // Find and count the same NAME -- see the produces loop above.
                i32 amount = 0;
                const u32 serial =
                    FindBackpackItemByName(client, made.c_str(), &amount);
                if (!serial || amount <= keepToWorkWith) continue;

                const i32 put = amount - keepToWorkWith;
                LogLine("bank: storing %d %s until there is an order for it "
                        "(keeping %d to work with; no NPC buys it and the "
                        "player market was quiet)",
                        put, made.c_str(), keepToWorkWith);
                IssueBankItemMove(client, obs, serial, static_cast<u16>(put),
                                  box);
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
                // Find and count the same NAME -- see the produces loop above.
                i32 amount = 0;
                const u32 serial =
                    FindBackpackItemByName(client, input.c_str(), &amount);
                if (!serial || amount <= keep) continue;
                const i32 put = amount - keep;
                LogLine("banking %d spare %s (keeping %d to work with)", put,
                        input.c_str(), keep);
                IssueBankItemMove(client, obs, serial, static_cast<u16>(put),
                                  box);
                return false;
            }
        }

        const u32 logs = loadDemandsIt || !needCfg_.profession
                             ? client.FindBackpackItemByGraphic(kLog)
                             : 0;
        if (logs) {
            const u16 amount = static_cast<u16>(client.BackpackItemCount(kLog));
            LogLine("banking %u logs", amount);
            IssueBankItemMove(client, obs, logs, amount, box);
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
            // TWO KEEP LISTS, because two kinds of thing are being named.
            //
            // Tools and consumables are declared as GRAPHICS by the
            // profession, and a graphic is all they ever are. Everything a
            // life makes, consumes or makes FROM is declared as a DEFNAME --
            // and turning those into graphics threw the answer away (S1):
            // GraphicsForItem("i_ore_iron") is 019b7..019ba, which is every
            // metal's ore, so a smith with i_ore_iron on its list quietly
            // exempted a pack full of valorite and could never put it down.
            // Named things are therefore matched by hue-resolved NAME.
            std::vector<u16> keepGfx{kLog};
            std::vector<std::string> keepNames;
            auto keepAll = [&keepGfx](const std::vector<u16>& g) {
                keepGfx.insert(keepGfx.end(), g.begin(), g.end());
            };
            auto keepNamed = [&keepNames](const std::string& item) {
                for (const std::string& have : keepNames)
                    if (have == item) return;
                keepNames.push_back(item);
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
                u32 serial = 0; u16 gfx = 0, amount = 0, hue = 0;
                if (!client.ContainerItemAt(pack, i, &serial, &gfx, &amount, &hue))
                    continue;
                if (!serial) continue;
                bool named = false;
                for (u16 k : keepGfx) { if (k == gfx) { named = true; break; } }
                // The hue-resolved name, so a coloured ore or ingot is judged
                // as ITSELF against the keep list rather than as iron.
                const char* itemName = econ::ItemNameForGraphicAndHue(gfx, hue);
                if (!named && itemName) {
                    for (const std::string& k : keepNames)
                        if (k == itemName) { named = true; break; }
                }
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
                    IssueBankItemMove(client, obs, serial, moving, box);
                    return false;
                }

                LogLine("banking dead weight: %s (0x%04X hue 0x%04X) x%u -- "
                        "this life has no use for it and the pack is at %.0f%%",
                        itemName ? itemName : "unnamed", gfx, hue, moving,
                        obs.WeightFraction() * 100.0);
                IssueBankItemMove(client, obs, serial, moving, box);
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

    // GET TO A BANK BEFORE ASKING FOR ONE.
    //
    // BankErrand only ever scans mobiles already in view (NearestMobileWithTrade)
    // -- it never travels anywhere. DoBank used to hand straight to it from
    // wherever the character happened to be standing, which right after
    // creation is nowhere near a counter: Draver and Lyra's very first BANK
    // goal failed "no banker in sight" in two seconds flat
    // (run_gates/g_Draver.console.txt, g_Lyra.console.txt, 00:32:06-00:32:11
    // and 00:32:19-00:33:51). Both only ever banked once an UNRELATED market
    // trip happened to walk them past a real banker five minutes later
    // (g_Draver: "market: taking 10 i_ingot_iron to britain_bank_2" at
    // 00:32:52, banker found at 00:37:35) -- proof the census itself is fine
    // once actually at a bank; the goal simply never travelled there on its
    // own. Every other service errand in this file travels first and asks
    // second (DoHeal, EARN_GOLD's buyer trip, ...); this one now does too.
    if (!bankErrand_.Running() && !NearAnyBank(client, obs)) {
        if (!travelInFlight_) {
            const KnownPlace* proven =
                state_.memory.NearestPlace("bank", obs.x, obs.y);
            // A bank learned during an old cross-city errand must not outrank
            // the home/current-city bank that the atlas already knows.  The
            // previous `proven ? ... : seeded` rule sent Minoc miners back to
            // Britain simply because Britain was the first bank they had
            // opened. Choose the genuinely nearer known counter, regardless
            // of whether it was learned by memory or seeded as city knowledge.
            const KnownPlace* seeded = state_.memory.NearestPlace(
                "common_knowledge_bank", obs.x, obs.y);
            const i32 provenDist = proven
                                       ? TileDist(proven->x, proven->y, obs.x, obs.y)
                                       : 0x7FFFFFFF;
            const i32 seededDist = seeded
                                       ? TileDist(seeded->x, seeded->y, obs.x, obs.y)
                                       : 0x7FFFFFFF;
            if (proven && provenDist <= seededDist) {
                LogLine("bank: walking back to a bank we have used before, "
                        "%d,%d (%d tiles; seeded alternative %d)",
                        proven->x, proven->y, provenDist, seededDist);
                travelInFlight_ =
                    client.TravelToPoint(proven->x, proven->y, 3, "known_bank");
            } else if (seeded) {
                LogLine("bank: nothing proven yet -- trying what common "
                        "knowledge says is nearest, %s at %d,%d (%d tiles)",
                        seeded->name.c_str(), seeded->x, seeded->y, seededDist);
                travelInFlight_ = client.TravelToPoint(seeded->x, seeded->y, 5,
                                                       "seeded_bank");
            } else {
                LogLine("bank: nothing known or seeded -- asking the world "
                        "for a bank");
                travelInFlight_ = client.TravelToService(
                    wm::Service::Banker, HomeOrNearest(state_.homeCity));
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=BANK reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 10000;
            }
            return false;
        }
        travelInFlight_ = false;
        if (!NearAnyBank(client, obs)) {
            LogLine("bank: trip reported %s but no bank is within reach of "
                    "%d,%d",
                    client.TravelSucceeded() ? "success" : "failure",
                    client.PlayerX(), client.PlayerY());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 3000;
        }
        return false;
    }

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

    // --- THE ERRAND OWNS GETTING THE BOX OPEN ----------------------------
    //
    // Six Runner members existed to open one container: bankerAsked_,
    // bankerCounted_, bankOpenTries_, bankerSilent_, bankShouts_ and
    // bankTitlesAskedMs_. All six are the same counters-on-the-runner pattern
    // that let a gear trip spend the spellbook's trip allowance, and all six
    // are now inside BankErrand where they belong to this errand alone.
    //
    // The two bank-specific truths survive intact, because both were learned
    // live and neither is obvious:
    //   * success is the box SERIAL, not its contents -- an empty box sends
    //     no 0x3C, so waiting for contents re-opened the bank forever;
    //   * the keyword works without a named banker, so a character standing
    //     in a bank may say "bank" aloud and be served.
    //
    // GEOMETRY, NOT EYE CONTACT. We only ever reach this line because the
    // block above already confirmed NearAnyBank -- so tell the errand it is
    // at a known location and let it speak straight away rather than hunt
    // for a banker mobile (owner ruling, 2026-08-31: bankers hear through
    // walls; see bank_errand.h).
    if (!bankErrand_.Running()) bankErrand_.Begin();
    bankErrand_.SetAtKnownBank(true);
    const life::BankErrandResult br = bankErrand_.Tick(client, obs);
    LogErrandReason("bank", br.why.c_str(), obs.nowMs);
    if (br.wake == life::Wake::AfterDelay && br.delayMs > 0)
        nextActionMs_ = obs.nowMs + br.delayMs;

    if (br.status == life::ActivityStatus::Success) {
        // Remember where the counter is, now that one has actually served
        // us. A keyword ask never named a banker (br.banker == 0 -- we did
        // not need to see one), so the character's own position is the best
        // evidence of where the bank is.
        i32 bx = obs.x, by = obs.y; i8 bz = obs.z;
        if (br.banker) client.MobilePosition(br.banker, &bx, &by, &bz);
        state_.memory.NotePlace("bank", "bank", bx, by, bz, obs.nowMs);
        // The box is open; the next tick takes the deposit branch above.
        planner_.NoteProgress();
        return false;
    }

    if (!life::IsTerminal(br.status)) {
        // ASKING IS NOT PROGRESS. NoteProgress() here reset the attempt
        // counter on every retry, so Exhausted() never fired and the planner
        // believed a goal that had done nothing for twenty minutes was
        // working. An ask is an attempt; the box opening is the progress.
        //
        // ...and a WAIT is not an ask either. Counting the "an ask is in
        // flight" polls made the tick rate, not the banker, decide when the
        // goal ran out of tries -- the same defect the potion errand paid for
        // in run_r4/w_Bruin.console.txt:317-323.
        if (br.acted) planner_.NoteAttempt(obs.nowMs);
        return false;
    }

    // Nobody here will open a box. Walking to another bank is the honest next
    // move, but not on this goal and not this second.
    LogLine("goal_failed=BANK status=%s reason=\"%s\"",
            life::ActivityStatusName(br.status), br.why.c_str());
    state_.memory.NoteEvent("bank_no_answer", br.why.c_str(), "",
                            obs.x, obs.y, obs.nowMs);
    planner_.Cooldown(GoalKind::Bank, obs.nowMs + kBankCooldownMs);
    planner_.Finish(false, "no banker answered", obs.nowMs);
    nextActionMs_ = obs.nowMs + 5000;
    return false;
}

// --- the work --------------------------------------------------------------

bool Runner::DoGatherLogs(Client& client, const Observation& obs) {
    // THE SHARED DECISION FIRST (section 22). Chopping, mining and fishing
    // all answer the same question -- swing, arm, move, or take the load in
    // -- and the branches below used to answer it three different ways. What
    // stays here is what is genuinely a lumberjack's: which tree, and how to
    // swing at it.
    {
        life::GatherRequest req;
        req.resource = "logs";
        req.loadWorthTaking = needCfg_.logsWorthBanking;
        req.packFullFraction = 0.95;   // this goal's own long-standing bar
        req.toolMustBeWielded = true;  // skill44_lumberjacking reads SRC.WEAPON

        life::GatherSight sight;
        sight.held = obs.logs;
        sight.weightFraction = obs.WeightFraction();
        sight.toolInPack = obs.axeInPack;
        sight.toolWielded = obs.axeEquipped;
        // The census and the exhaustion flag disagree on purpose: TreeCount
        // still sees trunks here while NearestTree has none left to offer.
        sight.targetInReach = obs.atWorkSite && !areaExhausted_;
        sight.areaWorkedOut = areaExhausted_;

        const life::GatherPlan plan = life::DecideGather(req, sight);
        if (plan.step == life::GatherStep::NeedTool) {
            LogLine("goal_failed=GATHER_LOGS reason=\"%s\"", plan.reason);
            planner_.Finish(false, "no axe", obs.nowMs);
            return false;
        }
        if (plan.step == life::GatherStep::TakeItIn) {
            LogLine("gather: %s (%.0f%% of capacity, %d logs)", plan.reason,
                    obs.WeightFraction() * 100.0, obs.logs);
            return true;
        }
        // LeaveArea, ArmTool and Swing all fall through: the branches below
        // already do those three things, and doing them well is this
        // handler's actual job.
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
                // WALK OUT AND LOOK, RATHER THAN WAIT FOR AN ATLAS ENTRY
                // THAT DOES NOT EXIST. Project owner, 2026-08-31: "if he
                // left the guard zone at Britain he would see farmable
                // trees" -- and the atlas backs this up literally:
                // data/revolution_atlas.txt has zero PLACE rows with
                // resources=lumber (grep -i "\tlumber$"), so
                // TravelToResource(Lumber) can never succeed here. A real
                // player in this position walks out of town; this is that,
                // bounded (world/GuardZoneAdvance.h -- same shape as
                // DoMine's DeeperMiningTarget, opposite direction).
                i32 stepX = 0, stepY = 0;
                if (client.StepOutOfGuardZone(obs.x, obs.y, &stepX, &stepY)) {
                    LogLine("gather: no stand and no lead left, and this is "
                            "guarded ground -- walking out to where trees can "
                            "actually be worked");
                    travelInFlight_ =
                        client.TravelToPoint(stepX, stepY, 4, "past_guard_line");
                } else {
                    LogLine("gather: no stand and no lead left; asking the "
                            "world for lumber");
                    travelInFlight_ =
                        client.TravelToResource(wm::ResourceKind::Lumber);
                }
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
        bool allGuarded = false;
        const bool found = client.NearestTree(obs.x, obs.y, cfg_.searchRadius,
                                              &tree, &visitedTrees_,
                                              &allGuarded);
        if (!found && allGuarded) {
            // OWNER RULE: no gathering inside guarded zones. Every candidate
            // this scan saw stands inside the guard line (Tarath chopped a
            // tree at 1449,1635 inside guarded a_townBritain before this
            // check existed) -- this is a town square, not an empty forest,
            // so do not dead-list the lead. Fall through to the
            // proven-stand/travel logic above by marking the area exhausted;
            // the NEXT tick re-enters at "am I actually where the work is?"
            // and walks to a stand that has actually paid out.
            LogLine("gather: nothing to take outside the guard line here -- "
                    "going to the proven stand");
            areaExhausted_ = true;
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
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

    // Do not open a fight while an earlier errand is still walking us toward
    // its destination.  The food run proved why: the planner selected combat
    // during the provisioner's final approach, attacked a town animal, then
    // the still-live travel action pulled the character back toward the
    // counter.  Finish the movement before making a combat decision.
    if (client.TravelBusy()) return false;

    // Preparation comes before selecting a new target.  A character may defend
    // itself above, but it must not initiate a town fight or graveyard trip
    // until it has a weapon and basic armour.
    if (!obs.weaponEquipped) {
        return HandOff(GoalKind::TrainCombat, GoalKind::ReplaceEquipment,
                       kGearCooldownMs, "no gear yet -- shopping before the "
                       "graveyard", obs.nowMs);
    }
    if (!HasBasicArmor(client, obs)) {
        return HandOff(GoalKind::TrainCombat, GoalKind::UpgradeGear,
                       kGearCooldownMs, "no gear yet -- shopping before the "
                       "graveyard", obs.nowMs);
    }

    const wm::Region* combatRegion = client.CurrentRegion();
    const bool inGuardedRegion = combatRegion && combatRegion->flags.guarded;

    // Notoriety alone is not a reason to begin training inside town.  Gray
    // wildlife and other lawful mobiles can appear there, but a normal new
    // warrior goes to an unguarded hunting ground for deliberate fights.
    // Self-defence remains handled above regardless of region.
    if (obs.hostilesNear > 0 && !inGuardedRegion && !client.ActionBusy()) {
        std::vector<Client::HostileHit> seen;
        client.ScanHostiles(12, seen);
        if (!seen.empty()) {
            // Seeing several creatures is normal at a graveyard.  ChoosePrey
            // scores the weakest and least-grouped one; "one at a time" means
            // issuing one opening attack and never selecting another while an
            // attacker exists, not waiting for the whole screen to contain
            // exactly one mobile (which left Hector standing until a pack
            // attacked him first).
            std::vector<combat::Candidate> cands;
            cands.reserve(seen.size());
            for (const Client::HostileHit& h : seen) {
                if (IsUnreachable(h.serial, obs.nowMs)) continue;
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
            if (cands.empty()) {
                LogLine("hunt: every visible hostile is on the recent retreat "
                        "list -- not re-engaging");
                client.EnsurePeaceMode();
                nextActionMs_ = obs.nowMs + 4000;
                return false;
            }
            combat::Stance me;
            // REGION_FLAG_GUARDED, straight from the atlas -- there is no
            // Observation field for it and inventing one would just cache a
            // fact the world model already answers.
            me.inGuardedRegion = false;
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
                // TRAIN_COMBAT opens the fight, but SURVIVE owns it as soon
                // as the target retaliates.  Initialise the shared fight
                // window here; waiting for DoSurvive to see a *different*
                // serial left fightStartedMs_ at zero and made the very first
                // assessment look millions of seconds old.
                fightStartedMs_ = obs.nowMs;
                chaseBestDist_ = c.dist;
                chaseProgressMs_ = obs.nowMs;
                foeHpAtStart_ = c.hpCur >= 0 && c.hpMax > 0
                                    ? static_cast<double>(c.hpCur) / c.hpMax
                                    : -1.0;
                foeHpAskedMs_ = obs.nowMs;
                client.RequestMobileStatus(c.serial);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            LogLine("hunt: %zu hostile(s) in sight and none worth starting on",
                    seen.size());
        }
        return DoSurvive(client, obs);
    }
    if (obs.hostilesNear > 0 && !inGuardedRegion) {
        // DoSurvive may report that there is presently nothing to defend
        // against.  That is not successful combat training and must not mark
        // TRAIN_COMBAT complete merely because an opened target has not yet
        // retaliated or has briefly left the mobile cache.
        DoSurvive(client, obs);
        return false;
    }
    if (obs.hostilesNear > 0 && inGuardedRegion) {
        LogLine("hunt: ignoring %d lawful hostile(s) in guarded town; heading "
                "to a hunting ground", obs.hostilesNear);
    }

    // NOTHING HERE. Until now that was the end of it -- "return true" -- and
    // it is why M6 has never once been exercised live: the layer that decides
    // what may legally be attacked was never given anything to decide about.
    // A fighter with no fight in reach should go and find one.
    if (!needCfg_.profession || !WantsToHunt(*needCfg_.profession)) return true;

    // GEAR UP BEFORE THE FIRST HUNT. "hunting ground can be graveyards for
    // early hunting, brit sewers maybe, but they need gear too" (project
    // owner, 2026-08-31). Affordability is not the issue -- a fresh fighter
    // Not while hurt, and not while loaded: the goal scorer already docks
    // both, but arriving at a graveyard at half health is a death rather than
    // a lesson, and that is a decision this goal should make for itself.
    //
    // FINISH(FALSE) WITH A COOLDOWN, NOT "return true". Returning true here
    // reports the goal COMPLETE -- Tick() calls planner_.Finish(true, ...)
    // right after -- so nothing happened and the planner is told it
    // succeeded. w_Kaelen logged this exact line into a five-times-in-five-
    // seconds spin (run_r4/w_Kaelen.console.txt:620-710, 25/32 health every
    // time) because Finish(true) carries no cooldown and TRAIN_COMBAT was the
    // very next need re-picked. A goal that decided not to act did nothing,
    // and "did nothing" stands down (goal-that-did-nothing-must-stand-down),
    // it does not report success.
    if (obs.hp * 100 < obs.hpMax * 80) {
        LogLine("hunt: %d/%d health -- not going looking for a fight",
                obs.hp, obs.hpMax);
        planner_.Cooldown(GoalKind::TrainCombat, obs.nowMs + kHuntStandDownMs);
        planner_.Finish(false, "too hurt to go looking for a fight", obs.nowMs);
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    if (obs.WeightFraction() >= 0.7) {
        LogLine("hunt: carrying too much to fight (%.0f%%)",
                obs.WeightFraction() * 100.0);
        planner_.Cooldown(GoalKind::TrainCombat, obs.nowMs + kHuntStandDownMs);
        planner_.Finish(false, "carrying too much to fight", obs.nowMs);
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    if (client.TravelBusy()) return false;
    if (!travelInFlight_) {
        if (++huntTrips_ > kMaxHuntTrips) {
            LogLine("goal_failed=TRAIN_COMBAT reason=\"no hunting ground "
                    "reachable after %d trips\"", huntTrips_);
            planner_.Cooldown(GoalKind::TrainCombat,
                              obs.nowMs + kNoHuntingGroundCooldownMs);
            planner_.Finish(false, "no hunting ground reachable", obs.nowMs);
            huntTrips_ = 0;
            nextActionMs_ = obs.nowMs + 60000;
            return false;
        }
        // TravelToHuntingGround resolves the nearest graveyard-category place
        // from world_atlas::Atlas::NearestHuntingGround -- the early-tier
        // hunting ground the owner named -- and logs where it is actually
        // going, not just "the nearest graveyard".
        std::string huntPlace;
        travelInFlight_ = client.TravelToHuntingGround(&huntPlace);
        if (travelInFlight_) {
            LogLine("hunt: heading to %s to train (trip %d)",
                    huntPlace.c_str(), huntTrips_);
        } else {
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

// The reader for the fact DoTradeWithPlayer has been writing all along. See
// the declaration in Runner.h for why the floor is gated on it.
bool Runner::PlayersDeclined(const std::string& item, i64 nowMs) const {
    if (item.empty()) return false;
    for (const LifeEvent& e : state_.memory.Events()) {
        if (e.kind != "no_player_buyer") continue;
        if (e.detail != item) continue;
        if (nowMs - e.atMs <= kPlayerWindowMemoryMs) return true;
    }
    return false;
}

// The buy half of the same reader. See the declaration in Runner.h.
bool Runner::SellersDeclined(const std::string& item, i64 nowMs) const {
    if (item.empty()) return false;
    for (const LifeEvent& e : state_.memory.Events()) {
        if (e.kind != "no_player_seller") continue;
        if (e.detail != item) continue;
        if (nowMs - e.atMs <= kPlayerWindowMemoryMs) return true;
    }
    return false;
}

// The surplus half of the same ruling. See the declaration in Runner.h.
market::MaterialSaleGate Runner::MaterialSaleGateFor(
    const std::string& item, const Observation& obs) const {
    const prof::Profession* me = needCfg_.profession;
    if (!me) {
        market::MaterialSaleGate g;
        g.reason = "no profession, so no plan to derive a cap from";
        return g;
    }
    // WHAT THE BUILD PLAN STILL HAS TO CLIMB. This is the piece the market
    // layer cannot see: the plan is life-layer state and the current sheet is an
    // observation. A skill at or past its target contributes nothing, which is
    // what makes a trained smith's cap smaller than a green one's.
    std::vector<market::SkillGap> gaps;
    gaps.reserve(state_.plan.skills.size());
    for (const SkillTarget& t : state_.plan.skills) {
        const i32 now = obs.SkillTenths(t.skillId);
        gaps.push_back({t.skillId, std::max(0, t.tenths - now)});
    }
    // PACK PLUS BANK. Unsold stock is banked the moment it has no buyer (see
    // DoBank, "put unsold stock away"), so counting the pack alone reports a
    // smith with six hundred banked ingots as holding none -- and the cap it is
    // measured against is in the hundreds.
    const i32 held = market::QtyOf(obs.pack, item) + market::QtyOf(obs.bank, item);
    return market::MaterialNpcSaleGate(
        *me, item.c_str(), held, PlayersDeclined(item, obs.nowMs),
        needCfg_.craftBatch, obs.gold, gaps,
        market::PolicyForPurse(obs.goldOnHand));
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
        // BOTH HALVES, through the shared check (section 18).
        // AGAINST WHAT WAS ACTUALLY OFFERED. A surplus sweep sells whatever
        // the vendor listed, not the item the goal is named after.
        const std::string& soldItem =
            sellVerifyItem_.empty() ? sellItem_ : sellVerifyItem_;

        life::Expectation want;
        want.itemBefore = sellItemBefore_;
        want.itemLoss = 1;          // one leaving proves the sale happened
        want.goldBefore = sellGoldBefore_;
        want.goldGainMin = 1;

        life::Observed seen;
        seen.itemNow = market::QtyOf(obs.pack, soldItem);
        seen.goldNow = obs.gold;

        const life::ProgressCheck sale = life::Verify(want, seen);
        if (sale.verdict == life::Verdict::Confirmed) {
            const i32 paid = sale.goldDelta;
            const i32 sold = -sale.itemDelta;
            const i32 each = sold > 0 ? paid / sold : paid;
            LogLine("earn_gold: sold %d %s for %d gold (%d each) to a '%s'",
                    sellWanted_, soldItem.c_str(), paid, each,
                    sellTrade_.c_str());

            // What it was worth, as OBSERVED. This is the only kind of price
            // this project lets a character know.
            market::PriceObservation po;
            po.item = soldItem;
            po.pricePerUnit = each;
            po.source = market::PriceSource::NpcVendorBuys;
            po.who = sellTrade_;
            po.x = obs.x; po.y = obs.y;
            po.whenMs = obs.nowMs;
            state_.prices.Note(po);

            // Selling to an NPC CREATES gold. Recording it as a source is what
            // makes the anti-arbitrage invariant checkable afterwards.
            state_.ledger.Note(market::GoldFlow::CreatedVendor, paid,
                               soldItem.c_str(), obs.nowMs);

            KnownSupplier sup;
            sup.need = std::string("buyer:") + soldItem;
            sup.name = sellTrade_;
            sup.sourceType = "npc_vendor";
            sup.x = obs.x; sup.y = obs.y; sup.z = obs.z;
            sup.observedPricePerUnit = each;
            sup.lastVerifiedMs = obs.nowMs;
            sup.policyAllows = true;
            state_.memory.NoteSupplier(sup);

            state_.memory.NoteEvent("sold_to_vendor", soldItem.c_str(),
                                    sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
            planner_.NoteProgress();
            sellSent_ = false;
            sellAsked_ = false;
            sellTrips_ = 0;
            sellLotCap_ = 0;   // this buyer could pay; stop rationing
            sellSweepGold_ += paid;
            Checkpoint(client, obs.nowMs, "sold to a vendor");

            // ONE VISIT, EVERYTHING SPARE.
            //
            // The old code returned success here, and that is the whole of
            // v4_Corwyn's 366 lost gold: Curtis had ALREADY named the six
            // heater shields he would buy, in the same 0x9E that listed the
            // two daggers. Corwyn took 72 gold for the daggers, reported the
            // errand done, and walked to the forge to make more daggers.
            //
            // A player empties their pack at the counter they are already
            // standing at. So re-ask the same vendor -- the sold items are
            // gone from the refreshed list, and whatever else is surplus is
            // still on it -- and only finish when nothing is left that this
            // buyer will take.
            if (++sellSweeps_ < kMaxSellSweeps) {
                LogLine("earn_gold: %d gold so far at this counter -- asking "
                        "the '%s' what else it will take before leaving",
                        sellSweepGold_, sellTrade_.c_str());
                sellVerifyItem_.clear();
                nextActionMs_ = obs.nowMs + 1200;
                return false;
            }
            LogLine("earn_gold: %d gold at this counter over %d sales -- "
                    "enough for one visit", sellSweepGold_, sellSweeps_);
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
    //
    // MID-SWEEP, THIS QUESTION IS ALREADY ANSWERED -- BY THE VENDOR.
    //
    // market::Surplus only knows what this life PRODUCES, and that is a
    // narrower question than "what will this counter take off my hands". The
    // first sweep proved it: Corwyn sold his daggers, said he would ask what
    // else Curtis wanted, and this chooser answered "nothing spare to sell"
    // and stood the goal down -- with six saleable shields in his pack -- one
    // second later. The 0x9E list is the authority once we are standing at
    // the counter, so keep the errand aimed where it is and let the sell
    // stage read it.
    const bool sweeping = sellSweeps_ > 0 && sellSweeps_ < kMaxSellSweeps &&
                          sellSweepGold_ > 0 && sellVendorSerial_ != 0;

    // The threshold bends when the purse is empty: see PolicyForPurse.
    const market::TradePolicy tp = market::PolicyForPurse(obs.goldOnHand);
    const std::vector<market::Offer> offers =
        market::Surplus(*me, obs.pack, tp);
    if (offers.empty() && !sweeping) {
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
            if (!market::MaySellToNpc(*me, o.item.c_str(), state_.ledger,
                                      PlayersDeclined(o.item, obs.nowMs))
                     .allowed) {
                continue;
            }
            // AND IS THERE ACTUALLY MORE OF IT THAN THE PLAN WANTS? Withdrawing
            // banked material to walk it to a counter is the exact trip the
            // 2026-09-02 ruling calls a last resort; the cap decides whether
            // this is one. Below it, the stock stays in the box.
            const market::MaterialSaleGate gate =
                MaterialSaleGateFor(o.item, obs);
            if (!gate.allowed) {
                LogLine("earn_gold: leaving %d %s in the bank -- %s "
                        "(held=%d cap=%d: plan %d + training %d + market %d)",
                        market::QtyOf(obs.bank, o.item), o.item.c_str(),
                        gate.reason, gate.held, gate.cap, gate.detail.ownPlan,
                        gate.detail.training, gate.detail.market);
                continue;
            }
            fetch = &o;
            break;
        }
        if (!fetch) {
            // AND THAT IS A STAND-DOWN, NOT A COMPLETION. This returned true
            // -- success with progress 0 -- so the planner freed the goal and
            // handed it straight back, which is the same shape as the 13,111
            // completions documented above. The stock has not moved and the
            // player market has not been asked, so the useful next act is a
            // WTS cycle, and this goal has to get out of the way for one.
            LogLine("earn_gold: the bank holds a surplus but no NPC route for "
                    "it -- that is the player market's job, not this goal's; "
                    "standing down so TRADE_WITH_PLAYER gets a turn");
            planner_.Cooldown(GoalKind::EarnGold,
                              obs.nowMs + kNothingToSellCooldownMs);
            planner_.Finish(false, "no NPC route for the banked surplus",
                            obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }

        if (client.BankContainer() == 0) {
            // ARRIVING IS NOT ENOUGH -- the box has to be OPENED, by asking a
            // banker for it. Travelling and then re-testing "am I at the bank"
            // loops forever the moment the trip completes instantly because
            // the character is already standing there, which is exactly what
            // it did: eight identical "going to fetch it" lines in twelve
            // seconds, never once opening the box. Same shape as the no-op
            // travel loop that pinned GATHER_LOGS.
            // THE SAME ERRAND AS DoBank'S, not a second hand-written one.
            //
            // This was the last unported bank-open, and it spun live within
            // three minutes of being left alone: "earn_gold: the stock is in
            // the bank (1 i_dagger) -- opening the box", every 2.5 seconds
            // against an 8-second deadline, so each ask cancelled the one
            // before it (v3_Corwyn, 15:08). The commit that left it here
            // predicted exactly that -- "no rotation, no deadline discipline
            // and no check that a box ever opened" -- which is an argument
            // for porting a known defect rather than annotating it.
            if (!bankErrand_.Running()) bankErrand_.Begin();
            const life::BankErrandResult br = bankErrand_.Tick(client, obs);
            if (!br.why.empty())
                LogLine("earn_gold: fetching %s -- %s", fetch->item.c_str(),
                        br.why.c_str());
            if (br.wake == life::Wake::AfterDelay && br.delayMs > 0)
                nextActionMs_ = obs.nowMs + br.delayMs;

            if (br.status == life::ActivityStatus::Success) {
                i32 bx = 0, by = 0; i8 bz = 0;
                if (br.banker && client.MobilePosition(br.banker, &bx, &by, &bz))
                    state_.memory.NotePlace("bank", "bank", bx, by, bz,
                                            obs.nowMs);
                return false;   // the box is open; the fetch continues below
            }
            if (life::IsTerminal(br.status)) {
                LogLine("goal_failed=EARN_GOLD status=%s reason=\"%s\"",
                        life::ActivityStatusName(br.status), br.why.c_str());
                planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kShortRestMs);
                planner_.Finish(false, "no banker opened a box", obs.nowMs);
                return false;
            }
            planner_.NoteAttempt(obs.nowMs);
            return false;
        }

        // BY NAME, NOT BY GRAPHIC (S1). `take` is read from obs.bank, which
        // has been hue-resolved since S1 -- but the SERIAL was still found by
        // graphic, and the ore and iron-ingot graphics are shared by every
        // metal. A ledger line for i_ingot_iron could therefore pick up the
        // valorite stack sitting beside it in the box and withdraw THAT,
        // then hand it to a vendor at the iron price.
        i32 inBox = 0;
        const u32 serial = FindContainerItemByName(
            client, client.BankContainer(), fetch->item.c_str(), &inBox);
        if (!serial) {
            LogLine("earn_gold: the bank ledger says %s but the open box does "
                    "not show it -- the ledger is stale", fetch->item.c_str());
            return true;
        }
        const i32 take = std::min(market::QtyOf(obs.bank, fetch->item), inBox);
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
        const market::SellRuling r = market::MaySellToNpc(
            *me, o.item.c_str(), state_.ledger,
            PlayersDeclined(o.item, obs.nowMs));
        if (!r.allowed) {
            LogLine("earn_gold: will NOT sell %d %s -- %s", o.qty,
                    o.item.c_str(), r.reason);
            state_.memory.NoteEvent("sale_refused_policy", o.item.c_str(),
                                    r.reason, obs.x, obs.y, obs.nowMs);
            continue;
        }
        // MATERIALS EXIST TO BE CRAFTED (owner ruling, 2026-09-02). MaySellToNpc
        // answers whether the SHARD will take it; this answers whether this
        // CHARACTER can spare it. Both, or the counter does not get the trip.
        const market::MaterialSaleGate gate = MaterialSaleGateFor(o.item, obs);
        if (!gate.allowed) {
            LogLine("earn_gold: will NOT sell %d %s to an NPC -- %s "
                    "(held=%d cap=%d: plan %d + training %d + market %d)",
                    o.qty, o.item.c_str(), gate.reason, gate.held, gate.cap,
                    gate.detail.ownPlan, gate.detail.training,
                    gate.detail.market);
            state_.memory.NoteEvent("sale_below_surplus_cap", o.item.c_str(),
                                    gate.reason, obs.x, obs.y, obs.nowMs);
            continue;
        }
        chosen = &o;
        break;
    }
    if (!chosen && !sweeping) {
        // Same correction as the banked-surplus branch: returning true here
        // completed the goal with progress 0 and it was re-picked at once.
        // Nothing about the answer changes on the next tick -- the pack, the
        // policy and the registry are all identical -- so stand down and let
        // the player market or the workbench have the turn.
        LogLine("earn_gold: everything spare is barred from an NPC sale; "
                "banking it and standing down");
        planner_.Cooldown(GoalKind::EarnGold,
                          obs.nowMs + kNothingToSellCooldownMs);
        planner_.Finish(false, "nothing spare may be sold to an NPC",
                        obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // Mid-sweep there is no new item to choose: the errand stays aimed at the
    // counter it is standing at, and the vendor's own list decides what goes.
    if (chosen) {
        if (sellItem_ != chosen->item) {
            sellItem_ = chosen->item;
            sellBuyerIndex_ = 0;
            sellTrade_.clear();
            sellTrips_ = 0;
            sellLotCap_ = 0;
            // A new item to sell is a new visit; what the last counter paid
            // says nothing about this one.
            sellSweeps_ = 0;
            sellSweepGold_ = 0;
            sellVerifyItem_.clear();
        }
        sellWanted_ = chosen->qty;
    }

    // --- who buys it? ------------------------------------------------------
    const std::vector<const market::NpcBuyer*> buyers = market::NpcBuyersFor(
        sellItem_.c_str(), PlayersDeclined(sellItem_, obs.nowMs));
    if (buyers.empty()) {
        // A real answer, not a failure. The character stays resource-rich and
        // wealth-poor, which is a legitimate state on this shard -- and it is
        // the case the owner's 2026-09-02 ruling names explicitly: "if no NPC
        // class buys the item, bank it and cool the goal down". Most materials
        // land here, because the runtime's own tm_vend.scp has the log, board,
        // ore, iron-ingot and hide BUY rows commented out (see kNpcBuyers).
        //
        // COOLING IT DOWN IS THE HALF THAT WAS MISSING. `return true` reported
        // success with no gold earned, so the planner handed EARN_GOLD back
        // immediately and the same lookup failed again forever.
        LogLine("earn_gold: no NPC trade on this shard buys %s; banking it "
                "and standing down for %llds", sellItem_.c_str(),
                static_cast<long long>(kNoBuyerCooldownMs / 1000));
        state_.memory.NoteEvent("no_buyer", sellItem_.c_str(), "",
                                obs.x, obs.y, obs.nowMs);
        planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kNoBuyerCooldownMs);
        planner_.Finish(false, "no NPC trade buys it", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
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
            // A REMEMBERED BUYER IS ONLY WORTH RETURNING TO IF IT IS NEARBY.
            //
            // This branch had no distance test at all, and it is how Corwyn
            // kept dying between the cities. Standing in Britain after a
            // resurrection, his only remembered buyer:i_dagger was the Minoc
            // blacksmith, so EARN_GOLD sent him:
            //
            //   [travel] buyer -> (2473,562) r=2 from (1450,1617)
            //
            // A thousand tiles on foot, unmounted, through open country, to
            // sell daggers at eighteen gold. Britain has blacksmiths -- he had
            // just walked past them -- but familiarity outranked distance.
            //
            // Same shape as the wind-down bank: "nearest known" is not
            // "nearest". Below the threshold a familiar counter is worth the
            // walk; beyond it, ask the atlas for whatever is close, which is
            // what HomeOrNearest already does by returning nullptr.
            const i32 knownDist =
                known ? TileDist(known->x, known->y, obs.x, obs.y) : -1;
            if (known && known->name == sellTrade_ &&
                knownDist <= kReturnToKnownBuyerWithin) {
                LogLine("earn_gold: back to a buyer we have used before, "
                        "'%s' at %d,%d (%d tiles)", known->name.c_str(),
                        known->x, known->y, knownDist);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 2, "buyer");
            } else {
                if (known && known->name == sellTrade_)
                    LogLine("earn_gold: the '%s' we know is %d tiles away -- "
                            "looking for a nearer one instead",
                            known->name.c_str(), knownDist);
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
        // A different counter. Its purse and its buy list are its own.
        sellSweeps_ = 0;
        sellSweepGold_ = 0;
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

    // --- sell, matching by NAME -------------------------------------------
    //
    // The 0x9E list carries the serials of OUR OWN items, so this is a join
    // against the pack rather than against the vendor's stock.
    //
    // AND THE JOIN IS BY NAME, NOT GRAPHIC (S1). 0x9E carries a graphic and
    // no hue, and ore is ONE graphic for sixteen metals while the iron ingot
    // is one graphic for thirteen. Asking to sell "i_ingot_iron" therefore
    // matched the valorite stack in the same pack and handed it over at the
    // iron price -- an irreversible loss of the rarest thing a miner owns,
    // silently, at the moment the bot thought it was doing its job. So each
    // offer line is resolved back through the PACK, where the hue lives.
    //
    // The fail-safe when a serial cannot be found in the pack (a sub-bag, a
    // stale cache) splits on whether the graphic is ambiguous at all:
    // GraphicNeedsHue() is true only for the two shared families, and for
    // those an unresolvable line is REFUSED rather than guessed. Every other
    // graphic names itself, so it is matched as before.
    const std::vector<u16> mine = econ::GraphicsForItem(sellItem_.c_str());
    auto isMine = [&](const Client::VendorItem& item) {
        bool graphicMatches = false;
        for (u16 g : mine) { if (item.graphic == g) { graphicMatches = true; break; } }
        if (!graphicMatches) return false;
        const char* name = PackItemNameBySerial(client, item.serial);
        if (name) return sellItem_ == name;
        return !econ::GraphicNeedsHue(item.graphic);
    };
    for (const Client::VendorItem& v : client.VendorSellOffer()) {
        if (!isMine(v)) continue;

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
            if (!isMine(w) || w.amount <= 0) continue;
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
        // AND WHAT THE PACK HELD. A sale is gold arriving AND goods leaving;
        // gold alone also rises from loot, a player trade and a bank
        // withdrawal, and crediting this sale for one of those teaches the
        // price book a number nobody paid. See interaction/progress.h.
        sellVerifyItem_ = sellItem_;
        sellItemBefore_ = market::QtyOf(obs.pack, sellVerifyItem_);
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

        // HOW MANY OF IT STAYS, not whether any of it is wanted. The role
        // says what it is for; disposal.h says how many that means keeping.
        const ItemRole role = RoleOfGraphic(v.graphic);

        // Every entry of this graphic the vendor listed, and every one in the
        // pack -- a dagger does not stack, so one entry is one dagger and the
        // count has to be gathered rather than read off a single row.
        i32 listed = 0;
        for (const Client::VendorItem& w : client.VendorSellOffer())
            if (w.graphic == v.graphic && w.amount > 0)
                listed += static_cast<i32>(w.amount);

        life::DisposalSight see;
        see.role = role;
        // The 0x9E list is built FROM the backpack, so what it lists is what
        // is carried. An equipped shield is not in it, which is exactly why
        // selling every spare cannot strip a character of the one it wears.
        see.carried = listed;
        see.vendorTakes = listed;
        see.pricePerUnit = static_cast<i32>(v.price);
        see.lotCap = sellLotCap_;

        const life::DisposalPlan plan = life::DecideDisposal(see, disposal_);
        if (plan.step != life::DisposalStep::Sell) continue;

        const i32 qty = plan.quantity;

        LogLine("earn_gold: selling %d of 0x%04X at %u each to a '%s' "
                "(%s -- %s)",
                qty, v.graphic, v.price, sellTrade_.c_str(),
                life::ItemRoleName(role), plan.reason);
        sellWanted_ = qty;
        sellGoldBefore_ = obs.gold;
        // AND WHAT THE PACK HELD -- OF THE THING ACTUALLY BEING SOLD. Counting
        // the goal's item while offering shields would credit a sale of
        // daggers that never happened. econ names only 63 graphics; when this
        // one is not among them the purse alone decides, which Verify()
        // already expresses as itemBefore = -1.
        const char* def = econ::ItemNameForGraphic(v.graphic);
        sellVerifyItem_ = def ? def : "";
        sellItemBefore_ = sellVerifyItem_.empty()
                              ? -1
                              : market::QtyOf(obs.pack, sellVerifyItem_);
        sellAskedMs_ = obs.nowMs;

        // THE WHOLE LOT, IN ONE TRANSACTION -- the same rule the primary path
        // learnt. Armour and weapons do not stack, so `qty` spans that many
        // separate serials and sending one row of amount N sells exactly one.
        std::vector<std::pair<u32, u16>> lot;
        i32 taken = 0;
        for (const Client::VendorItem& w : client.VendorSellOffer()) {
            if (taken >= qty) break;
            if (w.graphic != v.graphic || w.amount <= 0) continue;
            const i32 take = std::min<i32>(qty - taken, static_cast<i32>(w.amount));
            lot.emplace_back(w.serial, static_cast<u16>(take));
            taken += take;
        }
        if (lot.empty()) continue;
        client.ActionVendorSellMany(vendor, lot);
        sellSent_ = true;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // A SWEEP THAT RAN OUT OF THINGS TO SELL HAS SUCCEEDED, NOT FAILED. If
    // gold changed hands at this counter, the pack is as empty as this buyer
    // can make it; marching on to the next trade would be looking for a
    // buyer for nothing.
    if (sellSweepGold_ > 0) {
        LogLine("earn_gold: %d gold from this '%s' over %d sale(s), and it "
                "will take nothing else we carry -- done here",
                sellSweepGold_, sellTrade_.c_str(), sellSweeps_);
        return true;
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
    // NOTHING TO BUY IS NOT AN ERRAND DONE. Returning true here reported
    // `goal_completed=TRAIN_AT_NPC progress=0` and freed the planner to hand
    // the same goal straight back; standing the goal down is what stops that.
    if (skillId < 0) {
        planner_.Cooldown(GoalKind::TrainAtNpc, obs.nowMs + kNoTrainerCooldownMs);
        planner_.Finish(false, "no skill is waiting on a trainer", obs.nowMs);
        return false;
    }

    const i32 have = client.PlayerSkillBase(static_cast<u16>(skillId));

    // --- did the gold we handed over actually buy anything? ---------------
    //
    // The proof is the SERVER'S skill number, not our own bookkeeping. Asking
    // for a fresh skill list is what a player's client does anyway.
    if (trainPaid_) {
        // DID THE GOLD ACTUALLY BUY ANYTHING? Section 18's train rule --
        // "skill changed, or the trainer definitively refused" -- and the
        // judgement now lives in activities/TrainConfirm.cpp, routed through
        // interaction/progress.h. `training_unverified` is the name this
        // project gave to skipping it: GOLD_DESTROYED_TRAINER a dozen times
        // in one fleet run with no confirmed gain to show for any of it.
        TrainConfirmInput tin;
        tin.skillBefore = trainSkillBefore_;
        tin.skillNow    = have;
        tin.goldBefore  = trainGoldBefore_;
        tin.goldNow     = obs.gold;
        tin.quoted      = trainQuoted_;
        tin.msSincePaid = obs.nowMs - trainPaidMs_;
        tin.skillsAsked = trainSkillsAsked_;
        const TrainConfirmResult tconf = ConfirmTraining(tin);
        if (tconf.verdict == TrainVerdict::Learned) {
            // THE ONE PLACE THIS GOAL MAY CLAIM PROGRESS, and it was missing:
            // even a lesson that worked reported `goal_completed=TRAIN_AT_NPC
            // progress=0`, which is the exact signal the anti-spin backstop
            // reads as "succeeded at nothing".
            planner_.NoteProgress();
            LogLine("train: %s %.1f->%.1f bought from a trainer",
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
        if (tconf.verdict == TrainVerdict::AskForSkills) {
            client.ActionRequestSkills();
            trainSkillsAsked_ = true;
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        if (tconf.verdict == TrainVerdict::FeeTakenNoLesson ||
            tconf.verdict == TrainVerdict::NoAnswer) {
            const bool feeTaken =
                tconf.verdict == TrainVerdict::FeeTakenNoLesson;
            LogLine("train: refused %s -- paid %d for %s "
                    "(purse %+d, skill %+.1f)", tconf.reason, trainQuoted_,
                    rules::SkillName(skillId), tconf.check.goldDelta,
                    tconf.check.skillDelta / 10.0);
            state_.memory.NoteEvent(
                feeTaken ? "training_took_fee_taught_nothing"
                         : "training_no_answer",
                rules::SkillName(skillId), trainerTrade_.c_str(),
                obs.x, obs.y, obs.nowMs);
            if (feeTaken) {
                // A TRAINER THAT TAKES THE FEE AND TEACHES NOTHING IS A FACT
                // ABOUT THAT TRAINER. Write HIM off, not the skill -- the
                // mistake that cost Ysolde Meditation entirely -- so the next
                // lesson is bought from somebody else.
                bool known = false;
                for (u32 k : trainerSilent_)
                    if (k == trainerSerial_) { known = true; break; }
                if (!known && trainerSerial_) trainerSilent_.push_back(trainerSerial_);
            }
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
    // --- decide: done, buy from an NPC, or grind it (S2.4, DecideTrain) ----
    //
    // Whether this skill is worth an NPC's time at all: the catalogue's own
    // viaTrainer flag for this target, AND no trade-wide refusal has already
    // written it off (obs.trainerRefusedSkills; see Runner.cpp's need-assess
    // pass and Memory::TrainerSaysMaxed/TrainerRefused).
    bool worthBuying = true;
    for (usize i = 0; i < state_.plan.skills.size(); ++i) {
        if (state_.plan.skills[i].skillId != skillId) continue;
        worthBuying = (i < state_.plan.viaTrainer.size()) && state_.plan.viaTrainer[i];
        break;
    }
    for (int r : obs.trainerRefusedSkills) {
        if (r == skillId) { worthBuying = false; break; }
    }
    // What an NPC of this trade can teach: a durable TrainerVerdict for this
    // (skill, trade) outranks any guess -- it is what a trainer here already
    // SAID about its own ceiling. Absent one, train.h:19-23's own numbers:
    // 300 for a guildmaster, 225 otherwise, and a guildmaster only counts once
    // NearestGuildmasterForTrade actually finds one nearby.
    auto rememberedCeilingTenths = [&]() -> i32 {
        for (const TrainerVerdict& v : state_.memory.Trainers()) {
            if (v.skillId != skillId || v.trade != trainerTrade_) continue;
            if (v.taught) return v.atTenths;
            if (v.why == "the trainer has nothing left to give") return v.atTenths;
        }
        const bool guildmasterNearby =
            client.NearestGuildmasterForTrade(trainerTrade_.c_str(), trainerSilent_) != 0;
        return guildmasterNearby ? 300 : 225;
    };

    TrainRequest req1;
    req1.skillId = skillId;
    req1.targetTenths = obs.wantTrainTarget;
    req1.npcCeilingTenths = rememberedCeilingTenths();
    req1.feeQuoted = 0;   // no quote yet -- DecideTrain reads 0 as "go ask"
    req1.gold = obs.gold;
    req1.worthBuying = worthBuying;
    const TrainPlan plan1 = DecideTrain(req1, have);
    if (plan1.step != lastTrainPlan_) {
        LogPlan(TrainStepName(plan1.step), plan1.reason);
        lastTrainPlan_ = plan1.step;
    }
    if (plan1.step == TrainStep::Done) {
        // Same rule as before S2.4: the skill is where it should be, so
        // nothing was achieved and nothing needs to be. Rest the goal rather
        // than completing it -- `return true` here reported
        // `goal_completed=TRAIN_AT_NPC progress=0` and the planner handed the
        // same goal straight back (S3's fix; DecideTrain does not change it).
        planner_.Cooldown(GoalKind::TrainAtNpc, obs.nowMs + kNoTrainerCooldownMs);
        planner_.Finish(false, plan1.reason, obs.nowMs);
        return false;
    }
    if (plan1.step == TrainStep::Practise) {
        return HandOff(GoalKind::TrainAtNpc, GoalKind::PracticeSkill,
                       kNoTrainerCooldownMs, plan1.reason, obs.nowMs);
    }
    // TrainStep::Buy (CannotAfford is unreachable here -- feeQuoted is 0):
    // fall through to the unchanged travel/approach/ask machinery below.

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
            trainTrips_ = 0;
            return HandOff(GoalKind::TrainAtNpc, GoalKind::IdleBriefly,
                           kNoTrainerCooldownMs, "no trainer reachable",
                           obs.nowMs);
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
        const char* const skillKey = SkillKey(skillId);
        if (!skillKey[0]) {
            // Never send the generic "train" command. It makes a trainer
            // list appear and looks like a real conversation in the log, but
            // it cannot quote a fee or teach a selected skill.
            LogLine("goal_failed=TRAIN_AT_NPC reason=\"no Sphere training key for %s\"",
                    rules::SkillName(skillId));
            planner_.Cooldown(GoalKind::TrainAtNpc,
                              obs.nowMs + kNoTrainerCooldownMs);
            planner_.Finish(false, "missing Sphere training key", obs.nowMs);
            trainAsked_ = false;
            return false;
        }
        LogLine("training: asking the trainer about %s (key %s, total gold %d, purse %d)",
                rules::SkillName(skillId), skillKey, obs.gold, obs.goldOnHand);
        client.ActionNpcTrain(trainer, skillKey);
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // Refusals first -- each is a real answer, not a timeout. The table lives
    // in activities/TrainConfirm.cpp so ctest can hold it to section 18's
    // "the trainer definitively refused".
    usize nRefusals = 0;
    const TrainerRefusal* refusals = TrainerRefusals(&nRefusals);
    for (usize ri = 0; ri < nRefusals; ++ri) {
        const TrainerRefusal& r = refusals[ri];
        if (!client.JournalSaidSince(r.text, trainAskedMs_)) continue;
        LogLine("train: refused %s -- '%s' will not teach %s at %.1f",
                r.why, trainerTrade_.c_str(), rules::SkillName(skillId),
                have / 10.0);
        // A DURABLE verdict, not a log line -- see uo/activities/
        // train_confirm.h for the session this cost when it was neither.
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

    // PAY WHOEVER ACTUALLY QUOTED -- the SPEAKER of the quote when one can be
    // identified, not the NPC that was addressed. Two tinkers stand together
    // in Minoc and the fee went to the one who had offered nothing; the log
    // that proves it is in uo/activities/train_confirm.h.
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
                // Give up on this NPC for now. Deliberately NOT a trainer
                // verdict: a verdict is what an NPC SAID (train_confirm.h).
                LogLine("goal_failed=TRAIN_AT_NPC reason=\"'%s' never answered "
                        "about %s\"", trainerTrade_.c_str(),
                        rules::SkillName(skillId));
                state_.memory.NoteEvent("trainer_silent",
                                        rules::SkillName(skillId),
                                        trainerTrade_.c_str(), obs.x, obs.y,
                                        obs.nowMs);
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
                return HandOff(GoalKind::TrainAtNpc, GoalKind::IdleBriefly,
                               kNoTrainerCooldownMs, "the trainer never answered",
                               obs.nowMs);
            }
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // Call 2 (S2.4): the quote has landed, so DecideTrain now sees the real
    // fee instead of the "go ask" placeholder of 0. have/target/ceiling and
    // worthBuying have not changed since call 1 this tick -- only feeQuoted
    // has -- so only Buy and CannotAfford are actually reachable here.
    TrainRequest req2;
    req2.skillId = skillId;
    req2.targetTenths = obs.wantTrainTarget;
    req2.npcCeilingTenths = rememberedCeilingTenths();
    req2.feeQuoted = quoted;
    req2.gold = obs.gold;
    req2.worthBuying = worthBuying;
    const TrainPlan plan2 = DecideTrain(req2, have);
    if (plan2.step != lastTrainPlan_) {
        LogPlan(TrainStepName(plan2.step), plan2.reason);
        lastTrainPlan_ = plan2.step;
    }
    if (plan2.step == TrainStep::CannotAfford) {
        // NOT unbuyable -- too poor TODAY. No TrainerVerdict is written and
        // the skill is not marked refused: that is the whole point of this
        // step existing separately from Practise.
        state_.memory.NoteEvent("trainer_quote", rules::SkillName(skillId),
                                trainerTrade_.c_str(), obs.x, obs.y, obs.nowMs);
        trainAsked_ = false;
        return HandOff(GoalKind::TrainAtNpc, GoalKind::EarnGold, 60000,
                       plan2.reason, obs.nowMs);
    }
    if (plan2.step == TrainStep::Done || plan2.step == TrainStep::Practise) {
        // The quote itself changed nothing about have/target/ceiling, so this
        // is defensive rather than a path call 1 leaves live -- but the quote
        // is what finally proved the ceiling, so stand down rather than pay.
        return true;
    }
    // TrainStep::Buy: pay exactly what was quoted, below, unchanged.

    // --- pay exactly what was quoted ---------------------------------------
    //
    // Ask for the pack's contents FIRST: a give addressed to a serial Sphere
    // retired when it split the gold stack is a SILENT no-op
    // (uo/activities/train_confirm.h).
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
    LogLine("training: paying the quoted %d gold for %s (total gold %d, purse %d)",
            quoted, rules::SkillName(skillId), obs.gold, obs.goldOnHand);
    trainSkillBefore_ = have;
    // WHAT THE PURSE HELD BEFORE THE LESSON, so "the fee was taken and
    // nothing was taught" can be told apart from "the trainer refused and
    // kept nothing". Both used to log the same `training_unverified` line,
    // and they are completely different problems: the first is a trainer to
    // write off, the second a report that simply has not arrived yet.
    trainGoldBefore_ = obs.gold;
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
//
// S5: AND BOTH HALVES GO TO THE SAME PLACE.
//
// The bank this walked to was the NEAREST one (TravelToService(Banker,
// nullptr)), which is correct for every other errand and fatal for this one.
// Home is set once from homeCities.front(), so miner_smith always lives in
// Minoc and lumberjack_swordsman always in Britain -- 1,500 tiles and two
// different banks apart -- and the producer and the consumer of the one live
// trade edge in the catalogue could never be inside kTradeEarshot of each
// other. A market has to be ONE place. It is market::kMarketBankPlaceId,
// which cites the atlas line and the forum evidence for why that one.
// ---------------------------------------------------------------------------

bool Runner::MarketPlaceUsable(Client& client) {
    if (marketPlaceOk_ >= 0) return marketPlaceOk_ == 1;
    // Not resolved yet -- but the atlas is only there once the world knowledge
    // has loaded, so do not cache a "no" that is really a "not yet".
    if (!client.WorldKnowledgeReady()) return false;

    const wm::Place* p = client.KnownPlace(market::kMarketBankPlaceId);
    const char* bad = nullptr;
    if (!p)                                  bad = "the atlas has no such place";
    else if (!p->Offers(wm::Service::Banker)) bad = "it offers no banker";
    else if (!client.PlaceGuarded(*p))        bad = "it is not in a guarded region";

    if (bad) {
        // DEGRADE, NEVER SUBSTITUTE. atlasgen slugs place ids, so a
        // regenerated atlas could renumber britain_bank_2 -- and the answer to
        // that is the nearest bank the world actually knows about, not a pair
        // of literal coordinates baked into the bot.
        LogLine("market: no usable market place '%s' (%s) -- falling back to "
                "the nearest bank", market::kMarketBankPlaceId, bad);
        marketPlaceOk_ = 0;
        return false;
    }
    LogLine("market: the market is %s (%s) at %d,%d, radius %d, guarded",
            market::kMarketBankPlaceId, p->name.c_str(), p->position.x,
            p->position.y, p->radius);
    marketPlaceOk_ = 1;
    return true;
}

bool Runner::AtMarketBank(const Client& client) const {
    const wm::Place* p = client.KnownPlace(market::kMarketBankPlaceId);
    if (!p) return false;
    // The place's own radius plus two. Arriving is a pathfinder result, not a
    // tile equality: the walker stops on whatever legal tile it can reach, and
    // twenty bots converging on one bank cannot all stand on the same one.
    return TileDist(client.PlayerX(), client.PlayerY(), p->position.x,
                    p->position.y) <= p->radius + 2;
}

// A DIFFERENT QUESTION FROM AtMarketBank: that one asks "am I at THE market
// bank" (market::kMarketBankPlaceId, the one designated trade rendezvous);
// this asks "am I near A bank at all" -- the nearest one the atlas knows of,
// full stop. DoBank's own arrival test, so a fresh character's first BANK
// goal knows whether it has to travel before BankErrand's mobile scan has
// anything to find.
bool Runner::NearAnyBank(Client& client, const Observation& obs) const {
    const wm::Place* p = client.NearestServicePlace(wm::Service::Banker);
    if (!p) return false;
    // The place's own radius plus a few tiles of slack -- same shape as
    // AtMarketBank's "+2", widened a little because BankErrand's own mobile
    // scan (NearestMobileWithTrade) needs the banker in view, not merely the
    // place's rim.
    return TileDist(obs.x, obs.y, p->position.x, p->position.y) <=
           p->radius + 4;
}

void Runner::ForgetBankedStock(const char* item) {
    if (!item || !item[0]) return;
    for (usize i = 0; i < state_.bank.size(); ++i) {
        if (state_.bank[i].item != item) continue;
        state_.bank.erase(state_.bank.begin() +
                          static_cast<std::ptrdiff_t>(i));
        return;
    }
}

bool Runner::DoTradeWithPlayer(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    // A SECOND SELLER TURNED UP AND WAS CLOSED ON THE WIRE (Client.h
    // TakeDeclinedTrade). Say so out loud too: the packet ends their window,
    // the words end their errand -- otherwise they walk back and try again.
    {
        u32 declSerial = 0;
        std::string declName;
        if (client.TakeDeclinedTrade(&declSerial, &declName)) {
            LogLine("trade: declined a second window from %s -- already sorted "
                    "with %s", declName.c_str(), tradePartnerName_.c_str());
            client.ActionSay(market::FormatDecline(declName).c_str());
            tradeDeclined_.push_back(declSerial);
        }
    }

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
        client.TradeForget();
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
        // A CANCEL IS ACKNOWLEDGED ONCE.
        //
        // TradeState latches Phase::Cancelled until something clears it, and
        // this branch used only to reset the Runner's own bookkeeping -- so it
        // re-fired on every tick. g_Odessa.console.txt 09:12:14.785 onwards:
        // ~200 "trade:  cancelled (partner_cancelled)" lines at 60ms, the
        // partner name already blanked by the first pass, until the anti-spin
        // backstop noticed. Forgetting the window, ending the goal and resting
        // is what closes that loop.
        client.TradeForget();
        ResetTradeState();
        planner_.NoteAttempt(obs.nowMs);
        planner_.Cooldown(GoalKind::TradeWithPlayer,
                          obs.nowMs + kTradeRetryRestMs);
        planner_.Finish(false, "the trade window was cancelled", obs.nowMs);
        return false;
    }

    // --- listen ------------------------------------------------------------
    //
    // Done BEFORE announcing, so a character that can answer somebody else's
    // offer does that rather than adding its own to the noise.
    //
    // AN OFFER GOES STALE. `tradeHeardMs_` only ever advances inside this
    // handler, so a life that trades, spends ten minutes mining, and comes
    // back would answer a WTS that expired nine minutes ago -- walk to a
    // seller who has left, and burn one of its three trips doing it. Clamp the
    // window to two announce intervals before now. THE JOURNAL CLOCK, not the
    // tick clock: JournalHeardSince and Heard::timeMs are both on it, and the
    // two are different clocks (the same mistake made a ten-second training
    // verification expire in 8.7s).
    const i64 journalNow = client.JournalNowMs();
    if (tradeHeardMs_ < journalNow - 2 * kAnnounceIntervalMs)
        tradeHeardMs_ = journalNow - 2 * kAnnounceIntervalMs;
    std::vector<Client::Heard> heard;
    client.JournalHeardSince(tradeHeardMs_, heard);
    if (!heard.empty()) tradeHeardMs_ = heard.back().timeMs;

    // Our own name, so a line addressed to another player can be recognised as
    // not ours to answer. The IDENTITY's name, which is the one this character
    // logged in under and the one other bots hear on the wire. Empty is the
    // tolerant direction: it reads as "said to the room".
    const std::string& myName = state_.identity.characterName;

    for (const Client::Heard& h : heard) {
        // THE BUYER SORTED IT WITH SOMEBODY ELSE. Losing the race is a normal
        // outcome of a room where two sellers hold the same goods; hearing so
        // is much cheaper than waiting out the 25s give-up at the bank.
        if (tradePartner_ != 0 && h.speaker == tradePartner_ &&
            market::ParseDecline(h.text, nullptr) &&
            market::AddressedTo(h.text, myName)) {
            LogLine("trade: %s sorted it with somebody else -- standing down",
                    h.name.c_str());
            state_.memory.NoteEvent("trade_declined", tradeItem_.c_str(),
                                    h.name.c_str(), obs.x, obs.y, obs.nowMs);
            ResetTradeState();
            planner_.NoteAttempt(obs.nowMs);
            planner_.Cooldown(GoalKind::TradeWithPlayer,
                              obs.nowMs + kTradeRetryRestMs);
            planner_.Finish(false, "the buyer sorted it with somebody else",
                            obs.nowMs);
            return false;
        }
        // WHICH KIND OF WTB IS THIS? A reply to our own standing offer, or
        // somebody announcing demand to the room? The two branches below mean
        // opposite things and both used to fire on the same line: a full
        // "WTB 8 i_ingot_iron 52gp" broadcast reached the reply branch first,
        // so a seller committed to the buyer WITHOUT ever saying a WTS back,
        // and the buyer had no deal of its own to fund. See
        // market::ClassifyBuyLine for the evidence.
        market::TradeIntent wtb;
        const market::BuyLineKind wtbKind =
            market::ClassifyBuyLine(h.text, &wtb);

        // Somebody answered OUR offer -- and named US doing it. Without the
        // addressee test one bare "WTB i_ingot_iron" was an answer to every
        // seller in earshot at once (2026-09-02: Kharain and Elvar both took
        // Odessa's reply as their own).
        if (wtbKind == market::BuyLineKind::Reply &&
            !tradeOffer_.item.empty() && tradePartner_ == 0 &&
            wtb.item == tradeOffer_.item &&
            market::AddressedTo(h.text, myName)) {
            LogLine("trade: %s wants our %s", h.name.c_str(), wtb.item.c_str());
            tradePartner_ = h.speaker;
            tradePartnerName_ = h.name;
            tradeItem_ = tradeOffer_.item;
            tradeSellingQty_ = tradeOffer_.qty;
            return false;   // next tick walks over and opens the window
        }
        // A COLD WTB -- somebody wants something we happen to be carrying, and
        // we never announced it.
        //
        // This is the half of the market that did not exist. The branch above
        // only ever recognised an answer to THIS character's own offer, so a
        // crafter could shout for logs all day beside a lumberjack holding two
        // hundred of them and neither would ever hear the other. Demand has to
        // be able to start a trade, not only accept one.
        //
        // Guarded on having no partner yet: once committed to a deal, other
        // people's shopping is not this errand's business -- and without the
        // guard the buyer's own "WTB i_log" confirmation would look like a
        // second, competing request.
        //
        // ANNOUNCE, not Reply: a line that carries a quantity and a price and
        // names nobody is demand talking to the room. That classification is
        // the whole fix -- this branch never ran while the reply branch above
        // was swallowing the same line.
        if (wtbKind == market::BuyLineKind::Announce && tradePartner_ == 0) {
            market::TradeIntent fill;
            if (market::AnswerBuyWant(*me, obs.pack, state_.prices,
                                      tradePolicy_, wtb, &fill)) {
                // SAY IT OUT LOUD, in the seller's own form. The buyer is
                // already listening for a WTS (the branch below), so this both
                // closes the loop mechanically and reads, to a human watching
                // the bank, like one player answering another.
                const std::string line = market::FormatSellOffer(fill);
                LogLine("trade: %s wants %d %s -- answering '%s'",
                        h.name.c_str(), wtb.qty, wtb.item.c_str(), line.c_str());
                client.ActionSay(line.c_str());
                tradePartner_ = h.speaker;
                tradePartnerName_ = h.name;
                tradeItem_ = fill.item;
                tradeSellingQty_ = fill.qty;
                tradeOffer_ = fill;
                // The seller opens the window, so this is the clock the walk
                // and the 20s wait below both run against.
                tradeAnnouncedMs_ = obs.nowMs;
                return false;
            }
        }
        // Somebody is selling something we need.
        market::TradeIntent offer;
        if (!market::ParseSellOffer(h.text, &offer)) continue;
        // ONE SELLER PER WANT. Already committed: tell the other one so, out
        // loud and once, and go no further. Without this the loop happily
        // re-pointed tradePartner_ at a second seller mid-deal, and the two of
        // them raced to open a window on a buyer who could pay for one.
        if (tradePartner_ != 0) {
            const bool told =
                std::find(tradeDeclined_.begin(), tradeDeclined_.end(),
                          h.speaker) != tradeDeclined_.end();
            if (!told && h.speaker != tradePartner_ && offer.item == tradeItem_) {
                LogLine("trade: %s also offers %s -- already sorted with %s",
                        h.name.c_str(), offer.item.c_str(),
                        tradePartnerName_.c_str());
                client.ActionSay(market::FormatDecline(h.name).c_str());
                tradeDeclined_.push_back(h.speaker);
            }
            continue;
        }
        // S4: what is in the hands, so a life already armed does not buy a
        // second sword. Layers 1 and 2 are the weapon and shield hands
        // (Client.cpp:51-52); nothing else is a trade decision today.
        const std::vector<market::WornItem> wornNow = {
            {1, client.EquippedGraphicAt(1)}, {2, client.EquippedGraphicAt(2)}};
        // PACK COIN, NOT THE STATUS-BAR FIGURE. obs.gold counts the bank box
        // (PayFromPackOnly=0 is a buy-from-vendor rule, not a player-trade
        // one); DriveOpenTrade below only ever offers
        // FindBackpackItemByGraphic(kGoldCoin), so accepting against obs.gold
        // let a life commit to a deal its pack could not actually pay for,
        // then stand there offering nothing once the window opened.
        const market::BuyDecision d = market::ConsiderOffer(
            *me, obs.pack, obs.goldOnHand, tradePolicy_, offer, wornNow);
        LogLine("trade: heard '%s' from %s -> %s (%s)", h.text.c_str(),
                h.name.c_str(), d.accept ? "want it" : "no", d.reason);
        if (!d.accept) continue;
        // Say so out loud. The seller is listening for exactly this, and
        // saying it is also what makes the deal visible to a human watching.
        // NAME THE SELLER. See market.h FormatBuyReply: an unaddressed reply
        // is an answer to everybody holding that item.
        client.ActionSay(market::FormatBuyReply(offer.item, h.name).c_str());
        tradePartner_ = h.speaker;
        tradePartnerName_ = h.name;
        tradeItem_ = offer.item;
        tradeSellingQty_ = 0;            // we are the BUYER
        tradeWantQty_ = d.qty;
        tradeOfferPrice_ = offer.pricePerUnit;
        // STAMP THE CLOCK A BUYER ACTUALLY OWNS. The "seller never opened a
        // window" timeout below reads tradeAnnouncedMs_, but only the
        // ANNOUNCE path ever wrote it -- a pure buyer left it at 0, and
        // obs.nowMs is steady_clock-since-boot, so `obs.nowMs - 0` was
        // already past 20000 on the very first tick within 2 tiles: the
        // buyer dropped a seller who had just opened for it. This is the
        // moment a buyer commits to the deal, so this is the clock the
        // 20s wait has to start from.
        tradeAnnouncedMs_ = obs.nowMs;
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
            // AND END THE ERRAND. Resetting alone left the goal live with no
            // partner, so the buyer went straight back to listening where it
            // stood -- which is what "Kharain and Elvar stuck at bank" looked
            // like from outside.
            ResetTradeState();
            planner_.NoteAttempt(obs.nowMs);
            planner_.Cooldown(GoalKind::TradeWithPlayer,
                              obs.nowMs + kTradeRetryRestMs);
            planner_.Finish(false, "the seller never opened a window",
                            obs.nowMs);
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- what this life came here for: sell, buy, or neither ----------------
    //
    // A MARKET HAS TWO SIDES and this handler only ever modelled one. The
    // first thing it did was ask ChooseSellOffer, and a life with nothing
    // spare fell straight out of the bottom -- so a smith twenty logs short
    // of the spear it wants to forge could not so much as walk to a bank.
    // The buyer half is the same errand read the other way round, and it is
    // decided here, once, before anything moves.
    //
    // BOTH read the bank as well as the pack. Everything this character ever
    // gathered is in a box, because banking is what it does when the pack
    // fills; a surplus it cannot see is a trip it never makes, and a
    // shortfall it has already banked against is a trip it should not make.
    std::vector<market::Stock> holdings = obs.pack;
    for (const market::Stock& b : obs.bank) {
        bool merged = false;
        for (market::Stock& h : holdings) {
            if (h.item == b.item) { h.qty += b.qty; merged = true; break; }
        }
        if (!merged) holdings.push_back(b);
    }

    market::TradeIntent offer;
    const bool wantsToSell = market::ChooseSellOffer(*me, holdings, state_.prices,
                                                     tradePolicy_, &offer);
    const char* noBuyWhy = nullptr;
    const std::vector<market::Want> buyable =
        market::PlayerMarketWants(*me, holdings, obs.goldOnHand, tradePolicy_,
                                  &noBuyWhy);
    const bool wantsToBuy = !buyable.empty();

    if (!wantsToSell && !wantsToBuy) {
        // Nothing worth announcing -- most often because this character has
        // never seen a price for what it carries and refuses to invent one --
        // AND nothing it is short of that a player could supply and it could
        // pay for.
        // SAME DEAD END, SAME COOLDOWN. Returning plain success here let the
        // need score identically on the very next tick and the goal was
        // re-picked sixteen times a second -- a lumberjack logged
        // goal=TRADE_WITH_PLAYER eight times in half a second and did nothing
        // else all session. An errand that cannot even be started is the
        // market being unavailable, not a goal that succeeded.
        LogLine("trade: nothing to announce (no observed price for what is "
                "spare) and nothing to buy (%s)",
                noBuyWhy ? noBuyWhy : "nothing short");
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        // AND COOL THE GOAL, not only the need -- same reasoning as every
        // other stand-down below (fleet7.console.txt: a 244ms re-pick without
        // it). marketQuietUntilMs_ only blanks NeedTrade on the next Observe;
        // this dead end returned `true` (success) straight past the planner
        // without ever telling it to rest, so it was the one stand-down in
        // this handler still exposed to the instant re-pick.
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
        return true;
    }
    if (wantsToSell) tradeOffer_ = offer;

    // A BACK, NOT A BANK, CARRIES THE OFFER. i_log is WEIGHT=2.0
    // (runtime/scripts/items/i_provisions_logs.scp:64): 113 banked logs are 226
    // stones against a 40-STR cap of 180, so the withdrawal below would ask for
    // all of them, the server refuses, and the block re-issues it every 2 s for
    // the whole goal. Two stones per unit is the floor; four fifths of the
    // headroom is margin.
    if (wantsToSell) {
        const i32 fits = ((std::max(0, obs.maxWeight - obs.weight) * 4) / 5) / 2 +
                         market::QtyOf(obs.pack, offer.item);
        if (offer.qty > fits) offer.qty = std::max(0, fits);
    }

    // --- go to the market ---------------------------------------------------
    //
    // ONE PLACE FOR THE WHOLE FLEET. The nearest bank is the right answer for
    // every other errand and the wrong one for this: a rendezvous where each
    // party picks its own nearest bank is not a rendezvous. See
    // market::kMarketBankPlaceId for which bank and why.
    //
    // ARRIVAL IS GEOMETRY, NOT AN OPEN BOX. `obs.atBank` means the bank
    // container is open (Observe), which a buyer has no reason to do -- and
    // gating the journey on it would have a buyer standing at the market
    // re-issuing the walk forever.
    const bool haveMarket = MarketPlaceUsable(client);
    const bool arrived = haveMarket ? AtMarketBank(client)
                                    : (obs.atBank || client.BankContainer() != 0);
    if (!arrived) {
        // NOT AT THE MARKET: whatever box was open is behind us. The serial
        // and its cached contents both survive the walk (Client keeps them),
        // so this flag is the only thing that remembers the box was opened
        // somewhere else and must be asked for again on arrival.
        marketBoxOpened_ = false;
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            // DO NOT START A TRIP THE CLOCK CANNOT FINISH. A market attempt is
            // 250s out + 60s listening + 250s back (all three legs measured,
            // docs/S5_MARKET_TRIP_PLAN.md section 3), and wind-down needs its
            // own budget on top. Without this a life spends its last eight
            // minutes walking and wind-down finds it in open country -- which
            // is exactly the Corwyn death loop recorded above in the WindDown
            // phase: logged out in the wild, killed where it stood, full loot.
            const i64 leftMs = cfg_.sessionLimitMs - (obs.nowMs - sessionStartMs_);
            if (cfg_.sessionLimitMs > 0 && leftMs < kMarketTripBudgetMs) {
                LogLine("goal_blocked=TRADE_WITH_PLAYER reason=\"not enough "
                        "session left for the trip\" left=%llds need=%llds",
                        static_cast<long long>(leftMs / 1000),
                        static_cast<long long>(kMarketTripBudgetMs / 1000));
                planner_.Cooldown(GoalKind::TradeWithPlayer,
                                  obs.nowMs + kMarketQuietMs);
                planner_.Finish(false, "not enough session left for the trip",
                                obs.nowMs);
                marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
                return false;
            }
            // BOUND THE TRIPS. This walk was unbounded, and the flag below
            // clears on the very next tick, so a character that never arrives
            // re-issues the journey every two seconds until the goal's limit
            // kills it -- and is then handed the same errand again.
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
            if (wantsToSell)
                LogLine("market: taking %d %s to %s (trip %d)", offer.qty,
                        offer.item.c_str(),
                        haveMarket ? market::kMarketBankPlaceId
                                   : "the nearest bank",
                        tradeTrips_);
            else
                LogLine("market: going to %s to buy %d %s (trip %d)",
                        haveMarket ? market::kMarketBankPlaceId
                                   : "the nearest bank",
                        buyable.front().qty, buyable.front().item.c_str(),
                        tradeTrips_);
            travelInFlight_ =
                haveMarket ? client.TravelToPlace(market::kMarketBankPlaceId)
                           : client.TravelToService(wm::Service::Banker, nullptr);
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
    tradeTrips_ = 0;  // arrived: reset the trip allowance for the errand ahead.

    // --- collect the stock before selling it --------------------------------
    //
    // Announcing goods that are in a box on the other side of town is an offer
    // it cannot honour, so the withdrawal is part of the errand -- and it is
    // the ONLY reason this goal ever opens a bank box. A seller with the goods
    // already in its pack, and every buyer, stands at the market with the box
    // shut. Opening it "just in case" is what bank_errand.h:16-19 warns about:
    // an empty box sends no 0x3C, nothing ever flips, and the character
    // re-opens the bank every 2.5 seconds forever.
    if (wantsToSell) {
        const i32 inPack = market::QtyOf(obs.pack, offer.item);
        const i32 inBank = market::QtyOf(obs.bank, offer.item);
        if (inPack < offer.qty && inBank > 0) {
            // A LIFT IS AN ACTION AND YOU STAND STILL TO MAKE ONE.
            //
            // Neither guard was here, and both are the reason the withdrawal
            // below became a metronome: run_r4/pair_Durnholde.console.txt:4382
            // onwards logs "market: withdrawing 20 i_ingot_iron from the bank
            // to sell" seventy-six times between 20:37:43 and 20:40:27, once
            // every two seconds, each one answered immediately by
            // `drag_cancel: reason=0 cannot lift that`. The first of them was
            // issued at 20:37:43.245 -- 645 ms BEFORE `travel_done` at
            // 20:37:43.890 -- i.e. while still walking, which is the same
            // "the open container does not survive being walked away from"
            // lesson DoBank already carries for coin (see kCoin above).
            if (client.TravelBusy()) return false;
            if (client.ActionBusy()) return false;
            // (a) THE BOX MUST BE OPENED *HERE*, AT THE MARKET.
            //
            // `obs.atBank` is a CACHE test, not a proximity one --
            // `BankContainer() != 0 && ContainerKnown(...)` (Runner::Observe)
            // -- and neither half expires when the character walks away. So a
            // box opened in one town is still "open" a thousand tiles later:
            // Durnholde opened 0x40014400 at 20:34:17.896, walked to the
            // blacksmith guild and back, and arrived at the market with
            // obs.atBank still true and a cached stock list the server had
            // long stopped honouring. BankErrand::Tick returns Success
            // immediately whenever BankContainer() is set (bank_errand.h: the
            // box serial IS the success condition), so the inherited box has
            // to be dropped before asking, or the errand rubber-stamps it.
            if (!marketBoxOpened_) {
                if (!bankErrand_.Running()) {
                    if (client.BankContainer()) {
                        LogLine("market: the bank box was opened somewhere "
                                "else -- asking a banker here before trusting "
                                "what it says it holds");
                        client.ForgetBankContainer();
                    }
                    bankErrand_.Begin();
                }
                const life::BankErrandResult br = bankErrand_.Tick(client, obs);
                if (!br.why.empty())
                    LogLine("market: the stock is in the bank (%d %s) -- %s",
                            inBank, offer.item.c_str(), br.why.c_str());
                if (br.wake == life::Wake::AfterDelay && br.delayMs > 0)
                    nextActionMs_ = obs.nowMs + br.delayMs;
                if (br.status == life::ActivityStatus::Success) {
                    // Opened HERE. Cleared again the moment the character
                    // leaves for the market (the !arrived branch above) or the
                    // errand ends (ResetTradeState).
                    marketBoxOpened_ = true;
                    marketBoxReopens_ = 0;
                    return false;   // the box is open; the fetch runs next tick
                }
                if (life::IsTerminal(br.status)) {
                    LogLine("goal_blocked=TRADE_WITH_PLAYER reason=\"no banker "
                            "opened a box at the market (%s)\"", br.why.c_str());
                    bankErrand_.Cancel();
                    planner_.Cooldown(GoalKind::TradeWithPlayer,
                                      obs.nowMs + kMarketQuietMs);
                    planner_.Finish(false, "no banker at the market", obs.nowMs);
                    marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
                    return false;
                }
                planner_.NoteAttempt(obs.nowMs);
                return false;
            }
            // BY NAME, NOT BY GRAPHIC (S1) -- same reasoning as the bank
            // fetch in DoEarnGold: `inBank` is hue-resolved from obs.bank,
            // so the serial has to be found the same way or a shared metal
            // graphic hands over the wrong stack.
            i32 inBox = 0;
            const u32 serial = FindContainerItemByName(
                client, client.BankContainer(), offer.item.c_str(), &inBox);
            // (b) THE BOX IS THE TRUTH; THE LEDGER IS ONLY A MEMORY OF IT.
            //
            // `inBank` comes from obs.bank, which away from a box is
            // state_.bank -- what this character last SAW in its own box
            // (Runner::LearnFromObservation). A box that has been opened here
            // and shows none of it says the memory is wrong, and the memory is
            // what has to give. Silently falling through, as this did, left
            // the goal to reach the announce below and shout an offer it could
            // not honour -- or, with the offer still unsatisfied, to be handed
            // straight back and try the same lookup again.
            if (!serial) {
                LogLine("market: the bank ledger says %d %s but the box shows "
                        "none -- trusting the box", inBank,
                        offer.item.c_str());
                ForgetBankedStock(offer.item.c_str());
                planner_.Cooldown(GoalKind::TradeWithPlayer,
                                  obs.nowMs + kNoAudienceMs);
                planner_.Finish(false, "the bank does not hold the stock",
                                obs.nowMs);
                nextActionMs_ = obs.nowMs + kMarketWithdrawRetryMs;
                return false;
            }
            {
                i32 take = std::min(std::min(inBank, offer.qty - inPack), inBox);
                // CARRY WEIGHT IS A HARD LIMIT, NOT A SUGGESTION. `take` used
                // to be bounded only by what the box held and what the offer
                // asked for -- Tarath's own bank held 113 spare logs, and a
                // withdrawal of all of them on top of his working stock left
                // him unable to move. This handler has no per-item weight
                // table wired to it (tiledata::StaticTile::weight lives
                // behind Client, unreached here), so it bounds the COUNT by
                // the raw stones of headroom the status packet already
                // reports -- obs.maxWeight - obs.weight -- the same fields
                // DoMine's 0.95-full gate reads. Every tradeable good on this
                // shard costs at least one stone, so this can only ever
                // UNDER-admit units, never overload the pack; it is a floor,
                // not a measured per-item weight, and should be replaced with
                // real tiledata weight if that ever becomes reachable here.
                const i32 roomStones = std::max(0, obs.maxWeight - obs.weight);
                if (take > roomStones) {
                    LogLine("market: clamping withdrawal of %s from %d to %d "
                            "-- only %d stone(s) of carry room left (%d/%d)",
                            offer.item.c_str(), take, roomStones, roomStones,
                            obs.weight, obs.maxWeight);
                    take = roomStones;
                }
                if (take <= 0) {
                    LogLine("market: no carry room left for %s (%d/%d) -- "
                            "leaving it in the bank for now", offer.item.c_str(),
                            obs.weight, obs.maxWeight);
                    planner_.Cooldown(GoalKind::TradeWithPlayer,
                                      obs.nowMs + kNoAudienceMs);
                    planner_.Finish(false, "no carry room for the stock", obs.nowMs);
                    nextActionMs_ = obs.nowMs + 5000;
                    return false;
                }
                // A LIFT THE SERVER REFUSES IS AN ANSWER, NOT A HICCUP.
                //
                // The box serial outlives the visit and the cached contents
                // outlive the box, so a stale pair looks exactly like a full
                // one until the drag comes back "cannot lift that". Count the
                // refusals and re-ask a banker, which is precisely what
                // DoBank already does for coin ("the box will not give up its
                // coin -- reopening", coinLiftFails_ above). Progress -- any
                // change in what the pack holds -- resets the count.
                if (marketLiftItem_ != offer.item || marketLiftPack_ != inPack) {
                    marketLiftItem_ = offer.item;
                    marketLiftPack_ = inPack;
                    marketLiftFails_ = 0;
                }
                if (marketLiftFails_ >= kMaxMarketLiftFails) {
                    marketLiftFails_ = 0;
                    if (++marketBoxReopens_ > kMaxMarketBoxReopens) {
                        // Asked twice, opened twice, refused every time. The
                        // remembered stock is not really there.
                        LogLine("market: the bank ledger says %d %s but the "
                                "box will not give it up after %d reopenings "
                                "-- trusting the box", inBank,
                                offer.item.c_str(), marketBoxReopens_ - 1);
                        ForgetBankedStock(offer.item.c_str());
                        marketBoxReopens_ = 0;
                        planner_.Cooldown(GoalKind::TradeWithPlayer,
                                          obs.nowMs + kNoAudienceMs);
                        planner_.Finish(false,
                                        "the bank does not hold the stock",
                                        obs.nowMs);
                        nextActionMs_ = obs.nowMs + kMarketWithdrawRetryMs;
                        return false;
                    }
                    LogLine("market: the box will not give up its %s -- "
                            "asking a banker to open it again",
                            offer.item.c_str());
                    client.ForgetBankContainer();
                    marketBoxOpened_ = false;
                    nextActionMs_ = obs.nowMs + kMarketWithdrawRetryMs;
                    return false;
                }
                LogLine("market: withdrawing %d %s from the bank to sell",
                        take, offer.item.c_str());
                ++marketLiftFails_;   // cleared above the moment the pack moves
                // THROUGH THE ACTION SYSTEM, not around it. TakeFromContainer
                // sends 0x07/0x08 raw: no deadline, no ActionBusy, no verdict,
                // so a refused lift left nothing behind for the next tick to
                // read and the handler re-issued it every 2 s, seventy-six
                // times (run_r4/pair_Durnholde.console.txt:4382-4672).
                // ActionMoveItem is the same two packets with a 4 s deadline
                // (Client.cpp kMoveTimeoutMs) and a Rejected verdict on 0x27.
                client.ActionMoveItem(serial, static_cast<u16>(take),
                                      client.BackpackSerial());
                // (c) RETRY LONGER THAN THE DEADLINE. 2 s was shorter than the
                // 4 s move deadline, so every retry only ever superseded its
                // own predecessor and no attempt was ever allowed to resolve.
                nextActionMs_ = obs.nowMs + kMarketWithdrawRetryMs;
                return false;
            }
        }
        // The stock is in the pack: nothing is being lifted any more.
        marketLiftFails_ = 0;
        marketLiftItem_.clear();
        marketBoxReopens_ = 0;
    }

    // --- the buyer ASKS -----------------------------------------------------
    //
    // It used to only listen. Its whole errand at the market was to BE PRESENT
    // while somebody else announced, so a trade could start only when a
    // gatherer happened to shout the exact thing this life happened to need,
    // in the three minutes it happened to be standing here. Supply had a voice
    // and demand did not, which is half a market again.
    //
    // Now it says what it wants, on the same schedule and with the same bound
    // as the seller's WTS: item, quantity, and the most it will pay -- a number
    // from ITS OWN purse and its own observed prices, never a market rate,
    // because this fleet has no such thing.
    if (!wantsToSell) {
        market::TradeIntent want;
        const bool haveWant = market::ChooseBuyWant(
            *me, holdings, state_.prices, tradePolicy_, obs.goldOnHand, &want);
        if (marketListenFromMs_ == 0) {
            marketListenFromMs_ = obs.nowMs;
            LogLine("market: at the market to buy %d %s -- asking for %llds",
                    buyable.front().qty, buyable.front().item.c_str(),
                    static_cast<long long>(kListenMs / 1000));
        }
        if (haveWant && obs.nowMs - tradeAnnouncedMs_ >= kAnnounceIntervalMs) {
            const std::string line = market::FormatBuyWant(want);
            LogLine("trade: announcing '%s'", line.c_str());
            client.ActionSay(line.c_str());
            tradeAnnouncedMs_ = obs.nowMs;
            // THE ANNOUNCEMENT IS THE PLAN, and it has to outlive this tick.
            //
            // A seller answers a WTB by walking over and OPENING A WINDOW.
            // From that moment DoTradeWithPlayer short-circuits into
            // DriveOpenTrade and this listen loop never runs again, so
            // whatever the buyer knew about the deal had to be written down
            // before the window appeared. It never was: tradeWantQty_ and
            // tradeOfferPrice_ are set only in the "heard a WTS" branch, the
            // buyer computed owed = 0 and put nothing in
            // (g_Odessa.console.txt:257-284, 2026-09-02). Saying it out loud
            // and not remembering it is the whole defect.
            tradeWant_ = want;
            tradeWantAskedMs_ = obs.nowMs;
        }
        if (obs.nowMs - marketListenFromMs_ >= kListenMs) {
            LogLine("market: nobody answered %s in %llds -- back to work",
                    buyable.front().item.c_str(),
                    static_cast<long long>(kListenMs / 1000));
            marketListenFromMs_ = 0;
            state_.memory.NoteEvent("no_player_seller",
                                    buyable.front().item.c_str(), "", obs.x,
                                    obs.y, obs.nowMs);
            marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
            // GO AND MAKE IT YOURSELF, if this life can. RouteForInput is
            // catalogue reasoning -- "is this something my own profession
            // gathers or processes" -- and a miner_smith short of ingots has
            // exactly that answer: mine, then smelt. Standing down into a
            // ten-minute cooldown with a gather route available is a bot
            // waiting for a delivery it could have dug up.
            //
            // Nothing here buys the material from an NPC. Most materials are
            // WorldProcessed and the vendor policy refuses them, so the honest
            // fallback for everyone else is bank-and-wait -- which is what the
            // plain stand-down below already is.
            const std::string& shortOf = buyable.front().item;
            // WHICH gather goal depends on what this life gathers. "not ore,
            // therefore chop wood" was fine while only miners and lumberjacks
            // reached here; a tailor gathers WOOL, and handing it an axe would
            // have sent it to the forest for a bolt of cloth.
            const GoalKind gatherGoal =
                me->gathers == "ore"    ? GoalKind::Mine
                : me->gathers == "wool" ? GoalKind::MakeCloth
                                        : GoalKind::GatherLogs;
            if (!me->gathers.empty() &&
                market::RouteForInput(*me, shortOf.c_str(),
                                      /*npcTradeKnown=*/false) ==
                    market::SupplyRoute::SelfProduce) {
                return HandOff(GoalKind::TradeWithPlayer, gatherGoal,
                               kMarketQuietMs,
                               "nobody was selling it, and this life can "
                               "gather it itself",
                               obs.nowMs);
            }
            planner_.Cooldown(GoalKind::TradeWithPlayer,
                              obs.nowMs + kMarketQuietMs);
            planner_.Finish(false, "nobody was selling", obs.nowMs);
            return false;
        }
        nextActionMs_ = obs.nowMs + 1000;
        return false;
    }
    // NOTE: `marketListenFromMs_` is a BUYER-only clock now. It used to be
    // shared with a seller's empty-room wait (see the removed
    // PlayersNearby(kTradeEarshot)==0 gate below, dropped 2026-08-30): a
    // seller used to hold off announcing until a headcount saw somebody, but
    // that headcount only counts a mobile whose paperdoll TITLE is already
    // known, and nothing here proactively asked for one -- two bots stood
    // five tiles apart for three minutes each, both silent, because neither
    // had ever been double-clicked. A per-tick scan would fix that but does
    // not scale (300 bots at one bank double-clicking the whole room is an
    // O(N^2) paperdoll storm), so the seller no longer waits for a headcount
    // at all: it announces on schedule like a human at the bank, and
    // whoever answers is identified from the SPEECH packet's own speaker
    // serial, not from this cache.

    // ANNOUNCE ONLY WHAT IS IN THE HAND.
    //
    // `offer` was chosen from the pack AND the bank, because a surplus in a
    // box is a perfectly good reason to make the trip. An OFFER is a promise,
    // and a promise has to be honourable without a second errand: "WTS 113
    // i_log" from a character carrying none is a deal that cancels itself in
    // the trade window. If the withdrawal above did not move the goods, say so
    // and stand down rather than shouting a number that is not true.
    market::TradeIntent announce;
    if (!market::ChooseSellOffer(*me, obs.pack, state_.prices, tradePolicy_,
                                 &announce)) {
        LogLine("goal_blocked=TRADE_WITH_PLAYER reason=\"the stock is still in "
                "the bank (%d %s) and the box did not give it up\"",
                market::QtyOf(obs.bank, offer.item), offer.item.c_str());
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
        planner_.Finish(false, "the goods never left the bank", obs.nowMs);
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        return false;
    }
    tradeOffer_ = announce;

    // ANNOUNCE ON SCHEDULE, NOT ON A HEADCOUNT (design change, 2026-08-30).
    //
    // This used to hold off announcing until PlayersNearby(kTradeEarshot) > 0
    // and otherwise wait up to kListenMs for somebody to walk into range --
    // "dont try to sell with WTS if no one around" (project owner,
    // 2026-08-29). But PlayersNearby only counts a mobile whose paperdoll
    // TITLE is already known (Client.cpp PaperdollTitle), and a title only
    // arrives after a double-click (0x88); nothing in this wait loop ever
    // issued one, so two bots standing five tiles apart at the same bank
    // waited out the full three minutes each, silently, having never asked
    // who was there (run_r4/pair2). A per-tick scan would answer that but
    // does not scale: 300 bots at one bank each double-clicking the room is
    // an O(N^2) paperdoll storm.
    //
    // So this now behaves like a human at the counter: say the offer on the
    // normal kAnnounceIntervalMs/kMaxAnnounces schedule and let whoever is
    // listening answer it. A respondent is identified from the SPEECH
    // packet's own speaker serial (the `heard` loop above, via
    // JournalHeardSince), not from a nearby-mobile headcount, so no scan is
    // needed to close a sale -- only PlayersNearby/AudienceFingerprint, used
    // below purely to avoid repeating an offer to the same known audience,
    // still depend on titles, and those arrive incidentally (BankErrand's
    // own scan during a withdrawal, radius now matched to kTradeEarshot --
    // see Client.cpp:4234) rather than from anything this errand asks for.
    //
    // Other BOTS count: they are the market. NPCs do not, which is why the
    // fingerprint below still asks for players rather than for mobiles.

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
    // ZERO MEANS "NOBODY'S TITLE IS KNOWN", NOT "THE SAME EMPTY ROOM". With
    // no proactive scan, an unscanned room fingerprints as 0 far more often
    // than not, and tradeAudienceIgnored_ starts at 0 too (never declined).
    // Matching those would stand this errand down before its first-ever
    // announcement, and again every cycle whose room happened not to get
    // scanned by something else. Only suppress when the SAME KNOWN audience
    // declined -- an unknown audience is not evidence of anything.
    if (audience != 0 && audience == tradeAudienceIgnored_) {
        LogLine("trade: the same people who ignored the last offer are still "
                "here -- not repeating it");
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
        planner_.Finish(false, "audience already declined", obs.nowMs);
        return false;
    }

    if (obs.nowMs - tradeAnnouncedMs_ >= kAnnounceIntervalMs) {
        const std::string line = market::FormatSellOffer(announce);
        LogLine("trade: announcing '%s'", line.c_str());
        client.ActionSay(line.c_str());
        tradeAnnouncedMs_ = obs.nowMs;
        ++tradeAnnounceCount_;
    }
    if (tradeAnnounceCount_ >= kMaxAnnounces) {
        LogLine("trade: nobody answered %d offers of %s -- back to work",
                tradeAnnounceCount_, announce.item.c_str());
        tradeAudienceIgnored_ = client.AudienceFingerprint(kTradeEarshot);
        state_.memory.NoteEvent("no_player_buyer", announce.item.c_str(), "",
                                obs.x, obs.y, obs.nowMs);
        tradeAnnounceCount_ = 0;
        marketListenFromMs_ = 0;   // the wait is over; do not inherit it
        // AND STOP SCHEDULING IT for a while. Finishing the goal was not
        // enough: the need scored the same on the very next tick, the errand
        // was re-picked, and a lumberjack spent whole sessions announcing logs
        // to an empty Yew while its own training and hunting needs -- which it
        // could actually have finished -- sat underneath it.
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        // AND COOL THE GOAL, not only the need. This line was missing while
        // both sibling stand-downs above it ("no audience", "audience already
        // declined") had it, and the gap is measured: fleet7.console.txt:3245
        // stood down at 16:24:10.031 and the planner re-selected
        // TRADE_WITH_PLAYER at 16:24:10.275 -- 244 ms later, reason "previous
        // goal abandoned: nobody wanted it", NeedTrade 0.55 x 145 = 79.8. The
        // whole cycle repeated end-to-end every 50.9s. marketQuietUntilMs_
        // only blanks the NEED on the next Observe; the planner needed telling
        // too. A wasted market trip now costs 8m20s of walking, so the rest
        // after one has to exceed it: kMarketQuietMs is 10 minutes.
        planner_.Cooldown(GoalKind::TradeWithPlayer, obs.nowMs + kMarketQuietMs);
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

    // WHO IS ON THE OTHER SIDE -- from the packet, not from the journal.
    //
    // 0x6F SECURE_TRADE_OPEN carries the partner's serial AND name (trade.h),
    // which is authoritative. Heard::name comes from MobileName() and is empty
    // until something has learned the mobile's name, so a partner met purely
    // through speech logged as `trade:  put nothing in after 25s`
    // (g_Odessa.console.txt:280) and `trade: opening a window with  for 28
    // i_ingot_iron` (g_Elvar.console.txt:365). Backfilling from the window
    // costs nothing and makes every line below name somebody.
    if (tradePartner_ == 0 && tr.PartnerSerial() != 0)
        tradePartner_ = tr.PartnerSerial();
    if (tradePartnerName_.empty() && !tr.PartnerName().empty())
        tradePartnerName_ = tr.PartnerName();

    // A WINDOW THIS SIDE DID NOT PLAN, opened by a seller answering the WTB we
    // broadcast. There is no committed price or quantity because the "heard a
    // WTS" branch never ran for this deal -- but the want we said out loud a
    // few seconds ago is a plan, and it is the only honest basis for funding.
    //
    // FRESHNESS IS THE GUARD. `tradeWant_` is not cleared by every stand-down
    // path, so an old announcement must not fund a window opened half an hour
    // later for something else. The bound is the handshake's own turn times:
    // the listening window plus one announce turn.
    if (!tradeOffered_ && tradeSellingQty_ == 0 && tradeWantQty_ <= 0 &&
        tradeWant_.Valid() &&
        obs.nowMs - tradeWantAskedMs_ <= kListenMs + kAnnounceIntervalMs) {
        const i32 reserve =
            needCfg_.profession ? needCfg_.profession->goldReserve : 0;
        const market::FundingDecision fd =
            market::FundOpenWindow(tradeWant_, obs.goldOnHand, reserve);
        LogLine("trade: %s opened a window for the %d %s we asked for -- %s",
                tradePartnerName_.c_str(), tradeWant_.qty,
                tradeWant_.item.c_str(), fd.reason);
        if (fd.accept) {
            tradeItem_ = tradeWant_.item;
            tradeWantQty_ = fd.qty;
            tradeOfferPrice_ = tradeWant_.pricePerUnit;
            // The proof this trade moved anything is the pack and the purse,
            // and both have to be sampled BEFORE the coin goes in -- the
            // committed path samples them when it names a partner, and this
            // path has no such moment.
            tradePackBefore_ = market::QtyOf(obs.pack, tradeItem_);
            tradeGoldBefore_ = obs.gold;
        }
        // A refusal is left to time out on the window's own give-up clock
        // below rather than retried: there is no second answer to give.
    }

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
        // ActionTradeCancel latches Phase::Cancelled locally; forget it here
        // so the Cancelled branch in DoTradeWithPlayer does not then report the
        // same close a second time, and end the goal rather than dropping back
        // into the listening loop beside a partner that just went silent.
        client.TradeForget();
        ResetTradeState();
        planner_.NoteAttempt(obs.nowMs);
        planner_.Cooldown(GoalKind::TradeWithPlayer,
                          obs.nowMs + kTradeRetryRestMs);
        planner_.Finish(false, "the partner put nothing in the window",
                        obs.nowMs);
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
    // The broadcast want dies with the errand too -- a plan is only a plan
    // while the goal that made it is running. DriveOpenTrade additionally
    // bounds it by age, because not every stand-down reaches here.
    tradeWant_ = market::TradeIntent{};
    tradeWantAskedMs_ = 0;
    tradeOffered_ = false;
    tradePackBefore_ = 0;
    tradeGoldBefore_ = 0;
    tradeAnnounceCount_ = 0;
    tradeDeclined_.clear();
    travelInFlight_ = false;
    // THE TRIP ALLOWANCE IS PER ERRAND, NOT PER LIFE. It was reset only in the
    // failure branch, so a session that made one successful trip and later
    // wanted a second started from 1 of 3 and burned the whole allowance
    // across the day rather than across the errand.
    tradeTrips_ = 0;
    marketListenFromMs_ = 0;
    marketBoxOpened_ = false;
    marketLiftFails_ = 0;
    marketLiftPack_ = -1;
    marketLiftItem_.clear();
    marketBoxReopens_ = 0;
    // `bankErrand_` is deliberately NOT cancelled here: it is shared with the
    // bank and earn-gold errands, and an open box is useful to whatever runs
    // next. Only the market's own failure path cancels it.
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

// WHICH ERRAND ACTUALLY MAKES A THING THIS LIFE PRODUCES.
//
// Only consulted once market::RouteForInput has already said SelfProduce --
// i.e. the item is in this character's own `produces` list -- so this is
// choosing between that character's own goals, never inventing a capability.
// The prefixes are the same defname families SupplierTradeFor above matches
// on, and each maps to the goal that already exists for it: ore is dug (Mine),
// ingots are smelted from ore (Smelt, which chains back to Mine through
// NeedOre), fish are caught (Fish), timber is chopped (GatherLogs). Anything
// else a profession produces, it produces at a craft menu.
GoalKind ProducingGoalFor(const std::string& item) {
    if (item.rfind("i_ore_", 0) == 0)   return GoalKind::Mine;
    if (item.rfind("i_ingot_", 0) == 0) return GoalKind::Smelt;
    if (item.rfind("i_fish", 0) == 0)   return GoalKind::Fish;
    if (item == "i_log" || item == "i_board") return GoalKind::GatherLogs;
    return GoalKind::Craft;
}

// The craft-menu route table (kCraftMenus) and CraftMenuFor now live in
// Identity.cpp, next to ChooseCraft, so a no-server test can assert a route
// without linking the whole runner. Declaration: uo/life.h.

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

    // THE REAGENT POUCH COMES FIRST.
    //
    // PRACTICE_SKILL leaves a list here when the pack cannot pay for any spell
    // its book holds (see DoPracticeSkill and include/uo/spellcast.h). It is
    // the same errand as any other input -- a mage shop stocks every Magery
    // reagent, tm_vend.scp:633-656, which is why SupplierTradeFor already
    // answers "mage" for the i_reag_ family -- so it reuses this whole path
    // rather than growing a second vendor flow. It jumps the queue because a
    // mage with an empty pouch cannot practise at all.
    // Anything the pack has since acquired is off the list -- bought here a
    // moment ago, looted, or carried all along.
    for (usize i = 0; i < reagentWants_.size();) {
        if (market::QtyOf(obs.pack, reagentWants_[i]) > 0) {
            LogLine("supplies: %s is in the pack now -- off the reagent list",
                    reagentWants_[i].c_str());
            reagentWants_.erase(reagentWants_.begin() +
                                static_cast<std::ptrdiff_t>(i));
            // The reason PRACTICE_SKILL stood down has just been carried out
            // of the shop. Let it have its turn back rather than exploring
            // with a full pouch for the rest of the cooldown.
            if (reagentWants_.empty())
                planner_.ClearCooldown(GoalKind::PracticeSkill);
        } else {
            ++i;
        }
    }

    prod::Ingredient want;
    // prod::Ingredient::item is a const char*, so the string it points at has
    // to outlive the rest of this function -- hence the local copy.
    std::string reagentPick;
    if (!reagentWants_.empty()) {
        reagentPick = reagentWants_.front();
        want.item = reagentPick.c_str();
        want.qty = reagentWantQty_ > 0 ? reagentWantQty_ : 1;
    } else {
        const CraftIntent intent = ChooseCraft(*me, obs, needCfg_.craftBatch,
                                               &craftFocus_);
        if (!intent.item || intent.missing.empty()) {
            LogLine("supplies: nothing short after all");
            supplyItem_.clear();
            return true;
        }
        want = intent.missing.front();
    }
    if (supplyItem_ != want.item) {
        supplyItem_ = want.item;
        supplyTrips_ = 0;
        supplySkipPlaces_.clear();
        const char* trade = SupplierTradeFor(supplyItem_);
        supplyTrade_ = trade ? trade : "";
        // The service the trade word maps to, so a shopkeeper wearing a
        // different title for the same job is still recognised.
        supplyService_ = ServiceForTrade(supplyTrade_.c_str());
    }

    if (supplyTrade_.empty()) {
        // NO NPC SELLS IT IS NOT THE END OF THE ERRAND -- ask where it really
        // comes from. See market::RouteForInput (include/uo/market.h) for the
        // three characters this cost in the 2026-09-01 wave.
        const market::SupplyRoute route =
            market::RouteForInput(*me, supplyItem_.c_str(),
                                  /*npcTradeKnown=*/false);
        if (route == market::SupplyRoute::SelfProduce) {
            const GoalKind make = ProducingGoalFor(supplyItem_);
            LogLine("supplies: no NPC sells %s and this life makes it -- "
                    "handing the errand to %s instead of shopping for it",
                    supplyItem_.c_str(), GoalKindName(make));
            return HandOff(GoalKind::BuySupplies, make, kCraftStuckCooldownMs,
                           "this life produces its own input", obs.nowMs);
        }
        if (route == market::SupplyRoute::PlayerMarket) {
            LogLine("supplies: %s is a player-market good -- no NPC may sell "
                    "it, so this is a rendezvous, not a shopping trip",
                    supplyItem_.c_str());
            return HandOff(GoalKind::BuySupplies, GoalKind::TradeWithPlayer,
                           kCraftStuckCooldownMs,
                           "another profession makes this, not a shopkeeper",
                           obs.nowMs);
        }
        LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" item=%s route=%s",
                faucet::RefusalName(faucet::Refusal::NoKnownSupplier),
                supplyItem_.c_str(), market::SupplyRouteName(route));
        // STAND DOWN, like the two failure paths below already do. CRAFT hands
        // off to BUY_SUPPLIES on kCraftStuckCooldownMs (Runner.cpp, "nothing
        // carried or worn to open the menu with"), so with Craft on a two
        // minute brake and this path finishing with none, Finish(false) alone
        // lets the planner re-pick BUY_SUPPLIES on the very next tick and the
        // pair alternates for the rest of the session. No trade sells this
        // input; that verdict is a table lookup and will not change today.
        // (audit 2026-08-30, finding 5.)
        planner_.Cooldown(GoalKind::BuySupplies, obs.nowMs + kCraftStuckCooldownMs);
        planner_.Finish(false, "no trade known to sell it", obs.nowMs);
        return false;
    }

    // THE POLICY DECIDES, not the shop. An NPC that technically stocks a thing
    // is not thereby a legitimate source for it -- that is the whole point of
    // the vendor matrix, and buying a player-market good from a vendor would
    // cut a real player out of the economy this project exists to simulate.
    const econ::VendorRuling ruling = econ::CanBuyFromNPC(supplyItem_.c_str());
    if (!ruling.allowed) {
        LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" item=%s class=%s (%s)",
                faucet::RefusalName(faucet::Refusal::RevolutionAuthenticityUnknown),
                supplyItem_.c_str(), econ::VendorClassName(ruling.klass),
                ruling.reason ? ruling.reason : "");
        state_.memory.NoteEvent("policy_refused", supplyItem_.c_str(),
                                econ::VendorClassName(ruling.klass), obs.x,
                                obs.y, obs.nowMs);
        // Same brake, and this is the case the audit named: an input the
        // vendor matrix refuses is refused for the whole session, so retrying
        // it next tick is not a retry, it is a character standing still.
        planner_.Cooldown(GoalKind::BuySupplies, obs.nowMs + kCraftStuckCooldownMs);
        planner_.Finish(false, "the vendor policy refuses this input", obs.nowMs);
        return false;
    }

    if (client.TravelBusy()) {
        VetoTripOverSessionBudget(client, obs, GoalKind::BuySupplies,
                                  "BUY_SUPPLIES", kCraftStuckCooldownMs);
        return false;
    }

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
            LogLine("supplies: looking for a '%s' to sell %d %s (trip %d, %zu "
                    "place(s) already tried)",
                    supplyTrade_.c_str(), want.qty, supplyItem_.c_str(),
                    supplyTrips_, supplySkipPlaces_.size());
            // SKIP WHAT WAS ALREADY SENT TO. Without a persistent skip list
            // every retry re-ran PickServicePlace with an empty one and
            // picked the SAME shop -- trip 2 walked Dorvar right back to the
            // Ocllo provisioner whose transit had just stalled, instead of
            // falling through to the next-best candidate. See
            // supplySkipPlaces_.
            travelInFlight_ = client.TravelToServiceSkipping(
                ServiceForTrade(supplyTrade_.c_str()),
                HomeOrNearest(state_.homeCity), {}, &supplySkipPlaces_);
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

    // Prefer plain iron (hue 0) over a coloured vein when both are in the
    // pack -- see FindIronOrePreferPlain's comment. oreHue is what actually
    // got picked, logged below at the point the ore is targeted.
    u16 oreHue = 0;
    const u32 ore = FindIronOrePreferPlain(client, &oreHue);
    if (!ore) {
        LogLine("smelt: no ore in the pack to melt");
        smeltStartedMs_ = 0;
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }

    // WHAT THIS ORE WILL BECOME (S1). FindIronOrePreferPlain reaches for a
    // COLOURED vein once the plain iron is gone, and the ore's hue is the
    // only thing that says which metal it is -- so the ingot it smelts into
    // is decided here, from that hue, and not assumed to be iron. The ore ->
    // ingot step is the ore ITEMDEF's own TDATA1; see econ::IngotNameForOre.
    const char* pickedOre = econ::ItemNameForGraphicAndHue(kIronOre[0], oreHue);
    const char* pickedIngot = pickedOre ? econ::IngotNameForOre(pickedOre) : nullptr;
    if (!pickedIngot) pickedIngot = "i_ingot_iron";   // honest fallback

    // DID THE LAST DOUBLE-CLICK LAND? The pack is the only honest witness.
    // Both outcomes are clilocs -- 1044270 on success, craft_smelt_fail on a
    // failed roll -- and 0xC1 is an explicit no-op in this client, so there is
    // nothing to read in the journal. Counting metal is the truth. This is the
    // same reasoning DoCraft states for inscription.
    //
    // COUNT THE METAL THAT IS ACTUALLY BEING MADE. This used to read
    // i_ingot_iron unconditionally: melt a bag of valorite ore and the count
    // never moved, so the goal reported no progress while producing the most
    // valuable thing on the shard -- and if it had moved it would have been
    // crediting a number nobody made.
    const i32 metal = market::QtyOf(obs.pack, pickedIngot);
    if (smeltIngotName_ != pickedIngot) {
        // The metal changed under us -- the old baseline counted a different
        // ingot entirely, so retake it rather than compare across metals.
        smeltIngotName_ = pickedIngot;
        smeltIngotsBefore_ = metal;
    }
    if (smeltStartedMs_ != 0 && metal > smeltIngotsBefore_) {
        LogLine("smelt: +%d %s (%d in the pack)", metal - smeltIngotsBefore_,
                pickedIngot, metal);
        planner_.NoteProgress();
        if (!state_.memory.HasEvent("first_smelt")) {
            state_.memory.NoteEvent("first_smelt", pickedIngot, "", obs.x,
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
        // as well as refusals. TWO is enough: the second walk to the same
        // stand tile that did not get us within reach tells us nothing the
        // first did not. Four cost Elvar 11 s of visible pacing at the mine
        // forge (2561,501) every session -- "if it is unreachable then it
        // should be 1 try max 2" (project owner, 2026-09-02).
        if (forgeTile.x == smeltForgeX_ && forgeTile.y == smeltForgeY_) {
            if (++smeltApproaches_ >= 2) {
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
                return HandOff(GoalKind::Smelt, GoalKind::IdleBriefly, 60000,
                               "no forge reachable", obs.nowMs);
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
            return HandOff(GoalKind::Smelt, GoalKind::IdleBriefly, 120000,
                           "arrived but no forge in reach", obs.nowMs);
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
            // What is actually being melted -- hue-resolved at the top of
            // this function, so a coloured vein is named honestly instead of
            // logged as "i_ore_iron" (S1,
            // docs/CRAFTER_RUN_2026_08_30.md #20). The ore count is read
            // against that same name too: obs.pack now keys a coloured vein
            // by its own defname, so asking it for "i_ore_iron" while melting
            // rusty ore printed "0 ore" with a full pack.
            LogLine("smelt: giving the forge's cursor the ore (%s hue 0x%04X, "
                    "%d ore, %d %s so far)", pickedOre ? pickedOre : "?",
                    oreHue,
                    market::QtyOf(obs.pack, pickedOre ? pickedOre : "i_ore_iron"),
                    metal, pickedIngot);
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
            // RETIRE IT IN THE LIST THE FORGE FINDER ACTUALLY READS.
            //
            // This wrote to deadTargets_ -- the MINING dead list -- while
            // NearestForge is passed deadForges_ (see the call above). So the
            // unreachable forge was never skipped, and the same one was
            // nominated again on the next pick, and the next:
            //
            //   goal_failed=SMELT reason="the forge at 2561,501 refused every
            //   approach after 3 tries"
            //
            // -- at 17:53 and again at 18:01, the identical tile, with a
            // pack of ore each time. (2561,501) sits just inside Minoc Mine
            // 1, so the forge a miner naturally walks to is the one he cannot
            // stand beside; there are others in Minoc, and now he can reach
            // for them.
            LogLine("smelt: the forge at %d,%d refused every approach after "
                    "%d tries -- writing it off and looking for another",
                    forgeTile.x, forgeTile.y, smeltReachFails_ - 1);
            deadForges_.emplace_back(forgeTile.x, forgeTile.y);
            if (deadForges_.size() > 16) deadForges_.erase(deadForges_.begin());
            smeltReachFails_ = 0;
            smeltForgeX_ = smeltForgeY_ = 0;
            // NOT a goal failure: the errand still has ore to melt and another
            // forge to melt it at. Failing here cooled SMELT down for minutes
            // and sent the character back to mining ore it could not process.
            nextActionMs_ = obs.nowMs + 1000;
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
    if (!me) {
        // A character with no trade cannot craft, and saying "done" would
        // report progress=0 forever. Stand down instead.
        planner_.Cooldown(GoalKind::Craft, obs.nowMs + kCraftStuckCooldownMs);
        planner_.Finish(false, "this character has no trade", obs.nowMs);
        return false;
    }

    const CraftIntent intent = ChooseCraft(*me, obs, 1, &craftFocus_);
    if (!intent.item) {
        LogLine("goal_failed=CRAFT status=no_progress reason=\"nothing this "
                "life can make and sell (%s)\"", intent.why);
        // NOT A COMPLETED ERRAND. This returned true, so the planner logged
        // `goal_completed=CRAFT progress=0` and handed the goal straight back
        // -- the shape the anti-spin backstop exists to catch.
        planner_.Cooldown(GoalKind::Craft, obs.nowMs + kCraftStuckCooldownMs);
        planner_.Finish(false, "nothing to make", obs.nowMs);
        return false;
    }
    if (!intent.missing.empty()) {
        // A BURNING FIRE IS KINDLING ALREADY SPENT: the server consumes it at
        // LIGHTING time, not per steak, so a character cooking beside its own
        // lit fire is not short of anything.
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
            return HandOff(GoalKind::Craft, GoalKind::BuySupplies,
                           kCraftStuckCooldownMs, "inputs are short", obs.nowMs);
        }
    }

    // A FIRE IS A STATION YOU CARRY -- lighting kindling turns the piece
    // itself into ITEMID_CAMPFIRE, so a fisher can cook where it fished. The
    // engine citations, and why the raw steak cannot be clicked onto the fire:
    // docs/SHARD_MECHANICS_LEARNED.md section 12.
    if (const prod::Recipe* r = prod::FindRecipe(intent.item)) {
        if (r->station == prod::Station::Fire &&
            !client.FindWorldItemByGraphic(kCampfireGraphic, 3)) {
            if (client.ActionBusy()) return false;
            // STAND STILL FIRST, then light what is ALREADY on the ground
            // before dropping another piece: Use_Kindling refuses kindling in
            // a container, and a failed Camping roll leaves the piece lying
            // there. docs/SHARD_MECHANICS_LEARNED.md section 12.
            if (client.TravelBusy()) return false;
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
                return HandOff(GoalKind::Craft, GoalKind::BuySupplies,
                               kCraftStuckCooldownMs, "no kindling for a fire",
                               obs.nowMs);
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
            // A FORGE THAT WORKS ENDS THE SEARCH -- same rule DoSmelt already
            // follows (search above, "A FORGE THAT WORKS ENDS THE SEARCH").
            // TravelToServiceSkipping records every place it is ever SENT to
            // in smeltSkipPlaces_, success or not (ClientTravel.cpp), and
            // CRAFT shares that list with SMELT. Without clearing it here, a
            // smith who wandered off this forge to bank or sell and then
            // needed it again could never be sent back to it: Durnholde used
            // Minoc's own smithy at 21:18, wandered out of NearestForge's
            // 20-tile sight at 21:20, and because "Minoc blacksmith" was
            // already on the list, TravelToServiceSkipping picked "Sea
            // Market blacksmith" (no walkable ground) and then "Papua
            // weaponsmith" -- 904 tiles and three moongates into the Lost
            // Lands -- for a service Minoc had offered the whole time
            // (docs/CRAFTER_RUN_2026_08_30.md defect 4, run_r4/pair_Durnholde
            // .console.txt 21:16-21:25).
            smeltSkipPlaces_.clear();
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
                    return HandOff(GoalKind::Craft, GoalKind::GetTool,
                                   kCraftStuckCooldownMs, "no smith hammer",
                                   obs.nowMs);
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
        craftWait_.Configure(life::RetryPolicy{kMaxCraftAttempts,
                                               kCraftResolveMs, 1000});
        craftWait_.Reset();
        craftHadBefore_ = market::QtyOf(obs.pack, craftItem_);
        craftJournalMs_ = client.JournalNowMs();
        craftMade_ = 0;
    }

    // DID THE LAST SWING LAND? Section 18's craft rule and both of its halves
    // -- the pack count rose, or the shard said in words that it did not --
    // decided in uo/activities/craft_confirm.h, which also carries the shard
    // strings and their evidence. Exhausted() AND out of ActionIssued: the
    // counter rises at NoteIssued, so Exhausted() alone would stand the goal
    // down while the last swing was still inside its own deadline.
    const i32 now = market::QtyOf(obs.pack, craftItem_);
    CraftConfirmInput cin;
    cin.packBefore = craftHadBefore_;
    cin.packNow = now;
    cin.deadlineExpired = craftWait_.Expired(obs.nowMs);
    cin.attemptsExhausted =
        craftWait_.Exhausted() &&
        craftWait_.State() != life::HandshakeState::ActionIssued &&
        craftWait_.State() != life::HandshakeState::WaitingForServer;
    usize nCraftFails = 0;
    const CraftFailure* craftFails = CraftFailures(&nCraftFails);
    for (usize fi = 0; fi < nCraftFails; ++fi) {
        if (client.JournalSaidSince(craftFails[fi].text, craftJournalMs_)) {
            cin.heard = &craftFails[fi];
            break;
        }
    }
    const CraftConfirmResult conf = ConfirmCraft(cin);
    if (conf.verdict == CraftVerdict::ShardRefused ||
        conf.verdict == CraftVerdict::NoProgress) {
        // A goal that achieved nothing says so and stands DOWN, so the planner
        // gives the turn to something else instead of re-picking it in 60 ms.
        LogLine("goal_failed=CRAFT status=no_progress reason=\"%s\" (%s, "
                "attempt %d of %d)", conf.reason, craftItem_.c_str(),
                craftWait_.Attempts(), kMaxCraftAttempts);
        craftWait_.Reset();
        // A fresh window: the refusal that ended THIS sitting must not end the
        // next one before a single click has gone out.
        craftJournalMs_ = client.JournalNowMs();
        return HandOff(GoalKind::Craft, GoalKind::IdleBriefly,
                       kCraftStuckCooldownMs, conf.reason, obs.nowMs);
    }
    if (conf.verdict == CraftVerdict::Spoiled) {
        // A real answer, so the swing is over -- but the trade is not. An
        // ATTEMPT, never progress; ChooseCraft ends the goal when stock runs out.
        LogLine("craft: %s -- taking another swing at %s", conf.reason,
                craftItem_.c_str());
        craftWait_.Reset();
        craftJournalMs_ = client.JournalNowMs();
        planner_.NoteAttempt(obs.nowMs);
    }
    if (conf.verdict == CraftVerdict::Made) {
        craftMade_ += conf.made;
        craftHadBefore_ = now;
        craftWait_.Reset();            // the thing we were waiting for arrived
        planner_.NoteProgress();
        LogLine("craft: made %s pack %d->%d (%d this sitting)",
                craftItem_.c_str(), now - conf.made, now, craftMade_);
        if (!state_.memory.HasEvent("first_craft")) {
            state_.memory.NoteEvent("first_craft", craftItem_.c_str(), "",
                                    obs.x, obs.y, obs.nowMs);
        }
        // KEEP GOING WHILE THE MATERIAL LASTS, for ANY trade -- the stock in
        // the pack is the honest limit, and kindling is the one carve-out
        // because the server spends it at lighting time. The evidence and the
        // owner's wording: docs/SHARD_MECHANICS_LEARNED.md section 12.
        //
        // ONE INGREDIENT-SETS WALK, not two. The old moreToUse (bool) and
        // canMake (int) loops both iterated rr->inputs, both carved out lit
        // kindling, both compared against market::QtyOf -- lifted here and
        // fed to DecideCraft, which needs exactly this count for both the
        // continue/stop verdict and the .makelast quantity
        // (S2_WIRING_PLAN.md S2.5).
        i32 inputsAvailable = 0;
        const prod::Recipe* rr = prod::FindRecipe(craftItem_.c_str());
        if (rr) {
            inputsAvailable = 500;
            for (const prod::Ingredient& in : rr->inputs) {
                if (!in.item || in.qty <= 0) continue;
                if (rr->station == prod::Station::Fire &&
                    std::strcmp(in.item, "i_kindling") == 0 &&
                    client.FindWorldItemByGraphic(kCampfireGraphic, 3)) {
                    continue;   // the fire is already lit
                }
                const i32 have = market::QtyOf(obs.pack, in.item);
                const i32 fits = have / in.qty;
                if (fits < inputsAvailable) inputsAvailable = fits;
            }
            if (inputsAvailable > 500) inputsAvailable = 500;
        }

        CraftRequest req;
        req.item = craftItem_.c_str();
        // A TOTAL, not a delta. `now` (== held below) already includes
        // whatever was in the pack before this sitting started, and
        // craftMade_ cancels out of desiredTotal-held across every call --
        // the batch target stays craftBatch above the pre-sitting stock
        // regardless of which Made event this is. Re-buying/re-making what
        // is already held is the exact bug craft.h:45-47 and the
        // craftHadBefore_ tracking above both warn about.
        req.desiredTotal = craftMade_ + needCfg_.craftBatch;
        // UNKNOWN: no profession field carries a working reserve
        // (S2_WIRING_PLAN.md S2.5). 0 for gathered inputs is the honest
        // default -- "craft till you are out of iron on your bag" is a
        // reserve of 0 and it applies to ore a miner dug. A reserve for
        // bought inputs is its own measured slice.
        req.minimumMaterialsReserve = 0;
        const CraftPlan plan = DecideCraft(req, /*held=*/now, inputsAvailable);
        if (plan.step != lastCraftPlan_) {
            LogPlan(CraftStepName(plan.step), plan.reason);
            lastCraftPlan_ = plan.step;
        }

        if (plan.step == CraftStep::Done) {
            LogLine("craft: %d %s made -- %s", craftMade_, craftItem_.c_str(),
                    plan.reason);
            // ONE SITTING RECORDED, not one piece. A sitting is the unit a
            // player would recognise as "I spent the morning on boards", and
            // counting pieces instead would flip the focus in the middle of a
            // batch and leave the bench half-worked.
            craftFocus_.NoteMade(craftItem_.c_str(), obs.nowMs);
            LogLine("craft_focus=%s sittings_in_a_row=%d",
                    craftFocus_.Last().c_str(), craftFocus_.Run());
            craftItem_.clear();
            return true;
        }

        if (plan.step == CraftStep::ShortOfInputs ||
            plan.step == CraftStep::ReserveHit) {
            // Not a failure: some progress already happened this call
            // (NoteProgress fired above). Find the first ingredient actually
            // short, so the handoff and any vendor lookup name the real
            // thing rather than the recipe's headline material.
            const char* missing = craftItem_.c_str();
            if (rr) {
                for (const prod::Ingredient& in : rr->inputs) {
                    if (!in.item) break;
                    if (rr->station == prod::Station::Fire &&
                        std::strcmp(in.item, "i_kindling") == 0 &&
                        client.FindWorldItemByGraphic(kCampfireGraphic, 3)) {
                        continue;
                    }
                    if (market::QtyOf(obs.pack, in.item) < in.qty) {
                        missing = in.item;
                        break;
                    }
                }
            }
            if (econ::CanBuyFromNPC(missing).allowed) {
                return HandOff(GoalKind::Craft, GoalKind::BuySupplies,
                               kCraftStuckCooldownMs, plan.reason, obs.nowMs);
            }
            const std::string gathers =
                needCfg_.profession ? needCfg_.profession->gathers
                                    : std::string("logs");
            const GoalKind gatherGoal =
                gathers == "ore" ? GoalKind::Mine : GoalKind::GatherLogs;
            return HandOff(GoalKind::Craft, gatherGoal, kCraftStuckCooldownMs,
                           plan.reason, obs.nowMs);
        }

        // CraftStep::Make. plan.remaining is the batch target already
        // clamped to what the materials allow -- feed it straight to
        // .makelast rather than the old raw material-availability count,
        // which ignored needCfg_.craftBatch entirely.
        if (!makeLastIssued_ && obs.WeightFraction() < 0.90 &&
            plan.remaining > 1) {
            // REPEAT WITH .makelast RATHER THAN RE-WALKING THE MENU. The
            // first item still goes through the menu -- that is what sets
            // TAG.revo.makelast.item -- and the server re-checks CANMAKE
            // every repetition, so skill, materials, tool and station stay
            // enforced. Why it had never worked before 2026-08-30, and why
            // gathering is untouched: docs/SHARD_MECHANICS_LEARNED.md
            // section 12.
            char cmd[64];
            std::snprintf(cmd, sizeof(cmd), ".makelast %d",
                          static_cast<int>(plan.remaining));
            LogLine("craft: %d %s made -- repeating the other %d with "
                    "'%s' instead of walking the menu again",
                    craftMade_, craftItem_.c_str(),
                    static_cast<int>(plan.remaining), cmd);
            client.ActionSay(cmd);
            makeLastIssued_ = true;
            nextActionMs_ = obs.nowMs + 3000;
            return false;
        }
        LogLine("craft: %d %s made and the material is not finished -- "
                "carrying on", craftMade_, craftItem_.c_str());
    }

    const CraftMenuPath* path = CraftMenuFor(craftItem_);
    if (!path) {
        // SOME OUTPUTS ARE NOT MENU CRAFTS AT ALL.
        //
        // Provenance::WorldProcessed means exactly that: "a station transforms
        // it; no craft menu, no skill" (production.h). Ore becomes ingots by
        // double-clicking it beside a forge, a whole fish becomes steaks under
        // a blade, wool becomes yarn at a wheel -- each has its own goal, and
        // no amount of menu-walking will ever reach one. Draver spent the
        // 2026-09-02 wave refusing i_ingot_iron here while SMELT, the goal
        // that actually does it, sat unpicked. Hand it over rather than
        // refuse: nothing is missing, the request was simply addressed to the
        // wrong goal.
        const prod::Recipe* wp = prod::FindRecipe(craftItem_.c_str());
        const GoalKind owner = ProducingGoalFor(craftItem_);
        if (wp && wp->provenance == prod::Provenance::WorldProcessed &&
            owner != GoalKind::Craft) {
            LogLine("craft: %s is not a menu craft (%s) -- %s is the goal that "
                    "makes it", craftItem_.c_str(),
                    prod::ProvenanceName(wp->provenance), GoalKindName(owner));
            return HandOff(GoalKind::Craft, owner, kCraftStuckCooldownMs,
                           "not a menu craft", obs.nowMs);
        }
        craftFocus_.NoteNoRoute(craftItem_.c_str());
        LogLine("goal_failed=CRAFT reason=\"%s\" no menu path known for %s",
                faucet::RefusalName(faucet::Refusal::MissingRecipe),
                craftItem_.c_str());
        planner_.Finish(false, "no craft menu path known", obs.nowMs);
        return false;
    }

    if (client.ActionBusy()) return false;

    // WAITING ON THE LAST ONE -- enter ONLY while an attempt is outstanding.
    // Not `State() != Idle`: NoteExpiry leaves the handshake in Backoff, which
    // means "the last swing is closed out, take another" and is the
    // fall-through below. Why the pack and a Handshake rather than a timer,
    // and the session Voris spent proving it: uo/activities/craft_confirm.h
    // and uo/activities/craft.h.
    if (craftWait_.State() == life::HandshakeState::ActionIssued ||
        craftWait_.State() == life::HandshakeState::WaitingForServer) {
        if (client.ActionBusy()) return false;

        if (!craftWait_.Expired(obs.nowMs)) {
            nextActionMs_ = obs.nowMs + 700;
            return false;
        }
        craftWait_.NoteExpiry(obs.nowMs);
        LogLine("craft: no result from the last %s in %llds (attempt %d of %d)",
                craftItem_.c_str(),
                static_cast<long long>(kCraftResolveMs / 1000),
                craftWait_.Attempts(), kMaxCraftAttempts);

        if (craftWait_.Exhausted()) {
            // The swing is closed out and there are none left. Come straight
            // back rather than deciding here: the confirmation at the top of
            // this handler is the ONE place that says no_progress, and now
            // that the handshake is out of ActionIssued it can.
            nextActionMs_ = obs.nowMs;
            return false;
        }
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
            // Three of these and ChooseCraft stops offering the output at all
            // (CraftFocus::NoteNoRoute). scribe3 said "the menu does not offer
            // 'Spell Circle 3'" hundreds of times in one run; the route table
            // is not going to change mid-session, so say it three times and
            // move on to something this life CAN make.
            craftFocus_.NoteNoRoute(craftItem_.c_str());
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
        // LET THE CRAFT FINISH BEFORE TOUCHING ANYTHING ELSE. The swing now
        // belongs to the Handshake and its answer to uo/activities/
        // craft_confirm.h: the pack is watched until the count rises or the
        // shard says why not, and the deadline is only a floor under a craft
        // that failed silently. "you are not waiting to finish one poison"
        // (project owner, 2026-08-30).
        craftWait_.NoteIssued(obs.nowMs);
        craftJournalMs_ = client.JournalNowMs();   // read the answer from here
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
    // WORN COUNTS. THE TOOL IS USUALLY IN A HAND BY THE TIME WE LOOK.
    //
    // This goal equips the smith tool into HAND1 a few lines above, because
    // the blacksmith menu reads LAYER_HAND1 -- and then looked for it in the
    // BACKPACK, where it no longer is. Corwyn walked to a forge, armed his
    // hammer, and was told "nothing in the pack to open the i_dagger menu
    // with" while holding it (v1_Corwyn.console.txt, 14:42:39). That blocks
    // the whole miner_smith economy: no dagger, so no sale, so no income.
    //
    // The same trap the fishing pole set -- a newbie kit hands out tools the
    // shard then EQUIPS, so "the pole in my pack" finds nothing while the
    // character is holding it. FindItemByGraphic(includeEquipped) exists for
    // exactly this and the scenario engine already uses it.
    u32 opener = 0;
    for (u16 g : openGfx) {
        opener = client.FindItemByGraphic(g, /*includeEquipped=*/true);
        if (opener) break;
    }
    if (!opener) {
        LogLine("goal_blocked=CRAFT reason=\"%s\" nothing carried or worn to "
                "open the %s menu with (%s)",
                faucet::RefusalName(faucet::Refusal::MissingTool),
                craftItem_.c_str(), r->inputs[0].item);
        return HandOff(GoalKind::Craft, GoalKind::BuySupplies,
                       kCraftStuckCooldownMs, "no material to start from",
                       obs.nowMs);
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
    // CUT IN BATCHES, BECAUSE CUTTING COSTS THE POLE.
    //
    // Source-X wields a bladed weapon that is double-clicked in the pack, and
    // i_fishing_pole is TWOHANDS=Y -- so every single cut takes the pole out of
    // the hands and puts it in the pack. Cutting one fish the instant it is
    // caught therefore costs a re-arm per fish: wave 2026-09-02 logged 643
    // "arming the pole" lines for Ithion in thirty minutes, one pair per catch.
    // Waiting for a handful of fish (or a pack heavy enough that the weight is
    // the problem) pays for the swap once instead of once per fish.
    //
    // The blade is looked for IN THE HAND AS WELL AS THE PACK (FindBlade). The
    // pack-only lookup was the other half of the ping-pong: once the dagger was
    // wielded the runner could no longer see it, fell through to the arming
    // branch, re-armed the pole, and had to wield the dagger again next tick.
    const i32 wholeFish = CountAny(client, kWholeFish,
                                   sizeof(kWholeFish) / sizeof(kWholeFish[0]));
    const usize kBladeN = sizeof(kBladedGraphics) / sizeof(kBladedGraphics[0]);
    const bool bladeInHand =
        GraphicIsAny(client.EquippedGraphicAt(kLayerHand1), kBladedGraphics,
                     kBladeN) ||
        GraphicIsAny(client.EquippedGraphicAt(kLayerHand2), kBladedGraphics,
                     kBladeN);
    // Once the knife is out, finish the pile: stopping half way would pay the
    // swap twice.
    const bool worthCutting =
        wholeFish >= kFishCutBatch || obs.WeightFraction() >= 0.85 ||
        (bladeInHand && wholeFish > 0);
    if (worthCutting) {
        if (const u32 blade = FindBlade(client)) {
            for (u16 g : kWholeFish) {
                const u32 whole = client.FindBackpackItemByGraphic(g);
                if (!whole) continue;
                LogLine("fish: cutting a whole fish (0x%04X) into steaks -- "
                        "four each, and lighter (%d whole left, blade %s)",
                        g, wholeFish, bladeInHand ? "in hand" : "in pack");
                client.ActionUseItemOn(blade, whole);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
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
    if (pole) {
        fishArmTries_ = 0;   // it is in hand: whatever we tried, it worked
    } else {
        for (u16 g : poleGfx) {
            const u32 inPack = client.FindBackpackItemByGraphic(g);
            if (!inPack) continue;
            // COUNT THE ATTEMPTS AND BACK OFF.
            //
            // An equip that is refused leaves the item back in the pack, which
            // looks exactly like "never tried" -- so a bare retry is a tight
            // loop with no end (643 of them in one gate). Each failure waits
            // longer, and after six the goal says so instead of spinning.
            if (fishArmTries_ >= 6) {
                LogLine("goal_failed=FISH reason=\"%s\" the pole would not stay "
                        "in hand after %d tries (last serial 0x%08X)",
                        faucet::RefusalName(faucet::Refusal::MissingTool),
                        fishArmTries_, inPack);
                fishArmTries_ = 0;
                planner_.Finish(false, "pole will not arm", obs.nowMs);
                return false;
            }
            ++fishArmTries_;
            LogLine("fish: arming the pole (try %d)", fishArmTries_);
            client.ActionEquip(inPack, kLayerServerChooses);
            nextActionMs_ = obs.nowMs + 1500 * fishArmTries_;
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
            // Remember the stand tile (obs.x/obs.y), not the water tile the
            // cast targeted (fishX_/fishY_). BestProvenResource feeds this
            // straight into TravelToPoint on a later trip, and a water tile
            // is never walkable -- that produced Dorvar's "goal (3662,2302)
            // is not walkable" spam (wave 2, 2026-09-01): the character is
            // stationary while fishing, so obs.x/obs.y is exactly the shore
            // tile it is standing on right now.
            state_.memory.NoteResource("fish", obs.x, obs.y, obs.z, true,
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

// WHAT THIS GRAPHIC IS FOR, in this character's hands.
//
// LifeNeedsGraphic above answers yes/no, and yes/no is the wrong question for
// anything a life can own more than one of. A smith PRODUCES heater shields,
// so it answered "needed" for Corwyn's sixth shield exactly as loudly as for
// his first, and six of them rode along unsold past a vendor offering 61 gold
// each. The role decides the KEEP-COUNT; see uo/activities/disposal.h.
//
// Order matters. A thing can be both an input and a product -- iron ingots
// are made by a smith and consumed by one -- and being needed for the next
// item on the bench outranks being stock to sell.
ItemRole Runner::RoleOfGraphic(u16 gfx) const {
    if (gfx == kGoldCoin) return ItemRole::Money;

    const prof::Profession* me = needCfg_.profession;
    if (!me) return ItemRole::Unknown;

    auto named = [&](const std::string& item) {
        for (u16 g : econ::GraphicsForItem(item.c_str()))
            if (g == gfx) return true;
        return false;
    };

    for (const prof::ToolNeed& t : me->tools)
        for (u16 g : t.graphics) if (g == gfx) return ItemRole::Tool;

    for (const prof::ConsumableNeed& c : me->consumables)
        for (u16 g : c.graphics) if (g == gfx) return ItemRole::Consumable;

    for (const std::string& it : me->consumes)
        if (named(it)) return ItemRole::CraftInput;

    // An input to something on the recipe list is stock for the next make,
    // and outranks being a product in its own right.
    for (const std::string& made : me->produces) {
        const prod::Recipe* r = prod::FindRecipe(made.c_str());
        if (!r) continue;
        for (const prod::Ingredient& in : r->inputs)
            if (in.item && named(in.item)) return ItemRole::CraftInput;
    }

    // WHAT IT MAKES IS STOCK, AND STOCK IS FOR SELLING. Nothing here keeps a
    // spare back: the one being worn is equipped, and an equipped item is not
    // in the backpack the vendor's list is built from.
    for (const std::string& made : me->produces)
        if (named(made)) return ItemRole::Produce;

    // Anything else in the pack is loot, a gift, or a mistake. None of those
    // is a reason to keep carrying it.
    return ItemRole::Unknown;
}

// The first Create Food reagent the pack is out of, or nullptr when the cast
// can be paid for. Same table PRACTICE_SKILL reads (spellcast.h), same pack.
static const char* CreateFoodReagentShort(const Observation& obs) {
    const spell::SpellDef* d = spell::DefForSpell(kSpellCreateFood);
    if (!d) return nullptr;
    for (const char* const* r = d->reagents; *r; ++r)
        if (market::QtyOf(obs.pack, *r) <= 0) return *r;
    return nullptr;
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
        } else if (obs.mana >= kCreateFoodMana &&
                   CreateFoodReagentShort(obs) != nullptr) {
            // ReagentsRequired=1 on this shard (sphere.ini:1136): a cast with
            // an empty pouch is answered "You lack Mandrake Root for this
            // spell" and nothing else -- Elara logged it 94 times in one wave
            // (2026-09-02). No reagents means shop for bread like anyone else;
            // the practice loop's restock errand refills the pouch on its own
            // schedule.
            LogLine("food: Create Food is short of %s -- buying food instead "
                    "this time", CreateFoodReagentShort(obs));
            // fall through to buying
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

    // CORRECTION, 2026-08-30: A PROVISIONER *DOES* SELL FOOD HERE.
    //
    // The note below is kept because its reasoning is still right, but its
    // FACT is now wrong and acting on it would be a mistake. The four SELL
    // lines it calls commented out are live in the current tm_vend.scp
    // (1350-1353: bread, lamb, chicken, cooked bird at {5 38}), and the
    // project owner confirms provisioners sold food on Revolution.
    //
    // What actually failed in v1_Corwyn was not stock but the HANDSHAKE:
    // "food: the 'provisioner' would not open a shop". The shop never
    // opened, so its list was never read. Different problem, same afternoon.
    //
    // --- the original note, now historical -------------------------------
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
        // Revolution provisioners sell ordinary food.  Bakers are deliberately
        // not part of autonomous routing: normal lives use provisioners,
        // fishers, farms, or mage-created food, never a cross-city bakery trip.
        spec.Sell("provisioner", wm::Service::Provisioner);
        spec.graphic = 0;
        spec.what = "something to eat";
        spec.maxTrips = kMaxFoodTrips;
        foodErrand_.Begin(spec);
    }
    const life::VendorErrandResult r = foodErrand_.Tick(client, obs);
    LogErrandReason("food", r.why.c_str(), obs.nowMs);
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
        // An ask is an attempt; a wait is not. See DoReplaceEquipment.
        if (r.acted) planner_.NoteAttempt(obs.nowMs);
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

// --- WHAT IS IN A SPELLBOOK, READ THE WAY SPHERE ACTUALLY SENDS IT ----------
//
// A SPELLBOOK'S 0x3C IS NOT A LIST OF SCROLL ITEMS. Sphere synthesises one
// 19-byte record per spell in which the graphic is a CONSTANT and the SPELL
// NUMBER travels in the AMOUNT field
// (server/Source-X/src/network/send.cpp:1341-1358,
// PacketItemContents(const CClient*, const CItem* spellbook)):
//
//     for (int i = SPELL_Clumsy; i <= SPELL_MAGERY_QTY; ++i) {
//         if (!spellbook->IsSpellInBook((SPELL_TYPE)i)) continue;
//         writeInt32(UID_F_ITEM + UID_O_INDEX_FREE + i);  // synthetic serial
//         writeInt16(0x1F2E);                             // ALWAYS 0x1F2E
//         writeByte(0);
//         writeInt16((word)i);                            // <- the spell, 1..64
//         writeInt16(0); writeInt16(0);                   // x, y
//         writeInt32(spellbook->GetUID());
//         writeInt16(HUE_DEFAULT);
//     }
//
// (SPELL_Clumsy = 1, uofiles_enums.h:670. The 2.0.7 stream carries no grid
// byte, so the record is the 19-byte form Client::OnContainerContents already
// parses -- pol_packets.md's 0x3C, older-client loop. The 0x3C parse was and
// is correct; what was wrong was believing a book row's GRAPHIC meant
// anything.)
//
// Reading the graphic therefore reports "spell 1" for every row of every book:
// exactly the [1,1,1,1,1,1,1,1,1,1,1,1,1] Ilyandra's THIRTEEN-spell book
// produced (run_r4/w_Ilyandra.console.txt:501). The book was fine. The read
// was wrong -- and it made BookHasGraphic answer "already known" for a Clumsy
// scroll and "not known" for every other scroll on the shelf, which is the
// opposite of useful when the whole point is to buy a spell the book lacks.
bool Runner::BookHasSpell(Client& client, u32 book, int spell) const {
    if (!book || spell <= 0) return false;
    const usize n = client.ContainerItemCount(book);
    for (usize i = 0; i < n; ++i) {
        u32 serial = 0; u16 g = 0, amount = 0;
        if (!client.ContainerItemAt(book, i, &serial, &g, &amount)) continue;
        if (static_cast<int>(amount) == spell) return true;
    }
    return false;
}

// Is the spell this SCROLL teaches already in the book? Takes a scroll's own
// graphic -- what a vendor row or a pack item carries -- and asks the question
// in the book's own currency.
bool Runner::BookHasGraphic(Client& client, u32 book, u16 graphic) const {
    return BookHasSpell(client, book, SpellForScrollGraphic(graphic));
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
// THE SCROLL ERRAND GIVING UP, in one place so every exit rests the same way.
//
// FILL_SPELLBOOK only. The gear and scissors errands borrow BuyScrollFrom and
// must keep their own four-minute rest: an armourer that was out of chainmail
// really may have some in four minutes, whereas the reason a mage found no
// scroll is that few sellers stock one at all.
i64 Runner::StandDownFromScrollShopping(const Observation& obs,
                                        const char* why) {
    ++scrollStandDowns_;
    const i64 rest = life::ScrollShoppingRestMs(scrollStandDowns_);
    LogLine("goal_failed=FILL_SPELLBOOK reason=\"%s\" -- %d empty scroll "
            "errand(s) running, resting %llds so practice, earning and "
            "training get the turn (the book is still wanted)",
            why, scrollStandDowns_, static_cast<long long>(rest / 1000));
    planner_.Cooldown(GoalKind::FillSpellbook, obs.nowMs + rest);
    scrollShopSinceMs_ = 0;
    scrollShopTickMs_  = 0;
    // The next errand starts its shop tour from scratch rather than resuming
    // half-way through an exhausted one.
    spellbookTrips_ = 0;
    spellbookSkipPlaces_.clear();
    spellbookSkipSellers_.clear();
    scribeExhausted_ = false;
    return rest;
}

bool Runner::BuyScrollFrom(Client& client, const Observation& obs,
                           const char* trade, wm::Service svc, u16 graphic,
                           bool skipKnown, u16 qty, const char* what,
                           GoalKind owner) {
    if (client.TravelBusy()) return false;

    // TravelToService reaches a shop tile, not a named mobile.  This helper
    // used to arrive and immediately select the next atlas shop because its
    // title cache was still empty; a blacksmith could therefore be walked past
    // without ever being asked to open a trade window.  Finish the arrival
    // handshake once: scan paperdolls, let the normal cache update, then make
    // the seller decision on the next tick.
    if (travelInFlight_) {
        travelInFlight_ = false;
        LogLine("%s: arrived at a %s shop -- scanning nearby sellers",
                GoalKindName(owner), trade);
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // THE TRIP COUNTER BELONGS TO THE ERRAND, not to this helper. Three goals
    // share it, and without this a gear trip spent the spellbook's allowance
    // and vice versa -- the fencer's 1,932 identical "buying armour" lines
    // came out of exactly that confusion.
    if (buyTripsOwner_ != owner) {
        buyTripsOwner_ = owner;
        spellbookTrips_ = 0;
        spellbookSkipPlaces_.clear();
        spellbookSkipSellers_.clear();
    }
    // Armourers and blacksmiths share the atlas service but use different
    // paperdoll titles.  A newly arrived character may be standing in a
    // blacksmith shop before it has ever seen an "armorer", so use the smith
    // as an equivalent seller only for this armour errand.  The actual offer
    // is still checked against the requested graphic before any gold moves.
    const char* sellerTrade = trade;
    u32 keeper = client.NearestShopkeeperWithTrade(sellerTrade, svc,
                                                   &spellbookSkipSellers_);
    if (!keeper && std::strcmp(trade, "armorer") == 0) {
        sellerTrade = "blacksmith";
        keeper = client.NearestShopkeeperWithTrade(sellerTrade, svc,
                                                   &spellbookSkipSellers_);
    }
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
            if (owner == GoalKind::FillSpellbook) {
                StandDownFromScrollShopping(
                    obs, "no scroll seller reachable after 3 trips");
            } else {
                LogLine("goal_failed=%s reason=\"no '%s' reachable after 3 "
                        "trips\"", GoalKindName(owner), trade);
                planner_.Cooldown(owner, obs.nowMs + kNoSpellbookCooldownMs);
            }
            planner_.Finish(false, "no seller reachable", obs.nowMs);
            return false;
        }
        LogLine("%s: looking for a '%s' to sell %s (trip %d)",
                GoalKindName(owner), trade, what, spellbookTrips_);
        // A mage can leave the current city to fill a spellbook.  Remember each
        // attempted shop so retries progress through the atlas rather than
        // returning to the same seller that was just unreachable or empty.
        travelInFlight_ = client.TravelToServiceSkipping(
            svc, HomeOrNearest(state_.homeCity), spellbookSkipSellers_,
            &spellbookSkipPlaces_, true);
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
                GoalKindName(owner), sellerTrade, d);
        travelInFlight_ = client.TravelToEntity(keeper, 1);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    if (!OfferBelongsTo(client, keeper)) {
        LogLine("%s: asking the %s to show %s", GoalKindName(owner), sellerTrade, what);
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
        // AND DO NOT BUY ONE THE BOOK HAS ALREADY REFUSED.
        //
        // BookHasGraphic reads the container rows, and a spellbook row's
        // graphic is not the scroll's -- see BookHasSpell. So the shelf check
        // above can say "the book lacks this" about a spell the book will
        // silently refuse, and Selene bought the SAME Cunning Scroll four
        // times for 84 gold in ninety seconds, adding nothing each time
        // (run_gates/g_Selene.console.txt:1274-1753, 2026-09-02). The book's
        // own refusal is the authoritative answer and it is already recorded;
        // this just stops the purse paying for it again.
        if (skipKnown) {
            bool refusedBefore = false;
            for (u16 r : scrollBookRefused_)
                if (r == v.graphic) { refusedBefore = true; break; }
            if (refusedBefore) { ++skipped; continue; }
        }
        if (static_cast<i32>(v.price) > obs.gold) continue;
        LogLine("spellbook: buying %s ('%s', 0x%04X, spell %d) at %d gold "
                "-- %d of this shop's scrolls were already in the book",
                what, v.name.c_str(), v.graphic,
                SpellForScrollGraphic(v.graphic),
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

    // ONE SHOPKEEPER'S SHELF IS NOT THE WHOLE TRADE.
    //
    // This used to fail the goal outright, and for a mage shop that verdict is
    // simply wrong: the template sells FOUR RANDOM SCROLLS
    // (random_first_circle .. random_fourth_circle, tm_vend.scp:721-724), so
    // "all four are already in the book" says nothing about the next mage's
    // roll. Thalia stood down on exactly that -- goal_failed=FILL_SPELLBOOK
    // "this 'mage' does not stock a scroll (4 already known)"
    // (run_gates/g_Thalia.console.txt:523). Remember this seller, spend a trip,
    // and go and look at a different shelf. The trip budget above is still the
    // brake, so this cannot become a tour of every mage on the shard.
    if (spellbookTrips_ < kMaxSpellbookTrips) {
        ++spellbookTrips_;
        spellbookSkipSellers_.push_back(keeper);
        LogLine("%s: this '%s' has nothing the book lacks (%d already known) "
                "-- its stock is randomised, so trying another shop (trip %d)",
                GoalKindName(owner), sellerTrade, skipped, spellbookTrips_);
        travelInFlight_ = client.TravelToServiceSkipping(
            svc, HomeOrNearest(state_.homeCity), spellbookSkipSellers_,
            &spellbookSkipPlaces_, true);
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    if (owner == GoalKind::FillSpellbook) {
        StandDownFromScrollShopping(
            obs, "every seller within reach stocks nothing this book lacks");
    } else {
        LogLine("goal_failed=%s reason=\"this '%s' does not stock %s "
                "(%d already known)\"", GoalKindName(owner), sellerTrade, what,
                skipped);
        planner_.Cooldown(owner, obs.nowMs + kNoSpellbookCooldownMs);
    }
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
//
// AND WHAT IT COSTS. The list below used to be a bare set of spell numbers and
// the goal cast the first one the book held, which is how four mages spent a
// whole session being told "You lack Sulfurous Ash for this spell" every six
// seconds (wave 2026-09-02). The reagent table and the choice itself now live
// in include/uo/spellcast.h, read off spells_magery.scp's own RESOURCES lines,
// so the pack is asked BEFORE the server is.
spell::PracticeChoice Runner::PickPracticeSpell(Client& client,
                                                const Observation& obs) const {
    spell::PracticeChoice none;
    if (obs.spellbookSerial == 0) return none;

    // ASK THE WHOLE TABLE, NOT A HAND-PICKED LIST.
    //
    // This used to walk twelve spell numbers compiled into spellcast.h, so a
    // mage practised with whatever those twelve happened to be at whatever
    // circle. Owner ruling 2026-09-02: "for mage to cast there are lots of
    // skills, don't hard code Create Food." The candidate set is now every
    // spell in data/revolution_spells.tsv -- all eight circles -- and the
    // rules that narrow it (safe on oneself, SKILLREQ reached, mana, reagents,
    // gain window, rotation) live in spell::ChoosePracticeSpell.
    //
    // ASK THE BOOK IN ITS OWN CURRENCY. A spellbook row's graphic is the
    // constant 0x1F2E for every spell; the SPELL NUMBER is in the amount.
    // See Runner::BookHasSpell for the packet and the evidence.
    spell::LoadSpellTable(client.DataDir());
    spell::PracticeSight see;
    for (const spell::SpellDef& d : spell::SpellTable()) {
        if (BookHasSpell(client, obs.spellbookSerial, d.spell))
            see.inBook.push_back(d.spell);
    }
    see.magery = obs.SkillTenths(rules::kMagery);
    see.mana = obs.mana;
    see.casts = practiceCastCounts_;
    // The pack as the character can count it -- the same hue-resolved defname
    // stocks every other errand reads. A reagent in the BANK is not a reagent
    // in hand, and Sphere consumes from the pack.
    see.pack = obs.pack;
    see.uncastable = practiceRefusedSpells_;
    const spell::PracticeChoice choice = spell::ChoosePracticeSpell(see);
    if (choice.spell >= 0 || !choice.missing.empty()) return choice;

    const usize n = client.ContainerItemCount(obs.spellbookSerial);
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
                      static_cast<int>(amount));
        had += buf;
    }
    LogLine("practice: the book holds %d item(s), spells [%s] -- %s",
            static_cast<int>(n), had.c_str(), choice.reason);
    return choice;
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
// BUY an armorer upgrade when the purse is clear of the reserve.  A filled
// slot is not automatically done: fighters begin in weak leather and must be
// able to replace it with a stronger legal piece sold by an armorer.
//
// The class rule is not a preference. On this shard a metal set stops a
// caster casting entirely, so ArmorFor refuses metal to anyone with Magery
// rather than scoring it lower.
const ArmorPiece* ArmorFor(u16 graphic) {
    for (const ArmorPiece& a : kArmorPieces)
        if (a.graphic == graphic) return &a;
    return nullptr;
}

// IS ANYTHING FROM kArmorPieces ON THE BODY RIGHT NOW.
//
// A helmet alone is not "geared for a graveyard".  Require three distinct
// protected layers and make one of them a core torso/leg layer.  This still
// lets a 50-STR newcomer use leather/ringmail rather than waiting for plate,
// while ordinary clothing has no entry in kArmorPieces and never counts.
bool Runner::HasBasicArmor(Client& client, const Observation& obs) const {
    bool layers[32] = {};
    int protectedLayers = 0;
    bool hasCore = false;
    for (const ArmorPiece& a : kArmorPieces) {
        if (!MayWear(a, obs)) continue;
        const u8 layer = client.ItemEquipLayer(a.graphic);
        if (!layer) continue;
        const u16 wornGfx = client.EquippedGraphicAt(layer);
        if (!wornGfx || !ArmorFor(wornGfx)) continue;
        if (layer < 32 && !layers[layer]) {
            layers[layer] = true;
            ++protectedLayers;
        }
        // Inner/middle/outer torso and inner/outer legs in the classic UO
        // equipment-layer protocol.
        if (layer == 13 || layer == 17 || layer == 22 ||
            layer == 23 || layer == 24) {
            hasCore = true;
        }
    }
    return hasCore && protectedLayers >= 3;
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

    // --- BUY THE BEST ARMORER UPGRADE ------------------------------------
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
    // The first armour set is part of a fighter's starting kit, not a luxury
    // upgrade.  A 10k fighter reserve is meant to finance bandages and the
    // return from a graveyard; treating it as an untouchable floor when the
    // character starts with exactly 10k leaves the fighter naked forever.
    const bool bootstrapArmor = needCfg_.profession &&
                               WantsToHunt(*needCfg_.profession) &&
                               !HasBasicArmor(client, obs);
    const i32 spendFloor = bootstrapArmor ? kArmorMoney : reserve + kArmorMoney;
    if (obs.gold <= spendFloor) {
        LogLine("gear: nothing carried is an upgrade, and %d gold is not clear "
                "of the %d%s -- earning first", obs.gold, spendFloor,
                bootstrapArmor ? " starter-gear floor" : " reserve");
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
        const u16 wornGfx = client.EquippedGraphicAt(layer);
        const ArmorPiece* worn = wornGfx ? ArmorFor(wornGfx) : nullptr;
        const int wornArmor = worn ? worn->armor : 0;
        // A worn item only closes this slot when it is at least as protective
        // as the candidate.  The prior `if (worn) continue` made every
        // warrior who owned starter leather permanently ineligible for an
        // armorer upgrade.
        if (wornArmor >= a.armor) continue;
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

    // Never infer that a town has no armourer from the current screen.  At
    // startup a fighter normally sees a healer or provisioner, not the smithy;
    // using that short-range cache sent Hector on a long tailor tour before
    // looking for the armour this goal was created to buy.  The atlas routes
    // the armorer service across town (and across gates) and the errand itself
    // can report a genuine stock failure if the shop does not carry the piece.
    const u8 wantLayer = client.ItemEquipLayer(want->graphic);
    const u16 currentGfx = wantLayer ? client.EquippedGraphicAt(wantLayer) : 0;
    const ArmorPiece* current = currentGfx ? ArmorFor(currentGfx) : nullptr;
    LogLine("gear: armorer upgrade 0x%04X (armor %d, needs str %d) replaces "
            "0x%04X (armor %d); buying from a %s",
            want->graphic, want->armor, want->reqStr,
            currentGfx, current ? current->armor : 0,
            "armorer");
    BuyScrollFrom(client, obs, "armorer", wm::Service::Blacksmith,
                  want->graphic, false, 1, "a piece of armour",
                  GoalKind::UpgradeGear);
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
                // Counts toward the first-visit deeper-advance below. A
                // veteran with a remembered vein never gets this far off
                // course; a newborn at the mouth racks these up one entrance
                // rock at a time.
                ++mineConsecRefusals_;
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
            // The roll failed, not the tile -- this IS genuine resource
            // ground, so the refusal streak that would send DoMine looking
            // deeper is no longer honest. Clear it.
            mineConsecRefusals_ = 0;
            resolved = true;
        }
        if (!resolved && client.JournalSaidSince("You put", mineJournalMs_)) {
            LogLine("mine: ORE at %d,%d", mineX_, mineY_);
            state_.memory.NoteResource("ore", mineX_, mineY_, mineZ_, true,
                                       obs.nowMs);
            planner_.NoteProgress();
            mineConsecRefusals_ = 0;
            mineAdvances_ = 0;
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

    // Observation is sampled before movement is pumped. A just-completed
    // journey therefore has a valid current client pose while `obs` can still
    // describe the old leg's starting tile for this one life tick. Mining
    // makes follow-up movement decisions here, so it must use the current
    // pose: using the stale observation sent Draver from the cave floor back
    // to Minoc Mine's entrance immediately after every confirmed arrival.
    const i32 hereX = client.PlayerX();
    const i32 hereY = client.PlayerY();
    const i8  hereZ = client.PlayerZ();

    // GO TO THE ROCK FIRST. "first he needs to go to mining area" (project
    // owner, 2026-08-29).
    //
    // Being at WORK is a district; being able to MINE is a tile. atWorkSite
    // accepts 45 tiles from a resource area's edge, which is the right test
    // for "is this a mining town" and far too loose for "can I swing here":
    // Corwyn stood in Minoc hitting ordinary ground and was told "Try mining
    // elsewhere" every time. Walk into the area, then swing.
    bool atHomeMineInterior = false;
    {
        // A fresh miner knows the mine in their home city.  The generic atlas
        // picker is intentionally nearest-to-current-position, which sent
        // Draver from his Jhelom spawn to a Britain resource centroid even
        // though his seeded home knowledge says Minoc.  Use that known lead
        // first; it is an ordinary journey, never a teleport or global fact.
        const KnownResourceSource* homeMine = nullptr;
        if (!state_.homeCity.empty()) {
            for (const KnownResourceSource& source : state_.memory.Resources()) {
                if (source.resource != "ore" || !source.hinted ||
                    source.label.find(state_.homeCity) == std::string::npos)
                    continue;
                if (!homeMine ||
                    (source.label.find(" Mine ") != std::string::npos &&
                     homeMine->label.find(" Mine ") == std::string::npos))
                    homeMine = &source;
            }
        }
        // A cave resource marker may be at its entrance while the productive
        // mining floor is much deeper in the same cave.  Measure arrival
        // against the actual destination we selected, otherwise a miner who
        // successfully reaches the interior will keep trying to return to the
        // entrance and exhaust the trip budget without ever swinging.
        i32 homeMineX = 0, homeMineY = 0;
        bool homeMineInterior = false;
        if (homeMine) {
            homeMineX = homeMine->x;
            homeMineY = homeMine->y;
            homeMineInterior = client.MiningInteriorTarget(
                homeMine->x, homeMine->y, &homeMineX, &homeMineY);
            // Close to the centroid OR genuinely inside the cave's own RECTs.
            // The centroid-only test flips false the moment a miner walks
            // toward a real rock near the RECT's edge (Minoc Mine 1 is
            // 26x27 tiles; kMineReach is 6), which sends the branch below
            // straight back to "go to the interior" every following tick and
            // undoes the walk to the rock -- Elvar ping-ponged between a rock
            // and the interior anchor for the last 13 minutes of a session
            // and never struck ore again (run_gates/wave15).
            atHomeMineInterior =
                homeMineInterior &&
                (TileDist(homeMineX, homeMineY, hereX, hereY) <= kMineReach ||
                 client.WithinMiningRegion(homeMine->x, homeMine->y, hereX,
                                           hereY));
        }
        if (homeMine &&
            TileDist(homeMineX, homeMineY, hereX, hereY) > kMineReach) {
            if (++mineTrips_ > kMaxMineTrips) {
                LogLine("goal_failed=MINE reason=\"could not reach home mine "
                        "in %d trips\"", kMaxMineTrips);
                planner_.Cooldown(GoalKind::Mine, obs.nowMs + kNoOreCooldownMs);
                planner_.Finish(false, "home mine unreachable", obs.nowMs);
                mineTrips_ = 0;
                return false;
            }
            i32 mineX = homeMineX, mineY = homeMineY;
            std::string destination = homeMine->label;
            if (homeMineInterior) {
                destination += " interior";
                LogLine("mine: %s resident going directly to the interior of "
                        "%s at %d,%d (trip %d)", state_.homeCity.c_str(),
                        homeMine->label.c_str(), mineX, mineY, mineTrips_);
            } else {
                LogLine("mine: %s resident going to known %s at %d,%d "
                        "(interior unavailable; trip %d)",
                        state_.homeCity.c_str(), homeMine->label.c_str(),
                        mineX, mineY, mineTrips_);
            }
            travelInFlight_ = client.TravelToPoint(mineX, mineY, 3,
                                                   destination.c_str());
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
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
    i32 scanX = hereX, scanY = hereY;

    // START FROM GROUND THAT HAS ACTUALLY GIVEN ORE.
    //
    // A rock GRAPHIC is not a rock RESOURCE. The server draws the distinction
    // itself: "Try mining elsewhere" (DEFMSG_MINING_1) means
    // CheckNaturalResource returned NULL -- there is no ore region on that
    // tile at all -- which is a different failure from DEFMSG_MINING_2, the
    // depleted vein. Corwyn collected the first kind twice in a row at
    // (2554,500) and (2554,498), because Minoc Mine 1 is
    // RECT=2556,474,2582,501 and both of those are OUTSIDE it: he was hitting
    // the cliff beside the doorway. "because he was at the entrance of the
    // mine" (project owner, 2026-08-30).
    //
    // Scanning from the character's boots always finds that entrance wall
    // first, since it is the nearest thing shaped like rock. But this life
    // already knows where the ore is -- Minoc Mine 1 at 2558,499, twenty-six
    // successes -- so start the search from the remembered spot whenever one
    // is close enough to walk to. The existing jitter still spreads him
    // around inside once he is there, and the dead list still retires worked
    // ground.
    bool haveNearbyMemory = false;
    if (!atHomeMineInterior) {
        if (const KnownResourceSource* known =
                state_.memory.BestResource("ore", hereX, hereY, obs.nowMs)) {
        if (known->successes > 0 &&
            TileDist(known->x, known->y, hereX, hereY) <= kMineKnownSpotWithin) {
            scanX = known->x;
            scanY = known->y;
            haveNearbyMemory = true;
        }
        }
    }

    // THE MOUTH IS PICKED CLEAN -- HEAD DEEPER IN. First-visit only: a
    // newborn has no memory (haveNearbyMemory stays false above), so every
    // scan keeps restarting from wherever he is standing, which is the
    // mouth. Three refusals in a row there ("try mining elsewhere" and kin)
    // means the entrance's rock-graphic tiles are exhausted, and the real
    // vein can sit past the ordinary scan radius -- walking further into
    // the mine's own RECTs is what finds it, not another jittered rescan of
    // the same doorway. Bounded by mineAdvances_ so a genuinely empty cave
    // still fails honestly (owner: "it is not going to mine deep -- that is
    // the problem", 2026-08-31; DeeperMiningTarget only fires inside a
    // Cave-kind region, so open-air rock is unaffected).
    if (!haveNearbyMemory &&
        mineConsecRefusals_ >= kMineRefusalsBeforeAdvance &&
        mineAdvances_ < kMaxMineAdvances) {
        i32 deepX = 0, deepY = 0;
        if (client.DeeperMiningTarget(hereX, hereY, &deepX, &deepY)) {
            LogLine("mine: the mouth is picked clean -- heading deeper in");
            ++mineAdvances_;
            mineConsecRefusals_ = 0;
            mineRoam_ = false;
            travelInFlight_ =
                client.TravelToPoint(deepX, deepY, 2, "deeper into the mine");
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
    }

    if (mineRoam_) {
        scanX += (i32)(obs.nowMs % 17) - 8;
        scanY += (i32)((obs.nowMs / 17) % 17) - 8;
        mineRoam_ = false;
    }
    Client::MiningSpot spot;
    bool allGuarded1 = false, allGuarded2 = false;
    if (!client.NearestMiningSpot(scanX, scanY, hereZ, kMineScanRadius, &spot,
                                  &deadTargets_, &allGuarded1) &&
        !client.NearestMiningSpot(hereX, hereY, hereZ, kMineScanRadius, &spot,
                                  &deadTargets_, &allGuarded2)) {
        // OWNER RULE: no gathering inside guarded zones. Both scans saw rock
        // and rejected every candidate for standing inside the guard line --
        // this is a walled-off cave mouth in town, not an empty vein, so do
        // not cool the goal down or dead-list open ground. The known-vein
        // fallback right below is already the proven-stand/travel logic this
        // rule wants; it just needs the right sentence ahead of it.
        const bool allGuarded = allGuarded1 && allGuarded2;
        if (allGuarded) {
            LogLine("mine: nothing to take outside the guard line here -- "
                    "going to the proven stand");
        }
        // NO ROCK HERE IS A REASON TO WALK, NOT A REASON TO GIVE UP.
        //
        // The gate above measures DistanceToResource, which is the distance
        // to a resource AREA -- a district, not a tile. Standing in Minoc
        // town it reported "the ore is 11 tiles off", the goal walked its
        // short leg, arrived somewhere with no rock in it at all, and failed:
        //
        //   mine: no mineable rock within 24 tiles of 2460,429 -- moving on
        //   goal_failed=MINE reason="no rock in reach"
        //
        // -- a hundred tiles from a mine whose exact position this life had
        // recorded twenty-six successes at. The pickaxe had just broken and
        // been replaced in town (v4_Corwyn, 2026-08-30 17:42), which is
        // precisely when a miner is furthest from the rock and most needs to
        // go back to it.
        //
        // A remembered spot that has actually produced ore beats any area
        // centroid, so walk to that before admitting defeat. Bounded by the
        // same trip counter, so an unreachable memory still fails honestly.
        if (!atHomeMineInterior) {
            if (const KnownResourceSource* known =
                    state_.memory.BestResource("ore", hereX, hereY, obs.nowMs)) {
            const i32 back = TileDist(known->x, known->y, hereX, hereY);
            if (known->successes > 0 && back > kMineReach &&
                ++mineTrips_ <= kMaxMineTrips) {
                LogLine("mine: no rock within %d tiles of %d,%d, but '%s' at "
                        "%d,%d has given ore %d time(s) -- walking the %d "
                        "tiles back (trip %d)",
                        kMineScanRadius, hereX, hereY,
                        known->label.empty() ? "a known vein"
                                             : known->label.c_str(),
                        known->x, known->y, known->successes, back,
                        mineTrips_);
                deadTargets_.clear();
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 2, "ore");
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            }
        }

        if (!allGuarded) {
            LogLine("mine: no mineable rock within %d tiles of %d,%d -- moving on",
                    kMineScanRadius, hereX, hereY);
        }
        deadTargets_.clear();
        mineTrips_ = 0;
        // Give up honestly rather than carry a maxed-out advance count into
        // whatever mine this character tries next.
        mineConsecRefusals_ = 0;
        mineAdvances_ = 0;
        planner_.Cooldown(GoalKind::Mine, obs.nowMs + kNoOreCooldownMs);
        planner_.Finish(false, "no rock in reach", obs.nowMs);
        return false;
    }

    // STRIKE ONLY WHEN ACTUALLY BESIDE IT. The engine wants the target at
    // least 1 and at most RANGE=2 tiles off (CCharSkill.cpp:1432-1441,
    // skill45_mining.scp RANGE=2); further out, walk to the vetted stand tile
    // and let the NEXT tick re-measure from wherever the walk actually ended.
    const i32 toRock = TileDist(hereX, hereY, spot.rockX, spot.rockY);
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
//
// THE NAMES ARE THE OBSERVATION, AND THEY COST A PACKET. This handler filters
// on m.name because the creature table is keyed by name -- but a name only
// exists in the client after the 0x98 AllNames query that ActionScanMobiles
// issues (Client.cpp PrintNearbyMobiles), and this path never issued it. Rhea
// (Taming 50.0, wave 2026-09-02) therefore walked to all three pastures and
// logged "nothing tamable here" 60 ms after each arrival with eight
// c_sheep_woolly inside ten tiles. Ask first, judge after: see
// include/uo/activities/tame.h.
bool Runner::DoTameAnimal(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;
    LoadSeededCreatureDanger(client.DataDir());

    const double mySkill = obs.SkillTenths(rules::kTaming) / 10.0;

    // ------------------------------------------------------------------
    // 1. WHAT DID THE SHARD SAY to the last attempt? Sphere answers taming
    // in words and nothing else: CCharSkill.cpp CChar::Skill_Taming sends
    // DEFMSG_TAMING_* (runtime/scripts/core/messages.scp:911-922) and
    // skill35_taming.scp adds @Fail/@Abort. Success is "It seems to accept
    // you as master" / "...accepts you once more as it's master"; the refusals
    // are permanent facts about this animal or this character, so they end the
    // attempt instead of being retried into the ground.
    if (tameAskedMs_ > 0) {
        // SPHERE ANSWERS TAMING AS A SYSTEM MESSAGE, AND THE SPEECH READER
        // THROWS THOSE AWAY. Client::JournalHeardSince drops every entry
        // whose sourceSerial is 0 or 0xFFFFFFFF (ClientTravel.cpp:2580-2581)
        // -- 'System messages carry no speaker to walk to' -- and
        // CChar::Skill_Taming says everything through SysMessage/SysMessagef
        // (Source-X CCharSkill.cpp:2280-2330). So this handler could not hear
        // its own answer: Rhea asked a sheep eight times over ninety seconds
        // while the shard replied 'Sheep is already tame.' after every one
        // (run_gates/g_Rhea.console.txt 13:35:06-13:36:22, run 2 of
        // 2026-09-02). JournalSaidSince reads the whole journal, system lines
        // included, which is what a player's screen shows.
        static const char* const kMastered[] = {
            "as master", "as it's master", "as its master",
        };
        // Permanent facts about this animal or this character: retrying them
        // is the futile loop above.
        static const char* const kRefused[] = {
            "too many followers", "cannot be tamed", "is already tame",
            "you can't tame", "your own master", "too far away",
        };
        for (const char* phrase : kMastered) {
            if (!client.JournalSaidSince(phrase, tameAskedMs_)) continue;
            // WHAT HAPPENS TO THE PET IS SPHERE'S BUSINESS, not ours.
            // Skill_Taming ends with NPC_PetSetOwner(this) and
            // Skill_Start(NPCACT_FOLLOW_TARG) (CCharSkill.cpp), so the animal
            // follows its new master by itself. This runner has no pet system
            // and does not pretend to one -- it records the tame, credits the
            // progress, and stands the goal down.
            LogLine("tame: success %s serial=0x%08X req=%.1f -- accepts me as "
                    "master (Taming %.1f, %d attempt(s)); it follows me now",
                    tameTargetName_.c_str(), tameTarget_, tameTargetReq_,
                    mySkill, tameAttempts_);
            state_.memory.NotePlace("pasture", "tamed here", obs.x, obs.y,
                                    obs.z, obs.nowMs);
            planner_.NoteProgress();
            planner_.Finish(true, "tamed an animal", obs.nowMs);
            tameAskedMs_ = 0; tameAttempts_ = 0; tameTrips_ = 0;
            tameTarget_ = 0; tameTargetName_.clear(); tameScanMs_ = 0;
            tameTargetReq_ = -1.0; tameVisited_.clear(); tameRefused_.clear();
            planner_.Cooldown(GoalKind::TameAnimal, obs.nowMs + kNoPetCooldownMs);
            return true;
        }
        for (const char* phrase : kRefused) {
            if (!client.JournalSaidSince(phrase, tameAskedMs_)) continue;
            // THE SHARD'S OWN WORDS ARE THE REASON. One refused animal is not
            // a refused goal, though: this creature is remembered as a dead
            // end and the next tick judges the spot again, which is what a
            // player does when a sheep turns out to be somebody's pet.
            LogLine("tame: '%s' refused -- the shard said \"%s\"; leaving it "
                    "alone", tameTargetName_.c_str(), phrase);
            tameRefused_.push_back(tameTarget_);
            tameAskedMs_ = 0; tameAttempts_ = 0;
            tameTarget_ = 0; tameTargetName_.clear(); tameTargetReq_ = -1.0;
            if (tameRefused_.size() >= static_cast<usize>(kMaxTameRefusals)) {
                LogLine("goal_failed=TAME_ANIMAL reason=\"%d animal(s) refused "
                        "me, last said \"%s\"\"",
                        static_cast<int>(tameRefused_.size()), phrase);
                planner_.Cooldown(GoalKind::TameAnimal,
                                  obs.nowMs + kNoPetCooldownMs);
                planner_.Finish(false, "the animals refused me", obs.nowMs);
                tameTrips_ = 0; tameScanMs_ = 0;
                tameRefused_.clear(); tameVisited_.clear();
            }
            return false;
        }
    }

    // ------------------------------------------------------------------
    // 2. ASK FOR THE NAMES before reading them. One scan per spot: it is
    // re-armed on arrival at a pasture (below) and by the first tick of the
    // goal, and nothing may be called empty until it has come back.
    if (tameScanMs_ == 0) {
        if (client.TravelBusy()) return false;
        LogLine("tame: reading the names of everything nearby before judging "
                "this spot (Taming %.1f)", mySkill);
        client.ActionScanMobiles();
        tameScanMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    std::vector<Client::HostileHit> nearby;
    client.ScanMobiles(12, nearby);

    u32 best = 0;
    double bestReq = -1.0;
    std::string bestName;
    for (const Client::HostileHit& m : nearby) {
        if (m.name.empty()) continue;
        const double req = SeededTamingFor(m.name);
        if (req < 0.0) continue;            // not a tamable creature at all
        bool refusedBefore = false;         // already said no, this session
        for (usize r = 0; !refusedBefore && r < tameRefused_.size(); ++r)
            refusedBefore = tameRefused_[r] == m.serial;
        if (refusedBefore) continue;
        if (req > mySkill) continue;        // beyond this character today
        if (best == 0 || req > bestReq) {   // the best it can actually manage
            best = m.serial; bestReq = req; bestName = m.name;
        }
    }

    if (!best) {
        // NOT YET. An empty result is only believable once the 0x98 replies
        // have landed and a settle window has passed -- everything before that
        // is a client that has not been told the names yet, which is exactly
        // how three pastures full of sheep read as deserted.
        TameScanSight sight;
        sight.scanIssued = tameScanMs_ > 0;
        sight.namesPending = client.MobileNamesPending();
        sight.msSinceScan = obs.nowMs - tameScanMs_;
        if (!MayJudgeEmpty(sight, kTameSettleMs)) {
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }

        int named = 0;
        for (const Client::HostileHit& m : nearby) if (!m.name.empty()) ++named;

        // GO WHERE THE ANIMALS ARE. Standing down was wrong: Cassia lives in
        // Britain, a city, and cities have almost nothing tamable in them. She
        // logged "nothing in sight" once and then spent the session BLOCKED on
        // a three-minute cooldown, twelve times over, while sheep grazed
        // outside the walls.
        //
        // A miner travels to ore and a fisher to water; a tamer travels to
        // ANIMALS -- not to sheep. The herds come from the world save
        // (data/revolution_tamables.tsv, every chardef with a TAMING
        // requirement), and the choice is Sphere's own arithmetic: nothing
        // above this character's skill, the gain window preferred, nearest
        // first, and never further than the clock allows
        // (include/uo/activities/tame.h).
        if (client.TravelBusy()) return false;
        LoadTamables(client.DataDir());
        std::vector<TameCluster> spots = Tamables();
        // Drop the one we are standing in -- it has just been scanned and read
        // empty -- and every herd this goal has already walked to today.
        for (usize i = 0; i < spots.size(); ) {
            const i32 r = spots[i].radius > 0 ? spots[i].radius : 8;
            bool drop = TileDist(obs.x, obs.y, spots[i].x, spots[i].y) <= r;
            for (usize v = 0; !drop && v < tameVisited_.size(); ++v)
                drop = tameVisited_[v].first == spots[i].x &&
                       tameVisited_[v].second == spots[i].y;
            if (drop) spots.erase(spots.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        }
        const i64 remainingMs =
            cfg_.sessionLimitMs - (obs.nowMs - sessionStartMs_);
        const i32 budgetTiles = TameTravelBudgetTiles(
            remainingMs, kWindDownBudgetMs, kTameWorkReserveMs);
        const int pick = ChooseTameCluster(spots, obs.x, obs.y, mySkill,
                                           budgetTiles);
        if (pick >= 0 && ++tameTrips_ <= kMaxTameTrips) {
            const TameCluster& p = spots[static_cast<usize>(pick)];
            LogLine("tame: nothing tamable here -- %d named mobile(s) in sight, "
                    "none tamable at Taming %.1f; walking out to %s at %d,%d "
                    "(needs Taming %.1f, %d tiles of %d affordable, %s, "
                    "trip %d)",
                    named, mySkill, p.label.c_str(), p.x, p.y, p.req,
                    TileDist(obs.x, obs.y, p.x, p.y), budgetTiles,
                    TameCanGain(p.req, mySkill) ? "can still gain"
                                                : "too easy to gain",
                    tameTrips_);
            tameVisited_.push_back(std::make_pair(p.x, p.y));
            travelInFlight_ = client.TravelToPoint(p.x, p.y,
                                                   p.radius > 0 ? p.radius : 8,
                                                   "herd");
            // Arriving somewhere new means the names must be asked for again.
            tameScanMs_ = 0;
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        LogLine("goal_failed=TAME_ANIMAL reason=\"nothing tamable after %d "
                "trips, %d named mobile(s) at the last one, %d herd(s) known "
                "and none within %d tiles at Taming %.1f\"",
                tameTrips_, named, static_cast<int>(spots.size()), budgetTiles,
                mySkill);
        tameVisited_.clear(); tameRefused_.clear();
        planner_.Cooldown(GoalKind::TameAnimal, obs.nowMs + kNoPetCooldownMs);
        planner_.Finish(false, "nothing tamable in reach", obs.nowMs);
        tameTrips_ = 0;
        tameScanMs_ = 0;
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

    // A TAME IS SEVERAL ATTEMPTS, NOT ONE. Skill_Taming rolls 2-5 strokes
    // (m_atTaming.m_dwStrokeCount, CCharSkill.cpp) and skill35_taming.scp's
    // @Fail is an ordinary outcome, so keep asking -- but bounded, so an
    // animal this character will never manage is given up on honestly.
    if (tameTarget_ != best) { tameTarget_ = best; tameAttempts_ = 0; }
    if (++tameAttempts_ > kMaxTameAttempts) {
        LogLine("goal_failed=TAME_ANIMAL reason=\"'%s' resisted %d attempts "
                "(needs Taming %.1f, have %.1f)\"", bestName.c_str(),
                tameAttempts_ - 1, bestReq, mySkill);
        planner_.Cooldown(GoalKind::TameAnimal, obs.nowMs + kNoPetCooldownMs);
        planner_.Finish(false, "the animal resisted", obs.nowMs);
        tameAttempts_ = 0; tameTarget_ = 0; tameAskedMs_ = 0; tameScanMs_ = 0;
        tameVisited_.clear(); tameRefused_.clear();
        return false;
    }
    tameTargetName_ = bestName;
    tameTargetReq_ = bestReq;
    LogLine("tame: trying '%s' (needs Taming %.1f, have %.1f, %s, attempt %d)",
            bestName.c_str(), bestReq, mySkill,
            TameCanGain(bestReq, mySkill) ? "gainful" : "too easy to gain",
            tameAttempts_);
    // Mark the journal BEFORE the attempt so the answer above reads only this
    // attempt's messages.
    tameAskedMs_ = client.JournalNowMs();
    client.ActionUseSkill(rules::kTaming, best);
    planner_.NoteAttempt(obs.nowMs);
    // DELAY=2.0 and taming usually takes several attempts.
    nextActionMs_ = obs.nowMs + 6000;
    return false;
}

// ---------------------------------------------------------------------------
// REST AND ROAM -- what a life does when nothing is pressing
// (include/uo/activities/rest.h, S2_WIRING_PLAN.md S2.2). DoExplore and
// DoIdle are both two-line forwarders into RestTick: DecideRest is the one
// place that turns "idle" into EXPLORE (go and learn something), REST (stand
// still and mean it), SETTLE (the session is ending, go somewhere safe) or
// STAGNANT (a fault to report, not a rest to take).
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
// So the EXPLORE step goes and looks at an unvisited shop, and reads the
// paperdolls of whoever is standing in it. That is how NearestMobileWithTrade
// and the supplier memory get anything to work with.
bool Runner::RestTick(Client& client, const Observation& obs, GoalKind owner) {
    RestSight see;
    see.sessionEnding = cfg_.sessionLimitMs > 0 &&
        (obs.nowMs - sessionStartMs_) >= cfg_.sessionLimitMs - kRestSettleLeadMs;
    // The same guarded-region read as MayWear's caller (Runner.cpp, around
    // inGuardedRegion) -- NOT flags.safe, which is the no-skill-gain flag, a
    // different fact. An open bank box counts too, matching wind-down's
    // safeHere.
    const wm::Region* here = client.CurrentRegion();
    see.somewhereSafe = (here && here->flags.guarded) || client.BankContainer() != 0;
    see.worthExploring = !exploredEverything_;
    // No direct regen signal exists on this shard; hunger stopping HP
    // regeneration is the fact include/uo/activities/heal.h is written
    // around, so "not hungry and not full" is the cheapest honest proxy.
    see.regenerating = !obs.hungry && obs.hp < obs.hpMax;
    see.hpFraction = obs.HpFraction();
    see.blockedForMs = obs.nowMs - lastRealErrandMs_;

    RestTuning tune;
    tune.restWhileBelowHp = needCfg_.healHpFraction;   // agrees with DecideHeal

    const RestPlan plan = DecideRest(see, tune);
    if (plan.step != lastRestPlan_) {
        LogPlan(RestStepName(plan.step), plan.reason);
        lastRestPlan_ = plan.step;
    }

    switch (plan.step) {
        case RestStep::Explore: {
            if (client.ActionBusy()) return false;

            // Arrived somewhere: LOOK. A place walked to and not looked at
            // teaches nothing, and the scan is the entire point of the errand.
            if (travelInFlight_ && !client.TravelBusy()) {
                travelInFlight_ = false;
                LogLine("explore: arrived at %d,%d -- reading who is here",
                        obs.x, obs.y);
                client.ActionScanMobiles();
                // RECORD IT BY ID, which is what TravelToUnexploredPlace
                // matches against. Storing an empty name would leave the
                // place forever unvisited and send the character back to it
                // on the next tick.
                state_.memory.NotePlace("explored", exploreTarget_.c_str(),
                                        obs.x, obs.y, obs.z, obs.nowMs);
                // AND REMEMBER THE NAME SEPARATELY, because the place record
                // cannot.
                //
                // NotePlace matches on kind AND position (Memory.cpp:34-40),
                // so two atlas entries that resolve to the SAME tile collapse
                // into one record -- and the later one OVERWRITES the name.
                // Minoc's cobbler and provisioner both sit on 2453,430, so
                // the single record's name flipped between them, `seen`
                // never contained both at once, and the pair was
                // re-nominated forever:
                //
                //   going to 'minoc_cobbler' -- somewhere new (15 place(s) known)
                //   arrived at 2453,430 -- reading who is here
                //   going to 'minoc_provisioner' -- somewhere new (15 place(s) known)
                //   arrived at 2453,430 -- reading who is here
                //
                // Eleven picks in a three-minute session, half of everything
                // the character did, and the place count never moved off 15.
                // Keeping the visited IDs here means an id is spent once
                // whatever tile it shares.
                if (!exploreTarget_.empty()) {
                    bool already = false;
                    for (const std::string& id : exploredIds_)
                        if (id == exploreTarget_) { already = true; break; }
                    if (!already) exploredIds_.push_back(exploreTarget_);
                }
                exploreTarget_.clear();
                // A newly-learned place may reveal more still unexplored;
                // only "nowhere new to go" below is allowed to latch this.
                exploredEverything_ = false;
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 3000;
                return true;   // one place per outing; the next tick re-decides
            }
            if (client.TravelBusy()) return false;

            // Somewhere with a service, that this character has not been to.
            // The places it already knows come from its own memory, so two
            // characters explore differently and a character never re-walks
            // its own ground.
            std::vector<std::string> seen;
            for (const KnownPlace& p : state_.memory.Places()) {
                if (!p.name.empty()) seen.push_back(p.name);
            }
            // Plus every id already walked to this session -- see the
            // arrival branch above for why the place records alone cannot
            // answer this.
            for (const std::string& id : exploredIds_) {
                bool dup = false;
                for (const std::string& s : seen) if (s == id) { dup = true; break; }
                if (!dup) seen.push_back(id);
            }
            // HOME, DELIBERATELY -- the one errand where HomeOrNearest's
            // "a shop is a shop wherever you stand" does not apply. Explore
            // with no fence walked Odessa Britain -> Cove -> Minoc, each hop
            // "the nearest unknown place" from wherever the last one left
            // her. A player idles around their own town.
            if (!client.TravelToUnexploredPlace(seen, &exploreTarget_,
                                                state_.homeCity.c_str())) {
                LogLine("explore: nowhere new to go (%s) -- standing down",
                        client.TravelFailureText());
                exploredEverything_ = true;
                return HandOff(GoalKind::Explore, GoalKind::IdleBriefly,
                               kExploredAllCooldownMs, "nowhere unexplored",
                               obs.nowMs);
            }
            travelInFlight_ = true;
            LogLine("explore: nothing else to do, so going to '%s' -- somewhere "
                    "new (%zu place(s) known so far)", exploreTarget_.c_str(),
                    seen.size());
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        case RestStep::Rest: {
            // A bounded no-op. It exists so a tick with nothing to do SAYS so
            // rather than spinning, and so the planner is never in a "no
            // goal" state.
            nextActionMs_ = obs.nowMs + 5000;
            return obs.nowMs - planner_.Current().startedAtMs > 15000;
        }
        case RestStep::Settle: {
            // Phase::WindDown owns the walk to safety and already refuses to
            // log out unsafe -- nothing new is built here.
            EndSession(plan.reason);
            return false;
        }
        case RestStep::Stagnant: {
            LogLine("goal_stagnant=%s reason=\"%s\"", GoalKindName(owner),
                    plan.reason);
            // The third Wander kind, cooled alongside the handoff so it does
            // not simply win the very next Select.
            planner_.Cooldown(GoalKind::TravelToRequiredPlace,
                              obs.nowMs + kStagnantCooldownMs);
            // The advisory `to` is only ever logged, never dispatched -- but
            // when the owner IS Explore, HandOff(Explore, Explore, ...)
            // still reads as a goal advising itself, which is nonsense on
            // its face. IdleBriefly is the honest advisory here: Explore
            // itself is what just went stagnant.
            const GoalKind to = owner == GoalKind::Explore
                                     ? GoalKind::IdleBriefly
                                     : GoalKind::Explore;
            return HandOff(owner, to, kStagnantCooldownMs, plan.reason,
                          obs.nowMs);
        }
    }
    return false;
}

bool Runner::DoExplore(Client& client, const Observation& obs) {
    return RestTick(client, obs, GoalKind::Explore);
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
        return HandOff(GoalKind::MakeBandages, GoalKind::EarnGold,
                       kNoBandageCooldownMs, "no scissors and no money",
                       obs.nowMs);
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
    //
    // WITH A BLADE, NOT THE SCISSORS. This used to hand the scissors to the
    // sheep, on the strength of the comment above citing CClientTarg.cpp:1878
    // -- but that line sits inside `case IT_WEAPON_SWORD / _AXE / _FENCE`
    // (:1866-1900), not inside `case IT_SCISSORS` (:2135), and a sheep is a
    // CHARACTER so the scissors case never even sees it. The gesture answered
    // "Scissors cannot be used on that to produce anything" every time. See
    // FindBlade for the whole citation.
    const u32 blade = FindBlade(client);
    const u32 sheep = client.NearestMobileWithBody(kSheepBody, 12);
    if (sheep && blade) {
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
        client.ActionUseItemOn(blade, sheep);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    if (sheep && !blade) {
        LogLine("goal_failed=MAKE_BANDAGES reason=\"a sheep is here but nothing "
                "bladed is carried -- scissors will not shear\"");
        planner_.Cooldown(GoalKind::MakeBandages,
                          obs.nowMs + kNoBandageCooldownMs);
        planner_.Finish(false, "no blade to shear with", obs.nowMs);
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
// MAKING CLOTH.
//
// Owner ruling, 2026-09-02: "buy cloth from players first ... otherwise GATHER
// IT: sheep -> shear (bladed item) -> wool -> spinning wheel -> yarn -> loom ->
// bolt of cloth -> scissors -> cloth. Never buy cloth/thread/yarn from NPCs."
//
// This is the same five gestures DoMakeBandages walks and it deliberately does
// NOT share its body: the two stop at different places (bandages vs cloth),
// they are damped by different needs, and MakeBandages carries a fighter's
// looted-clothing shortcut that a tailor must not take -- shredding a shirt it
// could have sold. What IS shared is the mechanics, and both cite the same
// engine lines.
//
// MEASURED CHAIN NUMBERS, from Source-X and re-read for this change
// (docs/M3_7_RESOURCE_ECONOMY.md section 7 agrees):
//
//   blade on a woolly sheep  -> 1 wool   CClientTarg.cpp:1880 (CREID_SHEEP),
//                                        reached from case IT_WEAPON_SWORD /
//                                        _AXE / _FENCE, NOT from IT_SCISSORS
//   wool on a spinning wheel -> 3 yarn   CClientTarg.cpp:2053 (case IT_WOOL)
//   yarn on a loom           -> 1 bolt   CClientTarg.cpp:2186; the loom holds
//                                        4 and takes them in ONE gesture --
//                                        ConsumeAmount(iNeed) at :2235 eats up
//                                        to four from the stack at once, so a
//                                        stack of 4 yarn is one click, not four
//   scissors on the bolt     -> 50 cloth CClientTarg.cpp:2147 ConvertBolttoCloth
//
// KNOWN BLOCKER, recorded and not worked around: every stock Tailoring recipe
// on this runtime reads `RESOURCES=<n> i_cloth,1 i_thread`
// (items/i_provisions_clothing.scp:47, :71, :93, ...), and THREAD comes from
// COTTON, not from wool -- one cotton spins to six thread
// (CClientTarg.cpp:2078). So this loop produces CLOTH and only cloth, which is
// what the recipes need most of and the only half a wool chain can supply.
// Sewing remains blocked on thread until a cotton source is proven. See
// artifacts/tailor_loop_2026-09-02.md.
//
// EVERY STEP IS MEASURED BY AN INVENTORY DELTA. Issuing a double-click is not
// the same as the server honouring it: the wheel and the loom answer with a
// SysMessage and no menu, so there is no confirmation packet to wait on, and
// claiming progress for the gesture is precisely how four goals in this project
// ended up spinning (goals-that-spin). Three gestures in a row that move
// nothing stand the goal down.
bool Runner::DoMakeCloth(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;
    LoadPastures(client.DataDir());

    const i32 wool  = static_cast<i32>(client.BackpackItemCount(kWoolGraphic));
    const i32 yarn  = static_cast<i32>(client.BackpackItemCount(kYarnGraphic));
    const i32 bolts = static_cast<i32>(client.BackpackItemCount(kClothBoltGraphic));
    const i32 cloth = static_cast<i32>(client.BackpackItemCount(kClothGraphic));

    // DID THE LAST GESTURE ACTUALLY DO ANYTHING?
    if (clothWoolBefore_ >= 0 &&
        obs.nowMs - clothMarkMs_ > kClothMarkStaleMs) {
        // The turn went elsewhere and came back. Judge nothing on numbers
        // this old; take a fresh gesture and measure that instead.
        clothWoolBefore_ = -1;
    }
    if (clothWoolBefore_ >= 0) {
        const bool moved = wool != clothWoolBefore_ || yarn != clothYarnBefore_ ||
                           bolts != clothBoltBefore_ || cloth != clothClothBefore_;
        if (moved) {
            LogLine("cloth: wool %d->%d yarn %d->%d bolts %d->%d cloth %d->%d",
                    clothWoolBefore_, wool, clothYarnBefore_, yarn,
                    clothBoltBefore_, bolts, clothClothBefore_, cloth);
            planner_.NoteProgress();
            clothEmptySteps_ = 0;
        } else if (++clothEmptySteps_ >= kMaxEmptyClothSteps) {
            LogLine("goal_failed=MAKE_CLOTH reason=\"%d gestures in a row moved "
                    "nothing (wool %d yarn %d bolts %d cloth %d)\"",
                    clothEmptySteps_, wool, yarn, bolts, cloth);
            clothEmptySteps_ = 0;
            clothWoolBefore_ = -1;
            planner_.Cooldown(GoalKind::MakeCloth, obs.nowMs + kNoClothCooldownMs);
            planner_.Finish(false, "the chain moved nothing", obs.nowMs);
            return false;
        }
        clothWoolBefore_ = -1;
    }

    // ENOUGH? The honest test is the one the need asked: is the batch this
    // life wants to make still short of cloth? Not a cloth count of our own
    // invention -- the recipe decides how much is enough, and it differs by
    // garment (a bandana takes 2, a cape 14).
    const prof::Profession* me = needCfg_.profession;
    if (me) {
        const CraftIntent intent =
            ChooseCraft(*me, obs, needCfg_.craftBatch, &craftFocus_);
        bool stillShort = false;
        for (const prod::Ingredient& ing : intent.missing) {
            if (IsWoolChainMaterial(ing.item)) { stillShort = true; break; }
        }
        if (!stillShort) {
            LogLine("cloth: %d cloth and %d bolts is enough for the batch",
                    cloth, bolts);
            clothTrips_ = 0;
            clothShornSheep_.clear();
            planner_.Finish(true, nullptr, obs.nowMs);
            return true;
        }
    }

    // 1. BOLT -> CLOTH. Runs first: it is the step that actually produces the
    //    thing the recipe wants, and 50 cloth per bolt is the whole yield of
    //    the chain up to here.
    const u32 scissors = client.FindBackpackItemByGraphic(kScissorsGraphic);
    if (bolts > 0 && scissors) {
        const u32 bolt = client.FindBackpackItemByGraphic(kClothBoltGraphic);
        LogLine("cloth: cutting a bolt into cloth (%d cloth so far)", cloth);
        client.ActionUseItemOn(scissors, bolt);
        clothWoolBefore_ = wool; clothYarnBefore_ = yarn;
        clothBoltBefore_ = bolts; clothClothBefore_ = cloth;
        clothMarkMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }
    if (bolts > 0 && !scissors) {
        // A bolt with nothing to cut it is a shopping errand, not a failure.
        // Scissors are ITEMNEWBIE in every starter kit and cheap at a tailor.
        if (obs.gold >= kScissorsMoney) {
            BuyScrollFrom(client, obs, "tailor", wm::Service::Tailor,
                          kScissorsGraphic, false, 1, "a pair of scissors",
                          GoalKind::MakeCloth);
            return false;
        }
        LogLine("goal_failed=MAKE_CLOTH reason=\"%d bolts and no scissors, and "
                "only %d gold to buy a pair with\"", bolts, obs.gold);
        return HandOff(GoalKind::MakeCloth, GoalKind::EarnGold,
                       kNoClothCooldownMs, "no scissors and no money",
                       obs.nowMs);
    }

    // 2. YARN -> LOOM -> BOLT. The loom takes up to four from the stack in one
    //    gesture and only yields a bolt when it has all four, so fewer than
    //    four is not worth walking to a loom for -- it would consume the yarn
    //    into the loom's own store and hand back nothing.
    if (yarn >= kYarnPerBolt) {
        const u32 loom = client.FindWorldItemByGraphic(kLoomGraphic, 10);
        if (!loom) {
            LogLine("cloth: %d yarn and no loom in sight -- going to the "
                    "tailor, where the looms are", yarn);
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToPlace(kTailorWorkshopPlace);
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToService(
                    wm::Service::Tailor, HomeOrNearest(state_.homeCity));
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        const u32 spun = client.FindBackpackItemByGraphic(kYarnGraphic);
        LogLine("cloth: weaving %d yarn at the loom", yarn);
        client.ActionUseItemOn(spun, loom);
        clothWoolBefore_ = wool; clothYarnBefore_ = yarn;
        clothBoltBefore_ = bolts; clothClothBefore_ = cloth;
        clothMarkMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // 3. WOOL -> WHEEL -> YARN. Three yarn per wool, so this runs until the
    //    wool is gone rather than until some yarn target is hit.
    if (wool > 0) {
        const u32 wheel = client.FindWorldItemByGraphic(kSpinWheelGraphic, 10);
        if (!wheel) {
            LogLine("cloth: %d wool and no spinning wheel in sight -- going to "
                    "the tailor", wool);
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToPlace(kTailorWorkshopPlace);
            if (!travelInFlight_)
                travelInFlight_ = client.TravelToService(
                    wm::Service::Tailor, HomeOrNearest(state_.homeCity));
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        const u32 raw = client.FindBackpackItemByGraphic(kWoolGraphic);
        LogLine("cloth: spinning wool into yarn (%d wool, %d yarn)", wool, yarn);
        client.ActionUseItemOn(raw, wheel);
        clothWoolBefore_ = wool; clothYarnBefore_ = yarn;
        clothBoltBefore_ = bolts; clothClothBefore_ = cloth;
        clothMarkMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // 4. A SHEEP -> WOOL. Free, and the start of everything.
    const u32 blade = FindBlade(client);
    if (!blade) {
        // Not a failure of the chain -- a missing tool, which is somebody
        // else's errand. Say which, so the log names the fix.
        LogLine("goal_failed=MAKE_CLOTH reason=\"nothing bladed is carried; a "
                "sheep is sheared with a weapon or a knife, never with "
                "scissors\"");
        planner_.Cooldown(GoalKind::MakeCloth, obs.nowMs + kNoClothCooldownMs);
        planner_.Finish(false, "no blade to shear with", obs.nowMs);
        return false;
    }

    const u32 sheep = client.NearestMobileWithBody(kSheepBody, 12);
    if (sheep) {
        // A SHEEP THIS CHARACTER HAS ALREADY SHEARED IS NOT A SHEEP.
        //
        // Sphere flips it to CREID_SHEEP_SHORN (0x00DF) and starts a 30-minute
        // regrow timer (CClientTarg.cpp:1886, WoolGrowthTime), and answers a
        // second attempt with "wait for the wool to grow back" (:1895). The
        // body change normally drops it out of NearestMobileWithBody on its
        // own; this list covers the tick before the update lands. Refusal
        // means MOVE TO THE NEXT SHEEP, not fail.
        bool shorn = false;
        for (u32 s : clothShornSheep_) { if (s == sheep) { shorn = true; break; } }
        if (shorn) {
            LogLine("cloth: the nearest sheep is one I already sheared -- "
                    "trying another pasture");
            clothShornSheep_.clear();
        } else {
            i32 sx = 0, sy = 0; i8 sz = 0;
            if (client.MobilePosition(sheep, &sx, &sy, &sz)) {
                const i32 d = TileDist(obs.x, obs.y, sx, sy);
                if (d > 1) {
                    LogLine("cloth: a sheep %d tiles away -- walking up to it", d);
                    travelInFlight_ = client.TravelToEntity(sheep, 1);
                    nextActionMs_ = obs.nowMs + 2000;
                    return false;
                }
            }
            LogLine("cloth: shearing a sheep (%d wool carried)", wool);
            clothTrips_ = 0;
            client.ActionUseItemOn(blade, sheep);
            clothShornSheep_.push_back(sheep);
            clothWoolBefore_ = wool; clothYarnBefore_ = yarn;
            clothBoltBefore_ = bolts; clothClothBefore_ = cloth;
            clothMarkMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + 3000;
            return false;
        }
    }

    // 5. NO SHEEP IN SIGHT. Go where the save says they are.
    if (client.TravelBusy()) return false;
    const std::vector<Pasture>& pastures = Pastures();
    if (pastures.empty()) {
        LogLine("goal_failed=MAKE_CLOTH reason=\"no pasture table -- run "
                "tools/pasturegen.py against the world save\"");
        planner_.Cooldown(GoalKind::MakeCloth, obs.nowMs + kNoClothCooldownMs);
        planner_.Finish(false, "no pasture data", obs.nowMs);
        return false;
    }
    if (++clothTrips_ > kMaxClothTrips) {
        LogLine("goal_failed=MAKE_CLOTH reason=\"no sheep found after %d trips "
                "to the pastures\"", clothTrips_ - 1);
        clothTrips_ = 0;
        planner_.Cooldown(GoalKind::MakeCloth, obs.nowMs + kNoClothCooldownMs);
        planner_.Finish(false, "no sheep reachable", obs.nowMs);
        return false;
    }
    const Pasture& p =
        pastures[static_cast<usize>(clothPastureIdx_) % pastures.size()];
    ++clothPastureIdx_;
    LogLine("cloth: no sheep in sight -- walking to the flock of %d at %d,%d "
            "(trip %d)", p.count, p.x, p.y, clothTrips_);
    travelInFlight_ = client.TravelToPoint(p.x, p.y, std::max(4, p.radius / 2),
                                           "pasture");
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
            // A SPELL WENT IN, so this street is not empty after all. Clear the
            // escalating rest and give the errand a fresh time budget: Selene
            // bought three scrolls from one scribe in ninety seconds
            // (g_Selene.console.txt:2849-3056) and must not be damped for it.
            scrollStandDowns_ = 0;
            scrollShopSinceMs_ = obs.nowMs;
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

    // THE ERRAND IS BOUNDED IN TIME, NOT ONLY IN TRIPS.
    //
    // Everything above this line is free: reading the book and putting a looted
    // scroll into it costs seconds and always achieves something. Everything
    // below is SHOPPING, and shopping on this shard means walking -- about a
    // minute per shop. Aurelius stayed under the three-trip budget for a whole
    // five-minute gate and still cast nothing, because three trips is four
    // minutes of travel (g_Aurelius.console.txt:136-847). A trip budget cannot
    // see that; a clock can.
    //
    // The clock only runs while the goal keeps getting the turn. A gap longer
    // than kScrollShopGapStaleMs means the planner took the turn away and gave
    // it back, which starts a fresh stretch rather than blowing the budget on
    // the first tick -- the same staleness rule the wool chain's mark uses.
    if (scrollShopSinceMs_ == 0 ||
        obs.nowMs - scrollShopTickMs_ > kScrollShopGapStaleMs) {
        scrollShopSinceMs_ = obs.nowMs;
    } else if (obs.nowMs - scrollShopSinceMs_ > kScrollShopBudgetMs) {
        const i64 spent = obs.nowMs - scrollShopSinceMs_;
        char why[160];
        std::snprintf(why, sizeof(why),
                      "%llds of shopping and not one spell added to the book",
                      static_cast<long long>(spent / 1000));
        StandDownFromScrollShopping(obs, why);
        planner_.Finish(false, "nobody selling scrolls", obs.nowMs);
        return false;
    }
    scrollShopTickMs_ = obs.nowMs;

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
    // WHICH spell is not decided here and is not written down anywhere in this
    // client. Owner ruling 2026-09-02: "for mage to cast there are lots of
    // skills, don't hard code Create Food." The candidate set is the shard's
    // whole Magery table (data/revolution_spells.tsv, exported from
    // runtime/scripts/spells/spells_magery.scp by tools/spellgen.py, 64 spells
    // over 8 circles); the choice is spell::ChoosePracticeSpell, which keeps
    // only what this book holds, what SKILLREQ allows at this Magery, what the
    // mana and the pack can pay for, and what is safe to aim at oneself --
    // then prefers the highest circle in the gain window and rotates within it.
    //
    // Deliberately not a combat spell. Practising Magery must not be a way to
    // start fights the life did not choose. Create Food stays in DoGetFood,
    // where it is an errand rather than an exercise.
    if (skillId == rules::kMagery) {
        // Create Food belongs to the FOOD goal. It is absent from this shard's
        // starter book, but that must not veto Magery practice when the book
        // holds another safe spell. Read this character's book and choose from
        // it below instead of carrying the food-goal refusal into practice.
        // The book must be opened once before it can be read; an unopened book
        // is not an empty one.
        if (obs.spellbookSerial == 0) {
            LogLine("practice: no spellbook carried -- Magery cannot be "
                    "practised until FILL_SPELLBOOK has bought one");
            planner_.Cooldown(GoalKind::PracticeSkill,
                              obs.nowMs + kNoSelfSafeSpellCooldownMs);
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
        // READ THE SERVER'S ANSWER TO THE LAST CAST BEFORE SENDING ANOTHER.
        //
        // "You lack Sulfurous Ash for this spell" is a HARD signal: it names a
        // reagent, and Sphere will answer identically until one is bought. The
        // whole 2026-09-02 defect is that nothing read it -- Elara sent 322
        // casts and was refused 322 times. A refused spell is struck off for
        // the session (a later restock un-strikes it, because the pack check
        // below is what actually gates the cast) and the goal re-plans at once.
        if (practiceCastSpell_ >= 0 && practiceCastMark_ != 0 &&
            client.JournalSaidSince("you lack", practiceCastMark_)) {
            const spell::SpellDef* d = spell::DefForSpell(practiceCastSpell_);
            const char* named = nullptr;
            if (d) {
                for (const char* r : d->reagents) {
                    if (!r) break;
                    const char* needle = spell::LackNeedleFor(r);
                    if (needle && client.JournalSaidSince(needle, practiceCastMark_)) {
                        named = r;
                        break;
                    }
                }
            }
            if (named) {
                // Not mana, not skill -- a reagent, by name. Believe the pack
                // is empty of it whatever the container stream last said.
                LogLine("practice: the server refused spell %d for want of %s "
                        "-- striking that spell off for this session and "
                        "re-planning", practiceCastSpell_, named);
                bool known = false;
                for (int s : practiceRefusedSpells_)
                    if (s == practiceCastSpell_) known = true;
                if (!known) practiceRefusedSpells_.push_back(practiceCastSpell_);
            }
            // "You lack sufficient mana" reaches here too and must NOT strike
            // the spell off: mana comes back on its own.
            practiceCastSpell_ = -1;
            practiceCastMark_ = 0;
        }

        const spell::PracticeChoice pick = PickPracticeSpell(client, obs);
        // OUT OF REAGENTS IS A SHOPPING LIST, NOT A DEAD END.
        //
        // The pack is short of what every spell in this book costs, so there is
        // nothing to cast and nothing FILL_SPELLBOOK can do about it. Hand the
        // list to BUY_SUPPLIES -- the mage shop that sells scrolls sells these
        // too (tm_vend.scp:633-656) -- and stand down so it gets a turn.
        if (pick.spell < 0 && !pick.missing.empty()) {
            const i64 remainingMs =
                cfg_.sessionLimitMs > 0
                    ? cfg_.sessionLimitMs - (obs.nowMs - sessionStartMs_)
                    : 0;
            // A REAGENT KEEPS, SO THE HORIZON IS A SITTING, NOT THE REMAINDER.
            //
            // Buying only for the minutes left in this session is what a bot
            // does, not a player: Aurelius, holding 9,330 gold, walked to the
            // mage Alenne and bought THREE of each at 3 gold
            // (run_gates/g_Aurelius.console.txt:626,635,717). Reagents do not
            // spoil and the walk is the expensive part, so the target is a
            // whole session's practice -- still a rate times a time, still
            // per character, and still capped by the purse below.
            const i64 horizonMs = remainingMs > cfg_.sessionLimitMs
                                      ? remainingMs
                                      : cfg_.sessionLimitMs;
            const i32 casts = spell::ExpectedPracticeCasts(
                practiceCasts_, obs.nowMs - sessionStartMs_, horizonMs,
                kPracticeCastPeriodMs);
            // The price this character has SEEN, not one we assume. Unknown is
            // allowed: the vendor errand reads the shelf and clamps there.
            i32 unit = 0;
            if (const market::PriceObservation* p = state_.prices.Latest(
                    pick.missing.front().c_str(),
                    market::PriceSource::NpcVendorSells))
                unit = p->pricePerUnit;
            const i32 shortest = market::QtyOf(obs.pack, pick.missing.front().c_str());
            const spell::ReagentPlan plan = spell::PlanReagentBuy(
                shortest, casts, unit, obs.gold,
                static_cast<int>(pick.missing.size()));
            reagentWants_ = pick.missing;
            reagentWantQty_ = plan.buy > 0 ? plan.buy : 1;
            std::string list;
            for (const std::string& r : reagentWants_)
                list += (list.empty() ? "" : ", ") + r;
            LogLine("practice: out of reagents for spell %d -- wants %d of each "
                    "of [%s] (a sitting is %d cast(s) at this character's own "
                    "rate, %s) and is standing down so BUY_SUPPLIES can go to "
                    "a mage",
                    pick.shortFor, reagentWantQty_, list.c_str(), casts,
                    plan.why);
            planner_.Cooldown(GoalKind::PracticeSkill,
                              obs.nowMs + kNoReagentCooldownMs);
            planner_.Finish(false, "no reagents for any castable spell",
                            obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        const int spell = pick.spell;
        if (spell < 0) {
            // AND STAND DOWN LONG ENOUGH FOR THE FIX TO HAPPEN.
            //
            // This is not a pacing failure, it is a PREREQUISITE failure: the
            // book has to gain a spell before practising can work, and the
            // goal that buys one is FILL_SPELLBOOK. Finishing without a
            // cooldown handed PRACTICE_SKILL straight back every five seconds
            // and FILL_SPELLBOOK only got a turn once the noop backstop had
            // counted to five -- Ilyandra ran that cycle for the whole session
            // with Magery frozen at 50.0
            // (run_r4/w_Ilyandra.console.txt:496-711).
            LogLine("practice: nothing safe to cast at myself is in this book "
                    "-- standing down so FILL_SPELLBOOK can buy one");
            planner_.Cooldown(GoalKind::PracticeSkill,
                              obs.nowMs + kNoSelfSafeSpellCooldownMs);
            planner_.Finish(false, "no self-safe spell in book", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        // At oneself. spell::SafeToPractiseOnSelf has already refused every
        // harmful, cursing, summoning, field and non-self-targeted spell, and
        // every spell carrying a flag this client cannot read, so this can
        // neither hurt the caster nor make a criminal of them.
        {
            const spell::SpellDef* d = spell::DefForSpell(spell);
            std::string cost;
            if (d) {
                for (const char* r : d->reagents) {
                    if (!r) break;
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s%s x%d",
                                  cost.empty() ? "" : ", ", r,
                                  static_cast<int>(market::QtyOf(obs.pack, r)));
                    cost += buf;
                }
            }
            LogLine("practice: casting %s (spell %d, circle %d, needs Magery "
                    "%.1f) at myself to raise Magery (%.1f, mana %d, pack "
                    "holds %s)",
                    d ? d->name : "?", spell, d ? d->circle : 0,
                    d ? d->minSkillTenths / 10.0 : 0.0, have / 10.0, obs.mana,
                    cost.empty() ? "no reagent cost" : cost.c_str());
        }
        practiceCastMark_ = client.JournalNowMs();
        practiceCastSpell_ = spell;
        ++practiceCasts_;
        {
            bool seen = false;
            for (std::pair<int, i32>& c : practiceCastCounts_)
                if (c.first == spell) { ++c.second; seen = true; }
            if (!seen) practiceCastCounts_.push_back({spell, 1});
        }
        client.ActionCastSpell(spell, client.PlayerSerial());
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + kPracticeCastPeriodMs;
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
    return RestTick(client, obs, GoalKind::IdleBriefly);
}

}  // namespace uo::life
