// M7.1 -- producer/consumer separation, non-omniscient prices, gold ledger.
//
// The rule under test is the one that is easiest to break by accident: a bot
// may not know the market. Every price here has to have been SEEN, and a
// character with no observation must answer "I do not know" rather than a
// plausible number.
//
// No server, no MULs, no world data.

#include "uo/faucets.h"
#include "uo/market.h"
#include "uo/vendor_policy.h"
#include "uo/professions.h"
#include "uo/rules.h"

#include <cstdio>
#include <set>
#include <string>

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

// --------------------------------------------------------------------------
void TestInterdependence() {
    Section("chain: the catalogue expresses a real producer -> consumer link");

    const prof::Profession* ms = prof::Find("miner_smith");
    const prof::Profession* mg = prof::Find("scribe");
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    Check(ms && mg && lj, "the three lives exist");
    if (!ms || !mg || !lj) return;

    // A smith consumes ore and produces ingots; that is the chain M7 exists
    // to make real. If nothing in the catalogue links two lives, the shard is
    // a set of solo players with a shared map.
    bool anyLink = false;
    for (const prof::Profession& a : prof::All()) {
        for (const prof::Profession& b : prof::All()) {
            if (&a == &b) continue;
            if (CanSupply(a, b)) anyLink = true;
        }
    }
    Check(anyLink, "at least one profession can supply another");

    // The specific link, named. i_spear_short is 6 ingots plus one LOG
    // (Production.cpp:107), so a smith physically cannot make one without
    // something a lumberjack pulled out of a tree.
    std::string via;
    Check(CanSupply(*lj, *ms, &via),
          "the lumberjack can supply the smith");
    Check(via == "i_log", "and what passes between them is a log");
    Check(!CanSupply(*ms, *lj),
          "the link runs one way -- a swordsman buys its gear, it does not "
          "feed the forge");

    // The lumberjack is deliberately self-sufficient -- it is the control.
    Check(lj->consumes.empty(),
          "the lumberjack needs nothing from another character");
}

// --------------------------------------------------------------------------
void TestSurplusIsOwnOutputOnly() {
    Section("surplus: a life offers only what its own profession makes");

    const prof::Profession* ms = prof::Find("miner_smith");
    Check(ms != nullptr, "the smith exists");
    if (!ms || ms->produces.empty()) return;

    TradePolicy pol;
    pol.keepOfOwnOutput = 20;
    pol.minimumSurplusToOffer = 5;

    // NAME THE GOOD, do not take produces.front(). The working reserve exists
    // only for output a life feeds back into its OWN recipes, so this test is
    // about i_ingot_iron specifically -- every weapon the smith makes eats
    // four to six of them. front() happened to be the ingot until daggers were
    // added on 2026-08-29, whereupon the test silently started asserting about
    // a good with no reserve at all and failed three times over. A test that
    // depends on list ORDER is testing the wrong thing.
    const std::string made = "i_ingot_iron";
    bool makesIt = false;
    for (const std::string& p : ms->produces) makesIt = makesIt || (p == made);
    Check(makesIt, "the smith still makes the good this test is about");

    // Below the reserve: nothing is offered, however much is in the pack.
    std::vector<Stock> pack = {{made, 20}};
    Check(Surplus(*ms, pack, pol).empty(),
          "at exactly the working reserve there is nothing spare");

    pack = {{made, 24}};
    Check(Surplus(*ms, pack, pol).empty(),
          "four over the reserve is under the minimum worth offering");

    pack = {{made, 40}};
    const std::vector<Offer> offers = Surplus(*ms, pack, pol);
    Check(offers.size() == 1, "twenty over the reserve is one offer");
    if (!offers.empty()) {
        Check(offers[0].qty == 20, "and the offer is the surplus, not the pile");
        Check(!offers[0].reason.empty(), "every offer says why it is spare");
    }

    // Carrying something valuable it did NOT make is not a reason to trade in
    // it. This is the check that stops a gatherer becoming a reseller.
    std::vector<Stock> loot = {{"i_reag_black_pearl", 500}};
    Check(Surplus(*ms, loot, pol).empty(),
          "a smith carrying 500 reagents still offers no reagents");
}

// --------------------------------------------------------------------------
void TestShortfallSeparatesWorldFromPlayers() {
    Section("shortfall: a raw resource is not a player supplier's job");

    const prof::Profession* mg = prof::Find("scribe");
    Check(mg != nullptr, "the mage exists");
    if (!mg) return;

    TradePolicy pol;
    pol.restockConsumablesTo = 20;

    const std::vector<Want> wants = Shortfall(*mg, {}, pol);
    Check(!wants.empty(),
          "an empty-packed mage is short of everything it consumes");

    // Reagents: no profession in the catalogue makes them, so a player
    // supplier is impossible and the want must say so rather than sending the
    // character looking for a reagent-crafter that cannot exist.
    bool sawRaw = false;
    for (const Want& w : wants) {
        if (w.item.find("i_reag_") != std::string::npos) {
            sawRaw = true;
            Check(w.rawResource,
                  "a reagent is marked as something no profession produces");
        }
    }
    Check(sawRaw, "the mage's reagents show up as wants");

    // Already stocked -> not a want. Obvious, and the reason a bot does not
    // buy reagents it is already carrying.
    std::vector<Stock> stocked;
    for (const std::string& item : mg->consumes) stocked.push_back({item, 50});
    for (const Want& w : Shortfall(*mg, stocked, pol)) {
        Check(w.item.find("i_reag_") == std::string::npos,
              "a stocked reagent is no longer a want");
    }
}

// --------------------------------------------------------------------------
// A WANT THE PACK CANNOT COUNT IS A WANT THAT NEVER GOES AWAY.
//
// obs.pack is keyed by defname, and a defname is only ever reached through
// econ::ItemNameForGraphicAndHue -- so an item with no row in VendorPolicy's
// kGraphics can never appear in the pack, QtyOf stays 0 forever, and the want
// is re-issued on every tick no matter what the character bought.
// i_potion_poisondeadly is that item: all four poison tiers share
// ID=i_bottle_green and only i_potion_poison is mapped (deliberately, see
// VendorPolicy.cpp), so a fencer asked for twenty deadly poisons permanently.
//
// UNKNOWN, and NOT papered over here: whether a HUE tells the potion tiers
// apart on this shard. Until there is evidence, the want is skipped rather
// than given an invented identity.
void TestShortfallSkipsWhatThePackCannotCount() {
    Section("shortfall: an uncountable want is not a want");

    const prof::Profession* fen = prof::Find("fencer");
    Check(fen != nullptr, "the fencer exists");
    if (!fen) return;

    // The catalogue still says the fencer consumes it -- the skip belongs to
    // the shortfall, not to the profession.
    bool declared = false;
    for (const std::string& c : fen->consumes)
        if (c == "i_potion_poisondeadly") declared = true;
    Check(declared, "the fencer still declares deadly poison as an input");
    Check(uo::econ::GraphicsForItem("i_potion_poisondeadly").empty(),
          "and deadly poison still has no graphic row to count it by");

    TradePolicy pol;
    for (const Want& w : Shortfall(*fen, {}, pol)) {
        Check(w.item != "i_potion_poisondeadly",
              "an empty-packed fencer does not ask for what it cannot count");
    }

    // AND THE SKIP IS NARROW. A want with a real graphic row is untouched --
    // otherwise this fix would have quietly stopped every smith buying wood.
    const prof::Profession* ms = prof::Find("miner_smith");
    Check(ms != nullptr, "the miner-smith exists");
    if (!ms) return;
    bool sawLog = false;
    for (const Want& w : Shortfall(*ms, {}, pol))
        if (w.item == "i_log") sawLog = true;
    Check(sawLog, "an empty-packed smith is still short of logs");
}

// --------------------------------------------------------------------------
// A CONSUMABLE WAS COUNTED BY ITS CATALOGUE LABEL, NOT ITS DEFNAME.
//
// ConsumableNeed::name ("bandage", "heal potion", "food") is prose, chosen
// for FormatSellOffer-style output; there is no itemdef called "heal
// potion". obs.pack is keyed by DEFNAME, resolved through
// econ::ItemNameForGraphic off the wire graphic, the same way the errand
// counts a pack. `QtyOf(pack, c.name)` compared the pack against a string
// that can never appear in it, so `have` stayed 0 forever and every
// consumable was a permanent want no matter how much of it a character was
// actually carrying (flagged 2026-08-30).
void TestConsumablesCountedByGraphicNotLabel() {
    Section("shortfall: a consumable is counted by defname, not by its "
            "catalogue label");

    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    Check(lj != nullptr, "the lumberjack exists");
    if (!lj) return;

    bool declaresBandages = false;
    for (const prof::ConsumableNeed& c : lj->consumables)
        if (c.name == "bandage") declaresBandages = true;
    Check(declaresBandages, "the lumberjack still declares bandages as a "
          "consumable");

    TradePolicy pol;

    // 30 i_bandage -- the DEFNAME the wire and obs.pack actually use
    // (VendorPolicy.cpp kGraphics: 0x0E21 -> i_bandage) -- clears the want,
    // even though the pack has nothing keyed "bandage" (the label).
    const std::vector<Stock> stocked = {{"i_bandage", 30}};
    bool wantsBandagesStocked = false;
    for (const Want& w : Shortfall(*lj, stocked, pol))
        if (w.item == "bandage") wantsBandagesStocked = true;
    Check(!wantsBandagesStocked,
          "30 i_bandage in the pack clears the bandage want entirely");

    // With none at all the want is real -- the fix must not make bandages
    // unwantable, only countable.
    bool wantsBandagesEmpty = false;
    for (const Want& w : Shortfall(*lj, {}, pol))
        if (w.item == "bandage") wantsBandagesEmpty = true;
    Check(wantsBandagesEmpty, "and an empty pack is still short of bandages");
}

// --------------------------------------------------------------------------
void TestWhoProducesIsCatalogueNotMarket() {
    Section("knowledge: knowing a TRADE exists is not knowing the market");

    // "A smith is the sort of person who makes ingots" is knowledge any
    // player has. It carries no information about who is online, where they
    // are, or what they charge -- and this test exists to make that boundary
    // explicit rather than implied.
    const prof::Profession* ms = prof::Find("miner_smith");
    if (!ms || ms->produces.empty()) { Check(false, "smith produces nothing"); return; }

    const auto makers = WhoProduces(ms->produces.front().c_str());
    Check(!makers.empty(), "the catalogue knows which trade makes an ingot");
    Check(WhoProduces("i_something_nobody_makes").empty(),
          "and honestly reports nothing for a thing no trade makes");
    Check(WhoProduces(nullptr).empty(), "a null query is not a crash");
}

