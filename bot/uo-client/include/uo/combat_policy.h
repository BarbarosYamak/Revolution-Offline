// ---------------------------------------------------------------------------
// Combat survival policy (M3.9).
//
// WHY THIS EXISTS
//
// Every fighter this project has run fought to the death, because nothing ever
// told it to stop. The logs are unambiguous: RevolutionSpar was beaten to death
// by three undead in 27 seconds without ever disengaging; a later fighter traded
// blows with a wolf down to 17 HP and kept swinging. A bot that cannot decide to
// break off is not a fighter, it is a casualty.
//
// The asymmetry that gave it away: pet.h already models when an ANIMAL is in
// danger (kDangerHealthPercent) and what to do about it. Nothing modelled the
// character's own danger. We looked after the pet and not the player.
//
// WHAT THE SHARD ACTUALLY OFFERS, read from the runtime rather than assumed:
//
//   Bandages   SKILL 17 Healing, DELAY=3.0,1.0 and EFFECT=2.0,20.0
//              (runtime/scripts/skills/skill17_healing.scp). So a bandage takes
//              ~3 seconds and restores 2-20 HP. It is also NOT startable from
//              the skill list -- SKILL_HEALING is absent from Event_Skill_Use,
//              so a bandage must be DOUBLE-CLICKED (M2 established this the
//              hard way). Three seconds is a long time with something hitting
//              you, and the heal can be interrupted.
//   Potions    REAL, and craftable by an ordinary alchemist. They are defined
//              in items/i_provisions_potions.scp as
//                i_potion_HealLess   ALCHEMY  0.1, 1 ginseng, heals s_heal 50
//                i_potion_Heal       ALCHEMY 15.1, 3 ginseng, s_greater_heal 60
//                i_potion_HealGreat  ALCHEMY 55.1, 7 ginseng, s_greater_heal 100
//              all ID=i_bottle_yellow, TYPE=t_potion, consumed to an empty
//              bottle (TDATA1). A potion is a double-click and takes effect at
//              once.
//
//              A CORRECTION WORTH KEEPING: an earlier version of this header
//              stated flatly that no heal potion existed on this shard, and a
//              commit message repeated it. That was wrong. These ITEMDEFs put
//              the defname in the SECTION HEADER -- "[ITEMDEF i_potion_Heal]" --
//              rather than on a "DEFNAME=" line, so a search for
//              "^DEFNAME=i_potion_heal" found nothing and the absence was read
//              as evidence. Fifty-eight items carry TYPE=t_potion. Searching for
//              one spelling of a name and concluding the thing does not exist is
//              the same mistake as reading a body id out of generic UO
//              documentation, which this project has now made six times.
//
// THE THRESHOLDS BELOW ARE DERIVED, NOT REVOLUTION-DOCUMENTED. No archive source
// states what health a Revolution player retreats at. They are deliberately
// conservative and named so they can be tuned in one place, and they are marked
// DERIVED rather than dressed up as fidelity -- the same standard this project
// applied to the stat cap.
// ---------------------------------------------------------------------------
#pragma once

#include "uo/types.h"

namespace uo::combat {

// What the bot should do next. Ordered roughly by urgency.
enum class Tactic : u8 {
    Fight = 0,      // healthy enough; keep swinging
    DrinkPotion,    // hurt, but a potion is instant and costs no ground
    Disengage,      // leave war mode and break contact -- healing here is fatal
    Bandage,        // out of contact and hurt: 3 seconds is affordable now
    Flee,           // hurt, nothing to heal with, still in contact
    Rest,           // safe and hurt, with nothing to heal with: wait it out
    Count
};

const char* TacticName(Tactic t);

// DERIVED thresholds, percent of max health.
//
// Drinking earlier than disengaging is deliberate: a potion is free in time, so
// it is the cheap first answer, and it may end the fight without giving ground.
inline constexpr i32 kPotionPercent    = 60;  // sip at/below this
inline constexpr i32 kDisengagePercent = 35;  // stop fighting at/below this
inline constexpr i32 kFleePercent      = 20;  // no heals left: run
inline constexpr i32 kResumePercent    = 80;  // healed enough to re-engage

// A bandage takes about this long on this shard (SKILL 17 DELAY=3.0), which is
// why it is never the answer while an enemy is adjacent.
inline constexpr i32 kBandageSeconds = 3;

struct Vitals {
    i32  hpNow = -1;          // -1 = not yet known
    i32  hpMax = -1;
    bool inCombat = false;    // war mode / an active combat target
    bool enemyAdjacent = false;
    int  healPotions = 0;
    int  bandages = 0;
    i32  healingTenths = 0;   // bandages still work untrained, just worse
};

// Percent of max health, or -1 when it is not known.
//
// NEVER returns a cheerful default. pet.h learned this first: a false 100 reads
// as "fine" and is the one wrong answer that gets something killed. An unknown
// health is a reason to be careful, not a reason to fight on.
i32 HealthPercent(const Vitals& v);

// The decision. Pure, so it is testable without a shard.
Tactic Decide(const Vitals& v);

// Is it safe to go back to fighting?
bool ReadyToResume(const Vitals& v);

} // namespace uo::combat
