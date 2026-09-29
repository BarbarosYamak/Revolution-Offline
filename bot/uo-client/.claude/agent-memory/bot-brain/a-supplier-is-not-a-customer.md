---
name: a-supplier-is-not-a-customer
description: A crafter with empty shelves must not outbid its own trade with comforts — damp the comfort needs rather than pinning BUY_SUPPLIES to the front of the day
metadata:
  type: feedback
---

A life that MAKES a good must not open its day by BUYING that good, and its
comforts must not outbid the trade that pays for them.

**Why:** owner ruling 2026-09-04 — an alchemist opens by buying a lot of
nightshade and empty bottles and brewing poison (the brew batch is both the
training and the stock to sell), and "she must NOT open by buying NPC heal
potions". Elara's fresh session on 2026-09-03 went BUY_MOUNT (204) -> BANK
(180) -> REPLACE_EQUIPMENT buying four NPC heal potions (130, progress=0) ->
FILL_SPELLBOOK (77), with NeedSupplies at 62 never winning — while carrying a
mortar, Alchemy 50.0 and `i_potion_heal` in her own `produces` at a gate of
15.1.

**How to apply:** express it as need scoring, never as a goal order.

* The predicate is `unstockedCrafter` in `src/life/Needs.cpp`: first `income`
  is `Craft`, `ChooseCraft` at batch 1 cannot make anything, the shortfall is
  NPC-buyable, and there is working capital. The gate is the SHORTFALL, not the
  job title — a lumberjack is short of logs and a tailor of cloth, both refused
  by the vendor policy, so neither is ever touched.
* NeedSupplies is a rate between 0.95 and a 0.44 floor
  (`madeable / stockBatch`), so a balanced crafter still crafts (65) before it
  shops (62) and the old scribe fix is preserved.
* Comfort needs get damped, not deleted: NeedMount 0.80 -> 0.25, and
  NeedEquipment("heal potions") 0.50 -> 0.20 only when the profession's own
  `produces` names a heal potion AND the carried skill reaches that recipe's
  gate (`BrewsOwnHealPotion`). No weight under the emergency band beats
  BUY_MOUNT's 255, so raising the errand alone can never be enough.

Related: [[a-bulk-buy-sets-the-batch-size]], [[two-sale-questions]],
[[thresholds-are-rates-not-numbers]].