// --------------------------------------------------------------------------
void TestPricesMustHaveBeenSeen() {
    Section("prices: a character only knows what it has observed");

    // i_ingot_copper, deliberately NOT i_ingot_iron: this test is about a
    // character with literally nothing to go on, and i_ingot_iron now carries
    // a kForumPriceSeeds row (Market.cpp) -- BelievedSalePrice falls back to
    // that seed once every real observation is exhausted, which is correct
    // behaviour but would make "-1 with nothing observed" false for THAT
    // item. i_ingot_copper has no forum seed, so it still isolates the "zero
    // observations, zero basis" case this test exists to prove. The seed's
    // own behaviour is covered separately in TestForumPriceSeedsGroundFirstOffers.
    PriceBook book;
    Check(book.BelievedSalePrice("i_ingot_copper") == -1,
          "with no observation the answer is -1, NOT a plausible number");
    Check(book.Latest("i_ingot_copper", PriceSource::PlayerTraded) == nullptr,
          "and there is no observation to return");

    PriceObservation npc;
    npc.item = "i_ingot_copper";
    npc.pricePerUnit = 3;
    npc.source = PriceSource::NpcVendorBuys;
    npc.who = "Bertram the blacksmith";
    npc.whenMs = 1000;
    book.Note(npc);
    Check(book.BelievedSalePrice("i_ingot_copper") == 3,
          "an NPC's buy price is a belief when it is all the character has");

    // A player's quote outranks the NPC floor.
    PriceObservation quote = npc;
    quote.pricePerUnit = 8;
    quote.source = PriceSource::PlayerQuoted;
    quote.who = "Doran";
    quote.whenMs = 2000;
    book.Note(quote);
    Check(book.BelievedSalePrice("i_ingot_copper") == 8,
          "a player's quote outranks an NPC's buy price");

    // And a trade that actually happened outranks the quote.
    PriceObservation traded = quote;
    traded.pricePerUnit = 6;
    traded.source = PriceSource::PlayerTraded;
    traded.whenMs = 3000;
    book.Note(traded);
    Check(book.BelievedSalePrice("i_ingot_copper") == 6,
          "a completed trade outranks a quote, even for LESS money -- what "
          "was paid outranks what was claimed");

    // Same item, same source, same seller -> one row that updates.
    PriceObservation again = traded;
    again.pricePerUnit = 7;
    again.whenMs = 4000;
    book.Note(again);
    Check(book.Size() == 3, "one row per (item, source, who)");
    Check(book.BelievedSalePrice("i_ingot_copper") == 7, "and it updates");

    // Stale prices are not knowledge.
    book.Expire(100000, 50000);
    Check(book.Size() == 0, "everything older than the window is dropped");
    Check(book.BelievedSalePrice("i_ingot_copper") == -1,
          "and the character is back to not knowing -- no seed for THIS item "
          "either, so there is truly nothing left to fall back on");
}

// --------------------------------------------------------------------------
void TestGoldLedger() {
    Section("ledger: which flows actually create gold");

    // The anti-arbitrage invariant in one line: only the shard can MAKE gold.
    Check(IsGoldSource(GoldFlow::CreatedPvmLoot), "loot creates gold");
    Check(IsGoldSource(GoldFlow::CreatedVendor),
          "an NPC vendor creates gold -- it pays from nowhere");
    Check(IsGoldSource(GoldFlow::StartingKit),
          "the newbie kit's 1000gp creates gold");
    Check(!IsGoldSource(GoldFlow::TransferPlayerTrade),
          "selling to a PLAYER creates none -- it moves sideways, which is "
          "the entire point of a player economy");
    Check(!IsGoldSource(GoldFlow::DestroyedTrainer), "a trainer fee is a sink");
    Check(!IsGoldSource(GoldFlow::DestroyedVendorPurchase), "so is a purchase");

    Ledger led;
    led.Note(GoldFlow::StartingKit, 1000, "newbie kit", 0);
    led.Note(GoldFlow::DestroyedTrainer, 93, "Evaluating Intelligence", 1000);
    led.Note(GoldFlow::DestroyedTrainer, 108, "Inscription", 2000);
    led.Note(GoldFlow::TransferPlayerTrade, 50, "ingots", 3000);
    led.Note(GoldFlow::TransferPlayerTradeOut, 50, "ingots", 3000);

    Check(led.TotalFor(GoldFlow::DestroyedTrainer) == 201,
          "the two live trainer fees add up: 93 + 108");
    Check(led.TotalIn() == 1050, "in = kit + the player sale");
    Check(led.TotalOut() == 251, "out = both fees + the player purchase");
    Check(led.Net() == 799,
          "which leaves exactly the purse the live mage ended with");

    led.Note(GoldFlow::DestroyedTrainer, 0, "a free lesson", 4000);
    led.Note(GoldFlow::DestroyedTrainer, -5, "impossible", 4000);
    Check(led.TotalFor(GoldFlow::DestroyedTrainer) == 201,
          "zero and negative amounts are not entries");
}


// --------------------------------------------------------------------------
void TestReserveOnlyForOwnInputs() {
    Section("surplus: the reserve protects tomorrow's work, nothing else");

    const prof::Profession* ms = prof::Find("miner_smith");
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    Check(ms && lj, "the smith and the lumberjack exist");
    if (!ms || !lj) return;

    TradePolicy pol;
    pol.keepOfOwnOutput = 20;
    pol.minimumSurplusToOffer = 5;

    // A smith's ingots feed its own spear recipe (6 ingots + 1 log), so the
    // reserve applies: selling all 25 would leave it unable to smith.
    const std::vector<Offer> smith = Surplus(*ms, {{"i_ingot_iron", 25}}, pol);
    Check(smith.size() == 1, "25 ingots is one offer");
    if (!smith.empty()) {
        Check(smith[0].qty == 5, "and only 5 of them, holding 20 back to work");
    }

    // A lumberjack's logs feed NOTHING it makes. Holding 20 back would be 20
    // logs it never sells and never uses -- which is what the first version
    // of this rule did, because it keyed off the catalogue's `consumes`
    // field. That field means "obtain from someone else", so a smith's own
    // ingots are correctly absent from it even though every weapon eats six.
    // The lumberjack is a CARPENTER too now (owner: "one character,
    // lumberjack carpenter same guy crafter"), so its logs feed its own board
    // recipe and the reserve applies to them as well. Before that change it
    // held nothing back, correctly, because it had no use for a log.
    const std::vector<Offer> woodcutter = Surplus(*lj, {{"i_log", 25}}, pol);
    Check(woodcutter.size() == 1, "25 logs is one offer");
    if (!woodcutter.empty()) {
        Check(woodcutter[0].qty == 5,
              "holding 20 back, because a carpenter needs logs to make boards");
    }

    // Something it makes and does NOT feed back in: no reserve.
    const std::vector<Offer> boards = Surplus(*lj, {{"i_board", 25}}, pol);
    Check(boards.size() == 1 && boards[0].qty == 25,
          "boards are an output, not an input, so all 25 are spare");
}

// --------------------------------------------------------------------------
void TestNoClosedVendorLoop() {
    Section("selling: an NPC may not be both ends of the same cycle");

    const prof::Profession* ms = prof::Find("miner_smith");
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    Check(ms && lj, "the two lives exist");
    if (!ms || !lj) return;

    // Clean ledger: the smith gathered its own ore, so selling ingots is a
    // real sale backed by real time in a mountain.
    Ledger clean;
    clean.Note(GoldFlow::StartingKit, 1000, "newbie kit", 0);

    // Ingots are a PLAYER-MARKET good on Revolution, so this is refused on the
    // sell-class gate before the ledger is ever consulted. It used to be
    // allowed here, and that was the defect the project owner caught.
    const SellRuling ingots = MaySellToNpc(*ms, "i_ingot_iron", clean);
    Check(!ingots.allowed, "a smith may not sell ingots to an NPC");
    Check(ingots.reason != nullptr, "and the ruling says why");

    // The arbitrage guard is tested on a good that DOES have a tap, so the
    // two gates are exercised separately rather than one masking the other.
    const prof::Profession* mage = prof::Find("scribe");
    Check(mage != nullptr, "the mage exists");
    if (!mage) return;
    Check(MaySellToNpc(*mage, "i_scroll_poison", clean).allowed,
          "a scribe MAY sell a scroll it wrote -- one of the three taps");

    // Now it bought the blank scroll from an NPC. This used to be refused as a
    // vendor -> inscribe -> vendor loop, and the assertion here said so. The
    // premise was wrong, not the code: Revolution's own players describe that
    // exact route -- "people were scribing scroll and selling scrolls to
    // vendor make money" -- and scribing is one of the three named taps where
    // gold enters this shard. What made it LOOK like arbitrage was our
    // pricing: a stock blank scroll cost 10 against a 14-gold scroll. The
    // forum puts a blank at 6 (topic 88176), and the itemdefs now agree.
    //
    // So the loop test is scoped to routes the archive does not cover, and a
    // CONFIRMED faucet is sold even from bought inputs. The margin pays for
    // the skill and the time.
    Ledger dirty = clean;
    dirty.Note(GoldFlow::DestroyedVendorPurchase, 40, "i_scroll_blank", 1000);
    const SellRuling documented = MaySellToNpc(*mage, "i_scroll_poison", dirty);
    Check(documented.allowed,
          "a documented faucet survives NPC-bought inputs");

    // THE GUARD STILL HAS TO BITE where the archive is silent, or scoping it
    // would just have disabled it. The recall scroll is the case: allowed as a
    // faucet, but its history is only NOT_FULLY_CONFIRMED, so buying the blank
    // from a vendor and selling the written scroll back to one is exactly the
    // loop this test exists to catch. Same character, same shop, same bought
    // input as the poison scroll above -- only the strength of the evidence
    // differs, which is the whole point.
    const SellRuling undocumented = MaySellToNpc(*mage, "i_scroll_recall", dirty);
    Check(!undocumented.allowed,
          "an UNdocumented route from NPC-bought inputs is still refused");
    Check(undocumented.reason != nullptr, "and the refusal says why");

    // Buying something UNRELATED does not poison the sale.
    Ledger unrelated = clean;
    unrelated.Note(GoldFlow::DestroyedVendorPurchase, 30, "i_bandage", 1000);
    Check(MaySellToNpc(*mage, "i_scroll_poison", unrelated).allowed,
          "buying bandages does not block selling a scroll");

    // A life may not sell what it does not make, however much it carries.
    Check(!MaySellToNpc(*lj, "i_ingot_iron", clean).allowed,
          "a lumberjack may not sell ingots -- it is not a fence");
    Check(!MaySellToNpc(*ms, nullptr, clean).allowed,
          "a null item is refused, not a crash");

    // A trainer fee is a SINK, not a purchase of inputs, so it must not
    // block anything.
    Ledger trained = clean;
    trained.Note(GoldFlow::DestroyedTrainer, 108, "i_scroll_blank", 1000);
    Check(MaySellToNpc(*mage, "i_scroll_poison", trained).allowed,
          "only a PURCHASE poisons the cycle, not any ledger entry that "
          "happens to name the same item");
}


