---
name: sphere-rnn-loot-chance
description: Sphere loot `ITEM=x,Rnn` means 1-in-nn chance (CItem.cpp:479-483) — R99≈1%, R50=2%, R25=4%, R3=33%; verified source, two audits had left it UNKNOWN
metadata:
  type: project
---

`ITEM=<def>,Rnn` in a Sphere @CreateLoot / template = **1 in nn chance** to create the item. Source: `server/Source-X/src/game/items/CItem.cpp:479-483` — `if (g_Rand.GetVal(atoi(cmd+1))) return nullptr;` (GetVal(n) yields 0..n-1; only 0 creates). Weighted selectors `{a w b w}` = single roll over summed weights (`CRandGroupDef.cpp`).

**Why:** two loot audits (docs/REVO_LOOT_CURRENT_STATE.md, artifacts/loot_proposal_tns_2026-09-04.md) flagged Rnn as UNKNOWN/HYPOTHESIS and couldn't set rates. Stock Scripts-X dragon map `i_ttm_l4,R99` ≈ 1%.

**How to apply:** convert every proposed loot rate to Rnn with this rule; quote percentages to the owner, not R-values.
