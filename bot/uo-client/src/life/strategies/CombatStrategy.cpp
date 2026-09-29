#include "uo/strategies/combat_strategy.h"

namespace uo::life {

const char* CombatStrategyName(CombatStrategyId s) {
    switch (s) {
        case CombatStrategyId::AvoidCombat: return "avoid";
        case CombatStrategyId::Melee:       return "melee";
        case CombatStrategyId::Ranged:      return "ranged";
        case CombatStrategyId::Mage:        return "mage";
        case CombatStrategyId::Tamer:       return "tamer";
    }
    return "?";
}

const char* CombatMoveName(CombatMove m) {
    switch (m) {
        case CombatMove::Disengage:  return "disengage";
        case CombatMove::CloseIn:    return "close in";
        case CombatMove::BackOff:    return "back off";
        case CombatMove::Swing:      return "swing";
        case CombatMove::Shoot:      return "shoot";
        case CombatMove::CastAttack: return "cast at it";
        case CombatMove::CastHeal:   return "heal myself";
        case CombatMove::Bandage:    return "bandage";
        case CombatMove::Meditate:   return "meditate";
        case CombatMove::CommandPet: return "set the pet on it";
        case CombatMove::Wait:       return "hold";
    }
    return "?";
}

namespace {

CombatDecision Move(CombatMove m, const char* why) {
    CombatDecision d;
    d.move = m;
    d.reason = why;
    return d;
}

// SHARED BY EVERY STRATEGY, because none of them is worth dying for.
//
// 0.32 is the only number here with evidence behind it: a character
// disengaged at roughly a third of its health in M3.9.1 and survived. Above
// that it is a judgement, and the judgement belongs to the character's own
// tuning rather than to its class.
bool ShouldBreakOff(const CombatSight& see, const CombatTuning& tune) {
    if (see.HpFraction() <= tune.fleeHpFraction) return true;
    // OUTNUMBERED. Each extra attacker past the first is worth a slice of
    // tolerance: a character happy to fight one thing at 40% is not happy to
    // fight three.
    if (see.attackersOnMe > 1) {
        // The multiplier reaches ZERO at riskTolerance 1.0, on purpose: a
        // character with maximum tolerance is one that crowds do not
        // frighten, and it should leave on its own health alone. The first
        // version used (1.5 - riskTolerance), which never reached zero, so
        // the bravest possible fighter still ran from three foes at 45%
        // health -- caught by strategy_combat rather than by a session.
        const double crowded =
            tune.fleeHpFraction + 0.15 * (see.attackersOnMe - 1) *
                                      (1.0 - tune.riskTolerance);
        if (see.HpFraction() <= crowded) return true;
    }
    return false;
}

}  // namespace

CombatDecision DecideCombat(CombatStrategyId strategy, const CombatSight& see,
                            const CombatTuning& tune) {
    // --- what everyone does first -----------------------------------------
    if (strategy == CombatStrategyId::AvoidCombat)
        return Move(CombatMove::Disengage,
                    "this life does not fight for a living");

    if (ShouldBreakOff(see, tune))
        return Move(CombatMove::Disengage,
                    "too hurt, or too outnumbered, to see this through");

    switch (strategy) {
        // -----------------------------------------------------------------
        case CombatStrategyId::Melee: {
            // Bandaging mid-fight is what a warrior does instead of drinking
            // -- and it is why Healing and Anatomy are in the build at all.
            if (see.HpFraction() < tune.healHpFraction && see.bandages > 0)
                return Move(CombatMove::Bandage,
                            "hurt, and there are bandages for exactly this");
            if (see.foeDistance > 1)
                return Move(CombatMove::CloseIn,
                            "a sword only reaches one tile");
            if (!see.armed)
                return Move(CombatMove::Wait, "nothing in hand to swing");
            return Move(CombatMove::Swing, "in reach, so swing");
        }

        // -----------------------------------------------------------------
        case CombatStrategyId::Ranged: {
            // THE BOW NEEDS BOTH HANDS. Closing is not a tactical choice for
            // an archer, it is the end of its damage -- which is also why the
            // catalogue gives archers no shield.
            if (see.foeDistance <= 1)
                return Move(CombatMove::BackOff,
                            "a bow is useless in somebody's face");
            if (see.HpFraction() < tune.healHpFraction && see.bandages > 0 &&
                see.foeDistance >= tune.preferredRange)
                return Move(CombatMove::Bandage,
                            "far enough out to patch up safely");
            if (see.foeDistance > tune.preferredRange + 2)
                return Move(CombatMove::CloseIn,
                            "out of range; close to the edge of it");
            if (!see.armed)
                return Move(CombatMove::Wait, "no bow in hand");
            return Move(CombatMove::Shoot, "at range, so shoot");
        }

        // -----------------------------------------------------------------
        case CombatStrategyId::Mage: {
            // Metal armour ends casting on this shard, which is why the
            // catalogue dresses a mage in cloth -- and why a mage that lets
            // something into melee has already lost the fight.
            if (see.foeDistance <= 1)
                return Move(CombatMove::BackOff,
                            "nothing may be allowed into melee with a caster");
            if (see.HpFraction() < tune.healHpFraction && see.mana > 0)
                return Move(CombatMove::CastHeal, "hurt, and mana to fix it");
            if (see.mana <= 0) {
                // OUT OF MANA IS NOT OUT OF THE FIGHT, but it is out of THIS
                // fight: meditating in contact is how a mage dies.
                if (see.foeDistance >= tune.preferredRange)
                    return Move(CombatMove::Meditate,
                                "empty, and far enough out to sit down");
                return Move(CombatMove::BackOff,
                            "empty, and too close to recover here");
            }
            if (see.foeDistance > tune.preferredRange + 4)
                return Move(CombatMove::CloseIn,
                            "beyond casting range; close a little");
            return Move(CombatMove::CastAttack, "at range with mana: cast");
        }

        // -----------------------------------------------------------------
        case CombatStrategyId::Tamer: {
            // THE PET FIGHTS. A tamer that trades blows is a tamer without a
            // pet shortly afterwards.
            if (!see.petAlive)
                return Move(CombatMove::Disengage,
                            "no pet, and a tamer is not a fighter");
            if (see.petHpFraction >= 0.0 && see.petHpFraction < 0.5 &&
                see.mana > 0)
                return Move(CombatMove::CastHeal, "the pet is what needs it");
            if (see.foeDistance <= 2)
                return Move(CombatMove::BackOff,
                            "let the pet hold it; stay out of reach");
            return Move(CombatMove::CommandPet, "set the pet on it");
        }

        case CombatStrategyId::AvoidCombat:
        default:
            break;
    }

    return Move(CombatMove::Wait, "no move for this strategy");
}

}  // namespace uo::life
