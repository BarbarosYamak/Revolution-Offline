---
name: offline-life-harness
description: How to write a deterministic Runner+Client test offline (tests/life_harness.cpp) and the three limits that decide what such a test can prove
metadata:
  type: project
---

`tests/life_harness.cpp` (ctest `life_harness`) runs the REAL `life::Runner`
against the REAL `uo::Client` with no server and no socket. Three injections:
`Client::SetClockForTest` (every wall-clock read goes through `Client::NowMs`),
`Client::SetOfflineForTest` (Send captures into `SentForTest()` instead of
writing sock_), `Runner::SetObservationOverrideForTest` (Observe returns the
scripted Observation). Plus `SetInWorldForTest`, `CompleteActionForTest`,
`ErrandRunningForTest`.

**Why:** goal-lifetime bugs (an errand or an action outliving its goal) were
only ever seen in 30-minute live runs, where they cost a whole session to
reproduce and could not be asserted on.

**How to apply:** three limits decide what a harness scenario can prove.
1. No atlas is configured, so EVERY travel leg refuses with
   `goal_blocked=... "no atlas path configured"`. Errands reach their
   find/scan legs and fail fast; an offline test can never drive a purchase
   to completion. Assert on lifetime, not on outcomes.
2. `Client::Tick()` is never called, so `ActionTick()` never sweeps -- an
   in-flight action stays Pending until the goal machinery ends it. That is
   what makes "the action survived / did not survive" unambiguous.
3. `RunnerConfig::verbose=true` is what makes `[life]` lines visible; with
   verbose=false a failing scenario tells you nothing.

Same-kind re-picks are detectable without any new seam: `GetPlanner()
.Current()` exposes `kind` AND `startedAtMs`, so "same kind, new
startedAtMs" IS `goal_changed=X from=X`. See [[goal-exit-is-one-hook]].
