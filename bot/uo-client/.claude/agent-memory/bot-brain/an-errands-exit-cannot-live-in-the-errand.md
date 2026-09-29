---
name: an-errands-exit-cannot-live-in-the-errand
description: when success SILENCES the need, the goal is never picked again — so the closing step (a lock, a flag, a cleanup) has to run somewhere the planner does not gate
metadata:
  type: project
---

STAT_FARM's brief said "when STR reaches target, set the Wrestling lock DOWN".
The obvious place is the top of `DoStatFarm`. It can never run there: the
moment `obs.str >= plan.targetStr` the need falls silent, the goal scores
nothing, the planner never selects it, and the closing packet is never sent.

The shape to look for: **any errand whose completion condition is the same
condition that raises its need.** The exit must move to a per-tick keeper that
the planner does not gate — here `Runner::MaintainBuildLocks`
(`src/life/runner/Core.cpp`), which already runs every 30 s regardless of the
active goal.

Two details that make the moved exit correct rather than merely reachable:

* it needs to know a farm ever HAPPENED, so it does not hand a lock to a
  character that never earned it — a `bool` member dies with the process, so
  the marker is a durable memory event (`stat_farm_started` /
  `wrestling_locked_down`, `Memory::HasEvent`), same reason ms stand-downs
  have to be counted in sessions ([[ms-stand-downs-die-with-the-process]]);
* the keeper's normal policy has to stand aside while the errand is running
  (`statFarmActive_` skips the end-of-build stat-lock loop), or the two fight
  over DEX every 30 seconds.

**How to apply:** before writing an exit branch into a `Do*` handler, ask
whether the need that selects that goal is still true at the moment the exit
should fire. If it is not, the exit belongs in a keeper.
