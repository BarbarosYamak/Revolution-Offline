---
name: a-bulk-buy-sets-the-batch-size
description: A fixed craftBatch makes bulk buying pointless — 84 nightshade and 4 bottles read as "stocked"; size the sitting off the best-stocked input instead
metadata:
  type: project
---

A crafter's sitting size must be read off what the pack has already paid for,
not from a constant. `NeedConfig::craftBatch` is 5 for every character, and it
silently cancelled a bulk stocking trip: Elara bought 84 nightshade (42 poisons'
worth), kept the four empty bottles from her newbie kit, and the need model
asked "am I stocked for FIVE potions?" — one bottle short. One short is small
enough that the two-question rule ("can I make one right now?") answered yes and
no shopping errand was raised at all. She brewed twice and stopped with 82 leaves
in the pack.

**Why:** the two questions in `AssessNeeds` (batch 1, then `craftBatch`) both
size the sitting at a fixed number, so the answer to "is the pack balanced?"
depends on a constant rather than on the pack. `CraftBatchFromStock`
(`include/uo/life.h`, defined in `src/life/Identity.cpp`) takes the batch from
the BEST-stocked input of the chosen recipe. That input is by definition not
short at that size, so the rule can never invent a shortfall of the thing just
bought — it only ever names the other half of a recipe already half paid for,
and it converges when the recipe balances.

**How to apply:** whenever a need or a goal asks `ChooseCraft` for anything
other than "can I make ONE", pass `CraftBatchFromStock(...)`, not
`needCfg_.craftBatch` — the need model and `DoBuySupplies` must ask the same
question or the goal reports "nothing short after all" for the very shortfall
that selected it. The matching purchase quantity is `BulkSupplyQty`
(`src/life/runner/Economy.cpp`): budget above `goldReserve`, split across the
recipe inputs still to be bought, divided by the recipe ratio times the quoted
price, then capped by shelf stock and by carry weight. Related:
[[thresholds-are-rates-not-numbers]], [[craft-focus-rotates]].
