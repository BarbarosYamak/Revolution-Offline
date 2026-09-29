---
name: session-2026-09-06-validation
description: Live validation wave 2026-09-06 (Hector/Odessa/Aurelius, 30 min) — read docs/SESSION_STATE_2026-09-06.md and artifacts/validation_wave_2026-09-06.md first; D1-D14 ledger, fix agents sequenced
metadata:
  type: project
---

2026-09-06: owner switched from feature milestones to live validation (M4/M6, M5/M7, M7/M15, mixed roster).
First wave graded Hector 11/18, Aurelius 9/18, Odessa 13/18; 14 defects ranked in
artifacts/validation_wave_2026-09-06.md; verdicts by qa-forensics in artifacts/qa_validation_wave_2026-09-06.md.

Close-out 03:45: contract merged (453fd52) after a parallel Codex session's poison/ramp edits (df1fe75); four
fix tracks still queued serially -- see docs/SESSION_STATE_2026-09-06.md close-out section.

**Why:** the 44 unit tests pass but full archetype cycles were unproven; the wave showed the loops break on
priority/ordering (leave-then-return, comfort needs preempting work mid-trip, heat from won fights), not on
mechanics.

**How to apply:** read docs/SESSION_STATE_2026-09-06.md before continuing; fix agents run SEQUENTIALLY on
disjoint files (shared build dir; uncommitted tree forbids worktrees); every fix is smoked 5 min on the affected
characters and re-verified by qa-forensics before the next validation wave. Every smoke/wave report is
cross-checked against Sphere's `was killed by` log lines -- a fix agent's smoke (01:59) missed a real death.


**Close-out 2026-09-06 ~21:00:** revolution-sphere-m1 pushed at b5ebc13; runtime/scripts 89295d9 (graveyard
split live). Read docs/SESSION_STATE_2026-09-06.md 'Close-out' + 'Next session' first. Fleet-100 root families
fixed at handler+grader level but the honest re-measure is wave 3; nothing is PASS on runtime until then.
