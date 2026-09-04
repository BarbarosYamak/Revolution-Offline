---
name: caster-str-via-wrestling
description: Owner rule 2026-09-04 — casters farm STR by temporarily training Wrestling (bare-handed sparring), then lock Wrestling DOWN once STR target hit; verified in Source-X + skill scripts, not player memory
metadata:
  type: project
---

Casters raise STR through a temporary Wrestling phase: fists, spar on dummy /
low-danger mob / consenting bot, STR UP + DEX LOCK + INT UP, then Wrestling
DOWN at target STR so real mage skills reclaim the points.

**Why (verified 2026-09-04):** Source-X `Skill_Experience` rolls stat gain
toward the used skill's STAT_x and never past it. Magery STAT_STR=20,
Meditation 10, Wrestling 100 (`runtime/scripts/skills/`). A pure caster
cannot exceed STR 20 otherwise. Crime rules: hitting a GOOD player is not
auto-criminal; a speaking NPC witness flags you and calls guards in a
guarded region; retaliation against an aggressor is never a crime; dummy
caps at Wrestling 30.0 (`SkillPracticeMax=300`).

**How to apply:** spar outside guard zones and NPC LOS, HP floor, never
kill a player. Applies to every plan whose targetStr > max STAT_STR of its
planned skills (mage, scribe, alchemist, tamer…), not just "mage". Links
[[sparring-parties]] (partner mechanism) and [[start-stats-mage-50-25-5]].
