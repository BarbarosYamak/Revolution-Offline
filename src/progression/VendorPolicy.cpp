#include "uo/vendor_policy.h"

#include <algorithm>
#include <cstring>

namespace uo::econ {

namespace {

bool Same(const char* a, const char* b) {
    if (!a || !b) return false;
    return std::strcmp(a, b) == 0;
}

// ---------------------------------------------------------------------------
// THE MATRIX
//
// Every row is a grading from docs/REVOLUTION_VENDOR_MATRIX.md, which was built
// by extracting c_vendor_human.scp -> tm_vend.scp mechanically and then
// checking each item against the RevolutionUO archive.
//
// Only economically load-bearing items are listed. Anything absent grades as
// Unknown, which refuses -- so the table failing to mention something can never
// accidentally PERMIT it.
// ---------------------------------------------------------------------------
struct Row { const char* item; VendorClass klass; };

const Row kMatrix[] = {
    // --- the one dated NPC permission in the whole archive -------------------
    // 03.11.2010: "Pack horse ve pack llama artik animal trainer tezgahtarlari
    // tarafindan satilmaktadir." Ten days inside the profile window. An update
    // announcing that an NPC STARTED selling something is also evidence that
    // NPC stock lists were curated and narrow.
    {"i_pet_horse_pack",  VendorClass::RevolutionNpcVerified},
    {"i_pet_llama_pack",  VendorClass::RevolutionNpcVerified},
    // RIDING HORSES. Forum, 24.03.2011: an NPC-sold horse cost 800 gp
    // (docs/REVOLUTION_ECONOMY_FORUM_EVIDENCE.md:113). The runtime animal
    // trainer stocks these four at VALUE={450 500} (tm_vend.scp
    // VENDOR_S_TRAINER, i_char_icons.scp) -- cheaper than the era, a
    // recorded gap, not a refusal. Owner rule 2026-09-02: a character buys a
    // horse first and does the rest mounted.
    {"i_pet_horse_tan",      VendorClass::RevolutionNpcVerified},
    {"i_pet_horse_gray",     VendorClass::RevolutionNpcVerified},
    {"i_pet_horse_brown_lt", VendorClass::RevolutionNpcVerified},
    {"i_pet_horse_brown_dk", VendorClass::RevolutionNpcVerified},

    // --- named cooperative categories (08.11.2008, 19.12.2008) --------------
    // These are PLAYER goods by Revolution's own word. Ammunition is the
    // exception that proves the rule: the Bowcraft guide says bows are sold
    // "diger oyunculara ya da TEZGAHTARLARA" -- to players OR to NPC vendors --
    // so a player market and an NPC floor demonstrably coexisted. See §3.2 of
    // the matrix; blocking arrows would INVENT a restriction.
    {"i_cloth_bolt",      VendorClass::PlayerMarketGood},
    // Healing a character can actually use. See VendorClass::BasicHealing --
    // Corwyn was refused these four times in one session by our own policy
    // while standing at the healer that stocks them.
    {"i_potion_heal",     VendorClass::BasicHealing},
    {"i_bandage",         VendorClass::BasicHealing},

    {"i_ingot_iron",      VendorClass::PlayerMarketGood},
    // The other three metals a miner can actually smelt on this shard
    // (i_provisions_ore.scp). Same standing as iron: a smith's own output,
    // and a player-market good rather than something to dump on an NPC.
    {"i_ingot_copper",    VendorClass::PlayerMarketGood},
    {"i_ingot_gold",      VendorClass::PlayerMarketGood},
    {"i_ingot_silver",    VendorClass::PlayerMarketGood},
    {"i_arrow",           VendorClass::PlayerMarketGood},
    {"i_xbolt",           VendorClass::PlayerMarketGood},
    {"i_keg_potion",      VendorClass::PlayerMarketGood},
    {"i_spellbook",       VendorClass::PlayerMarketGood},
    {"i_spellbook_runebook", VendorClass::PlayerMarketGood},

    // --- basic craft tools: permitted, see the header for why ----------------
    // Note that several of these are ALSO PlayerCrafted. The tool grading wins,
    // because the alternative is a bootstrap deadlock: tinker tools need
    // Tinkering 35 and four ingots, ingots need a forge, and a forge needs a
    // smith hammer. Somebody has to be able to buy the first tool.
    {"i_shovel",        VendorClass::BasicCraftTool},   // REQSTR none; the STR-30 miner's pickaxe
    {"i_pickaxe",       VendorClass::BasicCraftTool},   // REQSTR 50
    {"i_hatchet",       VendorClass::BasicCraftTool},
    {"i_axe",           VendorClass::BasicCraftTool},
    {"i_scissors",      VendorClass::BasicCraftTool},
    {"i_sewing_kit",    VendorClass::BasicCraftTool},
    {"i_tongs",         VendorClass::BasicCraftTool},
    {"i_hammer_smith",  VendorClass::BasicCraftTool},
    {"i_tinker_tools",  VendorClass::BasicCraftTool},
    // Staples. See VendorClass::BasicFood -- a bot with hunger enabled must be
    // able to eat, and Revolution's own cooking economy shows food was not an
    // income good.
    {"i_bread_loaf",    VendorClass::BasicFood},
    {"i_food_bread_fr", VendorClass::BasicFood},
    {"i_ribs_cooked",   VendorClass::BasicFood},
    {"i_food_cookies",  VendorClass::BasicFood},
    {"i_saw",           VendorClass::BasicCraftTool},
    {"i_mortar_pestle", VendorClass::BasicCraftTool},
    {"i_fishing_pole",  VendorClass::BasicCraftTool},
    {"i_pen_and_ink",   VendorClass::BasicCraftTool},
    // THE COOKING TOOL. i_fish_cut_cooked carries SKILLMAKE=Cooking 0.0,
    // t_cooking -- no t_cooking item in the pack, no cooking, full stop. The
    // three t_cooking itemdefs are i_fry_pan (Tinkering 30.0 + 4 ingots),
    // i_flour_sifter (Tinkering 50.0 + 3 ingots) and i_rolling_pin
    // (Tinkering 0.0 + 1 log) -- every one of them behind a craft a fisher
    // does not have, which is precisely the bootstrap deadlock this class
    // exists to break. Stock Sphere agrees it is shop stock: the pristine
    // Scripts-X tm_vend.scp sells SELL=i_rolling_pin,{1 6} at the BAKER, a
    // row the TNS shop-list swap dropped and which the runtime file now
    // restores. It is a tool in the strict sense too: never consumed,
    // shortcuts no chain -- the fish still has to be caught, cut and cooked.
    {"i_rolling_pin",   VendorClass::BasicCraftTool},

    // KINDLING. Not a resource and not a shortcut: it is how a fire -- the
    // station cooking is hardcoded to demand (Source-X CCharSkill.cpp
    // Skill_Cooking: IT_FIRE / IT_FORGE / IT_CAMPFIRE within RANGE) -- exists
    // anywhere a kitchen does not. This shard's own runtime vendor tables,
    // TNS's swapped-in lists, stock it for sale: VENDOR_S_PROVISIONER carries
    // SELL=i_kindling,{5 38} (tm_vend.scp:1319), and a live run watched the
    // provisioner's window offer it. Refusing it as Unknown was the single
    // link that kept a fisher selling raw steaks at 2gp under a cook paying 5
    // for cooked ones: every path to the cooked recipe died at "kindling: no
    // Revolution evidence either way" while the provisioner stood there
    // selling it.
    {"i_kindling",      VendorClass::RevolutionNpcVerified},

    // --- world-gathered: the shortcuts M3.7 exists to close ------------------
    {"i_ore_iron",        VendorClass::WorldGathered},
    {"i_log",             VendorClass::WorldGathered},
    {"i_wool",            VendorClass::WorldGathered},
    {"i_cotton",          VendorClass::WorldGathered},
    {"i_flax_bundle",     VendorClass::WorldGathered},
    {"i_hide",            VendorClass::WorldGathered},
    {"i_feather",         VendorClass::WorldGathered},
    {"i_fish_big_1",      VendorClass::WorldGathered},
    {"i_fish_big_2",      VendorClass::WorldGathered},
    {"i_fish_big_3",      VendorClass::WorldGathered},
    {"i_fish_big_4",      VendorClass::WorldGathered},
    {"i_fish_small",      VendorClass::WorldGathered},
    {"i_fish_cut_cooked", VendorClass::WorldProcessed},

    // --- world-processed: a station did this, not a shopkeeper ---------------
    {"i_yarn_ball",       VendorClass::WorldProcessed},
    {"i_thread",          VendorClass::WorldProcessed},
    {"i_cloth",           VendorClass::WorldProcessed},
    {"i_hides_cut",       VendorClass::WorldProcessed},
    {"i_fish_cut_raw",    VendorClass::WorldProcessed},

    // --- player-crafted ------------------------------------------------------
    {"i_board",           VendorClass::PlayerCrafted},
    {"i_gears",           VendorClass::PlayerCrafted},
    {"i_nails",           VendorClass::PlayerCrafted},
    {"i_lockpick",        VendorClass::PlayerCrafted},
    {"i_dagger",          VendorClass::PlayerCrafted},
    {"i_sash",            VendorClass::PlayerCrafted},
    {"i_robe",            VendorClass::PlayerCrafted},
    {"i_leather_tunic",   VendorClass::PlayerCrafted},
    {"i_parchment",       VendorClass::PlayerCrafted},
    // Shields, the whole smithing menu of them (def_blacksmithing.scp:156-164).
    // Finished goods, exactly like i_dagger above -- which a blacksmith buys
    // at 18 gold without argument -- and not materials, so the "materials
    // never go to an NPC" rule does not reach them. The graphic table below
    // has to carry every one of these or the pack cannot see them at all.
    {"i_shield_buckler",     VendorClass::PlayerCrafted},
    {"i_shield_round_bronze",VendorClass::PlayerCrafted},
    {"i_shield_round_metal", VendorClass::PlayerCrafted},
    {"i_shield_heater",      VendorClass::PlayerCrafted},
    {"i_shield_kite_metal",  VendorClass::PlayerCrafted},
    {"i_shield_kite_wood",   VendorClass::PlayerCrafted},
    {"i_shield_wood",        VendorClass::PlayerCrafted},
    {"i_shield_chaos",       VendorClass::PlayerCrafted},
    {"i_shield_order",       VendorClass::PlayerCrafted},
    {"i_shield_scale",       VendorClass::PlayerCrafted},

    // --- stock Sphere only ---------------------------------------------------
    // Revolution players MARKED runes; that a stock vendor sells blanks is a
    // Sphere fact, not a Revolution one.
    {"i_rune_marker",     VendorClass::StockSphereOnly},

    // --- era conflicts: Necromancy reagents on a Renaissance mage shop -------
    // Necromancy is skill 49. This client ships skills 0-48 and cannot display
    // it. Eighteen of these sit in VENDOR_S_MAGE_SHOP today.
    // ---- the eight ordinary Magery reagents ---------------------------------
    //
    // M3.8: RESOLVED, and by the only source that could resolve it.
    //
    // M3.7 searched revolutionuo.net's guides, the full 1200-entry changelog and
    // forum topics 59111 and 54978, and deliberately recorded UNKNOWN. The
    // evidence proved a real reagent ECONOMY -- a dedicated Reagent Crystal
    // (07.11.2008), Recall cut from 3 reagents to 1 (14.05.2009), Gate Travel at
    // 6 each -- but not where the reagents came from. Two readings survived:
    // mage shops stocked them, or reagent fields were gathered. Forum SEARCH
    // required a login and was unavailable.
    //
    // The project owner played RevolutionUO and states directly that MAGE SHOPS
    // AND ALCHEMISTS SOLD REAGENTS. First-hand testimony is the strongest source
    // available here, and it reaches exactly what the archive could not.
    //
    // This is the single most consequential UNKNOWN in the matrix closing:
    // Magery training consumes reagents continuously, and with ReagentsRequired
    // restored to 1 (M3.8 Phase 6) a Mage that may not buy reagents cannot
    // train at all. Consumption and sourcing are now both answered, so the
    // Mage/Warlock archetype is no longer blocked.
    //
    // NOTE WHAT IS *NOT* COVERED: the eighteen Necromancy reagents above stay
    // EraConflict. Necromancy is skill 49 and this client ships 0-48; a mage
    // shop selling batwing and daemon bone in 2010 is stock Sphere leaking a
    // later era, and owner testimony about ordinary regs says nothing about it.
    {"i_reag_black_pearl",      VendorClass::RevolutionNpcVerified},
    {"i_reag_blood_moss",       VendorClass::RevolutionNpcVerified},
    {"i_reag_garlic",           VendorClass::RevolutionNpcVerified},
    {"i_reag_ginseng",          VendorClass::RevolutionNpcVerified},
    {"i_reag_mandrake_root",    VendorClass::RevolutionNpcVerified},
    {"i_reag_nightshade",       VendorClass::RevolutionNpcVerified},
    {"i_reag_spider_silk",      VendorClass::RevolutionNpcVerified},
    {"i_reag_sulfur_ash",       VendorClass::RevolutionNpcVerified},

    {"i_reag_batwing",          VendorClass::EraConflict},
    {"i_reag_blackmoor",        VendorClass::EraConflict},
    {"i_reag_blood_spawn",      VendorClass::EraConflict},
    {"i_reag_blood_vial",       VendorClass::EraConflict},
    {"i_reag_bone",             VendorClass::EraConflict},
    {"i_reag_brimstone",        VendorClass::EraConflict},
    {"i_reag_daemon_bone",      VendorClass::EraConflict},
    {"i_reag_fertile_dirt",     VendorClass::EraConflict},
    {"i_reag_dragon_blood",     VendorClass::EraConflict},
    {"i_reag_executioners_cap", VendorClass::EraConflict},
    {"i_reag_eye_of_newt",      VendorClass::EraConflict},
    {"i_reag_obsidian",         VendorClass::EraConflict},
    {"i_reag_pig_iron",         VendorClass::EraConflict},
    {"i_reag_pumice",           VendorClass::EraConflict},
    {"i_reag_nox_crystal",      VendorClass::EraConflict},
    {"i_reag_grave_dust",       VendorClass::EraConflict},
    {"i_reag_dead_wood",        VendorClass::EraConflict},
    {"i_reag_wyrm_heart",       VendorClass::EraConflict},

    // --- the eight Magery reagents: RESOLVED, and the dead rows removed -----
    //
    // This block used to repeat all eight reagents as Unknown, below the
    // RevolutionNpcVerified rows above. ClassifyForVendor returns the FIRST
    // match, so those rows never once decided anything -- the table said
    // "the biggest UNKNOWN in the audit" while the code had already answered
    // it. A duplicate that cannot win is not a record of doubt, it is a lie
    // about what the program does, so it is gone rather than kept for
    // conscience. The audit note itself is preserved above.
    //
    // i_scroll_blank: the mage shop SELLS them -- VENDOR_S_MAGE_SHOP,
    // templates/tm_vend.scp:633 `SELL=i_scroll_blank,{10 15}`, and the same
    // template BUYS them back at :671 -- and the shard owner confirmed it
    // directly (2026-08-28). It is the one input the whole Inscription chain
    // starts from, so an unresolved Unknown here made every scribe in the
    // catalogue unable to earn a copper.
    {"i_scroll_blank",       VendorClass::RevolutionNpcVerified},
    // Written scrolls: the mage shop BUYS these back by circle
    // (VENDOR_B_MAGE_SHOP, templates/tm_vend.scp:666-669, and poison is in
    // random_third_circle -- templates/tm_magic.scp:29). Selling them is the
    // scribe's income, so the class has to say so.
    {"i_scroll_poison",      VendorClass::RevolutionNpcVerified},
    {"i_scroll_recall",      VendorClass::RevolutionNpcVerified},
    // EMPTY BOTTLES. This said "nothing has been produced for it, and the
    // alchemy chain is genuinely blocked until something is". That was wrong,
    // and it was wrong in the most instructive way available: the shard writes
    // the SELL row in a different case from the BUY row --
    //     tm_vend.scp:597   SELL=i_BOTTLE_EMPTY,250        (VENDOR_S_ALCHEMIST)
    //     tm_vend.scp:1320  SELL=i_BOTTLE_EMPTY,{5 38}     (VENDOR_S_PROVISIONER)
    //     tm_vend.scp:606   BUY=i_bottle_empty,{3 18}
    // so a case-sensitive search for the lowercase defname finds only the BUY
    // rows and reports that nobody sells them. Sphere scripts are
    // case-insensitive; our greps have to be too.
    //
    // Runner.cpp already maps i_bottle_empty -> "alchemist" as a supplier
    // route, so the whole path was wired and only this grading refused it. An
    // alchemist starts with four bottles from the newbie kit and needs one per
    // potion; without a purchasable bottle the chain stops at four.
    {"i_bottle_empty",       VendorClass::RevolutionNpcVerified},

    // BANDAGES. The same class of gap as i_kindling: absent from this table,
    // so ClassifyForVendor fell through to Unknown and DoReplaceEquipment
    // (life/Runner.cpp) permanently refused to buy them regardless of gold --
    // "no supplier and the vendor policy grades a bandage UNKNOWN" -- while
    // the runtime's own vendor stock sells them outright: VENDOR_S_HEALER_SHOP
    // (runtime/scripts/templates/tm_vend.scp:1072, SELL=i_bandage,{5 20}) and
    // VENDOR_S_VET (:509, SELL=i_bandage,{6 66}), both live/uncommented rows,
    // not a TNS-swapped or stock-Sphere-only leftover. life/Needs.cpp already
    // documents this fact ("Bandages ARE sold here (VENDOR_S_HEALER_SHOP,
    // VENDOR_S_VET)") and the whole buy path in DoReplaceEquipment -- travel
    // to a healer, open the vendor, match the graphic, buy -- was already
    // wired and waiting; only this table's silence was stopping it. A
    // Sphere-era warrior economy that cannot buy its own bandages cannot
    // fight, which is exactly the deadlock that cost Kaelen the session
    // (2026-08-29/30 live runs: bandages=0, no scissors, no sheep found,
    // hp regen blocked by hunger -- every exit locked behind another).
    {"i_bandage",            VendorClass::RevolutionNpcVerified},
};

// Graphic -> defname, for the live buy path. A vendor offer on the wire carries
// a graphic, never a defname, so the policy has to be reachable from one. Only
// the audited, economically load-bearing items are here; anything else falls
// through to Unknown and is refused, which is the same fail-safe as an unlisted
// defname. Ids read from the runtime's own ITEMDEFs.
struct GraphicRow { u16 graphic; const char* item; };

const GraphicRow kGraphics[] = {
    {0x0DF8, "i_wool"},          {0x101F, "i_wool"},
    {0x0E1D, "i_yarn_ball"},     {0x0E1E, "i_yarn_ball"},  {0x0E1F, "i_yarn_ball"},
    {0x0FA0, "i_thread"},        {0x0FA1, "i_thread"},
    {0x175D, "i_cloth"},
    {0x0F95, "i_cloth_bolt"},    {0x0F96, "i_cloth_bolt"}, {0x0F97, "i_cloth_bolt"},
    {0x0DF9, "i_cotton"},        {0x0DEF, "i_cotton"},
    {0x1A9C, "i_flax_bundle"},   {0x1A9D, "i_flax_bundle"},
    // Riding-horse figurines (i_char_icons.scp). 0x211F is i_pet_horse_gray_2,
    // the same animal under a second icon.
    {0x259E, "i_pet_horse_tan"},      {0x2599, "i_pet_horse_gray"},
    {0x211F, "i_pet_horse_gray"},     {0x2120, "i_pet_horse_brown_lt"},
    {0x2121, "i_pet_horse_brown_dk"},
    // ORE IS ONE GRAPHIC FOR EVERY METAL. i_ore_copper, i_ore_gold,
    // i_ore_silver, i_ore_shadow, i_ore_agapite, i_ore_verite, i_ore_valorite
    // and the rest are all `ID=i_ore_iron` in items/i_provisions_ore.scp and
    // differ ONLY by COLOR (color_o_copper, color_o_gold, ...). So a graphic
    // table can never tell them apart, and this row is honestly iron-or-
    // unknown rather than iron. Telling special ore from iron needs the HUE,
    // which the pack view does not carry yet -- recorded as the open work
    // rather than papered over with a wrong row.
    {0x19B7, "i_ore_iron"},      {0x19B8, "i_ore_iron"},   {0x19B9, "i_ore_iron"},
    {0x19BA, "i_ore_iron"},      // DUPEITEM=019B7, and it was missing

    // INGOTS, THOUGH, ARE DISTINCT PER METAL -- each has its own ITEMDEF and
    // its own DUPELIST of five flips (i_provisions_ore.scp:220-351). Only
    // iron was here, and only three of its six graphics, so a smith carrying
    // copper or silver could not see it owned anything at all: obs.pack is
    // keyed by defname, and a graphic with no row produces no pack line, no
    // Surplus offer, and no bank deposit.
    //
    // What each ore smelts into is the ore's TDATA1, so this list is exactly
    // what a miner's smelting produces.
    {0x1BEF, "i_ingot_iron"},    {0x1BF0, "i_ingot_iron"}, {0x1BF1, "i_ingot_iron"},
    {0x1BF2, "i_ingot_iron"},    {0x1BF3, "i_ingot_iron"}, {0x1BF4, "i_ingot_iron"},
    {0x1BE3, "i_ingot_copper"},  {0x1BE4, "i_ingot_copper"},
    {0x1BE5, "i_ingot_copper"},  {0x1BE6, "i_ingot_copper"},
    {0x1BE7, "i_ingot_copper"},  {0x1BE8, "i_ingot_copper"},
    {0x1BE9, "i_ingot_gold"},    {0x1BEA, "i_ingot_gold"},
    {0x1BEB, "i_ingot_gold"},    {0x1BEC, "i_ingot_gold"},
    {0x1BED, "i_ingot_gold"},    {0x1BEE, "i_ingot_gold"},
    {0x1BF5, "i_ingot_silver"},  {0x1BF6, "i_ingot_silver"},
    {0x1BF7, "i_ingot_silver"},  {0x1BF8, "i_ingot_silver"},
    {0x1BF9, "i_ingot_silver"},  {0x1BFA, "i_ingot_silver"},
    {0x1BDD, "i_log"},
    {0x1BD7, "i_board"},
    {0x1078, "i_hide"},
    {0x1067, "i_hides_cut"},
    {0x0E34, "i_scroll_blank"},
    // The scribe's own output. Without these a crafted scroll is invisible
    // exactly the way three kinds of fish were: the pack counter reads these
    // names, so the craft goal could not tell that it had made anything and
    // the sell path could not find what it had made.
    // Ids: items/i_magic_magery.scp:468 and :653.
    {0x1F40, "i_scroll_poison"},
    {0x1F4C, "i_scroll_recall"},
    {0x0F0E, "i_bottle_empty"},
    {0x1BD1, "i_feather"},
    {0x0F3F, "i_arrow"},
    {0x1BFB, "i_xbolt"},
    {0x102E, "i_nails"},
    {0x1053, "i_gears"},
    {0x14FB, "i_lockpick"},
    {0x0F51, "i_dagger"},        {0x0F52, "i_dagger"},
    // THE SMITH'S OTHER TWO WEAPONS. i_dagger was the only one here, and the
    // smith makes three: data/revolution_recipes.tsv:17-19 and
    // life/Professions.cpp:361 both list i_spear_short and i_cutlass beside
    // it. obs.pack is keyed by defname and is built from graphics, so with no
    // row a finished cutlass is INVISIBLE to the pack view -- DoCraft's
    // verdict is a pack delta, so every cutlass a smith ever hammered read
    // back as NoProgress, and the sell path could not find what it had made.
    // The same silence i_shield_* and the crafted scrolls were fixed for.
    //
    // Ids from the runtime's own ITEMDEFs, DUPELIST included (a weapon flips
    // graphic when it is turned over):
    //   items/weapons/i_weapons.scp:1022 [ITEMDEF 01402] DEFNAME=i_spear_short
    //                                :1041 DUPELIST=01403
    //   items/weapons/i_weapons.scp:1221 [ITEMDEF 01440] DEFNAME=i_cutlass
    //                                :1238 DUPELIST=01441
    // economy/Market.cpp:835 already carries the same cutlass pair.
    //
    // NOTE this deliberately adds NO kMatrix row for either: economy/
    // Faucets.cpp:106 records that no Revolution evidence establishes NPC
    // buyback for i_spear_short, so both stay ungraded and therefore refused
    // for NPC sale. This row only lets the pack SEE them.
    {0x1402, "i_spear_short"},   {0x1403, "i_spear_short"},
    {0x1440, "i_cutlass"},       {0x1441, "i_cutlass"},
    // THE SPELLBOOK. kMatrix already grades i_spellbook PlayerMarketGood,
    // which is ALLOWED -- but a vendor offer arrives on the wire as a GRAPHIC,
    // and with no row here ItemNameForGraphic(0x0EFA) returned null and
    // CanUseNPCVendorForGraphic fell through to Unknown and refused:
    //   [policy] REFUSED NPC purchase of unmapped item (0x0EFA): graphic is
    //   not in the audited vendor matrix ... [UNKNOWN]
    // 32 times in one run. A grading is only reachable if the graphic that
    // names it is here too -- the two tables have to agree.
    {0x0EFA, "i_spellbook"},

    // SHIELDS. Not one of them was in this table, and a table is where the
    // pack view comes from: obs.pack is keyed by defname, so a shield with no
    // row here is a shield the character cannot see it owns. Corwyn carried
    // SIX heater shields for three sessions while market::Surplus reported
    // nothing spare, EARN_GOLD never scored on them, and a blacksmith stood in
    // front of him offering 61 gold each (v4_Corwyn, 2026-08-30 15:36).
    //
    // Every one of these is on the smithing menu -- def_blacksmithing.scp
    // lines 156-164 -- so they are exactly what a smith makes to sell.
    //
    // BOTH GRAPHICS OF EACH FLIP PAIR. Shields carry FLIP=1 and the second
    // ITEMDEF is a DUPEITEM of the first (items/i_provisions_shields.scp), so
    // the same shield reaches the pack under either id depending on how it
    // was laid down. Mapping only the named one loses half of them.
    {0x1B72, "i_shield_round_bronze"},
    {0x1B73, "i_shield_buckler"},
    {0x1B74, "i_shield_kite_metal"},
    {0x1B75, "i_shield_kite_metal"},    // DUPEITEM=01b74
    {0x1B76, "i_shield_heater"},        // VALUE=72, so 61 gp at a vendor
    {0x1B77, "i_shield_heater"},        // DUPEITEM=01b76
    {0x1B78, "i_shield_kite_wood"},
    {0x1B79, "i_shield_kite_wood"},     // DUPEITEM=01b78
    {0x1B7A, "i_shield_wood"},
    {0x1B7B, "i_shield_round_metal"},
    {0x1BC3, "i_shield_chaos"},
    {0x1BC4, "i_shield_order"},
    {0x1BC5, "i_shield_order"},         // DUPEITEM=01bc4
    {0x1BC6, "i_shield_scale"},
    {0x1BC7, "i_shield_scale"},         // DUPEITEM=01bc6

    // POTIONS. None of these were here at all, so GraphicsForItem returned
    // nothing for them and market::QtyOf could never count one: Voris brewed
    // Poison after Poison ("System: You put the Poison in your pack.") while
    // the bot's own pack view stayed at zero, so the craft never registered as
    // done, the batch command never fired, and a sale could never match.
    //
    // NOTE ALL FOUR POISONS SHARE ONE GRAPHIC. i_potion_Poison, PoisonLess,
    // PoisonGreat and PoisonDeadly all carry ID=i_bottle_green (0f0a) in
    // i_provisions_potions.scp -- the tiers are indistinguishable on the wire
    // and only the server knows which is which. i_potion_poison is listed
    // first so the graphic->name lookup answers with the common one.
    // ONE NAME PER GRAPHIC, and it must be the tier that has a BUYER.
    //
    // Mapping every tier to its shared graphic looked harmless and was not.
    // GraphicsForItem is used to COUNT the pack, so all four poisons reported
    // the same green bottles -- and because `produces` is ordered
    // strongest-first for the skill ladder, i_potion_poisondeadly claimed
    // them:
    //   BLOCKED_NEED EARN_GOLD: 4 x i_potion_poisondeadly spare, and no
    //   buyer known -- on this shard it is a player-market good
    // The character was holding plain Poison, which DOES have a buyer. Two
    // changes that were each right on their own combined into a life that
    // could not sell what it had just brewed.
    //
    // The tiers are indistinguishable on the wire -- one ID, no hue -- so
    // naming them all by the common, sellable defname is the honest reading.
    // The ladder is unaffected because ChooseCraft gates on SKILL, not on the
    // pack: at Alchemy 50 the 90.1 and 55.1 rungs are skipped and plain
    // Poison is chosen.
    {0x0F0A, "i_potion_poison"},
    {0x0F0C, "i_potion_heal"},
    {0x0F07, "i_potion_cure"},
    {0x0F0B, "i_potion_refresh"},
    {0x0F08, "i_potion_agilitygreat"},
    {0x1541, "i_sash"},
    {0x1F03, "i_robe"},          {0x1F04, "i_robe"},
    {0x1F14, "i_rune_marker"},
    // ALL FOUR big fish, plus the small one and the cooked steak. Only
    // 0x09CC was listed, so a character that caught a fish of any other kind
    // could not SEE it: the pack counter reads these names, Surplus() reads
    // the pack, and the sell path reads Surplus. A live fisher pulled fish out
    // of the sea and its own economy layer reported an empty hold.
    // Ids from items/i_profession_cook_barkeep_baker.scp:703-740, 102-114
    // and :1087.
    {0x09CC, "i_fish_big_1"},
    {0x09CD, "i_fish_big_2"},
    {0x09CE, "i_fish_big_3"},
    {0x09CF, "i_fish_big_4"},
    {0x0DD6, "i_fish_small"},
    {0x097A, "i_fish_cut_raw"},
    {0x097B, "i_fish_cut_cooked"},
    // the eight Magery reagents -- UNKNOWN, and therefore refused
    {0x0F7A, "i_reag_black_pearl"},   {0x0F7B, "i_reag_blood_moss"},
    {0x0F84, "i_reag_garlic"},        {0x0F85, "i_reag_ginseng"},
    {0x0F86, "i_reag_mandrake_root"}, {0x0F88, "i_reag_nightshade"},
    {0x0F8D, "i_reag_spider_silk"},   {0x0F8C, "i_reag_sulfur_ash"},
    // basic craft tools -- permitted
    {0x0F39, "i_shovel"},        {0x0F3A, "i_shovel"},
    {0x0E85, "i_pickaxe"},       {0x0E86, "i_pickaxe"},
    {0x0F43, "i_hatchet"},       {0x0F44, "i_hatchet"},
    {0x0F49, "i_axe"},           {0x0F4A, "i_axe"},
    {0x0F9E, "i_scissors"},      {0x0F9F, "i_scissors"},
    {0x0F9D, "i_sewing_kit"},
    {0x0FBB, "i_tongs"},         {0x0FBC, "i_tongs"},
    {0x13E3, "i_hammer_smith"},  {0x13E4, "i_hammer_smith"},
    {0x1EBC, "i_tinker_tools"},
    {0x103B, "i_bread_loaf"},
    {0x0F7A, "i_reag_black_pearl"},
    {0x0F7B, "i_reag_blood_moss"},
    {0x0F84, "i_reag_garlic"},
    {0x0F85, "i_reag_ginseng"},
    {0x0F86, "i_reag_mandrake_root"},
    {0x0F88, "i_reag_nightshade"},
    {0x0F8D, "i_reag_spider_silk"},
    {0x0F8C, "i_reag_sulfur_ash"},
    {0x09EB, "i_food_bread_fr"},
    {0x09F2, "i_ribs_cooked"},
    {0x1034, "i_saw"},           {0x1035, "i_saw"},
    {0x0E9B, "i_mortar_pestle"},
    {0x0DBF, "i_fishing_pole"},  {0x0DC0, "i_fishing_pole"},
    {0x0FBF, "i_pen_and_ink"},   {0x0FC0, "i_pen_and_ink"},
    // The cooking chain: kindling (0x0DE1, DUPELIST 0x0DE2) and the
    // rolling pin (0x1043), ids from i_provisions_misc.scp:121 and
    // i_profession_cook_barkeep_baker.scp:1265. Without these rows the pack
    // counter could not SEE either one -- bought kindling would read as zero
    // and the supplies goal would shop for it forever.
    {0x0DE1, "i_kindling"},      {0x0DE2, "i_kindling"},
    {0x1043, "i_rolling_pin"},
    // Bandages: [ITEMDEF 0e21] i_profession.scp:158, DUPELIST 0x0EE9. Without
    // this row CanUseNPCVendorForGraphic(kBandage) (life/Runner.cpp) could
    // never resolve a defname for the offer on the wire and fell through to
    // Unknown no matter what the matrix above said.
    {0x0E21, "i_bandage"},       {0x0EE9, "i_bandage"},
};

// HUE-AWARE METAL (S1, docs/CRAFTER_RUN_2026_08_30.md #20).
//
// The comment above kGraphics's ore rows explains the gap this closes: ore is
// ONE graphic for every metal -- i_ore_copper, i_ore_gold, i_ore_shadow and
// the rest are all `ID=i_ore_iron` in items/i_provisions_ore.scp, told apart
// ONLY by an `ON=@Create COLOR=color_o_*` line. The iron INGOT graphic is the
// same story for the twelve metals that never got their own ITEMDEF -- shadow,
// agapite, verite, rose, mytheril, blackrock, bloodrock, valorite, bronze,
// rusty, old_copper, dull_copper all carry `ID=i_ingot_iron` plus a COLOR.
// Copper, gold and silver ingots are NOT in that family: they have their own
// ITEMDEFs (01be3/01be9/01bf5) and no COLOR line, so their own graphic already
// says which metal it is and hue never needs consulting -- ItemNameForGraphic
// alone is already correct for them, which is why they are absent below.
//
// The hue constants are read from i_provisions_ore.scp's own
// [DEFNAME COLOR_ORE] block (color_o_iron 0, color_o_valorite 0515, ...).
struct HueRow { u16 hue; const char* item; };

// Ore: the pile-size graphics (019b7 small .. 019ba leftover, DUPELIST of
// 019b7) all carry the colour, so any of the four resolves the same way.
const HueRow kOreHues[] = {
    {0x0000, "i_ore_iron"},
    {0x06D6, "i_ore_bronze"},
    {0x045E, "i_ore_gold"},
    {0x0641, "i_ore_copper"},
    {0x0590, "i_ore_old_copper"},
    {0x060A, "i_ore_dull_copper"},
    {0x0482, "i_ore_silver"},
    {0x0770, "i_ore_shadow"},
    {0x04C2, "i_ore_bloodrock"},
    {0x0455, "i_ore_blackrock"},
    {0x052D, "i_ore_mytheril"},
    {0x0665, "i_ore_rose"},
    {0x07D1, "i_ore_verite"},
    {0x0400, "i_ore_agapite"},
    {0x0750, "i_ore_rusty"},
    {0x0515, "i_ore_valorite"},
};

// Ingot: only the twelve metals that share iron's ITEMDEF (ID=i_ingot_iron)
// need a hue lookup at all. Gold/copper/silver are deliberately absent -- see
// the block comment above.
const HueRow kIngotHues[] = {
    {0x0000, "i_ingot_iron"},
    {0x06D6, "i_ingot_bronze"},
    {0x0590, "i_ingot_old_copper"},
    {0x060A, "i_ingot_dull_copper"},
    {0x0770, "i_ingot_shadow"},
    {0x04C2, "i_ingot_bloodrock"},
    {0x0455, "i_ingot_blackrock"},
    {0x052D, "i_ingot_mytheril"},
    {0x0665, "i_ingot_rose"},
    {0x07D1, "i_ingot_verite"},
    {0x0400, "i_ingot_agapite"},
    {0x0750, "i_ingot_rusty"},
    {0x0515, "i_ingot_valorite"},
};

// The four ore graphics: 019b7 plus its DUPELIST piles 019b8/019b9/019ba.
bool IsOreGraphic(u16 g) { return g >= 0x19B7 && g <= 0x19BA; }

// The iron ingot graphic and its five pile flips (01bef, DUPELIST
// 01bf0..01bf4). Only 01bef is ever produced with a COLOR in the scripts
// today, but the whole family is treated as colourable so a hue on a pile
// flip -- if one is ever produced that way -- still resolves instead of
// silently falling back to iron.
bool IsColorableIngotGraphic(u16 g) { return g >= 0x1BEF && g <= 0x1BF4; }

const char* LookupHue(const HueRow* rows, usize count, u16 hue) {
    for (usize i = 0; i < count; ++i)
        if (rows[i].hue == hue) return rows[i].item;
    return nullptr;
}

const std::vector<std::pair<const char*, VendorClass>>& Matrix() {
    static const std::vector<std::pair<const char*, VendorClass>> kV = [] {
        std::vector<std::pair<const char*, VendorClass>> v;
        v.reserve(sizeof(kMatrix) / sizeof(kMatrix[0]));
        for (const Row& r : kMatrix) v.emplace_back(r.item, r.klass);
        return v;
    }();
    return kV;
}

} // namespace

const char* VendorClassName(VendorClass c) {
    switch (c) {
        case VendorClass::Unknown:               return "UNKNOWN";
        case VendorClass::BasicHealing:          return "BASIC_HEALING";
        case VendorClass::BasicCraftTool:        return "BASIC_CRAFT_TOOL";
        case VendorClass::BasicFood:             return "BASIC_FOOD";
        case VendorClass::RevolutionNpcVerified: return "REVOLUTION_NPC_VERIFIED";
        case VendorClass::PlayerMarketGood:      return "PLAYER_MARKET_GOOD";
        case VendorClass::WorldGathered:         return "WORLD_GATHERED";
        case VendorClass::WorldProcessed:        return "WORLD_PROCESSED";
        case VendorClass::PlayerCrafted:         return "PLAYER_CRAFTED";
        case VendorClass::PvmTreasure:           return "PVM_TREASURE";
        case VendorClass::StockSphereOnly:       return "STOCK_SPHERE_ONLY";
        case VendorClass::EraConflict:           return "ERA_CONFLICT";
        default:                                 return "?";
    }
}

const std::vector<std::pair<const char*, VendorClass>>& VendorMatrix() { return Matrix(); }

const char* ItemNameForGraphic(u16 graphic) {
    for (const GraphicRow& g : kGraphics) {
        if (g.graphic == graphic) return g.item;
    }
    return nullptr;
}

// Hue first, graphic fallback -- see the kOreHues/kIngotHues block comment.
// A graphic that is not one of the two colourable families ignores hue
// entirely and behaves exactly like ItemNameForGraphic (e.g. copper's own
// ingot graphic). A colourable graphic with a hue this table has never seen
// falls back to ItemNameForGraphic too, rather than guessing -- the same
// fail-safe default as an unmapped graphic.
const char* ItemNameForGraphicAndHue(u16 graphic, u16 hue) {
    if (IsOreGraphic(graphic)) {
        if (const char* name = LookupHue(kOreHues, sizeof(kOreHues) / sizeof(kOreHues[0]), hue))
            return name;
    } else if (IsColorableIngotGraphic(graphic)) {
        if (const char* name = LookupHue(kIngotHues, sizeof(kIngotHues) / sizeof(kIngotHues[0]), hue))
            return name;
    }
    return ItemNameForGraphic(graphic);
}

bool GraphicNeedsHue(u16 graphic) {
    return IsOreGraphic(graphic) || IsColorableIngotGraphic(graphic);
}

// ORE -> INGOT, straight off the ore ITEMDEF's TDATA1 in
// runtime/scripts/items/i_provisions_ore.scp -- :45 (iron), :80 (copper),
// :90 (gold), :100 (silver), :109 (shadow), :118 (agapite), :127 (verite),
// :136 (rose), :145 (mytheril), :154 (blackrock), :163 (bloodrock),
// :172 (valorite), :181 (bronze), :190 (rusty), :200 (old_copper),
// :210 (dull_copper). Written out rather than derived by swapping the
// "i_ore_" prefix so that a name that is not an ore cannot be turned into an
// ingot defname that does not exist.
const char* IngotNameForOre(const char* oreItem) {
    struct SmeltRow { const char* ore; const char* ingot; };
    static const SmeltRow kSmelt[] = {
        {"i_ore_iron",        "i_ingot_iron"},
        {"i_ore_copper",      "i_ingot_copper"},
        {"i_ore_gold",        "i_ingot_gold"},
        {"i_ore_silver",      "i_ingot_silver"},
        {"i_ore_shadow",      "i_ingot_shadow"},
        {"i_ore_agapite",     "i_ingot_agapite"},
        {"i_ore_verite",      "i_ingot_verite"},
        {"i_ore_rose",        "i_ingot_rose"},
        {"i_ore_mytheril",    "i_ingot_mytheril"},
        {"i_ore_blackrock",   "i_ingot_blackrock"},
        {"i_ore_bloodrock",   "i_ingot_bloodrock"},
        {"i_ore_valorite",    "i_ingot_valorite"},
        {"i_ore_bronze",      "i_ingot_bronze"},
        {"i_ore_rusty",       "i_ingot_rusty"},
        {"i_ore_old_copper",  "i_ingot_old_copper"},
        {"i_ore_dull_copper", "i_ingot_dull_copper"},
    };
    if (!oreItem) return nullptr;
    for (const SmeltRow& r : kSmelt) {
        if (std::strcmp(r.ore, oreItem) == 0) return r.ingot;
    }
    return nullptr;
}

std::vector<u16> GraphicsForItem(const char* item) {
    std::vector<u16> out;
    if (!item) return out;
    for (const GraphicRow& g : kGraphics) {
        if (std::strcmp(g.item, item) == 0) out.push_back(g.graphic);
    }
    return out;
}

VendorClass ClassifyForVendorGraphic(u16 graphic) {
    const char* item = ItemNameForGraphic(graphic);
    return item ? ClassifyForVendor(item) : VendorClass::Unknown;
}

VendorRuling CanUseNPCVendorForGraphic(u16 graphic) {
    const char* item = ItemNameForGraphic(graphic);
    if (!item) {
        VendorRuling out;
        out.klass  = VendorClass::Unknown;
        out.reason = "graphic is not in the audited vendor matrix; refusing and "
                     "recording an authenticity gap";
        out.authenticityGap = true;
        return out;
    }
    return CanUseNPCVendorFor(item);
}

VendorRuling CanBuyFromNPCGraphic(u16 graphic) {
    return CanBuyFromNPC(ItemNameForGraphic(graphic));
}

VendorClass ClassifyForVendor(const char* item) {
    for (const Row& r : kMatrix) {
        if (Same(r.item, item)) return r.klass;
    }
    return VendorClass::Unknown;
}

VendorRuling CanUseNPCVendorFor(const char* item) {
    VendorRuling out;
    out.klass = ClassifyForVendor(item);
    switch (out.klass) {
        case VendorClass::RevolutionNpcVerified:
            out.allowed = true;
            out.reason  = "a dated Revolution update records an NPC selling this";
            break;
        case VendorClass::BasicFood:
            out.allowed = true;
            out.reason = "basic food: a character with hunger enabled must be "
                         "able to eat, and Revolution's cooking economy shows "
                         "food was not an income good";
            break;
        case VendorClass::BasicHealing:
            out.allowed = true;
            out.reason  = "basic healing: a life with no Healing skill has no "
                          "other way to treat itself, and the shard's own "
                          "healer sells it";
            break;
        case VendorClass::BasicCraftTool:
            out.allowed = true;
            out.reason  = "a basic craft tool: a tool is not a resource, it "
                          "shortcuts no chain, and without a purchasable first "
                          "tool the craft tree cannot be bootstrapped at all";
            break;
        case VendorClass::PlayerMarketGood:
            // Permitted because the Bowcraft guide documents an NPC floor
            // alongside the player market. Narrow, and evidence-backed.
            out.allowed = true;
            out.reason  = "a player-market good with a documented NPC floor "
                          "(guide: sell to other players OR to vendors)";
            break;
        case VendorClass::WorldGathered:
            out.reason = "a gathering skill produces this; go and get it";
            break;
        case VendorClass::WorldProcessed:
            out.reason = "a station produces this; go and process it";
            break;
        case VendorClass::PlayerCrafted:
            out.reason = "a player craft produces this; craft it or buy from a player";
            break;
        case VendorClass::PvmTreasure:
            out.reason = "this comes from a corpse or a chest";
            break;
        case VendorClass::StockSphereOnly:
            out.reason = "on a stock Sphere vendor with no Revolution evidence";
            break;
        case VendorClass::EraConflict:
            out.reason = "belongs to a later era than revolution_2009_2010";
            break;
        case VendorClass::Unknown:
        default:
            out.reason = "no Revolution evidence either way; refusing and "
                         "recording an authenticity gap";
            out.authenticityGap = true;
            break;
    }
    return out;
}

VendorRuling CanBuyFromNPC(const char* item) {
    // Purchasing consumes the bot's own gold. Unlike an NPC buy-back, it is
    // not a faucet and cannot mint wealth. The owner ruling is therefore to
    // accept every item a real vendor window offers; the wire offer is the
    // stock proof. Keep the matrix classification attached so logs and the
    // research report still say what evidence we have (or lack), but do not
    // turn that research status into a purchase veto.
    VendorRuling out;
    out.allowed = true;
    out.klass = ClassifyForVendor(item);
    out.reason = "NPC purchase is allowed when the item is actually offered; "
                 "NPC selling remains separately restricted";
    return out;
}

// ---------------------------------------------------------------------------
// THE NPC PRICE FLOOR. See the block comment in vendor_policy.h.
// ---------------------------------------------------------------------------
namespace {
SalePolicy g_salePolicy;   // default-constructed: allowMaterialsToNpc = true
}  // namespace

const SalePolicy& CurrentSalePolicy() { return g_salePolicy; }
void SetSalePolicy(const SalePolicy& p) { g_salePolicy = p; }

bool IsFloorMaterial(const char* item) {
    if (!item || !*item) return false;

    // The two hue families first. i_ore_valorite and i_ingot_shadow are
    // `ID=i_ore_iron` / `ID=i_ingot_iron` plus a COLOR line, so kMatrix names
    // only the base of each and a lookup for the other fifteen returns
    // Unknown -- which would refuse the rarest thing a miner owns for a
    // reason that is an artefact of the script format, not a judgement.
    if (std::strncmp(item, "i_ore_", 6) == 0) return true;
    if (std::strncmp(item, "i_ingot_", 8) == 0) return true;

    switch (ClassifyForVendor(item)) {
        case VendorClass::WorldGathered:   // logs, ore, hides, wool, fish...
        case VendorClass::WorldProcessed:  // cloth, thread, yarn, cut hides...
            return true;
        default:
            break;
    }

    // Two the matrix grades otherwise but which are materials by the ruling's
    // own words. A bolt is cloth in a larger unit, and a board is "a material,
    // like the log it came from" (economy/Faucets.cpp, board_to_vendor).
    static const char* kAlsoMaterial[] = {"i_cloth_bolt", "i_board"};
    for (const char* m : kAlsoMaterial) {
        if (Same(m, item)) return true;
    }
    return false;
}

bool MaterialFloorOpen(const char* item, bool playersDeclined) {
    if (!g_salePolicy.allowMaterialsToNpc) return false;
    // PLAYER-FIRST IS NOT A PREFERENCE HERE, IT IS THE GATE. The floor exists
    // to stop a shortage becoming a deadlock, not to save the character a
    // shout, so it opens only once the announce cycle has finished unanswered.
    if (!playersDeclined) return false;
    return IsFloorMaterial(item);
}

const char* AcquisitionName(Acquisition a) {
    switch (a) {
        case Acquisition::None:           return "none";
        case Acquisition::AlreadyHeld:    return "already held";
        case Acquisition::Gather:         return "gather";
        case Acquisition::Process:        return "process";
        case Acquisition::Craft:          return "craft";
        case Acquisition::NpcPurchase:    return "buy from NPC";
        case Acquisition::PlayerPurchase: return "buy from player vendor";
        case Acquisition::DirectTrade:    return "direct player trade";
        case Acquisition::Pvm:            return "PvM";
        case Acquisition::Treasure:       return "treasure";
        case Acquisition::Tame:           return "tame";
        default:                          return "?";
    }
}

namespace {

i32 Held(const std::vector<prod::Ingredient>& inv, const char* item) {
    i32 n = 0;
    for (const prod::Ingredient& i : inv) {
        if (Same(i.item, item)) n += i.qty;
    }
    return n;
}

} // namespace

AcquisitionPlan ChooseAcquisitionMethod(const char* item,
                                        const AcquisitionContext& ctx) {
    AcquisitionPlan plan;

    if (Held(ctx.inventory, item) > 0) {
        plan.method = Acquisition::AlreadyHeld;
        plan.reason = "already in the pack";
        return plan;
    }

    // Self-production first, always. That ordering IS the milestone's thesis:
    // a Revolution character reaches for the world before the shop.
    const std::vector<prod::Requirement> blockers =
        prod::MissingInputs(item, ctx.capability, ctx.inventory);
    const prod::Recipe* recipe = prod::FindRecipe(item);

    if (blockers.empty() && recipe) {
        switch (recipe->provenance) {
            case prod::Provenance::WorldGathered:
            case prod::Provenance::AnimalHarvested:
                plan.method = Acquisition::Gather;
                plan.reason = "the character can gather this itself";
                return plan;
            case prod::Provenance::WorldProcessed:
                plan.method = Acquisition::Process;
                plan.reason = "the character holds the input and can reach the station";
                return plan;
            case prod::Provenance::PvmDrop:
                plan.method = Acquisition::Pvm;
                plan.reason = "carved from a corpse";
                return plan;
            case prod::Provenance::TreasureDrop:
                plan.method = Acquisition::Treasure;
                plan.reason = "from a chest or a map";
                return plan;
            default:
                plan.method = Acquisition::Craft;
                plan.reason = "the character has the skill, tool, station and inputs";
                return plan;
        }
    }

    // Cannot self-produce yet. Report exactly why, then look at buying.
    plan.blockers = blockers;

    const i32 spendable = ctx.gold - ctx.goldReserve;

    // A player is always a legitimate counterparty for anything -- that is the
    // point of a player economy -- so it is tried before an NPC even when both
    // are possible.
    if (ctx.observedPlayerPrice >= 0 && ctx.observedPlayerPrice <= spendable) {
        plan.method        = Acquisition::PlayerPurchase;
        plan.reason        = "another player is selling it at an observed price";
        plan.estimatedCost = ctx.observedPlayerPrice;
        return plan;
    }

    const VendorRuling ruling = CanBuyFromNPC(item);
    if (ruling.allowed) {
        if (ctx.observedNpcPrice < 0) {
            plan.method = Acquisition::NpcPurchase;
            plan.reason = "NPC purchase is permitted, but no price has been "
                          "observed yet -- open the vendor and read one";
            plan.estimatedCost = 0;
            return plan;
        }
        if (ctx.observedNpcPrice <= spendable) {
            plan.method        = Acquisition::NpcPurchase;
            plan.reason        = ruling.reason;
            plan.estimatedCost = ctx.observedNpcPrice;
            return plan;
        }
    }

    // Nothing legal is available. Say so rather than improvising: a bot that
    // quietly substitutes an illegal route is exactly what M3.7 forbids.
    plan.blocked = true;
    plan.method  = Acquisition::None;
    plan.reason  = ruling.allowed
                       ? "cannot self-produce, and cannot afford the observed price"
                       : ruling.reason;
    return plan;
}

std::vector<Match> MatchNeeds(const std::vector<ResourceNeed>& needs,
                              const std::vector<SellOffer>& offers,
                              std::vector<usize>* unmatched) {
    std::vector<Match> out;
    // Copy the remaining quantities so one offer cannot be sold twice.
    std::vector<i32> left;
    left.reserve(offers.size());
    for (const SellOffer& o : offers) left.push_back(o.qty);

    // Most urgent first; ties broken by index so the result is deterministic.
    std::vector<usize> order(needs.size());
    for (usize i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](usize a, usize b) {
        return needs[a].urgency > needs[b].urgency;
    });

    for (usize ni : order) {
        i32 want = needs[ni].qty;
        bool any = false;
        // Cheapest offer of the right item wins.
        while (want > 0) {
            usize best = offers.size();
            for (usize oi = 0; oi < offers.size(); ++oi) {
                if (left[oi] <= 0) continue;
                if (offers[oi].item != needs[ni].item) continue;
                if (best == offers.size() ||
                    offers[oi].askPerUnit < offers[best].askPerUnit) {
                    best = oi;
                }
            }
            if (best == offers.size()) break;
            const i32 take = std::min(want, left[best]);
            out.push_back({ni, best, take, offers[best].askPerUnit});
            left[best] -= take;
            want       -= take;
            any = true;
        }
        if (!any && unmatched) unmatched->push_back(ni);
    }
    return out;
}

} // namespace uo::econ
