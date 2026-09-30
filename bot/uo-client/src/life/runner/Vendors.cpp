#include "life/runner/RunnerInternal.h"
#include "uo/player_vendor.h"

#include <cstdlib>

// PLAYER VENDORS, both sides (uo/player_vendor.h).
//
//   owner  a house owner with goods no NPC buys buys a vendor deed (the
//          innkeeper and tavernkeeper carried it in the shard's vendor table,
//          commented out today), uses it in its house, remembers the vendor
//          that appears there, drops surplus on it and answers each price
//          prompt (0x9A) with AskingPrice -- which always nets more than an
//          NPC would pay after Revolution's ~10% tax -- then asks it to hand
//          over the takings ("vendor collect").
//   buyer  walks to a player vendor it has walked past before, says "buy",
//          and takes one thing it is short of when it beats the NPC shop and
//          is worth it (WorthBuying).
//
// UNVERIFIED on this tree: the deed, the drop-to-stock prompt and "vendor
// collect" are stock Sphere player-vendor behaviour, but the system was never
// ported to this runtime. Every step is proven by what the server shows (a
// vendor mobile appearing, a prompt arriving, a shop list), never assumed.

namespace uo::life {
using namespace runner_detail;

namespace {
constexpr u16 kDeed = 0x14F0;

const KnownPlace* PlaceOfKind(const Memory& m, const char* kind) {
    for (const KnownPlace& p : m.Places()) if (p.kind == kind) return &p;
    return nullptr;
}
}  // namespace

u32 Runner::MyVendorSerial() const {
    const KnownPlace* v = PlaceOfKind(state_.memory, "my_vendor");
    return v ? static_cast<u32>(std::strtoul(v->name.c_str(), nullptr, 16)) : 0;
}

void Runner::AddVendorNeeds(Client& client, const Observation& obs, std::vector<Need>& needs) {
    if (obs.dead || obs.underAttack || obs.attackersOnMe > 0 || !needCfg_.profession) return;
    if (obs.nowMs < vendorRestUntilMs_) return;
    Need n;
    n.kind = NeedKind::NeedVendor;
    // The owner: a house, goods only players buy, and a purse for the deed.
    market::TradeIntent offer;
    const bool surplus = market::ChooseSellOffer(*needCfg_.profession, obs.pack, state_.prices, tradePolicy_, &offer);
    if (OwnsHouse() && surplus && (MyVendorSerial() || obs.gold >= 5000) &&
        obs.nowMs - vendorStockedMs_ >= 60LL * 60000) {
        n.what = "stock my vendor"; n.urgency = 0.40; n.reason = "surplus only players buy, and a house to sell it from";
        needs.push_back(n);
        return;
    }
    // The buyer: a player vendor it knows, short of something, not visited lately.
    const std::vector<market::Want> wants = market::Shortfall(*needCfg_.profession, obs.pack, tradePolicy_);
    if (wants.empty() || obs.gold < 300) return;
    for (const KnownPlace& p : state_.memory.Places()) {
        if (p.kind != "player_vendor" || TileDist(obs.x, obs.y, p.x, p.y) > 120) continue;
        if (obs.nowMs - vendorVisitedMs_ < 2LL * 60 * 60000) return;
        n.what = "browse a player vendor"; n.urgency = 0.25; n.reason = "short of " + wants.front().item;
        needs.push_back(n);
        return;
    }
}

bool Runner::DoRunVendor(Client& client, const Observation& obs) {
    if (OwnsHouse() && (MyVendorSerial() || client.FindBackpackItemByGraphic(kDeed) || obs.gold >= 5000))
        return OwnVendor(client, obs);
    return BrowseVendor(client, obs);
}

bool Runner::OwnVendor(Client& client, const Observation& obs) {
    const KnownPlace* house = PlaceOfKind(state_.memory, "house");
    if (!house) return true;
    const u32 vendor = MyVendorSerial();

    // Answer the price prompt the last drop raised.
    if (vendorPricePending_ && client.PromptActive()) {
        client.ActionAnswerPrompt(std::to_string(vendorPricePending_));
        LogLine("vendor: priced at %d", vendorPricePending_);
        vendorPricePending_ = 0;
        ++vendorListed_;
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    if (vendorPricePending_ && obs.nowMs - vendorDropMs_ > 6000) {
        LogLine("vendor: no price prompt came -- stopping for now");
        vendorPricePending_ = 0;
        vendorRestUntilMs_ = obs.nowMs + 4LL * 60 * 60000;
        return true;
    }
    if (client.TravelBusy() || client.ActionBusy()) return false;

    if (TileDist(obs.x, obs.y, house->x, house->y) > 3) {
        client.TravelToPoint(house->x, house->y, 2, "my house");
        return false;
    }

    if (!vendor) {
        // Place one: the deed first (bought if need be), then look for the
        // vendor that appears beside us.
        u32 deed = 0;
        for (usize i = 0; i < client.ContainerItemCount(client.BackpackSerial()); ++i) {
            u32 s = 0; u16 g = 0, a = 0;
            if (!client.ContainerItemAt(client.BackpackSerial(), i, &s, &g, &a) || g != kDeed) continue;
            const std::string* label = client.ServerItemName(s);
            if (!label) { client.ActionLookAt(s); nextActionMs_ = obs.nowMs + 1500; return false; }
            if (label->find("endor") != std::string::npos || label->find("mploy") != std::string::npos) deed = s;
        }
        if (vendorPlacedMs_ && obs.nowMs - vendorPlacedMs_ < 20000) {
            std::vector<Client::TitledMobile> titled;
            client.TitledMobilesNear(6, titled);
            for (const auto& m : titled) {
                if (!vendors::IsPlayerVendorTitle(m.title)) continue;
                char hex[16];
                std::snprintf(hex, sizeof(hex), "%08X", m.serial);
                state_.memory.NotePlace("my_vendor", hex, m.x, m.y, 0, obs.nowMs);
                LogLine("vendor: our vendor stands in the house ('%s')", m.title.c_str());
                Checkpoint(client, obs.nowMs, "vendor placed");
                return false;
            }
            if (obs.nowMs - vendorScanAskMs_ > 3000) { vendorScanAskMs_ = obs.nowMs; client.ActionIdentifyNearbyPerson(); }
            return false;
        }
        if (deed) {
            LogLine("vendor: using the vendor deed in the house");
            client.ActionUseObject(deed);
            vendorPlacedMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        if (vendorPlacedMs_) {   // used a deed and no vendor came: the server refused
            vendorPlacedMs_ = 0;
            return BlockNeed(GoalKind::RunVendor, NeedKind::NeedVendor, BlockScope::Window,
                             "the vendor deed placed nothing here", 12LL * 60 * 60000, obs.nowMs);
        }
        if (FetchCoinForPurchase(client, obs, 3500)) return false;
        if (!vendorErrand_.Running()) {
            life::VendorErrandSpec spec;
            spec.Sell("innkeeper", wm::Service::Innkeeper);
            spec.Sell("tavernkeeper", wm::Service::Innkeeper);
            spec.graphic = kDeed;
            spec.nameContains = "vendor";
            spec.qty = 1;
            spec.what = "vendor deed";
            spec.maxPricePerUnit = 5000;
            vendorErrand_.Begin(spec);
        }
        const life::VendorErrandResult r = vendorErrand_.Tick(client, obs);
        LogErrandReason("vendor deed", r.why.c_str(), obs.nowMs);
        if (r.wake == life::Wake::AfterDelay && r.delayMs > 0) nextActionMs_ = obs.nowMs + r.delayMs;
        if (life::IsTerminal(r.status)) {
            vendorErrand_.Cancel();
            if (r.status != life::ActivityStatus::Success)
                return BlockNeed(GoalKind::RunVendor, NeedKind::NeedVendor, BlockScope::Window,
                                 "no innkeeper sells a vendor deed", 24LL * 60 * 60000, obs.nowMs);
        }
        return false;
    }

    // Stock it: one surplus stack per drop, priced by what we have seen.
    market::TradeIntent offer;
    if (vendorListed_ >= 5 ||
        !market::ChooseSellOffer(*needCfg_.profession, obs.pack, state_.prices, tradePolicy_, &offer)) {
        client.ActionSay("vendor collect");
        LogLine("vendor: stocked %d item(s) this visit", vendorListed_);
        vendorListed_ = 0;
        vendorStockedMs_ = obs.nowMs;
        return true;
    }
    i32 have = 0;
    const u32 stack = FindBackpackItemByName(client, offer.item.c_str(), &have);
    if (!stack) { vendorStockedMs_ = obs.nowMs; return true; }
    vendors::PriceInputs in;
    in.playerPrice = offer.pricePerUnit;
    if (const auto* npc = state_.prices.Latest(offer.item.c_str(), market::PriceSource::NpcVendorBuys))
        in.npcBuysFor = npc->pricePerUnit;
    if (const auto* shop = state_.prices.Latest(offer.item.c_str(), market::PriceSource::NpcVendorSells))
        in.npcSellsFor = shop->pricePerUnit;
    const i32 qty = std::min(have, std::max(1, offer.qty));
    const i32 price = vendors::AskingPrice(in) * qty;
    if (price <= 0) { vendorStockedMs_ = obs.nowMs; return true; }
    LogLine("vendor: offering %d %s for %d", qty, offer.item.c_str(), price);
    client.ActionMoveItem(stack, static_cast<u16>(qty), vendor);
    vendorPricePending_ = price;
    vendorDropMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 1500;
    return false;
}

bool Runner::BrowseVendor(Client& client, const Observation& obs) {
    const std::vector<market::Want> wants = market::Shortfall(*needCfg_.profession, obs.pack, tradePolicy_);
    const KnownPlace* best = nullptr;
    for (const KnownPlace& p : state_.memory.Places())
        if (p.kind == "player_vendor" && (!best || TileDist(obs.x, obs.y, p.x, p.y) < TileDist(obs.x, obs.y, best->x, best->y)))
            best = &p;
    if (!best || wants.empty()) return true;
    if (client.TravelBusy() || client.ActionBusy()) return false;
    if (TileDist(obs.x, obs.y, best->x, best->y) > 3) {
        client.TravelToPoint(best->x, best->y, 2, "a player vendor");
        return false;
    }
    std::vector<Client::TitledMobile> titled;
    client.TitledMobilesNear(6, titled);
    u32 vendor = 0;
    for (const auto& m : titled) if (vendors::IsPlayerVendorTitle(m.title)) vendor = m.serial;
    if (!vendor) {
        if (obs.nowMs - vendorScanAskMs_ > 3000) { vendorScanAskMs_ = obs.nowMs; client.ActionIdentifyNearbyPerson(); return false; }
        if (obs.nowMs - vendorBrowseStartMs_ > 30000) { vendorVisitedMs_ = obs.nowMs; return true; }
        return false;
    }
    if (!vendorBrowseStartMs_) vendorBrowseStartMs_ = obs.nowMs;
    if (client.VendorOffer().empty()) {
        if (obs.nowMs - vendorOpenMs_ > 5000) { vendorOpenMs_ = obs.nowMs; client.ActionVendorOpen(vendor, "buy"); }
        if (obs.nowMs - vendorBrowseStartMs_ > 30000) { vendorVisitedMs_ = obs.nowMs; vendorBrowseStartMs_ = 0; return true; }
        return false;
    }
    // One purchase per visit: the first shortfall item that is worth buying.
    for (const market::Want& w : wants) {
        for (const Client::VendorItem& v : client.VendorOffer()) {
            const char* item = econ::ItemNameForGraphic(v.graphic);
            if (!item || w.item != item) continue;
            const auto* shop = state_.prices.Latest(item, market::PriceSource::NpcVendorSells);
            const i32 worth = std::max(state_.prices.BelievedSalePrice(item) * 2, shop ? shop->pricePerUnit : 0);
            if (!vendors::WorthBuying(static_cast<i32>(v.price), shop ? shop->pricePerUnit : -1, worth, obs.gold / 4))
                continue;
            LogLine("vendor: buying %s from a player vendor for %u", item, v.price);
            client.ActionVendorBuy(vendor, v.serial, 1);
            state_.prices.Note({item, static_cast<i32>(v.price), market::PriceSource::PlayerTraded, "player vendor",
                                obs.x, obs.y, obs.nowMs});
            vendorVisitedMs_ = obs.nowMs; vendorBrowseStartMs_ = 0;
            client.ForgetVendorOffer();
            planner_.NoteProgress();
            return true;
        }
    }
    LogLine("vendor: nothing here worth buying");
    vendorVisitedMs_ = obs.nowMs; vendorBrowseStartMs_ = 0;
    client.ForgetVendorOffer();
    return true;
}

}  // namespace uo::life
