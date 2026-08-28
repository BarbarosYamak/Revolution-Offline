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
