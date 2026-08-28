#pragma once

// M7 -- what one character has to sell, what it has to buy, and the only
// prices it is allowed to know.
//
// The rule this file exists to enforce: A BOT MAY NOT KNOW THE MARKET. There
// is no global price index, no shard-wide inventory, no "cheapest vendor"
// query. A character knows a price because it stood in front of a vendor and
// read it, or because another character quoted it. That is the whole source.
//
// Everything here is a pure function of a profession record and what the
// character is carrying, so it runs under ctest with no server.

#include "uo/professions.h"
#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::market {

// One line of a pack, by itemdef defname (the same keys the production graph
// and the profession catalogue use).
struct Stock {
    std::string item;
    i32         qty = 0;
};

i32 QtyOf(const std::vector<Stock>& pack, const std::string& item);

// Something this character will hand over. Quantity only -- the price comes
// from PriceBook, which only knows what was observed.
struct Offer {
    std::string item;
    i32         qty = 0;
    // Why it is spare, printable, so a trade can be explained afterwards.
    std::string reason;
};

// Something this character needs and cannot make.
struct Want {
    std::string item;
    i32         qty = 0;
    // True when NO profession in the catalogue produces it -- the world does,
    // so a player supplier is impossible and a vendor is the only option.
    bool        rawResource = false;
    std::string reason;
};

// How much of its own output a life keeps back before selling any. A smith
// that sells every ingot cannot smith tomorrow.
struct TradePolicy {
    i32 keepOfOwnOutput = 20;
    i32 restockConsumablesTo = 20;
    // Below this, gathering more of something to sell is pointless -- it will
    // all be eaten by the character's own work.
    i32 minimumSurplusToOffer = 5;
};

// What this life can spare. Reads `produces` from the catalogue: a life only
// ever offers what its own profession makes.
std::vector<Offer> Surplus(const prof::Profession& p,
                           const std::vector<Stock>& pack,
                           const TradePolicy& policy);

// What this life is short of. Reads `consumes` and `consumables`.
std::vector<Want> Shortfall(const prof::Profession& p,
                            const std::vector<Stock>& pack,
                            const TradePolicy& policy);

// Which professions in the catalogue produce `item`. This is CATALOGUE
// knowledge -- "a smith is the sort of person who makes ingots" -- which a
// player plainly has. It says nothing about who is online, where they are, or
// what they charge, and it must never be used as if it did.
std::vector<const prof::Profession*> WhoProduces(const char* item);

// True when the two lives can trade in one direction: a produces something b
// consumes. The M7 interdependence test.
bool CanSupply(const prof::Profession& producer,
               const prof::Profession& consumer, std::string* itemOut = nullptr);

// ---------------------------------------------------------------------------
// Selling to an NPC. Permitted, but not unconditionally.
// ---------------------------------------------------------------------------

struct Ledger;   // defined below

struct SellRuling {
    bool        allowed = false;
    const char* reason  = nullptr;   // always populated, allowed or not
};

// WHERE NEW GOLD ENTERS THE SHARD.
//
// The buying question and the selling question are NOT the same question, and
// answering the second with the first's table was wrong. A spell scroll is
// PlayerCrafted -- a bot must never BUY one from an NPC, because that
// shortcuts the whole scribing profession -- and yet Revolution scribes made
// their living SELLING scrolls to vendors. Both are true at once.
//
// Project owner, 2026-08-28, describing Revolution directly:
//
//   "you cant sell logs to npc only to players"
//   "whole economy based on players mostly except fishing, killing mobs,
//    scribe etc"
//   "people were scribing scroll and selling scrolls to vendor make money or
//    caught fish cook fish then sell"
//
// So the shard has a few narrow taps where gold is created, and everything
// else is players trading with players. This enumerates the taps. Anything
// not listed is UNKNOWN and refused, and the accumulated refusals are the
// research backlog -- the same discipline the buying side has followed since
// M3.7.
// THE LINE, as the owner drew it (2026-08-28): MATERIALS go to players,
// FINISHED GOODS may go to an NPC.
//
// That is a coherent rule rather than a list of exceptions. A crafted item
// carries player labour -- gathering, a skill, a failure rate -- so the gold an
// NPC pays for it is bought with real time. A raw material carries none, and an
// NPC price for it just sets a floor that kills the player market underneath.
//
// Owner's research table, same date, with the ALLOW column as decided:
//
//   raw fish, cooked fish        ALLOW  Revolution guide, explicit
//   cooked animal food products  ALLOW  14 Apr 2008 update
//   crafted bows                 ALLOW  guide: "players or NPC vendors"
//   scrolls                      ALLOW  proven live on this shard
//   carpentry, smithing,
//   tailoring, tinkering,
//   alchemy                      ALLOW  owner decision; the historical NPC
//                                       channel is NOT confirmed, and that is
//                                       recorded rather than dressed up
//   raw logs, iron ingots        REFUSE Revolution listed both as player-market
enum class NpcSellClass : u8 {
    Unknown = 0,      // no evidence and no decision. Refused.
    // --- taps: an NPC pays, and new gold enters the shard ---------------
    Fish,             // raw or cooked; the fisher takes both
    CookedFood,       // animal food products (14 Apr 2008)
    MobLoot,          // what a corpse yielded
    ScribedScroll,    // a scribe's stock in trade
    CraftedGood,      // carpentry, smithing, tailoring, tinkering, alchemy,
                      // bowcraft -- a finished item, not a material
    // --- refused, and the refusal says which kind ------------------------
    RawResource,      // logs, boards, ore, hides -- materials, not goods
    PlayerMarketGood, // ingots and anything Revolution named as player-traded
    Count,
};

