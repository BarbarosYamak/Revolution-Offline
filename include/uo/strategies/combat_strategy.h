#pragma once

// ---------------------------------------------------------------------------
// COMBAT STRATEGIES -- the one place where archetypes genuinely differ
// (docs/BOT_ARCHITECTURE.md sections 21 and 54).
//
// EVERYTHING ELSE IN THIS PROJECT IS DATA. Seventeen professions are records,
// and exactly two archetype-conditional branches exist in the whole of
// src/life/. That is the property most worth not losing, and this file is the
// one deliberate exception to it -- because a mage and a warrior in a fight
// are not the same behaviour with different numbers:
//
//   WARRIOR   close the distance, stay in it, swing, bandage, chase, retreat
//   MAGE      keep the distance, manage mana, cast, kite, heal, meditate
//   RANGED    keep the distance and shoot; the bow needs BOTH hands, so
//             there is no shield and closing is a mistake, not a choice
//   TAMER     the pet fights; command it, stay out of it, heal it
//   AVOID     a crafter's answer to a fight is to leave
//
// Those are different ALGORITHMS. Trying to express them as fields produces a
// field per verb and a policy nobody can read.
//
// AND STILL NOT A CLASS PER PROFESSION. The profession record names which
// strategy it uses; the strategy knows nothing about miners or scribes. Five
// strategies serve seventeen professions today and would serve a hundred.
//
// WHAT THIS IS NOT: it does not decide whether a fight is LEGAL. That is
// combat/Targeting.cpp -- Classify, ChooseTarget, ChoosePrey -- which already
// answers "may I hit this" with the crime rules and 47 tests behind it. This
// answers only "the fight is on and lawful; what do I do this tick".
//
// Protocol-free: numbers in, a move out. ctest reaches it directly.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

enum class CombatStrategyId : u8 {
    // A crafter's answer to a fight is to leave it. Not cowardice: a tailor
    // that trades blows is a tailor that loses its tools.
    AvoidCombat = 0,
    Melee,
    Ranged,
    Mage,
    Tamer,
};

const char* CombatStrategyName(CombatStrategyId s);

// What a player can actually see of a fight. No monster tables, no server
// internals -- the same discipline Targeting.cpp holds to.
struct CombatSight {
    i32    hp = 0;
    i32    hpMax = 1;
    i32    mana = 0;
    i32    manaMax = 0;
    // Distance to the thing being fought, in tiles.
    i32    foeDistance = 0;
    // How much of the foe's bar is left, 0..1, or -1 when it has not been
    // shown to us yet.
    double foeHpFraction = -1.0;
    i32    attackersOnMe = 0;
    i32    bandages = 0;
    bool   petAlive = false;
    double petHpFraction = -1.0;
    // Is a weapon/spell actually usable right now?
    bool   armed = false;

    double HpFraction() const {
        return hpMax > 0 ? static_cast<double>(hp) / hpMax : 0.0;
    }
    double ManaFraction() const {
        return manaMax > 0 ? static_cast<double>(mana) / manaMax : 0.0;
    }
};

// How this particular character fights, from its profile rather than its
// class. Two warriors may differ here.
struct CombatTuning {
    // Below this, disengage. 0.32 is not a guess: a character disengaged at
    // ~32% in M3.9.1 and lived, which is the only evidence this project has.
    double fleeHpFraction = 0.32;
    // Below this, patch up rather than press on.
    double healHpFraction = 0.80;
    // The range a ranged fighter wants to keep, and the range a mage does.
    i32    preferredRange = 6;
    // 0 = flee everything, 1 = stand and fight. Personality, per §31.
    double riskTolerance = 0.5;
};

enum class CombatMove : u8 {
    Disengage = 0,   // break off: too hurt, or outnumbered past tolerance
    CloseIn,         // walk toward the foe
    BackOff,         // walk away to restore range
    Swing,           // melee attack
    Shoot,           // ranged attack
    CastAttack,
    CastHeal,
    Bandage,
    Meditate,        // out of mana and out of contact
    CommandPet,
    Wait,            // in position, nothing else to do this tick
};

const char* CombatMoveName(CombatMove m);

struct CombatDecision {
    CombatMove  move = CombatMove::Wait;
    const char* reason = "";
};

CombatDecision DecideCombat(CombatStrategyId strategy, const CombatSight& see,
                            const CombatTuning& tune);

}  // namespace uo::life
