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

    // --- named cooperative categories (08.11.2008, 19.12.2008) --------------
    // These are PLAYER goods by Revolution's own word. Ammunition is the
    // exception that proves the rule: the Bowcraft guide says bows are sold
    // "diger oyunculara ya da TEZGAHTARLARA" -- to players OR to NPC vendors --
    // so a player market and an NPC floor demonstrably coexisted. See §3.2 of
    // the matrix; blocking arrows would INVENT a restriction.
    {"i_cloth_bolt",      VendorClass::PlayerMarketGood},
    {"i_ingot_iron",      VendorClass::PlayerMarketGood},
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
    {0x19B7, "i_ore_iron"},      {0x19B8, "i_ore_iron"},   {0x19B9, "i_ore_iron"},
    {0x1BEF, "i_ingot_iron"},    {0x1BF0, "i_ingot_iron"}, {0x1BF1, "i_ingot_iron"},
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
    // THE SPELLBOOK. kMatrix already grades i_spellbook PlayerMarketGood,
    // which is ALLOWED -- but a vendor offer arrives on the wire as a GRAPHIC,
    // and with no row here ItemNameForGraphic(0x0EFA) returned null and
    // CanUseNPCVendorForGraphic fell through to Unknown and refused:
    //   [policy] REFUSED NPC purchase of unmapped item (0x0EFA): graphic is
    //   not in the audited vendor matrix ... [UNKNOWN]
    // 32 times in one run. A grading is only reachable if the graphic that
    // names it is here too -- the two tables have to agree.
    {0x0EFA, "i_spellbook"},
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

    const VendorRuling ruling = CanUseNPCVendorFor(item);
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
