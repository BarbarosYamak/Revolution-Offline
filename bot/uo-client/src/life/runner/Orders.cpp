#include "RunnerInternal.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace uo::life {
using namespace runner_detail;
namespace {
constexpr i64 kOrderLifetimeMs = 15 * 60 * 1000;
constexpr i64 kOrderAnnounceMs = 60000;
bool Orderable(const std::string& item) {
    // Fishers gather, cook and sell their stock; food is not commission work.
    if (item == "i_fish_cut_cooked" || item == "i_fish_cut_raw") return false;
    if (market::WhoProduces(item.c_str()).empty() || econ::GraphicsForItem(item.c_str()).empty()) return false;
    if (item == "i_bandage") return true;
    const auto* recipe = prod::FindRecipe(item.c_str());
    return recipe && recipe->provenance == prod::Provenance::PlayerCrafted && CraftMenuFor(item);
}
bool SameTerms(const CraftOrder& a, const CraftOrder& b) {
    return a.id == b.id && a.terms.item == b.terms.item &&
        a.terms.qty == b.terms.qty && a.terms.pricePerUnit == b.terms.pricePerUnit;
}
i32 KeepOutput(const prof::Profession& p, const std::string& item) {
    return std::find(p.consumes.begin(), p.consumes.end(), item) != p.consumes.end() ? 20 : 0;
}
bool CanMakeOrder(const prof::Profession& p, const Observation& obs, const CraftOrder& o,
                  const market::PriceBook& prices) {
    if (p.id == "fisher") return false;
    if (std::find(p.produces.begin(), p.produces.end(), o.terms.item) == p.produces.end() ||
        econ::GraphicsForItem(o.terms.item.c_str()).empty()) return false;
    const auto floor = market::ComputeCraftedGoodFloor(o.terms.item.c_str(), prices);
    if (floor.applies && o.terms.pricePerUnit < floor.floor) return false;
    if (market::QtyOf(obs.pack, o.terms.item) + market::QtyOf(obs.bank, o.terms.item) >=
        o.terms.qty + KeepOutput(p, o.terms.item)) return true;
    if (o.terms.item == "i_bandage") {
        // The existing sale-cutting goal can draw this cloth from the bank.
        return market::QtyOf(obs.pack, "i_cloth") + market::QtyOf(obs.bank, "i_cloth") >=
               o.terms.qty + 5;
    }
    const prod::Recipe* r = prod::FindRecipe(o.terms.item.c_str());
    const int spell = SpellTaughtByScroll(o.terms.item);
    if (spell > 0 && !obs.KnowsSpell(spell)) return false;
    return r && r->provenance == prod::Provenance::PlayerCrafted &&
        CraftMenuFor(o.terms.item) && obs.SkillTenths(r->skillId) >= r->skillTenths &&
        (r->skillId2 < 0 || obs.SkillTenths(r->skillId2) >= r->skillTenths2);
}
}

