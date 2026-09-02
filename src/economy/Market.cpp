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

// A CONSUMABLE IS COUNTED BY WHAT IT LOOKS LIKE ON THE WIRE, NOT BY ITS
// CATALOGUE LABEL. obs.pack (the `Stock` list every caller passes in) is
// keyed by itemdef DEFNAME, resolved from the wire graphic through
// econ::ItemNameForGraphic[AndHue] -- the same path VendorErrand's
// BackpackItemCount reads. ConsumableNeed::name is a human label ("bandage",
// "heal potion", "food": professions.h), chosen for prose and for
// FormatSellOffer-style output, and it is NOT a defname -- there is no
// itemdef called "heal potion". `QtyOf(pack, c.name)` therefore compared the
// pack against a string that can never appear in it, `have` was permanently
// 0, and every consumable was a permanent want regardless of what the
// character carried (flagged 2026-08-30).
//
// The fix walks c.graphics -- the same field the errand already carries a
// character's kit off of -- through econ::ItemNameForGraphic to reach the
// defname(s) the pack can actually match, and sums QtyOf over the distinct
// results (Food()'s two graphics resolve to two different defnames,
// i_bread_loaf and i_food_bread_fr, so both must count; Bandages()' one
// graphic resolves to one). A graphic with no row in kGraphics contributes
// nothing rather than crashing or guessing -- the same fail-safe every other
// caller of ItemNameForGraphic already relies on.
i32 QtyOfConsumable(const std::vector<Stock>& pack, const prof::ConsumableNeed& c) {
    i32 total = 0;
    std::vector<const char*> counted;   // defnames already summed, once each
    for (u16 g : c.graphics) {
        const char* def = uo::econ::ItemNameForGraphic(g);
        if (!def) continue;
        bool already = false;
        for (const char* d : counted) {
            if (std::strcmp(d, def) == 0) { already = true; break; }
        }
        if (already) continue;
        counted.push_back(def);
        total += QtyOf(pack, def);
    }
    return total;
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
        const i32 have = QtyOfConsumable(pack, c);
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

const char* SupplyRouteName(SupplyRoute r) {
    switch (r) {
        case SupplyRoute::NpcVendor:     return "NPC_VENDOR";
        case SupplyRoute::SelfProduce:   return "SELF_PRODUCE";
        case SupplyRoute::PlayerMarket:  return "PLAYER_MARKET";
        case SupplyRoute::NoKnownSource: return "NO_KNOWN_SOURCE";
    }
    return "?";
}

SupplyRoute RouteForInput(const prof::Profession& me, const char* item,
                          bool npcTradeKnown) {
    // THE VENDOR TABLE KEEPS PRECEDENCE. A scribe's blank scrolls and a
    // mage's reagents are bought, and they are bought even by the lumberjack
    // line that can technically make parchment -- changing that here would
    // rewrite a working errand to fix a broken one.
    if (npcTradeKnown) return SupplyRoute::NpcVendor;
    if (!item || !*item) return SupplyRoute::NoKnownSource;
    for (const std::string& made : me.produces) {
        if (made == item) return SupplyRoute::SelfProduce;
    }
    if (!WhoProduces(item).empty()) return SupplyRoute::PlayerMarket;
    return SupplyRoute::NoKnownSource;
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

std::vector<const prof::Profession*> WhoConsumes(const char* item) {
    std::vector<const prof::Profession*> out;
    if (!item) return out;
    for (const prof::Profession& p : prof::All()) {
        for (const std::string& eaten : p.consumes) {
            if (eaten == item) { out.push_back(&p); break; }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The material surplus cap. See the block comment on MaterialSurplusCap in
// include/uo/market.h for the formula and the evidence behind each term.
// ---------------------------------------------------------------------------
namespace {

// The owner's 500-600 ingots, expressed as what it actually is: a rate. A full
// 0->100.0 Blacksmithing climb is 100 skill POINTS and costs ~550 units of
// metal, so 5.5 units per point. Scaling by the REMAINING climb is what makes
// the number differ between two smiths, which is the whole point of the rule --
// a flat 550 would be exactly the global constant the ruling forbids.
constexpr double kUnitsPerSkillPoint = 5.5;

// The largest quantity of `item` any ONE recipe this profession makes consumes,
// and the skill that recipe is gated on. Zero/-1 when its bench never eats it.
void OwnRecipeDemand(const prof::Profession& p, const std::string& item,
                     i32* perCraftOut, int* skillOut) {
    i32 per = 0;
    int skill = -1;
    for (const std::string& made : p.produces) {
        if (made == item) continue;   // making it is not eating it
        const uo::prod::Recipe* r = uo::prod::FindRecipe(made.c_str());
        if (!r) continue;
        for (const uo::prod::Ingredient& in : r->inputs) {
            if (!in.item || item != in.item) continue;
            if (in.qty > per) { per = in.qty; skill = r->skillId; }
        }
    }
    if (perCraftOut) *perCraftOut = per;
    if (skillOut) *skillOut = skill;
}

}  // namespace

MaterialCap MaterialSurplusCap(const prof::Profession& p, const char* item,
                               i32 craftBatch, i32 gold,
                               const std::vector<SkillGap>& gaps,
                               const TradePolicy& policy) {
    MaterialCap out;
    if (!item || !*item) return out;
    out.isMaterial = uo::econ::IsFloorMaterial(item);
    if (!out.isMaterial) return out;

    const std::string name = item;

    // --- what the next sitting at the bench eats ---------------------------
    i32 perCraft = 0;
    int craftSkill = -1;
    OwnRecipeDemand(p, name, &perCraft, &craftSkill);
    out.ownPlan = perCraft * std::max(1, craftBatch);

    // --- what the build plan still has to climb on that bench --------------
    //
    // Only for a skill this character's OWN recipes for this material are gated
    // on: a lumberjack's Swordsmanship climb is no reason to hoard logs.
    if (craftSkill >= 0) {
        for (const SkillGap& g : gaps) {
            if (g.skillId != craftSkill || g.tenthsRemaining <= 0) continue;
            out.training = static_cast<i32>(
                (g.tenthsRemaining / 10.0) * kUnitsPerSkillPoint + 0.5);
            break;
        }
    }

    // --- what the player market will come asking for -----------------------
    const i32 consumers = static_cast<i32>(WhoConsumes(name.c_str()).size());
    out.market = consumers * std::max(0, policy.restockConsumablesTo);
    // The purse bends the market reserve, and only the market reserve: holding
    // stock for a buyer who has not turned up yet is the first thing a broke
    // character gives up, while its own plan and its own training are the last.
    if (gold < p.goldReserve) out.market /= 2;

    out.units = out.ownPlan + out.training + out.market;
    return out;
}

MaterialSaleGate MaterialNpcSaleGate(const prof::Profession& p, const char* item,
                                     i32 heldPackAndBank, bool playersDeclined,
                                     i32 craftBatch, i32 gold,
                                     const std::vector<SkillGap>& gaps,
                                     const TradePolicy& policy) {
    MaterialSaleGate g;
    g.held = heldPackAndBank;
    g.detail = MaterialSurplusCap(p, item, craftBatch, gold, gaps, policy);
    g.cap = g.detail.units;

    // Not a material at all. The ruling is about logs, ore, ingots, cloth and
    // hides; a finished good is what a crafter is SUPPOSED to take to a
    // counter, and this gate has nothing to say about it.
    if (!g.detail.isMaterial) {
        g.allowed = true;
        g.reason = "not a material -- the surplus cap does not apply";
        return g;
    }
    // A DOCUMENTED FAUCET IS NOT THE FLOOR, and this distinction is the whole
    // reason the gate consults the registry rather than the item class alone.
    //
    // FISH is graded WorldGathered (VendorPolicy.cpp:122-134) and is therefore
    // a "material" by class -- and it is also one of the three taps Revolution's
    // own players describe: "caught fish cook fish then sell". Capping it would
    // have taken away a fisher's entire income to enforce a ruling that was
    // never about fish; the owner's list is "logs, boards, ingots, ore, cloth,
    // hides, yarn". The registry already draws exactly that line, so ask it
    // instead of maintaining a second list that can drift from it.
    //
    // MaySellToNpc reaches the same routes; this repeats the question because
    // the two functions are asked independently and a gate that silently
    // assumed its caller had already checked would be a trap.
    for (const faucet::GoldFaucet* f : faucet::ForItem(item)) {
        if (!faucet::Allowed(f->policy)) continue;
        g.allowed = true;
        g.reason = "a documented gold faucet, not the material price floor";
        return g;
    }
    // Half one of the ruling: player-first. Unchanged, and asked first because
    // it is the half that is about other people rather than about this
    // character's own cupboard.
    if (!playersDeclined) {
        g.reason = "the player-first window is still open for this material";
        return g;
    }
    // Half two: is there genuinely more of it than this life's own plan wants?
    if (heldPackAndBank <= g.cap) {
        g.reason = "held stock is at or under this character's own plan cap -- "
                   "it banks and waits for a crafter";
        return g;
    }
    g.allowed = true;
    g.reason = "held stock exceeds this character's own plan cap and no player "
               "answered";
    return g;
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
                        const Ledger& ledger, bool playersDeclined) {
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
    // THE NPC PRICE FLOOR, before the refusal is written (owner ruling,
    // 2026-09-02). A material whose player-first window has closed may take
    // the counter -- but only if a live BUY row exists for it, because the
    // alternative is a walk across town to be refused and a goal that spins.
    // The arbitrage test below still applies: the floor changes WHO may buy,
    // never whether a vendor->craft->vendor loop is allowed to print gold.
    const bool viaFloor =
        !allowedRoute && faucet::NpcFloorOpenFor(item, playersDeclined) &&
        HasNpcBuyer(item, /*playersDeclined=*/true);

    if (!allowedRoute && !viaFloor) {
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
    out.reason = viaFloor
                     ? "no player answered the offer, so the NPC price floor "
                       "applies to this material (owner ruling, 2026-09-02) -- "
                       "a fallback, not the market"
                     : "own output, from inputs the world provided";
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

    // --- MATERIALS: the NPC price floor (owner ruling, 2026-09-02) ----------
    //
    // Reachable only once econ::MaterialFloorOpen says so -- switch on AND the
    // player-first WTS window closed for this item. See NpcBuyersFor below.
    //
    // EVERY ROW HERE WAS RE-DERIVED FROM THE INSTALLED RUNTIME.
    //
    // HISTORY, so the same wrong turn is not taken a third time. An earlier
    // pass read the 23 material BUY rows in the runtime's tm_vend.scp as
    // commented out and concluded this shard buys no logs, boards, ingots or
    // hides at all. That commenting was a TNS donor artefact, already REJECTED
    // in docs/TNS_WORLD_ECONOMY_DONOR_AUDIT.md section 3.5; the shard owner
    // restored the rows on 2026-09-02 and they are now byte-identical to
    // server/Scripts-X/templates/tm_vend.scp. Reading a disabled donor row as
    // shard truth cost this table nine correct entries.
    //
    // Line numbers are runtime/scripts/templates/tm_vend.scp, verified
    // 2026-09-02 and LIVE (no leading //). Payout is the ITEMDEF's VALUE
    // reduced by VENDORMARKUP, which is a PERCENTAGE, not a flat subtraction
    // (Source-X chars/CCharNPCStatus.cpp:332-345 -- "+100% = double price").
    // `{a b}` is the restock quantity, never a price.
    //
    // Each trade below was resolved template -> chardef -> world save:
    //   template            chardefs (c_vendor_human.scp)     spawned
    //   VENDOR_B_BLACKSMITH   c_blacksmith :1250 / _f :1350     18 + 5
    //   VENDOR_B_PROVISIONER  c_provis     :4287 / _f :4360     10 + 10
    //   VENDOR_B_CARPENTER    c_carpenter  :1798 / _f :1867      3 + 6
    //   VENDOR_B_BOWYER       c_bowyer     :1499 / _f :1572      1 + 6
    //   VENDOR_B_TINKER       c_tinker     :5466 / _f :5536      1 + 6
    //   VENDOR_B_JEWELER      c_jeweler    :3471 / _f :3545      1 + 7
    //   VENDOR_B_COBBLER      c_cobbler    :2054 / _f :2128     12 + 6
    //   VENDOR_B_TANNER       c_tanner     :5170 / _f :5244      6 + 3
    //   VENDOR_B_FURTRADER    c_furtrader  :2636 / _f :2710      6 + 2
    // (counts: tools/world_query.py --count, runtime/save, 2026-09-02.)
    // The elf and gargoyle chardefs use the same templates and spawn ZERO, so
    // they are not routes.
    //
    // NOT LISTED, deliberately: VENDOR_B_WEAPONS_BLADED (:1858/:1859) and
    // VENDOR_B_WEAPONS_BLUNT (:1894/:1895) carry live i_log and i_ingot_iron
    // rows too, but ServiceForTrade maps both "weaponsmith" and "blacksmith"
    // to wm::Service::Blacksmith, so a weaponsmith row would be the same
    // errand twice. Likewise "furtrader" and "tanner" both resolve to
    // wm::Service::Tanner (ClientTravel.cpp:1565/1566), so only "tanner" is
    // listed for hides.
    //
    // Order within an item is most-spawned-first: the shortest errand.
    {"i_feather",       "bowyer"},       // :1630 VENDOR_B_BOWYER, VALUE=2 -> 1
    // The provisioner takes BUY=VENDOR_B_BOWYER wholesale (:1465), so it buys
    // feathers too. Second because a bowyer is the shorter errand for a
    // fletcher and there are ten times as many provisioners as bowyers.
    {"i_feather",       "provisioner"},
    {"i_cotton",        "weaver"},       // :896  VENDOR_B_WEAVER, VALUE=3 -> 2
    {"i_cotton",        "tailor"},       // :1004 VENDOR_B_TAILOR
    {"i_thread",        "tailor"},       // :1006 VALUE=2 -> 1
    {"i_flax_bundle",   "tailor"},       // :1005 VALUE=2 -> 1
    // THE THREE METALS THAT KEPT THEIR OWN ITEMDEF. Copper, gold and silver
    // ingots are separate ITEMDEFs (01be3/01be9/01bf5) with their own BUY
    // rows; the other thirteen metals are ID=i_ingot_iron + COLOR and share
    // iron's commented-out row, so they have no buyer.
    //
    // PAYOUT IS UNVERIFIED HERE and must be read off the 0x9E window rather
    // than predicted: none of the three ITEMDEFs carries a VALUE= line, so
    // Sphere COMPUTES one from RESOURCES plus SKILLMAKE
    // (server/Source-X/src/game/items/CItemBase.cpp:1026 GetMakeValue ->
    // CalculateMakeValue). That is a nonzero number, not zero, but it is not
    // one this project may state until a purse has moved for it.
    {"i_ingot_copper",  "jeweler"},      // :1507
    {"i_ingot_gold",    "provisioner"},  // :1422
    {"i_ingot_gold",    "jeweler"},      // :1508
    {"i_ingot_silver",  "provisioner"},  // :1423
    {"i_ingot_silver",  "jeweler"},      // :1509

    // THE RESTORED ROWS (owner restore, 2026-09-02). These are the three
    // families bots actually stockpile, and until the restore this table
    // claimed nobody bought them.
    //
    // IRON INGOTS -- ID-only, no hue. VALUE is not on the ITEMDEF
    // (items/i_provisions_ore.scp:220 [ITEMDEF 01bef] has RESOURCES and
    // SKILLMAKE but no VALUE=), so Sphere computes the payout; read it off the
    // 0x9E window, never predict it.
    {"i_ingot_iron",    "blacksmith"},   // :2115 {44 88}
    {"i_ingot_iron",    "provisioner"},  // :1421 {5 38}
    {"i_ingot_iron",    "jeweler"},      // :1506 {3 13}
    {"i_ingot_iron",    "tinker"},       // :1084 {4 34}
    //
    // COLOURED INGOTS ARE **NOT** COVERED BY THE ROW ABOVE. Settled from
    // Source-X source, not assumed:
    //   CChar::NPC_FindVendableItem (CCharNPCStatus.cpp:611) looks the player's
    //   item up with ContentFind(CResourceID(RES_ITEMDEF, pVendItem->GetID())).
    //   CItem::IsResourceMatch (items/CItem.cpp:6041) compares that rid against
    //   the buy-container item's OWN GetResourceID(); the RES_ITEMDEF fallback
    //   below it (:6046-6071) special-cases only log<-board and hide<-leather.
    //   CItemBase::GetID (items/CItemBase.h:309) returns the itemdef's resource
    //   index, while a script `ID=` line sets only m_dwDispIndex
    //   (CItemBase.cpp:1659-1694, IBC_ID -> CopyBasic + m_dwDispIndex).
    // So [ITEMDEF i_ingot_shadow] ID=i_ingot_iron (i_provisions_ore.scp:356)
    // is a DISTINCT resource that merely borrows iron's artwork, and a
    // BUY=i_ingot_iron row will not match it. Hue is never consulted either
    // way. The thirteen coloured hues therefore still bank.
    //
    // LOGS AND BOARDS. Note i_log is t_log and i_board t_board
    // (i_provisions_logs.scp:62, :27) and NPC_FindVendableItem rejects a type
    // mismatch (CCharNPCStatus.cpp:618), so the log<-board leniency in
    // IsResourceMatch never fires here -- each needs its own row, and each has
    // one.
    {"i_log",           "blacksmith"},   // :2114 {4 18}
    {"i_log",           "provisioner"},  // :1438 {5 38}
    // WATCH THE LOG PAYOUT. i_log is VALUE=1 (i_provisions_logs.scp:63) and
    // i_board VALUE=2 (:28), so after markup a log may round to ZERO gold.
    // That would make a 200-log errand worth nothing while still consuming the
    // stock. UNKNOWN until a purse has moved; measure it before any life is
    // allowed to plan around log income.
    {"i_log",           "carpenter"},    // :293  {5 15}
    {"i_log",           "bowyer"},       // :1632 {24 72}
    {"i_log",           "tinker"},       // :1085 {4 34}
    {"i_board",         "provisioner"},  // :1439 {5 38}
    {"i_board",         "carpenter"},    // :294  {5 15}
    {"i_board",         "tinker"},       // :1086 {4 34}
    //
    // HIDES AND CUT LEATHER. i_hide is t_hide, i_hides_cut t_leather
    // (i_profession_tailor_tanner.scp:350, :298), both VALUE=5.
    //
    // NO i_hides_cut_2 ROW, though tm_vend.scp:343 and :481 carry one. That
    // ITEMDEF is DUPEITEM=01067 (i_profession_tailor_tanner.scp:398-399), and
    // CItemBase::FindItemBase resolves a dupe id straight to its master
    // (Source-X items/CItemBase.cpp:2254-2256, CItemBaseDupe::GetItemDef), so
    // such an item's GetID() is 01067 -- i_hides_cut -- and the two rows below
    // already cover it. The script rows are belt-and-braces; a bot-side row
    // would be dead, since the graphic table (VendorPolicy.cpp:332) only ever
    // names 0x1067.
    {"i_hide",          "cobbler"},      // :344  {2 6}
    {"i_hide",          "tanner"},       // :482  {5 55}; furtrader :406 same svc
    {"i_hides_cut",     "cobbler"},      // :342  {2 6}
    {"i_hides_cut",     "tanner"},       // :480  {5 55}
};

}  // namespace

std::vector<const NpcBuyer*> NpcBuyersFor(const char* item,
                                          bool playersDeclined) {
    std::vector<const NpcBuyer*> out;
    if (!item) return out;
    // Only buyers we may LEGITIMATELY use. The table below records what the
    // stock scripts allow; the policy decides what Revolution permits, and it
    // is the policy that answers. Returning a buyer the character would then
    // refuse on arrival is a 224-tile walk to say no.
    // Only routes the REGISTRY allows. Returning a buyer the character would
    // then refuse on arrival is a walk across town to say no.
    //
    // ...OR the NPC price floor is open for it: a MATERIAL, the switch on, and
    // the player-first window already closed. The floor never invents a buyer
    // -- the loop below still has to find a row backed by a live BUY line --
    // so an item with no row banks exactly as the ruling says it should.
    if (!faucet::AllowedForItem(item) &&
        !faucet::NpcFloorOpenFor(item, playersDeclined)) {
        return out;
    }
    for (const NpcBuyer& b : kNpcBuyers) {
        if (std::strcmp(b.item, item) == 0) out.push_back(&b);
    }
    return out;
}

bool HasNpcBuyer(const char* item, bool playersDeclined) {
    return !NpcBuyersFor(item, playersDeclined).empty();
}

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

namespace {

// ---------------------------------------------------------------------------
// Forum price seeds (flagged 2026-08-30: a fleet with zero observations
// opened every sale at TradePolicy::openingAsk == 2gp -- Tarath sold logs at
// 2 while the forum's own players priced them at 17. openingAsk exists so a
// FIRST sale can happen at all when nobody has ever seen a price; it was
// never meant to be the number a character keeps asking once real evidence
// exists. It just never had anywhere else to look.)
//
// docs/FORUM_SWEEP_2026_08_30.md is that evidence: verbatim player prices,
// dated and sourced to a forum thread. A seed is what BelievedSalePrice()
// falls back to when a character has made zero observations of its own --
// still ranked BELOW every real observation (NPC buy price, player quote,
// completed trade), because a seed is what a Revolution player would have
// walked in already knowing, not something this character personally
// witnessed. See PriceBook::BelievedSalePrice below for the ranking.
//
// ONLY rows that (a) name an actual number -- several forum rows are a bare
// "WTB, pm for price" and are worthless as a seed -- and (b) map onto a
// defname this codebase already tracks somewhere (VendorPolicy.cpp's
// kGraphics, Market.cpp's own tables, or a profession's produces/consumes)
// without guessing which item the poster meant.
//
// EXCLUDED, deliberately:
//   * Katana +12/+9, Katana "+15", Cutlass +15/+12 -- every weapon price in
//     the sweep is for an ENHANCED (+N) or magic copy. The number prices the
//     enchantment, not the base i_katana/i_cutlass this client tracks, and
//     seeding the plain item at an enhanced item's price would be a made-up
//     mapping, which is exactly what this table exists to refuse to do.
//   * Every mount (Horse, Llama, the Ostard family, Steed, Mare, Unicorn,
//     Kirin, Fresian, Shire...) -- no mount has a defname anywhere in this
//     client (grepped: no i_horse/i_llama/i_ostard/i_steed/i_mare/i_unicorn
//     token exists in src/ or include/), so there is nothing to seed.
//   * Shell -- same: no defname for it exists in this codebase.
//   * Bow prices (Ulrika/Quakin/Ekroan) and the armour sets (Blackrock/
//     Bloodrock/Mytheril/Valorite) -- named by a shop tier/quality, not a
//     plain itemdef; no clean single-item defname to seed.
struct ForumPriceSeed { const char* item; i32 price; };
const ForumPriceSeed kForumPriceSeeds[] = {
    // Log, 17 gp/unit ("vergili" -- incl. shard sales tax), stock "49,000
    // satışta, 97,000 depoda" (49k listed, 97k in storage). Poster dated
    // 05 Şub 2016, updated 03 Mar 2016.
    // FORUM_SWEEP_2026_08_30.md #2 row "Log", topic,93370.0.html
    {"i_log", 17},
    // Iron ingot, 35 gp/unit ("vergili genel vendor fiyatlarıdır" -- incl.
    // tax, general-vendor-equivalent price), stock 14,000. Same poster, same
    // thread, 05 Şub 2016 (updated 03 Mar 2016).
    // FORUM_SWEEP_2026_08_30.md #2 row "Iron ingot", topic,93370.0.html
    {"i_ingot_iron", 35},
    // Cloth/bolt ("kumaş", "rulo halinde" -- sold by the bolt/roll), 17
    // gp/unit, bulk discount for large buys. 29 Şub 2016. i_cloth_bolt (not
    // i_cloth) is the bolt-form defname (VendorPolicy.cpp kGraphics
    // 0x0F95-0x0F97).
    // FORUM_SWEEP_2026_08_30.md #2 row "Cloth/bolt (kumaş)", topic,94084.0.html
    {"i_cloth_bolt", 17},
    // Resurrection scroll, 160 gp each, bulk discount available. 12 Tem 2011.
    // FORUM_SWEEP_2026_08_30.md #2 row "Resurrection scroll (res scroll)",
    // topic,86762.0.html
    {"i_scroll_resurrection", 160},
    // Runebook, 5,000 gp. 29 Mar 2011. i_spellbook_runebook is this client's
    // runebook defname (VendorPolicy.cpp:61, PlayerMarketGood).
    // FORUM_SWEEP_2026_08_30.md #2 row "Runebook", topic,86712.0.html
    {"i_spellbook_runebook", 5000},
    // Full spellbook ("Full Speel Book"), 40,000 gp. Same post, 29 Mar 2011.
    // FORUM_SWEEP_2026_08_30.md #2 row "Full spellbook (Full Speel Book)",
    // topic,86712.0.html
    {"i_spellbook", 40000},
    // Potions, Greater Heal/Cure/Agility/Strength/Total Refresh -- one price
    // for the whole named list -- 15,000 gp per 100 = 150 gp each. Greater
    // Heal is in that list and i_potion_heal is the only heal-potion defname
    // this client tracks (VendorPolicy.cpp:442, graphic 0x0F0C -- every heal
    // tier shares that graphic, so there is only one defname to seed).
    // 06 Şub 2016.
    // FORUM_SWEEP_2026_08_30.md #2 row "Potions, Greater
    // Heal/Cure/Agility/Strength/Total Refresh", topic,93388.0.html
    {"i_potion_heal", 150},
    // Deadly Poison potion, 20,000 gp per 100 = 200 gp each. Same post,
    // 06 Şub 2016. i_potion_poisondeadly is the defname the fencer/PK
    // catalogue already consumes (professions.h); it has no graphic row
    // (VendorPolicy.cpp -- the four poison tiers share ID=i_bottle_green and
    // the wire cannot tell them apart), but that only blocks COUNTING it in
    // a pack, not naming a believed price for it.
    // FORUM_SWEEP_2026_08_30.md #2 row "Deadly Poison potion",
    // topic,93388.0.html
    {"i_potion_poisondeadly", 200},
};

i32 ForumSeedSalePrice(const char* item) {
    if (!item) return -1;
    for (const ForumPriceSeed& s : kForumPriceSeeds) {
        if (std::strcmp(s.item, item) == 0) return s.price;
    }
    return -1;
}

}  // namespace

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
    // No observation of our own. A forum seed (kForumPriceSeeds above) ranks
    // BELOW every real observation -- it is what a Revolution player would
    // already have known walking in, not proof this character has personally
    // seen -- but it still beats inventing nothing at all, which is what sent
    // every unobserved sale out at TradePolicy::openingAsk == 2gp regardless
    // of what the thing was actually worth (flagged 2026-08-30, i_log sold at
    // 2 against a forum price of 17).
    const i32 seed = ForumSeedSalePrice(item);
    if (seed >= 0) return seed;
    return -1;   // never seen one and no seed either. Not zero, not a guess.
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

std::string FormatBuyReply(const std::string& item, const std::string& toWhom) {
    if (toWhom.empty()) return "WTB " + item;
    return toWhom + ", WTB " + item;
}

std::string SpeechAddressee(const std::string& said) {
    const usize comma = said.find(',');
    if (comma == std::string::npos || comma == 0) return std::string();
    std::string who = said.substr(0, comma);
    // A NAME, not a clause. UO characters are one word here, and treating
    // "well, WTB i_log" as an address to a player called "well" would silence
    // every seller in the room.
    for (char c : who) {
        if (c == ' ' || c == '\t') return std::string();
    }
    return who;
}

bool AddressedTo(const std::string& said, const std::string& me) {
    const std::string who = SpeechAddressee(said);
    if (who.empty()) return true;          // said to the room
    return Lower(who) == Lower(me);
}

std::string FormatDecline(const std::string& toWhom) {
    if (toWhom.empty()) return "sorry -- sorted";
    return toWhom + ", sorry -- sorted";
}

bool ParseDecline(const std::string& said, std::string* whoOut) {
    const std::string low = Lower(said);
    if (low.find("sorry") == std::string::npos) return false;
    if (low.find("sorted") == std::string::npos) return false;
    if (whoOut) *whoOut = SpeechAddressee(said);
    return true;
}

std::string FormatBuyWant(const TradeIntent& t) {
    return "WTB " + std::to_string(t.qty) + " " + t.item + " " +
           std::to_string(t.pricePerUnit) + "gp";
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

bool ParseBuyWant(const std::string& said, TradeIntent* out) {
    if (!out) return false;
    const std::string low = Lower(said);
    const usize at = low.find("wtb ");
    if (at == std::string::npos) return false;

    usize i = at + 4;
    // THE QUANTITY IS OPTIONAL, and that is the whole compatibility story: the
    // reply form ("WTB i_log") has never carried one and must keep parsing.
    // ReadInt leaves `i` where it found nothing, so a non-numeric first word is
    // simply read as the item below.
    i32 qty = ReadInt(low, i);
    const std::string item = ReadWord(low, i);
    // "WTB 20" with no item is not addressed to anybody: a quantity alone names
    // nothing this fleet can hand over.
    if (item.empty()) return false;
    if (qty < 0) qty = 0;   // the bare reply form carried no quantity

    SkipSpace(low, i);
    const i32 price = ReadInt(low, i);

    out->item = item;
    out->qty = qty;
    out->pricePerUnit = price > 0 ? price : 0;
    return true;
}

bool ParseBuyReply(const std::string& said, std::string* itemOut) {
    if (!itemOut) return false;
    // ONE PARSER, not two that can drift. A buyer that learned to say
    // "WTB 20 i_log 4gp" would otherwise stop being recognised as answering a
    // seller's offer, because this used to read "20" as the item name.
    TradeIntent t;
    if (!ParseBuyWant(said, &t)) return false;
    if (t.item.empty()) return false;
    *itemOut = t.item;
    return true;
}

BuyLineKind ClassifyBuyLine(const std::string& said, TradeIntent* out) {
    TradeIntent t;
    if (!ParseBuyWant(said, &t)) return BuyLineKind::NotABuyLine;
    if (out) *out = t;
    // Addressed to one player by name: only the REPLY form is ever addressed
    // (FormatBuyReply), and a line naming somebody is that somebody's business
    // whatever else it carries.
    if (!SpeechAddressee(said).empty()) return BuyLineKind::Reply;
    // Said to the room WITH a quantity and a price: that is demand announcing
    // itself, not an answer to anybody's standing offer.
    return (t.qty > 0 && t.pricePerUnit > 0) ? BuyLineKind::Announce
                                             : BuyLineKind::Reply;
}

FundingDecision FundOpenWindow(const TradeIntent& planned, i32 goldOnHand,
                               i32 goldReserve) {
    FundingDecision d;
    if (planned.item.empty() || planned.qty <= 0) {
        d.reason = "no want was announced, so there is nothing to fund";
        return d;
    }
    // A ceiling of zero is the bare reply form -- "some, at whatever you ask".
    // That is a fine thing to SAY and an impossible thing to fund: there is no
    // number to multiply. Refusing here is what lets the window's own give-up
    // timer end the deal instead of this side paying an amount it never named.
    if (planned.pricePerUnit <= 0) {
        d.reason = "no price was named for it";
        return d;
    }
    const i32 spendable = goldOnHand - goldReserve;
    if (spendable < planned.pricePerUnit) {
        d.reason = "the purse cannot cover one unit at the announced price";
        return d;
    }
    // PAY FOR WHAT THE PURSE COVERS, not for what was asked for. The
    // announcement was already bounded by the purse when it was made, but the
    // purse can have shrunk since (a vendor errand, a death) and a window
    // funded past it is a promise the drag refuses.
    d.qty = std::min(planned.qty, spendable / planned.pricePerUnit);
    d.gold = d.qty * planned.pricePerUnit;
    d.accept = true;
    d.reason = "funding it from the want this life announced out loud";
    return d;
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

// The most this life will pay per unit for `item`, sight unseen. EXACTLY the
// ceiling ConsiderOffer applies, factored out so the two can never drift: a
// shouted ceiling the buyer then refuses at the window is worse than silence.
static i32 CeilingPerUnit(const TradePolicy& policy, const char* item) {
    const i32 seed = ForumSeedSalePrice(item);
    return seed >= 0 ? seed + seed / 2 : policy.blindPriceCeiling;
}

bool ChooseBuyWant(const prof::Profession& p,
                   const std::vector<Stock>& holdings,
                   const PriceBook& book,
                   const TradePolicy& policy,
                   i32 gold,
                   TradeIntent* out) {
    if (!out) return false;
    // The same list the buy errand is scored from: wants a PLAYER could supply,
    // already filtered for `rawResource` and for affordability.
    const std::vector<Want> wants =
        PlayerMarketWants(p, holdings, gold, policy, nullptr);
    for (const Want& w : wants) {
        i32 ask = CeilingPerUnit(policy, w.item.c_str());
        // A KNOWN PRICE BEATS A CEILING. Announcing the ceiling when this life
        // has actually watched the thing trade at 2gp invites every seller in
        // earshot to charge the ceiling, and the fleet's price discovery goes
        // backwards. Half again over the observed number is the same tolerance
        // ConsiderOffer allows against a forum seed.
        const i32 believed = book.BelievedSalePrice(w.item.c_str());
        if (believed >= 0) ask = std::min(ask, believed + believed / 2);
        if (ask <= 0) continue;

        // WHAT THE PURSE CAN ACTUALLY HONOUR, at that ceiling, without eating
        // the reserve this life keeps for tools. Announcing 20 when it can pay
        // for 3 is a promise the trade window cancels.
        const i32 spendable = gold - p.goldReserve;
        if (spendable < ask) continue;
        const i32 affordable = spendable / ask;
        const i32 qty = std::min(w.qty, affordable);
        if (qty <= 0) continue;

        out->item = w.item;
        out->qty = qty;
        out->pricePerUnit = ask;
        return out->Valid();
    }
    return false;
}

bool AnswerBuyWant(const prof::Profession& p,
                   const std::vector<Stock>& pack,
                   const PriceBook& book,
                   const TradePolicy& policy,
                   const TradeIntent& want,
                   TradeIntent* out) {
    if (!out || want.item.empty()) return false;

    // A DIRECT REQUEST OUTRANKS THE TRIP THRESHOLD -- but not the rule that a
    // life only ever hands over what its own profession makes. Surplus() owns
    // both, so ask it with the threshold relaxed rather than re-deriving it.
    TradePolicy asked = policy;
    asked.minimumSurplusToOffer = 1;
    i32 spare = 0;
    for (const Offer& o : Surplus(p, pack, asked)) {
        if (o.item == want.item) { spare = o.qty; break; }
    }
    if (spare <= 0) return false;

    const i32 believed = book.BelievedSalePrice(want.item.c_str());
    const i32 ask = (believed >= 0) ? believed : policy.openingAsk;
    // The buyer named a ceiling. Undercutting an observed price to meet it
    // teaches the fleet a number this life does not believe, so decline
    // instead -- the goods keep, and the buyer may come back higher.
    if (want.pricePerUnit > 0 && ask > want.pricePerUnit) return false;

    out->item = want.item;
    out->qty = (want.qty > 0) ? std::min(spare, want.qty) : spare;
    out->pricePerUnit = ask;
    return out->Valid();
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
    //
    // blindPriceCeiling is protection for a character with NO basis for the
    // price at all -- the number exists so a character does not hand over a
    // fortune for something it has never seen sold. A forum seed
    // (kForumPriceSeeds) changes that: the buyer is no longer blind, it walked
    // in already knowing roughly what Revolution's own players charged, so
    // holding it to the flat 12gp ceiling would reject a genuine 17gp log
    // offer as if it were still guessing. So WITH a seed the test is honesty
    // to that number instead -- up to 50% over what the forum said, which
    // still catches a seller trying to charge multiples of the going rate --
    // and WITHOUT one, the original blind ceiling stands unchanged.
    const i32 seed = ForumSeedSalePrice(offer.item.c_str());
    if (seed >= 0) {
        if (offer.pricePerUnit > seed + seed / 2) {
            d.reason = "more than this life will pay, even against a known price";
            return d;
        }
    } else if (offer.pricePerUnit > policy.blindPriceCeiling) {
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
