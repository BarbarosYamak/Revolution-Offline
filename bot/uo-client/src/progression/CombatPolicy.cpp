#include "uo/combat_policy.h"

namespace uo::combat {

const char* TacticName(Tactic t) {
    switch (t) {
        case Tactic::Fight:       return "fight";
        case Tactic::DrinkPotion: return "drink_potion";
        case Tactic::Disengage:   return "disengage";
        case Tactic::Bandage:     return "bandage";
        case Tactic::Flee:        return "flee";
        case Tactic::Rest:        return "rest";
        case Tactic::Count:       break;
    }
    return "?";
}

i32 HealthPercent(const Vitals& v) {
    if (v.hpNow < 0 || v.hpMax <= 0) return -1;
    const i32 pct = (v.hpNow * 100) / v.hpMax;
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

Tactic Decide(const Vitals& v) {
    const i32 pct = HealthPercent(v);

    // UNKNOWN HEALTH IS NOT GOOD HEALTH. If we are fighting and cannot see our
    // own health, the safe answer is to break contact and find out, not to keep
    // swinging and hope. Out of combat it costs nothing to wait.
    if (pct < 0) return v.inCombat ? Tactic::Disengage : Tactic::Rest;

    // Healthy: fight.
    if (pct > kPotionPercent) return Tactic::Fight;

    // A potion is instant, so it is the first thing to try -- it may end the
    // fight without conceding any ground. This is checked BEFORE disengaging on
    // purpose: retreating from a fight you could have won by drinking is its own
    // kind of failure.
    if (v.healPotions > 0) return Tactic::DrinkPotion;

    // No potion. Below the disengage line, stop fighting first: a bandage takes
    // ~3 seconds and standing still next to something that hits is how a bot
    // dies at 17 HP with bandages still in its pack.
    if (pct <= kDisengagePercent) {
        if (v.inCombat || v.enemyAdjacent) {
            // Nothing to heal with and still in contact -- running is the only
            // move left, and it is a real one. Fleeing is not failure; dying
            // with unused options is.
            if (v.bandages <= 0 && pct <= kFleePercent) return Tactic::Flee;
            return Tactic::Disengage;
        }
        if (v.bandages > 0) return Tactic::Bandage;
        return Tactic::Rest;
    }

    // Between the potion line and the disengage line with no potion: keep
    // fighting if still engaged -- the fight may be nearly over -- but bandage
    // the moment we are out of contact rather than walking away wounded.
    if (!v.inCombat && !v.enemyAdjacent && v.bandages > 0) return Tactic::Bandage;
    return Tactic::Fight;
}

bool ReadyToResume(const Vitals& v) {
    const i32 pct = HealthPercent(v);
    if (pct < 0) return false;   // unknown is never "ready"
    return pct >= kResumePercent;
}

} // namespace uo::combat
