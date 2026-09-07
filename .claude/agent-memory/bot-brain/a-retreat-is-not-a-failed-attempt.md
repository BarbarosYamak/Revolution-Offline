---
name: a-retreat-is-not-a-failed-attempt
description: A 2s flee arm calling NoteAttempt exhausts SURVIVE's five attempts in ten seconds and hands the slot back, aborting the escape route each time
metadata:
  type: feedback
---

A handler that ticks faster than the planner's attempt budget must not
call `NoteAttempt` on every pass. Credit the thing that is actually
advancing — for a retreat, a live travel route.

**Why:** `DoSurvive`'s flee arm reschedules every 2000 ms and called
`planner_.NoteAttempt` unconditionally, so `Exhausted()` fired at
"attempts 5 >= 5" ten seconds into every flight. Odessa cycled
SURVIVE → abandoned → BANK → SURVIVE (emergency preempt) three times in
forty seconds (run_gates/g_Odessa.console.txt 2026-09-07:648,659,687,698,727),
and each handover ran `TravelAbort` + a fresh plan on the escape route she
was standing in. The goal was scoring 900.0 and winning every time; the
budget was killing it anyway.

**How to apply:** `NoteProgress` only on evidence the world moved
(`client.TravelBusy()` here), `NoteAttempt` otherwise, so a genuinely
stuck handler still exhausts on schedule and the goal's own time limit
still bounds both. This is the narrow exception to
[[progress-counts-issues-not-results]]: the credit is tied to a route
existing, not to "come back later". Related: [[goal-that-did-nothing-must-stand-down]].
