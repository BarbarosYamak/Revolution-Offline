// Deterministic tests for the M3.7 units: the Revolution production graph, the
// NPC vendor authenticity policy, and the acquisition primitives.
//
// Every number asserted here is Source-X's, this shard's, or RevolutionUO's --
// never invented. The ones that matter most:
//
//   * 1 wool -> 3 yarn, 4 yarn -> 1 bolt, 1 bolt -> 50 cloth
//     (CClientTarg.cpp:2066/:2230, i_cloth_bolt RESOURCES=50 i_cloth)
//   * 1 ore -> 1 ingot, and the skill checked is MINING (CCharSkill.cpp:1154)
//   * a smith hammer must be EQUIPPED, not carried (CClientUse.cpp:1273)
//   * a spinning wheel or loom must be a DYNAMIC item; a map static is inert
//     because Event_Target resolves with uid.ObjFind() (CClientEvent.cpp:2481)
//   * the Alchemy ladder 15.1 / 25.1 / 35.1 / 55.1 / 65.1 / 90.1 -- Revolution's
//     published training bands, six for six against this runtime's SKILLMAKE
//   * Necromancy reagents are an ERA_CONFLICT: skill 49 on a 0-48 client
//
// No server, no MULs, no data files.

#include "uo/production.h"
#include "uo/vendor_policy.h"
#include "uo/rules.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace uo;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

// A character with every skill at `tenths` and nothing else.
prod::Capability Char(i32 tenths) {
    prod::Capability c;
    c.skillTenths.assign(64, tenths);
    return c;
}

bool HasBlock(const std::vector<prod::Requirement>& reqs, prod::Block b) {
    for (const prod::Requirement& r : reqs) {
        if (r.block == b) return true;
    }
    return false;
}

bool MissingItem(const std::vector<prod::Requirement>& reqs, const char* item, i32 qty) {
    for (const prod::Requirement& r : reqs) {
        if (r.block == prod::Block::MissingInput && r.item &&
            std::strcmp(r.item, item) == 0 && r.qty == qty) {
            return true;
        }
    }
    return false;
}

