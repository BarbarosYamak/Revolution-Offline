#pragma once
#include "uo/types.h"

#include <cstdlib>

// TREASURE HUNTING: a decoded map, a walk, a dig, a locked chest, its
// guardians, and the loot.
//
// Revolution (docs/REVOLUTION_WORLD_POPULATION.md:84-88,
// REVOLUTION_PRODUCTION_CHAINS.md:536-548):
//   * maps are levels 2-5; decoding needs Cartography and opening needs
//     Lockpicking at 40 / 60 / 80 / 100;
//   * unlocking wakes the guardians, and all must die before the chest opens;
//   * a pickaxe digs, no Mining needed; a sextant shows the distance;
//   * maps drop from monsters (dragons +200% from 14.05.2009) and from S.O.S
//     bottles (fishing, and from 18.05.2009 mining and lumberjacking).
// UNKNOWN: the guardians per level, the map item's name and graphic on this
// tree (f_treasure_map.scp lives only on the operator's runtime), the exact
// server lines for "decoded" and "dug". So nothing here waits for a message:
// every step is proven by STATE the client can see -- a map view with a pin,
// the player standing on the spot, a chest in the world, a chest's contents.
//
// Pure: the runner gathers the facts, this says what to do next.
namespace uo::treasure {

// Graphics. Map and chest graphics are the classic art ids; which ones this
// tree uses is UNKNOWN, so the lists are broad and a wrong guess only means a
// bot does not recognise its own map (it never invents one).
inline constexpr u16 kMapGraphics[] = {0x14EB, 0x14EC};
inline constexpr u16 kDigTools[] = {0x0F39, 0x0F3A, 0x0E86, 0x0E85};    // shovels, pickaxes
inline constexpr u16 kLockpicks[] = {0x14FB, 0x14FC, 0x14FD, 0x14FE};
inline constexpr u16 kChestGraphics[] = {0x0E40, 0x0E41, 0x0E42, 0x0E43, 0x0E7C, 0x09AB, 0x0E7D, 0x0E7E};

inline bool IsMap(u16 g) { return g == 0x14EB || g == 0x14EC; }

// Revolution: level 2..5 needs 40/60/80/100 in both Cartography and
// Lockpicking. Level 0 = not known: let the server decide.
inline i32 RequiredTenths(int level) {
    switch (level) {
        case 2: return 400; case 3: return 600; case 4: return 800; case 5: return 1000;
        default: return 0;
    }
}

// A pin's pixel on the map image -> the world tile it marks.
inline bool PinToWorld(i32 ulx, i32 uly, i32 lrx, i32 lry, i32 width, i32 height,
                       i32 px, i32 py, i32* x, i32* y) {
    if (width <= 0 || height <= 0 || lrx <= ulx || lry <= uly) return false;
    if (px < 0 || py < 0 || px > width || py > height) return false;
    *x = ulx + static_cast<i32>(static_cast<i64>(px) * (lrx - ulx) / width);
    *y = uly + static_cast<i32>(static_cast<i64>(py) * (lry - uly) / height);
    return true;
}

inline constexpr int kMaxDecodeTries = 5;
inline constexpr int kMaxDigTries = 6;
inline constexpr int kMaxOpenTries = 4;
inline constexpr int kMaxPickTries = 8;
inline constexpr double kMinHpToDig = 0.70;     // guardians come next

struct Sight {
    bool   haveMap = false;
    int    level = 0;               // 0 = unknown
    i32    cartographyTenths = 0, lockpickingTenths = 0;
    bool   pointKnown = false;
    i32    tilesToSpot = 0;
    bool   haveDigTool = false, haveLockpick = false;
    u32    chest = 0;               // a chest in the world at the spot
    bool   chestOpen = false;       // its contents have arrived
    i32    chestItemsLeft = 0;
    bool   hostilesNear = false;    // guardians (or anything else) in reach
    double hpFrac = 1.0;
    double weightFrac = 0.0;
    bool   busy = false;            // an action or trip is in flight
    int    decodeTries = 0, digTries = 0, openTries = 0, pickTries = 0;
};

enum class Step : u8 { Wait = 0, Decode, Travel, Dig, Open, Unlock, Loot, Done, GiveUp };

inline const char* StepName(Step s) {
    static const char* k[] = {"wait", "decode", "travel", "dig", "open", "unlock", "loot", "done", "give_up"};
    return k[static_cast<int>(s)];
}

struct Plan { Step step = Step::Wait; const char* reason = ""; };

inline Plan Decide(const Sight& s) {
    if (!s.haveMap && !s.chest) return {Step::GiveUp, "no treasure map carried"};
    if (s.hostilesNear) return {Step::Wait, "guardians first: the fight owns the tick"};
    if (s.busy) return {Step::Wait, "an action or trip is in flight"};
    const i32 need = RequiredTenths(s.level);
    if (!s.pointKnown && !s.chest) {
        if (need && s.cartographyTenths < need) return {Step::GiveUp, "Cartography too low for this map's level"};
        if (s.decodeTries >= kMaxDecodeTries) return {Step::GiveUp, "the map would not decode"};
        return {Step::Decode, "read the map"};
    }
    if (!s.chest) {
        if (s.tilesToSpot > 1) return {Step::Travel, "walk (or recall) to the marked spot"};
        if (!s.haveDigTool) return {Step::GiveUp, "no shovel or pickaxe to dig with"};
        if (s.hpFrac < kMinHpToDig) return {Step::GiveUp, "too hurt to face the guardians"};
        if (s.digTries >= kMaxDigTries) return {Step::GiveUp, "dug and found nothing"};
        return {Step::Dig, "dig at the spot"};
    }
    if (!s.chestOpen) {
        // Try the lid first; a locked chest does not open and then it is the
        // lockpick's turn. Alternate so neither can loop alone.
        if (s.openTries <= s.pickTries) {
            if (s.openTries >= kMaxOpenTries) return {Step::GiveUp, "the chest will not open"};
            return {Step::Open, "try the lid"};
        }
        if (!s.haveLockpick) return {Step::GiveUp, "the chest is locked and there is no lockpick"};
        if (need && s.lockpickingTenths < need) return {Step::GiveUp, "Lockpicking too low for this chest"};
        if (s.pickTries >= kMaxPickTries) return {Step::GiveUp, "the lock would not give"};
        return {Step::Unlock, "pick the lock"};
    }
    if (s.chestItemsLeft > 0 && s.weightFrac < 0.90) return {Step::Loot, "take the treasure"};
    return {Step::Done, s.chestItemsLeft > 0 ? "pack full: leave the rest" : "chest emptied"};
}

}  // namespace uo::treasure
