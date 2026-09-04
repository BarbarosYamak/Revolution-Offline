---
name: crafters-stock-then-sit
description: Owner rule 2026-09-04 — every crafter buys/gathers bulk materials first, then sits at the bench for one long sitting sized by the pack (not a batch of 5); alchemist run (89aa11e) is the template; server-side DELAY per craft skill set by owner ruling
metadata:
  type: feedback
---

Every crafter: stock bulk materials first, then one long craft sitting sized by what the pack funds (`craftSittingTarget_` = max(craftBatch, inputs available), `.makelast N`, shard cap 500). EARN_GOLD must not preempt a funded bench (urgency capped 0.40 while `StillMakeable > 0`). Alchemist is the template; scribe verified 2026-09-04 (89 blank + 91 reagents bought, 55-66 scrolls in a row).

Server-side craft DELAY (owner ruling 2026-09-04, PLAYER_MEMORY "no quick crafting"): carto 4, alch 4, blacksmith 5, carpentry 5, tailor 4, tinker 4, bowcraft 3, cooking 2, inscription 4, mining/lumberjack 3. Bot CraftStrokeMs table mirrors it.

**Why:** owner: "we bought lots of materials for alchemy and worked craft for a long time that should be the norm for almost all crafters". Short sittings looked like NPC behaviour and starved skill gain.

**How to apply:** any new crafter loop (tailor, tinker, carpenter, bowyer, cook) must show bulk stock then long sitting in its smoke before its loop counts as closed. Do not lower shard DELAY to speed tests.
