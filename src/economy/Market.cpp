#include "uo/market.h"

#include "uo/production.h"
#include "uo/vendor_policy.h"

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

    // WHICH TAP IS THIS? Selling to an NPC is the one flow that CREATES gold,
    // and on Revolution only a few narrow kinds of good did it. Everything
    // else was players trading with players.
    switch (ClassifyForNpcSale(item)) {
        case NpcSellClass::Fish:
        case NpcSellClass::CookedFood:
        case NpcSellClass::MobLoot:
        case NpcSellClass::ScribedScroll:
        case NpcSellClass::CraftedGood:
            break;   // a tap. Carry on to the arbitrage test.
        case NpcSellClass::RawResource:
            out.reason = "a raw resource: an NPC price would set a floor and "
                         "no player would pay more than the floor";
            return out;
        case NpcSellClass::PlayerMarketGood:
            out.reason = "a player-market good: this is what players buy from "
                         "each other, and an NPC buyer replaces that";
            return out;
        default:
            out.reason = "no Revolution evidence that an NPC bought this";
            return out;
    }

    // The arbitrage test. Every RAW input of the item -- the leaves of the
    // production graph, not the intermediate steps -- checked against what
    // this character has actually paid an NPC for.
    // EVERY input at EVERY level, not just the raw leaves. The first version
    // asked RawInputsFor(), which walks down to the leaves of the graph -- so
    // a scroll's raw inputs are logs and reagents, and a blank scroll bought
    // from a vendor slipped straight past it. The intermediate step is exactly
    // where a vendor loop is cheapest to open.
    std::vector<std::string> inputs;
    for (const uo::prod::Ingredient& in : uo::prod::RawInputsFor(item, 1)) {
        if (in.item) inputs.emplace_back(in.item);
    }
    bool cycle = false;
    for (const uo::prod::Recipe* r : uo::prod::ProductionOrder(item, &cycle)) {
        if (!r) continue;
        for (const uo::prod::Ingredient& in : r->inputs) {
            if (in.item) inputs.emplace_back(in.item);
        }
    }

    for (const std::string& needed : inputs) {
        for (const GoldEntry& e : ledger.entries) {
            if (e.flow != GoldFlow::BoughtFromNpcVendor) continue;
            if (e.detail != needed) continue;
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

// What the STOCK SCRIPTS allow, with tm_vend.scp line numbers as the citation.
//
// This table is a fact about Sphere, not a permission. NpcBuyersFor() filters
// it through the M3.7 vendor policy, and on Revolution both of these are
// player-market goods -- so today the filter returns nothing for either, and
// that is correct rather than a gap in the table.
const NpcBuyer kNpcBuyers[] = {
    // --- the taps. These are the rows NpcBuyersFor can actually return -----
    // Spell scrolls: the mage shop buys them back. tm_vend.scp:1848
    // (VENDOR_B_MAGE_3RD, i_scroll_poison) and :1881 (VENDOR_B_MAGE_4TH,
    // i_scroll_recall), both {10 15}. This is the scribe's income the owner
    // named.
    {"i_scroll_poison",      "mage"},
    {"i_scroll_recall",      "mage"},
    {"i_scroll_gate_travel", "mage"},
    {"i_scroll_resurrection","mage"},
    // Fish: tm_vend.scp:730 (VENDOR_B_COOK) and :1022 (VENDOR_B_FISHER),
    // both {4 24}.
    // The fisher is named first because the owner named it: it buys fish
    // steaks and raw fish both. The cook's list (tm_vend.scp:730) overlaps.
    {"i_fish_small",         "fisher"},
    {"i_fish_big_1",         "fisher"},
    {"i_fish_big_2",         "fisher"},
    {"i_fish_big_3",         "fisher"},
    {"i_fish_big_4",         "fisher"},
    {"i_fish_cut_raw",       "fisher"},
    {"i_fish_cut_raw",       "cook"},
    {"i_fish_cut_cooked",    "fisher"},
    {"i_fish_cut_cooked",    "cook"},

    // Crafted goods, with the template that actually carries the BUY row.
    // Verified per item -- i_tunic_leather has NO buy row anywhere, so leather
    // armour genuinely has no NPC channel on this shard whatever the policy
    // permits, and NpcBuyersFor correctly returns nothing for it.
    {"i_spear_short",        "weaponsmith"},   // tm_vend.scp:1716 WEAPONS_BLUNT
    {"i_club",               "weaponsmith"},   // tm_vend.scp:1710 {10 15}
    {"i_robe",               "tailor"},        // tm_vend.scp:865  TAILOR
    {"i_potion_cure",        "alchemist"},     // tm_vend.scp:510  ALCHEMIST
    {"i_potion_refresh",     "alchemist"},
    {"i_crossbow",           "bowyer"},        // tm_vend.scp:1444 BOWYER
    {"i_bow",                "bowyer"},

    // --- refused by policy, kept for the record -----------------------------
    // The rows below describe what the STOCK SCRIPTS allow. NpcBuyersFor
    // filters them out, because on Revolution both are player-market goods.
    // They stay so the table shows what was considered and rejected rather
    // than looking like an oversight.
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
    // Only buyers we may LEGITIMATELY use. The table below records what the
    // stock scripts allow; the policy decides what Revolution permits, and it
    // is the policy that answers. Returning a buyer the character would then
    // refuse on arrival is a 224-tile walk to say no.
    switch (ClassifyForNpcSale(item)) {
        case NpcSellClass::Fish:
        case NpcSellClass::CookedFood:
        case NpcSellClass::MobLoot:
        case NpcSellClass::ScribedScroll:
        case NpcSellClass::CraftedGood:
            break;
        default:
            return out;   // no legitimate NPC buyer for this
    }
    for (const NpcBuyer& b : kNpcBuyers) {
        if (std::strcmp(b.item, item) == 0) out.push_back(&b);
    }
    return out;
}

bool HasNpcBuyer(const char* item) { return !NpcBuyersFor(item).empty(); }

const char* NpcSellClassName(NpcSellClass c) {
    switch (c) {
        case NpcSellClass::Unknown:          return "UNKNOWN";
        case NpcSellClass::Fish:             return "FISH";
        case NpcSellClass::CookedFood:       return "COOKED_FOOD";
        case NpcSellClass::CraftedGood:      return "CRAFTED_GOOD";
        case NpcSellClass::MobLoot:          return "MOB_LOOT";
        case NpcSellClass::ScribedScroll:    return "SCRIBED_SCROLL";
        case NpcSellClass::RawResource:      return "RAW_RESOURCE";
        case NpcSellClass::PlayerMarketGood: return "PLAYER_MARKET_GOOD";
        case NpcSellClass::Count:            break;
    }
    return "?";
}

namespace {

struct SellRow { const char* item; NpcSellClass klass; };

// Evidence class O (project owner, describing Revolution, 2026-08-28) for the
// three taps; everything else is either explicitly refused with a reason or
// left off the table entirely, which refuses it as UNKNOWN.
const SellRow kSellMatrix[] = {
    // --- the taps: where gold enters the shard ---------------------------
    // The fisher buys raw fish AND fish steaks (owner, 2026-08-28). Cooked is
    // worth about 1 gp more -- roughly 5 against 4 -- which matches TNS's own
    // saturating buyer exactly: System_SellerBuro.scp:426-440 pays 5 gp for
    // cooked fish until the day's shard-wide volume passes 500,000.
    {"i_fish_small",        NpcSellClass::Fish},
    {"i_fish_big_1",        NpcSellClass::Fish},
    {"i_fish_big_2",        NpcSellClass::Fish},
    {"i_fish_big_3",        NpcSellClass::Fish},
    {"i_fish_big_4",        NpcSellClass::Fish},
    {"i_fish_cut_raw",      NpcSellClass::Fish},
    {"i_fish_cut_cooked",   NpcSellClass::Fish},
    {"i_scroll_poison",     NpcSellClass::ScribedScroll},
    {"i_scroll_recall",     NpcSellClass::ScribedScroll},
    {"i_scroll_gate_travel",NpcSellClass::ScribedScroll},
    {"i_scroll_resurrection",NpcSellClass::ScribedScroll},

    // --- refused, and the reason is worth keeping separate ---------------
    // Raw: an NPC price sets a floor and the player market for it dies.
    {"i_log",               NpcSellClass::RawResource},
    {"i_board",             NpcSellClass::RawResource},
    {"i_ore_iron",          NpcSellClass::RawResource},
    {"i_hide",              NpcSellClass::RawResource},
    {"i_wool",              NpcSellClass::RawResource},
    // Player-market: what players buy from each other.
    {"i_ingot_iron",        NpcSellClass::PlayerMarketGood},
    {"i_spear_short",       NpcSellClass::CraftedGood},   // smithing
    // A carpenter's club. It is a WEAPON, so the blunt weaponsmith buys it,
    // not the carpenter (owner, 2026-08-28: "since it is weapon sell it
    // weaponsmith").
    {"i_club",              NpcSellClass::CraftedGood},   // carpentry
    {"i_scroll_blank",      NpcSellClass::PlayerMarketGood},
    {"i_potion_refresh",    NpcSellClass::CraftedGood},   // alchemy
    {"i_potion_cure",       NpcSellClass::CraftedGood},
    {"i_cloth",             NpcSellClass::RawResource},   // a material
    {"i_robe",              NpcSellClass::CraftedGood},   // tailoring
    // Leather and armour go to PLAYERS (owner, 2026-08-28: "leather and armor
    // to the players"). This closes the UNKNOWN that stood here -- the earlier
    // quote listed "collect leather crafting leather or armor set or mage robe
    // and selling those too" without naming the buyer.
    {"i_hides_cut",         NpcSellClass::RawResource},   // a material
    {"i_leather",           NpcSellClass::RawResource},
    // Finished leather armour is a TAILORING good, so it may go to an NPC --
    // even though the owner's preference is that it goes to players. Allowing
    // the NPC channel does not forbid the player one; it is a floor, and once
    // player trade exists the sale path should prefer a player.
    {"i_boots_thigh",       NpcSellClass::CraftedGood},
    {"i_tunic_leather",     NpcSellClass::CraftedGood},
    {"i_leggings_leather",  NpcSellClass::CraftedGood},
    {"i_gorget_leather",    NpcSellClass::CraftedGood},
    {"i_cap_leather",       NpcSellClass::CraftedGood},
    {"i_sleeves_leather",   NpcSellClass::CraftedGood},
    // Bowcraft: the guide says explicitly "players or NPC vendors".
    {"i_bow",               NpcSellClass::CraftedGood},
    {"i_crossbow",          NpcSellClass::CraftedGood},
};

}  // namespace

NpcSellClass ClassifyForNpcSale(const char* item) {
    if (!item) return NpcSellClass::Unknown;
    for (const SellRow& r : kSellMatrix) {
        if (std::strcmp(r.item, item) == 0) return r.klass;
    }
    return NpcSellClass::Unknown;
}

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
// Player-to-player trade
// ---------------------------------------------------------------------------
namespace {

std::string Lower(const std::string& s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// Read a run of digits at `i`, advancing it. -1 when there are none.
i32 ReadInt(const std::string& s, usize& i) {
    if (i >= s.size() || s[i] < '0' || s[i] > '9') return -1;
    i64 v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        if (v > 1000000) return -1;   // not a quantity anybody means
        ++i;
    }
    return static_cast<i32>(v);
}

void SkipSpace(const std::string& s, usize& i) {
    while (i < s.size() && s[i] == ' ') ++i;
}

std::string ReadWord(const std::string& s, usize& i) {
    SkipSpace(s, i);
    const usize start = i;
    while (i < s.size() && s[i] != ' ') ++i;
    return s.substr(start, i - start);
}

}  // namespace

std::string FormatSellOffer(const TradeIntent& t) {
    return "WTS " + std::to_string(t.qty) + " " + t.item + " " +
           std::to_string(t.pricePerUnit) + "gp";
}

std::string FormatBuyReply(const std::string& item) {
    return "WTB " + item;
}

bool ParseSellOffer(const std::string& said, TradeIntent* out) {
    if (!out) return false;
    const std::string low = Lower(said);
    const usize at = low.find("wts ");
    if (at == std::string::npos) return false;

    usize i = at + 4;
    const i32 qty = ReadInt(low, i);
    if (qty <= 0) return false;
    const std::string item = ReadWord(low, i);
    if (item.empty()) return false;
    SkipSpace(low, i);
    const i32 price = ReadInt(low, i);
    if (price < 0) return false;

    out->item = item;
    out->qty = qty;
    out->pricePerUnit = price;
    return out->Valid();
}

bool ParseBuyReply(const std::string& said, std::string* itemOut) {
    if (!itemOut) return false;
    const std::string low = Lower(said);
    const usize at = low.find("wtb ");
    if (at == std::string::npos) return false;
    usize i = at + 4;
    const std::string item = ReadWord(low, i);
    if (item.empty()) return false;
    *itemOut = item;
    return true;
}

bool ChooseSellOffer(const prof::Profession& p,
                     const std::vector<Stock>& pack,
                     const PriceBook& book,
                     const TradePolicy& policy,
                     TradeIntent* out) {
    if (!out) return false;
    for (const Offer& o : Surplus(p, pack, policy)) {
        // If an NPC will take it, that is a shorter errand and the player
        // market does not need to carry it. This is the whole reason the
        // player path exists: it is for what the NPCs refuse.
        if (HasNpcBuyer(o.item.c_str())) continue;

        const i32 believed = book.BelievedSalePrice(o.item.c_str());
        // No belief is not silence. See TradePolicy::openingAsk: a belief
        // without evidence would be a lie, but an OPENING ASK is just an
        // offer, and somebody has to name a number first or no price is ever
        // discovered and the market never starts.
        out->item = o.item;
        out->qty = o.qty;
        out->pricePerUnit = (believed >= 0) ? believed : policy.openingAsk;
        return out->Valid();
    }
    return false;
}

BuyDecision ConsiderOffer(const prof::Profession& p,
                          const std::vector<Stock>& pack,
                          i32 gold,
                          const TradePolicy& policy,
                          const TradeIntent& offer) {
    BuyDecision d;
    if (!offer.Valid()) {
        d.reason = "not a well-formed offer";
        return d;
    }

    // Do I actually want it? Shortfall answers from the catalogue, so a bot
    // never buys something its life has no use for -- which is what stops a
    // fleet turning into a room full of speculators.
    i32 want = 0;
    for (const Want& w : Shortfall(p, pack, policy)) {
        if (w.item == offer.item) { want = w.qty; break; }
    }
    if (want <= 0) {
        d.reason = "this life has no use for it";
        return d;
    }

    const i32 qty = std::min(want, offer.qty);
    const i32 cost = qty * offer.pricePerUnit;
    if (cost > gold) {
        d.reason = "cannot afford it";
        return d;
    }
    // Never spend the reserve. It is what buys a replacement tool after a
    // death, and a character that trades it away is one bad fight from
    // unemployable.
    if (gold - cost < p.goldReserve) {
        d.reason = "would eat into the reserve this life keeps for tools";
        return d;
    }

    // A price ceiling, because a bot with a full purse will otherwise accept
    // any number a seller says and one greedy seller drains the fleet.
    if (offer.pricePerUnit > policy.blindPriceCeiling) {
        d.reason = "more than this life will pay sight unseen";
        return d;
    }

    d.accept = true;
    d.qty = qty;
    d.reason = "needed, affordable, and within the price ceiling";
    return d;
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
