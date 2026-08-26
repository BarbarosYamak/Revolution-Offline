#include "uo/builders.h"
#include "uo/buf_reader.h"
#include "uo/endian.h"
#include "uo/packet_ids.h"

#include <cstdio>
#include <cstring>

namespace uo::build {

usize VendorBuy(u8* out, u32 vendorSerial, const VendorBuyEntry* entries, usize count) {
    BufWriter w(out, 256);
    w.WriteU8(RawId(PacketId::VendorBuy));
    const usize len_at = w.size();
    w.WriteU16(0);
    w.WriteU32(vendorSerial);
    w.WriteU8(0x02);                 // flag 2 = buying (server rejects anything else)
    for (usize i = 0; i < count; ++i) {
        w.WriteU8(entries[i].layer);
        w.WriteU32(entries[i].serial);
        w.WriteU16(entries[i].qty);
    }
    w.PatchU16(len_at, static_cast<u16>(w.size()));
    return w.size();
}

usize VendorSell(u8* out, u32 vendorSerial, const VendorSellEntry* entries,
                 usize count) {
    BufWriter w(out, 256);
    w.WriteU8(0x9F);
    const usize len_at = w.size();
    w.WriteU16(0);
    w.WriteU32(vendorSerial);
    w.WriteU16(static_cast<u16>(count));
    for (usize i = 0; i < count; ++i) {
        w.WriteU32(entries[i].serial);
        w.WriteU16(entries[i].qty);
    }
    w.PatchU16(len_at, static_cast<u16>(w.size()));
    return w.size();
}

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

// 0x73 Ping request — 2 bytes. Same shape as the reply; Source-X answers
// with PacketPingAck (src/network/receive.cpp:1335-1344).
usize PingRequest(u8* out, u8 sequence) {
    out[0] = RawId(PacketId::ServerPingNullsub);
    out[1] = sequence;
    return 2;
}

// 0xD1 Logout request — 2 bytes (cmd + 0x00).
usize LogoutRequest(u8* out) {
    out[0] = 0xD1;
    out[1] = 0x00;
    return 2;
}

// 0x00 Create Character — 104 bytes.
// Offsets verified field-by-field against PacketCreate::onReceive
// (Source-X src/network/receive.cpp:58-152), whose reads start at offset 1:
//   [0]       cmd
//   [1-9]     skip(9)   : pattern1(4) + pattern2(4) + 1
//   [10-39]   name      : fixed 30 ASCII (Packet::readStringASCII, packet.cpp:782)
//   [40-41]   skip(2)
//   [42-45]   flags(u32)
//   [46-53]   skip(8)
//   [54]      profession
//   [55-69]   skip(15)
//   [70]      race/sex flag (even=male, odd=female; <7.0.0.0: 0/1 = human)
//   [71-73]   str, dex, int
//   [74-79]   skill1, val1, skill2, val2, skill3, val3
//   [80-81]   skin hue
//   [82-85]   hair id, hair hue
//   [86-89]   beard id, beard hue
//   [90]      shard index
//   [91]      start location index
//   [92-99]   skip(8)   : slot(4) + client ip(4)
//   [100-103] shirt hue, pants hue
usize CreateCharacter(u8* out, const CreateCharacterParams& p) {
    BufWriter w(out, 104);
    w.WriteU8(0x00);
    w.WriteU32(0xEDEDEDEDu);   // pattern1
    w.WriteU32(0xFFFFFFFFu);   // pattern2
    w.WriteU8(0x00);           // "kuoc" marker byte
    w.WriteFixedAscii(p.name, 30);
    w.WriteU16(0x0000);
    w.WriteU32(0x00000001u);   // client flags (T2A)
    w.WriteU32(0x00000000u);   // unknown
    w.WriteU32(0x00000000u);   // unknown
    w.WriteU8(0x00);           // profession 0 = custom (skills come from below)
    for (int i = 0; i < 15; ++i) w.WriteU8(0x00);
    w.WriteU8(p.female ? 0x01 : 0x00);
    w.WriteU8(p.str);
    w.WriteU8(p.dex);
    w.WriteU8(p.intel);
    w.WriteU8(p.skill1); w.WriteU8(p.skill1Val);
    w.WriteU8(p.skill2); w.WriteU8(p.skill2Val);
    w.WriteU8(p.skill3); w.WriteU8(p.skill3Val);
    w.WriteU16(p.skinHue);
    w.WriteU16(p.hairId);
    w.WriteU16(p.hairHue);
    w.WriteU16(p.beardId);
    w.WriteU16(p.beardHue);
    w.WriteU8(0x00);           // shard index
    w.WriteU8(p.startLoc);
    w.WriteU32(p.slot);
    w.WriteU32(p.clientIP);
    w.WriteU16(p.shirtHue);
    w.WriteU16(p.pantsHue);
    return w.size();
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

// 0x05 Attack Request — 5 bytes: cmd + serial(4 BE).
usize Attack(u8* out, u32 serial) {
    BufWriter w(out, 5);
    w.WriteU8(RawId(PacketId::AttackRequest));
    w.WriteU32(serial);
    return w.size();
}

usize WarMode(u8* out, bool warMode, u8 arg1, u8 arg2, u8 arg3) {
    BufWriter w(out, 5);
    w.WriteU8(RawId(PacketId::WarMode));
    w.WriteU8(warMode ? 1u : 0u);
    w.WriteU8(arg1);
    w.WriteU8(arg2);
    w.WriteU8(arg3);
    return w.size();
}

// 0x2C Resurrection Menu choice — 2 bytes: cmd + choice.
usize ResurrectChoice(u8* out, u8 choice) {
    BufWriter w(out, 2);
    w.WriteU8(RawId(PacketId::ResurrectionMenu));
    w.WriteU8(choice);
    return w.size();
}

// 0x7D Response To Dialog Box — 13 bytes: cmd + dialogSerial + menuId + index +
// model + hue. Mirrors Packet_BuildDialogBoxResponse @0x427b20.
usize DialogResponse(u8* out, u32 dialogSerial, u16 menuId, u16 index, u16 model, u16 hue) {
    BufWriter w(out, 13);
    w.WriteU8(RawId(PacketId::DialogResponse));
    w.WriteU32(dialogSerial);
    w.WriteU16(menuId);
    w.WriteU16(index);
    w.WriteU16(model);
    w.WriteU16(hue);
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

// 0x12 Action Request — generic string action (variable):
//   BYTE    cmd 0x12
//   BYTE[2] total length (BE) = payloadLen + 5
//   BYTE    subcommand
//   BYTE[N] payload ASCII + NUL
// Models Packet_BuildActionRequest @0x4257f0; cast spell (0x56), use skill
// (0x24) and bow/salute (0xC7) all flow through this same packet.
static usize ActionRequestStr(u8* out, u8 subcommand, const char* payload) {
    BufWriter w(out, 256);
    w.WriteU8(0x12);
    const usize len_at = w.size();
    w.WriteU16(0);
    w.WriteU8(subcommand);
    const usize plen = std::strlen(payload);
    w.WriteBytes(reinterpret_cast<const u8*>(payload), plen);
    w.WriteU8(0); // NUL
    w.PatchU16(len_at, static_cast<u16>(w.size()));
    return w.size();
}

usize CastSpell(u8* out, int spellId) {
    char payload[16];
    std::snprintf(payload, sizeof(payload), "%d", spellId);
    return ActionRequestStr(out, 0x56, payload);
}

usize UseSkill(u8* out, int skillId) {
    char payload[16];
    std::snprintf(payload, sizeof(payload), "%d 0", skillId);
    return ActionRequestStr(out, 0x24, payload);
}

usize PickUpItem(u8* out, u32 serial, u16 amount) {
    BufWriter w(out, 7);
    w.WriteU8(0x07);
    w.WriteU32(serial);
    w.WriteU16(amount);
    return w.size();
}

usize DropItem(u8* out, u32 serial, u16 x, u16 y, i8 z, u32 container) {
    BufWriter w(out, 14);
    w.WriteU8(0x08);
    w.WriteU32(serial);
    w.WriteU16(x);
    w.WriteU16(y);
    w.WriteU8(static_cast<u8>(z));
    w.WriteU32(container);
    return w.size();
}

usize EquipItem(u8* out, u32 serial, u8 layer, u32 mobile) {
    BufWriter w(out, 10);
    w.WriteU8(0x13);
    w.WriteU32(serial);
    w.WriteU8(layer);
    w.WriteU32(mobile);
    return w.size();
}

// 0x6C Target Cursor response (19 bytes) — see builders.h for the field map.
// Shared body for the object/ground/cancel variants; `type` and the target
// fields differ per caller.
static usize TargetCursorResponse(u8* out, u8 type, u32 cursorId, u8 subtype,
                                  u32 serial, u16 x, u16 y, i8 z, u16 model) {
    BufWriter w(out, 19);
    w.WriteU8(RawId(PacketId::TargetCursor));
    w.WriteU8(type);
    w.WriteU32(cursorId);
    w.WriteU8(subtype);
    w.WriteU32(serial);
    w.WriteU16(x);
    w.WriteU16(y);
    w.WriteU8(0);                       // unknown high byte of the z word
    w.WriteU8(static_cast<u8>(z));      // signed z
    w.WriteU16(model);
    return w.size();
}

usize TargetCursorObject(u8* out, u32 cursorId, u8 subtype, u32 serial,
                         u16 x, u16 y, i8 z, u16 model) {
    return TargetCursorResponse(out, 0, cursorId, subtype, serial, x, y, z, model);
}

usize TargetCursorGround(u8* out, u32 cursorId, u8 subtype,
                         u16 x, u16 y, i8 z, u16 model) {
    return TargetCursorResponse(out, 1, cursorId, subtype, 0, x, y, z, model);
}

usize TargetCursorCancel(u8* out, u32 cursorId, u8 subtype) {
    return TargetCursorResponse(out, 0, cursorId, subtype, 0, 0xFFFF, 0xFFFF, 0, 0);
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
