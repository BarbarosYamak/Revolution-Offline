---
name: feedback-budget
description: Owner budget complaint 2026-09-02 — 20% of weekly usage burned on one character (tailor); rules for cheap iteration
metadata:
  type: feedback
---

Do not spend agents on one profession's chain link by link. Owner: "you spend 20% of my weekly usage just for one char" (2026-09-02, tailor Aelia/Amara).

**Why:** Serial opus agents each re-read Runner.cpp; qa gates proved one link per 5-min run; two gates wasted (clobbered, 0-gold block); a throwaway agent used for message passing; content side-quests (sheep, graveyards) mixed into the fix.

**How to apply:**
- Small known edits (<3 files): lead edits directly, no agent.
- One qa gate after all likely links fixed, not per link. Never launch a gate while one is running.
- Scenario must fund/seed the char so the run does not block on purse.
- Query hostiles (`world_query.py --near`) before placing any content coordinate.
- Park content side-quests until owner asks.
- Prefer sonnet for triage; opus only when root cause unknown after direct look.

**2026-09-05 addendum:** after three agents in one morning the owner said "dont start agents today do it yourself". Read as: at most one agent per session, research/verification only; implementation stays in the main thread once the root cause is in hand.
