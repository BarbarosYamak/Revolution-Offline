---
name: a-stat-ceiling-is-a-skill-property
description: a build's reachable STR is max STAT_STR over its PLANNED skills, so "needs STR" is not "needs to wrestle" — the detour is only right once the character's own work is spent
metadata:
  type: project
---

Source-X rolls a stat gain on every skill use, success or failure, but only
toward that skill's own `STAT_x`, and `if (uiStatVal >= bStatTarg) continue;`
means it never grows one point past it (`CCharSkill.cpp` Skill_Experience,
~:490). So the STR a build can reach by doing its own job is
`max(STAT_STR)` over the skills in its plan — `rules::SkillStatStr` holds the
table for all 58 skills, transcribed from `runtime/scripts/skills/`.

The trap when wiring an "I need STR" need: `obs.str < plan.targetStr` alone
fires for almost every profession in the catalogue, including a lumberjack at
STR 50 whose axe pays to 85 (Lumberjacking STAT_STR=85) and a smith whose
hammer pays to 95. Firing there would be a swordsman dropping his sword to
punch a rabbit. The correct gate is **two** tests in this order:

    obs.str >= PlanStrCeiling(plan)   // own work is spent
    obs.str <  plan.targetStr         // and the target is still ahead

Casters have a ceiling of 20 (Magery) or 10 (Meditation); fighters 55-100.
Wrestling is 100 with `BONUS_STR=50 BONUS_STATS=10`, which is why it is the
authentic detour — and why DEX must be LOCKED while it runs (Wrestling's
`STAT_DEX=75`; Source-X skips any stat whose lock is not UP, so the lock is a
real brake, not a preference).

**Why:** the catalogue's numbers make this counter-intuitive. Every profession
starts at STR 50 and the pure casters target 25-50, so the one group the loop
was written for never raises the need at all; the lives it actually serves are
treasure_hunter 85 / tamer 80 / fisher 80 / archer 70, whose ceilings are 20-60.

**How to apply:** compute ceilings from the plan before claiming a profession
"cannot train X". Related: [[thresholds-are-rates-not-numbers]].
