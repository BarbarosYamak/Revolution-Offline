#include "uo/activities/acquire.h"

#include "Client.h"
#include "uo/interaction/progress.h"
#include "uo/life.h"

namespace uo::life {

namespace {

// What the CLIENT allows an equip action (Client.cpp's kEquipTimeoutMs),
// mirrored so the handshake derives its retry gap from the real number rather
// than from a hand-picked one. That derivation is the whole reason the
// 2.5-second retries could exist elsewhere.
constexpr i64 kEquipActionDeadlineMs = 4000;

}  // namespace

// ---------------------------------------------------------------------------
// EquipActivity
// ---------------------------------------------------------------------------

void EquipActivity::Begin(u16 graphic, u8 layer, const char* what) {
    graphic_ = graphic;
    layer_ = layer;
    what_ = (what && what[0]) ? what : "the item";
    running_ = true;

    RetryPolicy rp;
    rp.actionDeadlineMs = kEquipActionDeadlineMs;
    rp.maxAttempts = 3;
    rp.backoffMs = 1500;
    ask_.Configure(rp);
    ask_.Reset();
}

ActivityTickResult EquipActivity::Tick(Client& client, const Observation& obs) {
    if (!running_)
        return ActivityTickResult::Done(ActivityStatus::Failed,
                                        "the equip was never begun");

    // THE PAPERDOLL IS THE ANSWER, not the packet (section 18). This is the
    // check whose absence let a bought piece sit in the pack while the slot
    // read empty and the next tick bought another.
    Expectation want;
    want.equipLayer = layer_;
    want.equipGraphic = graphic_;

    Observed seen;
    seen.equippedAtLayer = client.EquippedGraphicAt(layer_);

    if (Verify(want, seen).verdict == Verdict::Confirmed) {
        running_ = false;
        ask_.Note(Outcome::Succeeded, obs.nowMs);
        lastWhy_ = std::string(what_) + " is on the paperdoll";
        return ActivityTickResult::Done(ActivityStatus::Success,
                                        lastWhy_.c_str());
    }

    if (client.ActionBusy())
        return ActivityTickResult::Waiting(Wake::ActionResolves, 0,
                                           "the equip is in flight");

    // A definitive refusal from the server ends this now rather than at the
    // deadline -- the lesson the forge taught 311 times.
    if (client.ActionKind() == act::Kind::Equip) {
        const act::Result res = client.ActionResult();
        if (res == act::Result::Rejected ||
            res == act::Result::InvalidState ||
            res == act::Result::ServerFailure)
            ask_.Note(Outcome::Refused, obs.nowMs, act::ResultName(res));
    }
    if (ask_.Expired(obs.nowMs)) ask_.NoteExpiry(obs.nowMs);

    const u32 carried = client.FindBackpackItemByGraphic(graphic_);
    if (!carried) {
        running_ = false;
        // Not in the pack and not on the layer: somebody else moved it, or it
        // was never there. Retryable rather than fatal -- the caller may buy
        // one and come back.
        lastWhy_ = std::string("no ") + what_ + " in the pack to wear";
        return ActivityTickResult::Done(ActivityStatus::RetryableFailure,
                                        lastWhy_.c_str());
    }

    const char* whyNot = "";
    if (!ask_.MayIssue(obs.nowMs, &whyNot)) {
        if (ask_.Exhausted() ||
            ask_.State() == HandshakeState::ConfirmedFailure) {
            running_ = false;
            const char* said = ask_.Refusal();
            lastWhy_ = std::string("the server would not put ") + what_ +
                       " on the paperdoll" +
                       ((said && said[0]) ? std::string(": ") + said
                                          : std::string());
            // BLOCKED, not Failed: the refusal is usually about the character
            // rather than the item -- too weak, wrong hand, already holding
            // something -- and that changes.
            return ActivityTickResult::Done(ActivityStatus::Blocked,
                                            lastWhy_.c_str());
        }
        return ActivityTickResult::Waiting(Wake::ActionResolves, 0, whyNot);
    }

    client.ActionEquip(carried, layer_);
    ask_.NoteIssued(obs.nowMs);
    lastWhy_ = std::string("putting ") + what_ + " on";
    return ActivityTickResult::Waiting(Wake::ActionResolves, 0,
                                       lastWhy_.c_str());
}

// ---------------------------------------------------------------------------
// AcquireItemActivity
// ---------------------------------------------------------------------------

void AcquireItemActivity::Begin(const AcquireRequest& req) {
    req_ = req;
    running_ = true;
    buy_.Cancel();
    equip_.Cancel();
}

void AcquireItemActivity::Cancel() {
    running_ = false;
    buy_.Cancel();
    equip_.Cancel();
}

ActivityTickResult AcquireItemActivity::Tick(Client& client,
                                             const Observation& obs) {
    if (!running_)
        return ActivityTickResult::Done(ActivityStatus::Failed,
                                        "the request was never begun");

    // Decide from the WORLD every tick, not from what was last attempted.
    // A piece bought two ticks ago is in the pack now, and the plan should
    // say "wear it" rather than "buy one" -- which is exactly the transition
    // the old two-loop version could not make.
    const i32 held = static_cast<i32>(client.BackpackItemCount(req_.graphic));
    const u16 worn = req_.layer ? client.EquippedGraphicAt(req_.layer) : 0;
    const AcquirePlan plan = DecideAcquire(req_, held, worn);

    switch (plan.step) {
        case AcquireStep::Done:
            running_ = false;
            return ActivityTickResult::Done(ActivityStatus::Success,
                                            plan.reason);

        case AcquireStep::Refuse:
            // The thing this file exists for: gold is NOT spent on something
            // that can only sit in the pack.
            running_ = false;
            return ActivityTickResult::Done(ActivityStatus::Blocked,
                                            plan.reason);

        case AcquireStep::Wear: {
            if (!equip_.Running())
                equip_.Begin(req_.graphic, req_.layer, req_.item);
            const ActivityTickResult r = equip_.Tick(client, obs);
            // Wearing is the LAST step, so its verdict is the request's.
            if (IsTerminal(r.status)) running_ = false;
            return r;
        }

        case AcquireStep::Buy: {
            if (!buy_.Running()) {
                BuyRequest br;
                br.graphic = req_.graphic;
                br.item = req_.item;
                br.desiredTotal = req_.desiredTotal;
                br.minimumGoldReserve = req_.minimumGoldReserve;
                br.maxPricePerUnit = req_.maxPricePerUnit;
                for (i32 i = 0; i < req_.sellerCount; ++i)
                    br.Sell(req_.sellers[i].trade, req_.sellers[i].service);
                buy_.Begin(br);
            }
            ActivityTickResult r = buy_.Tick(client, obs);
            // A SUCCESSFUL BUY IS NOT A FINISHED REQUEST when the thing is
            // meant to be worn. Fall through to the wear step next tick
            // rather than reporting done with the gorget still in the pack.
            if (r.status == ActivityStatus::Success && req_.mustWear) {
                lastWhy_ = "bought; now to put it on";
                return ActivityTickResult::Waiting(Wake::Now, 0,
                                                   lastWhy_.c_str());
            }
            if (IsTerminal(r.status)) running_ = false;
            return r;
        }
    }

    running_ = false;
    return ActivityTickResult::Done(ActivityStatus::Failed,
                                    "unreachable acquire state");
}

}  // namespace uo::life
