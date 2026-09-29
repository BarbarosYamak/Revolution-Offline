#pragma once
#include "uo/types.h"

#include <cstddef>

// Consensual party sparring -- the Revolution training culture the owner
// described (PLAYER_MEMORY 2026-09-04, .claude/agent-memory/revolution-god/
// sparring-parties.md): 2-3 players in a party fight each other with gear that
// makes hits land often but hurt little, bandage themselves, and bystanders
// bandage them to practise Healing. Party membership makes it legal on
// Source-X (CCharNotoriety.cpp:169 same party -> NOTO_GUILD_SAME, so no crime
// check and no guards), so it may happen inside town on purpose.
//
// v2 (2026-09-30). v1 never completed a round in any recorded run:
//   * it demanded full iron armour + a training weapon from EVERYONE, casters
//     included, although the owner ruled casters spar bare-handed -- and a
//     spellbook in the hand failed "bare hands", so no mage ever qualified;
//   * every 5-second round needed a numbered spoken "Ready for round N" from
//     both sides, whose counters could drift apart and deadlock;
//   * its 120 s budget started at the INVITATION, so walking to the meeting
//     place and forming the party usually spent it before the first swing;
//   * it stopped at 60% where the owner asked for 40%.
// This file now holds the pure decisions (kit, compatibility, round plan) so
// they are testable without a client; ClientSocial.cpp and runner/Social.cpp
// only gather the facts and carry out the verdict.
namespace uo::sparring {

// Tunables. Conservative training policy, not shard balance changes.
inline constexpr i32 kStartPercent = 80;    // both must be at least this to start a round
inline constexpr i32 kStopPercent = 40;     // owner ruling: stop swinging below 40%
inline constexpr i64 kRoundMs = 20000;      // one round's lease; renewed round by round
inline constexpr i64 kRoundGapMs = 3000;    // breath between rounds (bandage time)
inline constexpr i64 kMeetingMs = 300000;   // whole meeting, counted from PARTY formed
inline constexpr i64 kReadyWaitMs = 45000;  // how long to wait for the partner's "ready"
inline constexpr i64 kHealthFreshMs = 3000;
inline constexpr i32 kMaxRounds = 12;
inline constexpr i32 kMinBandages = 10;

inline bool PoisonReceiver(i32 healing, i32 anatomy, i32 bandages) {
    return healing > 600 && anatomy > 600 && bandages >= 10;
}
inline bool TrainingWeapon(u16 graphic) {
    return graphic == 0 || graphic == 0x0F51 || graphic == 0x0F52 ||
           graphic == 0x13B3 || graphic == 0x13B4;
}
// A spellbook in the hand fights as fists: the caster's wrestling spar.
inline bool Spellbook(u16 graphic) { return graphic == 0x0EFA; }
inline bool IronArmour(u16 graphic) {
    switch (graphic) {
        case 0x1408: case 0x1409: case 0x140A: case 0x140B: case 0x140C: case 0x140D:
        case 0x140E: case 0x140F: case 0x1410: case 0x1411: case 0x1412: case 0x1413:
        case 0x1414: case 0x1415: case 0x13BB: case 0x13BE: case 0x13BF: return true;
        default: return false;
    }
}
inline bool HealthAllows(i32 hp, i32 max, i32 threshold) {
    return max > 0 && hp > 0 && static_cast<i64>(hp) * 100 >= static_cast<i64>(max) * threshold;
}

// ---- kit -------------------------------------------------------------------
struct Worn { u8 layer; u16 graphic; u16 hue; };

enum class Kit : u8 {
    None = 0,     // may not spar in this gear
    Fists,        // bare hands or a spellbook, no shield: wrestling spar, no armour needed
    Weapon,       // a training weapon (dagger/club) in full plain iron
};

inline const char* KitName(Kit k) {
    return k == Kit::Fists ? "fists" : k == Kit::Weapon ? "training weapon" : "none";
}

// Layers that make a "full iron set" (chest, arms, legs, gloves, gorget, helm).
inline constexpr u8 kIronLayers[] = {4, 6, 7, 10, 13, 19};

inline Kit KitFor(const Worn* w, std::size_t n) {
    u16 hand = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (w[i].layer == 25) return Kit::None;            // mounted: dismount first
        if (w[i].layer == 2) return Kit::None;             // shield / two-hander
        if (w[i].layer == 1) {
            if (w[i].hue != 0) return Kit::None;           // a dyed/magic weapon is not practice gear
            hand = w[i].graphic;
        }
    }
    if (hand == 0 || Spellbook(hand)) return Kit::Fists;
    if (!TrainingWeapon(hand)) return Kit::None;
    for (u8 layer : kIronLayers) {
        bool found = false;
        for (std::size_t i = 0; i < n; ++i)
            if (w[i].layer == layer && w[i].hue == 0 && IronArmour(w[i].graphic)) found = true;
        if (!found) return Kit::None;
    }
    return Kit::Weapon;
}

