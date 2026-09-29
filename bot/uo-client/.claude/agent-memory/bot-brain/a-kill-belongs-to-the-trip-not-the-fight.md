---
name: a-kill-belongs-to-the-trip-not-the-fight
description: Per-tick hooks that run before goal selection cannot call NoteProgress -- they credit whichever goal holds the slot; record the fact, let the owning goal consume it
metadata:
  type: feedback
---

A fact produced by a per-tick hook that runs BEFORE the planner picks a goal
must be recorded in a counter and consumed by the goal that owns the errand --
never credited with `planner_.NoteProgress()` where it is observed.

**Why:** `ProcessHuntAftermath` confirms the kill from `Runner::Tick`, ahead of
goal selection, and the fight itself is usually held by SURVIVE. Every kill in
fleet-100 therefore landed on `goal_completed=SURVIVE progress=0` while
TRAIN_COMBAT -- the goal that actually walked to the graveyard -- completed
with progress 0 until the anti-spin backstop cooled it off. Hector had 4 real
kills and Leander 6; both graded "verified training events 0"
(artifacts/fleet100_triage_2026-09-06.md Cause A).

**How to apply:** the shape is `i32 huntKillsPending_` in Runner.h, `++` beside
`++session_.kills`, consumed at the top of `DoTrainCombat` (after the
underAttack forward) into one NoteProgress per kill plus the goal's success
return. Two companions worth checking whenever a handler "has no success path":
(1) an allowance that is reset by the very event it is supposed to bound --
`huntTrips_` was cleared by ARRIVING, so an empty yard could loop forever;
(2) `Planner::Finish` counts failure-side terminations toward the spin limit
too, so five honest `Finish(false)` calls read as a spin. Related:
[[an-errands-exit-cannot-live-in-the-errand]], [[progress-counts-issues-not-results]].

grade_life.py TRAIN-2 accepts `goal_completed=<TRAIN goal> progress=1` as a
LITERAL -- progress=2 (fight opened + kill credited in one pick) does not
match, and `train: X a->b gained in combat` does not match its `train:` regex
either, which still requires "bought from a trainer".
