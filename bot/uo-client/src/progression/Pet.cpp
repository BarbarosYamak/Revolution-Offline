#include "uo/pet.h"

namespace uo::pet {

const char* CommandWords(Command c) {
    // Exactly the engine's own words (CCharNPCPet.cpp:88-115). Not paraphrased,
    // not translated, not "improved".
    switch (c) {
        case Command::Come:         return "come";
        case Command::FollowTarget: return "follow";
        case Command::Stay:         return "stay";
        case Command::Stop:         return "stop";
        case Command::Kill:         return "kill";
        case Command::Attack:       return "attack";
        case Command::Guard:        return "guard";
        case Command::GuardMe:      return "guard me";
        case Command::Release:      return "release";
        case Command::Count:        break;
    }
    return "";
}

bool NeedsTarget(Command c) {
    switch (c) {
        // Spoken, then the server raises a cursor.
        case Command::FollowTarget:
        case Command::Kill:
        case Command::Attack:
        case Command::Guard:
            return true;
        // Immediate. Waiting for a cursor here would hang forever.
        case Command::Come:
        case Command::Stay:
        case Command::Stop:
        case Command::GuardMe:
        case Command::Release:
        case Command::Count:
            return false;
    }
    return false;
}

const char* RoleName(Role r) {
    switch (r) {
        case Role::Mount:          return "MOUNT";
        case Role::CombatPet:      return "COMBAT_PET";
        case Role::PackAnimal:     return "PACK_ANIMAL";
        case Role::MountAndCombat: return "MOUNT_AND_COMBAT";
        case Role::Count:          break;
    }
    return "?";
}

i32 HealthPercent(const OwnedAnimal& a) {
    // -1 rather than an optimistic 100. The server sends a RATIO for foreign
    // mobiles, and "we have not been told" is a different fact from "healthy" --
    // conflating them is how a bot walks a dying pet into a second fight.
    if (a.hpCur < 0 || a.hpMax <= 0) return -1;
    const i64 pct = (static_cast<i64>(a.hpCur) * 100) / a.hpMax;
    return static_cast<i32>(pct);
}

bool IsInDanger(const OwnedAnimal& a) {
    if (!a.alive) return false;          // past saving, not in danger
    const i32 pct = HealthPercent(a);
    if (pct < 0) return false;           // unknown is not danger; it is unknown
    return pct <= kDangerHealthPercent;
}

bool CanVeterinaryHeal(const OwnedAnimal& a, i32 distanceTiles) {
    if (!a.alive) return false;
    if (a.mounted) return false;         // it is an item on layer 25, not a target
    if (!a.nearby) return false;
    if (distanceTiles > kVeterinaryTiles) return false;
    const i32 pct = HealthPercent(a);
    // Bandaging a healthy pet burns a bandage and a skill timer for nothing.
    return pct >= 0 && pct < 100;
}

bool IsUnobservableBecauseMounted(const OwnedAnimal& a) {
    return a.mounted;
}

}  // namespace uo::pet
