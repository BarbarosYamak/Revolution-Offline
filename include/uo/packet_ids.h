#pragma once

#include "uo/types.h"

// Symbolic UO 2.0.7 packet command bytes. Only ones the new client
// actually constructs, consumes by name, or special-cases are listed —
// other opcodes flow through the dispatcher's "all others" path.

namespace uo {

enum class PacketId : u8 {
    // Outbound
    MoveRequest         = 0x02,
    SpeechAscii         = 0x03,
    AttackRequest       = 0x05,
    DoubleClick         = 0x06,
    TargetCursor        = 0x6C,   // bidirectional: server requests, client answers
    WarMode             = 0x72,
    SingleClick         = 0x09,
    MobNameQuery        = 0x98,
    PlayCharacter       = 0x5D,
    LoginRequest        = 0x80,
    SelectServer        = 0xA0,
    GameServerLogin     = 0x91,

    // Inbound (selected)
    Stats               = 0x11,
    LoginConfirm        = 0x1B,
    AsciiMessage        = 0x1C,
    DeleteObject        = 0x1D,
    DrawGamePlayer      = 0x20,
    MobAttributes       = 0x2D,
    GetPlayerStatus     = 0x34,
    PauseClient         = 0x33,
    Skills              = 0x3A,
    LoginComplete       = 0x55,
    ServerPingNullsub   = 0x73,   // server-initiated keepalive
    EntityStatus        = 0x77,
    UpdatePlayer        = 0x78,
    CharacterAnimation  = 0x6E,
    LoginDenied         = 0x82,
    ConnectToGameServer = 0x8C,
    ServerList          = 0xA8,
    UpdateMana          = 0xA2,
    UpdateStamina       = 0xA3,
    CharacterList       = 0xA9,
    UnicodeMessage      = 0xAE,
    DeathAnimation      = 0xAF,
    SupportedFeatures   = 0xB9,
    ClientVersionQuery  = 0xBD,
    ExtendedCommand     = 0xBF,
    LocalizedText       = 0xC1,
    ClientViewRange     = 0xC8,
    ServerPing          = 0xC9,
    GlobalQueueCount    = 0xCB,
    ClilocMessageAffix  = 0xCC,
};

inline u8 RawId(PacketId id) { return static_cast<u8>(id); }

}
