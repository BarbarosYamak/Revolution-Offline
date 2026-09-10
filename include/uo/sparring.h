#pragma once
#include "uo/types.h"

namespace uo::sparring {
// Conservative training policy, not shard balance changes. Full iron armour,
// short rounds and early stops leave room for an in-flight server swing.
inline constexpr i32 kStartPercent = 90;
inline constexpr i32 kStopPercent = 60;
inline constexpr i64 kRoundMs = 5000;
inline constexpr i64 kHealthFreshMs = 3000;
inline bool PoisonReceiver(i32 healing, i32 anatomy, i32 bandages) {
    return healing > 600 && anatomy > 600 && bandages >= 10;
}
inline bool TrainingWeapon(u16 graphic) {
    return graphic == 0 || graphic == 0x0F51 || graphic == 0x0F52 ||
           graphic == 0x13B3 || graphic == 0x13B4;
}
inline bool IronArmour(u16 graphic) {
    switch (graphic) {
        case 0x1408: case 0x1409: case 0x140A: case 0x140B: case 0x140C: case 0x140D:
        case 0x140E: case 0x140F: case 0x1410: case 0x1411: case 0x1412: case 0x1413:
        case 0x1414: case 0x1415: case 0x13BB: case 0x13BE: case 0x13BF: return true;
        default: return false;
    }
}
inline bool HealthAllows(i32 hp, i32 max, i32 threshold) {
    return max > 0 && hp > 0 && static_cast<i64>(hp) * 100 >= static_cast<i64>(max) * threshold;
}
} // namespace uo::sparring
