---
name: parallel-fixer-astra
description: "Astra" = a parallel fixer session the owner runs alongside this one; its work is not visible in git until landed — ask which defects it holds before dispatching fix agents
metadata:
  type: project
---

Owner runs a second session called Astra that fixes defects in parallel
(first mentioned 2026-09-06 after the fleet-100 run). Its edits do not
show in this tree until committed.

**Why:** two agents editing the same runner/needs files collide; the owner
said "astra already fixes couple" when I proposed the serial fix queue.

**How to apply:** before assigning any fix agent, ask the owner which items
Astra holds; run read-only triage/QA freely; check `git log`/`git status`
for Astra's landed work before re-diagnosing a defect.