market::TradeIntent CraftOrderWant(const prof::Profession& p, const Observation& obs,
                                  const market::PriceBook& prices,
                                  const market::TradePolicy& policy) {
    std::vector<market::Stock> holdings = obs.pack;
    for (const auto& banked : obs.bank) {
        auto found = std::find_if(holdings.begin(), holdings.end(), [&](const auto& s) { return s.item == banked.item; });
        if (found == holdings.end()) holdings.push_back(banked);
        else found->qty += banked.qty;
    }
    prof::Profession active = p;
    for (auto& consumable : active.consumables) {
        if (!consumable.graphics.empty()) {
            const char* item = econ::ItemNameForGraphic(consumable.graphics.front());
            if (item && *item) consumable.name = item;
        }
    }
    // A long-term Poisoning target is not current demand for green bottles.
    if (obs.wantPracticeSkill == rules::kPoisoning && market::QtyOf(obs.pack, "i_dagger") > 0 &&
        std::find(active.consumes.begin(), active.consumes.end(), "i_potion_poison") == active.consumes.end())
        active.consumes.push_back("i_potion_poison");
    if (obs.wantPracticeSkill != rules::kPoisoning)
        active.consumes.erase(std::remove(active.consumes.begin(), active.consumes.end(),
                                         "i_potion_poison"), active.consumes.end());
    market::TradeIntent want;
    const SchoolWeapon* weapon = SchoolWeaponFor(p);
    std::string equipment;
    if (weapon && !obs.schoolWeaponEquipped && market::QtyOf(holdings, weapon->defname) == 0)
        equipment = weapon->defname;
    if (!Orderable(equipment)) equipment.clear();
    if (equipment.empty()) equipment = obs.armorOrderItem;
    if (equipment.empty()) {
        for (const auto& tool : p.tools) {
            if (obs.HasTool(tool.name.c_str()) || tool.graphics.empty()) continue;
            const char* item = econ::ItemNameForGraphic(tool.graphics.front());
            if (!item || market::QtyOf(holdings, item) > 0 || !Orderable(item)) continue;
            if (std::find(p.produces.begin(), p.produces.end(), item) != p.produces.end()) continue;
            equipment = item;
            break;
        }
    }
    if (!equipment.empty() || (obs.food <= 2 && market::QtyOf(holdings, "i_fish_cut_cooked") < 5)) {
        want.item = equipment.empty() ? "i_fish_cut_cooked" : equipment;
        want.qty = equipment.empty() ? 5 : 1;
        const auto ask = market::ComputeCraftedGoodAsk(want.item.c_str(), prices, 0);
        want.pricePerUnit = ask.applies ? ask.high : policy.blindPriceCeiling;
        if (static_cast<i64>(want.qty) * want.pricePerUnit > obs.gold - p.goldReserve)
            want = {};
    }
    if (!want.Valid()) {
        auto orderPolicy = policy;
        active.consumes.erase(std::remove_if(active.consumes.begin(), active.consumes.end(),
            [](const auto& item) { return !Orderable(item); }), active.consumes.end());
        active.consumables.erase(std::remove_if(active.consumables.begin(), active.consumables.end(),
            [](const auto& need) { return !Orderable(need.name); }), active.consumables.end());
        orderPolicy.productionInputs.erase(std::remove_if(orderPolicy.productionInputs.begin(), orderPolicy.productionInputs.end(),
            [](const auto& input) { return !Orderable(input.item); }), orderPolicy.productionInputs.end());
        market::ChooseBuyWant(active, holdings, prices, orderPolicy, obs.gold, &want);
    }
    // A RUNEBOOK FOR A MAGE WHO CAN RECALL. Ordered from a scribe like any
    // other crafted good; Orderable() keeps this silent until a scribe can
    // really make one (menu path + player-crafted recipe).
    if (!want.Valid() && WantsSpellCombat(p) && obs.SkillTenths(rules::kMagery) >= 400 &&
        market::QtyOf(holdings, "i_spellbook_runebook") == 0 && Orderable("i_spellbook_runebook")) {
        const auto ask = market::ComputeCraftedGoodAsk("i_spellbook_runebook", prices, 0);
        const i32 price = ask.applies && ask.high > 0 ? ask.high : 1500;
        if (price <= obs.gold - p.goldReserve) want = {"i_spellbook_runebook", 1, price};
    }
    if (!want.Valid() && WantsSpellCombat(p) && obs.SpellbookRead()) {
        for (const auto& recipe : prod::KnownRecipes()) {
            if (recipe.skillId != rules::kInscription) continue;
            const int number = SpellTaughtByScroll(recipe.output);
            const auto* spell = spell::DefForSpell(number);
            if (!spell || obs.KnowsSpell(number) || market::QtyOf(holdings, recipe.output) > 0 ||
                obs.SkillTenths(rules::kMagery) < spell->minSkillTenths || obs.manaMax < spell->mana) continue;
            const auto ask = market::ComputeCraftedGoodAsk(recipe.output, prices, 0);
            if (!ask.applies || ask.high <= 0 || ask.high > obs.gold - p.goldReserve) continue;
            want = {recipe.output, 1, ask.high};
            break;
        }
    }
    if (want.Valid() && want.pricePerUnit > 0)
        want.qty = std::min({200, want.qty, 65535 / want.pricePerUnit});
    if (!want.Valid() || want.pricePerUnit <= 0 || !Orderable(want.item)) return {};
    return want;
}

