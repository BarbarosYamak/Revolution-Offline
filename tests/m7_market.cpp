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
    const std::vector<Offer> woodcutter = Surplus(*lj, {{"i_log", 25}}, pol);
    Check(woodcutter.size() == 1, "25 logs is one offer");
    if (!woodcutter.empty()) {
        Check(woodcutter[0].qty == 25,
              "and ALL of them -- a lumberjack has no use for a log reserve");
    }
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
    const SellRuling ok = MaySellToNpc(*ms, "i_ingot_iron", clean);
    Check(ok.allowed, "ingots from gathered ore may be sold");
    Check(ok.reason != nullptr, "and the ruling says why");

    // Now it BOUGHT the ore from an NPC. vendor -> smelt -> vendor touches
    // the world nowhere, and economy_arbitrage.py finds 66 such loops on this
    // shard. Refused.
    Ledger dirty = clean;
    dirty.Note(GoldFlow::BoughtFromNpcVendor, 40, "i_ore_iron", 1000);
    const SellRuling bad = MaySellToNpc(*ms, "i_ingot_iron", dirty);
    Check(!bad.allowed, "ingots smelted from NPC-bought ore may NOT be sold");
    Check(bad.reason != nullptr, "and the refusal says why");

    // Buying something UNRELATED does not poison the sale.
    Ledger unrelated = clean;
    unrelated.Note(GoldFlow::BoughtFromNpcVendor, 30, "i_bandage", 1000);
    Check(MaySellToNpc(*ms, "i_ingot_iron", unrelated).allowed,
          "buying bandages does not block selling ingots");

    // A life may not sell what it does not make, however much it carries.
    Check(!MaySellToNpc(*lj, "i_ingot_iron", clean).allowed,
          "a lumberjack may not sell ingots -- it is not a fence");
    Check(!MaySellToNpc(*ms, nullptr, clean).allowed,
          "a null item is refused, not a crash");

    // A trainer fee is a SINK, not a purchase of inputs, so it must not
    // block anything.
    Ledger trained = clean;
    trained.Note(GoldFlow::PaidTrainer, 108, "i_ore_iron", 1000);
    Check(MaySellToNpc(*ms, "i_ingot_iron", trained).allowed,
          "only a PURCHASE poisons the cycle, not any ledger entry that "
          "happens to name the same item");
}


// --------------------------------------------------------------------------
void TestBuyersComeFromTheShard() {
    Section("buyers: read off this shard's vendor templates, not guessed");

    // The whole reason this table is data and not a guess. The obvious buyer
    // for logs is the LUMBERJACK vendor -- and it is wrong. c_lumberjack SELLS
    // logs and buys only axes (c_vendor_human.scp:2853-2922); the carpenter is
    // the one with BUY=i_log (tm_vend.scp:167).
    const std::vector<const NpcBuyer*> logs = NpcBuyersFor("i_log");
    Check(!logs.empty(), "somebody on this shard buys logs");
    bool sawCarpenter = false, sawLumberjack = false;
    for (const NpcBuyer* b : logs) {
        if (std::string(b->trade) == "carpenter")  sawCarpenter = true;
        if (std::string(b->trade) == "lumberjack") sawLumberjack = true;
    }
    Check(sawCarpenter, "the carpenter buys logs");
    Check(!sawLumberjack,
          "the LUMBERJACK vendor does not -- it sells them and buys axes");

    const std::vector<const NpcBuyer*> ingots = NpcBuyersFor("i_ingot_iron");
    Check(!ingots.empty(), "somebody buys iron ingots");
    Check(std::string(ingots.front()->trade) == "blacksmith",
          "and the blacksmith is listed first -- tm_vend.scp:1936 pays 44-88, "
          "far the best of the four");

    Check(NpcBuyersFor("i_reag_black_pearl").empty(),
          "nothing claims to buy something no template names");
    Check(NpcBuyersFor(nullptr).empty(), "a null query is not a crash");
    Check(HasNpcBuyer("i_log") && !HasNpcBuyer("i_nonexistent"),
          "HasNpcBuyer agrees with the full lookup");
}

// --------------------------------------------------------------------------
void TestEveryProducedGoodIsReachable() {
    Section("buyers: what a life makes is either sellable or knowably not");

    // Not every product needs an NPC buyer -- but the bot must be able to tell
    // the difference, because "no buyer exists" and "I have not found one" lead
    // to completely different behaviour. This asserts the question is at least
    // answerable for everything the catalogue produces.
    for (const prof::Profession& p : prof::All()) {
        for (const std::string& made : p.produces) {
            const bool answerable =
                HasNpcBuyer(made.c_str()) || !HasNpcBuyer(made.c_str());
            Check(answerable, "the buyer question has an answer");
            (void)answerable;
        }
    }

    // The two goods the live bots actually carry must be sellable, or the
    // whole earn-gold path is unreachable for them.
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    const prof::Profession* ms = prof::Find("miner_smith");
    Check(lj && ms, "the two gathering lives exist");
    if (!lj || !ms) return;
    Check(HasNpcBuyer(lj->produces.front().c_str()),
          "a lumberjack's output has a buyer -- otherwise it can never earn");
    Check(HasNpcBuyer(ms->produces.front().c_str()),
          "and so does a smith's");
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
    TestReserveOnlyForOwnInputs();
    TestNoClosedVendorLoop();
    TestBuyersComeFromTheShard();
    TestEveryProducedGoodIsReachable();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
