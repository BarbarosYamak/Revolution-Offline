---
name: a-standdown-that-forgets-reopens
description: Copying StandDownBandageShopping's counter reset into the potion errand made the next tick begin a fresh visit to the same empty shelf; a stand-down only clears its memory when it changes route
metadata:
  type: feedback
---

A stand-down may clear the counter that triggered it **only if it hands the
work to a different route**. If there is no second route, clearing it means the
very next tick re-opens the errand it just closed.

**Why:** `StandDownBandageShopping` (Gear.cpp) ends with
`bandageCountersDrained_ = 0` and that is correct *for bandages* — it hands off
to `MakeBandages`, so the shop route is genuinely over and the town's emptiness
is carried in the `bandage_counters_empty` memory event instead. I copied the
same reset into the new heal-potion stand-down (D10, 2026-09-06). A life that
cannot brew has no second way to a heal potion, so the reset simply erased the
one fact that was suppressing the visit: the harness scenario drove
`DoReplaceEquipment` five times and the potion errand was running again on call
two. Dropping the reset — letting `potionCountersDrained_` expire naturally with
`drainedShelves_` on `kShelfRestockMs` — fixed it.

**How to apply:** when adding a stand-down, ask what the character does INSTEAD.
Handoff to another producing goal -> clearing is safe, and the durable fact
belongs in a session-tagged memory event. No alternative -> the suppressing
state must outlive the stand-down, and the only honest expiry is the shard
clock the fact came from.

Related: [[an-errands-exit-cannot-live-in-the-errand]],
[[ms-stand-downs-die-with-the-process]].
