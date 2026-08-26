#pragma once

#include "uo/types.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Pure decision rules for talking to a SphereServer Source-X shard.
//
// Each of these encodes a behaviour that was wrong (or hard-coded) in the M1
// proof-of-concept and cost a debugging session to find. They live here, free
// of sockets and client state, so the regression tests exercise exactly the
// code the client runs -- not a copy of it.
//
// Every rule cites the Source-X source that justifies it.
// ---------------------------------------------------------------------------

namespace uo::sphere {

// --- 0x8C relay ------------------------------------------------------------
// Source-X advertises its own ServIP/ServPort in 0x8C. For a single-server
// shard that is the endpoint the client is already connected to, and
// PacketServerRelay::onSent (src/network/send.cpp:2823-2830) has already
// switched that socket to the game protocol -- it expects a bare 0x91 next.
// Re-sending the 4-byte seed there would be read as an opcode and the whole
// buffer discarded (src/network/CNetworkInput.cpp:399-406).
//
// advertisedIp == 0 means "stay here" (some emulators advertise no address).
inline bool StayOnLoginSocket(u32 advertisedIp, u16 advertisedPort,
                              u32 peerIp, u16 peerPort,
                              bool hasEndpointOverride) {
    if (hasEndpointOverride) return false;
    if (advertisedPort != peerPort) return false;
    return advertisedIp == 0 || advertisedIp == peerIp;
}

// --- character selection ---------------------------------------------------
// Returns the slot to play, or -1 when nothing matches. A configured name wins
// over a configured slot; an empty name falls back to the slot index.
inline int SelectCharacterSlot(const char* const* slotNames, int slotCount,
                               const char* wantName, int wantSlot) {
    if (!slotNames || slotCount <= 0) return -1;

    if (wantName && wantName[0]) {
        for (int i = 0; i < slotCount; ++i) {
            const char* n = slotNames[i];
            if (!n || !n[0]) continue;
#if defined(_WIN32)
            if (_stricmp(n, wantName) == 0) return i;
#else
            if (strcasecmp(n, wantName) == 0) return i;
#endif
        }
        return -1;
    }

    if (wantSlot < 0 || wantSlot >= slotCount) return -1;
    const char* n = slotNames[wantSlot];
    return (n && n[0]) ? wantSlot : -1;
}

// --- inbound 0x73 ----------------------------------------------------------
// Source-X answers a client ping with its own 0x73 (PacketPingReq::onReceive,
// src/network/receive.cpp:1335-1344) and never pings first. Echoing that answer
// makes the server answer again: M1 measured ~24,000 exchanges in 100 seconds,
// which tripped Sphere's MaxSizeClientIn quota and got the client disconnected.
enum class PingAction : u8 {
    ConsumeAsReply,  // the answer to a keepalive we sent; send nothing
    Echo,            // unsolicited server ping; echo it once
    Ignore,          // unsolicited, but too soon after the last echo
};

inline PingAction DecidePing(int pingsOutstanding, i64 nowMs,
                             i64 lastEchoMs, i64 minEchoGapMs) {
    if (pingsOutstanding > 0) return PingAction::ConsumeAsReply;
    if (lastEchoMs != 0 && (nowMs - lastEchoMs) < minEchoGapMs)
        return PingAction::Ignore;
    return PingAction::Echo;
}

// --- credential redaction --------------------------------------------------
// 0x80 (account login) and 0x91 (game login) carry the password in clear.
// Returns the offset of the 30-byte password field, or 0 when the packet
// carries no password.
inline usize CredentialPasswordOffset(const u8* pkt, usize size) {
    if (!pkt || size == 0) return 0;
    if (pkt[0] == 0x80 && size >= 62) return 31;   // cmd + user[30]
    if (pkt[0] == 0x91 && size >= 65) return 35;   // cmd + authkey[4] + user[30]
    return 0;
}

inline constexpr usize kCredentialFieldLen = 30;
inline constexpr u8    kRedactionFill = 0xEE;

// --- movement sequence -----------------------------------------------------
// Sphere validates the sequence in exactly one case -- it must be 0 when the
// server has reset it (PacketMovementReq::onReceive,
// src/network/receive.cpp:270-273). On accepting 255 the server itself does
// `if (sequence == UINT8_MAX) sequence = 0; m_sequence = ++sequence;`, so its
// next expected value after 255 is 1, not 0. The client wraps the same way.
inline u8 NextMoveSequence(u8 current) {
    return (current == 0xFF) ? u8(1) : u8(current + 1);
}

// --- movement gait ---------------------------------------------------------
// Real players run. Walking is the exception, not the baseline: a bot that
// walks a road at 400ms/tile reads as an NPC to anyone watching it.
//
// The wire form is one bit. Source-X splits the 0x02 direction byte into
// `DIR_TYPE(rawdir & 0xF)` for the facing and `rawdir & DIR_MASK_RUNNING` for
// the gait (CClient::Event_Walk, src/game/clients/CClientEvent.cpp:862,904),
// with DIR_MASK_RUNNING = 0x80 (src/game/uo_files/uofiles_enums.h:435). The
// run bit only sets STATF_FLY on the character; the sequence handling in
// PacketMovementReq::onReceive (src/network/receive.cpp:265-282) never looks at
// it, so gait cannot desync the sequence or turn an accept into a reject.
enum class Gait : u8 {
    Walk,   // clear 0x80: 400ms cadence
    Run,    // set 0x80: 200ms cadence
    Auto,   // Run, unless a Sphere rule below makes running the worse trade
};

// DIR_MASK_RUNNING (src/game/uo_files/uofiles_enums.h:435).
inline constexpr u8 kDirMaskRunning = 0x80;

inline u8 MoveDirectionByte(u8 dir, bool run) {
    return static_cast<u8>((dir & 0x07) | (run ? kDirMaskRunning : u8(0)));
}

inline const char* GaitName(Gait g) {
    switch (g) {
        case Gait::Walk: return "walk";
        case Gait::Run:  return "run";
        case Gait::Auto: return "auto";
    }
    return "?";
}

// Stamina reserve below which Auto stops running (percent of max stamina).
//
// UNKNOWN: Source-X has no gait-specific stamina gate -- there is no "too
// tired to run" rule anywhere in Event_Walk or CanMoveWalkTo. What it does
// have is a hard, gait-independent block: CChar::CanMove refuses every step
// at STAT_DEX <= 0 and says "You are too fatigued to move."
// (src/game/chars/CCharAct.cpp:4611-4617), which reaches us as a 0x21 reject.
// This reserve is therefore OURS, not the server's: walking below it stops
// spending stamina so the regen timer can refill instead of trading the last
// few points for speed. It only ever makes the bot more conservative than the
// server requires, and never changes what the server would have accepted.
inline constexpr i32 kRunStaminaReservePercent = 15;

// Carry-weight load (percent of max) at which Auto stops running.
//
// This one IS a server rule. CChar::CanMoveWalkTo rolls a per-step stamina
// loss with chance Calc_GetSCurve(loadPercent - StaminaLossAtWeight, 10)
// (src/game/chars/CCharAct.cpp:4818-4838) and adds RunningPenalty percentage
// points to loadPercent when STATF_FLY is set -- i.e. only while running.
// This shard runs the Source-X defaults (runtime/sphere.ini:316,319):
// StaminaLossAtWeight=150, RunningPenalty=50. So a runner's roll is
// (load + 50 - 150) into a variance-10 bell curve: at load 0-50% that is
// -100..-50, which Calc_GetBellCurve halves 5-10 times down to ~0/1000.
// Running is FREE while light -- which is exactly why Run is the default.
// It is only from ~100% load that the run bit alone pushes the roll into the
// loss band, so that is where Auto drops back to walking.
inline constexpr i32 kRunWeightLoadPercent = 100;

// Resolve a gait to the run bit. stam/weight may be -1 (server has not told us
// yet); an unknown input simply does not veto running.
inline bool GaitWantsRun(Gait gait, i32 stamCur, i32 stamMax,
                         i32 weight, i32 maxWeight) {
    if (gait == Gait::Walk) return false;
    if (gait == Gait::Run) return true;
    if (stamMax > 0 && stamCur >= 0 &&
        stamCur * 100 < stamMax * kRunStaminaReservePercent)
        return false;
    if (maxWeight > 0 && weight >= 0 &&
        weight * 100 >= maxWeight * kRunWeightLoadPercent)
        return false;
    return true;
}

}  // namespace uo::sphere
