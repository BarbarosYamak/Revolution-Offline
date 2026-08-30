// S1 (docs/CRAFTER_RUN_2026_08_30.md #20): hue-aware metal resolution.
//
// (graphic, hue) -> defname, using the shard's own COLOR_ORE table
// (runtime/scripts/items/i_provisions_ore.scp [DEFNAME COLOR_ORE]), hue
// first, graphic fallback. Ore is ONE graphic for every metal -- copper,
// gold, silver, shadow and the rest are all `ID=i_ore_iron` there, told
// apart only by an `ON=@Create COLOR=color_o_*` line -- and the iron INGOT
// graphic is the same story for the twelve special metals that never got
// their own ITEMDEF. Copper/gold/silver ingots DO have their own ITEMDEF
// (01be3/01be9/01bf5) and are never coloured, so the graphic alone already
// says which metal they are.
//
// No server, no MULs, no world data -- pure table lookup.

#include "uo/vendor_policy.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace uo;

namespace {

int g_failures = 0;
int g_checks = 0;

void CheckName(const char* got, const char* want, const char* what) {
    ++g_checks;
    if (!got || std::strcmp(got, want) != 0) {
        ++g_failures;
        std::printf("  FAIL  %s (got %s, want %s)\n", what, got ? got : "(null)",
                    want);
    }
}

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

} // namespace

