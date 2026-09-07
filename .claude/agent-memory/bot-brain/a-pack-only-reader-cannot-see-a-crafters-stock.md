---
name: a-pack-only-reader-cannot-see-a-crafters-stock
description: A crafter's finished goods live in the BANK, so any need or gate that reads obs.pack alone reads a busy crafter as empty-handed
metadata:
  type: project
---

Two live cases, same shape, 2026-09-07:

* `CutClothForSale` counted `client.BackpackItemCount(kClothGraphic)` only, and
  ran only from MAKE_CLOTH's "the batch is covered" exit. Aelia had BANK
  `i_cloth` x280 and PACK x10 and cut none of it in a whole gate — she was
  still shearing (`artifacts/gate_bandage20_20260907/`).
* `NeedCloth`'s shortfall (`src/life/Needs.cpp`, `clothShort`) reads
  `QtyIn(obs.pack, craft.item)`, so the same 280 banked cloth did not stop the
  need firing either.

`obs.bank` exists precisely for this and costs nothing: `Runner::Observe`
(`src/life/runner/Core.cpp` ~505-531) fills it from the OPEN box when
`obs.atBank`, and from `state_.bank` — what the character REMEMBERS — when away.
That is not omniscience, it is remembering your own container.

**How to apply:** when writing or auditing a need/gate about how much of
something a life HAS, ask whether the thing is a crafter's output or a bulk
input. If it is, `pack + bank` is the honest count, and the handler must then
own the withdrawal (`runner/Bank.cpp` `IssueBankItemMove` + `bankErrand_`), not
assume the goods are in hand. Sizing the withdrawal and arming the need from
ONE shared resolver keeps them from drifting.

Related: [[stock-before-you-train]], [[a-cut-consumes-the-whole-stack]],
[[one-resolver-for-need-and-handler]].