// Fists against fists; a weapon only against someone in iron. A dagger user
// never spars an unarmoured wrestler.
inline bool Compatible(Kit a, Kit b) {
    if (a == Kit::None || b == Kit::None) return false;
    return a == b;
}

// ---- the round plan ----------------------------------------------------------
struct RoundSight {
    i64  nowMs = 0;
    i64  meetingStartMs = 0;      // when the PARTY was confirmed (not the invitation)
    bool consented = false;       // the partner agreed to spar in this meeting
    bool inParty = false;         // both still in our party
    bool peerReady = false;       // partner said "Ready to spar." this meeting
    i64  readyAskedMs = 0;        // when WE said it (0 = not yet)
    bool kitsCompatible = false;
    bool externalThreat = false;
    bool healthFresh = false;     // both health bars updated within kHealthFreshMs
    i32  selfPct = -1, peerPct = -1;
    i32  bandages = 0;
    bool roundActive = false;     // the client holds a live round lease
    i64  lastRoundEndMs = 0;
    i32  roundsDone = 0;
    bool busy = false;            // an action is in flight
};

enum class Step : u8 {
    Wait = 0,     // nothing to do this tick
    SayReady,     // announce "Ready to spar." (once per meeting)
    Bandage,      // heal self between rounds
    StartRound,   // attack the partner (a new lease)
    Continue,     // a round is running; the client's safety tick guards it
    End,          // finish the meeting
};

struct RoundPlan { Step step = Step::Wait; const char* reason = ""; };

inline RoundPlan DecideRound(const RoundSight& s) {
    auto end = [](const char* why) { return RoundPlan{Step::End, why}; };
    if (!s.consented) return end("no consent");
    if (!s.inParty) return end("partner left the party");
    if (!s.kitsCompatible) return end("gear no longer fit for sparring");
    if (s.externalThreat) return end("a real threat is near");
    if (s.bandages <= 0) return end("out of bandages");
    if (s.nowMs - s.meetingStartMs >= kMeetingMs) return end("meeting time is up");
    if (s.roundsDone >= kMaxRounds) return end("all rounds done");
    if (s.roundActive) return {Step::Continue, "round in progress"};
    if (!s.peerReady) {
        if (!s.readyAskedMs) return {Step::SayReady, "announce readiness once"};
        if (s.nowMs - s.readyAskedMs >= kReadyWaitMs) return end("partner never said ready");
        return {Step::Wait, "waiting for the partner's ready"};
    }
    if (s.busy) return {Step::Wait, "action in flight"};
    if (s.selfPct >= 0 && s.selfPct < kStartPercent) return {Step::Bandage, "heal before the next round"};
    if (s.nowMs - s.lastRoundEndMs < kRoundGapMs) return {Step::Wait, "breath between rounds"};
    if (!s.healthFresh) return {Step::Wait, "health bars not fresh"};
    if (s.peerPct < kStartPercent) return {Step::Wait, "partner still healing"};
    return {Step::StartRound, "both healthy"};
}

// Should the running round stop? (the client's safety tick)
inline bool StopRound(i32 selfPct, i32 peerPct, bool fresh, bool threat, bool inRange, bool leaseExpired) {
    return !fresh || threat || !inRange || leaseExpired || selfPct < kStopPercent || peerPct < kStopPercent;
}

// ---- bystander healer --------------------------------------------------------
// A bot that heard sparring consent nearby and still wants Healing walks over
// and bandages whichever sparrer is lowest, below this.
inline constexpr i32 kBystanderHealBelow = 85;
inline constexpr i64 kBystanderWindowMs = 240000;
inline constexpr i64 kBystanderBandageGapMs = 8000;

} // namespace uo::sparring
