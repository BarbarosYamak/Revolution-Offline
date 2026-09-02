---
name: vendor-shelves-are-random
description: Mage shops roll four random scrolls per restock, so "this shopkeeper stocks none" is one NPC's roll and must not fail the goal
metadata:
  type: project
---

`[TEMPLATE VENDOR_S_MAGE_SHOP]` sells `random_first_circle` ..
`random_fourth_circle`, `{4 24}`
(`runtime/scripts/templates/tm_vend.scp:717-742`). That is FOUR RANDOM SCROLL
SLOTS, not a fixed catalogue. Confirmed live: Aurelius's vendor window offered
exactly four scroll SKUs — Feeblemind, Strength, Telekinisis, Fire Field.
`{4 24}` is the stock band, not a price.

**Why:** Thalia failed FILL_SPELLBOOK outright on "this 'mage' does not stock
a scroll (4 already known)". Her observation was right and her conclusion was
wrong: the next mage's roll is different, and so is the same mage's after a
restock.

**How to apply:** treat "this shopkeeper has nothing I need" as a fact about
one NPC. Record the serial in the errand's skip list, spend a trip, and try
another shop of the same service — passing the skip list to BOTH the
shopkeeper lookup and `TravelToServiceSkipping` (the sighting cache in
`ClientTravel.cpp` will otherwise walk straight back to the same NPC). Fail
the goal only when the trip budget is spent. The restock timer is not saved
across shard restarts.
