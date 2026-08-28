#include "uo/market.h"

#include "uo/production.h"

#include <algorithm>
#include <cstring>

namespace uo::market {

i32 QtyOf(const std::vector<Stock>& pack, const std::string& item) {
    for (const Stock& s : pack) {
        if (s.item == item) return s.qty;
    }
    return 0;
}

const char* PriceSourceName(PriceSource s) {
    switch (s) {
        case PriceSource::NpcVendorSells: return "npc_sells";
        case PriceSource::NpcVendorBuys:  return "npc_buys";
        case PriceSource::PlayerQuoted:   return "player_quoted";
        case PriceSource::PlayerTraded:   return "player_traded";
    }
    return "?";
}

const char* GoldFlowName(GoldFlow f) {
    switch (f) {
        case GoldFlow::LootedFromCorpse:    return "looted_from_corpse";
        case GoldFlow::SoldToNpcVendor:     return "sold_to_npc_vendor";
        case GoldFlow::SoldToPlayer:        return "sold_to_player";
        case GoldFlow::StartingKit:         return "starting_kit";
        case GoldFlow::BoughtFromNpcVendor: return "bought_from_npc_vendor";
        case GoldFlow::BoughtFromPlayer:    return "bought_from_player";
        case GoldFlow::PaidTrainer:         return "paid_trainer";
        case GoldFlow::Count:               break;
    }
    return "?";
}

bool IsGoldSource(GoldFlow f) {
    switch (f) {
        // Only these three put gold that did not exist before into a player's
        // hands. A sale to another PLAYER is not a source -- it moves gold
        // sideways, which is the whole point of a player economy.
        case GoldFlow::LootedFromCorpse:
        case GoldFlow::SoldToNpcVendor:
        case GoldFlow::StartingKit:
            return true;
        default:
            return false;
    }
}

namespace {

// Does this life feed `item` back into something else it makes?
bool IsOwnInput(const prof::Profession& p, const std::string& item) {
    for (const std::string& made : p.produces) {
        if (made == item) continue;
        const uo::prod::Recipe* r = uo::prod::FindRecipe(made.c_str());
        if (!r) continue;
        for (const uo::prod::Ingredient& in : r->inputs) {
            if (in.item && item == in.item) return true;
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Surplus and shortfall
// ---------------------------------------------------------------------------
std::vector<Offer> Surplus(const prof::Profession& p,
                           const std::vector<Stock>& pack,
                           const TradePolicy& policy) {
    std::vector<Offer> out;
    // A life offers only what its own profession makes. It does not become a
    // trader in something it happened to pick up: that is how a "miner" turns
    // into an omniscient reseller, which is exactly what M7 forbids.
    for (const std::string& item : p.produces) {
        const i32 have = QtyOf(pack, item);

        // The reserve exists so a smith that sells every ingot can still
        // smith tomorrow -- so it applies ONLY to output this life feeds back
        // into its OWN recipes. A lumberjack does not eat logs; holding twenty
        // back would just be twenty logs it never sells and never uses.
        //
        // The question is answered by the production graph, not by the
        // catalogue's `consumes` field: that field means "must obtain from
        // someone else", so a smith's own ingots are correctly absent from it
        // even though every weapon it makes eats six of them.
        const bool selfConsumed = IsOwnInput(p, item);
        const i32 reserve = selfConsumed ? policy.keepOfOwnOutput : 0;

        const i32 spare = have - reserve;
        if (spare < policy.minimumSurplusToOffer) continue;
        Offer o;
        o.item = item;
        o.qty = spare;
        o.reason = selfConsumed ? "own output beyond the working reserve"
                                : "own output; this life does not consume it";
        out.push_back(std::move(o));
    }
    return out;
}

std::vector<Want> Shortfall(const prof::Profession& p,
                            const std::vector<Stock>& pack,
                            const TradePolicy& policy) {
    std::vector<Want> out;

    // Inputs the profession's own recipes eat.
    for (const std::string& item : p.consumes) {
        const i32 have = QtyOf(pack, item);
        if (have >= policy.restockConsumablesTo) continue;
        Want w;
        w.item = item;
        w.qty = policy.restockConsumablesTo - have;
        w.rawResource = WhoProduces(item.c_str()).empty();
        w.reason = w.rawResource
            ? "an input no profession makes -- the world does, so this is a "
              "vendor or a gathering trip, not a player supplier"
            : "an input another character's profession produces";
        out.push_back(std::move(w));
    }

    // Consumables the catalogue names with their own floors.
    for (const prof::ConsumableNeed& c : p.consumables) {
        const i32 have = QtyOf(pack, c.name);
        if (have >= c.low) continue;
        Want w;
        w.item = c.name;
        w.qty = std::max(1, c.restockTo - have);
        w.rawResource = WhoProduces(c.name.c_str()).empty();
        w.reason = "below this life's own floor for it";
        out.push_back(std::move(w));
    }
    return out;
}

std::vector<const prof::Profession*> WhoProduces(const char* item) {
    std::vector<const prof::Profession*> out;
    if (!item) return out;
    for (const prof::Profession& p : prof::All()) {
        for (const std::string& made : p.produces) {
            if (made == item) { out.push_back(&p); break; }
        }
    }
    return out;
}

bool CanSupply(const prof::Profession& producer,
               const prof::Profession& consumer, std::string* itemOut) {
    for (const std::string& made : producer.produces) {
        for (const std::string& eaten : consumer.consumes) {
            if (made != eaten) continue;
            if (itemOut) *itemOut = made;
            return true;
        }
    }
    return false;
}

SellRuling MaySellToNpc(const prof::Profession& p, const char* item,
                        const Ledger& ledger) {
    SellRuling out;
    if (!item) { out.reason = "no item named"; return out; }

    // A life sells what it MAKES. Selling something it merely picked up turns
    // a character into a fence, and there is no world contact behind the sale.
    bool mine = false;
    for (const std::string& made : p.produces) {
        if (made == item) { mine = true; break; }
    }
    if (!mine) {
        out.reason = "this life does not produce it";
        return out;
    }

    // The arbitrage test. Every RAW input of the item -- the leaves of the
    // production graph, not the intermediate steps -- checked against what
    // this character has actually paid an NPC for.
    const std::vector<uo::prod::Ingredient> raw = uo::prod::RawInputsFor(item, 1);
    for (const uo::prod::Ingredient& in : raw) {
        if (!in.item) continue;
        for (const GoldEntry& e : ledger.entries) {
            if (e.flow != GoldFlow::BoughtFromNpcVendor) continue;
            if (e.detail != in.item) continue;
            out.reason = "its inputs were bought from an NPC -- selling the "
                         "result back to one would be a closed vendor loop";
            return out;
        }
    }

    out.allowed = true;
    out.reason = "own output, from inputs the world provided";
    return out;
}

namespace {

// tm_vend.scp buy-template line numbers are the citation for each row.
const NpcBuyer kNpcBuyers[] = {
    // i_log -- :167 CARPENTER, :964 TINKER, :1273 PROVISIONER, :1451 BOWYER,
    //          :1685 WEAPONS_BLADED, :1722 WEAPONS_BLUNT, :1935 BLACKSMITH
    {"i_log",        "carpenter"},
    {"i_log",        "provisioner"},
    {"i_log",        "tinker"},
    {"i_log",        "bowyer"},
    {"i_log",        "blacksmith"},
    // i_ingot_iron -- :1936 BLACKSMITH pays 44-88, much the best of them;
    //                 :963 TINKER, :1256 PROVISIONER, :1341 JEWELER
    {"i_ingot_iron", "blacksmith"},
    {"i_ingot_iron", "tinker"},
    {"i_ingot_iron", "provisioner"},
    {"i_ingot_iron", "jeweler"},
};

}  // namespace

std::vector<const NpcBuyer*> NpcBuyersFor(const char* item) {
    std::vector<const NpcBuyer*> out;
    if (!item) return out;
    for (const NpcBuyer& b : kNpcBuyers) {
        if (std::strcmp(b.item, item) == 0) out.push_back(&b);
    }
    return out;
}

bool HasNpcBuyer(const char* item) { return !NpcBuyersFor(item).empty(); }

// ---------------------------------------------------------------------------
// PriceBook
// ---------------------------------------------------------------------------
void PriceBook::Note(const PriceObservation& o) {
    for (PriceObservation& e : obs_) {
        if (e.item == o.item && e.source == o.source && e.who == o.who) {
            e = o;
            return;
        }
    }
    obs_.push_back(o);
}

const PriceObservation* PriceBook::Latest(const char* item,
                                          PriceSource source) const {
    if (!item) return nullptr;
    const PriceObservation* best = nullptr;
    for (const PriceObservation& e : obs_) {
        if (e.item != item || e.source != source) continue;
        if (!best || e.whenMs > best->whenMs) best = &e;
    }
    return best;
}

i32 PriceBook::BelievedSalePrice(const char* item) const {
    // In order of how much each one actually proves. A completed trade is a
    // fact; a quote is a claim; an NPC's buy price is only a floor, and using
    // it as a belief about player prices is how a bot ends up undercutting
    // the whole shard on nothing but vendor data.
    static const PriceSource kOrder[] = {
        PriceSource::PlayerTraded,
        PriceSource::PlayerQuoted,
        PriceSource::NpcVendorBuys,
    };
    for (PriceSource s : kOrder) {
        if (const PriceObservation* o = Latest(item, s)) return o->pricePerUnit;
    }
    return -1;   // never seen one. Not zero, and not a guess.
}

void PriceBook::Expire(i64 nowMs, i64 maxAgeMs) {
    std::vector<PriceObservation> keep;
    keep.reserve(obs_.size());
    for (const PriceObservation& e : obs_) {
        if (nowMs - e.whenMs <= maxAgeMs) keep.push_back(e);
    }
    obs_.swap(keep);
}

// ---------------------------------------------------------------------------
// Ledger
// ---------------------------------------------------------------------------
void Ledger::Note(GoldFlow f, i32 amount, const char* detail, i64 whenMs) {
    if (amount <= 0) return;
    GoldEntry e;
    e.flow = f;
    e.amount = amount;
    e.detail = detail ? detail : "";
    e.whenMs = whenMs;
    entries.push_back(std::move(e));
}

i32 Ledger::TotalIn() const {
    i32 n = 0;
    for (const GoldEntry& e : entries) {
        if (IsGoldSource(e.flow) || e.flow == GoldFlow::SoldToPlayer) n += e.amount;
    }
    return n;
}

i32 Ledger::TotalOut() const {
    i32 n = 0;
    for (const GoldEntry& e : entries) {
        if (!IsGoldSource(e.flow) && e.flow != GoldFlow::SoldToPlayer) n += e.amount;
    }
    return n;
}

i32 Ledger::TotalFor(GoldFlow f) const {
    i32 n = 0;
    for (const GoldEntry& e : entries) {
        if (e.flow == f) n += e.amount;
    }
    return n;
}

}  // namespace uo::market