// --------------------------------------------------------------------------
void TestNpcsMayNotBuyPlayerMarketGoods() {
    Section("selling: NPCs do not set a floor under a player-market good");

    // REVOLUTION, stated by the project owner: you sold logs to PLAYERS, not
    // to a carpenter. The stock Sphere scripts disagree -- VENDOR_B_CARPENTER
    // carries BUY=i_log,{5 15} (tm_vend.scp:167) -- and that disagreement is
    // the whole reason the M3.7 vendor matrix exists.
    //
    // M3.7 built that matrix to stop bots BUYING player-craftable goods from
    // NPCs. The same reasoning runs in reverse and was missed for a while: an
    // NPC that BUYS a gathered resource at a fixed price sets a floor under
    // it, and no player pays more than the floor for something they can dump
    // at a vendor. The player market dies either way.
    Check(econ::ClassifyForVendor("i_log") == econ::VendorClass::WorldGathered,
          "the matrix grades a log WorldGathered");
    Check(econ::ClassifyForVendor("i_ingot_iron") ==
              econ::VendorClass::PlayerMarketGood,
          "and an iron ingot PlayerMarketGood");

    // WHAT THESE FOUR NOW MEAN, since the 2026-09-02 restore. The runtime's
    // tm_vend.scp DOES carry live BUY=i_log (:293/:1085/:1438/:1632/:2114) and
    // BUY=i_ingot_iron (:1084/:1421/:1506/:2115) rows, and kNpcBuyers now
    // records them. These calls take the DEFAULT playersDeclined=false, i.e.
    // the player-first WTS window is still open -- and while it is open the
    // policy answers "no NPC", exactly as the owner ruled. The refusal is the
    // sale policy, no longer an empty vendor table. See
    // TestNpcPriceFloorBuyerResolution for the window-closed half.
    Check(NpcBuyersFor("i_log").empty(),
          "no npc buyer for logs while the player-first window is open");
    Check(NpcBuyersFor("i_ingot_iron").empty(),
          "nor for ingots");
    Check(!HasNpcBuyer("i_log"), "HasNpcBuyer agrees");
    Check(NpcBuyersFor(nullptr).empty(), "a null query is not a crash");
    // ...and the counters are demonstrably THERE, which is the half that was
    // wrong before. Both flip once nobody answered the offer.
    Check(HasNpcBuyer("i_log", /*playersDeclined=*/true),
          "logs DO have a live NPC counter once the window closes");
    Check(HasNpcBuyer("i_ingot_iron", /*playersDeclined=*/true),
          "and so do iron ingots");

    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    const prof::Profession* ms = prof::Find("miner_smith");
    Check(lj && ms, "the two gathering lives exist");
    if (!lj || !ms) return;

    Ledger clean;
    const SellRuling logs = MaySellToNpc(*lj, "i_log", clean);
    Check(!logs.allowed, "a lumberjack may not sell its logs to an NPC");
    Check(logs.reason != nullptr, "and the refusal says why");

    const SellRuling ingots = MaySellToNpc(*ms, "i_ingot_iron", clean);
    Check(!ingots.allowed, "nor a smith its ingots");

    // The consequence, stated so it is not mistaken for a bug: a gatherer
    // with no player buyer stays resource-rich and wealth-poor. That is a
    // legitimate state on this shard and the reason M7's real target is
    // player-to-player trade, not a vendor errand.
}

// --------------------------------------------------------------------------
void TestWhatAnNpcMayStillBuy() {
    Section("selling: the narrow case an NPC may still be used for");

    // Only a DATED Revolution entry opens the door. Reagents are the class
    // that has one, which is why a mage buying reagents from a mage shop is
    // era behaviour and a lumberjack dumping logs on a carpenter is not.
    Check(econ::ClassifyForVendor("i_reag_black_pearl") ==
              econ::VendorClass::RevolutionNpcVerified,
          "reagents carry a dated Revolution NPC entry");

    // WHICH LIVES HAVE AN NPC INCOME, computed rather than asserted from
    // memory, because the answer has moved three times.
    Ledger clean;
    std::set<std::string> withNpcIncome;
    for (const prof::Profession& p : prof::All()) {
        for (const std::string& made : p.produces) {
            if (MaySellToNpc(p, made.c_str(), clean).allowed) {
                withNpcIncome.insert(p.id);
                break;
            }
        }
    }

    // ONLY the scribe, from scrolls. The registry refuses smith, carpentry,
    // tailoring, tinkering and alchemy output as NPC faucets -- the stock
    // templates buy all of them, and that is a fact about Sphere.
    //
    // This said "mage" until 2026-08-29, and the mage was carrying the
    // scribe's identity: it produced scrolls, drew a crafting income and had
    // Inscription in its build. Scroll-writing is what a SCRIBE is, and the
    // scribe profession already existed -- so the mage is now pure (Magery,
    // Meditation, Eval Int; income from loot) and the scroll faucet belongs
    // to the profession that actually writes them.
    //
    // THE ASYMMETRY IS THE POINT. A smith with no NPC faucet has to reach
    // players, hunt, or find another route, and that is the intended shape of
    // a shard economy rather than a gap to be filled in.
    Check(withNpcIncome.count("scribe") == 1,
          "a scribe has an NPC income -- live-proven on this shard");
    // CHANGED 2026-08-29 by owner decision: "brannoc he can make gold by
    // selling daggers he crafted, go mine then forge then make dagger and sell
    // them". A smith now HAS a narrow NPC faucet.
    //
    // The concern this assertion was written to protect is real and has not
    // gone away -- mine -> smith -> dump to NPC does print gold -- so what is
    // checked now is the SHAPE of the exception rather than its absence:
    // exactly one smith good is sellable, it is the day-one one
    // (SKILLMAKE=Blacksmithing 0.0), and the rest of the smith's output is
    // still refused. If that count ever grows quietly, this fails.
    Check(withNpcIncome.count("miner_smith") == 1,
          "a smith now has ONE narrow NPC faucet: the dagger the owner ruled "
          "on, and nothing else");
    Check(withNpcIncome.count("alchemist") == 1,
          "an alchemist now has one too -- poison, by owner decision, over "
          "CONFIRMED evidence that potions were player goods. The evidence is "
          "kept in the registry entry and the price (3 gold) is what keeps it "
          "a training sink rather than an income loop");
    Check(withNpcIncome.count("lumberjack_swordsman") == 0,
          "nor the carpenter, whose market is players");

    // The club, named, because this row REVERSES an earlier allowance in this
    // repository and the reversal should fail loudly if it is undone.
    const prof::Profession* ljp = prof::Find("lumberjack_swordsman");
    if (ljp) {
        const SellRuling club = MaySellToNpc(*ljp, "i_club", clean);
        Check(!club.allowed, "a carpenter's club may NOT be dumped on an NPC");
        Check(club.refusal == faucet::Refusal::RevolutionAuthenticityUnknown,
              "and the refusal names WHICH kind of no it is");
        Check(club.via != nullptr, "traceable to the registry row that decided it");
    }

    // Every refusal must carry a typed reason. "cannot earn gold" is never an
    // acceptable answer when the truth is "this is a player-market good".
    const prof::Profession* ms2 = prof::Find("miner_smith");
    if (ms2) {
        const SellRuling ingot = MaySellToNpc(*ms2, "i_ingot_iron", clean);
        Check(ingot.refusal == faucet::Refusal::PlayerMarketGood,
              "an ingot is refused as a PLAYER MARKET GOOD, not vaguely");
        Check(ingot.reason != nullptr && ingot.reason[0] != 0,
              "with prose a human can read");
    }

    Check(ClassifyForNpcSale("i_log") == NpcSellClass::RawResource,
          "a log is a raw resource, not a tap");
    Check(ClassifyForNpcSale("i_ingot_iron") == NpcSellClass::PlayerMarketGood,
          "an ingot is a player-market good, not a tap");
    Check(ClassifyForNpcSale("i_nothing_at_all") == NpcSellClass::Unknown,
          "and anything unlisted is UNKNOWN, which refuses");
}

// --------------------------------------------------------------------------
void TestArbitrageGuardStillApplies() {
    Section("selling: the closed-loop guard sits behind the policy gate");

    // Both gates have to hold. The policy decides whether the good has an NPC
    // price at all; the ledger decides whether THIS character earned it. A
    // hypothetical NPC-verified good bought from a vendor and sold back is
    // still refused, which is the 66-loop case from economy_arbitrage.py.
    const prof::Profession* mg = prof::Find("scribe");
    Check(mg != nullptr, "the mage exists");
    if (!mg) return;

    Ledger dirty;
    dirty.Note(GoldFlow::DestroyedVendorPurchase, 40, "i_reag_nightshade", 1000);
    // A scribed scroll IS an NPC good on this shard -- see the note in
    // TestArbitrage above and the faucet registry's own row. This assertion
    // used to read the other way round, on a premise the forum record
    // refutes.
    const SellRuling r = MaySellToNpc(*mg, "i_scroll_poison", dirty);
    Check(r.allowed, "a scribed scroll is one of the three gold taps");

    Check(!MaySellToNpc(*mg, "i_log", dirty).allowed,
          "and a life still may not sell what it does not produce");
}

