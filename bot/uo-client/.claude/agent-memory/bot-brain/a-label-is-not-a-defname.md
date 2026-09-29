---
name: a-label-is-not-a-defname
description: prof::ConsumableNeed::name is a human label ("bandage"), and obs.pack/produces/PriceBook are all keyed by defname — a want named by the label can never match a producer, a pack or a price
metadata:
  type: project
---

Three catalogue fields look like they name the same thing and do not:

- `prof::ConsumableNeed::name` is a HUMAN LABEL — "bandage", "heal potion",
  "food". `life::WantsConsumable(cfg, "bandage")` and
  `ResolveConsumableThresholds` match on it, so it cannot be renamed.
- `produces` / `consumes` are DEFNAMES — "i_bandage", "i_cloth".
- `obs.pack` is keyed by defname, and `Runner::Observe` (runner/Core.cpp)
  fills it from `produces` + `consumes` ONLY (plus metals and reagents).

Consequences that bit on 2026-09-07 (bandage supply slice):

- `market::Shortfall`'s consumables arm emits `w.item = c.name`, so
  `WhoProduces("bandage")` was empty, the want was `rawResource`, and
  `PlayerMarketWants` dropped it. No fighter could ever announce
  "WTB i_bandage", so the tailor who can cut bandages had no customer.
- An item on NEITHER list is invisible to the pack: `QtyOf(pack, "i_bandage")`
  was permanently 0 for fighters, so a want sized "floor minus held" would
  have asked for the whole floor with a hundred already carried.

**How to apply:** to make an item tradeable between two lives, put the DEFNAME
in the producer's `produces` and the consumer's `consumes`. That single change
gives you the pack count, the non-empty `WhoProduces`, the `Surplus()` offer
and the `Shortfall()` want at once. Then check the other `consumes` readers
before you commit: `RoleOfGraphic` ranks a declared consumable above a
declared input (fine), but `runner/Bank.cpp`'s input-deposit pass does not —
it read i_bandage as a craft input and would have boxed a fighter's medicine
down to `craftBatch*2`.

Related: [[one-resolver-for-need-and-handler]], [[two-sale-questions]],
[[demand-needs-a-voice]].
