#include "uo/market.h"

#include "uo/production.h"
#include "uo/faucets.h"
#include "uo/vendor_policy.h"
// S4: the held/worn/wearable/reserve arithmetic is DecideAcquire's, not a
// second copy of it living in the trade predicate.
#include "uo/activities/acquire.h"

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
        case GoldFlow::CreatedVendor:           return "GOLD_CREATED_VENDOR";
        case GoldFlow::CreatedPvmLoot:          return "GOLD_CREATED_PVM_LOOT";
        case GoldFlow::CreatedTreasure:         return "GOLD_CREATED_TREASURE";
        case GoldFlow::CreatedBounty:           return "GOLD_CREATED_BOUNTY";
        case GoldFlow::CreatedBegging:          return "GOLD_CREATED_BEGGING";
        case GoldFlow::StartingKit:             return "GOLD_CREATED_STARTING_KIT";
        case GoldFlow::DestroyedVendorPurchase: return "GOLD_DESTROYED_VENDOR_PURCHASE";
        case GoldFlow::DestroyedTrainer:        return "GOLD_DESTROYED_TRAINER";
        case GoldFlow::DestroyedService:        return "GOLD_DESTROYED_SERVICE";
        case GoldFlow::TransferPlayerTrade:     return "GOLD_TRANSFER_PLAYER_TRADE";
        case GoldFlow::TransferPlayerTradeOut:  return "GOLD_TRANSFER_PLAYER_TRADE_OUT";
        case GoldFlow::Count:                   break;
    }
    return "?";
}

const char* GoldEffectName(GoldEffect e) {
    switch (e) {
        case GoldEffect::Created:     return "CREATED";
        case GoldEffect::Destroyed:   return "DESTROYED";
        case GoldEffect::Transferred: return "TRANSFERRED";
        case GoldEffect::Count:       break;
    }
    return "?";
}

GoldEffect EffectOf(GoldFlow f) {
    switch (f) {
        case GoldFlow::CreatedVendor:
        case GoldFlow::CreatedPvmLoot:
        case GoldFlow::CreatedTreasure:
        case GoldFlow::CreatedBounty:
        case GoldFlow::CreatedBegging:
        case GoldFlow::StartingKit:
            return GoldEffect::Created;
        case GoldFlow::DestroyedVendorPurchase:
        case GoldFlow::DestroyedTrainer:
        case GoldFlow::DestroyedService:
            return GoldEffect::Destroyed;
        case GoldFlow::TransferPlayerTrade:
        case GoldFlow::TransferPlayerTradeOut:
        case GoldFlow::Count:
            break;
    }
    return GoldEffect::Transferred;
}

bool IsGoldSource(GoldFlow f) { return EffectOf(f) == GoldEffect::Created; }

