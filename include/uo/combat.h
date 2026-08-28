#pragma once

// M6 -- who a character may fight, and which of them is worth fighting.
//
// Protocol-free on purpose, exactly like uo/life.h: everything here is a pure
// function of what a HUMAN PLAYER can see on screen -- a name, a notoriety
// hue, a health BAR (a fraction, never a number of hit points), whether the
// thing is in war mode, and whether it has already hit us. No monster
// database, no true HitsMax, no server-side aggression state.
//
// The rules that decide legality are not guessed. They are read off this
// shard's own sphere.ini and Source-X source, and each one is cited where it
// is declared. Every character this project has lost so far died to a rule it
// did not model: M3.9 lost one to GuardsInstantKill after it swung at a blue
// farm animal.

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::combat {

// Source-X NOTO_TYPE, verbatim (src/common/sphereproto.h:540-550). These are
// the wire values in 0x78/0x77 and the only thing a client is told about
// another character's standing.
enum class Noto : u8 {
    Unknown       = 0,   // NOTO_INVALID -- the hue has not arrived yet
    Innocent      = 1,   // NOTO_GOOD        blue
    GuildSame     = 2,   // NOTO_GUILD_SAME  green
    Neutral       = 3,   // NOTO_NEUTRAL     gray
    Criminal      = 4,   // NOTO_CRIMINAL    gray
    GuildWar      = 5,   // NOTO_GUILD_WAR   orange
    Murderer      = 6,   // NOTO_EVIL        red
    Invulnerable  = 7,   // NOTO_INVUL       yellow
};

const char* NotoName(Noto n);

// This shard's crime settings. Defaults are READ VALUES, not conventions --
// runtime/sphere.ini lines 745-772. A different shard would need different
// numbers, which is why they are a struct and not constants in the code.
struct CrimeRules {
    // AttackingIsaCrime=1. Source-X only calls CheckCrimeSeen on an attack
    // when this is set AND the target reads NOTO_GOOD to us AND it has not
    // already aggressed us (CCharFight.cpp:1474).
    bool attackingIsACrime = true;

    // GuardsInstantKill=1. In a guarded region a criminal or murderer is
    // killed outright -- there is no fight to flee from and no health bar to
    // watch. This is the single most expensive rule on the shard for a bot.
    bool guardsInstantKill = true;

    // CriminalTimer=3 (minutes). How long the gray flag lasts once earned.
    i32 criminalMinutes = 3;

    // MurderMinCount=1. The FIRST reported murder turns the character red,
    // and MurderDecayTime=8*60*60 means it stays that way for eight hours of
    // play. On this shard there is no "one mistake" allowance.
    i32 murderMinCount = 1;
    i32 murderDecaySeconds = 8 * 60 * 60;
};

// The values above, as read from runtime/sphere.ini.
const CrimeRules& RevolutionCrimeRules();

// What a player can actually see about one nearby mobile, plus the two things
// we know from our own memory of the last few seconds: whether it is hitting
// us, and whether it hit us first.
struct Candidate {
    u32         serial = 0;
    std::string name;
    Noto        noto = Noto::Unknown;
    i32         dist = 0;

    // The health BAR. hpMax is the bar's own scale as the server sends it for
    // display, so hpCur/hpMax is a fraction a player reads by eye. Neither
    // number is treated as a monster's real strength anywhere below.
    i32  hpCur = -1, hpMax = -1;

    bool warMode     = false;   // drawn in war mode
    bool isPlayer    = false;   // a real player, as far as we can tell
    bool attackingMe = false;   // currently swinging at us
    bool aggressedMe = false;   // IT started it (Source-X MEMORY_AGGREIVED)
    bool isMyPet     = false;   // our own follower
};

// Where WE are standing, and what we already are. Legality depends on both.
struct Stance {
    bool inGuardedRegion = false;  // a guard will answer a call here
    bool iAmCriminal     = false;  // we are already flagged gray
    bool iAmMurderer     = false;  // we are already red
    i32  attackersOnMe   = 0;
};

enum class Legality : u8 {
    // Free to attack: no flag, no guard, no consequence beyond the fight.
    Lawful = 0,
    // Attacking would flag us criminal for CriminalTimer minutes.
    FlagsCriminal,
    // Attacking would flag us AND we are standing where guards answer, which
    // with GuardsInstantKill=1 is not a risk but a death.
    GuardKill,
    // Cannot be attacked at all (invulnerable), or must not be (our own pet,
    // a guildmate), or we simply do not know yet.
    Forbidden,
};

const char* LegalityName(Legality l);

struct Classification {
    Legality legality = Legality::Forbidden;
    // May we swing right now, under this policy?
    bool     engage = false;
    // 0..1, from what is visible only. Higher = more dangerous to us.
    double   threat = 0.0;
    // Why, in one printable line, the way the life layer's goals print.
    std::string reason;
};

// How willing this character is to fight. Comes from the profession's
// riskTolerance, so a mage and a swordsman genuinely differ.
struct EngagePolicy {
    double riskTolerance = 0.5;
    // Never start a fight that would flag us, even outside a guard zone. A
    // criminal flag follows the character across logouts, so this defaults to
    // refusing: the M3.9 loss began with a single swing at a blue.
    bool   acceptCriminalFlag = false;
    // Fight back when something is already on us, even if hitting back would
    // flag us. Self-defence against an aggressor is NOT a crime in Source-X
    // (CCharFight.cpp:1474 excludes MEMORY_AGGREIVED|MEMORY_HARMEDBY).
    bool   defendSelf = true;
    // Do not open a fight below this health fraction.
    double minHpFractionToOpen = 0.60;
    i32    maxEngageDistance = 10;
};

// The whole M6 legality question for one candidate. Pure: no clock, no I/O.
Classification Classify(const Candidate& c, const Stance& me,
                        const CrimeRules& rules, const EngagePolicy& policy,
                        double myHpFraction);

// The best thing to attack, or -1 if nothing should be attacked. Chooses the
// most threatening ENGAGEABLE candidate, because the thing already hitting us
// is the thing that kills us.
int ChooseTarget(const std::vector<Candidate>& candidates, const Stance& me,
                 const CrimeRules& rules, const EngagePolicy& policy,
                 double myHpFraction,
                 std::vector<Classification>* verdictsOut = nullptr);

}  // namespace uo::combat
