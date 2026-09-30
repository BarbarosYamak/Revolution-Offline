#include "life/runner/RunnerInternal.h"
#include "uo/pvp.h"

// PLAYER VERSUS PLAYER (uo/pvp.h). Two roles:
//   * the PK (the "pk" profession): roams unguarded work grounds -- mines,
//     woods -- and ambushes a lone worker when the opening is good enough
//     for its nerve. Ambush AI only (owner ruling); no Head Hunters.
//   * the anti-PK (a hunting fighter with nerve): engages reds, criminals and
//     guild-war enemies it sees, and answers a "PK var! yardim!" alarm.
// Both open the fight the way TRAIN_COMBAT does (attack + close in); once the
// target fights back SURVIVE owns the exchange, with its per-fight retreat
// judgement. The server decides every consequence: criminal and murder flags,
// guards, looting as a crime.

namespace uo::life {
using namespace runner_detail;

static bool PvpAttackingInnocent(Client& client, const Client::HostileHit& p,
                                 const std::vector<Client::HostileHit>& players);

pvp::Role Runner::PvpRole() const {
    if (!needCfg_.profession || cfg_.noPvp) return pvp::Role::None;
    if (needCfg_.profession->id == "pk") return pvp::Role::PlayerKiller;
    if (WantsToHunt(*needCfg_.profession) && Nerve(needCfg_) >= 0.55) return pvp::Role::AntiPk;
    return pvp::Role::None;
}

pvp::Self Runner::PvpSelf(Client& client, const Observation& obs) const {
    pvp::Self me;
    me.hpFrac = obs.HpFraction();
    me.bandages = obs.bandages;
    me.nerve = Nerve(needCfg_);
    me.guardedHere = client.CurrentRegion() && client.CurrentRegion()->flags.guarded;
    return me;
}

// Build what this character can SEE about every player near it.
std::vector<pvp::Target> Runner::PvpTargets(Client& client, const Observation& obs) const {
    std::vector<Client::HostileHit> players;
    client.NearbyPlayers(pvp::kMaxEngageTiles + 4, players);
    std::vector<pvp::Target> out;
    for (const auto& p : players) {
        if (p.serial == client.PlayerSerial() || p.name.empty()) continue;
        pvp::Target t;
        t.serial = p.serial;
        t.noto = p.noto;
        t.dist = TileDist(obs.x, obs.y, p.x, p.y);
        t.hpPct = (p.hpCur >= 0 && p.hpMax > 0) ? p.hpCur * 100 / p.hpMax : -1;
        t.hand1 = client.MobileEquipGraphic(p.serial, 1);
        t.hand2 = client.MobileEquipGraphic(p.serial, 2);
        t.mounted = client.MobileEquipGraphic(p.serial, 25) != 0;
        const auto* rel = social::Find(state_.memory.relationships, p.name);
        t.friendOfOurs = (rel && !rel->foe && rel->trust >= 2) || client.PartyContains(p.serial) ||
                         p.noto == 2 ||   // guild-green: our own guild or an ally
                         FamilyMember(p.name) ||  // family stands together (owner, 2026-09-30)
                         GuildMate(client, p.serial, p.noto);   // our own guild tag
        t.beatUsBefore = SocialFoe(p.name) && rel && rel->foe;
        for (const auto& q : players) {
            if (q.serial == p.serial) continue;
            if (TileDist(p.x, p.y, q.x, q.y) <= 8) ++t.playersNearTarget;
            const auto* qr = social::Find(state_.memory.relationships, q.name);
            if (TileDist(obs.x, obs.y, q.x, q.y) <= 8) {
                if ((qr && !qr->foe && qr->trust >= 2) || client.PartyContains(q.serial) || q.noto == 2 ||
                    FamilyMember(q.name) || GuildMate(client, q.serial, q.noto))
                    ++t.myAlliesNear;
                else if (q.noto == 6 || q.noto == 4) ++t.hostilePlayersNear;
            }
        }
        t.attackingInnocent = PvpAttackingInnocent(client, p, players);
        out.push_back(t);
    }
    return out;
}

// Is this red or criminal swinging at a blue player? A bystander sees war
// mode and who stands in reach; IsAttackingMe covers the case where it is us.
static bool PvpAttackingInnocent(Client& client, const Client::HostileHit& p,
                                 const std::vector<Client::HostileHit>& players) {
    if (p.noto != 6 && p.noto != 4) return false;
    if (client.IsAttackingMe(p.serial)) return true;
    if (!p.warMode) return false;
    for (const auto& q : players)
        if (q.serial != p.serial && q.noto == 1 && TileDist(p.x, p.y, q.x, q.y) <= 2) return true;
    return false;
}

void Runner::ObservePvp(Client& client, const Observation& obs) {
    // The victim's shout. Once a minute, only for a real player attacker who
    // is not our sparring partner, and never from the PK itself.
    std::vector<Client::HostileHit> players;
    client.NearbyPlayers(18, players);
    if (PvpRole() != pvp::Role::PlayerKiller && obs.nowMs - pvpAlarmSaidMs_ >= 60000) {
        for (const auto& p : players) {
            if (p.serial == socialPeer_ || !client.IsAttackingMe(p.serial)) continue;
            pvpAlarmSaidMs_ = obs.nowMs;
            client.ActionSay(pvp::kAlarmLine);
            LogLine("pvp: attacked by %s -- calling for help", p.name.c_str());
            break;
        }
    }
    // Hearing someone else's shout.
    const i64 journalNow = client.JournalNowMs();
    if (pvpHeardMs_ == 0 || pvpHeardMs_ < journalNow - 30000) pvpHeardMs_ = journalNow - 30000;
    std::vector<Client::Heard> heard;
    client.JournalHeardSince(pvpHeardMs_, heard);
    if (!heard.empty()) pvpHeardMs_ = heard.back().timeMs;
    for (const auto& h : heard) {
        if (h.speaker == client.PlayerSerial() || !h.hasPosition || !pvp::IsAlarm(h.text)) continue;
        if (TileDist(obs.x, obs.y, h.x, h.y) > 18) continue;
        pvpAlarmUntilMs_ = obs.nowMs + 60000;
        pvpAlarmX_ = h.x; pvpAlarmY_ = h.y;
        LogLine("pvp: heard '%s' from %s", h.text.c_str(), h.name.c_str());
    }
}

void Runner::AddPvpNeeds(Client& client, const Observation& obs, std::vector<Need>& needs) {
    const pvp::Role role = PvpRole();
    if (role == pvp::Role::None || obs.dead || obs.underAttack || obs.attackersOnMe > 0) return;
    const pvp::Self me = PvpSelf(client, obs);
    if (me.hpFrac < pvp::kMinHpToStart || me.bandages < pvp::kMinBandages) return;
    Need n;
    n.kind = NeedKind::NeedPvp;
    const auto targets = PvpTargets(client, obs);
    bool opening = false;
    for (const auto& t : targets)
        opening = opening || (role == pvp::Role::PlayerKiller ? pvp::ShouldAmbush(t, me) : pvp::ShouldHunt(t, me));
    if (role == pvp::Role::PlayerKiller) {
        n.what = "a lone victim";
        n.urgency = opening ? 0.90 : 0.45;
        n.reason = opening ? "a good opening in sight" : "roam the work grounds outside the guards";
    } else {
        const bool alarm = pvpAlarmUntilMs_ > obs.nowMs;
        if (!opening && !alarm) return;
        n.what = "a red, criminal or war enemy";
        n.urgency = opening ? 0.85 : 0.75;
        n.reason = opening ? "a lawful target in sight" : "someone nearby shouted PK";
    }
    needs.push_back(n);
}

bool Runner::DoHuntPlayers(Client& client, const Observation& obs) {
    const pvp::Role role = PvpRole();
    if (role == pvp::Role::None) return true;
    const pvp::Self me = PvpSelf(client, obs);
    if (me.hpFrac < pvp::kMinHpToStart || me.bandages < pvp::kMinBandages)
        return HandOff(GoalKind::HuntPlayers, GoalKind::Heal, 60000, "patch up before any fight", obs.nowMs);

    const pvp::Target* best = nullptr;
    double bestScore = 0.0;
    const auto targets = PvpTargets(client, obs);
    for (const auto& t : targets) {
        const bool go = role == pvp::Role::PlayerKiller ? pvp::ShouldAmbush(t, me) : pvp::ShouldHunt(t, me);
        const double s = role == pvp::Role::PlayerKiller ? pvp::VictimScore(t, me) : pvp::HuntScore(t, me);
        if (go && s > bestScore) { best = &t; bestScore = s; }
    }
    if (best) {
        std::string name;
        std::vector<Client::HostileHit> players;
        client.NearbyPlayers(pvp::kMaxEngageTiles + 4, players);
        for (const auto& p : players) if (p.serial == best->serial) name = p.name;
        LogLine("pvp: %s engages %s (noto %u, score %.2f)", pvp::RoleName(role), name.c_str(),
                best->noto, bestScore);
        client.ActionAttack(best->serial);
        if (best->dist > 1 && !client.GotoBusy()) client.ActionGotoMobile(best->serial, 1);
        currentFoe_ = best->serial;
        currentFoeName_ = name;
        fightStartedMs_ = obs.nowMs;
        chaseBestDist_ = best->dist;
        chaseProgressMs_ = obs.nowMs;
        client.RequestMobileStatus(best->serial);
        state_.memory.NoteEvent(role == pvp::Role::PlayerKiller ? "pk_ambush" : "pk_hunt",
                                name.c_str(), "engaged", obs.x, obs.y, obs.nowMs);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    if (role == pvp::Role::AntiPk) {
        if (pvpAlarmUntilMs_ > obs.nowMs && TileDist(obs.x, obs.y, pvpAlarmX_, pvpAlarmY_) > 3) {
            if (!client.TravelBusy()) client.TravelToPoint(pvpAlarmX_, pvpAlarmY_, 2, "answer a PK alarm");
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        }
        return true;   // nothing lawful to fight here any more
    }
    // The PK with no opening: go where people work alone, outside the guards.
    if (!client.TravelBusy()) {
        if (me.guardedHere || obs.nowMs - pvpRoamMs_ > 180000) {
            pvpRoamMs_ = obs.nowMs;
            const wm::ResourceKind ground = (pvpRoamFlip_ = !pvpRoamFlip_) ? wm::ResourceKind::Mining
                                                                            : wm::ResourceKind::Lumber;
            if (!client.TravelToResource(ground))
                return BlockNeed(GoalKind::HuntPlayers, NeedKind::NeedPvp, BlockScope::Window,
                                 "no work ground known to lie in wait at", 20 * 60000, obs.nowMs);
            LogLine("pvp: roaming to the %s grounds", ground == wm::ResourceKind::Mining ? "mining" : "lumber");
        }
    }
    nextActionMs_ = obs.nowMs + 2000;
    return false;
}

}  // namespace uo::life
