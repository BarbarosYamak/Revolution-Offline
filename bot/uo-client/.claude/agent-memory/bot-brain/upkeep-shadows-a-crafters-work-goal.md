---
name: upkeep-shadows-a-crafters-work-goal
description: REPLACE_EQUIPMENT scores 260 x 0.50 = 130 on an empty potion pouch or bandage kit and outranks EVERY work goal a crafter has; set the comforts before asserting a work pick
metadata:
  type: project
---

Writing the planner-pick test for BANDAGES_FOR_SALE, the pick kept coming back
REPLACE_EQUIPMENT 130.00 while the goal under test scored 87.75. The cause was
not the new goal: `NeedEquipment("heal potions")` fires at 0.50 with weight 260
whenever a crafter holds fewer than its low of 2 — and the same clause exists
for bandages. 260 x 0.50 beats every work weight in the table (Craft 130,
MakeCloth 135, EarnGold 150 x its lower urgency).

Two consequences worth remembering:

* **In a test**, a work-goal pick assertion needs the comforts satisfied —
  `obs.healPotions`, `obs.bandages`, `obs.food` — or you are asserting against
  UPKEEP, not against the goal you meant to compare with. `Planner::Score` prints
  every candidate with its `need urgency x weight = score` reason; dump that
  before changing a weight.
* **Live**, the code already knows this and damps rather than re-weights:
  `BandageCountersEmpty` / `PotionCountersEmpty` (`src/life/Needs.cpp` ~491-520)
  read the `bandage_counters_empty` / `potion_counters_empty` memory events that
  `DoReplaceEquipment` writes when it gives up, and drop the urgency to 0.10 so
  "the shop errand yields to the cloth-cutting one". The fix for "upkeep is
  crowding out work" is a recorded observation, never a weight bump.

**How to apply:** never raise a work goal's weight to beat REPLACE_EQUIPMENT. If
a life genuinely cannot buy the comfort (0 gold, drained counters), the honest
lever is the recorded fact that damps the shop need.

Related: [[a-blocked-need-shadows-a-ready-one]], [[thresholds-must-be-dynamic]].