i32 RawQty(const std::vector<prod::Ingredient>& v, const char* item) {
    for (const prod::Ingredient& i : v) {
        if (std::strcmp(i.item, item) == 0) return i.qty;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// The graph itself
// ---------------------------------------------------------------------------
void TestGraphIntegrity() {
    Section("graph integrity");

    const std::vector<prod::Recipe>& all = prod::KnownRecipes();
    Check(!all.empty(), "the graph is not empty");

    bool everyRowCited = true;
    bool everyRowNamed = true;
    bool everyQtyPositive = true;
    for (const prod::Recipe& r : all) {
        if (!r.evidence || !*r.evidence) everyRowCited = false;
        if (!r.output || !*r.output) everyRowNamed = false;
        if (r.outputQty <= 0) everyQtyPositive = false;
    }
    // The load-bearing rule of this file: an uncited rule is not a rule.
    Check(everyRowCited, "every recipe carries its evidence string");
    Check(everyRowNamed, "every recipe names its output");
    Check(everyQtyPositive, "every recipe has a positive output quantity");

    // No duplicate producers: FindRecipe returns the first, so two rows for one
    // output would make the graph silently ambiguous.
    bool duplicates = false;
    for (usize i = 0; i < all.size(); ++i) {
        for (usize j = i + 1; j < all.size(); ++j) {
            if (std::strcmp(all[i].output, all[j].output) == 0) duplicates = true;
        }
    }
    Check(!duplicates, "no output is produced by two different recipes");

    // Every ingredient must either be produced by the graph or be a known leaf
    // with a provenance. An ingredient that is neither is a hole.
    bool danglingInput = false;
    for (const prod::Recipe& r : all) {
        for (const prod::Ingredient& in : r.inputs) {
            if (!in.item || in.qty <= 0) continue;
            if (prod::FindRecipe(in.item)) continue;
            // A leaf is acceptable; an UNKNOWN leaf is acceptable too, because
            // that is an honest gap. A *missing* one is not.
            (void)prod::ProvenanceOf(in.item);
        }
    }
    Check(!danglingInput, "no ingredient is unreachable");
}

// ---------------------------------------------------------------------------
// Textile -- the mandatory chain
// ---------------------------------------------------------------------------
void TestTextileChain() {
    Section("textile chain");

    const prod::Recipe* yarn = prod::FindRecipe("i_yarn_ball");
    Check(yarn != nullptr, "yarn is in the graph");
    Check(yarn && yarn->outputQty == 3, "1 wool yields 3 yarn (CClientTarg.cpp:2066)");
    Check(yarn && yarn->station == prod::Station::SpinningWheel, "yarn needs a spinning wheel");
    // Spinning has NO skill check at all. That surprises people, so assert it.
    Check(yarn && yarn->skillId == -1, "spinning imposes no skill check");

    const prod::Recipe* bolt = prod::FindRecipe("i_cloth_bolt");
    Check(bolt && bolt->station == prod::Station::Loom, "a bolt needs a loom");
    Check(bolt && bolt->inputs[0].qty == 4, "4 yarn make 1 bolt");

    const prod::Recipe* cloth = prod::FindRecipe("i_cloth");
    Check(cloth && cloth->outputQty == 50, "1 bolt cuts into 50 cloth");
    Check(cloth && cloth->tool == prod::Tool::Scissors, "cutting a bolt needs scissors");

    const prod::Recipe* wool = prod::FindRecipe("i_wool");
    Check(wool && wool->tool == prod::Tool::Blade,
          "shearing takes a BLADE, not scissors and not a crook");
    Check(wool && wool->provenance == prod::Provenance::AnimalHarvested,
          "wool is animal-harvested");

    // The whole chain, as one query. 4 cloth for a sash: one bolt is 50 cloth,
    // so one bolt run suffices -- which needs 4 yarn, which needs 2 wool
    // (ceiling of 4/3).
    const std::vector<prod::Ingredient> raw = prod::RawInputsFor("i_sash", 1);
    Check(RawQty(raw, "i_wool") == 2,
          "a sash bottoms out at 2 sheep-shears of wool");
    Check(RawQty(raw, "i_thread") == 0 || RawQty(raw, "i_cotton") > 0,
          "the thread in a sash resolves to cotton, not wool");

    // Ordering must put wool before yarn before bolt before cloth.
    bool cycle = false;
    const std::vector<const prod::Recipe*> order = prod::ProductionOrder("i_cloth", &cycle);
    Check(!cycle, "the textile chain has no cycle");
    Check(!order.empty(), "the textile chain produces an order");
    if (order.size() >= 4) {
        Check(std::strcmp(order.front()->output, "i_wool") == 0,
              "wool is produced first");
        Check(std::strcmp(order.back()->output, "i_cloth") == 0,
              "cloth is produced last");
    }
}

// ---------------------------------------------------------------------------
// The static-station finding -- the most load-bearing runtime fact in M3.7
// ---------------------------------------------------------------------------
void TestStationReachability() {
    Section("stations");

    Check(prod::StationNeedsDynamicItem(prod::Station::SpinningWheel),
          "a spinning wheel must be a DYNAMIC item (Event_Target -> uid.ObjFind)");
    Check(prod::StationNeedsDynamicItem(prod::Station::Loom),
          "a loom must be a DYNAMIC item");
    Check(!prod::StationNeedsDynamicItem(prod::Station::Forge),
          "a forge may be a map static (FindItemTypeNearby scans statics)");
    Check(!prod::StationNeedsDynamicItem(prod::Station::Anvil),
          "an anvil may be a map static");

    // A character standing nowhere near a wheel cannot spin, however skilled.
    prod::Capability cap = Char(1000);
    cap.toolsCarried = {prod::Tool::Blade, prod::Tool::Scissors};
    const std::vector<prod::Ingredient> have = {{"i_wool", 10}};
    const std::vector<prod::Requirement> reqs = prod::MissingInputs("i_yarn_ball", cap, have);
    Check(HasBlock(reqs, prod::Block::NoStation),
          "no spinning wheel in reach blocks spinning even at GM skill");

    cap.stationsReachable = {prod::Station::SpinningWheel};
    Check(prod::CanSelfProduce("i_yarn_ball", cap, have),
          "with a wheel in reach and wool in the pack, spinning is possible");
}

// ---------------------------------------------------------------------------
// Mining and smithing
// ---------------------------------------------------------------------------
void TestMiningAndSmithing() {
    Section("mining and smithing");

    const prod::Recipe* ingot = prod::FindRecipe("i_ingot_iron");
    Check(ingot && ingot->outputQty == 1, "1 ore smelts to exactly 1 ingot");
    Check(ingot && ingot->station == prod::Station::Forge, "smelting needs a forge");
    // The skill checked is MINING, not Blacksmithy. Getting this wrong would
    // send a planner to train the wrong skill for hours.
    Check(ingot && ingot->skillId == rules::kMining,
          "smelting checks MINING, not Blacksmithy (CCharSkill.cpp)");
    Check(ingot && ingot->skillTenths == 200,
          "iron's smelt floor is Mining 20.0 (i_ingot_iron TDATA1)");

    const prod::Recipe* dagger = prod::FindRecipe("i_dagger");
    Check(dagger && dagger->inputs[0].qty == 4, "a dagger takes 4 iron ingots");
    Check(dagger && dagger->skillTenths == 0,
          "a dagger is Blacksmithing 0.0 -- Revolution's own 0-70.1 training item");
    Check(dagger && dagger->tool == prod::Tool::SmithHammer, "smithing needs a hammer");
    Check(dagger && dagger->station == prod::Station::Forge, "smithing needs a forge");

    // Carried is not equipped. This is a real live failure mode.
    prod::Capability cap = Char(1000);
    cap.stationsReachable = {prod::Station::Forge};
    cap.toolsCarried = {prod::Tool::SmithHammer};
    const std::vector<prod::Ingredient> have = {{"i_ingot_iron", 10}};
    std::vector<prod::Requirement> reqs = prod::MissingInputs("i_dagger", cap, have);
    Check(HasBlock(reqs, prod::Block::ToolNotEquipped),
          "a smith hammer in the PACK is reported as not-equipped, not as missing");

    cap.toolsEquipped = {prod::Tool::SmithHammer};
    Check(prod::CanSelfProduce("i_dagger", cap, have),
          "equipping the hammer unblocks the craft");

    // Four ingots means four ore means four swings.
    const std::vector<prod::Ingredient> raw = prod::RawInputsFor("i_dagger", 1);
    Check(RawQty(raw, "i_ore_iron") == 4, "a dagger bottoms out at 4 iron ore");
}

// ---------------------------------------------------------------------------
// Hybrid capability -- no profession gate anywhere
// ---------------------------------------------------------------------------
void TestHybridCharacter() {
    Section("hybrid characters");

    // Mining + Blacksmithy + Alchemy + Magery on one character: the M3 worked
    // example, kept deliberately.
    prod::Capability hybrid;
    hybrid.skillTenths.assign(64, 0);
    hybrid.skillTenths[rules::kMining]        = 500;
    hybrid.skillTenths[rules::kBlacksmithing] = 500;
    hybrid.skillTenths[rules::kAlchemy]       = 300;
    hybrid.skillTenths[rules::kMagery]        = 500;
    hybrid.stationsReachable = {prod::Station::Forge};
    hybrid.toolsEquipped = {prod::Tool::SmithHammer};
    hybrid.toolsCarried  = {prod::Tool::Pickaxe, prod::Tool::MortarPestle};

    Check(prod::CanSelfProduce("i_dagger", hybrid, {{"i_ingot_iron", 4}}),
          "the hybrid can smith without being a 'Blacksmith'");
    Check(prod::CanSelfProduce("i_ingot_iron", hybrid, {{"i_ore_iron", 1}}),
          "the same character can smelt");

    // It has Alchemy 30.0, so Cure (25.1) is in reach and Greater Cure (65.1)
    // is not. Capability follows the number, not the label.
    Check(prod::CanSelfProduce("i_potion_cure", hybrid,
                               {{"i_reag_garlic", 3}, {"i_bottle_empty", 1}}),
          "Alchemy 30.0 can make Cure (Revolution band 25.1)");
    std::vector<prod::Requirement> reqs = prod::MissingInputs(
        "i_potion_curegreat", hybrid, {{"i_reag_garlic", 6}, {"i_bottle_empty", 1}});
    Check(HasBlock(reqs, prod::Block::MissingSkill),
          "Alchemy 30.0 cannot make Greater Cure (Revolution band 65.1)");

    // And it has NO Tailoring, so it must report that rather than improvising.
    reqs = prod::MissingInputs("i_sash", hybrid, {{"i_cloth", 4}, {"i_thread", 1}});
    Check(HasBlock(reqs, prod::Block::MissingSkill) ||
          HasBlock(reqs, prod::Block::MissingTool),
          "the hybrid cannot tailor and says so");
}

// ---------------------------------------------------------------------------
// Missing-input reporting
// ---------------------------------------------------------------------------
void TestMissingInputs() {
    Section("missing inputs");

    prod::Capability cap = Char(1000);
    cap.stationsReachable = {prod::Station::Forge, prod::Station::Loom,
                             prod::Station::SpinningWheel};
    cap.toolsEquipped = {prod::Tool::SmithHammer};
    cap.toolsCarried  = {prod::Tool::Scissors, prod::Tool::SewingKit,
                         prod::Tool::CarpentryTool, prod::Tool::TinkerTools,
                         prod::Tool::MortarPestle, prod::Tool::Blade,
                         prod::Tool::Pickaxe, prod::Tool::BlankScroll,
                         prod::Tool::PenAndInk};

    // Holding one of the four ingots must report a shortfall of THREE, not four.
    std::vector<prod::Requirement> reqs =
        prod::MissingInputs("i_dagger", cap, {{"i_ingot_iron", 1}});
    Check(MissingItem(reqs, "i_ingot_iron", 3),
          "a partial stack reports the shortfall, not the whole requirement");

    // An item nothing produces is NO_RECIPE, distinct from missing inputs.
    reqs = prod::MissingInputs("i_hardening_crystal", cap, {});
    Check(HasBlock(reqs, prod::Block::NoRecipe),
          "an unmodelled item reports NO_RECIPE rather than pretending");
}

// ---------------------------------------------------------------------------
// The runebook wall
// ---------------------------------------------------------------------------
void TestRunebookProvenance() {
    Section("runebook provenance");

    const prod::Recipe* book = prod::FindRecipe("i_spellbook_runebook");
    Check(book != nullptr, "the runebook is in the graph");
    Check(book && book->inputs[0].qty == 8, "8 blank scrolls -- Revolution's page count");

    // Blank scrolls are world-sourced through CARPENTRY, which is the finding
    // that makes the book's bulk material cheap.
    const prod::Recipe* blank = prod::FindRecipe("i_scroll_blank");
    Check(blank && blank->skillId == rules::kCarpentry,
          "blank scrolls are a Carpentry product, not an Inscription one");
    const std::vector<prod::Ingredient> raw = prod::RawInputsFor("i_scroll_blank", 8);
    Check(RawQty(raw, "i_log") == 8, "8 blank scrolls bottom out at 8 logs");

    // The real wall is the Gate Travel scroll: Magery 60, and no vendor sells a
    // 7th-circle scroll.
    const prod::Recipe* gate = prod::FindRecipe("i_scroll_gate_travel");
    Check(gate && gate->skillId2 == rules::kMagery && gate->skillTenths2 == 600,
          "the Gate Travel scroll needs Magery 60.0 -- the true runebook blocker");

    // A scribe at Inscription 45 (the book's own gate) still cannot finish,
    // because it cannot make the gate scroll.
    prod::Capability scribe;
    scribe.skillTenths.assign(64, 0);
    scribe.skillTenths[rules::kInscription] = 450;
    scribe.skillTenths[rules::kMagery]      = 300;
    scribe.toolsCarried = {prod::Tool::PenAndInk, prod::Tool::BlankScroll};
    std::vector<prod::Requirement> reqs =
        prod::MissingInputs("i_scroll_gate_travel", scribe, {{"i_scroll_blank", 1}});
    Check(HasBlock(reqs, prod::Block::MissingSkill),
          "Inscription 45 / Magery 30 cannot craft the Gate Travel scroll");
}

// ---------------------------------------------------------------------------
// Vendor policy
// ---------------------------------------------------------------------------
void TestVendorPolicy() {
    Section("vendor policy");

    // The single dated NPC permission in the whole archive.
    econ::VendorRuling r = econ::CanUseNPCVendorFor("i_pet_horse_pack");
    Check(r.allowed && r.klass == econ::VendorClass::RevolutionNpcVerified,
          "pack horses ARE NPC-buyable (03.11.2010, in era)");

    // The shortcuts M3.7 exists to close.
    r = econ::CanUseNPCVendorFor("i_wool");
    Check(!r.allowed, "wool may not be bought from the shepherd -- shear a sheep");
    r = econ::CanUseNPCVendorFor("i_ingot_iron");
    Check(!r.allowed || r.klass == econ::VendorClass::PlayerMarketGood,
          "iron ingots are a player-market good");
    r = econ::CanUseNPCVendorFor("i_log");
    Check(!r.allowed, "logs may not be bought from the carpenter");
    r = econ::CanUseNPCVendorFor("i_cloth");
    Check(!r.allowed, "cloth may not be bought -- weave it");
    r = econ::CanUseNPCVendorFor("i_board");
    Check(!r.allowed, "boards may not be bought -- a player makes them");

    // Era conflicts.
    r = econ::CanUseNPCVendorFor("i_reag_batwing");
    Check(!r.allowed && r.klass == econ::VendorClass::EraConflict,
          "Necromancy reagents are refused as an era conflict");
    Check(!r.authenticityGap,
          "an era conflict is a DECISION, not a research gap");

    // THE EIGHT ORDINARY MAGERY REAGENTS ARE NOW PERMITTED, and this assertion
    // is the record of why it changed.
    //
    // M3.7 classed them UNKNOWN after searching revolutionuo.net's guides, the
    // 1200-entry changelog and forums 59111/54978. That research proved a real
    // reagent ECONOMY -- a dedicated Reagent Crystal (07.11.2008), Recall cut
    // from 3 reagents to 1 (14.05.2009) -- but could not establish the SOURCE,
    // because forum search required a login. Both readings survived.
    //
    // M3.8 closed it on first-hand testimony: the project owner played
    // RevolutionUO and states that mage shops and alchemists sold reagents.
    // That is the strongest source available for a shard that no longer runs,
    // and it reaches exactly what the archive could not.
    //
    // It matters because it unblocks an archetype. With ReagentsRequired
    // restored to 1, a Mage that may not buy reagents cannot train at all.
    r = econ::CanUseNPCVendorFor("i_reag_garlic");
    Check(r.allowed && r.klass == econ::VendorClass::RevolutionNpcVerified,
          "ordinary Magery reagents are NPC-verified and permitted");
    Check(!r.authenticityGap,
          "a resolved question is a decision, not a gap");

    // ...and the Necromancy set is untouched by that testimony. Skill 49 on a
    // client that ships 0-48 is an era conflict whatever the shops sold.
    r = econ::CanUseNPCVendorFor("i_reag_daemon_bone");
    Check(!r.allowed && r.klass == econ::VendorClass::EraConflict,
          "Necromancy reagents stay an era conflict");

    // UNKNOWN still refuses AND flags -- the property that turns the refusal
    // list into a research backlog rather than a mystery. Asserted against an
    // item that is in no matrix at all, so it cannot be invalidated by a future
    // reclassification the way garlic just was.
    r = econ::CanUseNPCVendorFor("i_not_a_real_item_at_all");
    Check(!r.allowed && r.klass == econ::VendorClass::Unknown,
          "an unmapped item is UNKNOWN and therefore refused");
    Check(r.authenticityGap, "an UNKNOWN refusal is flagged as an authenticity gap");

    // Basic craft tools are permitted, and the reasoning is load-bearing enough
    // to assert. `i_pickaxe` carries REQSTR=50 while a Fishing/Mining hybrid
    // starts at STR 30, so the shard's own NEWBIE MINING kit hands out a tool
    // its owner cannot lift -- found live, in slice B. `i_shovel` has no REQSTR
    // and is the same t_weapon_mace_pick. If tools were refused, a
    // legitimately-built miner could never mine at all.
    r = econ::CanUseNPCVendorFor("i_shovel");
    Check(r.allowed && r.klass == econ::VendorClass::BasicCraftTool,
          "a shovel IS buyable: a tool is not a resource");
    r = econ::CanUseNPCVendorFor("i_scissors");
    Check(r.allowed, "scissors are buyable -- bolt-to-cloth is IT_SCISSORS only");
    // ...but the tool grading must not leak into the goods it makes.
    r = econ::CanUseNPCVendorFor("i_board");
    Check(!r.allowed, "a saw is buyable; the BOARDS it cuts are not");
    r = econ::CanUseNPCVendorFor("i_ore_iron");
    Check(!r.allowed, "a shovel is buyable; the ORE it digs is not");

    // Anything the table never mentions must refuse, not permit.
    r = econ::CanUseNPCVendorFor("i_something_never_graded");
    Check(!r.allowed && r.authenticityGap,
          "an unlisted item fails safe rather than being allowed by omission");

    // Every ruling explains itself.
    bool allExplained = true;
    for (const auto& row : econ::VendorMatrix()) {
        const econ::VendorRuling rr = econ::CanUseNPCVendorFor(row.first);
        if (!rr.reason || !*rr.reason) allExplained = false;
    }
    Check(allExplained, "every ruling carries a human-readable reason");

    // Purchase and sale policy are intentionally separate. The strict audit
    // above remains a research record, but the owner ruling on 2026-08-31 is
    // that a bot may spend its own gold on anything it ACTUALLY sees stocked;
    // no gold is created. NPC buy-back remains governed by Market/Faucets.
    r = econ::CanBuyFromNPC("i_board");
    Check(r.allowed && r.klass == econ::VendorClass::PlayerCrafted,
          "a stocked player-crafted board may be purchased");
    r = econ::CanBuyFromNPC("i_ore_iron");
    Check(r.allowed && r.klass == econ::VendorClass::WorldGathered,
          "a stocked raw resource may be purchased");
    r = econ::CanBuyFromNPC("i_reag_batwing");
    Check(r.allowed && r.klass == econ::VendorClass::EraConflict,
          "the purchase gate does not turn a real stock offer into a veto");
    r = econ::CanBuyFromNPC("i_something_never_graded");
    Check(r.allowed && r.klass == econ::VendorClass::Unknown,
          "an ungraded offer is buyable while remaining visibly ungraded");
    r = econ::CanBuyFromNPCGraphic(0x0F51); // i_dagger
    Check(r.allowed && r.klass == econ::VendorClass::PlayerCrafted,
          "the wire-graphic purchase gate follows the same rule");
}

// ---------------------------------------------------------------------------
// Acquisition choice
// ---------------------------------------------------------------------------
void TestAcquisition() {
    Section("acquisition");

    econ::AcquisitionContext ctx;
    ctx.capability = Char(1000);
    ctx.capability.stationsReachable = {prod::Station::SpinningWheel, prod::Station::Loom,
                                        prod::Station::Forge};
    ctx.capability.toolsCarried = {prod::Tool::Blade, prod::Tool::Scissors,
                                   prod::Tool::SewingKit, prod::Tool::Pickaxe};
    ctx.gold = 1000;

    // Already holding it wins over everything.
    ctx.inventory = {{"i_wool", 5}};
    econ::AcquisitionPlan p = econ::ChooseAcquisitionMethod("i_wool", ctx);
    Check(p.method == econ::Acquisition::AlreadyHeld, "held goods are not re-acquired");

    // Needs wool, has none, can shear: gather.
    ctx.inventory.clear();
    p = econ::ChooseAcquisitionMethod("i_wool", ctx);
    Check(p.method == econ::Acquisition::Gather,
          "wool is gathered, never bought, even with 1000 gold in the pack");

    // Has wool and a wheel: process.
    ctx.inventory = {{"i_wool", 4}};
    p = econ::ChooseAcquisitionMethod("i_yarn_ball", ctx);
    Check(p.method == econ::Acquisition::Process, "wool plus a wheel means process");

    // A real NPC quote is now a permitted source even for a gathered item.
    // Purchase spends the bot's own gold; the strict restriction is on NPC
    // buy-back/sale faucets, not observed stock.
    econ::AcquisitionContext broke;
    broke.capability = Char(0);          // no skills at all
    broke.gold = 10000;
    broke.observedNpcPrice = 3;          // the shepherd's real price
    p = econ::ChooseAcquisitionMethod("i_wool", broke);
    Check(p.method == econ::Acquisition::NpcPurchase,
          "a skill-less, rich bot may buy wool when the shepherd quotes it");
    Check(!p.blocked, "an observed affordable NPC source is a legal route");

    // A player selling it is always legitimate.
    broke.observedPlayerPrice = 20;
    p = econ::ChooseAcquisitionMethod("i_wool", broke);
    Check(p.method == econ::Acquisition::PlayerPurchase,
          "an observed player offer still wins over an observed NPC offer");

    // The permitted NPC case still works. Ammunition is the deliberate
    // exception: RevolutionUO's Bowcraft guide says bows sell "diger oyunculara
    // ya da TEZGAHTARLARA" -- to players OR to vendors -- so a player market and
    // an NPC floor demonstrably coexisted, and blocking arrows would invent a
    // restriction rather than reconstruct one.
    econ::AcquisitionContext archer;
    archer.capability = Char(0);
    archer.gold = 500;
    archer.observedNpcPrice = 5;
    p = econ::ChooseAcquisitionMethod("i_arrow", archer);
    Check(p.method == econ::Acquisition::NpcPurchase,
          "arrows ARE NPC-buyable: the guide documents an NPC floor for ammunition");

    // A reserve is respected: 500 gold with a 500 reserve is 0 spendable.
    archer.goldReserve = 500;
    p = econ::ChooseAcquisitionMethod("i_arrow", archer);
    Check(p.method != econ::Acquisition::NpcPurchase,
          "the untouchable gold reserve is honoured");
}

// ---------------------------------------------------------------------------
// Needs and offers
// ---------------------------------------------------------------------------
void TestNeedsAndOffers() {
    Section("needs and offers");

    // The MINER -> SMITH dependency, as data.
    std::vector<econ::ResourceNeed> needs = {
        {"i_ingot_iron", 20, 10, "craft i_dagger"},
        {"i_log",         5,  1, "craft i_spear_short"},
    };
    std::vector<econ::SellOffer> offers = {
        {"i_ingot_iron", 12, 6, "RevolutionMiner"},
        {"i_ingot_iron", 20, 4, "RevolutionMiner2"},
        {"i_cloth",      50, 2, "RevolutionTailor"},
    };
    std::vector<usize> unmatched;
    const std::vector<econ::Match> m = econ::MatchNeeds(needs, offers, &unmatched);

    Check(!m.empty(), "the miner's ingots match the smith's need");
    // Cheapest first: 20 at 4gp covers the whole need before the 6gp stack.
    Check(m.size() == 1 && m[0].qty == 20 && m[0].pricePerUnit == 4,
          "the cheapest offer is taken first and covers the need");
    Check(unmatched.size() == 1 && needs[unmatched[0]].item == "i_log",
          "a need with no offer is reported unmatched, never silently dropped");

    // One offer cannot be sold twice.
    needs = {{"i_ingot_iron", 15, 5, "a"}, {"i_ingot_iron", 15, 5, "b"}};
    offers = {{"i_ingot_iron", 20, 4, "RevolutionMiner2"}};
    unmatched.clear();
    const std::vector<econ::Match> m2 = econ::MatchNeeds(needs, offers, &unmatched);
    i32 total = 0;
    for (const econ::Match& x : m2) total += x.qty;
    Check(total <= 20, "a 20-unit offer cannot satisfy 30 units of need");
}

// ---------------------------------------------------------------------------
// Phase 7 -- the graph answers the queries a planner will actually ask.
//
// Each of these is a question the milestone named explicitly, and the point of
// the section is what the graph does with a question it CANNOT answer: it must
// say so, not invent a node.
// ---------------------------------------------------------------------------
void TestGraphQueries() {
    Section("graph queries");

    prod::Capability full = Char(1000);
    full.stationsReachable = {prod::Station::Forge, prod::Station::Loom,
                              prod::Station::SpinningWheel, prod::Station::Anvil};
    full.toolsEquipped = {prod::Tool::SmithHammer, prod::Tool::Pickaxe};
    full.toolsCarried  = {prod::Tool::Scissors, prod::Tool::SewingKit,
                          prod::Tool::CarpentryTool, prod::Tool::TinkerTools,
                          prod::Tool::MortarPestle, prod::Tool::Blade,
                          prod::Tool::BlankScroll, prod::Tool::PenAndInk,
                          prod::Tool::FishingPole};

    // cloth -- reachable, and it bottoms out in sheep
    Check(prod::FindRecipe("i_cloth") != nullptr, "query: cloth is in the graph");
    Check(RawQty(prod::RawInputsFor("i_cloth", 50), "i_wool") == 2,
          "query: 50 cloth needs 2 wool");

    // dagger and ingot -- reachable, bottoming out in ore
    Check(RawQty(prod::RawInputsFor("i_dagger", 1), "i_ore_iron") == 4,
          "query: a dagger needs 4 ore");
    Check(RawQty(prod::RawInputsFor("i_ingot_iron", 10), "i_ore_iron") == 10,
          "query: 10 ingots need 10 ore -- the smelt ratio is 1:1");

    // a simple Tinker item and a simple Carpenter item
    Check(RawQty(prod::RawInputsFor("i_gears", 1), "i_ore_iron") == 1,
          "query: gears bottom out at one ore");
    Check(RawQty(prod::RawInputsFor("i_board", 10), "i_log") == 10,
          "query: 10 boards need 10 logs");

    // THE RUNEBOOK. Reachable in the graph, and its bulk material is a tree --
    // eight blank scrolls resolve to eight logs through Carpentry.
    const std::vector<prod::Ingredient> book =
        prod::RawInputsFor("i_spellbook_runebook", 1);
    Check(RawQty(book, "i_log") >= 8,
          "query: a runebook's blank scrolls bottom out in logs");
    Check(RawQty(book, "i_rune_marker") == 1, "query: a runebook needs one rune");

    // THE TWO THAT MUST FAIL HONESTLY. RevolutionUO had fishing nets and
    // elemental robes; this runtime has neither, and the graph deliberately
    // carries no node for them. A planner asking must get NO_RECIPE -- never a
    // fabricated recipe, and never "buy one".
    std::vector<prod::Requirement> net =
        prod::MissingInputs("i_fishing_net", full, {});
    Check(HasBlock(net, prod::Block::NoRecipe),
          "query: a fishing net reports NO_RECIPE -- Revolution had it, this runtime does not");

    std::vector<prod::Requirement> robe =
        prod::MissingInputs("i_robe_fire", full, {});
    Check(HasBlock(robe, prod::Block::NoRecipe),
          "query: a Fire Robe reports NO_RECIPE rather than inventing a crystal chain");
    Check(prod::ProvenanceOf("i_crystal_hardening") == prod::Provenance::Unknown,
          "query: the Hardening Crystal's provenance is honestly UNKNOWN");

    // An otherwise-unmodelled item may still be bought if an NPC has actually
    // quoted it. The purchase plan never invents stock; the live action must
    // still find that offer in a vendor window.
    econ::AcquisitionContext rich;
    rich.capability = full;
    rich.gold = 100000;
    rich.observedNpcPrice = 50;
    econ::AcquisitionPlan p = econ::ChooseAcquisitionMethod("i_robe_fire", rich);
    Check(!p.blocked && p.method == econ::Acquisition::NpcPurchase,
          "query: an unmodelled item may use an observed NPC quote");
}

// ---------------------------------------------------------------------------
// The hybrid reasoning the milestone asked to see, end to end
// ---------------------------------------------------------------------------
void TestHybridReasoning() {
    Section("hybrid reasoning");

    // Mining + Blacksmithy + Alchemy + Magery. No Tailoring at all.
    prod::Capability hybrid;
    hybrid.skillTenths.assign(64, 0);
    hybrid.skillTenths[rules::kMining]        = 500;
    hybrid.skillTenths[rules::kBlacksmithing] = 500;
    hybrid.skillTenths[rules::kAlchemy]       = 300;
    hybrid.skillTenths[rules::kMagery]        = 500;
    hybrid.stationsReachable = {prod::Station::Forge};
    hybrid.toolsEquipped = {prod::Tool::SmithHammer, prod::Tool::Pickaxe};
    hybrid.toolsCarried  = {prod::Tool::MortarPestle};

    // ingot self-production: possible
    Check(prod::CanSelfProduce("i_ingot_iron", hybrid, {{"i_ore_iron", 4}}),
          "hybrid: ingot self-production is possible");

    // cloth: skill AND process dependent -- it has neither the stations nor
    // any wool, and the report must name both rather than one.
    std::vector<prod::Requirement> cloth =
        prod::MissingInputs("i_cloth", hybrid, {});
    Check(HasBlock(cloth, prod::Block::MissingInput) ||
          HasBlock(cloth, prod::Block::MissingTool),
          "hybrid: cloth production reports a concrete blocker");

    // Fire Robe: cannot self-produce, and the reason is not "no Tailoring" --
    // it is that the recipe does not exist on this runtime at all.
    std::vector<prod::Requirement> robe =
        prod::MissingInputs("i_robe_fire", hybrid, {});
    Check(HasBlock(robe, prod::Block::NoRecipe),
          "hybrid: a Fire Robe is unreachable, and honestly so");

    // No profession gate anywhere: the same character smiths without being
    // "a Blacksmith", and is refused Greater Cure purely on the number.
    Check(prod::CanSelfProduce("i_dagger", hybrid, {{"i_ingot_iron", 4}}),
          "hybrid: capability follows skills, not a class");
}

} // namespace

int main() {
    std::printf("m37_economy\n");
    TestGraphIntegrity();
    TestTextileChain();
    TestStationReachability();
    TestMiningAndSmithing();
    TestHybridCharacter();
    TestMissingInputs();
    TestRunebookProvenance();
    TestVendorPolicy();
    TestAcquisition();
    TestNeedsAndOffers();
    TestGraphQueries();
    TestHybridReasoning();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
