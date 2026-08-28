// M7.1 -- producer/consumer separation, non-omniscient prices, gold ledger.
//
// The rule under test is the one that is easiest to break by accident: a bot
// may not know the market. Every price here has to have been SEEN, and a
// character with no observation must answer "I do not know" rather than a
// plausible number.
//
// No server, no MULs, no world data.

#include "uo/market.h"
#include "uo/professions.h"

#include <cstdio>
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
    const prof::Profession* mg = prof::Find("mage");
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

    const std::string made = ms->produces.front();

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

    const prof::Profession* mg = prof::Find("mage");
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

    PriceBook book;
    Check(book.BelievedSalePrice("i_ingot_iron") == -1,
          "with no observation the answer is -1, NOT a plausible number");
    Check(book.Latest("i_ingot_iron", PriceSource::PlayerTraded) == nullptr,
          "and there is no observation to return");

    PriceObservation npc;
    npc.item = "i_ingot_iron";
    npc.pricePerUnit = 3;
    npc.source = PriceSource::NpcVendorBuys;
    npc.who = "Bertram the blacksmith";
    npc.whenMs = 1000;
    book.Note(npc);
    Check(book.BelievedSalePrice("i_ingot_iron") == 3,
          "an NPC's buy price is a belief when it is all the character has");

    // A player's quote outranks the NPC floor.
    PriceObservation quote = npc;
    quote.pricePerUnit = 8;
    quote.source = PriceSource::PlayerQuoted;
    quote.who = "Doran";
    quote.whenMs = 2000;
    book.Note(quote);
    Check(book.BelievedSalePrice("i_ingot_iron") == 8,
          "a player's quote outranks an NPC's buy price");

    // And a trade that actually happened outranks the quote.
    PriceObservation traded = quote;
    traded.pricePerUnit = 6;
    traded.source = PriceSource::PlayerTraded;
    traded.whenMs = 3000;
    book.Note(traded);
    Check(book.BelievedSalePrice("i_ingot_iron") == 6,
          "a completed trade outranks a quote, even for LESS money -- what "
          "was paid outranks what was claimed");

    // Same item, same source, same seller -> one row that updates.
    PriceObservation again = traded;
    again.pricePerUnit = 7;
    again.whenMs = 4000;
    book.Note(again);
    Check(book.Size() == 3, "one row per (item, source, who)");
    Check(book.BelievedSalePrice("i_ingot_iron") == 7, "and it updates");

    // Stale prices are not knowledge.
    book.Expire(100000, 50000);
    Check(book.Size() == 0, "everything older than the window is dropped");
    Check(book.BelievedSalePrice("i_ingot_iron") == -1,
          "and the character is back to not knowing");
}

// --------------------------------------------------------------------------
void TestGoldLedger() {
    Section("ledger: which flows actually create gold");

    // The anti-arbitrage invariant in one line: only the shard can MAKE gold.
    Check(IsGoldSource(GoldFlow::LootedFromCorpse), "loot creates gold");
    Check(IsGoldSource(GoldFlow::SoldToNpcVendor),
          "an NPC vendor creates gold -- it pays from nowhere");
    Check(IsGoldSource(GoldFlow::StartingKit),
          "the newbie kit's 1000gp creates gold");
    Check(!IsGoldSource(GoldFlow::SoldToPlayer),
          "selling to a PLAYER creates none -- it moves sideways, which is "
          "the entire point of a player economy");
    Check(!IsGoldSource(GoldFlow::PaidTrainer), "a trainer fee is a sink");
    Check(!IsGoldSource(GoldFlow::BoughtFromNpcVendor), "so is a purchase");

    Ledger led;
    led.Note(GoldFlow::StartingKit, 1000, "newbie kit", 0);
    led.Note(GoldFlow::PaidTrainer, 93, "Evaluating Intelligence", 1000);
    led.Note(GoldFlow::PaidTrainer, 108, "Inscription", 2000);
    led.Note(GoldFlow::SoldToPlayer, 50, "ingots", 3000);
    led.Note(GoldFlow::BoughtFromPlayer, 50, "ingots", 3000);

    Check(led.TotalFor(GoldFlow::PaidTrainer) == 201,
          "the two live trainer fees add up: 93 + 108");
    Check(led.TotalIn() == 1050, "in = kit + the player sale");
    Check(led.TotalOut() == 251, "out = both fees + the player purchase");
    Check(led.Net() == 799,
          "which leaves exactly the purse the live mage ended with");

    led.Note(GoldFlow::PaidTrainer, 0, "a free lesson", 4000);
    led.Note(GoldFlow::PaidTrainer, -5, "impossible", 4000);
    Check(led.TotalFor(GoldFlow::PaidTrainer) == 201,
          "zero and negative amounts are not entries");
}

}  // namespace

int main() {
    std::printf("m7_market\n");
    TestInterdependence();
    TestSurplusIsOwnOutputOnly();
    TestShortfallSeparatesWorldFromPlayers();
    TestWhoProducesIsCatalogueNotMarket();
    TestPricesMustHaveBeenSeen();
    TestGoldLedger();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
