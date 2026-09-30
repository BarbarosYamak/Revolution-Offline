#include "life/runner/RunnerInternal.h"
#include "uo/housing.h"
#include "uo/persona.h"

// HOUSES (uo/housing.h). A character that has saved enough buys a deed from
// an architect, walks out of town and uses it; the server's 0x99 placement
// cursor is answered with the spot, and a house multi appearing there is the
// only proof it stands. Refused spots are skipped in a fixed, per-character
// order. If no architect sells a deed -- which is this tree's state today --
// the need blocks for the day and nothing is faked.

namespace uo::life {
using namespace runner_detail;

bool Runner::OwnsHouse() const {
    for (const KnownPlace& p : state_.memory.Places())
        if (p.kind == "house") return true;
    return false;
}

void Runner::AddHousingNeeds(Client& client, const Observation& obs, std::vector<Need>& needs) {
    if (obs.dead || obs.underAttack || obs.attackersOnMe > 0 || !needCfg_.profession) return;
    housing::Wealth w;
    w.totalGold = obs.gold;
    w.ownsHouse = OwnsHouse();
    w.deedInPack = client.FindBackpackItemByGraphic(housing::kDeedGraphic) != 0;
    if (!housing::WantsHouse(w)) return;
    Need n;
    n.kind = NeedKind::NeedHousing;
    n.what = "a house";
    n.urgency = w.deedInPack ? 0.70 : 0.35;
    n.reason = w.deedInPack ? "a house deed waiting to be placed" : "savings enough to look for a house";
    needs.push_back(n);
}

bool Runner::DoBuyHouse(Client& client, const Observation& obs) {
    const u32 deed = client.FindBackpackItemByGraphic(housing::kDeedGraphic);
    if (houseSites_.empty()) {
        const HomeReturn home = ResolveHomeReturn(client.WorldAtlas(), state_.homeCity, obs.x, obs.y);
        const i32 hx = home.resolved ? home.x : obs.x, hy = home.resolved ? home.y : obs.y;
        for (const housing::Site& s : housing::Candidates(hx, hy, persona::Fnv1a(state_.identity.identityId)))
            houseSites_.push_back({s.x, s.y});
    }
    const bool haveSite = houseSite_ < static_cast<int>(houseSites_.size());
    const i32 sx = haveSite ? houseSites_[houseSite_].first : 0;
    const i32 sy = haveSite ? houseSites_[houseSite_].second : 0;

    housing::Sight s;
    s.ownsHouse = OwnsHouse();
    s.deedInPack = deed != 0;
    s.deedBudget = housing::DeedBudget(obs.gold);
    s.deedErrandFailed = houseDeedFailed_;
    s.siteIndex = houseSite_;
    s.siteCount = static_cast<int>(houseSites_.size());
    s.tilesToSite = haveSite ? TileDist(obs.x, obs.y, sx, sy) : 0;
    s.cursorActive = client.MultiCursorActive();
    s.placedHere = haveSite && houseTriedMs_ && client.FindMultiNear(sx, sy, 12, houseTriedMs_) != 0;
    s.triesHere = houseTriesHere_;
    // A placement answer waits a few seconds for the server's multi before
    // the spot counts as refused.
    s.busy = client.TravelBusy() || (client.ActionBusy() && !s.cursorActive) ||
             (!s.cursorActive && houseTriesHere_ > 0 && !s.placedHere && obs.nowMs - houseTriedMs_ < 8000);

    const housing::Plan plan = housing::Decide(s);
    if (plan.step != housing::Step::Wait)
        LogLine("house: %s (%s)", housing::StepName(plan.step), plan.reason);
    switch (plan.step) {
        case housing::Step::Wait:
            if (houseErrand_.Running()) break;   // the errand's own tick below
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        case housing::Step::BuyDeed: break;
        case housing::Step::Travel:
            if (!client.TravelToPoint(sx, sy, 1, "house site")) {
                ++houseSite_; houseTriesHere_ = 0;
            }
            planner_.NoteProgress();
            return false;
        case housing::Step::UseDeed:
            ++houseTriesHere_;
            houseTriedMs_ = obs.nowMs;
            client.ActionUseObject(deed);
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        case housing::Step::Place:
            client.ActionPlaceMulti(sx, sy, static_cast<i8>(client.PlayerZ()));
            houseTriedMs_ = obs.nowMs;
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        case housing::Step::NextSite:
            LogLine("house: the spot %d,%d was refused", sx, sy);
            ++houseSite_; houseTriesHere_ = 0; houseTriedMs_ = 0;
            return false;
        case housing::Step::Done:
            if (!OwnsHouse()) {
                state_.memory.NotePlace("house", "my house", sx, sy, static_cast<i8>(client.PlayerZ()), obs.nowMs);
                LogLine("house: placed at %d,%d", sx, sy);
                Checkpoint(client, obs.nowMs, "house placed");
            }
            return true;
        case housing::Step::GiveUp:
            return BlockNeed(GoalKind::BuyHouse, NeedKind::NeedHousing, BlockScope::Session,
                             plan.reason, 12 * 60 * 60000, obs.nowMs);
    }

    // Buying the deed: coin first, then an architect (a Carpenter in the atlas).
    if (FetchCoinForPurchase(client, obs, std::min(s.deedBudget, 60000))) return false;
    if (!houseErrand_.Running()) {
        life::VendorErrandSpec spec;
        spec.Sell("architect", wm::Service::Carpenter);
        spec.graphic = housing::kDeedGraphic;
        spec.qty = 1;
        spec.what = "house deed";
        spec.maxPricePerUnit = s.deedBudget;
        spec.goldFloor = housing::kKeepAfterHouse;
        houseErrand_.Begin(spec);
    }
    const life::VendorErrandResult r = houseErrand_.Tick(client, obs);
    LogErrandReason("house deed", r.why.c_str(), obs.nowMs);
    if (r.wake == life::Wake::AfterDelay && r.delayMs > 0) nextActionMs_ = obs.nowMs + r.delayMs;
    if (life::IsTerminal(r.status)) {
        houseErrand_.Cancel();
        if (r.status != life::ActivityStatus::Success) {
            houseDeedFailed_ = true;
            LogLine("house: no deed bought (%s)", r.why.c_str());
        }
    }
    if (r.acted) planner_.NoteProgress();
    return false;
}

}  // namespace uo::life
