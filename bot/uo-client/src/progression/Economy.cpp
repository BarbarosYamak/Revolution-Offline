#include "uo/economy.h"

namespace uo::econ {

const char* PriceSourceName(PriceSource s) {
    switch (s) {
        case PriceSource::VendorObserved:      return "vendor_observed";
        case PriceSource::PlayerTradeObserved: return "player_trade_observed";
        case PriceSource::ItemdefValue:        return "itemdef_value";
        case PriceSource::HistoricalForum:     return "historical_forum";
        default:                               return "none";
    }
}

bool IsLivePrice(PriceSource s) {
    // ItemdefValue counts as live: it is this ruleset's own number, read from
    // this shard's scripts. It is still not what a vendor pays -- M3 measured
    // a VALUE=2 fish selling for 1gp -- so callers preferring an observation
    // will find one ranked higher below.
    return s == PriceSource::VendorObserved ||
           s == PriceSource::PlayerTradeObserved ||
           s == PriceSource::ItemdefValue;
}

namespace {

// Lower is better.
int Rank(PriceSource s) {
    switch (s) {
        case PriceSource::VendorObserved:      return 0;
        case PriceSource::PlayerTradeObserved: return 1;
        case PriceSource::ItemdefValue:        return 2;
        default:                               return 99;
    }
}

} // namespace

void PriceBook::Observe(const Price& p) {
    if (!p.graphic) return;
    for (Price& e : prices_) {
        if (e.graphic != p.graphic || e.source != p.source) continue;
        // Newest wins within a source: a vendor's price today beats the same
        // vendor's price an hour ago.
        if (p.atMs >= e.atMs) e = p;
        return;
    }
    prices_.push_back(p);
}

const Price* PriceBook::Best(u16 graphic) const {
    const Price* best = nullptr;
    for (const Price& e : prices_) {
        if (e.graphic != graphic || !IsLivePrice(e.source)) continue;
        if (!best || Rank(e.source) < Rank(best->source) ||
            (Rank(e.source) == Rank(best->source) && e.atMs > best->atMs)) {
            best = &e;
        }
    }
    return best;
}

const Price* PriceBook::HistoricalBaseline(u16 graphic) const {
    const Price* best = nullptr;
    for (const Price& e : prices_) {
        if (e.graphic != graphic || e.source != PriceSource::HistoricalForum) continue;
        if (!best || e.atMs > best->atMs) best = &e;
    }
    return best;
}

ProfitEstimate EstimateTransformation(const Transformation& t, i32 amount,
                                      const PriceBook& book) {
    ProfitEstimate est;
    if (amount <= 0 || t.outputPerInput <= 0) return est;

    const Price* raw = book.Best(t.inputGraphic);
    const Price* out = book.Best(t.outputGraphic);
    if (!raw || !out) {
        // Say so rather than guessing. A bot with no price for the product
        // should go and look at a vendor, not invent a number.
        return est;
    }

    est.valid = true;
    est.cost = raw->gold * amount;                                  // what selling raw would have made
    est.revenue = out->gold * amount * t.outputPerInput;             // what the product makes
    est.margin = est.revenue - est.cost;
    est.marginPerInputUnit = est.margin / amount;

    // Per-stone is the number that stopped M3's fisher dropping its catch on
    // the dock. Weight is in tenths, so scale to keep integer resolution.
    const i32 outWeightTenths = t.outputWeightTenths * t.outputPerInput * amount;
    if (outWeightTenths > 0) {
        est.marginPerStone = (est.margin * 100) / outWeightTenths;
    }
    return est;
}

const std::vector<Transformation>& KnownTransformations() {
    // Everything here was established on THIS shard during M3 and is cited.
    static const std::vector<Transformation> kAll = [] {
        std::vector<Transformation> v;

        Transformation carve;
        carve.name = "carve fish";
        carve.inputGraphic = 0x09CC;      // i_fish_big_1; _2.._4 behave the same
        carve.outputGraphic = 0x097A;     // i_fish_cut_raw
        carve.outputPerInput = 4;
        // Any bladed weapon: IT_WEAPON_FENCE / SWORD / AXE / MACE_SHARP /
        // CARPENTRY_CHOP. A dagger is in every character's newbie kit. The
        // fishing pole is NOT one -- it is t_fish_pole, not a weapon type.
        carve.toolGraphics = {0x0F51, 0x0F52};
        carve.skillId = -1;               // no check at all
        carve.requiredSkillTenths = 0;
        carve.inputWeightTenths = 50;     // WEIGHT=5.0
        carve.outputWeightTenths = 1;     // WEIGHT=0.1
        carve.evidence =
            "Source-X CClientTarg.cpp:1948-1951 SetAmount(4*GetAmount()); "
            "measured live m3_cut1 (12 fish -> 48 steaks, 24 sold for 48gp)";
        v.push_back(carve);

        return v;
    }();
    return kAll;
}

} // namespace uo::econ
