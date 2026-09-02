---
name: feedback-agent-stop-rule
description: Owner 2026-09-02 — agents must stop and report BLOCKED instead of wandering; ~25 calls without root cause, two failed smokes, or out-of-brief scope ends the run
metadata:
  type: feedback
---

Agents stop when they cannot fix it. Written into CLAUDE.md "Subagent
Contract → Stop rule" (2026-09-02): ~25 tool calls without root cause,
two failed smoke runs, or a fix needing something outside the brief →
BLOCKED report with learned/ruled-out/next-check. No third attempt.

**Why:** owner: "agents shouldn't spend tokens randomly as well, if they
can't fix it no reason to continue." Same day the wiki was built to cut
exploration cost; wandering agents are the other half of that bill.

**How to apply:** every brief already inherits the rule via CLAUDE.md;
still put the smoke budget ("two runs max") in the brief explicitly. When
a BLOCKED lands, escalate per [[escalate-after-three-tries]] or re-scope —
never re-spawn the same brief. Related: [[feedback-agent-spend]],
[[feedback-observe-via-agent]].