const char* NpcSellClassName(NpcSellClass c);

// How Revolution treated SELLING this to an NPC.
NpcSellClass ClassifyForNpcSale(const char* item);

// May this life sell `item` to an NPC vendor?
//
// Selling to an NPC CREATES gold -- the vendor pays from nowhere -- so it is
// the one flow that can print money. What makes it legitimate is a real
// player-side loss on the other side: the log came out of a tree the character
// had to find and swing at, and that time is the cost.
//
// What makes it ILLEGITIMATE is buying the inputs from an NPC too. Then the
// cycle is vendor -> craft -> vendor with no world contact at all, and
// tools/economy_arbitrage.py finds 66 such loops on this shard's own itemdefs
// (a cake returns +16.6 gp at 2.44x, a crossbow +20.0 at 3.48x). The audit's
// invariant states it directly:
//
//   No deterministic NPC/vendor/crafting/recycling cycle may generate net gold
//   or resources without a player-side loss.
//
// So the ledger is the evidence: if this character bought a raw input of
// `item` from an NPC, selling the result back to one is refused.
SellRuling MaySellToNpc(const prof::Profession& p, const char* item,
                        const Ledger& ledger);

// --- who on this shard will buy a thing -------------------------------------
//
// Read off the shard's own vendor buy-templates
// (runtime/scripts/templates/tm_vend.scp), not inferred from the trade name --
// and the difference matters. The obvious guess for logs is the LUMBERJACK
// vendor, and it is wrong: c_lumberjack SELLS logs and buys only axes
// (c_vendor_human.scp:2853-2922). The carpenter is the one that buys them.
//
// Trades are named as the paperdoll-title substring to look for. Mapping a
// trade to a travel destination is the caller's job, because that is world-
// model knowledge and this layer stays protocol-free.
struct NpcBuyer {
    const char* item;
    const char* trade;
};

// Every trade that buys `item`, best price first. Empty is a real answer: it
// means no NPC on this shard takes it, and the character should bank the goods
// rather than walk the world looking for a buyer that does not exist.
std::vector<const NpcBuyer*> NpcBuyersFor(const char* item);

// Cheap form of the same question, for the need layer.
bool HasNpcBuyer(const char* item);

// ---------------------------------------------------------------------------
// Prices. Observed only.
// ---------------------------------------------------------------------------

enum class PriceSource : u8 {
    NpcVendorSells = 0,   // read off an open vendor window: what it charges us
    NpcVendorBuys,        // what it offered US for the same thing
    PlayerQuoted,         // another character said a number
    PlayerTraded,         // a trade that actually completed at this number
};

const char* PriceSourceName(PriceSource s);

struct PriceObservation {
    std::string item;
    i32         pricePerUnit = 0;
    PriceSource source = PriceSource::NpcVendorSells;
    std::string who;             // vendor or character name, as seen
    i32         x = 0, y = 0;
    i64         whenMs = 0;
};

// Everything one character has ever seen a price be. Per-character by
// construction: it is stored in that character's own memory file and is never
// merged with anybody else's.
class PriceBook {
public:
    void Note(const PriceObservation& o);

    // The most recent observation of `item` from `source`, or nullptr. Returns
    // NOTHING rather than a guess when the character has never seen one --
    // "I do not know what this is worth" is a legitimate and common state.
    const PriceObservation* Latest(const char* item, PriceSource source) const;

    // What this character believes it can get for one unit, or -1 if it has
    // no basis. A completed player trade outranks a quote, which outranks an
    // NPC's buy price, because that is the order of how much each one proves.
    i32 BelievedSalePrice(const char* item) const;

    // Anything older than `maxAgeMs` is stale: prices move, and acting on a
    // week-old number is not knowledge.
    void Expire(i64 nowMs, i64 maxAgeMs);

    const std::vector<PriceObservation>& All() const { return obs_; }
    std::vector<PriceObservation>&       Mutable()   { return obs_; }
    usize Size() const { return obs_.size(); }

private:
    std::vector<PriceObservation> obs_;
};

// ---------------------------------------------------------------------------
// Where the gold went. Telemetry, not a rule.
// ---------------------------------------------------------------------------

enum class GoldFlow : u8 {
    // Sources
    LootedFromCorpse = 0,
    SoldToNpcVendor,
    SoldToPlayer,
    StartingKit,
    // Sinks
    BoughtFromNpcVendor,
    BoughtFromPlayer,
    PaidTrainer,
    Count,
};

const char* GoldFlowName(GoldFlow f);
// True for the entries that ADD gold to the shard's player economy.
bool IsGoldSource(GoldFlow f);

struct GoldEntry {
    GoldFlow    flow = GoldFlow::LootedFromCorpse;
    i32         amount = 0;      // always positive; the flow says the direction
    std::string detail;          // what was bought/sold/trained
    i64         whenMs = 0;
};

// A character's own ledger. The point is the anti-arbitrage invariant: if
// sources consistently exceed sinks with no player-side loss, the economy is
// printing gold, and the audit's ratchet test should be failing.
struct Ledger {
    std::vector<GoldEntry> entries;

    void Note(GoldFlow f, i32 amount, const char* detail, i64 whenMs);
    i32  TotalIn() const;
    i32  TotalOut() const;
    i32  Net() const { return TotalIn() - TotalOut(); }
    i32  TotalFor(GoldFlow f) const;
};

}  // namespace uo::market