CraftOrder* Runner::ActiveCraftOrder(bool buying, i64 now) {
    for (auto& o : state_.craftOrders)
        if (o.buying == buying && o.Active(now)) return &o;
    return nullptr;
}

void Runner::TickCraftOrders(Client& client, const Observation& obs) {
    const auto* me = needCfg_.profession;
    if (!me) return;
    for (auto& o : state_.craftOrders) {
        // Let the bounded secure-trade verifier settle a transfer already
        // in flight, even if its commission deadline passes meanwhile.
        if (o.id == tradeOrderId_ && tradePartner_ == o.partnerSerial &&
            (client.Trade().Active() || client.Trade().CurrentPhase() == trade::Phase::Completed)) continue;
        if (o.phase <= OrderPhase::Ready &&
            ((me->id == "fisher" && !o.buying) ||
             o.terms.item == "i_fish_cut_cooked" || o.terms.item == "i_fish_cut_raw")) {
            o.phase = OrderPhase::Cancelled;
            if (!o.buying) state_.productionBatch = {};
            LogLine("order_cancelled id=%s reason=\"fishing stock is not commission work\"", o.id.c_str());
            continue;
        }
        if (o.phase <= OrderPhase::Ready && !o.Active(obs.nowMs)) {
            o.phase = OrderPhase::Expired;
            if (!o.buying) state_.productionBatch = {};
            LogLine("order_expired id=%s item=%s delivered=%d", o.id.c_str(),
                    o.terms.item.c_str(), o.delivered);
        }
    }
    // Keep a small persisted history, never discard a live commitment.
    while (state_.craftOrders.size() > 16) {
        auto old = std::find_if(state_.craftOrders.begin(), state_.craftOrders.end(),
            [&](const CraftOrder& o) { return o.id != tradeOrderId_ && !o.Active(obs.nowMs); });
        if (old == state_.craftOrders.end()) break;
        state_.craftOrders.erase(old);
    }
    if (obs.dead || obs.underAttack || obs.attackersOnMe > 0 || client.Trade().Active()) return;
    if (!tradePartner_) {
        if (auto* order = ActiveCraftOrder(true, obs.nowMs)) {
            const i32 held = market::QtyOf(obs.pack, order->terms.item) + market::QtyOf(obs.bank, order->terms.item);
            const auto* school = SchoolWeaponFor(*me);
            const bool equipped = school && order->terms.item == school->defname && obs.schoolWeaponEquipped;
            bool worn = false;
            for (u16 graphic : econ::GraphicsForItem(order->terms.item.c_str())) {
                const u8 layer = client.ItemEquipLayer(graphic);
                if (layer && client.EquippedGraphicAt(layer) == graphic) worn = true;
            }
            const int spell = SpellTaughtByScroll(order->terms.item);
            if ((order->targetStock > 0 && held >= order->targetStock) || equipped || worn ||
                (spell > 0 && obs.KnowsSpell(spell)) ||
                (order->terms.item == "i_fish_cut_cooked" && obs.food >= 5) ||
                static_cast<i64>(order->terms.qty - order->delivered) * order->terms.pricePerUnit >
                    obs.gold - me->goldReserve) {
                if (!order->partner.empty()) client.ActionSay(FormatCraftOrder(*order, "CANCEL", order->partner).c_str());
                order->phase = OrderPhase::Cancelled;
                LogLine("order_cancelled id=%s reason=need or budget changed", order->id.c_str());
            }
        }
    }
    std::vector<Client::Heard> heard;
    const i64 journalNow = client.JournalNowMs();
    orderHeardMs_ = std::max(orderHeardMs_, journalNow - 10000);
    client.JournalHeardSince(orderHeardMs_, heard);
    if (!heard.empty()) orderHeardMs_ = heard.back().timeMs;
    for (const auto& h : heard) {
        if (!h.speaker || h.speaker == client.PlayerSerial() || h.name.empty() || SocialFoe(h.name)) continue;
        CraftOrder message;
        std::string verb;
        if (!ParseCraftOrder(h.text, verb, message) ||
            !market::AddressedTo(h.text, state_.identity.characterName)) continue;
        if (verb != "REQUEST" && market::SpeechAddressee(h.text).empty()) continue;
        if (verb == "REQUEST") {
            if (ActiveCraftOrder(false, obs.nowMs) || !CanMakeOrder(*me, obs, message, state_.prices)) continue;
            bool seen = false;
            for (const auto& o : state_.craftOrders) if (o.id == message.id) seen = true;
            i32 x = 0, y = 0; i8 z = 0;
            if (seen || !client.MobilePosition(h.speaker, &x, &y, &z)) continue;
            message.buying = false;
            message.partner = h.name; message.partnerSerial = h.speaker;
            message.phase = OrderPhase::Accepted;
            message.x = x; message.y = y;
            message.createdMs = obs.nowMs; message.expiresMs = obs.nowMs + kOrderLifetimeMs;
            state_.craftOrders.push_back(message);
            if (!craftItem_.empty())
                LeaveGoal(client, GoalKind::Craft, GoalKind::Craft, false, "accepted customer order");
            if (const auto* recipe = prod::FindRecipe(message.terms.item.c_str())) {
                state_.productionBatch.item = message.terms.item;
                const i32 remaining = message.terms.qty + KeepOutput(*me, message.terms.item) -
                                      market::QtyOf(obs.pack, message.terms.item);
                state_.productionBatch.attempts = std::max(1, (remaining + recipe->outputQty - 1) / recipe->outputQty);
                state_.productionBatch.phase = ProductionPhase::Stock;
            }
            client.ActionSay(FormatCraftOrder(message, "ACCEPT", h.name).c_str());
            planner_.ClearCooldown(GoalKind::Craft);
            planner_.ClearCooldown(GoalKind::MakeBandagesForSale);
            LogLine("order_accepted id=%s buyer=%s qty=%d item=%s price=%d",
                    message.id.c_str(), h.name.c_str(), message.terms.qty,
                    message.terms.item.c_str(), message.terms.pricePerUnit);
        } else {
            for (auto& o : state_.craftOrders) {
                if (!o.Active(obs.nowMs) || !SameTerms(o, message)) continue;
                if (verb == "ACCEPT" && o.buying) {
                    if (o.partnerSerial && o.partnerSerial != h.speaker) {
                        client.ActionSay(FormatCraftOrder(o, "CANCEL", h.name).c_str());
                        continue;
                    }
                    if (o.partnerSerial == h.speaker && o.partner == h.name) continue;
                    o.partner = h.name; o.partnerSerial = h.speaker;
                    o.phase = OrderPhase::Accepted;
                    o.expiresMs = obs.nowMs + kOrderLifetimeMs;
                    coinWanted_ = o.terms.qty * o.terms.pricePerUnit + me->goldReserve;
                    planner_.ClearCooldown(GoalKind::Bank);
                    LogLine("order_committed id=%s crafter=%s", o.id.c_str(), h.name.c_str());
                } else if (o.partnerSerial == h.speaker && o.partner == h.name) {
                    if (verb == "CANCEL") {
                        o.phase = OrderPhase::Cancelled;
                        if (!o.buying) state_.productionBatch = {};
                    }
                    if (verb == "READY" && o.buying) {
                        o.phase = OrderPhase::Ready;
                        planner_.ClearCooldown(GoalKind::TradeWithPlayer);
                    }
                }
            }
        }
    }
    if (auto* sale = ActiveCraftOrder(false, obs.nowMs)) {
        const i32 needed = sale->terms.qty - sale->delivered + KeepOutput(*me, sale->terms.item);
        if (market::QtyOf(obs.pack, sale->terms.item) >= needed) {
            // Finished customer stock needs delivery, not another input batch.
            state_.productionBatch = {};
            if (sale->phase != OrderPhase::Ready) {
                sale->phase = OrderPhase::Ready;
                LogLine("order_ready id=%s item=%s qty=%d", sale->id.c_str(),
                        sale->terms.item.c_str(), sale->terms.qty - sale->delivered);
                planner_.ClearCooldown(GoalKind::TradeWithPlayer);
            }
        } else if (sale->terms.item != "i_bandage") {
            sale->phase = OrderPhase::Accepted;
            const auto* recipe = prod::FindRecipe(sale->terms.item.c_str());
            if (recipe) {
                state_.productionBatch.item = sale->terms.item;
                state_.productionBatch.attempts = std::max(1,
                    (needed - market::QtyOf(obs.pack, sale->terms.item) + recipe->outputQty - 1) / recipe->outputQty);
                state_.productionBatch.phase = ProductionShopping(state_.productionBatch, obs).missing.empty()
                    ? ProductionPhase::Work : ProductionPhase::Stock;
            }
        } else {
            sale->phase = OrderPhase::Accepted;
        }
    }
    if (client.ActionBusy() || client.TravelBusy() || client.GotoBusy() ||
        !NearAnyBank(client, obs) || obs.nowMs - orderAnnouncedMs_ < kOrderAnnounceMs) return;
    if (auto* buy = ActiveCraftOrder(true, obs.nowMs)) {
        if (buy->phase == OrderPhase::Requested) {
            client.ActionSay(FormatCraftOrder(*buy, "REQUEST").c_str());
            orderAnnouncedMs_ = obs.nowMs;
        }
        return;
    }
    const auto want = CraftOrderWant(*me, obs, state_.prices, tradePolicy_);
    if (!want.Valid()) return;
    CraftOrder order;
    order.id = std::to_string(client.PlayerSerial()) + "_" + std::to_string(obs.nowMs);
    order.terms = want; order.buying = true;
    order.targetStock = market::QtyOf(obs.pack, want.item) + market::QtyOf(obs.bank, want.item) + want.qty;
    order.x = obs.x; order.y = obs.y;
    order.createdMs = obs.nowMs; order.expiresMs = obs.nowMs + 120000;
    state_.craftOrders.push_back(order);
    client.ActionSay(FormatCraftOrder(order, "REQUEST").c_str());
    orderAnnouncedMs_ = obs.nowMs;
    LogLine("order_requested id=%s item=%s qty=%d price=%d", order.id.c_str(),
            want.item.c_str(), want.qty, want.pricePerUnit);
}

