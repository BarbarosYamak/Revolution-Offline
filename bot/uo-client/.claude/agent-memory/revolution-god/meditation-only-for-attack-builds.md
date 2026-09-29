---
name: meditation-only-for-attack-builds
description: Owner rule 2026-09-06 — craft builds do not plan Meditation; attack builds (fighters/casters) may. Sphere refuses Meditation at full mana, so a non-mana build can never raise it
metadata:
  type: project
---

Owner ruling 2026-09-06: "if it is craft no need, if it's attack sure" —
Meditation belongs only in attack (combat/caster) builds; pure crafter
plans (alchemist, tailor, smith, tinker, carpenter...) drop it.

**Why:** Selene's alchemist plan carried Meditation 50.0 with no
mana-spending skill; Sphere's Skill_Meditation refuses at full mana
(CCharSkill.cpp:2674, "You are at peace"), so the plan could never complete
and the practice need burned attempts (fixed 1877518 to block honestly).

**How to apply:** Professions.cpp catalogue rows — Meditation only where
the family has Magery or another mana consumer; a unit test over
prof::All() should assert "Meditation planned => a mana-spending skill
planned". Inscription/Alchemy mana use must be verified in runtime/scripts
before treating scribe as an exception.
