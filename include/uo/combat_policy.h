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
//   Potions    REFERENCED BUT NOT DEFINED ON THIS SHARD. The alchemy menu names
//              i_potion_healless / i_potion_heal / i_potion_healgreat
//              (crafting/interface/def_alchemy.scp, category 4) and tm_loot.scp
//              hands new characters i_potion_heal -- but a search of the whole
//              runtime finds NO ITEMDEF for any of them. The only potion items
//              that exist are i_potion_bottle and the vats.
//
//              So DrinkPotion is correct policy and currently unreachable: it
//              is kept because the tactic is right and the world is what is
//              missing, and because a healPotions count that can only ever be
//              zero is honest in a way that deleting the branch would not be.
//              Recorded as runtime debt: alchemy category 4 cannot produce
//              anything, and the newbie-loot line that grants a heal potion
//              refers to an item that does not exist.
//
//              PRACTICAL CONSEQUENCE: on this shard today, surviving a fight
//              means bandages and disengagement. Nothing else is available to a
//              non-mage.
//
// That difference drives the whole policy: A POTION IS FOR MID-FIGHT, A BANDAGE
// IS FOR AFTER DISENGAGING. Reversing them gets a bot killed while it stands
// still for three seconds.
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
