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

**How to apply:** at wave start spawn the sonnet watcher with the defect
list to confirm and a verdict rubric; it does `rev.py wait` + `grade` too.
Owner mid-wave reports ("X stuck") get forwarded into the next agent brief,
answered from evidence the agent returns. Only read an `artifacts/` file
when a verdict is disputed. Related: [[feedback-agent-spend]].
