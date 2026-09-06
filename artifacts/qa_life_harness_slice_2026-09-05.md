# QA verification — life_harness slice (2026-09-05)

Base: working tree on 621d3d9 (uncommitted).

## 1. Build/test
`python tools/rev.py test` → 100% tests passed, 44/44 (matches claim).
`build-m1/uo_client.exe` mtime 17:02:51.06, `build-m1/tests/life_harness.exe`
mtime 17:02:51.32 — both 2026-09-05, both LATER than every touched source
file (latest src edit `src/life/runner/Core.cpp` 17:02:43). Binaries reflect
current source.

## 2. Non-vacuity (tests/life_harness.cpp)
S1: asserts on `Runner::ErrandRunningForTest("bandage")`, a real accessor
reading `bandageBuy_.Running()`, driven by a genuine Observation change
(hp=8, underAttack, hostilesNear) that the real Planner scores, not a
test-forced state. Not vacuous: the artifact's own before/after table shows
this scenario genuinely FAILS on a scratch build with the fix reverted.
S3: asserts `ActionBusy()==false` and `ActionResult()==InvalidState` after a
real goal change following the same emergency injection; before/after table
shows it genuinely FAILS (busy=1, result=pending) without the fix.
S2: drives a real self-supersession (`goal_changed=X from=X`) and checks
`ActionBusy()` survives; would fail if `LeaveGoal`'s `if (sameKind) return;`
were removed (confirmed in the artifact's scratch-build counter-test:
"in-flight action survived=0"). All three are behavioral, not tautological.

## 3. Policy preservation (git diff)
`Client.cpp`/`Client.h`: only test seams (`SetClockForTest`,
`SetOfflineForTest`+`SentForTest`, `SetInWorldForTest`,
`CompleteActionForTest`) plus one new production method
`AbandonGoalOwnedAction` (FinishAction(InvalidState) + drag reset +
CancelTargetCursor), called only from `LeaveGoal`. `NowMs()` gains a
`clockOverrideMs_ >= 0` guard, false (-1) on every live path.
`Runner.h`: test seam (`SetObservationOverrideForTest`, `ErrandRunningForTest`)
+ `LeaveGoal` declaration + `leavePending_/leavePendingFrom_/leavePendingWhy_`
members.
`Core.cpp` (185 lines): `Observe()` short-circuits to `obsOverride_` (test
only); the old inline goal-change reset block was extracted verbatim into
`LeaveGoal` and is now also called from the Exhausted branch, from both
session-limit exits, and (deferred one tick) from `HandOff`. No change
found beyond: LeaveGoal hook, errand `Cancel()` calls (previously the
errands were NOT cancelled at all on goal change — only flags/sentinels
were reset; this is the actual S1 fix), `AbandonGoalOwnedAction` call (S3
fix), deferred LeaveGoal via `HandOff`, and the `to != from` guard on the
TrainCombat `TravelAbort` branch (was already `to != from`-safe since `to`
is always `planner_.Current().kind` at that call site; the guard formalizes
it for the new call sites where `to == from` is passed intentionally,
e.g. Exhausted/session-limit/HandOff-deferred).

Tick-order trace (does deferred LeaveGoal ever cancel an Emergency goal's
own action?): No. `leavePending_` is drained at the very top of
`Phase::Live`, strictly BEFORE `Observe()`, `AssessNeeds`, `Select()`, and
`RunGoal()` in that same tick — so the incoming goal has not been chosen,
let alone acted, when the deferred cleanup runs. Additionally `HandOff`
sets `nextActionMs_ = nowMs+2000`, which gates `RunGoal()` (line ~2001)
for a full 2s after the handoff regardless of which goal Select() picks —
a second independent guard against the race. For the direct-supersession
path, `LeaveGoal` (line 1422) runs immediately after `Select()` but before
`RunGoal()` (line 1446) in the same tick — same ordering guarantee. HandOff
also puts `from` on `planner_.Cooldown`, so Select() cannot re-pick the
same kind next tick, which is why forcing `sameKind=false` in the deferred
call cannot violate the Corran rule for that path.

## 4. Smoke evidence — run_gates/g_Hector
`goal_exit=` : 4 (lines 201, 593, 673, 819) — exact match to claim.
`goal_changed=`: 4 (193, 466, 583/663-templated).
`handoff=`: 4 (512, 523/674-templated).
`disengage=died`: 0. `invalid_state` (console): 0 — consistent, no death
and no stuck action surfaced this run.
Cloth errand trip counters: :542, :613, :694 all "trip 1" (restart after
each real MAKE_BANDAGES->TRAIN_COMBAT/REPLACE_EQUIPMENT flip); the :466
same-kind repick (REPLACE_EQUIPMENT from=REPLACE_EQUIPMENT) sits between
trip 2 (:456) and trip 3 (:487) of the SAME errand and does not reset it —
Corran rule holds live, not just in the harness.
grade_life.py independently re-run: 13/18, FAILING RULES: FARM-2 TRAIN-1
TRAIN-2 LIVE-1 LIVE-5 (self_superseded=1, LIVE-2 PASS). Matches claim
exactly. Prior comparisons confirmed in `artifacts/wave_2026-09-04_60min.md`
(10/18) and `artifacts/wave2_grade_2026-09-01.md` (9/18).
`.err.txt` diff: the pre-existing "no place offers veterinarian" family
recurs (unrelated to this fix). One NEW family not present before: "goal
(535,867) is not walkable; snapping to nearest standable tile" (x2) —
pathfinding/navgrid, unrelated to goal-exit mechanics; not attributable to
this diff (no touched file owns navgrid snapping). Flagged for the
travel-navigation owner, not blocking this slice.

## 5. Verdict
PASS for the stated criterion ("changing goals cannot leave unrelated work
running") — offline harness evidence is non-vacuous and behavioral, the
live-path diff is confined to the described hook plus test seams, tick
ordering rules out the specific Emergency-preempt race asked about, and
live-run evidence (g_Hector) shows the hook firing on real goal changes
(preempt, TrainCombat handoff chain, session-limit) while the same-kind
Corran-rule case in the same run keeps its trip counter. Grade improvement
(9/18 -> 10/18 -> 13/18) is consistent with, not proof of, the fix (grade
also reflects unrelated planner variance run to run).
