#pragma once
// Novice engagement policy -- how a low-skill, low-hp fighter opens and
// breaks off a fight. Pure: ints and doubles only, no Client, no Observation,
// no world model, so tests/novice_engage.cpp links it with nothing.
//
// WHY THIS EXISTS. Hector (fencer, 51 hp, Fencing 50.7, starter leather) died
// twice in the WEAK band of the Britain graveyard on 2026-09-06, both times
// against a group, both times with the flee interrupt firing after the fight
// was already lost:
//
//   Trace A -- artifacts/wave2_observation_2026-09-06.md, "02:16 -- DEATH":
//     two c_skeleton, hp 51 -> 34 -> 27 -> (potion) 41 -> 32 -> 30 ->
//     (potion) -> 13%. interrupt=FLEE at 02:16:23 "HP 25%; 2 attacker(s);
//     bail at 30%"; dead at 02:16:24, ONE SECOND after the decision.
//
//   Trace B -- run_gates/g_Hector.console.txt 23:36:39-23:38:10, Skeleton +
//     Skeleton + Spectre at 1389,1505 (sphere2026-09-06.log
//     "23:38 ... P'Hector' was killed by"):
//       23:37:21.5  51/51        23:38:01.8  38/51  (-13 over 5.0 s)
//       23:37:26.6  46/51 (-5)   23:38:02.4  31/51  (-7  over 0.6 s)
//       23:37:31.6  42/51 (-4)   23:38:07.5  21/51  (-10 over 5.1 s)
//       23:37:56.8  51/51 potion 23:38:10.6  DEAD   (-21 over 3.1 s)
//     "bail at 22%" all through -- 11 hp of 51, i.e. under two landed blows.
//
// THE TWO NUMBERS BELOW ARE READ OFF THOSE ROWS. Nothing here is a guess at a
// Sphere damage formula; the shard's damage-after-armour rule and this
// character's AR are both still UNKNOWN (artifacts/hunt_tier_gate_2026-09-06.md
// section 3), so the policy is stated in observed hp lost per exchange.
namespace uo::life::novice {

// HP TAKEN PER LANDED BLOW, weak band vs starter leather.
//   Trace B 23:38:01.8 -> 23:38:02.4: 7 hp in 0.6 s -- one blow, isolated.
//   Trace A 51 -> 34 in one poll with two attackers: 8.5 hp each.
// Round up to the larger of the two so the flee line is never optimistic.
inline constexpr int kHitDamage = 8;

// HOW MANY OF THOSE BLOWS THE RETREAT ITSELF HAS TO SURVIVE, per hostile that
// can reach us. A bail decision is acted on up to one observation poll late:
// the survival tick reschedules 2 s out and the hp watchdog reports every
// ~5 s. Both deaths measure exactly one such window between the last decision
// and the corpse -- trace A fled at 13 hp and died 1 s later, trace B was last
// seen at 21 hp and died 3.1 s later -- so one window is what killed him and
// two is the smallest margin the evidence supports.
inline constexpr int kHitsToLiveOnRetreat = 2;

// The hp floor a retreat needs against `attackers` things that can reach us.
// 1 attacker = 16 hp, 2 = 32, 3 = 48. For Hector's 51-hp bar that reads
// 31% / 63% / 94%: fight one, leave two, never stand in three -- which is the
// same conclusion the owner's "one target at a time" rule reaches from the
// other side.
inline constexpr int RetreatFloorHp(int attackers) {
    return (attackers < 1 ? 1 : attackers) * kHitDamage * kHitsToLiveOnRetreat;
}

// As a fraction of this character's own bar, capped where the existing
// survival code already caps (Survive.cpp: bailAt is clamped to 0.90).
inline double RetreatFloorFraction(int attackers, int hpMax) {
    if (hpMax <= 0) return 0.0;
    const double f = static_cast<double>(RetreatFloorHp(attackers)) /
                     static_cast<double>(hpMax);
    return f > 0.90 ? 0.90 : f;
}

// WHO THIS APPLIES TO. Both deaths were the same profile: best weapon skill
// 50.7 (507 tenths) and a 51-point health bar. These bounds are the measured
// profile rounded up one step -- a policy choice about where the novice band
// ends, NOT a measured cliff, and deliberately generous so the character that
// actually died is inside it by a wide margin. A veteran keeps the older,
// looser rules.
inline constexpr int kNoviceWeaponTenths = 600;   // 60.0
inline constexpr int kNoviceHpMax        = 70;

inline constexpr bool IsNovice(int bestWeaponTenths, int hpMax) {
    return bestWeaponTenths < kNoviceWeaponTenths || hpMax < kNoviceHpMax;
}

// A BUILD WITH NO COMBAT SKILL NEVER FIGHTS AT ALL.
//
// Odessa (merchant_tinker, 50 hp, Tinkering/Blacksmithing/Mining, no weapon
// or Magery target anywhere in her plan) walked north out of Britain to the
// mine on 2026-09-07, was picked up by a Harpy and three orcs at 1448,1375
// and beaten from 50/50 down over ninety seconds
// (run_gates/g_Odessa.console.txt:1161-1361; sphere2026-09-07.log
// "00:55:P'Odessa' was killed by N'Harpy'., N'Hysil'., N'Noogugh'.,
// N'Fitaki'."). Nothing in that fight was winnable: she had no weapon skill
// to win it with. `plannedCombatSkills` counts the Primary/Secondary weapon
// or Magery targets in the character's 700-point plan -- zero means every
// engagement question below is already answered "no", whatever the board
// looks like and whatever the character's nerve says.
inline constexpr bool BuildMayFight(int plannedCombatSkills) {
    return plannedCombatSkills > 0;
}

// DO NOT OPEN ON A GROUP -- BUT COUNT THE RIGHT THING.
//
// The first version of this rule counted hostiles WITHIN REACH, and that read
// a duel next to a bystander as a group. Hector, 2026-09-07: he picked a
// Cougar at 9 tiles and a Spectre at 10, walked in, and both times a second
// skeleton drifted inside four tiles on the way -- so `in_reach=2` fired the
// break-off at 100% health with attackers=1 and once with attackers=0
// (run_gates/g_Hector.console.txt:108,128,632,1106,1127,1129). Five refusals,
// zero fights, zero kills in ten minutes, then
// goal_failed=TRAIN_COMBAT "no hunting ground reachable after 3 trips".
// A graveyard always has a second skeleton somewhere in the yard; a player
// who waited for an empty one would never train.
//
// So the duel test is about who is SWINGING AT US, not who is standing near.
// Company still costs something -- it raises the retreat floor above, because
// `board` is max(attackers, inReach) -- but it no longer vetoes the fight.
// What survives unchanged is the owner's hard ceiling (2026-09-04): "don't
// fight where 3+ hostiles are within reach", which is where both 2026-09-06
// deaths actually happened.
inline constexpr int kNoviceCrowdCeiling = 3;   // 3+ within reach: leave

// A radius wide enough to see a pull turn into a pack: the Britain graveyard's
// ring spacing is 6-7 tiles (artifacts/hunt_tier_gate_2026-09-06.md section 1).
// It is REPORTED, not vetoed on -- see above.
inline constexpr int kSoloRadius = 8;

// May a novice START a fight? One thing already on us at most, and fewer than
// three things within reach of joining it.
inline constexpr bool NoviceMayOpen(int attackersOnMe, int inReach) {
    return attackersOnMe <= 1 && inReach < kNoviceCrowdCeiling;
}

// BREAK CONTACT WHEN A SECOND ONE ATTACKS -- at whatever health, not at 25%.
// Trace A's flee fired at 25% with two attackers and was one second too late;
// by RetreatFloorFraction a novice with two attackers is already under the
// line at 63% of a 51-hp bar, so the honest rule is "two ON ME is too many".
// The in-reach arm keeps the owner's 3+ ceiling and nothing narrower.
inline constexpr bool NoviceMustDisengage(int attackersOnMe, int inReach) {
    return attackersOnMe >= 2 || inReach >= kNoviceCrowdCeiling;
}

}  // namespace uo::life::novice