// --------------------------------------------------------------------------
void TestSpokenOffers() {
    Section("trade: an offer is something you SAY, and might mishear");

    // The wire format is a spoken line on purpose. There is no shard-wide
    // want list to query -- a character learns somebody wants boards by
    // HEARING them say so, and one out of earshot simply does not know.
    TradeIntent t;
    t.item = "i_board"; t.qty = 20; t.pricePerUnit = 4;
    const std::string said = FormatSellOffer(t);
    Check(said == "WTS 20 i_board 4gp", "the offer reads like a player's WTS");

    TradeIntent heard;
    Check(ParseSellOffer(said, &heard), "and parses back");
    Check(heard.item == "i_board" && heard.qty == 20 &&
          heard.pricePerUnit == 4, "to the same deal");
    Check(heard.Total() == 80, "with a total anybody can check");

    // Heard inside ordinary chatter, and in the wrong case.
    Check(ParseSellOffer("Galrin: wts 5 i_log 2gp anyone?", &heard),
          "an offer buried in a sentence is still an offer");
    Check(heard.qty == 5 && heard.item == "i_log", "and reads correctly");

    // Things that are NOT offers. Most of what a character hears is not
    // addressed to it, so the common case must be a clean false.
    Check(!ParseSellOffer("hello there", &heard), "chatter is not an offer");
    Check(!ParseSellOffer("WTS", &heard), "a bare keyword is not an offer");
    Check(!ParseSellOffer("WTS lots of boards", &heard),
          "no quantity, no deal");
    Check(!ParseSellOffer("WTS 20 i_board", &heard),
          "no price, no deal -- a bot must not fill in a number for a seller");
    Check(!ParseSellOffer("", &heard), "an empty line is not an offer");
    Check(!ParseSellOffer(said, nullptr), "a null out is refused, not a crash");

    std::string item;
    Check(ParseBuyReply(FormatBuyReply("i_board"), &item) && item == "i_board",
          "a buy reply round-trips");
    Check(!ParseBuyReply("WTS 20 i_board 4gp", &item),
          "a sell offer is not mistaken for a buy reply");
}

// --------------------------------------------------------------------------
void TestWhatToAnnounce() {
    Section("trade: a seller announces only what NPCs refuse, at a seen price");

    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    Check(lj != nullptr, "the carpenter exists");
    if (!lj) return;

    TradePolicy pol;
    PriceBook empty;
    TradeIntent out;

    // 40 boards and no idea what a board is worth. It announces anyway, at
    // the OPENING ASK.
    //
    // The first version of this stayed silent, and that was wrong in a way
    // worth recording: every price in a PriceBook arrives by a trade
    // completing, no trade completes until somebody names a number, so a fleet
    // where nobody will ever name one has no market at all. Refusing to invent
    // a BELIEF is right; refusing to make an OFFER is paralysis.
    const std::vector<Stock> boards = {{"i_board", 40}};
    Check(ChooseSellOffer(*lj, boards, empty, pol, &out),
          "with no price seen it still makes an opening offer");
    Check(out.pricePerUnit == pol.openingAsk,
          "at the opening ask, which is deliberately low");
    Check(empty.BelievedSalePrice("i_board") == -1,
          "and its BELIEF is still -1: an offer is not knowledge");

    // Now it has heard one.
    PriceBook book;
    PriceObservation po;
    po.item = "i_board"; po.pricePerUnit = 4;
    po.source = PriceSource::PlayerTraded; po.who = "Aeryn"; po.whenMs = 1000;
    book.Note(po);
    Check(ChooseSellOffer(*lj, boards, book, pol, &out),
          "with a price it has seen, it announces");
    Check(out.item == "i_board" && out.pricePerUnit == 4,
          "at the price it saw, not a markup it invented");

    // A SCROLL has an allowed NPC route, so the player market need not carry
    // it -- that is a shorter errand and the announcement would be noise.
    const prof::Profession* mg2 = prof::Find("scribe");
    if (mg2) {
        PriceBook scrollBook;
        PriceObservation cp;
        cp.item = "i_scroll_poison"; cp.pricePerUnit = 12;
        cp.source = PriceSource::NpcVendorBuys; cp.who = "mage"; cp.whenMs = 1;
        scrollBook.Note(cp);
        const std::vector<Stock> scrolls = {{"i_scroll_poison", 40}};
        TradeIntent unused;
        Check(!ChooseSellOffer(*mg2, scrolls, scrollBook, pol, &unused),
              "what an NPC will buy is not announced to players");
    }
}

// --------------------------------------------------------------------------
void TestWhetherToAnswer() {
    Section("trade: a buyer answers only for what its own life needs");

    const prof::Profession* ms = prof::Find("miner_smith");
    const prof::Profession* mg = prof::Find("scribe");
    Check(ms && mg, "the smith and the mage exist");
    if (!ms || !mg) return;

    TradePolicy pol;
    TradeIntent logs;
    logs.item = "i_log"; logs.qty = 20; logs.pricePerUnit = 2;

    // A smith needs logs -- i_spear_short is 6 ingots plus one log.
    const BuyDecision yes = ConsiderOffer(*ms, {}, 1000, pol, logs);
    Check(yes.accept, "a smith answers an offer of logs");
    Check(yes.qty > 0 && yes.qty <= logs.qty, "for no more than is on offer");
    Check(yes.reason != nullptr, "and says why");

    // A mage has no use for a log. This is what stops a fleet becoming a room
    // full of speculators buying whatever is cheap.
    const BuyDecision no = ConsiderOffer(*mg, {}, 1000, pol, logs);
    Check(!no.accept, "a mage does not buy logs");
    Check(no.reason != nullptr, "and says why not");

    // Already stocked -> no longer a want.
    std::vector<Stock> full;
    for (const std::string& c : ms->consumes) full.push_back({c, 500});
    Check(!ConsiderOffer(*ms, full, 1000, pol, logs).accept,
          "a smith with a full stock of logs stops buying them");

    // Broke.
    Check(!ConsiderOffer(*ms, {}, 5, pol, logs).accept,
          "a character that cannot afford it declines");

    // The reserve is not spendable: it is what buys a replacement tool after
    // a death, and trading it away leaves the character unemployable.
    const i32 justOverReserve = ms->goldReserve + 10;
    Check(!ConsiderOffer(*ms, {}, justOverReserve, pol, logs).accept,
          "and it will not eat into the tool reserve to buy stock");

    // A greedy seller. Without a ceiling a bot with a full purse accepts any
    // number, and one seller drains the fleet.
    TradeIntent gouge = logs;
    gouge.pricePerUnit = 500;
    Check(!ConsiderOffer(*ms, {}, 100000, pol, gouge).accept,
          "an absurd price is refused however rich the buyer is");

    TradeIntent junk;
    Check(!ConsiderOffer(*ms, {}, 1000, pol, junk).accept,
          "a malformed offer is refused, not acted on");
}

// --------------------------------------------------------------------------
// FORUM PRICE SEEDS -- the reported bug in one test: Tarath sold i_log at
// TradePolicy::openingAsk (2gp) all night while
// docs/FORUM_SWEEP_2026_08_30.md's own players priced it at 17. A seed lets
// BelievedSalePrice answer with real Revolution evidence instead of -1 when
// a character has made no observation of its own, and it changes
// ConsiderOffer's ceiling from a flat "never seen this, so barely anything"
// number into an honest ±50% band around what is actually known.
void TestForumPriceSeedsGroundFirstOffers() {
    Section("prices: a forum-seeded good is not sold or bought blind");

    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    const prof::Profession* ms = prof::Find("miner_smith");
    const prof::Profession* mg = prof::Find("scribe");
    Check(lj && ms && mg, "the lumberjack, the smith and the scribe exist");
    if (!lj || !ms || !mg) return;

    TradePolicy pol;
    PriceBook empty;

    // SELLER, no observation of its own.
    const std::vector<Stock> logs60 = {{"i_log", 60}};
    TradeIntent out;
    Check(ChooseSellOffer(*lj, logs60, empty, pol, &out),
          "with no observation it still announces");
    Check(out.pricePerUnit == 17,
          "at the forum-seeded price (17gp), not the blind opening ask of 2");
    Check(empty.BelievedSalePrice("i_log") == 17,
          "and BelievedSalePrice reports that same seed");

    // BUYER, with the seed present: the smith is no longer blind, so it will
    // pay up to 50% over the seed even though 17 is above the flat
    // blindPriceCeiling of 12.
    TradeIntent atSeed;
    atSeed.item = "i_log"; atSeed.qty = 20; atSeed.pricePerUnit = 17;
    Check(ConsiderOffer(*ms, {}, 1000, pol, atSeed).accept,
          "the buyer accepts 17gp for logs even though that is above the "
          "flat blindPriceCeiling of 12 -- a seed makes it an informed "
          "price, not a blind one");

    // But a seller asking multiples of the seeded price is still refused: 40
    // is more than 17 * 1.5 (25).
    TradeIntent overSeed = atSeed;
    overSeed.pricePerUnit = 40;
    Check(!ConsiderOffer(*ms, {}, 1000, pol, overSeed).accept,
          "40gp is refused -- more than 50% over the seeded price");

    // UNSEEDED goods are untouched on BOTH sides.
    //
    // Sell side: i_board has no forum row, so the opening ask is still the
    // blind default.
    const std::vector<Stock> boards40 = {{"i_board", 40}};
    TradeIntent boardOut;
    Check(ChooseSellOffer(*lj, boards40, empty, pol, &boardOut),
          "an unseeded good still gets an opening offer");
    Check(boardOut.pricePerUnit == pol.openingAsk,
          "at the blind opening ask -- there is no seed for a board");

    // Buy side: i_reag_black_pearl has no forum row either, so the flat
    // blindPriceCeiling (12) still governs exactly as before -- 12 is fine,
    // 13 is refused, even though 13 would pass a 50%-over-seed test if this
    // item DID have a seed. Well clear of the scribe's own 5000gp reserve
    // (Professions.cpp) -- otherwise a refusal here would be the reserve
    // guard firing, not the ceiling this assertion is actually about.
    const i32 scribeRich = mg->goldReserve + 1000;
    TradeIntent reagentOk;
    reagentOk.item = "i_reag_black_pearl"; reagentOk.qty = 5;
    reagentOk.pricePerUnit = 12;
    Check(ConsiderOffer(*mg, {}, scribeRich, pol, reagentOk).accept,
          "an unseeded good at exactly the flat ceiling is still accepted");

    TradeIntent reagentTooMuch = reagentOk;
    reagentTooMuch.pricePerUnit = 13;
    Check(!ConsiderOffer(*mg, {}, scribeRich, pol, reagentTooMuch).accept,
          "and one gold piece over the flat ceiling is still refused -- "
          "unseeded behaviour is unchanged");
}

