#pragma once

#include "uo/types.h"

// Outbound packet builders. Each writes a complete packet to the
// caller-provided buffer (must be large enough; 256 bytes is plenty
// for every M1 packet) and returns the number of bytes written.
//
// Wire layout follows the original client (`Packet_Build*` /
// `Packet_HandleClientVersion` in client_2.0.7.exe). All multi-byte
// numeric fields are big-endian on the wire.

namespace uo::build {

// Plaintext encryption seed: 4 raw bytes prepended to the first TCP
// stream before any UO packet. In NoCrypt mode the seed value is
// effectively a token; server doesn't decrypt with it.
usize Seed(u8* out, u32 seed);

// 0x80 Account Login (62 bytes).
usize LoginRequest(u8* out, const char* user, const char* pass,
                   u8 nextLoginKey = 0x5D);

// 0xA0 Select Server (3 bytes). serverIndex is 0-based.
usize SelectServer(u8* out, u16 serverIndex);

// 0x91 Game Server Login (65 bytes). authKey = seed received in 0x8C.
usize GameLogin(u8* out, u32 authKey, const char* user, const char* pass);

// 0x5D Play Character (73 bytes). Layout per Packet_BuildPlayCharacter
// (IDB 0x425EE0): pattern 0xEDEDEDED, name[30], zero[30],
// slotOrFlag(u32 BE), clientIP(u32 BE). Slot byte fits in low byte of
// slotOrFlag for shards that use the canonical mapping.
usize PlayCharacter(u8* out, const char* charName,
                    u32 slotOrFlag, u32 clientIP);

// 0xBD Client Version response (variable). version is a NUL-terminated
// ASCII string ("2.0.7" for parity with the original binary).
usize ClientVersion(u8* out, const char* version);

// 0x73 Ping reply (2 bytes). Echo the sequence byte the server sent.
usize PingReply(u8* out, u8 sequence);

// 0x03 ASCII Speech (variable).
// type: 0=normal, 2=emote, 6=system, 8=whisper, 9=yell ...
usize SpeechAscii(u8* out, u8 type, u16 hue, u16 font, const char* text);

// 0x09 Single Click (5 bytes): cmd + serial(4 BE).
usize SingleClick(u8* out, u32 serial);

// 0x34 Get Player Status (10 bytes): cmd, 0xEDEDEDED marker, request type,
// serial. type 0x04 requests basic status (0x11); 0x05 requests skills (0x3A).
usize GetPlayerStatus(u8* out, u8 type, u32 serial);

// 0x98 AllNames / MobName query (7 bytes): cmd + len(2) + serial(4 BE).
// Requests the target mobile's name (server replies with 0x98 including name).
usize MobNameQuery(u8* out, u32 serial);

// 0x06 Double Click (5 bytes): cmd + serial(4 BE). Used to "use" an object,
// e.g. double-clicking a door makes the server open it (DoorOpen).
usize DoubleClick(u8* out, u32 serial);

// 0x12 Action Request — Open Door (5 bytes): cmd + len + subcommand 0x58 +
// NUL. The legit player "open door" hotkey macro; the server spatial-searches
// the tile the player is facing and opens any door there (no serial needed),
// so it works even if the door's object packet hasn't arrived yet.
usize OpenDoor(u8* out);

// 0x02 Move Request (3 or 7 bytes).
//   BYTE cmd
//   BYTE direction (0..7, +0x80 for run)
//   BYTE sequence  (1..255, wraps 0xFF -> 1; 0 reserved for resync)
//   [optional, T2A+ only] BYTE[4] fastWalkKey (BE)
// Use legacy=true for pre-T2A shards (UO Demo). Length mismatch is
// fatal: server silently buffers trailing bytes and goes silent.
usize MoveRequest(u8* out, u8 direction, u8 sequence,
                  u32 fastWalkKey = 0, bool legacy = false);

}
