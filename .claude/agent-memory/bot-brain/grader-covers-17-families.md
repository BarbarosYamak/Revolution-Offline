---
name: grader-covers-17-families
description: tools/grade_life.py now keys all 17 professions; the non-obvious traps are the wool chain logging under "bandages:" and there being no treasure loop at all
metadata:
  type: project
---

`bot/uo-client/tools/grade_life.py` FAMILIES covers all 17 `p.id` values in
`src/life/Professions.cpp` as of 2026-08-31. Per-family rules and the known
limitations are written up in `docs/LIFE_GATES.md` §7b.

**Why:** two things cost real time to discover and are easy to get wrong again.

1. The tailor's wool faucet is **not** logged under a `wool:` or `tailor:`
   prefix. Shearing, spinning, weaving and cutting all log under
   `[life] bandages:` because the cloth chain is shared with bandage
   manufacture. Grepping for "wool:" finds nothing and looks like absence.
2. `treasure_hunter` has **no** implemented loop: `Goals.cpp` has no map, dig
   or chest goal, and no `LogLine` in `src/` mentions cartography, a shovel or
   a treasure chest. It is graded on its declared `p.income={Hunt}` plus the
   generic rules, with a TODO. Do not invent treasure criteria for it.

**How to apply:** before adding or changing a FARM-2 clause, take the evidence
string from the actual `LogLine(...)` in `src/life/Runner.cpp` and cite the
line number in the comment. Never write a log format from memory.

Related: [[project-wave10-truncated]]