// --------------------------------------------------------------------------
void TestTheChainCanActuallyClose() {
    Section("trade: the lumberjack -> smith chain has both ends");

    // The M7 claim in one test. A carpenter with surplus logs and an observed
    // price announces; a smith that needs logs answers. If either half fails
    // the fleet is a set of solo players sharing a map.
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    const prof::Profession* ms = prof::Find("miner_smith");
    Check(lj && ms, "both lives exist");
    if (!lj || !ms) return;

    PriceBook book;
    PriceObservation po;
    po.item = "i_log"; po.pricePerUnit = 2;
    po.source = PriceSource::PlayerTraded; po.who = "Dorthor"; po.whenMs = 1;
    book.Note(po);

    TradePolicy pol;
    TradeIntent announced;
    const std::vector<Stock> logs = {{"i_log", 60}};
    Check(ChooseSellOffer(*lj, logs, book, pol, &announced),
          "the carpenter announces its spare logs");
    Check(announced.item == "i_log", "and it is logs it is offering");

    // The smith hears the SPOKEN LINE -- not a struct handed to it.
    TradeIntent heard;
    Check(ParseSellOffer(FormatSellOffer(announced), &heard),
          "the smith parses what it heard");
    const BuyDecision d = ConsiderOffer(*ms, {}, 1000, pol, heard);
    Check(d.accept, "and takes the deal");
    Check(d.qty > 0, "for a real quantity");
}

}  // namespace

// M7 -- THE DISPOSAL ORDER for what a life will not wear.
//
// "mage wears only mage equipment, sell the rest -- studded is ok -- to
// players first, NPC only if nobody buys" (project owner). Three steps, in
// order, and the third one is gated by the Gold Faucet Registry rather than by
// convenience -- which today means looted armour is BANKED, because
// monster_loot_resale is Policy::Unknown.
void TestTheDisposalOrder() {
    std::printf("[disposal: wear it, then offer it, then -- only if the "
                "registry says so -- sell it]\n");
    const prof::Profession* mage = prof::Find("mage");
    if (!mage) { std::printf("  FAIL: no mage in the catalogue\n"); ++g_failures; return; }
    market::Ledger empty;

    // 1. It fits: wear it, and no further question is asked.
    {
        const market::DisposalRuling r =
            market::DisposeOfGear(*mage, "i_leather_tunic", true, false, empty);
        if (r.what != market::Disposal::Wear) {
            std::printf("  FAIL: something wearable was not worn (%s)\n",
                        market::DisposalName(r.what));
            ++g_failures;
        }
        ++g_checks;
    }

    // 2. It does not fit: the players hear about it FIRST. This is the step
    //    that inverts the ordinary surplus rule, where an NPC buyer means the
    //    player market is skipped as the longer errand.
    {
        const market::DisposalRuling r =
            market::DisposeOfGear(*mage, "i_platemail_gorget", false, false, empty);
        if (r.what != market::Disposal::OfferToPlayers) {
            std::printf("  FAIL: unwearable gear went to %s before the players "
                        "were asked\n", market::DisposalName(r.what));
            ++g_failures;
        }
        ++g_checks;
    }

    // 3a. Nobody wanted it, and no established NPC route exists for armour.
    //     It is BANKED. Refusing to dump it is the point: gold from an NPC is
    //     new gold, and this route is UNKNOWN, not merely unprofitable.
    {
        const market::DisposalRuling r =
            market::DisposeOfGear(*mage, "i_platemail_gorget", false, true, empty);
        if (r.what != market::Disposal::Bank) {
            std::printf("  FAIL: looted armour was %s -- the registry does not "
                        "establish that route\n", market::DisposalName(r.what));
            ++g_failures;
        }
        ++g_checks;
    }

    // 3b. And where the registry DOES establish a route, step 3 is reached.
    //     i_bow carries Policy::Allow on Revolution's own Bowcraft guidance.
    {
        const market::DisposalRuling r =
            market::DisposeOfGear(*mage, "i_bow", false, true, empty);
        if (r.what != market::Disposal::SellToNpc || !r.via) {
            std::printf("  FAIL: an item with an ALLOWED faucet route was %s\n",
                        market::DisposalName(r.what));
            ++g_failures;
        }
        ++g_checks;
    }

    // 3c. Unless this very character bought it from an NPC to begin with --
    //     then selling it back is a vendor loop and the box is the answer.
    {
        market::Ledger bought;
        bought.Note(market::GoldFlow::DestroyedVendorPurchase, 40, "i_bow", 0);
        const market::DisposalRuling r =
            market::DisposeOfGear(*mage, "i_bow", false, true, bought);
        if (r.what != market::Disposal::Bank) {
            std::printf("  FAIL: an item bought from a vendor was %s back to "
                        "one\n", market::DisposalName(r.what));
            ++g_failures;
        }
        ++g_checks;
    }

    // Every ruling names its reason. A silent refusal is the thing this whole
    // layer exists to avoid.
    for (bool declined : {false, true}) {
        const market::DisposalRuling r =
            market::DisposeOfGear(*mage, "i_platemail_gorget", false, declined, empty);
        if (!r.reason || !*r.reason) {
            std::printf("  FAIL: a disposal ruling carried no reason\n");
            ++g_failures;
        }
        ++g_checks;
    }
}

// --------------------------------------------------------------------------
// WHERE A MISSING INPUT COMES FROM, when no NPC shopkeeper sells it.
//
// The 2026-09-01 30-bot wave failed BUY_SUPPLIES with REFUSE_NO_KNOWN_BUYER on
// three characters that were each holding the wrong end of their own supply
// chain (run_gates/g_Dorvar.console.txt:740, g_Zarthal:488, g_Titus:636).
// Refusing the NPC was correct -- materials never come from shopkeepers -- but
// "no vendor" is not "no source", and the route each of them actually needed
// is decidable from the catalogue alone.
void TestSupplyRouteForAMissingInput() {
    Section("supply route: no NPC sells it is not the same as no source");

    const prof::Profession* fisher = prof::Find("fisher");
    const prof::Profession* smith  = prof::Find("miner_smith");
    const prof::Profession* archer = prof::Find("archer");
    const prof::Profession* scribe = prof::Find("scribe");
    Check(fisher && smith && archer && scribe,
          "the four professions from the wave are in the catalogue");
    if (!fisher || !smith || !archer || !scribe) return;

    // Dorvar: a FISHER short of fish. Nobody sells him fish; he catches them.
    Check(RouteForInput(*fisher, "i_fish_big_1", false) ==
              SupplyRoute::SelfProduce,
          "a fisher short of fish goes fishing, it does not go shopping");

    // Zarthal: a MINER_SMITH short of iron ingots. He smelts his own.
    Check(RouteForInput(*smith, "i_ingot_iron", false) ==
              SupplyRoute::SelfProduce,
          "a smith short of ingots makes them, it does not buy them");

    // Titus: an ARCHER short of logs. He does not make logs -- a lumberjack
    // does -- so this is a player rendezvous, which is exactly the one live
    // producer/consumer edge the catalogue has.
    Check(RouteForInput(*archer, "i_log", false) == SupplyRoute::PlayerMarket,
          "an archer short of logs buys them from a player, not an NPC");
    Check(!WhoProduces("i_log").empty(),
          "and the catalogue really does name a log producer");

    // The vendor table keeps precedence where it has an answer: a scribe's
    // blank scrolls are a mage-shop purchase and must stay one, even though
    // the lumberjack line can technically make parchment.
    Check(RouteForInput(*scribe, "i_scroll_blank", true) ==
              SupplyRoute::NpcVendor,
          "a known shopkeeper trade still wins -- this fix adds routes, it "
          "does not reroute working ones");

    // And the genuinely unknown case is still refused rather than invented.
    Check(RouteForInput(*smith, "i_not_a_real_defname", false) ==
              SupplyRoute::NoKnownSource,
          "an item nobody makes and nobody sells has no route at all");
    Check(RouteForInput(*smith, "", false) == SupplyRoute::NoKnownSource,
          "and neither does an empty defname");

    for (SupplyRoute r : {SupplyRoute::NpcVendor, SupplyRoute::SelfProduce,
                          SupplyRoute::PlayerMarket,
                          SupplyRoute::NoKnownSource}) {
        const char* n = SupplyRouteName(r);
        Check(n && *n && n[0] != '?', "every route prints a name");
    }

    // THE RULE THIS MUST NOT WEAKEN: routing a material to SelfProduce or
    // PlayerMarket must never make it NPC-sellable *while players are still
    // being offered it*. Ask the vendor policy directly for the two materials
    // above, at the default playersDeclined=false.
    Check(!HasNpcBuyer("i_ingot_iron"),
          "ingots go to players first, whatever the supply route says");
    Check(!HasNpcBuyer("i_log"), "and so do logs");
    // The restored NPC counters (tm_vend.scp, 2026-09-02) are a FLOOR under
    // that market, reachable only after the offer went unanswered -- so this
    // test's rule and the floor do not contradict each other.
    Check(HasNpcBuyer("i_ingot_iron", /*playersDeclined=*/true),
          "and the floor is underneath, not instead");
}

// The refusal the buy path now uses must be its own reason, distinct from the
// sell-side one it was borrowing. Conflating them is what made a smith who
// should have gone mining read as a broken buyer lookup.
void TestBuySideRefusalIsItsOwnReason() {
    Section("refusal: the buy side has its own word for 'no source'");
    Check(faucet::Refusal::NoKnownSupplier != faucet::Refusal::NoKnownBuyer,
          "supplier and buyer are different refusals");
    const char* supplier = faucet::RefusalName(faucet::Refusal::NoKnownSupplier);
    const char* buyer = faucet::RefusalName(faucet::Refusal::NoKnownBuyer);
    Check(supplier && std::string(supplier) == "REFUSE_NO_KNOWN_SUPPLIER",
          "and they print differently");
    Check(buyer && std::string(buyer) == "REFUSE_NO_KNOWN_BUYER",
          "the sell-side name is unchanged");
}

