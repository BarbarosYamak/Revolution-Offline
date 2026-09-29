---
name: the-supplier-table-disagrees-with-the-policy
description: SupplierTradeFor is a five-row hand table and it contradicts revolution_vendor_policy.tsv in both directions — a missing row silently reroutes to the player market, a wrong row hard-fails the goal
metadata:
  type: project
---

Two tables answer "where does this input come from" and they are not kept in
sync:

* `econ::CanBuyFromNPC` reads `data/revolution_vendor_policy.tsv` — the RULE.
* `SupplierTradeFor` in `src/life/runner/Economy.cpp` — a five-row hand table
  (reagents, `i_scroll_blank`, `i_bottle_empty`, `i_feather`, `i_kindling`) —
  the ROUTE.

The order in `DoBuySupplies` is route-then-rule, so the disagreements are
asymmetric and both are silent:

* **No row, policy allows** (`i_ingot_iron`, PLAYER_MARKET_GOOD buy=1): the
  route branch falls through to `RouteForInput`, finds a profession that
  produces it, and hands the errand to TRADE_WITH_PLAYER. The need model,
  which asks `CanBuyFromNPC`, was saying "go shopping". A merchant/tinker's
  entire input stream lives on this crack.
* **Row present, policy refuses** (`i_feather`, WORLD_GATHERED buy=0): the
  non-empty trade word SKIPS the RouteForInput branch entirely, so the
  gather/player fallback is never reached and the goal ends at
  `goal_failed=BUY_SUPPLIES` + a two-minute cooldown. An archer never gets a
  second answer about feathers.

**Why:** found in the 2026-09-04 crafter audit; it blocks two whole families
while every generic mechanism around it (the supplies-first gate, BulkSupplyQty)
is profession-agnostic and working.

**How to apply:** before concluding a family "cannot buy" or "is player-market
only", read BOTH tables. A route word is not permission and permission is not
a route. See [[goals-addressed-to-nobody]],
[[a-material-by-class-is-not-a-material-by-rule]].
