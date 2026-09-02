---
name: feedback-agent-spend
description: Owner's cost rules for delegation — sonnet for triage, opus for fixes, Fable main thread only; serial agents not parallel; no separate triage pass before a fix; qa-forensics only for live-run verdicts
metadata:
  type: feedback
---

Delegation cost rules (owner, 2026-09-01):

1. `model: "sonnet"` for investigation/triage agents, `model: "opus"` for
   fix agents. Fable is the main thread only. Set the `model` param on
   every Agent call.
2. Main thread effort medium (owner runs `/effort medium`; I cannot set it).
   If a step genuinely needs high — arbitrating a disputed verdict, a
   fidelity ruling on mixed evidence, hand-fixing code myself — say so
   explicitly and the owner will switch; drop back after.
3. ONE agent at a time, strictly. Parallel agents burn the 5-hour usage
   window several times faster — that is the binding limit, not per-token
   price. Stop after each and skip what the previous result made
   unnecessary. There is no tool to stop a running agent, so the decision
   is made at spawn time.
4. No separate read-only triage pass before a fix. Hand the fixer the raw
   cluster list / symptom and let it investigate once.
5. qa-forensics only for live-run verdicts. Unit tests + ctest counts are
   enough for code-level proof; don't spend an agent re-verifying them.

**Why:** Agent spend was the biggest line item — three parallel agents plus
a triage pass plus a qa pass on the same wave (~380k subagent tokens in one
evening before any fix landed). Owner estimates 3-5× cut from model choice
alone.

**How to apply:** Before spawning, ask: is this a live-run verdict (qa,
opus/sonnet by need), a fix (opus), or a look-up (sonnet)? One agent at a
time unless the work is trivially independent AND cheap. Read the previous
agent's NEXT before starting the following one.
