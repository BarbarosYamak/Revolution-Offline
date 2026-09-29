// S4 -- the gear half of the trade predicate.
// docs/CRAFTER_RUN_2026_08_30.md section 1, slice S4.
//
// THE DEFECT THIS SUITE EXISTS TO MAKE UNWRITABLE: `market::ConsiderOffer`
// asked `Shortfall`, `Shortfall` reads `consumes` and `consumables`, and a
// SWORD is in neither. So a swordsman standing at the Britain bank, unarmed,
// hearing a smith announce a sword, answered "this life has no use for it" --
// and `Profession::wears` had never been consulted by the economy layer at
// all.
//
// Everything asserted here about the weapon itself comes from the shard's own
// scripts and is cited at the assertion, never from generic UO:
//
//   runtime/scripts/crafting/interface/def_blacksmithing.scp:173
//       blacksmithing_category_6_4  "i_cutlass"          -- a smith may make it
//   runtime/scripts/items/weapons/i_weapons.scp:1221-1240
//       TYPE=t_weapon_sword  SKILL=Swordsmanship  TWOHANDS=N
//       RESOURCES=8 i_ingot_iron  SKILLMAKE=Blacksmithing 24.3  ReqStr=25
//
// No server, no MULs, no world data.

#include "uo/life.h"
#include "uo/market.h"
#include "uo/production.h"
#include "uo/professions.h"
#include "uo/rules.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

using namespace uo;
using namespace uo::market;

// The weapon, as the shard defines it. One place, so a later change to the
// choice moves one line and the citations move with it.
constexpr const char* kSword = "i_cutlass";
constexpr u16         kSwordGfx = 0x1440;
constexpr u16         kSwordFlip = 0x1441;   // DUPELIST=01441
constexpr u16         kKatanaGfx = 0x13FE;   // another Swordsmanship weapon

TradeIntent Wts(const char* item, i32 qty, i32 price) {
    TradeIntent t;
    t.item = item;
    t.qty = qty;
    t.pricePerUnit = price;
    return t;
}

// --------------------------------------------------------------------------
// The shard evidence, checked rather than trusted.
// --------------------------------------------------------------------------
void TestTheWeaponIsOneTheShardSupports() {
    Section("evidence: the smith can make it and the swordsman's skill uses it");

    const GearItem* g = FindGear(kSword);
    Check(g != nullptr, "the trade layer knows the weapon at all");
    if (!g) return;

    // SKILL= is the only authority on what a weapon trains. The crafting
    // menu's "Bladed" category is art, not mechanics -- i_dagger is filed in
    // it and is SKILL=Fencing (i_weapons.scp:501).
    Check(g->weaponSkill == rules::kSwordsmanship,
          "i_weapons.scp:1226 SKILL=Swordsmanship");
    Check(g->graphic == kSwordGfx && g->flip == kSwordFlip,
          "i_weapons.scp:1221 ITEMDEF 01440, :1240 DUPELIST=01441");
    Check(g->layer == 1, "TWOHANDS=N (:1231) -- the weapon hand, Client.cpp:51");
    Check(g->reqStr == 25, "i_weapons.scp:1237 ReqStr=25");
    Check(FindGearByGraphic(kSwordFlip) == g,
          "the flipped art resolves to the same weapon");

    // The recipe graph must agree, or the smith has an order it cannot fill.
    const prod::Recipe* r = prod::FindRecipe(kSword);
    Check(r != nullptr, "the production graph carries the recipe");
    if (r) {
        Check(r->skillId == rules::kBlacksmithing && r->skillTenths == 243,
              "i_weapons.scp:1233 SKILLMAKE=Blacksmithing 24.3");
        Check(r->inputs[0].item && std::string(r->inputs[0].item) == "i_ingot_iron" &&
                  r->inputs[0].qty == 8,
              "i_weapons.scp:1232 RESOURCES=8 i_ingot_iron");
        Check(!r->inputs[1].item,
              "iron and nothing else -- no log, so the smith needs no supplier");
        // A ~50 Blacksmithing smith (Durnholde, Brannoc) clears it, and
        // def_blacksmithing.scp:88 sets _chanceatmin to 0.0, so the headroom
        // IS the success chance.
        Check(r->skillTenths <= 500,
              "a smith at 50.0 can make it (def_blacksmithing.scp:88 chanceatmin 0.0)");
    }

    // And the smith actually offers it.
    const prof::Profession* ms = prof::Find("miner_smith");
    Check(ms != nullptr, "the smith exists");
    if (ms) {
        bool makes = false;
        for (const std::string& made : ms->produces) makes = makes || (made == kSword);
        Check(makes, "miner_smith.produces names the swordsman's weapon");
    }
}

