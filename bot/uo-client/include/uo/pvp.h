#pragma once
#include "uo/types.h"

#include <algorithm>
#include <cctype>
#include <string>

// PLAYER AGAINST PLAYER: the PK who picks a victim, the anti-PK who hunts
// reds, the guild warrior, and the alarm that ties them together.
//
// Owner rulings (.claude/agent-memory/revolution-god/): "PK activity = ambush
// AI only" and "Head Hunters: not needed yet" (living-world-chains.md,
// owner-rulings-2026-09-03-truthpack.md); "no fixed disengage" -- retreat is
// a per-fight judgement or PvP never happens (feedback-no-fixed-disengage.md).
//
// The SERVER enforces every consequence and the bot plays under them:
// sphere.ini AttackingIsaCrime=1, MurderMinCount=1 (one murder turns you red),
// MurderDecayTime=8h, GuardsInstantKill=1, GuardsOnMurderers=1,
// LootingIsaCrime=1. So a PK never ambushes inside guards (the guards would
// kill it), a murderer never walks into town, and attacking a red, a criminal
// or a guild-war enemy is lawful (combat.h Classify).
//
// Notoriety (combat.h): 1 innocent, 2 guild ally, 3 neutral, 4 criminal,
// 5 guild war enemy, 6 murderer, 7 invulnerable.
//
// UNKNOWN: Revolution's own PK culture numbers (how often, where). The weights
// below are DERIVED tuning, bounded so nobody attacks from full safety into a
// crowd.
namespace uo::pvp {

enum class Role : u8 { None = 0, PlayerKiller, AntiPk };

inline const char* RoleName(Role r) {
    return r == Role::PlayerKiller ? "pk" : r == Role::AntiPk ? "anti_pk" : "none";
}

// Tools in the hand of someone working -- the classic ambush victim.
inline bool IsWorkTool(u16 g) {
    switch (g) {
        case 0x0E85: case 0x0E86:          // pickaxes
        case 0x0F39: case 0x0F3A:          // shovels
        case 0x0F43: case 0x0F44:          // hatchets
        case 0x0DBF: case 0x0DC0:          // fishing poles
        case 0x13E3: case 0x13E4:          // smith hammers
        case 0x0FBB: case 0x0FBC:          // tongs
        case 0x1034: case 0x1035:          // saws
            return true;
        default: return false;
    }
}
inline bool IsSpellbook(u16 g) { return g == 0x0EFA; }

// Is this notoriety a lawful player target (no criminal flag for hitting it)?
inline bool LawfulTarget(u8 noto) { return noto == 4 || noto == 5 || noto == 6; }

struct Self {
    double hpFrac = 1.0;
    i32    bandages = 0;
    double nerve = 0.5;          // life::Nerve: profession + persona
    bool   guardedHere = false;  // standing in a guarded region
    bool   iAmRed = false;
};

struct Target {
    u32  serial = 0;
    u8   noto = 0;
    i32  dist = 99;
    i32  hpPct = -1;             // -1 = unknown
    u16  hand1 = 0, hand2 = 0;   // what they hold
    bool mounted = false;
    i32  playersNearTarget = 0;  // OTHER players within 8 tiles of them (not us)
    i32  myAlliesNear = 0;       // friends/party within 8 tiles of us
    i32  hostilePlayersNear = 0; // other reds/criminals near us
    bool guardedThere = false;
    bool friendOfOurs = false;
    bool beatUsBefore = false;   // our own memory: this one killed or routed us
    bool attackingInnocent = false;  // it is hitting a blue player right now
};

inline constexpr i32 kMaxEngageTiles = 14;
inline constexpr double kMinHpToStart = 0.80;
inline constexpr i32 kMinBandages = 5;

// ---- the PK: is this a victim worth ambushing? --------------------------------
inline double VictimScore(const Target& t, const Self& me) {
    if (t.noto != 1 && t.noto != 3) return 0.0;               // innocents and neutrals only
    if (t.friendOfOurs || t.guardedThere || me.guardedHere) return 0.0;
    if (t.dist > kMaxEngageTiles) return 0.0;
    if (me.hpFrac < kMinHpToStart || me.bandages < kMinBandages) return 0.0;
    double s = 0.5;
    const bool working = IsWorkTool(t.hand1) || IsWorkTool(t.hand2);
    const bool armed = (t.hand1 && !working && !IsSpellbook(t.hand1)) || (t.hand2 && !IsWorkTool(t.hand2));
    if (working) s += 0.25;                                     // a miner at the rock
    if (armed) s -= 0.20;                                       // a fighter fights back
    if (IsSpellbook(t.hand1) || IsSpellbook(t.hand2)) s -= 0.10; // a mage recalls away or hits hard
    if (t.mounted) s -= 0.10;                                   // may simply ride off
    if (t.hpPct >= 0) s += 0.20 * (1.0 - t.hpPct / 100.0);      // already hurt
    s -= 0.30 * t.playersNearTarget;                            // witnesses, helpers
    if (t.beatUsBefore) s -= 0.40;
    return std::max(0.0, std::min(1.0, s));
}

// A bolder PK needs less of an opening. Nerve 0.85 (the pk profession) ->
// 0.51; a timid 0.3 PK would want 0.73.
inline double AmbushThreshold(double nerve) { return 0.85 - 0.40 * nerve; }

inline bool ShouldAmbush(const Target& t, const Self& me) {
    return VictimScore(t, me) >= AmbushThreshold(me.nerve);
}

// ---- the anti-PK / guild warrior: is this red, criminal or war enemy worth
// engaging? Lawful targets only; never alone into a gang.
inline double HuntScore(const Target& t, const Self& me) {
    if (!LawfulTarget(t.noto) || t.friendOfOurs) return 0.0;
    if (t.dist > kMaxEngageTiles || me.hpFrac < kMinHpToStart || me.bandages < kMinBandages) return 0.0;
    if (me.nerve < 0.45) return 0.0;                            // not everyone is a hero
    double s = 0.40;
    if (t.noto == 6) s += 0.10;                                 // a murderer
    if (t.attackingInnocent) s += 0.30;                         // a rescue
    s += 0.10 * std::min(3, t.myAlliesNear);
    s -= 0.35 * t.hostilePlayersNear;                           // his friends
    if (t.hpPct >= 0) s += 0.15 * (1.0 - t.hpPct / 100.0);
    if (t.beatUsBefore) s -= 0.25;
    return std::max(0.0, std::min(1.0, s));
}

inline bool ShouldHunt(const Target& t, const Self& me) {
    return HuntScore(t, me) >= 0.85 - 0.40 * me.nerve;
}

// ---- break off a PvP fight? A judgement, never a constant ------------------------
// Stays while the exchange is going our way; leaves when losing the race,
// outnumbered, or out of bandages. `foeHpPct` -1 = unknown.
inline bool ShouldBreakOff(double myHpFrac, i32 foeHpPct, double nerve, i32 alliesNear,
                           i32 foesNear, i32 bandages) {
    const double me = myHpFrac * 100.0;
    if (me < 15.0) return true;                                // nobody fights to the last hit point
    double line = 45.0 - 30.0 * nerve;                         // nerve 0.85 -> ~20%, 0.5 -> 30%
    line += 12.0 * std::max(0, foesNear - 1 - alliesNear);     // outnumbered
    if (bandages <= 0) line += 10.0;
    if (foeHpPct >= 0 && foeHpPct + 15 < me) line -= 10.0;     // winning the race
    return me < line;
}

// ---- the alarm -------------------------------------------------------------------
// "PK var!" -- what a Turkish player shouts when attacked. The victim says it;
// anti-PKs within earshot answer it.
inline constexpr const char* kAlarmLine = "PK var! yardim!";

inline bool IsAlarm(const std::string& text) {
    std::string t;
    for (unsigned char c : text) t.push_back(static_cast<char>(std::tolower(c)));
    return t.find("pk var") != std::string::npos || t.find("pk geldi") != std::string::npos ||
           t.find("yardim") != std::string::npos || t.find("help pk") != std::string::npos ||
           t == "pk!" || t == "pk" || t.find("red var") != std::string::npos;
}

}  // namespace uo::pvp
