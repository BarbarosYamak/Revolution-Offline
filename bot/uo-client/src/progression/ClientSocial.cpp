#include "Client.h"
#include "uo/builders.h"
#include "uo/sparring.h"
#include <algorithm>
#include <cstdlib>

namespace uo {

void Client::ActionIdentifyNearbyPerson() {
    // One unknown human per call, not a fleet-wide census. Never double-click
    // animals: on this shard that can mount them rather than inspect them.
    for (const auto& m : mobileCache_) {
        if (m.serial == playerSerial_ || m.deadRemoveMs ||
            (m.body != 0x0190 && m.body != 0x0191) || PaperdollTitle(m.serial) ||
            std::max(std::abs(m.x - playerX_), std::abs(m.y - playerY_)) > 8) continue;
        u8 packet[8];
        Send(packet, build::MobNameQuery(packet, m.serial), "social name query");
        SendDoubleClick(m.serial);
        break;
    }
}

void Client::NearbyPlayers(i32 radius, std::vector<HostileHit>& out) const {
    out.clear();
    for (const auto& m : mobileCache_) {
        if (m.serial == playerSerial_ || m.deadRemoveMs || !KnownPlayer(m.serial) ||
            std::max(std::abs(m.x - playerX_), std::abs(m.y - playerY_)) > radius) continue;
        HostileHit p;
        p.serial = m.serial;
        const char* name = MobileName(m.serial);
        p.name = name ? name : "";
        p.x = m.x; p.y = m.y; p.z = m.z; p.noto = m.noto;
        p.warMode = m.warMode; p.hpCur = m.hpCur; p.hpMax = m.hpMax;
        out.push_back(p);
    }
}

bool Client::PartyContains(u32 serial) const {
    return std::find(partyMembers_.begin(), partyMembers_.end(), serial) != partyMembers_.end();
}

// Wire layouts verified in Source-X network/send.cpp PacketPartyList,
// PacketPartyRemoveMember and PacketPartyInvite. Other 0xBF subcommands
// remain untouched; malformed updates cannot grant membership.
void Client::OnPartyPacket(const u8* data, usize size) {
    if (size < 6 || data[3] != 0 || data[4] != 6) return;
    const auto serialAt = [&](usize at) -> u32 {
        return (static_cast<u32>(data[at]) << 24) | (static_cast<u32>(data[at+1]) << 16) |
               (static_cast<u32>(data[at+2]) << 8) | data[at+3];
    };
    if (data[5] == 7 && size == 10) {
        partyInviter_ = serialAt(6);
        return;
    }
    if (data[5] != 1 && data[5] != 2) return;
    if (size < 7) return;
    const usize count = data[6];
    const usize offset = data[5] == 2 ? 11 : 7;
    if (count > 10 || size != offset + count * 4) return;
    std::vector<u32> members;
    for (usize i = 0; i < count; ++i) {
        const u32 serial = serialAt(offset + i * 4);
        if (!serial || std::find(members.begin(), members.end(), serial) != members.end()) return;
        members.push_back(serial);
    }
    if (std::find(members.begin(), members.end(), playerSerial_) == members.end()) members.clear();
    partyMembers_ = std::move(members);
    partyInviter_ = 0;
}

void Client::ActionPartyInvite() {
    const u8 packet[] = {0xBF, 0, 6, 0, 6, 1};
    Send(packet, sizeof(packet), "party invite cursor");
}

void Client::ActionPartyAccept(u32 leader) {
    if (!leader || partyInviter_ != leader) return;
    const u8 packet[] = {0xBF, 0, 10, 0, 6, 8, static_cast<u8>(leader >> 24),
        static_cast<u8>(leader >> 16), static_cast<u8>(leader >> 8), static_cast<u8>(leader)};
    Send(packet, sizeof(packet), "party accept");
    partyInviter_ = 0;
}

void Client::ActionPartyLeave() {
    if (!PartyContains(playerSerial_)) return;
    const u32 serial = playerSerial_;
    const u8 packet[] = {0xBF, 0, 10, 0, 6, 2, static_cast<u8>(serial >> 24),
        static_cast<u8>(serial >> 16), static_cast<u8>(serial >> 8), static_cast<u8>(serial)};
    Send(packet, sizeof(packet), "party leave");
}


sparring::Kit Client::SparringKitOf(u32 serial) const {
    const auto* mobile = FindMobileBySerial(serial);
    if (serial != playerSerial_ && !mobile) return sparring::Kit::None;
    const auto& equipment = serial == playerSerial_ ? playerEquip_ : mobile->equip;
    std::vector<sparring::Worn> worn;
    worn.reserve(equipment.size());
    for (const auto& e : equipment) worn.push_back({e.layer, e.graphic, e.hue});
    return sparring::KitFor(worn.data(), worn.size());
}

u16 Client::MobileEquipGraphic(u32 serial, u8 layer) const {
    const auto* mobile = FindMobileBySerial(serial);
    if (serial != playerSerial_ && !mobile) return 0;
    const auto& equipment = serial == playerSerial_ ? playerEquip_ : mobile->equip;
    for (const auto& e : equipment) if (e.layer == layer) return e.graphic;
    return 0;
}

bool Client::SparringHealthFresh(u32 peer) const {
    const auto* m = FindMobileBySerial(peer);
    const i64 now = NowMs();
    return m && selfHealthSeenMs_ > 0 && now-selfHealthSeenMs_ <= sparring::kHealthFreshMs &&
           m->healthSeenMs > 0 && now-m->healthSeenMs <= sparring::kHealthFreshMs;
}

bool Client::SparringKit(u32 serial) const {
    return SparringKitOf(serial) != sparring::Kit::None;
}

bool Client::SparringExternalThreat(u32 peer) const {
    for (const auto& entry : attackersOnMe_)
        if (entry.first != peer && IsAttackingMe(entry.first)) return true;
    std::vector<HostileHit> threats;
    ScanHostiles(6, threats);
    for (const auto& threat : threats) if (threat.serial != peer) return true;
    return false;
}

bool Client::SparringReady(u32 peer) const {
    const auto* m = FindMobileBySerial(peer);
    const i64 now = NowMs();
    return peer && peer != playerSerial_ && m && KnownPlayer(peer) && !IsDead() &&
        PartyContains(playerSerial_) && PartyContains(peer) && PartySize() <= 3 &&
        std::max(std::abs(m->x-playerX_), std::abs(m->y-playerY_)) <= 2 &&
        std::abs(m->z-playerZ_) <= 4 &&
        sparring::Compatible(SparringKitOf(playerSerial_), SparringKitOf(peer)) &&
        selfHealthSeenMs_ > 0 && now-selfHealthSeenMs_ <= sparring::kHealthFreshMs &&
        m->healthSeenMs > 0 && now-m->healthSeenMs <= sparring::kHealthFreshMs &&
        sparring::HealthAllows(player_.hpCur, player_.hpMax, sparring::kStartPercent) &&
        sparring::HealthAllows(m->hpCur, m->hpMax, sparring::kStartPercent) && !SparringExternalThreat(peer);
}

bool Client::MobilePoisoned(u32 serial) const {
    if (serial == playerSerial_) return player_.poisoned;
    const auto* m = FindMobileBySerial(serial);
    return m && m->poisoned;
}

bool Client::PoisonPracticeReady(u32 peer) const {
    const auto* m = FindMobileBySerial(peer);
    const i64 now = NowMs();
    return peer && peer != playerSerial_ && m && KnownPlayer(peer) && !IsDead() &&
        PartyContains(playerSerial_) && PartyContains(peer) && PartySize() == 2 &&
        std::max(std::abs(m->x-playerX_), std::abs(m->y-playerY_)) <= 2 &&
        std::abs(m->z-playerZ_) <= 4 && !MobilePoisoned(peer) && !MobilePoisoned(playerSerial_) &&
        selfHealthSeenMs_ > 0 && now-selfHealthSeenMs_ <= sparring::kHealthFreshMs &&
        m->healthSeenMs > 0 && now-m->healthSeenMs <= sparring::kHealthFreshMs &&
        sparring::HealthAllows(player_.hpCur, player_.hpMax, sparring::kStartPercent) &&
        sparring::HealthAllows(m->hpCur, m->hpMax, sparring::kStartPercent) && !SparringExternalThreat(peer);
}

bool Client::PrepareSparringRound(u32 peer) {
    if (ActionBusy() || (sparringPeer_ && sparringPeer_ != peer) || !SparringReady(peer)) return false;
    sparringPeer_ = peer; sparringUntilMs_ = NowMs() + sparring::kRoundMs;
    return true;
}

bool Client::BeginSparringRound(u32 peer) {
    if ((sparringPeer_ && sparringPeer_ != peer) || ActionBusy() || !SparringReady(peer)) return false;
    sparringPeer_ = peer;
    sparringUntilMs_ = NowMs() + sparring::kRoundMs;
    ActionAttack(peer);
    return true;
}

void Client::StopSparring(const char* reason) {
    if (!sparringPeer_) return;
    attackersOnMe_.erase(sparringPeer_);
    sparringPeer_ = 0; sparringUntilMs_ = 0;
    war_.OnPeacefulIntent(NowMs());
    SetWarMode(false); // Always send: attack/war acknowledgements can lag behind HP.
    war_.NoteExitRequested(NowMs());
    if (action_.kind == act::Kind::Attack) FinishAction(act::Result::InvalidState, reason);
}

void Client::SparringSafetyTick() {
    if (!sparringPeer_) return;
    const auto* m = FindMobileBySerial(sparringPeer_);
    const i64 now = NowMs();
    if (!m || IsDead() || !PartyContains(sparringPeer_) || !PartyContains(playerSerial_) ||
        PartySize() > 3 || !sparring::Compatible(SparringKitOf(playerSerial_), SparringKitOf(sparringPeer_))) {
        StopSparring("sparring safety stop");
        return;
    }
    const bool fresh = now-selfHealthSeenMs_ <= sparring::kHealthFreshMs &&
                       now-m->healthSeenMs <= sparring::kHealthFreshMs;
    const bool inRange = std::max(std::abs(m->x-playerX_), std::abs(m->y-playerY_)) <= 2 &&
                         std::abs(m->z-playerZ_) <= 4;
    const i32 selfPct = player_.hpMax > 0 ? static_cast<i32>(static_cast<i64>(player_.hpCur) * 100 / player_.hpMax) : -1;
    const i32 peerPct = m->hpMax > 0 ? static_cast<i32>(static_cast<i64>(m->hpCur) * 100 / m->hpMax) : -1;
    if (sparring::StopRound(selfPct, peerPct, fresh, SparringExternalThreat(sparringPeer_), inRange,
                            now >= sparringUntilMs_))
        StopSparring("sparring safety stop");
}

} // namespace uo
