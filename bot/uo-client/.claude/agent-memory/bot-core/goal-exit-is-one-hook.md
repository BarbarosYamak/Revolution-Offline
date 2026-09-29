---
name: goal-exit-is-one-hook
description: Runner::LeaveGoal is the single owned goal-exit; what it cancels, why sameKind must return early, and the HandOff deviation
metadata:
  type: project
---

Every place a goal ends calls `Runner::LeaveGoal(client, from, to, sameKind,
why)` (`src/life/runner/Core.cpp`): the Select-changed path, RunGoal's
Exhausted branch, both session-limit exits, and -- deferred by one tick --
`HandOff`. On `!sameKind` it cancels every Runner-owned errand
(BuyActivity/VendorErrand/BankErrand: `Cancel()` suffices, since `Begin()`
resets step, baselines, trips and retry policy), clears the unsettled
purchase/bank/craft bookkeeping, and calls
`Client::AbandonGoalOwnedAction` (FinishAction InvalidState + drag reset +
CancelTargetCursor). Logs `goal_exit=FROM->TO dropped errand|action`.

**Why:** neither `BuyActivity::Cancel` nor `VendorErrand::Cancel` had a
caller outside `Begin()`, so an errand interrupted mid-Verify resumed
against pack/purse baselines from before the interruption and blacklisted a
shopkeeper that never refused anything; and an in-flight action outlived its
goal (Xerxes reissued the same open_container across four goal boundaries).

**How to apply:** the `sameKind` early return is the Corran rule and is not
negotiable -- a self-supersession (`goal_changed=X from=X`) must reset
NOTHING, and `tests/life_harness.cpp` S2 fails the moment it is removed.
`HandOff` passes no Client (47 call sites), so it records
`leavePending_/leavePendingFrom_` and the next Live tick runs the exit; the
successor is unknown there, which is why LeaveGoal's TrainCombat
`TravelAbort` also requires `to != from`. See [[offline-life-harness]].
