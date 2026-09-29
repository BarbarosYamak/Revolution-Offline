---
name: hunger-ticks-every-30-minutes
description: A 5-minute gate cannot observe eating — the shard states hunger once per 30 minutes and only below 40%, so a hunger/eat fix needs a 35-minute run on an already-hungry character
metadata:
  type: project
---

Anything gated on hunger needs a gate LONGER than 30 minutes to be observed
at all.

**Why:** `CChar::OnTickFood` emits the "You are <level>" warning only on a
food-decay tick and only while food <= 40% of max. `runtime/sphere.ini`
`Regen3=1800` makes that tick half-hourly (measured live 2026-09-02: Dorvar's
warnings at 10:09:43 and 10:39:45). Two 5-minute gates on characters with
FOOD=0/15 and FOOD=4/15 produced zero hunger lines and therefore zero eats —
a green run that proved nothing.

**How to apply:** pick the subject from the world save, not from the roster —
`FOOD` and `MAXFOOD` are in `runtime/save/spherechars.scp`, hungry is
`FOOD*8/MAXFOOD <= 2`. Prefer a character that already carries food so the
eat fires the moment the tick lands. Then run 35+ minutes. The same caution
applies to any slow shard timer (regen, restock, decay).
