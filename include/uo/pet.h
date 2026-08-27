#pragma once

#include "uo/types.h"

#include <cstdint>

namespace uo::pet {

// ---------------------------------------------------------------------------
// M3.9 Phases 10-11 -- pet / combat-animal semantics.
//
// Primitives, not a Tamer AI. Nothing here decides to acquire, command or risk
// an animal; it encodes WHAT THE SERVER ACTUALLY ACCEPTS so a behaviour layer
// cannot invent vocabulary.
//
// EVERY WORD BELOW IS FROM THE ENGINE, NOT FROM UO FOLKLORE.
// CCharNPCPet.cpp:88-115 holds the word list; :117 dispatches it.
//
// TRANSPORT: pet commands are ORDINARY SPOKEN SPEECH -- 0x03
// (PacketSpeakReq, receive.cpp:290) or 0xAD (unicode, receive.cpp:2082), both
// funnelling into Event_Talk_Common (CClientEvent.cpp:1855). There is no pet
// packet. For each NPC in earshot the server checks either the exact prefix
// "ALL " (with the trailing space, case-insensitive, CClientEvent.cpp:1954) or
// the pet's own name (NPC_OnHearName, :1962), and only then -- and only if
// NPC_IsOwnedBy passes -- routes the rest to NPC_OnHearPetCmd (:1972).
//
// TURKISH VARIANTS DO NOT EXIST. Searched the engine and the whole of
// runtime/scripts: no saldir / takip / gel / dur anywhere. A Turkish shard did
// not translate its pet commands, and guessing otherwise would have produced a
// bot that talks to its animals in a language they do not speak.
//
// THE ONE THAT WOULD HAVE HUNG US: on THIS shard confirmations are SILENT.
// sphere.ini:686 sets SpeechPet=spk_pet, which makes NPC_OnPetCommand return
// early (CCharNPCPet.cpp:20-21), so the stock "Yes Master" / "Sorry" reply
// never comes. A primitive that waited for it would block forever. Success is
// observed instead as: the target cursor arriving (for targeted commands), and
// then the animal's behaviour changing.
// ---------------------------------------------------------------------------

enum class Command : u8 {
    Come,        // follow the speaker
    FollowTarget,// follow a targeted thing
    Stay,
    Stop,        // identical effect to Stay in the engine
    Kill,        // attack a targeted thing
    Attack,      // synonym the engine also accepts
    Guard,
    GuardMe,
    Release,
    Count,
};

// The exact words to speak, WITHOUT the "all " prefix.
const char* CommandWords(Command c);

// Does the server answer this command with a target cursor (0x6C)?
//
// kill / attack / follow / guard / friend / transfer / go are spoken-THEN-
// targeted: addTarget(CLIMODE_TARG_PET_CMD, ...) at CCharNPCPet.cpp:360-368,
// answered into OnTarg_Pet_Command (CClientTarg.cpp:1549). come / stay / stop /
// "guard me" act immediately with no cursor, so waiting for one would hang.
bool NeedsTarget(Command c);

// The broadcast prefix. Commands every owned pet in earshot.
inline constexpr const char* kAllPrefix = "all ";

// How far a pet can hear a command: NPCDistanceHear, which defaults to
// UO_MAP_VIEW_SIGHT = 14 (uofiles_macros.h:17, CServerConfig.cpp:215) and is
// left commented out in this runtime's sphere.ini:303. Line of sight is also
// required (CanSeeLOS, CClientEvent.cpp:1946).
inline constexpr i32 kHearingTiles = 14;

// Veterinary and Healing both run on a skill timer and both require the healer
// to be within two tiles (CCharSkill.cpp:2724-2794). The bandage is a plain
// double-click that raises a target cursor (CClientUse.cpp:407); the server
// picks VETERINARY over HEALING when the target is an NPC with TAMING > 0 and
// an animal-ish brain (CClientTarg.cpp:2007-2046).
inline constexpr i32 kVeterinaryTiles = 2;

// What a bot may legitimately do with an animal it owns.
enum class Role : u8 {
    Mount,
    CombatPet,
    PackAnimal,
    MountAndCombat,
    Count,
};

const char* RoleName(Role r);

// Minimal observable state. Deliberately no goals, no schedule, no personality.
struct OwnedAnimal {
    u32  serial = 0;
    std::uint16_t body = 0;
    Role role = Role::Mount;
    bool alive = true;
    bool mounted = false;      // on layer 25, so NOT a world mobile
    bool nearby = false;       // within kHearingTiles and visible
    i32  hpCur = -1;           // -1 = unknown; 0x77/0xA1 carry a ratio for foreign mobs
    i32  hpMax = -1;
    u32  commandedTarget = 0;
    i64  lastSeenMs = 0;
};

// Percent, or -1 when the server has not told us. NEVER guesses 100.
i32 HealthPercent(const OwnedAnimal& a);

// Below this a pet is in danger and a future behaviour layer should pull it out.
// No autonomous loop exists yet -- this is the number that loop will use.
inline constexpr i32 kDangerHealthPercent = 40;

// True when the animal is worth rescuing right now. A pet is expensive: a
// Nightmare costs 199.8 of a 700-point build, and death is permanent loss.
bool IsInDanger(const OwnedAnimal& a);

// Can a bandage legitimately be applied? Requires the animal alive, nearby and
// actually hurt -- bandaging a healthy pet wastes the bandage and the timer.
bool CanVeterinaryHeal(const OwnedAnimal& a, i32 distanceTiles);

// A MOUNTED ANIMAL IS NOT A MISSING ANIMAL.
//
// Sphere deletes the mobile and equips a mount item on layer 25, so a pet you
// are riding vanishes from every mobile scan. M3.7.1 lost four runs to this in
// four different disguises. Anything that reasons about "where is my pet"
// must ask this first.
bool IsUnobservableBecauseMounted(const OwnedAnimal& a);

}  // namespace uo::pet
