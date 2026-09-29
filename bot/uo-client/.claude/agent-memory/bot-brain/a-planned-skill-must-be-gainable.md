---
name: a-planned-skill-must-be-gainable
description: A build target the server can refuse forever becomes NeedSkillTraining's permanent answer; Magery at its creation 50.0 in a Utility role is not a mana spender
metadata:
  type: project
---

A profession target that can never gain does not merely sit there — it
becomes the life's training answer for ever. `NeedSkillTraining` picks the
plan's highest unfinished skill, so the one entry the server refuses
outranks every entry that is already done.

**Why:** the alchemist row planned Meditation 50.0 beside Magery 50.0.
Magery's target equalled the 50.0 creation grants, so it was finished at
login; Alchemy trains through crafting; Meditation was the only unfinished
target left. Sphere's Skill_Meditation refuses at full mana and a brewer
spends none, so Selene logged `want_train=Meditation (target 50.0)` on
every tick of a whole session with the skill flat at 0.0
(artifacts/fleet_ramp_20260906/Selene.console.txt). After the removal her
first goal was CRAFT and she made 29 poison potions in five minutes
(run_gates/g_Selene.console.txt, commit 464476e).

**How to apply:** when auditing a catalogue row, read the targets as
"what will still be unfinished after creation", not as a wish list. A
skill's presence is not evidence the build uses it — Magery in a
`SkillRole::Utility` slot at the creation value is a dabble that spends no
mana, which is why `tests/build_mana_plan.cpp` keys the Meditation rule on
the ROLE rather than on Magery being listed. Removing a target also frees
`startZeroSkill`: m5_professions asserts the third creation slot names a
planned skill, so drop it to -1 (creation falls back to Remove Trap 0.0)
and move the freed tenths to `unresolvedTenths` so the 700 budget still
adds up. See [[thresholds-are-rates-not-numbers]] and
[[a-spells-cost-is-not-the-professions-consumes-list]].
