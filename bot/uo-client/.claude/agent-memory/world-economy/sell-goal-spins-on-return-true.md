---
name: sell-goal-spins-on-return-true
description: In DoEarnGold a `return true` on a no-sale path is success-with-progress-0 — the planner re-picks it instantly; every dead end needs Cooldown + Finish(false)
metadata:
  type: feedback
---

A goal path that cannot act must **stand down**, never report success.
`return true` from `Runner::DoEarnGold` means the errand completed; the planner
frees the goal and hands it straight back, because nothing about the world
changed. Three such paths existed and were fixed 2026-09-02:

- "the bank holds a surplus but no NPC route for it"
- "everything spare is barred from an NPC sale"
- "no NPC trade on this shard buys X"

Correct shape (already used by the sibling "nothing spare to sell" branch):

```
planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + <cooldown>);
planner_.Finish(false, "<reason>", obs.nowMs);
nextActionMs_ = obs.nowMs + 5000;
return false;
```

**Why:** the same failure family is documented in the code itself — Kaelen
completed EARN_GOLD 13,111 times at 60 ms intervals with progress 0. A
completion with no gold delta is a goal succeeding at nothing.

**How to apply:** when touching any Do* goal, audit every `return true`. Ask
"did anything actually move?" — gold, an item count, a position. If not, it is
a stand-down. `NoteProgress` belongs only inside a verified delta
(`life::Verdict::Confirmed`, which requires gold in AND goods out), never after
a packet is merely sent.

See [[npc-price-floor-design]].