namespace {

// Does this life feed `item` back into something else it makes? The reserve
// exists to protect tomorrow's work, so it applies only to output the
// character consumes itself -- and the production graph knows that, while the
// catalogue's `consumes` field does not (it means "obtain from someone else",
// so a smith's own ingots are correctly absent from it).
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
        // AN ITEM THE PACK CANNOT COUNT IS AN ITEM THIS LIST MUST NOT NAME.
        //
        // obs.pack is keyed by defname, resolved from graphic+hue through
        // econ::ItemNameForGraphicAndHue -- so a defname with NO row in
        // kGraphics (VendorPolicy.cpp) can never appear in `pack`, QtyOf is
        // permanently 0, and the want is re-issued forever no matter what the
        // character is carrying. i_potion_poisondeadly is exactly that: every
        // fencer and pk asked for 20 of it on every single tick, for good.
        //
        // DELIBERATELY NOT FIXED BY ADDING A GRAPHIC ROW. All four poison
        // tiers share ID=i_bottle_green (0f0a) and the wire carries no way to
        // tell them apart; mapping the strong tier to the shared graphic is
        // the change that already cost Voris his sales (VendorPolicy.cpp:412-
        // 440). Whether a HUE separates the potion tiers on this shard is
        // UNKNOWN -- no evidence either way -- so the honest answer is to say
        // nothing about a want we cannot verify rather than to invent an
        // identity for it. When a graphic row (or a hue rule) exists, this
        // want comes back on its own.
        //
        // The cost is real and recorded: ConsiderOffer reads Shortfall too, so
        // a fencer will now answer "this life has no use for it" to an
        // alchemist offering deadly poison. A trade it cannot verify
        // afterwards is a trade it should not be making blind.
        // (audit 2026-08-30, finding 3.)
        // Reads, in one line: "uncountable in the pack -- skipping until it
        // has a graphic row". Not LOGGED: Shortfall is a pure function with
        // no logger and the skipped want has no Want to carry a reason on.
        if (uo::econ::GraphicsForItem(item.c_str()).empty()) continue;
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

std::vector<Want> PlayerMarketWants(const prof::Profession& p,
                                    const std::vector<Stock>& holdings,
                                    i32 gold,
                                    const TradePolicy& policy,
                                    const char** whyNotOut) {
    std::vector<Want> out;
    if (whyNotOut) *whyNotOut = nullptr;

    // ASKED BEFORE THE JOURNEY, not after it. The identical test lives in
    // ConsiderOffer -- `gold - cost < p.goldReserve` -- and one unit at the
    // blind ceiling is the worst single purchase this life would ever agree
    // to. Below that the trip is wasted before it starts.
    if (gold - policy.blindPriceCeiling < p.goldReserve) {
        if (whyNotOut)
            *whyNotOut = "would eat into the reserve this life keeps for tools";
        return out;
    }

    const std::vector<Want> shortOf = Shortfall(p, holdings, policy);
    for (const Want& w : shortOf) {
        // The world makes it. Go and gather it, or buy it from a vendor --
        // either way nobody is standing at a bank with any to sell.
        if (w.rawResource) continue;
        out.push_back(w);
    }
    if (out.empty() && whyNotOut) {
        *whyNotOut = shortOf.empty()
            ? "stocked on everything this life buys"
            : "short only of things the world makes, not another player";
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
    if (!item) {
        out.refusal = faucet::Refusal::NotSellable;
        out.reason = "no item named";
        return out;
    }

    // A life sells what it MAKES. Selling something it merely picked up turns
    // a character into a fence, and there is no world contact behind the sale.
    bool mine = false;
    for (const std::string& made : p.produces) {
        if (made == item) { mine = true; break; }
    }
    if (!mine) {
        out.refusal = faucet::Refusal::NotSellable;
        out.reason = "this life does not produce it";
        return out;
    }

    // THE REGISTRY DECIDES, not a branch here. Every route carries its own
    // evidence -- separately for history and for this runtime -- and the
    // refusal names which kind of no it is.
    const std::vector<const faucet::GoldFaucet*> routes = faucet::ForItem(item);
    const faucet::GoldFaucet* allowedRoute = nullptr;
    for (const faucet::GoldFaucet* f : routes) {
        if (faucet::Allowed(f->policy)) { allowedRoute = f; break; }
    }
    if (!allowedRoute) {
        if (routes.empty()) {
            out.refusal = faucet::Refusal::NoKnownBuyer;
            out.reason = "no faucet in the registry pays for this";
            return out;
        }
        // Report the FIRST considered route, so the log says which verdict
        // and on what grounds rather than a generic denial.
        const faucet::GoldFaucet* f = routes.front();
        out.via = f;
        out.reason = f->reason;
        switch (f->policy) {
            case faucet::Policy::RefusePlayerMarket:
                out.refusal = faucet::Refusal::PlayerMarketGood;
                break;
            case faucet::Policy::BlockedRuntime:
                out.refusal = faucet::Refusal::EconomicRouteBlocked;
                break;
            case faucet::Policy::RefuseAuthenticity:
            case faucet::Policy::Unknown:
            default:
                out.refusal = faucet::Refusal::RevolutionAuthenticityUnknown;
                break;
        }
        return out;
    }
    out.via = allowedRoute;

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

    // A DOCUMENTED FAUCET OUTRANKS THE HEURISTIC.
    //
    // The loop test below is a guess about routes nobody has evidence for. It
    // is not entitled to overrule a route Revolution's own players describe,
    // and it did: a scribe wrote nine poison scrolls and refused to sell any
    // of them, because it had bought the blanks from the same mage. But
    // buying blanks and reagents from a vendor, writing scrolls and selling
    // them back is EXACTLY what the forum record describes --
    //
    //   "people were scribing scroll and selling scrolls to vendor make money"
    //
    // -- and scribing is one of the three named taps where gold enters this
    // shard, beside fishing and mob loot (docs/REVOLUTION_ECONOMY_FORUM_
    // EVIDENCE.md). The skill and the time are what the margin pays for.
    //
    // So the heuristic applies only where history is silent. Where the
    // registry says Confirmed, the shard has already answered.
    //
    // AND AN OWNER DECISION IS NOT SILENCE. That grade exists precisely to
    // record the owner answering a question the forum record does not cover,
    // and treating it as unanswered turned an explicit permission into a
    // refusal: Voris brewed four poison potions from bought nightshade and
    // then would not sell one of them --
    //   "will NOT sell 4 i_potion_poison -- its inputs were bought from an
    //    NPC and nothing on record says this shard allowed that route"
    // -- while the registry entry immediately above it reads
    // poison_potion_to_alchemist, Policy::Allow, on "Voris it can make poison
    // bottle and it can sell to npc". The heuristic was overruling the
    // authority it exists to defer to. The same applies to the dagger, which
    // is allowed on the same grade and would hit this the moment a smith
    // bought an ingot rather than digging it.
    const bool historyConfirms =
        allowedRoute &&
        (allowedRoute->history == faucet::HistoryEvidence::Confirmed ||
         allowedRoute->history == faucet::HistoryEvidence::OwnerDecision);

    if (!historyConfirms) {
        for (const std::string& needed : inputs) {
            for (const GoldEntry& e : ledger.entries) {
                if (e.flow != GoldFlow::DestroyedVendorPurchase) continue;
                if (e.detail != needed) continue;
                out.refusal = faucet::Refusal::EconomicRouteBlocked;
                out.reason = "its inputs were bought from an NPC and nothing "
                             "on record says this shard allowed that route -- "
                             "selling the result back would be a closed "
                             "vendor loop";
                return out;
            }
        }
    }

    out.allowed = true;
    out.reason = "own output, from inputs the world provided";
    return out;
}


const char* DisposalName(Disposal d) {
    switch (d) {
        case Disposal::Wear:           return "wear";
        case Disposal::OfferToPlayers: return "offer to players";
        case Disposal::SellToNpc:      return "sell to an NPC";
        case Disposal::Bank:           return "bank";
    }
    return "?";
}

DisposalRuling DisposeOfGear(const prof::Profession& p, const char* item,
                             bool wearable, bool playersDeclined,
                             const Ledger& ledger) {
    (void)p;
    DisposalRuling out;
    if (!item || !*item) {
        out.what = Disposal::Bank;
        out.reason = "no item named";
        return out;
    }

    // 1. WEAR IT.
    if (wearable) {
        out.what = Disposal::Wear;
        out.reason = "it fits this life's class and beats what is worn";
        return out;
    }

    // 2. OFFER IT TO PLAYERS -- first, and whatever an NPC would pay.
    if (!playersDeclined) {
        out.what = Disposal::OfferToPlayers;
        out.reason = "this life will not wear it, and the owner's rule is "
                     "players before vendors for everything it did not make";
        return out;
    }

    // 3. SELL IT TO AN NPC, only where the registry establishes the route.
    //
    // Deliberately NOT MaySellToNpc(): that function's first question is
    // "does this life PRODUCE it", and the answer for looted gear is always
    // no, which is the right answer to a different question (a smith must not
    // become a fence for other people's goods). Here the item is already in
    // the character's own hands and has already been offered; what is left to
    // decide is whether the vendor channel for THIS item is established.
    // The ledger still gets the last word, for the same reason it does there:
    // a route is not legitimate if the character bought the thing from an NPC
    // to begin with.
    const std::vector<const faucet::GoldFaucet*> routes = faucet::ForItem(item);
    for (const faucet::GoldFaucet* f : routes) {
        if (!faucet::Allowed(f->policy)) continue;
        bool boughtItself = false;
        for (const GoldEntry& e : ledger.entries) {
            if (e.flow != GoldFlow::DestroyedVendorPurchase) continue;
            if (e.detail != item) continue;
            boughtItself = true;
            break;
        }
        if (boughtItself) {
            out.what = Disposal::Bank;
            out.reason = "an NPC sold this character the very same item -- "
                         "selling it back is a vendor loop, not income";
            return out;
        }
        out.what = Disposal::SellToNpc;
        out.via = f;
        out.reason = f->reason;
        return out;
    }

    out.what = Disposal::Bank;
    out.reason = routes.empty()
        ? "nobody wanted it and no faucet in the registry pays for it -- it "
          "goes in the box rather than being dumped for gold that would come "
          "from nowhere"
        : "nobody wanted it and every NPC route for it is unestablished "
          "(monster_loot_resale is UNKNOWN) -- banked, not dumped";
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
    // BOTH TRADES buy the steak and the cooked steak. I had cut the fisher
    // out of this on the strength of TNS's stock BUY rows, which is a fair
    // reading of a file and a poor one of a shard: the owner is the authority
    // on what Revolution did, and the tables have been corrected to match
    // (tm_vend.scp, VENDOR_B_FISHER and VENDOR_B_COOK, 2026-08-28).
    {"i_fish_cut_raw",       "fisher"},
    {"i_fish_cut_raw",       "cook"},
    {"i_fish_cut_cooked",    "fisher"},
    {"i_fish_cut_cooked",    "cook"},

    // Crafted goods, with the template that actually carries the BUY row.
    // Verified per item -- i_tunic_leather has NO buy row anywhere, so leather
    // armour genuinely has no NPC channel on this shard whatever the policy
    // permits, and NpcBuyersFor correctly returns nothing for it.
    // THE DAGGER, and the blacksmith is named FIRST because that is where the
    // smith already stands. The faucet registry has allowed
    // dagger_to_weaponsmith since the owner called it -- "he can make gold by
    // selling daggers he crafted" -- but this table had no row for it, so
    // NpcBuyersFor returned nothing and EARN_GOLD reported "16 x i_dagger
    // spare, and no buyer known" with fourteen freshly made daggers in the
    // pack. Permission without a buyer is a goal addressed to nobody.
    //
    // Both trades genuinely carry the row: VENDOR_B_WEAPONS_BLADED has
    // BUY=i_dagger,{10 15} (tm_vend.scp:1794), and this shard's blacksmith is
    // TNS's c_h_blacksmith, whose shop block takes BUY=VENDOR_B_WEAPONS_BLADED
    // wholesale -- which is why Olin and Curtis, standing at the Minoc forge,
    // buy what was just made on it. "yes but you can sell this to vendor it is
    // ok" (project owner, 2026-08-29).
    {"i_dagger",             "blacksmith"},
    {"i_dagger",             "weaponsmith"},
    {"i_spear_short",        "weaponsmith"},   // tm_vend.scp:1716 WEAPONS_BLUNT
    {"i_club",               "weaponsmith"},   // tm_vend.scp:1710 {10 15}
    {"i_robe",               "tailor"},        // tm_vend.scp:865  TAILOR
    // POISON. The faucet registry has allowed poison_potion_to_alchemist
    // since the owner called it -- "Voris it can make poison bottle and it can
    // sell to npc" -- and VENDOR_B_ALCHEMIST carries BUY=i_potion_poison,
    // {3 18} outright. But this table had no row, so NpcBuyersFor returned
    // nothing and EARN_GOLD reported "4 x i_potion_poison spare, and no buyer
    // known" with four potions in the pack and an empty purse. Permission
    // without a buyer is a goal addressed to nobody -- the same gap the dagger
    // had, and the third time this pair has been out of step.
    {"i_potion_poison",      "alchemist"},     // tm_vend.scp VENDOR_B_ALCHEMIST
    {"i_potion_heal",        "alchemist"},     // BUY=i_potion_heal,{3 18}
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
    // Only routes the REGISTRY allows. Returning a buyer the character would
    // then refuse on arrival is a walk across town to say no.
    if (!faucet::AllowedForItem(item)) return out;
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
    // The fisher buys raw fish AND fish steaks (owner, 2026-08-28).
    //
    // WHAT A FISH ACTUALLY PAYS, measured and then derived. A whole fish sold
    // for exactly 1 gold (run_m5/selln, 15 fish -> 15 gold), and the engine
    // says why: the payout is the itemdef VALUE adjusted by the vendor markup,
    // VALUE=2 for every i_fish_big_* (i_profession_cook_barkeep_baker.scp:707
    // onward) less VendorMarkup=15 (CServerConfig.cpp:158, unset in
    // sphere.ini) = 1. The {4 24} in a BUY row is NOT a price at all: it is
    // the RESTOCK QUANTITY (CItem.cpp:612-623 stores it via SetContainedLayer).
    //
    // So the steak is the fisher's real product, not the fish: i_fish_cut_raw
    // has VALUE=3 and weighs 0.1 against the whole fish's 5.0, which pays 2
    // gold at a fiftieth of the weight.
    //
    // Cooked fish pays nothing HERE. There is no BUY row for i_fish_cut_cooked
    // anywhere in runtime/scripts. The 5gp figure is real but it belongs to
    // TNS's own saturating buyer, which lives in the donor tree at
    // references/tns/scripts/Systems/System_SellerBuro.scp:420-445 and is not
    // installed on this shard.
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

// ---------------------------------------------------------------------------
// S4 -- gear.
//
// EVERY FIELD IS OFF THE SHARD'S OWN ITEMDEFS. The one that matters is SKILL=:
// the blacksmithing menu files these six and the dagger together under
// "Bladed" (def_blacksmithing.scp:170-194) and the categories are ART, not
// mechanics. i_dagger is TYPE=t_weapon_fence SKILL=Fencing, so a swordsman
// wants none of it, and the smith's whole output before this slice was a
// fencer's weapon and two materials.
//
// One-handed swords only, because those are the rows whose layer is certain:
// TWOHANDS=N and Client.cpp:51-52 name layer 1 the weapon hand. Two-handed
// swords, axes, maces, bows and every piece of armour are ABSENT rather than
// guessed -- kArmorPieces (Runner.cpp) already carries the armour numbers and
// lives in a translation unit this one cannot reach, and duplicating it here
// would create a second table to drift.
const GearItem kGear[] = {
    {"i_cutlass",      0x1440, 0x1441, 1, rules::kSwordsmanship, 25,
     "i_weapons.scp:1221-1240 TYPE=t_weapon_sword SKILL=Swordsmanship "
     "TWOHANDS=N ReqStr=25 DUPELIST=01441; def_blacksmithing.scp:173"},
    {"i_katana",       0x13FE, 0x13FF, 1, rules::kSwordsmanship, 25,
     "i_weapons.scp:966-985 SKILL=Swordsmanship ReqStr=25 DUPELIST=013ff; "
     "def_blacksmithing.scp:175"},
    {"i_scimitar",     0x13B5, 0x13B6, 1, rules::kSwordsmanship, 25,
     "i_weapons.scp:741-760 SKILL=Swordsmanship ReqStr=25 DUPELIST=013b6; "
     "def_blacksmithing.scp:178"},
    {"i_sword_viking", 0x13B9, 0x13BA, 1, rules::kSwordsmanship, 40,
     "i_weapons.scp:797-816 SKILL=Swordsmanship ReqStr=40 DUPELIST=013ba; "
     "def_blacksmithing.scp:179"},
    {"i_sword_broad",  0x0F5E, 0x0F5F, 1, rules::kSwordsmanship, 24,
     "i_weapons.scp:553-572 SKILL=Swordsmanship ReqStr=24 DUPELIST=0f5f; "
     "def_blacksmithing.scp:171"},
    {"i_sword_long",   0x0F60, 0x0F61, 1, rules::kSwordsmanship, 32,
     "i_weapons.scp:581-601 SKILL=Swordsmanship ReqStr=32 DUPELIST=0f61; "
     "def_blacksmithing.scp:177"},
    // LISTED SO IT CAN BE REFUSED. The smith already makes daggers and will
    // announce them; without this row the predicate would have no basis to
    // say WHY a swordsman does not want one.
    {"i_dagger",       0x0F51, 0x0F52, 1, rules::kFencing,       10,
     "i_weapons.scp:496-515 TYPE=t_weapon_fence SKILL=Fencing ReqStr=10 "
     "DUPELIST=0f52; def_blacksmithing.scp:174"},
};

const GearItem* FindGear(const char* item) {
    if (!item) return nullptr;
    for (const GearItem& g : kGear)
        if (std::strcmp(g.item, item) == 0) return &g;
    return nullptr;
}

const GearItem* FindGearByGraphic(u16 graphic) {
    if (!graphic) return nullptr;
    for (const GearItem& g : kGear)
        if (g.graphic == graphic || g.flip == graphic) return &g;
    return nullptr;
}

namespace {

// What is on `layer`, or 0. An unobserved paperdoll is an empty one -- see the
// note on WornItem: the caller supplies the truth it has.
u16 WornOnLayer(const std::vector<WornItem>& worn, u8 layer) {
    for (const WornItem& w : worn)
        if (w.layer == layer) return w.graphic;
    return 0;
}

// Does this build actually TRAIN the weapon's skill? `targets` is the finished
// build the character is working towards, so this is the same question a
// player answers by looking at their own skill plan.
bool TrainsSkill(const prof::Profession& p, int skillId) {
    if (skillId < 0) return false;
    for (const prof::SkillTargetSpec& t : p.targets)
        if (t.skillId == skillId && t.tenths > 0) return true;
    return false;
}

}  // namespace

BuyDecision WantsGear(const prof::Profession& p,
                      const std::vector<Stock>& pack,
                      i32 gold,
                      const TradePolicy& policy,
                      const TradeIntent& offer,
                      const std::vector<WornItem>& worn) {
    BuyDecision d;
    d.reason = "not a piece of equipment this fleet trades";
    if (!offer.Valid()) {
        d.reason = "not a well-formed offer";
        return d;
    }

    const GearItem* g = FindGear(offer.item.c_str());
    if (!g) return d;

    // --- is this MY class of thing? -----------------------------------------
    //
    // For a WEAPON the answer is the SKILL, not `wears`. `wears` is the armour
    // grade, and the shard's own evidence for it is about armour: the mining
    // guide's "bu setleri giyen karakterler buyu atamazlar" is said of ore
    // metal SETS. Whether a metal WEAPON hinders a caster on this shard is
    // UNKNOWN -- no script line and no forum entry says -- so it is not
    // asserted here in either direction. What IS certain is that a weapon
    // whose skill the build never trains is a weapon this life will not use,
    // and that alone refuses the mage the swordsman's cutlass.
    if (g->weaponSkill >= 0 && !TrainsSkill(p, g->weaponSkill)) {
        d.reason = "this life does not train the skill that weapon uses";
        return d;
    }

    // --- am I already carrying that kind of thing? --------------------------
    //
    // DecideAcquire compares GRAPHIC against graphic, which is right for the
    // question it was written for -- "is the piece for this slot on the
    // paperdoll" -- and wrong for a trade: a swordsman holding a katana does
    // not need a cutlass, and the two graphics differ. So the CLASS check
    // comes first, and only then the piece-level arithmetic.
    const u16 wornGfx = WornOnLayer(worn, g->layer);
    if (const GearItem* on = FindGearByGraphic(wornGfx)) {
        if (on->weaponSkill == g->weaponSkill) {
            d.reason = "already carrying one of those";
            return d;
        }
    }
    const i32 held = QtyOf(pack, offer.item);

    // --- the arithmetic, which is DecideAcquire's ---------------------------
    life::AcquireRequest req;
    req.graphic = g->graphic;
    req.item = g->item;
    req.desiredTotal = 1;          // the second shield was never the point
    req.layer = g->layer;
    req.mustWear = true;
    req.wearable = true;           // decided above, and handed in, not re-derived
    req.minimumGoldReserve = p.goldReserve;
    const life::AcquirePlan plan = life::DecideAcquire(req, held, wornGfx);
    if (plan.step != life::AcquireStep::Buy) {
        d.reason = plan.reason;
        return d;
    }

    // --- can I pay for it without eating the reserve? -----------------------
    //
    // ONE. Equipment is not stock, and a life that buys five swords because
    // five were offered is the heater-shield bug wearing a different hat.
    const i32 qty = 1;
    const i32 cost = qty * offer.pricePerUnit;
    if (cost > gold) {
        d.reason = "cannot afford it";
        return d;
    }
    if (gold - cost < p.goldReserve) {
        d.reason = "would eat into the reserve this life keeps for tools";
        return d;
    }
    if (offer.pricePerUnit > policy.blindPriceCeiling) {
        d.reason = "more than this life will pay sight unseen";
        return d;
    }

    d.accept = true;
    d.qty = qty;
    d.reason = "would wear it";
    return d;
}

BuyDecision ConsiderOffer(const prof::Profession& p,
                          const std::vector<Stock>& pack,
                          i32 gold,
                          const TradePolicy& policy,
                          const TradeIntent& offer,
                          const std::vector<WornItem>& worn) {
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
        // MATERIALS ARE NOT THE ONLY REASON TO BUY. Shortfall reads `consumes`
        // and `consumables`; a sword is in neither, and saying "no use for it"
        // to a swordsman being offered a sword was the whole of S4's defect.
        const BuyDecision gear = WantsGear(p, pack, gold, policy, offer, worn);
        if (gear.accept) return gear;
        // The gear half's refusal is the more specific one whenever it
        // recognised the item at all; otherwise the material answer stands.
        d.reason = FindGear(offer.item.c_str()) ? gear.reason
                                                : "this life has no use for it";
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
        if (IsGoldSource(e.flow) || e.flow == GoldFlow::TransferPlayerTrade) n += e.amount;
    }
    return n;
}

i32 Ledger::TotalOut() const {
    i32 n = 0;
    for (const GoldEntry& e : entries) {
        if (!IsGoldSource(e.flow) && e.flow != GoldFlow::TransferPlayerTrade) n += e.amount;
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