// --------------------------------------------------------------------------
// The four cases the slice is specified by.
// --------------------------------------------------------------------------
void TestWhoWantsASword() {
    Section("trade: a swordsman wants a sword, and nobody else does");

    const prof::Profession* sw = prof::Find("lumberjack_swordsman");
    const prof::Profession* mg = prof::Find("mage");
    Check(sw && mg, "both lives exist");
    if (!sw || !mg) return;

    TradePolicy pol;
    // The price a seller with no belief actually names (TradePolicy::openingAsk),
    // which is what the smith will say on the first trade of the run.
    const TradeIntent sale = Wts(kSword, 1, pol.openingAsk);
    // Clear of the swordsman's 10,000 reserve with room to spare.
    const i32 rich = sw->goldReserve + 1000;

    // 1. UNARMED SWORDSMAN, AFFORDABLE PRICE -> yes.
    const BuyDecision yes = ConsiderOffer(*sw, {}, rich, pol, sale, {});
    Check(yes.accept, "an unarmed swordsman accepts a sword he can pay for");
    Check(yes.qty == 1, "ONE -- equipment is not stock (the six heater shields)");
    Check(yes.reason && std::string(yes.reason) == "would wear it",
          "and says why in the slice's own words");

    // 2. A PURE MAGE -> no. Magery, Meditation, Eval Int; no weapon skill in
    //    the build, so the sword is somebody else's tool.
    const BuyDecision no = ConsiderOffer(*mg, {}, rich, pol, sale, {});
    Check(!no.accept, "a pure mage refuses the same sword at the same price");

    // 3. ALREADY WIELDING ONE -> no. This is the check DecideAcquire cannot
    //    make on its own: it compares graphic to graphic.
    const std::vector<WornItem> armed = {{1, kSwordGfx}, {2, 0}};
    Check(!ConsiderOffer(*sw, {}, rich, pol, sale, armed).accept,
          "a swordsman already holding that sword refuses another");
    const std::vector<WornItem> flipped = {{1, kSwordFlip}, {2, 0}};
    Check(!ConsiderOffer(*sw, {}, rich, pol, sale, flipped).accept,
          "and the flipped art is the same sword");
    // A DIFFERENT sword of the same skill is still "already armed".
    const std::vector<WornItem> katana = {{1, kKatanaGfx}, {2, 0}};
    const BuyDecision armedAlready = ConsiderOffer(*sw, {}, rich, pol, sale, katana);
    Check(!armedAlready.accept,
          "a swordsman holding a katana does not need a cutlass too");
    Check(armedAlready.reason &&
              std::string(armedAlready.reason) == "already carrying one of those",
          "and the refusal names the class, not the graphic");

    // 4. BELOW THE RESERVE -> no. The reserve is what buys a replacement tool
    //    after a death; a trade must never eat it.
    const i32 broke = sw->goldReserve + pol.openingAsk - 1;
    const BuyDecision poor = ConsiderOffer(*sw, {}, broke, pol, sale, {});
    Check(!poor.accept, "one coin short of the reserve is a refusal");
    Check(poor.reason && std::string(poor.reason) ==
              "would eat into the reserve this life keeps for tools",
          "and the refusal is about the reserve, not about wanting it");
    Check(ConsiderOffer(*sw, {}, 1, pol, sale, {}).accept == false,
          "an empty purse is a refusal too");
}

// --------------------------------------------------------------------------
void TestTheRefusalsAreSpecific() {
    Section("trade: every no says which no it is");

    const prof::Profession* sw = prof::Find("lumberjack_swordsman");
    if (!sw) { Check(false, "the swordsman exists"); return; }
    TradePolicy pol;
    const i32 rich = sw->goldReserve + 1000;

    // ALREADY IN THE PACK. DecideAcquire answers Wear, not Buy -- carrying one
    // is a reason to put it on, never a reason to buy a second.
    const std::vector<Stock> hasOne = {{kSword, 1}};
    const BuyDecision carried =
        ConsiderOffer(*sw, hasOne, rich, pol, Wts(kSword, 1, pol.openingAsk), {});
    Check(!carried.accept, "one already in the pack stops the purchase");

    // THE SMITH'S OTHER BLADE. i_dagger is filed under "Bladed" in the same
    // menu (def_blacksmithing.scp:174) and is TYPE=t_weapon_fence
    // SKILL=Fencing (i_weapons.scp:498,501). A swordsman does not want it.
    const BuyDecision dagger =
        ConsiderOffer(*sw, {}, rich, pol, Wts("i_dagger", 1, pol.openingAsk), {});
    Check(!dagger.accept, "a swordsman refuses a fencer's dagger");
    Check(dagger.reason && std::string(dagger.reason) ==
              "this life does not train the skill that weapon uses",
          "and the reason is the skill, not the price");

    // A GOUGING PRICE is refused sight unseen, exactly as for materials.
    const BuyDecision gouge = ConsiderOffer(
        *sw, {}, 1000000, pol, Wts(kSword, 1, pol.blindPriceCeiling + 1), {});
    Check(!gouge.accept, "above the blind ceiling, gear is refused like anything else");

    // SOMETHING THAT IS NOT GEAR AT ALL keeps the old answer word for word,
    // so nothing downstream that reads the reason string changes meaning.
    const BuyDecision junk =
        ConsiderOffer(*sw, {}, rich, pol, Wts("i_reag_black_pearl", 5, 2), {});
    Check(!junk.accept, "a swordsman still has no use for reagents");
    Check(junk.reason && std::string(junk.reason) == "this life has no use for it",
          "and the material refusal is unchanged");
}

