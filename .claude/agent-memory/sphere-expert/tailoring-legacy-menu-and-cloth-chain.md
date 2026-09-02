---
name: tailoring-legacy-menu-and-cloth-chain
description: Legacy tailoring skillmenu route strings for sash/robe/leather tunic, plus the wool->cloth chain being SphereServer-hardcoded (not scripted)
metadata:
  type: project
---

Shard runs legacy tailoring menus: `scp.NewCrafting_Tailoring=0`
(server/Scripts-X/crafting/crafting_settings.scp:32). CURRENT_SCRIPT source
is `server/Scripts-X/crafting/interface/legacy skillmenu/sm_legacy_tailoring.scp`.

**Root selection happens before the table.** Dclick `i_sewing_kit` opens a
target cursor (CURRENT_SOURCE: CClientUse.cpp:551-557), then the *targeted
material's type* — not the item name — picks the root menu (CClientTarg.cpp
:2383-2399, hardcoded, no script hook): `IT_LEATHER`/`IT_HIDE` ->
`sm_tailor_leather`, `IT_CLOTH` -> `sm_tailor_cloth`. Bot-core must target
leather for i_leather_tunic and cloth for i_sash/i_robe before walking
kCraftMenus.

Route strings for `bot::rules::kCraftMenus` (Identity.cpp:358), level1/2
only, both plain substrings (no collision found):
- `{"i_sash", "Misc.", "sash", nullptr}` — sm_tailor_cloth -> "Misc." (sm_cloth_misc) -> `i_sash`
- `{"i_robe", "Shirts", "robe", nullptr}` — sm_tailor_cloth -> "Shirts" (sm_cloth_shirts) -> `i_robe`
- `{"i_leather_tunic", "Leather Armour", "leather tunic", nullptr}` — sm_tailor_leather -> "Leather Armour" (sm_leather_armor) -> `i_leather_tunic`

Level3 text for these three items is unresolvable from script (no `NAME=`
tag on the itemdefs, item_provisions_clothing.scp:328/989,
item_provisions_armor.scp:256) — same situation as the existing `i_dagger`
-> "dagger" entry, which also has no NAME=. Follows the codebase's existing
convention of using the client tiledata name (HISTORICAL_REFERENCE,
standard UO name, not invented). "Leather Armour" (level1, no collision
with "Studded Armour"/"Female Armour"/leather-root "Misc") vs cloth-root
"Misc." are on different root menus so the literal string overlap between
leather's "Misc" and cloth's "Misc." never actually collides in practice.

**Wool -> cloth chain is engine-hardcoded, not scriptable** (CURRENT_SOURCE,
Source-X/src/game/clients/CClientTarg.cpp):
1. Dclick `i_wool` (or `i_cotton`), target `i_spinwheel` (IT_SPINWHEEL,
   :2053-2085) -> wool yields **`i_yarn_ball` x3** (ITEMID_YARN1), cotton
   yields `i_thread` x6 (ITEMID_THREAD1). Wool never makes thread.
2. Dclick yarn OR thread (IT_YARN/IT_THREAD, :2186-2266), target
   `i_loom_upright` (IT_LOOM) repeatedly until 4 units accumulated -> loom
   produces one `i_cloth_bolt` (ITEMID_CLOTH_BOLT1). Both yarn and thread
   work on the loom.
3. Scissors on the bolt (`ConvertBolttoCloth`) -> `i_cloth` (folded cloth),
   the RESOURCES currency used by tailoring recipes.

So bot logs reporting `MAKE_CLOTH` as short `i_yarn_ball` are describing a
correct intermediate step of the wool route (step 1 above), not a bug —
yarn is the wool-derived spinwheel product and is loom-valid. Confirms/
supersedes the "thread-vs-yarn quirk is UNKNOWN" line in
[[tailor-cloth-source]]: it is now KNOWN — wool -> yarn is the intended
route to cloth, cotton -> thread is the alternate route, both feed the same
loom slot.

Item type IDs (defs_types_hardcoded.scp): t_loom=116, t_scissors=147,
t_thread=148, t_yarn=149, t_spinwheel=150, t_sewing_kit=170 (item_types.h).
