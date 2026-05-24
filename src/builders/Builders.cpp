#include "uo/builders.h"
#include "uo/buf_reader.h"
#include "uo/endian.h"
#include "uo/packet_ids.h"

#include <cstring>

namespace uo::build {

usize Seed(u8* out, u32 seed) {
    StoreBE32(out, seed);
    return 4;
}

// 0x80 Account Login — 62 bytes.
//   BYTE cmd
//   BYTE[30] username (NUL-padded)
//   BYTE[30] password (NUL-padded)
//   BYTE nextLoginKey
usize LoginRequest(u8* out, const char* user, const char* pass, u8 nextLoginKey) {
    BufWriter w(out, 62);
    w.WriteU8(RawId(PacketId::LoginRequest));
    w.WriteFixedAscii(user, 30);
    w.WriteFixedAscii(pass, 30);
    w.WriteU8(nextLoginKey);
    return w.size();
}

// 0xA0 Select Server — 3 bytes.
usize SelectServer(u8* out, u16 serverIndex) {
    BufWriter w(out, 3);
    w.WriteU8(RawId(PacketId::SelectServer));
    w.WriteU16(serverIndex);
    return w.size();
}

// 0x91 Game Server Login — 65 bytes.
//   BYTE cmd
//   BYTE[4] authKey (BE)
//   BYTE[30] username
//   BYTE[30] password
usize GameLogin(u8* out, u32 authKey, const char* user, const char* pass) {
    BufWriter w(out, 65);
    w.WriteU8(RawId(PacketId::GameServerLogin));
    w.WriteU32(authKey);
    w.WriteFixedAscii(user, 30);
    w.WriteFixedAscii(pass, 30);
    return w.size();
}

// 0x5D Play Character — 73 bytes.
// Field order verbatim from Packet_BuildPlayCharacter @ 0x425EE0:
//   BYTE cmd
//   BYTE[4] 0xEDEDEDED (BE)
//   BYTE[30] character name
//   BYTE[30] zeros (memset'd to 0 by the original — looks like a
//                   placeholder for a removed password field)
//   BYTE[4]  slot-or-flag (BE)
//   BYTE[4]  clientIP (BE) — read from the socket's local-address slot
usize PlayCharacter(u8* out, const char* charName,
                    u32 slotOrFlag, u32 clientIP) {
    BufWriter w(out, 73);
    w.WriteU8(RawId(PacketId::PlayCharacter));
    w.WriteU32(0xEDEDEDEDu);
    w.WriteFixedAscii(charName, 30);
    u8 zeros[30] = {0};
    w.WriteBytes(zeros, sizeof(zeros));
    w.WriteU32(slotOrFlag);
    w.WriteU32(clientIP);
    return w.size();
}

// 0xBD Client Version response — variable.
//   BYTE cmd
//   BYTE[2] total length (BE, includes cmd+len)
//   BYTE[N] version string, NUL-terminated
usize ClientVersion(u8* out, const char* version) {
    BufWriter w(out, 256);
    w.WriteU8(RawId(PacketId::ClientVersionQuery));
    const usize len_at = w.size();
    w.WriteU16(0); // placeholder; patched after we know the length
    const usize ver_len = std::strlen(version);
    w.WriteBytes(reinterpret_cast<const u8*>(version), ver_len);
    w.WriteU8(0); // NUL terminator
    w.PatchU16(len_at, static_cast<u16>(w.size()));
    return w.size();
}

// 0x73 Ping reply — 2 bytes.
usize PingReply(u8* out, u8 sequence) {
    out[0] = RawId(PacketId::ServerPingNullsub);
    out[1] = sequence;
    return 2;
}

// 0x09 Single Click — 5 bytes.
usize SingleClick(u8* out, u32 serial) {
    BufWriter w(out, 5);
    w.WriteU8(RawId(PacketId::SingleClick));
    w.WriteU32(serial);
    return w.size();
}

usize GetPlayerStatus(u8* out, u8 type, u32 serial) {
    BufWriter w(out, 10);
    w.WriteU8(RawId(PacketId::GetPlayerStatus));
    w.WriteU32(0xEDEDEDEDu);
    w.WriteU8(type);
    w.WriteU32(serial);
    return w.size();
}

// 0x98 AllNames / MobName query — 7 bytes.
usize MobNameQuery(u8* out, u32 serial) {
    BufWriter w(out, 7);
    w.WriteU8(RawId(PacketId::MobNameQuery));
    w.WriteU16(7);
    w.WriteU32(serial);
    return w.size();
}

// 0x06 Double Click — 5 bytes.
usize DoubleClick(u8* out, u32 serial) {
    BufWriter w(out, 5);
    w.WriteU8(RawId(PacketId::DoubleClick));
    w.WriteU32(serial);
    return w.size();
}

// 0x12 Action Request — Open Door (5 bytes): cmd, length(2 BE), subcommand
// 0x58, empty NUL payload. The server opens any door in the tile the player
// faces; no serial needed, so it is immune to door packets arriving late.
usize OpenDoor(u8* out) {
    out[0] = 0x12;
    out[1] = 0x00;
    out[2] = 0x05;  // total packet length
    out[3] = 0x58;  // subcommand: open door (faced tile spatial search)
    out[4] = 0x00;  // empty NUL-terminated payload
    return 5;
}

// 0x02 Move Request.
// Wire layout is version-gated:
//   - Pre-T2A (UO Demo, 1997 protocol): 3 bytes — cmd + dir + seq
//   - T2A and later: 7 bytes — cmd + dir + seq + fastWalkKey(4 BE)
// `legacy=true` produces the 3-byte form; mismatched length causes the
// shard to silently accumulate trailing zeros into a phantom packet and
// drop all subsequent moves.
usize MoveRequest(u8* out, u8 direction, u8 sequence, u32 fastWalkKey,
                  bool legacy) {
    if (legacy) {
        BufWriter w(out, 3);
        w.WriteU8(RawId(PacketId::MoveRequest));
        w.WriteU8(direction);
        w.WriteU8(sequence);
        return w.size();
    }
    BufWriter w(out, 7);
    w.WriteU8(RawId(PacketId::MoveRequest));
    w.WriteU8(direction);
    w.WriteU8(sequence);
    w.WriteU32(fastWalkKey);
    return w.size();
}

// 0x03 ASCII Speech — variable.
//   BYTE cmd
//   BYTE[2] total length (BE)
//   BYTE    type
//   BYTE[2] hue (BE)
//   BYTE[2] font (BE)
//   BYTE[N] text + NUL
usize SpeechAscii(u8* out, u8 type, u16 hue, u16 font, const char* text) {
    BufWriter w(out, 256);
    w.WriteU8(RawId(PacketId::SpeechAscii));
    const usize len_at = w.size();
    w.WriteU16(0);
    w.WriteU8(type);
    w.WriteU16(hue);
    w.WriteU16(font);
    const usize tlen = std::strlen(text);
    w.WriteBytes(reinterpret_cast<const u8*>(text), tlen);
    w.WriteU8(0); // NUL
    w.PatchU16(len_at, static_cast<u16>(w.size()));
    return w.size();
}

}
