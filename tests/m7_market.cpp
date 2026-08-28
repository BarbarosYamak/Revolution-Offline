// M7.1 -- producer/consumer separation, non-omniscient prices, gold ledger.
//
// The rule under test is the one that is easiest to break by accident: a bot
// may not know the market. Every price here has to have been SEEN, and a
// character with no observation must answer "I do not know" rather than a
// plausible number.
//
// No server, no MULs, no world data.

#include "uo/market.h"
#include "uo/vendor_policy.h"
#include "uo/professions.h"

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
    const prof::Profession* mage = prof::Find("mage");
    Check(mage != nullptr, "the mage exists");
    if (!mage) return;
    Check(MaySellToNpc(*mage, "i_scroll_poison", clean).allowed,
          "a scribe MAY sell a scroll it wrote -- one of the three taps");

    // Now it bought the blank scroll from an NPC. vendor -> inscribe ->
    // vendor touches the world nowhere; economy_arbitrage.py finds 66 such
    // loops on this shard. Refused.
    Ledger dirty = clean;
    dirty.Note(GoldFlow::BoughtFromNpcVendor, 40, "i_scroll_blank", 1000);
    const SellRuling bad = MaySellToNpc(*mage, "i_scroll_poison", dirty);
    Check(!bad.allowed, "a scroll written on an NPC-bought blank may NOT be sold");
    Check(bad.reason != nullptr, "and the refusal says why");

    // Buying something UNRELATED does not poison the sale.
    Ledger unrelated = clean;
    unrelated.Note(GoldFlow::BoughtFromNpcVendor, 30, "i_bandage", 1000);
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
    trained.Note(GoldFlow::PaidTrainer, 108, "i_scroll_blank", 1000);
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

    Check(NpcBuyersFor("i_log").empty(),
          "so NO npc buyer is offered for logs, whatever the scripts allow");
    Check(NpcBuyersFor("i_ingot_iron").empty(),
          "nor for ingots");
    Check(!HasNpcBuyer("i_log"), "HasNpcBuyer agrees");
    Check(NpcBuyersFor(nullptr).empty(), "a null query is not a crash");

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
    // memory, because the answer moved twice while this was being written.
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

    // The owner's line: MATERIALS to players, FINISHED GOODS may go to an NPC.
    // A smith's spear, an alchemist's potions and a mage's scrolls are
    // finished; everything the lumberjack/carpenter makes below Carpentry 95
    // is a material -- boards, parchment, blank scrolls.
    Check(withNpcIncome.count("miner_smith") == 1, "a smith sells its spears");
    Check(withNpcIncome.count("alchemist") == 1, "an alchemist sells potions");
    Check(withNpcIncome.count("mage") == 1, "a mage sells scrolls");

    // And the one that is NOT a gap. The owner said it directly -- "craft as
    // carpenter and sell stuff to other players" -- so a carpenter with no NPC
    // channel is the intended design, not a missing table row. Its only
    // non-material product in the graph is i_model_ship at Carpentry 95.0
    // (Production.cpp:127), which is endgame.
    // The carpenter DOES have one, and it is a club: a finished weapon, so
    // the blunt weaponsmith buys it (tm_vend.scp:1710) rather than the
    // carpenter. Its materials -- logs, boards, blank scrolls -- still go to
    // players only, which is the line holding.
    Check(withNpcIncome.count("lumberjack_swordsman") == 1,
          "the lumberjack/carpenter earns from clubs");
    Ledger fresh;
    const prof::Profession* ljp = prof::Find("lumberjack_swordsman");
    if (ljp) {
        Check(MaySellToNpc(*ljp, "i_club", fresh).allowed, "a club may be sold");
        Check(!MaySellToNpc(*ljp, "i_log", fresh).allowed, "a log may not");
        Check(!MaySellToNpc(*ljp, "i_board", fresh).allowed, "nor a board");
        Check(!MaySellToNpc(*ljp, "i_scroll_blank", fresh).allowed,
              "nor a blank scroll -- that one goes to a mage, player to player");
    }
    const std::vector<const NpcBuyer*> clubBuyers = NpcBuyersFor("i_club");
    Check(!clubBuyers.empty() &&
          std::string(clubBuyers.front()->trade) == "weaponsmith",
          "and the buyer is the weaponsmith, because a club is a weapon");

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
    const prof::Profession* mg = prof::Find("mage");
    Check(mg != nullptr, "the mage exists");
    if (!mg) return;

    Ledger dirty;
    dirty.Note(GoldFlow::BoughtFromNpcVendor, 40, "i_reag_nightshade", 1000);
    // The mage produces spell scrolls, which are PlayerCrafted, so this is
    // refused on the POLICY gate before the ledger is even consulted -- and
    // that ordering is deliberate: the cheaper, more fundamental test first.
    const SellRuling r = MaySellToNpc(*mg, "i_scroll_poison", dirty);
    Check(!r.allowed, "a crafted scroll is not an NPC good");

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

    // A club HAS an NPC buyer, so the player market does not need to carry it
    // -- that is a shorter errand and the announcement would be noise.
    PriceBook clubBook;
    PriceObservation cp;
    cp.item = "i_club"; cp.pricePerUnit = 12;
    cp.source = PriceSource::NpcVendorBuys; cp.who = "weaponsmith"; cp.whenMs = 1;
    clubBook.Note(cp);
    const std::vector<Stock> clubs = {{"i_club", 40}};
    Check(!ChooseSellOffer(*lj, clubs, clubBook, pol, &out),
          "what an NPC will buy is not announced to players");
}

// --------------------------------------------------------------------------
void TestWhetherToAnswer() {
    Section("trade: a buyer answers only for what its own life needs");

    const prof::Profession* ms = prof::Find("miner_smith");
    const prof::Profession* mg = prof::Find("mage");
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
    TestNpcsMayNotBuyPlayerMarketGoods();
    TestWhatAnNpcMayStillBuy();
    TestArbitrageGuardStillApplies();
    TestSpokenOffers();
    TestWhatToAnnounce();
    TestWhetherToAnswer();
    TestTheChainCanActuallyClose();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
