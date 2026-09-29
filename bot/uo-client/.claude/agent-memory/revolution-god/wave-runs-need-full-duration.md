---
name: wave-runs-need-full-duration
description: A killed fleet wave grades as regressions that aren't; grader sentinels for missing lines look like real failures. Check logout_complete count before reading scores.
metadata:
  type: feedback
---

A fleet wave killed before wind-down is not gate evidence. Check
`logout_complete` across g_*.console.txt before reading any grade.

**Why:** 2026-09-01 18:14 wave: owner killed all 30 at ~6 min ("standing
still / walking back and forth"). grade_life.py then reported Draver 14→10
etc. — but TRAIN-1 `0.0->0.0`, STOCK-3 `gold 0->0`, LIVE-1/2/4/5 and
`self_superseded=999` are sentinel defaults for "line never emitted", not
measured regressions. First read looked like a fleet-wide collapse.

Owner 2026-09-02: "we don't need 30 min test, normally in 5 min we should
see they work in their proper flow." So: fix verification = 5-min smoke on
the affected characters only (`rev.py gates CHARS=a,b MINUTES=5`), judged by
flow (first goal, reach target, first action result), not by grade. Full
30-min wave only for economy/progression proof and milestone verdicts. The
rule above (don't grade a killed wave) still holds for the long runs.

**How to apply:** `python tools/rev.py grade` flags "(not logged out)";
treat those rows as void. The owner's live observation is the evidence for
a killed wave — triage per-character behaviour (what it did, first thing
wrong, cluster by family) instead of grading. When the owner says "almost
every char had problems", ask what it looked like on screen; "standing
still / back and forth" mapped to navigation clusters covering 25/30.