// ---------------------------------------------------------------------------
// THE NPC PRICE FLOOR: buyer resolution (owner ruling, 2026-09-02).
//
// The ruling permits an NPC sale of materials once the player-first window has
// closed. It does NOT create a buyer, and on this shard that distinction is
// almost the whole story: the runtime's own tm_vend.scp has the log, board,
// ore, iron-ingot and hide BUY rows COMMENTED OUT (:293, :294, :1084, :1421,
// :1506, :2114-5, :342/:344/:406/:480/:482), and the live rows that survive
// for i_log and i_ingot_iron are in sphere_template_vend_gargish.scp, used
// only by c_*_gargoyle chardefs that the world save never spawns.
//
// So this suite asserts BOTH halves: what the floor opens, and -- far more of
// it -- what still banks because nobody buys it.
void TestNpcPriceFloorBuyerResolution() {
    Section("npc floor: a real BUY row, or the goods bank");

    // --- the player-first window is the gate ------------------------------
    Check(NpcBuyersFor("i_feather").empty(),
          "no buyer while the WTS window is still open, even for a material");
    Check(!HasNpcBuyer("i_feather"), "HasNpcBuyer agrees, and defaults strict");
    Check(!NpcBuyersFor("i_feather", /*playersDeclined=*/true).empty(),
          "and a buyer appears once nobody answered the offer");

    // --- WHAT THE SHARD ACTUALLY BUYS -------------------------------------
    // Each of these was re-derived on 2026-09-02 by resolving every
    // VENDOR_B_* template (transitively -- VENDOR_B_PROVISIONER takes
    // BUY=VENDOR_B_BOWYER wholesale at tm_vend.scp:1465) back to the chardefs
    // that use it. The trade string is the paperdoll-title substring.
    struct Row { const char* item; const char* trade; };
    const Row kBuys[] = {
        {"i_feather",      "bowyer"},        // tm_vend.scp:1630
        {"i_feather",      "provisioner"},   // via :1465
        {"i_cotton",       "weaver"},        // :896
        {"i_cotton",       "tailor"},        // :1004
        {"i_thread",       "tailor"},        // :1006
        {"i_flax_bundle",  "tailor"},        // :1005
        {"i_ingot_copper", "jeweler"},       // :1507
        {"i_ingot_gold",   "jeweler"},       // :1508
        {"i_ingot_gold",   "provisioner"},   // :1422
        {"i_ingot_silver", "jeweler"},       // :1509
        {"i_ingot_silver", "provisioner"},   // :1423
        // THE RESTORED ROWS. These five items previously appeared in the
        // kNoBuyer list below, on the strength of `//BUY=` lines in
        // runtime/scripts/templates/tm_vend.scp. That commenting was a TNS
        // donor artefact -- rejected in
        // docs/TNS_WORLD_ECONOMY_DONOR_AUDIT.md section 3.5 -- and the shard
        // owner restored all 23 rows on 2026-09-02, byte-identical to
        // server/Scripts-X/templates/tm_vend.scp. Asserting their absence was
        // asserting the defect.
        {"i_ingot_iron",   "blacksmith"},    // :2115 VENDOR_B_BLACKSMITH
        {"i_ingot_iron",   "provisioner"},   // :1421 VENDOR_B_PROVISIONER
        {"i_ingot_iron",   "jeweler"},       // :1506 VENDOR_B_JEWELER
        {"i_ingot_iron",   "tinker"},        // :1084 VENDOR_B_TINKER
        {"i_log",          "blacksmith"},    // :2114
        {"i_log",          "provisioner"},   // :1438
        {"i_log",          "carpenter"},     // :293  VENDOR_B_CARPENTER
        {"i_log",          "bowyer"},        // :1632 VENDOR_B_BOWYER
        {"i_log",          "tinker"},        // :1085
        {"i_board",        "provisioner"},   // :1439
        {"i_board",        "carpenter"},     // :294
        {"i_board",        "tinker"},        // :1086
        {"i_hide",         "cobbler"},       // :344  VENDOR_B_COBBLER
        {"i_hide",         "tanner"},        // :482  VENDOR_B_TANNER
        {"i_hides_cut",    "cobbler"},       // :342
        {"i_hides_cut",    "tanner"},        // :480
        // i_hides_cut_2 is deliberately absent even though :343 and :481 buy
        // it: it is DUPEITEM=01067, and Sphere resolves a dupe id to its master
        // before the vendor ever sees it (CItemBase.cpp:2254-2256), so
        // i_hides_cut IS the row. Asserted below in kNoBuyer.
    };
    for (const Row& r : kBuys) {
        bool found = false;
        for (const NpcBuyer* b : NpcBuyersFor(r.item, true)) {
            if (std::string(b->trade) == r.trade) { found = true; break; }
        }
        Check(found, r.item);
    }

    // --- AND WHAT IT DOES NOT. These bank; the ruling says so outright. ----
    const char* kNoBuyer[] = {
        "i_ore_iron",       // no BUY row anywhere in the runtime, live or not
        // COLOURED INGOTS. [ITEMDEF i_ingot_valorite] carries ID=i_ingot_iron
        // (i_provisions_ore.scp), but a script `ID=` line sets only the DISPLAY
        // index (CItemBase.cpp:1659-1694 IBC_ID -> m_dwDispIndex), while
        // CItemBase::GetID (CItemBase.h:309) still returns the itemdef's own
        // resource index. NPC_FindVendableItem looks the item up by that
        // resource id (CCharNPCStatus.cpp:611) and IsResourceMatch compares
        // resource ids, with no hue term and a fallback that special-cases only
        // log<-board and hide<-leather (CItem.cpp:6041-6071). So a live
        // BUY=i_ingot_iron row does NOT buy valorite. Source-verified, not
        // runtime-verified.
        "i_ingot_valorite",
        // The DUPEITEM. tm_vend.scp:343/:481 name it, but Sphere folds the id
        // into its master 01067 before the buy lookup, so i_hides_cut carries
        // the trade and this defname is never the one to ask about.
        "i_hides_cut_2",
        "i_cloth",
        "i_cloth_bolt",
        "i_wool",
        "i_yarn_ball",
    };
    for (const char* item : kNoBuyer) {
        Check(NpcBuyersFor(item, /*playersDeclined=*/true).empty(), item);
    }
    // The one that matters most: an open floor plus no buyer must NOT produce
    // a walk across town to be refused. That is the shape the whole table's
    // header warns about, and it is how a sell goal spins. Iron ore is the
    // honest case now that logs and ingots have their counters back: it is a
    // material the policy will pass, and no template in the tree buys one.
    Check(econ::MaterialFloorOpen("i_ore_iron", true),
          "the POLICY permits iron ore at the counter...");
    Check(!HasNpcBuyer("i_ore_iron", true),
          "...and there is still no counter that takes it, so it banks");

    // --- MaySellToNpc: the restored counters really do open ---------------
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    const prof::Profession* ms = prof::Find("miner_smith");
    Check(lj && ms, "the two gathering lives exist");
    if (!lj || !ms) return;
    Ledger clean;
    Check(MaySellToNpc(*lj, "i_log", clean, /*playersDeclined=*/true).allowed,
          "a lumberjack may sell logs once nobody answered the WTS");
    Check(MaySellToNpc(*ms, "i_ingot_iron", clean, true).allowed,
          "and a smith iron ingots, for the same reason");
    // ...but ONLY once the window has closed. Player-first is untouched by the
    // restore: the vendor table gained rows, the sale policy did not.
    Check(!MaySellToNpc(*lj, "i_log", clean, /*playersDeclined=*/false).allowed,
          "logs still refused while the player-first window is open");
    Check(!MaySellToNpc(*ms, "i_ingot_iron", clean, false).allowed,
          "and ingots likewise");

    // ...and WITH a buyer, the floor really does open. THREAD is the honest
    // case to test it on: the tailor already produces i_cloth_bolt off the
    // same spinning wheel, i_thread has a live BUY row (tm_vend.scp:1006), and
    // the production graph knows it is spun from cotton
    // (Production.cpp:62, ENGINE CClientTarg.cpp:2075) -- so the arbitrage
    // guard below has a real input to reason about. The `produces` line is the
    // one thing constructed here; the policy, the recipe and the vendor table
    // are all the real ones.
    const prof::Profession* tl = prof::Find("tailor");
    Check(tl != nullptr, "the tailor life exists");
    if (!tl) return;
    prof::Profession spinner = *tl;
    spinner.produces.push_back("i_thread");
    const SellRuling shut = MaySellToNpc(spinner, "i_thread", clean,
                                         /*playersDeclined=*/false);
    Check(!shut.allowed, "thread refused while the WTS window is open");
    const SellRuling open = MaySellToNpc(spinner, "i_thread", clean,
                                         /*playersDeclined=*/true);
    Check(open.allowed, "and allowed once nobody answered -- the NPC floor");
    Check(open.reason && std::string(open.reason).find("floor") !=
                             std::string::npos,
          "and the reason says so, so a log line can be read back");

    // THE ARBITRAGE GUARD IS NOT WEAKENED BY THE FLOOR. A floor route carries
    // no HistoryEvidence -- it has no registry row at all -- so the
    // closed-vendor-loop test still applies to it in full.
    Ledger bought;
    bought.Note(GoldFlow::DestroyedVendorPurchase, 40, "i_cotton", 1000);
    Check(!MaySellToNpc(spinner, "i_thread", bought, true).allowed,
          "cotton bought from an NPC still blocks selling the thread back");

    // --- THE SWITCH OFF: the strict behaviour, byte for byte --------------
    econ::SalePolicy strict;
    strict.allowMaterialsToNpc = false;
    econ::SetSalePolicy(strict);
    Check(NpcBuyersFor("i_feather", true).empty(),
          "switch OFF: no NPC buyer for a material, window closed or not");
    Check(NpcBuyersFor("i_ingot_gold", true).empty(), "switch OFF: nor ingots");
    Check(!MaySellToNpc(spinner, "i_thread", clean, true).allowed,
          "switch OFF: MaySellToNpc refuses again");
    // ...while everything the REGISTRY allows unconditionally is untouched:
    // the switch governs the floor, not the documented taps.
    Check(!NpcBuyersFor("i_fish_big_1").empty(),
          "switch OFF: a fisher still sells fish -- that is a faucet, not a floor");
    econ::SetSalePolicy(econ::SalePolicy{});
    Check(!NpcBuyersFor("i_feather", true).empty(), "switch back ON");
}

