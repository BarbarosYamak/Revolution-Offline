---
name: ms-stand-downs-die-with-the-process
description: LifeEvent::atMs is a per-process steady_clock reading, so any "rest for hours" written in ms restarts at the next login; count sessions (NeedConfig::sessionIndex) for durable stand-downs
metadata:
  type: project
---

A stand-down measured in milliseconds cannot outlive a session. `Memory::
NoteEvent` stamps `LifeEvent::atMs` with the runner's `nowMs`, which is a
`steady_clock` reading taken inside one process; it is persisted to
`bot_data/state.json` and reloaded, but it is not comparable with the next
process's clock (Core.cpp's corpse-restore path already treats a negative age
as "from before this boot").

**Why:** BUY_MOUNT's 600 s cooldown meant "not again today" inside one run and
"start over" at the next login — Hector opened two consecutive sessions on the
same horse errand and failed it the same way both times. Every gate character
runs as its own process, so ms-based rests are per-session by construction.

**How to apply:** for anything that must be remembered across logins, count
SESSIONS PLAYED. `Identity::sessions` is persisted and incremented in
Phase::Reconcile; it is exposed to the pure need model as
`NeedConfig::sessionIndex` and the failure is recorded as a memory event whose
`detail` carries `session=<n>`. Read it back by parsing that field, not by
subtracting `atMs`. Wall-clock hours are still not available to a need — do not
claim them.

Related: [[a-winning-goal-can-hand-itself-away]].