void Runner::AddCraftOrderNeeds(const Observation& obs, std::vector<Need>& needs) {
    CraftOrder* sale = ActiveCraftOrder(false, obs.nowMs);
    CraftOrder* buy = ActiveCraftOrder(true, obs.nowMs);
    if (!sale && (!buy || buy->phase == OrderPhase::Requested)) return;
    if (sale) {
        for (auto& n : needs) {
            if ((n.kind == NeedKind::NeedGold && n.what == "sell surplus") ||
                (n.kind == NeedKind::NeedTrade && n.what == "sell to a player") ||
                (n.kind == NeedKind::NeedBank && n.what == "put unsold stock away")) {
                n.blocked = true; n.urgency = 0;
                n.reason = "stock is committed to a customer order";
            }
        }
        if (sale->phase != OrderPhase::Ready) {
            Need work;
            if (market::QtyOf(obs.bank, sale->terms.item) > 0 && obs.WeightFraction() < 0.80) {
                work.kind = NeedKind::NeedBank; work.what = "withdraw customer order";
                work.urgency = 1.2; work.reason = "deliver finished goods already in the bank";
                needs.insert(needs.begin(), work);
                return;
            }
            work.kind = sale->terms.item == "i_bandage" ? NeedKind::NeedBandagesForSale : NeedKind::NeedCraft;
            work.what = sale->terms.item;
            work.urgency = 1.2;
            work.reason = "produce an accepted customer order";
            if (sale->terms.item != "i_bandage" && state_.productionBatch.phase != ProductionPhase::Work)
                work.blocked = true;
            needs.insert(needs.begin(), work);
            return;
        }
    }
    // Buyers revisit the observed meeting point while a commission is being
    // prepared. They do not receive a remote view of the crafter's inventory.
    if (sale || (buy && (buy->phase == OrderPhase::Ready || obs.nowMs - buy->createdMs > 120000))) {
        Need delivery;
        delivery.kind = NeedKind::NeedTrade;
        delivery.urgency = 1.3;
        delivery.what = sale ? "sell to a player" : "buy from a player";
        delivery.reason = "meet the partner for an accepted crafting order";
        needs.insert(needs.begin(), delivery);
    }
}

