---
name: grader-and-loop-gaps
description: grade_life.py covers all 17 families; TRAIN-2 counts only NPC purchases so combat/craft skill gain reads as 'never trained'; §7b bugs; treasure_hunter has NO loop
metadata:
  type: project
---

grade_life.py FAMILIES covers all 17 professions (2026-08-31). Known grader
bugs are documented in docs/LIFE_GATES.md §7b, deliberately NOT patched
because each fix flips existing verdicts (STOCK-1 produce check dead for
gathers-less trades; generic income clause omits kills; tailor cloth chain
logs under bandages prefix).

`treasure_hunter` has no treasure loop anywhere in src/ — no map/dig/chest
goal, no cartography/shovel log lines. Graded generic-liveness only.

**Why:** wave10 grading found 11/33 characters unscoreable; extension derived
rules from real Runner.cpp log lines, never invented formats. Tailor FARM-2
and hunt-kill clauses are verified-source, not verified-runtime (thin log
evidence).

**How to apply:** fix §7b bugs only as a deliberate slice with re-grades of
old waves; treasure_hunter loop is missing CONTENT (roadmap item), don't
grade it as a failure of the character. A truncated run (no session_summary)
zeroes TRAIN-1/STOCK-3/LIVE-1/2/4/5 for everyone — check run length before
reading scores.

**Fleet-100 lesson (2026-09-06):** TRAIN-2 fails 92/98 largely because it only
accepts `train: X a->b bought from a trainer` -- combat kills and Working-
practiced craft gains (Needs.cpp skips NeedTraining for Tailoring/Tinkering/
Smithing/Inscription/Alchemy) are invisible to it, while TRAIN-1 (skill delta)
shows the real growth. Read TRAIN-1 as the truth for training; TRAIN-2 is
"bought training" until a `gained in combat` / craft-gain line exists. The
hunt-kill grep was stale (`hunt: killed '` -> `hunt: confirmed kill target=`),
fixed 2026-09-06. Ledger: artifacts/fleet100_triage_2026-09-06.md.
