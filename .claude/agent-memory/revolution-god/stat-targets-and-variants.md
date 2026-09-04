---
name: stat-targets-and-variants
description: Owner stat table 2026-09-04 (STR/DEX/INT per family, applied to Professions.cpp) plus the rule that each family rolls a variant distribution and that final stats are separate from the temporary stat-training strategy
metadata:
  type: project
---

Owner stat table (STR/DEX/INT), applied to `Professions.cpp` 2026-09-04:
pure mage 100/35/90 (forum); warlock 90/100/35 fast or 90/90/45 balanced
(forum); warriors (fencer, macer, archer, pk, lumberjack, miner_smith)
100/100/25; alchemist, scribe, tamer, treasure_hunter 90/35/100; tailor,
merchant_tinker, fisher 100/75/50; mage_blacksmith, full_crafter 100/50/75.

**Variants (owner, same day, not yet implemented):** never one distribution
per family. Pure mage 70% 100/90/35(INT/DEX order as owner wrote:
STR/INT/DEX) 20% 95/95/35 10% 90/100/35; warlock 60% 90/35/100 25%
90/45/90 15% 100/35/90; warrior 70% 100/25/100 20% 100/35/90 10%
90/35/100. Roll per character (identity hash), persist in state.

**Final stats vs how to get there are separate:** a pure mage ends 100 STR
but farms it with a temporary STR skill (Wrestling, or Mace) then drops
it; a warlock reaches STR/DEX organically through melee training. Roster
definition = build -> target stats -> temporary stat-training strategy.

**Why:** Brit bank must not be a row of identical 100/25/100 robots;
Sphere HP=STR so casters at STR 25 died to one hit.

**How to apply:** any plan edit reads this table; variant roll lives in
the plan resolver, not the archetype table. See [[caster-str-via-wrestling]],
[[archetypes-are-families]].