bool Runner::DriveCraftOrder(Client& client, const Observation& obs) {
    if (tradePartner_ || client.Trade().Active()) return false;
    auto* order = ActiveCraftOrder(false, obs.nowMs);
    if (!order || order->phase != OrderPhase::Ready) order = ActiveCraftOrder(true, obs.nowMs);
    if (!order || order->phase == OrderPhase::Requested) return false;
    const auto* me = needCfg_.profession;
    if (order->buying) {
        const i32 bill = (order->terms.qty - order->delivered) * order->terms.pricePerUnit;
        if (obs.gold - me->goldReserve < bill) {
            client.ActionSay(FormatCraftOrder(*order, "CANCEL", order->partner).c_str());
            order->phase = OrderPhase::Cancelled;
            LogLine("order_cancelled id=%s reason=insufficient funds", order->id.c_str());
            return false;
        }
        if (obs.goldOnHand - me->goldReserve < bill) {
            coinWanted_ = bill + me->goldReserve;
            HandOff(GoalKind::TradeWithPlayer, GoalKind::Bank, 1000,
                    "withdraw payment for accepted order", obs.nowMs);
            return true;
        }
    }
    i32 x = 0, y = 0; i8 z = 0;
    const bool partnerNearby = client.MobilePosition(order->partnerSerial, &x, &y, &z) &&
        TileDist(obs.x, obs.y, x, y) <= 12;
    if (!partnerNearby && TileDist(obs.x, obs.y, order->x, order->y) > 4) {
        if (!client.TravelBusy() && !client.GotoBusy())
            client.TravelToPoint(order->x, order->y, 3, "customer order meeting");
        nextActionMs_ = obs.nowMs + 1500;
        return true;
    }
    if (order->buying) {
        tradeWant_ = order->terms;
        tradeWant_.qty -= order->delivered;
        tradeWantAskedMs_ = obs.nowMs;
    }
    if (partnerNearby &&
        (!order->buying || order->phase == OrderPhase::Ready)) {
        tradePartner_ = order->partnerSerial; tradePartnerName_ = order->partner;
        tradeOrderId_ = order->id;
        tradeItem_ = order->terms.item;
        tradeOffer_ = order->terms; tradeOffer_.qty -= order->delivered;
        tradeSellingQty_ = order->buying ? 0 : tradeOffer_.qty;
        tradeWantQty_ = order->buying ? tradeOffer_.qty : 0;
        tradeOfferPrice_ = order->terms.pricePerUnit;
        tradeAnnouncedMs_ = obs.nowMs;
        if (!order->buying) client.ActionSay(FormatCraftOrder(*order, "READY", order->partner).c_str());
        nextActionMs_ = obs.nowMs + 1500;
        return true;
    }
    nextActionMs_ = obs.nowMs + 2000;
    return true;
}