// --------------------------------------------------------------------------
// THE MATERIAL SURPLUS CAP (owner ruling, 2026-09-02).
//
//   "materials exist to be CRAFTED, not sold. NPC sale of materials ONLY when
//    bank+pack exceeds a plan-derived surplus cap, dynamic per character."
//
// The point of the test is the word DYNAMIC: two characters must get two
// different numbers, and the difference must be traceable to their plans rather
// than to a constant somebody tuned.
void TestMaterialSurplusCapIsPerCharacter() {
    Section("floor: the NPC sale of a material needs a surplus above the "
            "character's own plan cap");

    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    const prof::Profession* ms = prof::Find("miner_smith");
    Check(lj && ms, "the lumberjack and the smith exist");
    if (!lj || !ms) return;

    TradePolicy pol;
    const i32 batch = 5;

    // CHARACTER ONE: a lumberjack whose Carpentry is finished. Its bench still
    // eats a log per board, but it is not stocking for a skill climb any more.
    const std::vector<SkillGap> done = {{rules::kCarpentry, 0}};
    const MaterialCap capDone =
        MaterialSurplusCap(*lj, "i_log", batch, /*gold=*/50000, done, pol);
    // CHARACTER TWO: a green smith with the whole Blacksmithing climb ahead of
    // it -- the case the owner's "stock 500-600 then start training" rule is
    // about.
    const std::vector<SkillGap> green = {{rules::kBlacksmithing, 1000}};
    const MaterialCap capGreen =
        MaterialSurplusCap(*ms, "i_ingot_iron", batch, /*gold=*/50000, green, pol);

    std::printf("  lumberjack/i_log cap=%d (plan %d + training %d + market %d)\n",
                capDone.units, capDone.ownPlan, capDone.training, capDone.market);
    std::printf("  miner_smith/i_ingot_iron cap=%d (plan %d + training %d + market %d)\n",
                capGreen.units, capGreen.ownPlan, capGreen.training,
                capGreen.market);

    Check(capDone.isMaterial && capGreen.isMaterial,
          "a log and an iron ingot are both materials the ruling covers");
    Check(capDone.units != capGreen.units,
          "two characters, two different caps -- the number is derived, not "
          "global");
    Check(capGreen.units > capDone.units,
          "and the one with a skill still to climb keeps far more back");

    // Every term is traceable.
    //   i_board <- {i_log 1} (Production.cpp:146), so one sitting of five eats
    //   five logs.
    Check(capDone.ownPlan == batch,
          "own plan = craft batch x the largest per-craft demand (1 log a board)");
    //   i_cutlass <- {i_ingot_iron 8} (Production.cpp:136) is the smith's
    //   hungriest recipe.
    Check(capGreen.ownPlan == batch * 8,
          "the smith's cap counts its hungriest recipe, 8 ingots a cutlass");
    //   The owner's 500-600 for a full climb, as a rate: 5.5 units a point.
    Check(capGreen.training >= 500 && capGreen.training <= 600,
          "a full 0->100.0 Blacksmithing climb banks the owner's 500-600");
    Check(capDone.training == 0,
          "a finished skill banks nothing for training");
    // Half the climb, half the stock. This is what stops the rule being 550
    // with extra steps.
    const std::vector<SkillGap> half = {{rules::kBlacksmithing, 500}};
    const MaterialCap capHalf =
        MaterialSurplusCap(*ms, "i_ingot_iron", batch, 50000, half, pol);
    Check(capHalf.training * 2 == capGreen.training ||
              capHalf.training * 2 == capGreen.training + 1,
          "and a smith halfway up banks half of it");

    //   The market reserve is one restock lot per profession that must BUY it.
    Check(capDone.market ==
              static_cast<i32>(WhoConsumes("i_log").size()) *
                  pol.restockConsumablesTo,
          "market reserve = a restock lot for each catalogue consumer");
    Check(capDone.market > 0,
          "a pure gatherer still holds stock back for the crafters who need it");

    // A BROKE CHARACTER RELEASES ITS MARKET STOCK SOONER -- and only that.
    const MaterialCap poor =
        MaterialSurplusCap(*lj, "i_log", batch, /*gold=*/0, done, pol);
    Check(poor.market < capDone.market, "an empty purse halves the market reserve");
    Check(poor.ownPlan == capDone.ownPlan,
          "but never the stock its own bench needs");

    // --- the gate itself ---------------------------------------------------
    const i32 cap = capDone.units;
    // BELOW the cap: no NPC trip, whatever the player market did.
    const MaterialSaleGate under = MaterialNpcSaleGate(
        *lj, "i_log", cap, /*playersDeclined=*/true, batch, 50000, done, pol);
    Check(!under.allowed, "at the cap exactly, the stock stays banked");
    Check(under.reason != nullptr && under.cap == cap && under.held == cap,
          "and the refusal reports the numbers it was decided on");
    // ABOVE it, with the player window closed: this is the last resort opening.
    const MaterialSaleGate over = MaterialNpcSaleGate(
        *lj, "i_log", cap + 1, true, batch, 50000, done, pol);
    Check(over.allowed, "one above the cap, with nobody buying, the floor opens");
    // ABOVE it, with the player window still OPEN: player-first is untouched.
    const MaterialSaleGate early = MaterialNpcSaleGate(
        *lj, "i_log", cap * 10, /*playersDeclined=*/false, batch, 50000, done, pol);
    Check(!early.allowed,
          "a mountain of stock is still not a reason to skip the player market");

    // A FINISHED GOOD IS NOT WHAT THIS RULE IS ABOUT. A smith's daggers are
    // exactly what it is supposed to take to a counter.
    const MaterialSaleGate made = MaterialNpcSaleGate(
        *ms, "i_dagger", 1, /*playersDeclined=*/false, batch, 50000, green, pol);
    Check(made.allowed && !made.detail.isMaterial,
          "the cap does not apply to a crafted good");

    // NOR TO A DOCUMENTED FAUCET. Fish is graded WorldGathered, so it is a
    // "material" by class -- and it is also the tap a fisher lives on ("caught
    // fish cook fish then sell"). Capping it would have deleted a whole
    // profession's income to enforce a ruling that was never about fish.
    const prof::Profession* fi = prof::Find("fisher");
    if (fi) {
        const MaterialSaleGate caught = MaterialNpcSaleGate(
            *fi, "i_fish_big_1", 1, /*playersDeclined=*/false, batch, 0, {},
            pol);
        Check(caught.detail.isMaterial,
              "fish is a material by class -- that is the trap");
        Check(caught.allowed,
              "and a fisher sells it anyway: it is a documented faucet, not "
              "the floor");
    }
}

