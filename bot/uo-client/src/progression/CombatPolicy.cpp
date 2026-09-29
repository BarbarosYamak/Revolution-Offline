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

    // CONTACT IS NOT WAR MODE. A character who never swings back is still
    // being hit, and this policy used to read that as peace: v.inCombat is
    // war mode and v.enemyAdjacent needs a chosen target, so a crafter with
    // no weapon skill had both false all the way down her health bar.
    const bool contact = v.inCombat || v.enemyAdjacent || v.hostilesNear > 0;
    // Nothing in sight, and nothing has been in sight for long enough to
    // believe it. See kRestAllClearSeconds for where the number comes from.
    const bool allClear = v.hostilesNear <= 0 &&
                          v.quietSeconds >= kRestAllClearSeconds;

    // UNKNOWN HEALTH IS NOT GOOD HEALTH. If we are fighting and cannot see our
    // own health, the safe answer is to break contact and find out, not to keep
    // swinging and hope. Out of combat it costs nothing to wait.
    if (pct < 0) return contact ? Tactic::Disengage : Tactic::Rest;

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
        if (contact) {
            // Nothing to heal with and still in contact -- running is the only
            // move left, and it is a real one. Fleeing is not failure; dying
            // with unused options is.
            if (v.bandages <= 0 && pct <= kFleePercent) return Tactic::Flee;
            return Tactic::Disengage;
        }
        // Out of contact but not yet out of danger: keep breaking away. A
        // three-second bandage or a sit-down inside the window something can
        // walk back into is how the last two minutes of Odessa's life went.
        if (!allClear) return Tactic::Flee;
        if (v.bandages > 0) return Tactic::Bandage;
        return Tactic::Rest;
    }

    // Between the potion line and the disengage line with no potion: keep
    // fighting if still engaged -- the fight may be nearly over -- but bandage
    // the moment we are out of contact rather than walking away wounded.
    if (!contact && allClear && v.bandages > 0) return Tactic::Bandage;
    return Tactic::Fight;
}

bool ReadyToResume(const Vitals& v) {
    const i32 pct = HealthPercent(v);
    if (pct < 0) return false;   // unknown is never "ready"
    return pct >= kResumePercent;
}

} // namespace uo::combat
