---
name: a-new-goal-does-not-stop-the-old-walk
description: Winning the scoring changes the goal, not the legs already in flight — Vorar took GATHER_LOGS in town and the un-aborted TRAIN_COMBAT walk carried him 168 tiles into the graveyard and killed him
metadata:
  type: feedback
---

Raising a need until a work goal wins is only half the fix: `Planner::Select`
changes `goal_.kind`, but the travel state machine keeps walking wherever the
PREVIOUS goal sent it. `Runner::LeaveGoal` aborts an in-flight trip only for
an explicit list of (from, to) pairs — everything else "lets the walk finish,
because a shopping trip that is nearly there is usually still worth arriving
at".

**Why:** 2026-09-06. TRAIN_COMBAT planned the Britain Graveyard patrol at
18:39:13. My NeedLogs boost made GATHER_LOGS win at 18:39:33 (94.5 vs 58.5);
the bot armed its axe and then was carried 168 more tiles by the hunting walk,
ARRIVED at the graveyard at 18:40:52 and was killed by a Lich at 18:40:58,
losing the hatchet and with it every log the session could cut. The scoring
change looked correct in the log and produced a death and `logs=+0`. Adding
GatherLogs to the abort list turned the same session into `logs=+4 deaths=0`.

**How to apply:** whenever a change makes goal B beat goal A, ask what A's
destination was and whether arriving there is now harmful. Read
`goal_changed=` next to the `[travel] plan <label>` lines — a plan label
belonging to the goal that just LOST is the tell. If B is never served at A's
destination, add the pair to LeaveGoal's abort list (still a list, never "any
goal change": a shop or practice errand can legitimately be served on the way).
See [[a-winning-goal-can-hand-itself-away]] for the other half — winning is
not doing.
