#include "life/runner/RunnerInternal.h"
#include "uo/party_hunt.h"

// HUNTING AS A PARTY (uo/party_hunt.h): the leader calls the target aloud,
// everyone hits it, members patch each other up while they fight, kills are
// looted in turn, and each member does its own job -- the melee holds the
// monster, the archer shoots from range, the caster heals first.

namespace uo::life {
using namespace runner_detail;

bool Runner::InHuntingParty(Client& client) const {
    return socialGroupUntilMs_ && socialActivity_ == social::Activity::Hunt && socialPeer_ &&
           client.PartyContains(socialPeer_);
}

party::Role Runner::MyPartyRole() const {
    if (!needCfg_.profession) return party::Role::Tank;
    const auto s = needCfg_.profession->combatStrategy;
    return party::RoleFor(s == CombatStrategyId::Mage ? 2 : s == CombatStrategyId::Ranged ? 1 : 0);
}

// The leader says which monster it is on, once per new target.
void Runner::CallFocus(Client& client, i64 nowMs, u32 target, const std::string& name) {
    if (!InHuntingParty(client) || client.PartyLeader() != client.PlayerSerial()) return;
    if (target == focusCalledSerial_ || name.empty() || nowMs - focusCalledMs_ < 4000) return;
    focusCalledSerial_ = target;
    focusCalledMs_ = nowMs;
    const std::string call = party::FocusCall(name);
    client.ActionSay(call.c_str());
    LogLine("party: called the target -- %s", name.c_str());
}

// A follower's pick among the hostiles it can see: the called monster nearest
// the leader, or whatever the leader is fighting. -1 = no party preference.
int Runner::PartyFocusIndex(Client& client, const Observation& obs,
                            const std::vector<party::Seen>& hostiles) const {
    if (!InHuntingParty(client) || client.PartyLeader() == client.PlayerSerial()) return -1;
    i32 lx = 0, ly = 0;
    const u32 leader = client.PartyLeader();
    if (!client.MobilePosition(leader, &lx, &ly)) return -1;
    const std::string called = focusUntilMs_ > obs.nowMs ? focusName_ : std::string();
    return party::PickFocus(hostiles, called, lx, ly, client.MobileWarMode(leader));
}

// Heal a friend who needs it more than we need to swing. Returns true when it
// used the tick.
bool Runner::TickPartySupport(Client& client, const Observation& obs) {
    if (!InHuntingParty(client) || obs.dead || client.ActionBusy()) return false;
    if (obs.nowMs - partyHealMs_ < 6000) return false;
    if (obs.nowMs - partyStatusMs_ >= 3000) {
        partyStatusMs_ = obs.nowMs;
        for (u32 m : client.PartyMembers())
            if (m != client.PlayerSerial()) client.RequestMobileStatus(m);
    }
    std::vector<party::Member> members;
    for (u32 m : client.PartyMembers()) {
        if (m == client.PlayerSerial()) continue;
        i32 x = 0, y = 0;
        if (!client.MobilePosition(m, &x, &y)) continue;
        party::Member pm;
        pm.serial = m;
        pm.hpPct = client.MobileHpPercent(m);
        pm.dist = TileDist(obs.x, obs.y, x, y);
        members.push_back(pm);
    }
    const party::Role role = MyPartyRole();
    const bool canCast = role == party::Role::Healer && obs.SkillTenths(rules::kMagery) >= 400 &&
                         obs.mana >= 11 && (!obs.SpellbookRead() || obs.KnowsSpell(29));
    const u32 who = party::HealTarget(members, role, obs.bandages > 0, canCast);
    if (!who) return false;
    partyHealMs_ = obs.nowMs;
    const party::Member* m = nullptr;
    for (const auto& pm : members) if (pm.serial == who) m = &pm;
    if (canCast && m && m->hpPct < party::kSpellHealBelow) {
        client.ActionCastSpell(29, who);           // Greater Heal on the friend
        LogLine("party: greater heal on a member at %d%%", m->hpPct);
    } else {
        const u32 bandage = client.FindBackpackItemByGraphic(0x0E21);
        if (!bandage) return false;
        client.ActionUseBandage(bandage, who);
        LogLine("party: bandaging a member at %d%%", m ? m->hpPct : -1);
    }
    social::Remember(state_.memory.relationships, socialPeerName_, social::Encounter::Help, obs.nowMs);
    return true;
}

// Whose turn is this kill? Counted the same way by every member.
bool Runner::PartyLootTurn(Client& client) {
    if (!InHuntingParty(client)) return true;
    const auto& roster = client.PartyMembers();
    int me = 0;
    for (usize i = 0; i < roster.size(); ++i) if (roster[i] == client.PlayerSerial()) me = static_cast<int>(i);
    const bool mine = party::MyLootTurn(partyKills_, me, static_cast<int>(roster.size()));
    ++partyKills_;
    return mine;
}

}  // namespace uo::life
