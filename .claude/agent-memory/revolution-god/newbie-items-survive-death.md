---
name: newbie-items-survive-death
description: Verified 2026-09-06 — creation/starter items carry ATTR=04 (ATTR_NEWBIE) and stay in the pack on death; everything else goes to the corpse
metadata:
  type: project
---

Odessa died twice on 2026-09-06 and kept i_pickaxe, i_tinker_tools, i_dagger; the world save shows ATTR=04
(ATTR_NEWBIE) on each. Gears, bandages, potions, reagents and loot went to the corpse.

**Why:** the "death = full loot loss" model (m2-complete-test-characters) is right for earned items but wrong for
creation kit; recovery planning that assumes tools are lost sends bots shopping for things they hold.

**How to apply:** death-recovery logic (RECOVER_CORPSE, re-equip) should read the live pack, not assume; corpse
value = non-newbie items only. Bought replacements of a tool are NOT newbie and will be lost next time.