int main() {
    using econ::ItemNameForGraphicAndHue;

    // The three cases named in the slice spec, verbatim.
    CheckName(ItemNameForGraphicAndHue(0x1BEF, 0x0515), "i_ingot_valorite",
              "i_ingot_iron graphic + valorite hue -> i_ingot_valorite");
    CheckName(ItemNameForGraphicAndHue(0x1BEF, 0), "i_ingot_iron",
              "i_ingot_iron graphic + hue 0 -> i_ingot_iron");
    CheckName(ItemNameForGraphicAndHue(0x19B9, 0x0770), "i_ore_shadow",
              "large-pile ore graphic + shadow hue -> i_ore_shadow");

    // The whole ore pile-size family (019b7 small .. 019ba leftover,
    // DUPELIST of 019b7) must resolve the colour identically -- a miner's
    // ore changes GRAPHIC as the pile grows, never its hue.
    CheckName(ItemNameForGraphicAndHue(0x19B7, 0), "i_ore_iron",
              "small-pile ore graphic + hue 0 -> i_ore_iron");
    CheckName(ItemNameForGraphicAndHue(0x19B8, 0x0641), "i_ore_copper",
              "medium-pile ore graphic + copper hue -> i_ore_copper");
    CheckName(ItemNameForGraphicAndHue(0x19BA, 0x0770), "i_ore_shadow",
              "leftover-pile ore graphic + shadow hue -> i_ore_shadow");

    // Base metals with their OWN ingot graphic (never coloured in the
    // scripts) resolve from the graphic alone, same as ItemNameForGraphic,
    // whatever hue happens to arrive.
    CheckName(ItemNameForGraphicAndHue(0x1BE3, 0), "i_ingot_copper",
              "copper ingot's own graphic, hue 0 -> i_ingot_copper");
    CheckName(ItemNameForGraphicAndHue(0x1BE9, 0), "i_ingot_gold",
              "gold ingot's own graphic, hue 0 -> i_ingot_gold");
    CheckName(ItemNameForGraphicAndHue(0x1BF5, 0), "i_ingot_silver",
              "silver ingot's own graphic, hue 0 -> i_ingot_silver");

    // A handful of the other ten special ingots, all sharing 0x1BEF with
    // iron, told apart only by hue.
    CheckName(ItemNameForGraphicAndHue(0x1BEF, 0x0400), "i_ingot_agapite",
              "iron ingot graphic + agapite hue -> i_ingot_agapite");
    CheckName(ItemNameForGraphicAndHue(0x1BEF, 0x07D1), "i_ingot_verite",
              "iron ingot graphic + verite hue -> i_ingot_verite");
    CheckName(ItemNameForGraphicAndHue(0x1BEF, 0x0455), "i_ingot_blackrock",
              "iron ingot graphic + blackrock hue -> i_ingot_blackrock");

    // Gold/copper/silver hues never apply to the iron ingot graphic in the
    // scripts (those three metals have their own ITEMDEF), so a hue that
    // only means something for ORE must not accidentally resolve on the
    // ingot graphic.
    Check(std::strcmp(ItemNameForGraphicAndHue(0x1BEF, 0x0641),
                       "i_ingot_copper") != 0,
          "the ore-only copper hue does not make the iron ingot graphic "
          "read as copper");

    // An unrecognised hue on a colourable graphic falls back to the honest
    // graphic-only default rather than guessing -- the same fail-safe
    // ItemNameForGraphic already uses for an unmapped graphic.
    CheckName(ItemNameForGraphicAndHue(0x1BEF, 0x1234), "i_ingot_iron",
              "unrecognised hue on the iron ingot graphic falls back to iron");
    CheckName(ItemNameForGraphicAndHue(0x19B7, 0x1234), "i_ore_iron",
              "unrecognised hue on the ore graphic falls back to iron ore");

    // A graphic outside both colourable families ignores hue entirely and
    // behaves exactly like ItemNameForGraphic.
    CheckName(ItemNameForGraphicAndHue(0x1BDD, 0x0515), "i_log",
              "a non-metal graphic ignores hue, same as ItemNameForGraphic");

    // A graphic this table has never heard of stays nullptr, same as
    // ItemNameForGraphic.
    Check(ItemNameForGraphicAndHue(0xFFFF, 0) == nullptr,
          "unmapped graphic returns nullptr regardless of hue");

    // --- WHICH GRAPHICS ARE AMBIGUOUS AT ALL --------------------------------
    //
    // A caller holding only a graphic has to know whether it yet knows what
    // the item is. Exactly two families are shared between metals: raw ore
    // (019b7..019ba) and the iron ingot (01bef..01bf4). Everything else names
    // itself, which is what lets the vendor sell path fall back safely when a
    // serial cannot be found in the pack.
    Check(econ::GraphicNeedsHue(0x19B7) && econ::GraphicNeedsHue(0x19BA),
          "the whole ore pile family needs a hue");
    Check(econ::GraphicNeedsHue(0x1BEF) && econ::GraphicNeedsHue(0x1BF4),
          "the whole iron-ingot family needs a hue");
    Check(!econ::GraphicNeedsHue(0x1BE3) && !econ::GraphicNeedsHue(0x1BE9) &&
              !econ::GraphicNeedsHue(0x1BF5),
          "copper/gold/silver ingots have their own graphic and need no hue");
    Check(!econ::GraphicNeedsHue(0x1BDD) && !econ::GraphicNeedsHue(0x0F51),
          "a log and a dagger name themselves");

    // --- ORE -> INGOT, the ore ITEMDEF's own TDATA1 -------------------------
    //
    // DoSmelt measures its progress by counting the ingot it is MAKING, and
    // the ore picker falls back to a coloured vein once the plain iron is
    // gone. Counting i_ingot_iron then never moves, so a smelter melting
    // valorite reports no progress while producing the rarest metal on the
    // shard. TDATA1 lines: i_provisions_ore.scp:45, :80, :90, :100, :109,
    // :118, :127, :136, :145, :154, :163, :172, :181, :190, :200, :210.
    CheckName(econ::IngotNameForOre("i_ore_iron"), "i_ingot_iron",
              "iron ore smelts to iron ingots");
    CheckName(econ::IngotNameForOre("i_ore_valorite"), "i_ingot_valorite",
              "valorite ore smelts to valorite ingots");
    CheckName(econ::IngotNameForOre("i_ore_rusty"), "i_ingot_rusty",
              "rusty ore smelts to rusty ingots");
    CheckName(econ::IngotNameForOre("i_ore_old_copper"), "i_ingot_old_copper",
              "old copper ore smelts to old copper ingots");
    CheckName(econ::IngotNameForOre("i_ore_dull_copper"), "i_ingot_dull_copper",
              "dull copper ore smelts to dull copper ingots");
    CheckName(econ::IngotNameForOre("i_ore_bronze"), "i_ingot_bronze",
              "bronze ore smelts to bronze ingots");
    // The three metals with their own ingot ITEMDEF are ores like any other.
    CheckName(econ::IngotNameForOre("i_ore_copper"), "i_ingot_copper",
              "copper ore smelts to copper ingots");
    CheckName(econ::IngotNameForOre("i_ore_gold"), "i_ingot_gold",
              "gold ore smelts to gold ingots");
    CheckName(econ::IngotNameForOre("i_ore_silver"), "i_ingot_silver",
              "silver ore smelts to silver ingots");
    // Anything that is not one of the sixteen ores gets no answer at all,
    // rather than an ingot defname that does not exist on the shard.
    Check(econ::IngotNameForOre("i_ingot_iron") == nullptr,
          "an ingot is not an ore");
    Check(econ::IngotNameForOre("i_log") == nullptr, "a log is not an ore");
    Check(econ::IngotNameForOre(nullptr) == nullptr, "null is not an ore");

    // Every ore hue the shard can hand a smelter must have an ingot, or
    // DoSmelt silently falls back to counting iron. Composed the way DoSmelt
    // composes it: graphic+hue -> ore name -> ingot name.
    {
        const u16 kAllOreHues[] = {0x0000, 0x06D6, 0x045E, 0x0641, 0x0590,
                                   0x060A, 0x0482, 0x0770, 0x04C2, 0x0455,
                                   0x052D, 0x0665, 0x07D1, 0x0400, 0x0750,
                                   0x0515};
        for (u16 h : kAllOreHues) {
            const char* ore = ItemNameForGraphicAndHue(0x19B7, h);
            Check(ore != nullptr && econ::IngotNameForOre(ore) != nullptr,
                  "every COLOR_ORE hue resolves to an ore with an ingot");
        }
    }

    // --- THE SMITH'S OTHER TWO WEAPONS --------------------------------------
    //
    // i_cutlass and i_spear_short are both on the smith's produces list
    // (life/Professions.cpp:361) and both had no graphic row, so a finished
    // one was invisible to the pack view and every craft read as NoProgress.
    // Ids and DUPELISTs: items/weapons/i_weapons.scp:1022/:1041 and
    // :1221/:1238.
    CheckName(econ::ItemNameForGraphic(0x1440), "i_cutlass",
              "the cutlass graphic names itself");
    CheckName(econ::ItemNameForGraphic(0x1441), "i_cutlass",
              "the cutlass DUPELIST flip names itself too");
    CheckName(econ::ItemNameForGraphic(0x1402), "i_spear_short",
              "the short spear graphic names itself");
    CheckName(econ::ItemNameForGraphic(0x1403), "i_spear_short",
              "the short spear DUPELIST flip names itself too");
    // A weapon is not metal: hue must not change the answer.
    CheckName(ItemNameForGraphicAndHue(0x1440, 0x0515), "i_cutlass",
              "a hued cutlass is still a cutlass");
    // Both directions agree, which is what the pack counter relies on.
    {
        const std::vector<u16> g = econ::GraphicsForItem("i_cutlass");
        Check(g.size() == 2, "i_cutlass has both of its graphics");
    }

    std::printf("hue_metal: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
