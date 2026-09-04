---
name: feedback-observe-via-agent
description: Per-minute wave observation and log slicing belong to a sonnet qa agent, not the Fable main thread; owner flagged the cost 2026-09-02
metadata:
  type: feedback
---

Delegate live-wave watching (per-minute counters, per-character log
slices, screenshot follow-ups) to ONE sonnet `qa-forensics` agent and read
only its ≤1200-token report. Main thread answers the owner from the report,
not from grep.

**Why:** 2026-09-02 wave — I ran ~15 grep/sed passes over run_gates in the
main thread (Fable). Owner: "maybe you shouldn't read this because it is
expensive, don't we have skills/agent for this?" The observe-every-minute
rule still stands; the *reader* changes.

**How to apply:** for a 5-min smoke, main thread runs `rev.py wait` (cheap)
THEN spawns a sonnet post-mortem with the exact grep patterns (`goal=`,
`supplies: buying`, `craft: made`, `session_summary`, `BLOCKED_NEED`,
`goal_failed`) and one artifact — ~10 calls, ~1 min. Owner 2026-09-04 asked
"can sonnet do it faster?": a per-minute live-watch brief idles the agent
through the whole gate and then slices 40x. Live watching only for long
waves when the owner is at the client. Main thread may do ≤3 targeted greps
itself when the owner is waiting on the answer.
Owner mid-wave reports ("X stuck") get forwarded into the next agent brief,
answered from evidence the agent returns. Only read an `artifacts/` file
when a verdict is disputed. Related: [[feedback-agent-spend]].
