#include "uo/activities/buy.h"

#include "Client.h"
#include "uo/life.h"

namespace uo::life {

void BuyActivity::Begin(const BuyRequest& req) {
    req_ = req;
    running_ = true;
    keeper_ = 0;
    why_ = "";
    errand_.Cancel();
}

void BuyActivity::Cancel() {
    running_ = false;
    errand_.Cancel();
}

ActivityTickResult BuyActivity::Tick(Client& client, const Observation& obs) {
    if (!running_)
        return ActivityTickResult::Done(ActivityStatus::Failed,
                                        "the purchase was never begun");

    // WHAT DO WE ALREADY HOLD? Asked every tick, not once at the start,
    // because the answer changes underneath: a trade completes, a craft
    // consumes, another errand buys. Deciding once and acting on a stale
    // number is how a bot buys what it already has.
    const i32 held = static_cast<i32>(client.BackpackItemCount(req_.graphic));
    const BuyPlan plan = Decide(req_, held, client.PlayerGold(), 0);

    if (plan.satisfied) {
        running_ = false;
        why_ = plan.reason;
        // SUCCESS, and honestly so: the world is in the state the request
        // asked for. Nothing was bought, and nothing needed to be.
        return ActivityTickResult::Done(ActivityStatus::Success, plan.reason);
    }
    if (plan.blocked) {
        running_ = false;
        why_ = plan.reason;
        // Blocked, not Failed: the answer changes when the purse does.
        return ActivityTickResult::Done(ActivityStatus::Blocked, plan.reason);
    }

    if (!errand_.Running()) {
        VendorErrandSpec spec;
        for (i32 i = 0; i < req_.sellerCount; ++i)
            spec.Sell(req_.sellers[i].trade, req_.sellers[i].service);
        spec.graphic = req_.graphic;
        spec.qty = plan.quantity;
        spec.what = req_.item;
        spec.goldFloor = req_.minimumGoldReserve;
        spec.maxPricePerUnit = req_.maxPricePerUnit;
        for (i32 i = 0; i < req_.avoidCount; ++i) spec.Avoid(req_.avoid[i]);
        errand_.Begin(spec);
    }

    const VendorErrandResult r = errand_.Tick(client, obs);
    if (r.keeper) keeper_ = r.keeper;

    ActivityTickResult out;
    out.status = r.status;
    out.wake = r.wake;
    out.delayMs = r.delayMs;
    out.acted = r.acted;
    out.offerOpen = r.offerOpen;
    // The errand's reason is a std::string built per tick; hold it here so
    // the caller's const char* stays valid for the length of this tick.
    lastWhy_ = r.why;
    out.reason = lastWhy_.c_str();

    if (IsTerminal(r.status)) {
        running_ = false;
        why_ = lastWhy_.c_str();
    }
    return out;
}

}  // namespace uo::life
