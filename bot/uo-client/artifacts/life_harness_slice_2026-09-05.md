# Deterministic life harness + one owned goal exit — 2026-09-05

Base: working tree on 621d3d9 (uncommitted, not stashed).

## A. The harness

`tests/life_harness.cpp`, ctest name `life_harness`. Real `life::Runner`
driving a real `uo::Client`. Three injections, nothing else:

| Seam | Where | What it replaces |
|---|---|---|
| `Client::SetClockForTest(i64)` | `src/Client.h`, impl in `Client::NowMs` | the wall clock (all 48 in-file reads go through `NowMs()`) |
| `Client::SetOfflineForTest(bool)` + `SentForTest()` | `Client::Send` | the socket — packets are captured, still built by the live builders |
| `Runner::SetObservationOverrideForTest(const Observation*)` | `Runner::Observe` (`runner/Core.cpp`) | the world; nowMs is patched in per tick |

Support seams: `Client::SetInWorldForTest()` (leave `Phase::AwaitWorld`),
`Client::CompleteActionForTest(act::Result, why)` (drives the real
`FinishAction`), `Runner::ErrandRunningForTest(const char*)`.

Build: `UO_CLIENT_SOURCES` minus `src/main.cpp` is now the OBJECT library
`uo_client_core` (root `CMakeLists.txt`); `uo_client`, `uo_viewer` and
`life_harness` consume it, so every client TU compiles once and every
function-local static has one definition.

### Harness limits (real, and load-bearing for anyone extending it)

- No atlas/navgrid is configured, so every travel leg refuses with
  `goal_blocked=... "no atlas path configured"`. Errands therefore reach
  their find/scan legs and then fail fast; they cannot be driven to a
  purchase offline.
- `Client::Tick()` is never called, so `ActionTick()` never sweeps: an
  in-flight action stays `Pending` until something ends it. That is what
  makes S2/S3 unambiguous — only the goal machinery can finish it.
- `RunnerConfig::sessionLimitMs = 0` (no session clock).

## Scenario results (12 checks)

| Scenario | Before B | After B |
|---|---|---|
| S1 errand does not survive a genuine goal change | FAIL — `bandageBuy_` still `Running()` after `SURVIVE` preempted `REPLACE_EQUIPMENT` (printout: `goal after recovery: SURVIVE, bandage errand running=1`) | PASS — cancelled on the switch, begun afresh on the way back |
| S2 same-kind re-pick keeps its journey (Corran rule) | PASS (regression guard) | PASS — and FAILS on a scratch build with `if (sameKind) return;` removed (`in-flight action survived=0`), so it is a real guard, not vacuous |
| S3 in-flight action finished on goal change | FAIL — `action busy=1 result=pending` after the goal changed (the Xerxes shape) | PASS — `action busy=0 result=invalid_state` |

"Before B" = scratch build with LeaveGoal's errand-cancel and
`AbandonGoalOwnedAction` block removed; restored afterwards.

## B. `Runner::LeaveGoal`

`src/life/runner/Core.cpp` (declaration `src/life/Runner.h`). Called from
every place a goal ends:

- the Select-changed path in `Tick` (the old inline reset block moved inside
  it verbatim),
- `RunGoal`'s `Exhausted` branch,
- `HandOff` — indirectly: it sets `leavePending_/leavePendingFrom_/
  leavePendingWhy_` and the next `Phase::Live` tick runs `LeaveGoal` before
  anything reads the slate. **Deviation from the brief**: `HandOff` has 47
  call sites and no `Client&`; threading one through all of them is a larger
  diff than the fix. Nothing can tick in the gap (`nextActionMs_ = now+2000`
  holds `RunGoal` off).
- both session-limit exits in `Tick`.

`!sameKind` additionally: cancels `bandageBuy_`, `bandageClothBuy_`,
`potionBuy_`, `clothingBuy_`, `weaponBuy_`, `foodErrand_`, `bankErrand_`
(`Cancel()` is enough — every `Begin()` resets step, baselines, trips and
retry policy); clears `pendingBuyItem_`, `pendingBuyGoldBefore_`,
`craftHadBefore_`, `craftJournalMs_`, `bankItemMovePending_`; and calls the
new `Client::AbandonGoalOwnedAction(why)` — `FinishAction(InvalidState)`,
drag reset (same as the `ActionTick` timeout path), `CancelTargetCursor`.
Client tracks no owner for an action, so the cancel is unconditional on a
real goal change; stated, not hidden.

`sameKind` returns after the unconditional `vendorChases_ = 0` only —
byte-for-byte today's behaviour. The `TrainCombat`-only `TravelAbort` is
unchanged except for a `to != from` guard, which is what keeps the
unknown-successor call sites (Exhausted, HandOff, session limit) from
widening it. Emits `goal_exit=FROM->TO dropped errand|action reason="..."`
only when something was actually dropped.

## Gates

- `python tools/rev.py build` — clean.
- `python tools/rev.py test` — 44/44 (43 pre-existing + `life_harness`).
- Smoke `python tools/rev.py gate CHAR=Hector MINUTES=5`
  (`run_gates/g_Hector.console.txt`, 17:03–17:09): grade 13/18, vs 10/18 in
  `artifacts/wave_2026-09-04_60min.md` and 9/18 in
  `artifacts/wave2_grade_2026-09-01.md`. The hook fires live, four times,
  on exactly the family the review flagged:
  - :201 `goal_exit=MAKE_BANDAGES->REPLACE_EQUIPMENT dropped errand`
  - :593, :673 `goal_exit=MAKE_BANDAGES->TRAIN_COMBAT dropped errand`
  - :819 `goal_exit=MAKE_BANDAGES->MAKE_BANDAGES ... "session time limit reached"`
  and the cloth errand restarts at `trip 1` after each real flip (:542,
  :613, :694) instead of carrying 2, 3 across them — while the same-kind
  re-pick at :466 (`REPLACE_EQUIPMENT from=REPLACE_EQUIPMENT`) kept its
  counter (trip 2 -> trip 3). Both rules visible in one run.

## Known consequence, not fixed here

Restarting the errand also restarts its trip budget, so a planner that
flip-flops (`TRAIN_COMBAT <-> MAKE_BANDAGES`, three times in this run)
pays a fresh set of trips each time. The flip itself is a scoring problem
and is out of this slice's scope.
