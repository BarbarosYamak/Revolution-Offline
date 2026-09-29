#include "life/runner/RunnerInternal.h"
#include "uo/treasure.h"

// TREASURE HUNTING (uo/treasure.h). A map in the pack becomes a need; the
// handler walks the pure step machine: decode -> travel -> dig -> open /
// pick -> (guardians) -> loot. Every step is ordinary player input (a
// double-click, a target cursor, a lift) and every result is read back from
// state the server sent, never assumed.

namespace uo::life {
using namespace runner_detail;

namespace {
constexpr usize kMapN = sizeof(treasure::kMapGraphics) / sizeof(treasure::kMapGraphics[0]);
constexpr usize kDigN = sizeof(treasure::kDigTools) / sizeof(treasure::kDigTools[0]);
constexpr usize kPickN = sizeof(treasure::kLockpicks) / sizeof(treasure::kLockpicks[0]);
constexpr usize kChestN = sizeof(treasure::kChestGraphics) / sizeof(treasure::kChestGraphics[0]);
}  // namespace

bool Runner::WantsTreasure() const {
    if (!needCfg_.profession) return false;
    if (needCfg_.profession->id == "treasure_hunter") return true;
    for (const auto& t : state_.plan.skills)
        if (t.skillId == rules::kCartography && t.tenths >= 400) return true;
    return false;
}

void Runner::AddTreasureNeeds(Client& client, const Observation& obs, std::vector<Need>& needs) {
    if (!WantsTreasure() || obs.dead) return;
    const u32 map = FindAny(client, treasure::kMapGraphics, kMapN);
    if (!map && !treasureChest_) return;
    Need n;
    n.kind = NeedKind::NeedTreasure;
    n.what = "treasure map";
    n.urgency = treasureChest_ ? 1.0 : 0.60;
    n.reason = treasureChest_ ? "a dug chest is waiting" : "a treasure map in the pack";
    if (obs.HpFraction() < treasure::kMinHpToDig && !treasureChest_) {
        n.blocked = true;
        n.reason = "heal before facing guardians";
    }
    needs.push_back(n);
}

void Runner::EndTreasure(const char* why) {
    if (treasureMap_ || treasureChest_) LogLine("treasure: done (%s)", why);
    treasureMap_ = treasureChest_ = 0;
    treasurePointKnown_ = false;
    treasureX_ = treasureY_ = 0;
    treasureDecodeTries_ = treasureDigTries_ = treasureOpenTries_ = treasurePickTries_ = 0;
    treasureCursorPending_ = 0;
    treasureLastMs_ = 0;
}

bool Runner::DoHuntTreasure(Client& client, const Observation& obs) {
    const u32 map = FindAny(client, treasure::kMapGraphics, kMapN);
    if (map && map != treasureMap_ && !treasureChest_) {
        EndTreasure("a new map");
        treasureMap_ = map;
    }

    // The map view the server showed us, once decoded: its pin is the spot.
    if (treasureMap_ && !treasurePointKnown_) {
        if (const Client::MapView* v = client.MapViewOf(treasureMap_)) {
            if (!v->pins.empty() &&
                treasure::PinToWorld(v->ulx, v->uly, v->lrx, v->lry, v->width, v->height,
                                     v->pins.front().first, v->pins.front().second,
                                     &treasureX_, &treasureY_)) {
                treasurePointKnown_ = true;
                LogLine("treasure: the map marks %d,%d", treasureX_, treasureY_);
            }
        }
    }

    // A dig raises a chest at the spot; recognise it by being there.
    if (treasurePointKnown_ && !treasureChest_ && treasureDigTries_ > 0) {
        treasureChest_ = client.FindWorldItemNear(treasure::kChestGraphics, kChestN, treasureX_, treasureY_, 3);
        if (treasureChest_) LogLine("treasure: a chest 0x%08X came up", treasureChest_);
    }

    // Answer a cursor our own double-click armed (dig tool / lockpick).
    if (treasureCursorPending_) {
        if (client.TargetActive()) {
            if (treasureCursorPending_ == 1) {
                client.ActionTargetGround(treasureX_, treasureY_, static_cast<i8>(client.PlayerZ()));
            } else {
                client.ActionTargetObject(treasureChest_);
            }
            treasureCursorPending_ = 0;
            nextActionMs_ = obs.nowMs + 4000;
            return false;
        }
        if (obs.nowMs - treasureLastMs_ < 5000) return false;
        treasureCursorPending_ = 0;   // no cursor came; the step counts as tried
    }

    treasure::Sight s;
    s.haveMap = treasureMap_ != 0;
    s.cartographyTenths = obs.SkillTenths(rules::kCartography);
    s.lockpickingTenths = obs.SkillTenths(rules::kLockpicking);
    s.pointKnown = treasurePointKnown_;
    s.tilesToSpot = treasurePointKnown_ ? TileDist(obs.x, obs.y, treasureX_, treasureY_) : 0;
    s.haveDigTool = FindAny(client, treasure::kDigTools, kDigN) != 0 ||
                    std::find(std::begin(treasure::kDigTools), std::end(treasure::kDigTools),
                              client.EquippedGraphicAt(1)) != std::end(treasure::kDigTools) ||
                    std::find(std::begin(treasure::kDigTools), std::end(treasure::kDigTools),
                              client.EquippedGraphicAt(2)) != std::end(treasure::kDigTools);
    s.haveLockpick = FindAny(client, treasure::kLockpicks, kPickN) != 0;
    s.chest = treasureChest_;
    s.chestItemsLeft = treasureChest_ ? static_cast<i32>(client.ContainerItemCount(treasureChest_)) : 0;
    s.chestOpen = treasureChest_ && treasureOpenTries_ > 0 && client.ContainerKnown(treasureChest_);
    s.hostilesNear = obs.hostilesNear > 0 || obs.underAttack || obs.attackersOnMe > 0;
    s.hpFrac = obs.HpFraction();
    s.weightFrac = obs.WeightFraction();
    s.busy = client.ActionBusy() || client.TravelBusy();
    s.decodeTries = treasureDecodeTries_;
    s.digTries = treasureDigTries_;
    s.openTries = treasureOpenTries_;
    s.pickTries = treasurePickTries_;

    const treasure::Plan plan = treasure::Decide(s);
    if (plan.step != treasure::Step::Wait)
        LogLine("treasure: %s (%s)", treasure::StepName(plan.step), plan.reason);
    switch (plan.step) {
        case treasure::Step::Wait:
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        case treasure::Step::Decode:
            ++treasureDecodeTries_;
            client.ActionUseObject(treasureMap_);   // Cartography: the server decodes and shows the map
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        case treasure::Step::Travel:
            if (!client.TravelToPoint(treasureX_, treasureY_, 1, "treasure spot"))
                return BlockNeed(GoalKind::HuntTreasure, NeedKind::NeedTreasure, BlockScope::Window,
                                 "no route to the treasure spot", 20 * 60000, obs.nowMs);
            planner_.NoteProgress();
            return false;
        case treasure::Step::Dig: {
            u32 tool = FindAny(client, treasure::kDigTools, kDigN);
            if (!tool) tool = client.EquippedAtLayer(1);
            ++treasureDigTries_;
            treasureCursorPending_ = 1;
            treasureLastMs_ = obs.nowMs;
            client.ActionUseObject(tool);
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
        case treasure::Step::Open:
            ++treasureOpenTries_;
            client.ActionOpenContainer(treasureChest_);
            nextActionMs_ = obs.nowMs + 3000;
            return false;
        case treasure::Step::Unlock:
            ++treasurePickTries_;
            treasureCursorPending_ = 2;
            treasureLastMs_ = obs.nowMs;
            client.ActionUseObject(FindAny(client, treasure::kLockpicks, kPickN));
            nextActionMs_ = obs.nowMs + 500;
            return false;
        case treasure::Step::Loot: {
            u32 serial = 0; u16 graphic = 0, amount = 0;
            if (client.ContainerItemAt(treasureChest_, 0, &serial, &graphic, &amount)) {
                client.ActionMoveItem(serial, amount ? amount : 1, client.BackpackSerial());
                planner_.NoteProgress();
            }
            nextActionMs_ = obs.nowMs + 800;
            return false;
        }
        case treasure::Step::Done:
            state_.memory.NoteEvent("treasure_found", "chest", plan.reason, obs.x, obs.y, obs.nowMs);
            EndTreasure(plan.reason);
            return true;
        case treasure::Step::GiveUp:
            EndTreasure(plan.reason);
            return BlockNeed(GoalKind::HuntTreasure, NeedKind::NeedTreasure, BlockScope::Window,
                             plan.reason, 30 * 60000, obs.nowMs);
    }
    return false;
}

}  // namespace uo::life
