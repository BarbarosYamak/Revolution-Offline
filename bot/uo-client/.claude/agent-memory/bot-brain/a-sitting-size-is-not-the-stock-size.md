---
name: a-sitting-size-is-not-the-stock-size
description: CraftBatchFromStock sizes the shopping trip but never the bench; the sitting is the constant craftBatch=5, so ten sittings of five is what a bulk buy actually produces
metadata:
  type: project
---

`CraftBatchFromStock` is consulted by the NEED model and by BUY_SUPPLIES, but
the goal that works the bench asks a different question:
`req.desiredTotal = (now - craftMade_) + needCfg_.craftBatch`. `craftBatch` is
a hard `5` in `include/uo/life.h` and is never assigned anywhere in the tree —
one grep hit, the initialiser.

**Why:** Elara's 2026-09-04 post-fix run bought 73 nightshade in one order and
then made ten separate five-potion sittings, each with a full planner
re-selection in between (`craft_focus=... sittings_in_a_row=1..10`). The bulk
buy worked; the bench never learned the order was bulk. The owner rule is
"stock in bulk, then work the bench until stock is spent, sizes dynamic per
character" — three of those four words land in the shopping half only.

**How to apply:** when a crafter looks like it is "only doing one batch per
goal pick", check `Craft.cpp`'s `desiredTotal`, not the need model. And note
the related shape: `CraftFocus::kFocusRun` is 4, but a satiated recipe is
still returned when nothing else is workable (`Identity.cpp`, the `satiated`
fallback) — so the 4-sitting rotation only bites when a SECOND recipe is fully
stocked. Ten in a row is legal. See [[craft-focus-rotates]],
[[a-bulk-buy-sets-the-batch-size]], [[thresholds-are-rates-not-numbers]].
