// ---------------------------------------------------------------------------
// Client progression accessors (M3).
//
// Reading what the server says about this character's skills, stats and
// carrying capacity. Every value here comes off the wire (0x3A skills, 0x11
// status) and none of it is ever written locally: a bot's skill goes up when
// Sphere says it went up, or not at all.
// ---------------------------------------------------------------------------

#include "Client.h"

#include <algorithm>

namespace uo {

bool Client::PlayerSkillInfo(u16 index, SkillReport* out) const {
    // 0x3A ids are 1-based; index is the [SKILL n] number the scripts use.
    const auto it = player_.skills.find(static_cast<u16>(index + 1));
    if (it == player_.skills.end()) return false;
    if (out) {
        out->wireId = it->second.id;
        out->index = static_cast<u16>(it->second.id - 1);
        out->valueTenths = it->second.valueTenths;
        out->baseTenths = it->second.baseTenths;
        out->capTenths = it->second.hasCap ? it->second.capTenths : 0;
        out->lock = it->second.lock;
    }
    return true;
}

i32 Client::PlayerSkillBase(u16 index) const {
    SkillReport r{};
    return PlayerSkillInfo(index, &r) ? static_cast<i32>(r.baseTenths) : -1;
}

void Client::PlayerSkillsAll(std::vector<SkillReport>& out) const {
    out.clear();
    out.reserve(player_.skills.size());
    for (const auto& kv : player_.skills) {
        if (kv.second.id == 0) continue;
        SkillReport r{};
        r.wireId = kv.second.id;
        r.index = static_cast<u16>(kv.second.id - 1);
        r.valueTenths = kv.second.valueTenths;
        r.baseTenths = kv.second.baseTenths;
        r.capTenths = kv.second.hasCap ? kv.second.capTenths : 0;
        r.lock = kv.second.lock;
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(),
              [](const SkillReport& a, const SkillReport& b) {
                  return a.index < b.index;
              });
}

u32 Client::PlayerSkillSum() const {
    u32 sum = 0;
    for (const auto& kv : player_.skills) {
        if (kv.second.id == 0) continue;
        sum += kv.second.baseTenths;
    }
    return sum;
}

u32 Client::FindItemByGraphic(u16 graphic, bool includeEquipped) const {
    unsigned zones = kZoneBackpack;
    if (includeEquipped) zones |= kZoneEquip;
    return FindItem(/*hasType=*/true, graphic, std::string(), zones, nullptr,
                    nullptr, nullptr);
}

i32 Client::PlayerStatSum() const {
    if (player_.strength < 0 || player_.dexterity < 0 ||
        player_.intelligence < 0)
        return -1;
    return player_.strength + player_.dexterity + player_.intelligence;
}

} // namespace uo
