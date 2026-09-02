---
name: a-deferral-needs-a-bound
description: Any "don't end/act now, we're busy" exception must carry a time bound, or a stuck goal turns the deferral into a permanent veto
metadata:
  type: feedback
---

Every deferral of a deadline needs its own bound. If code says "not while X",
add "…but no longer than N" in the same breath.

**Why:** `Runner::Tick`'s session limit deferred forever while the character
was dead or the goal was `RecoverCorpse`. A corpse run stuck in a retry loop
held both conditions true permanently, so `EndSession` was never called and
Hector stayed connected past his 30-minute window. The deferral rule itself
was correct (never log out as a ghost, never mid-corpse-run); the missing
bound is what turned a safety rule into a stuck session.
See `Runner.h kSessionOverrunGraceMs` and
`artifacts/hector_fix_2026-09-02.md`.

**How to apply:** when reviewing or writing a guard of the form
`if (busy) skip the deadline`, ask what happens if `busy` never clears. The
answer must be a bounded grace that then cancels the in-flight work and runs
the normal path — for a session that is travel abort + `Finish(false)` +
wind-down + logout, never a process kill. Relates to
[[action-timeout-means-unrecognised-answer]] and
[[goal-that-did-nothing-must-stand-down]].
