---
name: goal-exit-ownership
description: Review 2026-09-05 — dominant live failure family is errand/action state outliving goal_changed; goal exit must be one owned hook (Runner::LeaveGoal); same-kind re-pick keeps its journey
metadata:
  type: project
---

Goal exit was nobody's job until 2026-09-05: Core.cpp's goal-switch block cleared travel flags and
log sentinels only; BuyActivity/VendorErrand/BankErrand members, pending buy ledger, bank drag,
craft transients, Client action/cursor/drag all leaked into the next goal (no caller of
BuyActivity::Cancel existed). Live: Xerxes open_container reissued across 4 self-supersessions,
Hector cloth-trip counter across 3 flips, Survive-preempt -> stale packBefore_ -> Contradicted ->
innocent keeper marked drained 10 min.

**Why:** every later slice (Objective carrier, errand-owned attempt budget, ContainerSync) assumes a
clean exit; without it, fixes get blamed on the wrong goal.
**How to apply:** any new errand/activity member on Runner must have Cancel() and be listed in
LeaveGoal. Never cancel on same-kind re-pick (Corran rule: TRAIN_AT_NPC re-picked mid-journey must
keep travelInFlight_). Ranked list + sequence: docs/ARCHITECTURE_REVIEW_2026-09-05.md.
