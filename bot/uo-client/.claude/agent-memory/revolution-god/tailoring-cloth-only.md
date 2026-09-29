---
name: tailoring-cloth-only
description: Owner ruling 2026-09-03 — Revolution tailoring needs only cloth (or hides); thread stripped from all recipes except robes; mage robe = Hardening Crystal + cloth per truth pack §9.1 (item not yet in runtime); plain i_robe is cloth only
metadata:
  type: project
---

Revolution tailoring recipes take only cloth (or cut hides for leather), no thread — except the mage robe and "special mage robes", whose extra ingredients are UNKNOWN.

**Why:** Owner (PLAYER_MEMORY, 2026-09-03): "at revo most of the tailor item needed only cloth except mage robe and special mage robes". Corroborated by runtime: no TNS vendor SELLs i_thread (only BUYs it), so the stock Scripts-X `1 i_thread` requirement was a dead gate that hid every craft-menu entry except hats. Applied 2026-09-03 to runtime + Scripts-X copies of i_provisions_clothing/armor/misc (227 recipes). `i_robe`, `i_robe2/3`, `i_robe_arcane`, elven/gargish robes still carry thread until the real Revolution robe recipe is known.

Same day, same owner: **Armslore is not a Revolution skill** — stripped from every SKILLMAKE (174 recipes: leather, metal armour, shields) and from the smith/tailor bot builds (`2f5ee9e`). Leather/studded/boots are made from `i_hides_cut` (the 0x1067 leather pile, TYPE t_leather). Plain leather = leather only; **studded keeps its iron ingots** ("makes sense", owner). SE gear (kimono/kamishimo/hakama) already pruned from craft menus 2026-09-02.

**How to apply:** Sphere's legacy skillmenu hides an entry unless skill AND resources are in the pack (`Cmd_Skill_Menu_Build` → `Skill_MakeItem(SKTRIG_SELECT)`), so a "missing" craft entry is usually skill or materials, not a script bug. Tailor bots need cloth only; do not make them hunt thread. If the robe recipe surfaces (forum/changelog), update robes and this note.