// --------------------------------------------------------------------------
// The material half must not have moved.
// --------------------------------------------------------------------------
void TestTheMaterialHalfIsUnchanged() {
    Section("trade: the log edge still works exactly as it did");

    const prof::Profession* ms = prof::Find("miner_smith");
    if (!ms) { Check(false, "the smith exists"); return; }

    TradePolicy pol;
    // i_spear_short is 6 ingots plus one LOG, so a smith with no logs is short.
    const TradeIntent logs = Wts("i_log", 10, 2);
    Check(ConsiderOffer(*ms, {}, 1000, pol, logs, {}).accept,
          "a smith with no logs still buys logs");
    // With the worn view supplied, and with none: the same answer.
    const std::vector<WornItem> worn = {{1, 0}, {2, 0}};
    Check(ConsiderOffer(*ms, {}, 1000, pol, logs, worn).accept,
          "and passing a paperdoll view does not change a material decision");

    // The smith is not a swordsman: it must not start buying swords either.
    const BuyDecision sword =
        ConsiderOffer(*ms, {}, 100000, pol, Wts(kSword, 1, 2), {});
    Check(!sword.accept, "the smith makes the sword; it does not want one");
}

// --------------------------------------------------------------------------
// SCHOOL -> WEAPON. The gap this fix closes: DoReplaceEquipment could only
// ARM a weapon already in the pack, so a WantsToHunt fighter created
// bare-handed, or stripped of its weapon by death, had no path back to one.
// `life::SchoolWeaponFor` is the pure half of the buy that closes it -- no
// Client, so it is exercised here directly rather than through the live bot.
//
// Every profession below is the catalogue's own (prof::Find), not a
// hand-built fixture, so this checks the same records DoReplaceEquipment
// reads. Each maps to the exact defname the newbie kit hands that skill and
// the fix report cites -- see life/Identity.cpp for the itemdef/tm_vend.scp
// line numbers.
// --------------------------------------------------------------------------
void TestSchoolWeaponMapping() {
    Section("trade: every WantsToHunt family maps to an existing defname");

    struct Want { const char* profId; const char* defname; int skill; };
    const Want kWants[] = {
        {"lumberjack_swordsman", "i_katana", rules::kSwordsmanship},
        {"fencer",               "i_kryss",  rules::kFencing},
        {"macer",                "i_club",   rules::kMaceFighting},
        {"archer",               "i_bow",    rules::kArchery},
    };
    for (const Want& w : kWants) {
        const prof::Profession* p = prof::Find(w.profId);
        if (!p) { Check(false, w.profId); continue; }
        Check(life::WantsToHunt(*p), w.profId);

        const life::SchoolWeapon* sw = life::SchoolWeaponFor(*p);
        if (!sw) { Check(false, w.profId); continue; }
        Check(std::string(sw->defname) == w.defname, w.profId);
        Check(sw->skill == w.skill, w.profId);
        // Both graphics of the flip pair are real (non-zero) -- an unset
        // DUPELIST entry would leave FindAny(client, graphics, 2) blind to
        // half of what the server can hand back on the wire.
        Check(sw->graphics[0] != 0 && sw->graphics[1] != 0, w.profId);
        Check(sw->defname != nullptr && sw->defname[0] != '\0', w.profId);
        Check(sw->item != nullptr && sw->item[0] != '\0', w.profId);
    }

    // Archery is the one school sold at an unambiguously-titled shop (the
    // bowyer) in ADDITION to the weaponsmith -- see the citation in
    // life/Identity.cpp. The other three are weaponsmith-only.
    if (const prof::Profession* archer = prof::Find("archer")) {
        const life::SchoolWeapon* sw = life::SchoolWeaponFor(*archer);
        if (sw) Check(sw->bowyerFallback, "archer: the bowyer is offered as a second seller");
    }
    if (const prof::Profession* sword = prof::Find("lumberjack_swordsman")) {
        const life::SchoolWeapon* sw = life::SchoolWeaponFor(*sword);
        if (sw) Check(!sw->bowyerFallback, "swordsman: no bowyer fallback");
    }

    // A LIFE WITH NO WEAPON SCHOOL AT ALL gets nothing to buy -- the smith
    // makes the katana, it does not need to shop for one (same predicate
    // TestTheMaterialHalfIsUnchanged already exercises for the sword itself).
    if (const prof::Profession* ms = prof::Find("miner_smith")) {
        Check(!life::WantsToHunt(*ms), "miner_smith: not a hunter");
        Check(life::SchoolWeaponFor(*ms) == nullptr,
              "miner_smith: no school weapon to buy");
    }
}

}  // namespace

int main() {
    std::printf("trade_gear\n");
    TestTheWeaponIsOneTheShardSupports();
    TestWhoWantsASword();
    TestTheRefusalsAreSpecific();
    TestTheMaterialHalfIsUnchanged();
    TestSchoolWeaponMapping();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
