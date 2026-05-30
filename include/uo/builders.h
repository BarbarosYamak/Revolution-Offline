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

// 0x05 Attack Request (5 bytes): cmd + serial(4 BE). Sent when double-clicking
// a mobile in war mode (Packet_BuildAttackRequest @0x425680). The server replies
// 0xAA (attack accepted / refused).
usize Attack(u8* out, u32 serial);

// 0x72 War Mode request (5 bytes): cmd, target mode, three cached args.
// Official client initializes the cached args to 04 00 00 and updates them from
// inbound 0x72 confirmations.
usize WarMode(u8* out, bool warMode, u8 arg1 = 4, u8 arg2 = 0, u8 arg3 = 0);

// 0x2C Resurrection Menu choice (2 bytes): cmd + choice. choice: 1 = resurrect,
// 2 = remain a ghost (0 = manifest/prompt). Mirrors Packet_BuildResMenuChoice
// @0x4262a0; the server prompts with an inbound 0x2C and we answer with this.
usize ResurrectChoice(u8* out, u8 choice);

// 0x7D Response To Dialog Box (13 bytes): cmd, dialogSerial(4 BE), menuId(2 BE),
// index(2 BE, 1-based; 0 = cancel), model(2 BE), hue(2 BE). Answers the 0x7C
// Open Dialog/Menu; index/model/hue identify the chosen option (model+hue echo
// the 0x7C entry). Mirrors Packet_BuildDialogBoxResponse @0x427b20.
usize DialogResponse(u8* out, u32 dialogSerial, u16 menuId, u16 index, u16 model, u16 hue);

// 0x12 Action Request — Open Door (5 bytes): cmd + len + subcommand 0x58 +
// NUL. The legit player "open door" hotkey macro; the server spatial-searches
// the tile the player is facing and opens any door there (no serial needed),
// so it works even if the door's object packet hasn't arrived yet.
usize OpenDoor(u8* out);

// 0x12 Action Request — Cast Spell (variable): cmd + len(2 BE) + subcommand
// 0x56 + ASCII spell id + NUL. Mirrors MACRO_CASTSPELL in client_2.0.7
// (Macro_ExecuteAction @0x4b52ec: payload = "%d", spellId). spellId is the
// 1-based spell number (the macro stores it 0-based and sends intParam+1).
usize CastSpell(u8* out, int spellId);

// 0x12 Action Request — Use Skill (variable): cmd + len(2 BE) + subcommand
// 0x24 + ASCII "<skillId> 0" + NUL. Mirrors MACRO_USESKILL
// (Macro_ExecuteAction @0x4b5228 / SkillsGump_SendUseSkillAction @0x49fe78:
// payload = "%d %d", skillId, 0). skillId is the 0-based skill index; the
// trailing 0 is the lock/last-skill flag the official client always sends.
usize UseSkill(u8* out, int skillId);

// 0x6C Target Cursor response (19 bytes). Sent in reply to an inbound 0x6C
// target request (e.g. after casting a spell or using an item that needs a
// target). Layout mirrors TargetCursor_SendResponse @0x41E560:
//   BYTE  cmd (0x6C)
//   BYTE  type        (0 = object/serial, 1 = ground/static)
//   BYTE[4] cursorId  (BE, echoed from the request)
//   BYTE  subtype     (echoed from the request)
//   BYTE[4] serial    (BE, target object; 0 for ground)
//   BYTE[2] x (BE)
//   BYTE[2] y (BE)
//   BYTE  unknown (0)
//   BYTE  z (signed)
//   BYTE[2] model     (BE, target graphic; 0 for ground)
//
// Object click: type 0, the clicked serial plus its x/y/z and graphic.
usize TargetCursorObject(u8* out, u32 cursorId, u8 subtype, u32 serial,
                         u16 x, u16 y, i8 z, u16 model);
// Ground/static click: type 1, serial 0.
usize TargetCursorGround(u8* out, u32 cursorId, u8 subtype,
                         u16 x, u16 y, i8 z, u16 model);
// Cancel (Esc / right-click): type 0, serial 0, x=y=0xFFFF, z/model 0.
usize TargetCursorCancel(u8* out, u32 cursorId, u8 subtype);

// 0x07 Pick Up Item (7 bytes): cmd + serial(4 BE) + amount(2 BE). Lifts an
// item onto the drag cursor; amount 0 means the whole stack. Models
// Packet_BuildPickUp @0x425700. Always paired with a 0x08 drop or 0x13 equip.
usize PickUpItem(u8* out, u32 serial, u16 amount = 0);

// 0x08 Drop Item (14 bytes): cmd + serial(4 BE) + x(2 BE) + y(2 BE) + z(1) +
// container(4 BE). Drops the lifted item; x=y=0xFFFF drops "anywhere" inside
// the target container, container 0xFFFFFFFF drops on the ground. Models
// Packet_BuildDropRequest @0x425410.
usize DropItem(u8* out, u32 serial, u16 x, u16 y, i8 z, u32 container);

// 0x13 Equip Item (10 bytes): cmd + serial(4 BE) + layer(1) + mobile(4 BE).
// Wears the lifted item on `mobile` at `layer`. Models Packet_BuildEquipRequest
// @0x425ac0. Layers: 1 = one-handed (weapon), 2 = two-handed (shield), 21 =
// backpack.
usize EquipItem(u8* out, u32 serial, u8 layer, u32 mobile);

// One vendor buy-list row sent inside the 0x3B buy packet. `layer` selects the
// vendor's shop container (0x1A = 26 stock list, 0x1B = 27 offered/resale);
// `serial` is the stock item's serial as listed by the 0x3C/0x74 shop dump.
struct VendorBuyEntry {
    u8  layer;
    u32 serial;
    u16 qty;
};

// 0x3B Vendor Buy (variable): cmd + len(2 BE) + vendorSerial(4 BE) + flag(0x02)
// + count×{ layer(1), serial(4 BE), qty(2 BE) }. Mirrors HandlePacket_OFFERACCEPT
// (server @0x00496C0F: numItems = (len-8)/7). An empty list (count 0) is a no-op.
usize VendorBuy(u8* out, u32 vendorSerial, const VendorBuyEntry* entries, usize count);

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
