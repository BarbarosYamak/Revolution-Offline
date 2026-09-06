---
name: feedback-speed-over-hand-polling
description: Owner 2026-09-06 approved the three accelerators (commit often -> worktrees, atlas-backed offline harness, need<->handler contract) and wants the lead coordinating, not hand-polling logs
metadata:
  type: feedback
---

Hand the per-minute wave watch to one sonnet qa agent from minute 1; keep the tree committed so fix agents run in
parallel worktrees with their own build dirs; invest in the offline atlas harness and the need<->handler contract
rather than per-case patches.

**Why:** owner asked "why is this too hard for you?" after a session where the lead spent hours polling logs by hand,
ran three fix agents sequentially because the uncommitted tree blocked worktrees, and chased two wrong hypotheses.
Owner approved all three accelerators in order ("1 2 3").

**How to apply:** at wave start spawn the observer agent, then do coordination work; commit after every smoked fix
batch; when a defect is an instance of "need scores but handler cannot act", route it to the contract work, not a
one-off patch.