// --------------------------------------------------------------------------
// DEMAND HAS A VOICE. Before this a crafter short of materials walked to the
// market and stood there silently: a trade could only start if a gatherer
// happened to announce the exact thing somebody happened to need.
void TestDemandSideWtb() {
    Section("trade: a short crafter shouts WTB, and a gatherer holding it "
            "answers");

    const prof::Profession* ms = prof::Find("miner_smith");
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    Check(ms && lj, "the smith and the lumberjack exist");
    if (!ms || !lj) return;

    // --- the wire form, both directions ------------------------------------
    TradeIntent want;
    want.item = "i_log"; want.qty = 20; want.pricePerUnit = 4;
    const std::string said = FormatBuyWant(want);
    Check(said == "WTB 20 i_log 4gp", "the WTB line reads like a player's");

    TradeIntent back;
    Check(ParseBuyWant(said, &back), "and parses back");
    Check(back.item == "i_log" && back.qty == 20 && back.pricePerUnit == 4,
          "with the item, the quantity and the ceiling intact");

    // THE OLD REPLY FORM STILL WORKS, and -- the part that would otherwise
    // break silently -- the full form is still recognised as a reply. A seller
    // that stopped recognising answers to its own offer the moment buyers
    // learned to speak first would have lost every trade it used to make.
    std::string item;
    Check(ParseBuyReply("WTB i_log", &item) && item == "i_log",
          "the bare reply form is unchanged");
    Check(ParseBuyReply(said, &item) && item == "i_log",
          "and a full WTB is read as a reply for the same item");
    TradeIntent bare;
    Check(ParseBuyWant("WTB i_log", &bare) && bare.qty == 0 &&
              bare.pricePerUnit == 0,
          "a bare WTB means 'some, at whatever you ask'");
    Check(!ParseBuyWant("hello there", &bare), "and ordinary speech is not a WTB");
    Check(!ParseBuyWant("WTB 20", &bare), "nor is a quantity naming nothing");

    // --- TWO REPLIES, ONE ACCEPTED, ONE DECLINED ---------------------------
    //
    // The 2026-09-02 wave: Odessa shouted for ingots, Kharain and Elvar both
    // held some, and the bare "WTB i_ingot_iron" was an answer to BOTH of
    // them. They both opened a trade window on a buyer who could pay one, both
    // handshakes timed out at 25s, and both sellers then stood at the bank.
    // An addressed reply is what makes the choice exclusive.
    const std::string toKharain = FormatBuyReply("i_ingot_iron", "Kharain");
    Check(toKharain == "Kharain, WTB i_ingot_iron",
          "an accepted reply names the seller it accepts");
    std::string got;
    Check(ParseBuyReply(toKharain, &got) && got == "i_ingot_iron",
          "and still parses as a reply for that item");
    Check(AddressedTo(toKharain, "Kharain"),
          "the named seller may answer it");
    Check(!AddressedTo(toKharain, "Elvar"),
          "the OTHER seller holding the same goods must not");
    Check(AddressedTo("WTB i_ingot_iron", "Elvar"),
          "an unaddressed want is still open to the room");
    // THE LIMIT, stated rather than papered over: an address is the single
    // token before the first comma, so a MULTI-word clause is correctly not
    // one, but a single incidental word would be read as a name. Nothing this
    // fleet says begins that way (FormatBuyReply and FormatDecline are the
    // only comma-bearing lines), and the failure direction is the safe one --
    // a line addressed to a name nobody has is simply answered by nobody.
    Check(AddressedTo("i think, WTB i_log", "Elvar"),
          "a multi-word clause before a comma is not an address");

    const std::string sorry = FormatDecline("Elvar");
    std::string declined;
    Check(ParseDecline(sorry, &declined) && declined == "Elvar",
          "the losing seller is told so by name");
    Check(AddressedTo(sorry, "Elvar") && !AddressedTo(sorry, "Kharain"),
          "and only that seller stands down on it");
    Check(!ParseDecline(toKharain, &declined),
          "an ordinary reply is not a decline");

    // --- the buyer chooses what to shout -----------------------------------
    PriceBook blind;
    TradePolicy pol;
    TradeIntent shout;
    // A smith with an empty pack is short of logs (i_spear_short eats one) and
    // has money over its reserve.
    Check(ChooseBuyWant(*ms, {}, blind, pol, ms->goldReserve + 5000, &shout),
          "a smith with no logs and money to spend has something to shout");
    Check(shout.item == "i_log", "and it is the input a player could supply");
    Check(shout.qty > 0 && shout.pricePerUnit > 0,
          "with a quantity and a ceiling");
    // THE CEILING IS ONE IT WILL HONOUR. A bot that advertises a number and
    // then refuses it at the window is a bot no gatherer can plan around.
    TradeIntent atCeiling = shout;
    atCeiling.qty = shout.qty;
    Check(ConsiderOffer(*ms, {}, ms->goldReserve + 5000, pol, atCeiling).accept,
          "and an offer at exactly that ceiling is one it accepts");

    // NO SHOUT WITHOUT THE MONEY. This is the "no spin" half: a life that
    // cannot pay says nothing rather than announcing forever.
    TradeIntent none;
    Check(!ChooseBuyWant(*ms, {}, blind, pol, ms->goldReserve, &none),
          "a purse down to the tool reserve announces nothing");
    // Nor when it is not short of anything.
    std::vector<Stock> stocked;
    for (const std::string& c : ms->consumes) stocked.push_back({c, 500});
    Check(!ChooseBuyWant(*ms, stocked, blind, pol, 100000, &none),
          "nor does a life with a full stock");

    // --- the gatherer answers ----------------------------------------------
    const std::vector<Stock> woodpile = {{"i_log", 200}};
    TradeIntent fill;
    Check(AnswerBuyWant(*lj, woodpile, blind, pol, shout, &fill),
          "a lumberjack carrying logs answers a WTB for logs");
    Check(fill.item == "i_log" && fill.qty > 0 && fill.qty <= shout.qty,
          "for no more than was asked for");
    Check(fill.pricePerUnit <= shout.pricePerUnit,
          "at or under the buyer's ceiling -- otherwise there is no deal");
    // AND THE ANSWER CLOSES THE LOOP: the buyer must accept the WTS it gets
    // back, or the two halves are talking past each other.
    Check(ConsiderOffer(*ms, {}, ms->goldReserve + 5000, pol, fill).accept,
          "and the buyer accepts the answer it provoked");

    // A DIRECT REQUEST OUTRANKS THE TRIP THRESHOLD. minimumSurplusToOffer (5)
    // exists so nobody treks across town with two of something; the buyer is
    // already standing here. The WORKING RESERVE is a different rule and is NOT
    // relaxed -- a log feeds this life's own boards, so keepOfOwnOutput (20)
    // still comes off the top and 22 logs is a spare of two.
    TradeIntent small;
    Check(AnswerBuyWant(*lj, {{"i_log", 22}}, blind, pol, shout, &small),
          "two spare logs are worth handing to somebody who asked for them");
    Check(small.qty == 2, "the two above the working reserve, and no more");
    TradeIntent reserved;
    Check(!AnswerBuyWant(*lj, {{"i_log", 20}}, blind, pol, shout, &reserved),
          "and a life down to its working reserve answers nothing");

    // NOBODY ANSWERS WHAT THEY DO NOT HAVE, and nobody becomes a fence.
    TradeIntent nope;
    Check(!AnswerBuyWant(*lj, {}, blind, pol, shout, &nope),
          "an empty pack answers nothing");
    Check(!AnswerBuyWant(*ms, woodpile, blind, pol, shout, &nope),
          "and a smith holding logs does not resell them -- it is not a fence");

    // A CEILING BELOW WHAT THE SELLER BELIEVES IS A DECLINE, NOT AN UNDERCUT.
    PriceBook seen;
    PriceObservation po;
    po.item = "i_log"; po.pricePerUnit = 40;
    po.source = PriceSource::PlayerTraded; po.who = "somebody";
    seen.Note(po);
    TradeIntent lowball = shout;
    lowball.pricePerUnit = 3;
    TradeIntent unused;
    Check(!AnswerBuyWant(*lj, woodpile, seen, pol, lowball, &unused),
          "a seller that has watched logs trade at 40 does not sell at 3");
}

// --------------------------------------------------------------------------
// The buyer-initiated deal: the buyer shouts, the SELLER opens the window, and
// the buyer has to fund a window it did not itself commit to.
//
// Runtime defect this is written against (2026-09-02 wave, 12:50-13:20):
// Odessa broadcast "WTB 8 i_ingot_iron 52gp" nineteen times; Elvar read it as a
// reply to its own standing WTS (g_Elvar.console.txt:340 `trade:  wants our
// i_ingot_iron`), said nothing back, and opened a trade window
// (g_Odessa.console.txt:257). Odessa had no price and no quantity of its own,
// offered nothing, and the deal died at 25s (:280). Zero player trades
// completed in the whole run.
void TestBuyerFundsTheWindowItAskedFor() {
    Section("trade: a WTB broadcast is demand, and the buyer funds the window "
            "the seller opens for it");

    // --- half one: the two "WTB" lines are different lines ------------------
    TradeIntent parsed;
    Check(ClassifyBuyLine("WTB 8 i_ingot_iron 52gp", &parsed) ==
              BuyLineKind::Announce,
          "a quantity + a price + no addressee is demand announcing itself");
    Check(parsed.item == "i_ingot_iron" && parsed.qty == 8 &&
              parsed.pricePerUnit == 52,
          "and the announcement's terms come back intact");
    Check(ClassifyBuyLine("WTB i_ingot_iron", &parsed) == BuyLineKind::Reply,
          "the bare form is a reply to somebody's standing offer");
    Check(ClassifyBuyLine("Kharain, WTB i_ingot_iron") == BuyLineKind::Reply,
          "and so is an addressed one");
    // A buyer that learns to accept in the full form is still accepting, so
    // long as it names the seller. This is the tolerance ParseBuyReply was
    // given, kept honest here rather than lost to the new classifier.
    Check(ClassifyBuyLine("Kharain, WTB 8 i_ingot_iron 52gp") ==
              BuyLineKind::Reply,
          "an ADDRESSED full form is a reply, not a broadcast");
    Check(ClassifyBuyLine("WTS 8 i_ingot_iron 4gp") == BuyLineKind::NotABuyLine,
          "a seller's line is not a WTB at all");
    Check(ClassifyBuyLine("hello there") == BuyLineKind::NotABuyLine,
          "nor is ordinary speech");

    // --- half two: how much coin goes in the window ------------------------
    TradeIntent planned;
    planned.item = "i_ingot_iron"; planned.qty = 8; planned.pricePerUnit = 52;

    // Rich enough for the lot: pay for exactly what was asked for.
    FundingDecision d = FundOpenWindow(planned, /*goldOnHand=*/1000,
                                       /*goldReserve=*/100);
    Check(d.accept && d.qty == 8 && d.gold == 8 * 52,
          "a purse that covers the announcement funds all of it");
    Check(d.reason != nullptr, "and says why");

    // The purse shrank since the shout. Pay for what it now covers rather than
    // promising a number the drag would refuse.
    d = FundOpenWindow(planned, /*goldOnHand=*/300, /*goldReserve=*/100);
    Check(d.accept && d.qty == 3 && d.gold == 3 * 52,
          "a purse that covers three units funds three, not eight");
    Check(d.gold <= 300 - 100, "and never dips into the reserve");

    // Cannot cover one unit: a refusal, not a zero-coin window.
    d = FundOpenWindow(planned, /*goldOnHand=*/120, /*goldReserve=*/100);
    Check(!d.accept && d.gold == 0,
          "a purse that cannot cover one unit funds nothing");

    // NO PLAN, NO FUNDING. This is the rule the defect broke in the other
    // direction: the buyer put nothing in because it had nothing written down.
    // The answer is not "pay something anyway" -- it is to refuse explicitly so
    // the window's own give-up clock ends the deal.
    d = FundOpenWindow(TradeIntent{}, 1000, 0);
    Check(!d.accept && d.reason != nullptr,
          "a window nobody planned for is refused, with a reason");
    TradeIntent priceless;
    priceless.item = "i_log"; priceless.qty = 20;   // pricePerUnit stays 0
    d = FundOpenWindow(priceless, 1000, 0);
    Check(!d.accept, "and so is a want with no price to multiply");

    // THE SELLER'S SIDE OF THE SAME DEAL, so the pair actually closes: a
    // gatherer holding the goods must have something to say back to the
    // announcement. Without this the buyer's plan is funded against silence.
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    Check(lj != nullptr, "the lumberjack exists");
    if (!lj) return;
    const std::vector<Stock> woodpile = {{"i_log", 200}};
    TradePolicy pol;
    PriceBook blind;
    TradeIntent shout;
    shout.item = "i_log"; shout.qty = 20; shout.pricePerUnit = 40;
    TradeIntent answer;
    Check(AnswerBuyWant(*lj, woodpile, blind, pol, shout, &answer) &&
              answer.item == "i_log" && answer.qty == 20,
          "a holder answers the announcement with a WTS the buyer can hear");
}

int main() {
    std::printf("m7_market\n");
    TestInterdependence();
    TestSurplusIsOwnOutputOnly();
    TestShortfallSeparatesWorldFromPlayers();
    TestShortfallSkipsWhatThePackCannotCount();
    TestConsumablesCountedByGraphicNotLabel();
    TestWhoProducesIsCatalogueNotMarket();
    TestPricesMustHaveBeenSeen();
    TestGoldLedger();
    TestReserveOnlyForOwnInputs();
    TestNoClosedVendorLoop();
    TestNpcsMayNotBuyPlayerMarketGoods();
    TestWhatAnNpcMayStillBuy();
    TestArbitrageGuardStillApplies();
    TestSpokenOffers();
    TestWhatToAnnounce();
    TestWhetherToAnswer();
    TestForumPriceSeedsGroundFirstOffers();
    TestTheChainCanActuallyClose();
    TestTheDisposalOrder();
    TestSupplyRouteForAMissingInput();
    TestBuySideRefusalIsItsOwnReason();
    TestNpcPriceFloorBuyerResolution();
    TestMaterialSurplusCapIsPerCharacter();
    TestDemandSideWtb();
    TestBuyerFundsTheWindowItAskedFor();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
