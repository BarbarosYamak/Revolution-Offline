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

}  // namespace uo::sphere
