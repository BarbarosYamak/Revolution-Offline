#pragma once

#include "uo/packet_lengths.h"

// ---------------------------------------------------------------------------
// Sphere (SphereServer Source-X) packet-length overlay.
//
// `kPacketLength` in packet_lengths.h is a verbatim port of
// g_PacketLengthTable @ 0x5170A8 in client_2.0.7.exe and is treated as a
// read-only reverse-engineering artifact — it is never edited.
//
// A Sphere shard can legitimately send a handful of opcodes that the 1997
// table has no entry for (value 0 = "unknown, cannot frame"). Framing is
// impossible without a length, so an unknown opcode is fatal to the stream.
// This overlay supplies lengths for the opcodes Source-X can emit to a client
// that reports an old version, so those packets frame cleanly instead of
// killing the session.
//
// Only add an entry with a cited server-side source. Each one below names the
// Source-X constructor that fixes the size.
// ---------------------------------------------------------------------------

namespace uo {

// Extra fixed lengths, indexed by opcode. 0 = no override.
// Values use the same encoding as kPacketLength (kPacketLengthVariable bit set
// for self-describing packets).
inline constexpr u16 kSpherePacketLength[256] = {
    // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x10-0x1F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x20-0x2F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x30-0x3F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x40-0x4F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x50-0x5F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x60-0x6F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x70-0x7F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x80-0x8F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0x90-0x9F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0xA0-0xAF
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0xB0-0xBF
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0xC0-0xCF
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0xD0-0xDF
    //  0xD1 PacketLogoutAck — PacketSend(XCMD_LogoutStatus, 2) in
    //  Source-X src/network/send.cpp:4631 (reply to our 0xD1 logout request).
    0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0xE0-0xEF
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    // 0xF0-0xFF
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// Effective wire length for an opcode: the 2.0.7 client table first, then the
// Sphere overlay. Returns 0 when neither knows the opcode (unframeable).
inline constexpr u16 PacketLengthFor(u8 cmd) {
    const u16 base = kPacketLength[cmd];
    return base != 0 ? base : kSpherePacketLength[cmd];
}

}