void Runner::SettleCraftOrder(const Observation& obs) {
    const bool buying = tradeSellingQty_ == 0;
    auto found = std::find_if(state_.craftOrders.begin(), state_.craftOrders.end(),
        [&](const auto& candidate) { return candidate.id == tradeOrderId_ && candidate.buying == buying; });
    if (found == state_.craftOrders.end()) return;
    auto* order = &*found;
    if (order->phase == OrderPhase::Completed || order->phase == OrderPhase::Cancelled ||
        order->partnerSerial != tradePartner_ || order->terms.item != tradeItem_) return;
    const i32 now = market::QtyOf(obs.pack, tradeItem_);
    const i32 qty = buying ? now - tradePackBefore_ : tradePackBefore_ - now;
    const i32 gold = buying ? tradeGoldBefore_ - obs.goldOnHand : obs.goldOnHand - tradeGoldBefore_;
    if (qty <= 0 || qty > order->terms.qty - order->delivered ||
        gold != static_cast<i64>(qty) * order->terms.pricePerUnit) return;
    order->delivered += qty;
    if (order->delivered == order->terms.qty) order->phase = OrderPhase::Completed;
    LogLine("order_delivered id=%s qty=%d total=%d/%d gold=%d complete=%d", order->id.c_str(),
            qty, order->delivered, order->terms.qty, gold, order->phase == OrderPhase::Completed);
}
}
