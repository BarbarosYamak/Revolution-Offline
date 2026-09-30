#pragma once
#include "uo/types.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

// HUNTING AS A PARTY: how big a group, where it goes, which monster everyone
// hits, who heals whom, whose turn it is to loot, and what each member's job
// is. Pure: the runner gathers the facts (the party roster, health bars,
// positions, what the leader said) and does what this decides.
//
// The words are ordinary party speech a Turkish group would use -- "hedef"
// is "target", "ganimet" is "loot" -- spoken aloud, so every member decides
// from what it HEARD and SAW, never from another bot's memory.
//
// UNKNOWN: Revolution's dungeon spawns per level. The ground choice below
// only sends groups to a dungeon's FIRST level and only with the skill and
// numbers that make a graveyard look too easy; the route planner or the
// danger memory still turns them back.
namespace uo::party {

inline constexpr int kMaxGroup = 5;              // leader + 4 (a UO party holds 10; a hunt is smaller)
inline constexpr i64 kGatherMs = 60000;          // leader waits this long for more after the first
inline constexpr i64 kFocusMs = 30000;           // a called target stays called this long
inline constexpr int kBandageHealBelow = 70;     // % -- a melee member bandages a friend below this
inline constexpr int kSpellHealBelow = 60;       // % -- a caster heals by spell below this

// A sociable character wants a bigger group. 2..5.
inline int DesiredGroupSize(i32 sociability) {
    return std::min(kMaxGroup, 2 + std::max(0, sociability) / 30);
}

// ---- where ------------------------------------------------------------------------
enum class Ground : u8 { Graveyard = 0, Despise, Covetous };

inline const char* GroundName(Ground g) {
    return g == Ground::Despise ? "Despise" : g == Ground::Covetous ? "Covetous" : "graveyard";
}
// The atlas region a dungeon trip heads for: always the FIRST level.
inline const char* GroundRegion(Ground g) {
    return g == Ground::Despise ? "a_despise_level_1_1" : g == Ground::Covetous ? "a_covetous_level_1_1" : "";
}
// Skill needed to go at all, counted with the group: two members stand in for
// 5.0 each of skill, so a four-man group can do what one veteran does.
inline i32 GroundMinTenths(Ground g) {
    return g == Ground::Covetous ? 900 : g == Ground::Despise ? 700 : 0;
}
inline Ground ChooseGround(i32 bestFightTenths, int groupSize) {
    const i32 effective = bestFightTenths + 50 * std::max(0, groupSize - 1);
    if (effective >= GroundMinTenths(Ground::Covetous) && groupSize >= 3) return Ground::Covetous;
    if (effective >= GroundMinTenths(Ground::Despise)) return Ground::Despise;
    return Ground::Graveyard;
}
inline bool GroundSuits(Ground g, i32 bestFightTenths) {
    // A joiner may be weaker than the leader, but not far below the bar.
    return bestFightTenths + 100 >= GroundMinTenths(g);
}

// "Anyone for a Despise hunt? Meet here." The graveyard wording is the old
// phrase, unchanged, so older characters still hear it.
inline std::string Invitation(Ground g) {
    return std::string("Anyone for a ") + GroundName(g) + " hunt? Meet here.";
}
inline bool ParseInvitation(const std::string& text, Ground* g) {
    for (Ground c : {Ground::Graveyard, Ground::Despise, Ground::Covetous})
        if (text == Invitation(c)) { *g = c; return true; }
    return false;
}

// ---- which monster ------------------------------------------------------------------
inline std::string FocusCall(const std::string& monster) { return "hedef: " + monster; }
inline bool ParseFocusCall(const std::string& text, std::string* monster) {
    static const std::string kPrefix = "hedef: ";
    if (text.size() <= kPrefix.size() || text.compare(0, kPrefix.size(), kPrefix) != 0) return false;
    *monster = text.substr(kPrefix.size());
    return true;
}

struct Seen { std::string name; i32 x = 0, y = 0; };

// The called monster nearest the leader; failing a call, whatever stands in
// the leader's reach while the leader is fighting. -1 = nothing to focus.
inline int PickFocus(const std::vector<Seen>& hostiles, const std::string& called,
                     i32 leaderX, i32 leaderY, bool leaderFighting) {
    int best = -1;
    i32 bestD = 1 << 30;
    for (usize i = 0; i < hostiles.size(); ++i) {
        const i32 d = std::max(std::abs(hostiles[i].x - leaderX), std::abs(hostiles[i].y - leaderY));
        const bool match = !called.empty() ? hostiles[i].name == called : (leaderFighting && d <= 2);
        if (match && d < bestD) { bestD = d; best = static_cast<int>(i); }
    }
    return best;
}

// ---- who heals whom -------------------------------------------------------------------
enum class Role : u8 { Tank = 0, Striker, Healer };
inline const char* RoleName(Role r) { return r == Role::Healer ? "healer" : r == Role::Striker ? "striker" : "tank"; }

struct Member { u32 serial = 0; i32 hpPct = -1; i32 dist = 99; };

// The friend to patch up now, or 0. A healer (a caster) reaches by spell and
// acts earlier in the fight's priorities; anyone else bandages at arm's length.
inline u32 HealTarget(const std::vector<Member>& members, Role myRole, bool haveBandages, bool canCastHeal) {
    u32 best = 0;
    i32 bestHp = 101;
    for (const Member& m : members) {
        if (m.hpPct < 0) continue;
        const bool bySpell = canCastHeal && m.dist <= 10 && m.hpPct < kSpellHealBelow;
        const bool byBandage = haveBandages && m.dist <= 1 && m.hpPct < kBandageHealBelow;
        if (!(bySpell || byBandage)) continue;
        if (myRole != Role::Healer && !byBandage) continue;
        if (m.hpPct < bestHp) { bestHp = m.hpPct; best = m.serial; }
    }
    return best;
}

// ---- whose turn to loot ------------------------------------------------------------
// Kills are looted in turn round the party, in roster order, so every member
// gets a share without anyone counting coins: the Nth kill the group made
// belongs to member N mod size. Everyone counts the same kills because
// everyone hits the same monster (focus fire).
inline bool MyLootTurn(int partyKills, int myIndex, int groupSize) {
    if (groupSize <= 1) return true;
    return ((partyKills % groupSize) + groupSize) % groupSize == myIndex;
}

// ---- who does what ------------------------------------------------------------------
// Melee holds the monster, an archer shoots from range, a caster heals first
// and fights second. `strategy`: 0 melee, 1 ranged, 2 mage (combat strategy).
inline Role RoleFor(int strategy) { return strategy == 2 ? Role::Healer : strategy == 1 ? Role::Striker : Role::Tank; }
// How close a member keeps to the leader while following.
inline i32 FollowDistance(Role r) { return r == Role::Tank ? 2 : 4; }
// A striker or healer holds its first blow until the called target is engaged,
// so the tank takes the monster's attention.
inline bool MayOpen(Role r, bool leaderEngaged) { return r == Role::Tank || leaderEngaged; }

// ---- who invites -----------------------------------------------------------------------
// A hunter's free time goes to hunting: two sessions in three it asks for a
// hunt, the third it spars or trains. Deterministic per character and session.
inline bool PrefersHunt(bool wantsToHunt, u32 serial, i32 sessions) {
    return wantsToHunt && (serial + static_cast<u32>(sessions)) % 3 != 0;
}

}  // namespace uo::party
