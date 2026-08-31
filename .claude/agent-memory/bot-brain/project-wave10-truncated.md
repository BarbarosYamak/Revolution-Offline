---
name: project-wave10-truncated
description: run_gates/wave10 emitted no session_summary/session_goals for any of the 33 characters, so its low LIFE-GATE scores are a run artefact, not a behaviour regression
metadata:
  type: project
---

`run_gates/wave10/` (2026-08-31 17:35) has **zero** `session_summary duration=`
and zero `session_goals families=` lines in all 33 consoles — the wave was cut
off before wind-down.

**Why:** `grade_life.py` derives TRAIN-1, STOCK-3, LIVE-1, LIVE-2, LIVE-4 and
LIVE-5 entirely from those two lines. With them absent every character scores
7-11 / 18 regardless of profession, and `self_superseded` defaults to 999.

**How to apply:** do not read a wave10 score as evidence about a family's
behaviour, and do not chase LIVE-1/2/4/5 failures there. Use a wave that
actually reached wind-down (wave15 is much richer) when grading behaviour.
wave10 also has no `state_before` snapshots; the grading pass substituted
`run_gates/wave10/_empty_before.json` (`{"bank": []}`) for both state
arguments, which forces STOCK-1 to bank-growth-of-nothing.

Related: [[grader-covers-17-families]]
