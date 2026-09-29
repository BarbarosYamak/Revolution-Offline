---
name: a-phase-is-not-a-goal
description: No goal runs in Phase::WindDown or Reconcile, so DoSurvive never guards the logout walk; safety there has to live in the destination choice
metadata:
  type: project
---

`Runner::Tick` is a switch on `phase_`, and only `Phase::Live` reaches
`RunGoal` and the per-tick keepers next to it (`KeepCallingGuards`,
`LearnFromObservation`, `MaintainBuildLocks`). `Phase::WindDown` is a
self-contained arm: it picks a destination, walks, and logs out. **DoSurvive,
the engagement layer and the guard shout are all unreachable from it.** The
only thing still watching is the HP watchdog inside travel.

**Why:** Odessa, 2026-09-07 00:54:08. `session_end_requested` -> wind-down had
learned no bank -> "asking the world for one" -> `TravelToService(Banker)`
routed her north through the orc camp -> killed 00:55:57 at (1448,1375) by a
Harpy and three orcs. The engagement work landed at 0f649fd and did not touch
this, because the fix was in a goal and the walk was in a phase.

**How to apply:** any behaviour you add "so the bot is safe" is only live in
`Phase::Live` unless you put it in the phase arm too. In wind-down the fix has
to be the *destination*: hostiles in scan -> shout if this ground is guarded
(`CallGuardsIfProtected`, callable from Core.cpp), else run to the nearest
guarded PLACE, else stand still. Bound the guarded hold by
`kWindDownBudgetMs`, because a hostile merely in scan never has to leave and
would otherwise hold the session open for ever. Same caution applies to
`Phase::Reconcile` and `Phase::LoggingOut`.

Related: [[a-flee-percent-is-not-a-retreat]],
[[a-keyword-mechanic-is-a-per-tick-state]], [[a-new-goal-does-not-stop-the-old-walk]].
