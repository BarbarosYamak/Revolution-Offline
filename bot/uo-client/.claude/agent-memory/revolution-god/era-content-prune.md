---
name: era-content-prune
description: Owner 2026-09-02 — Scripts-X carries Samurai Empire / ninja-era clothing (kimono, hakama, tabi…) Revolution never had; prune from tailor craft menus, vendor stock, loot, and the bot recipe table
metadata:
  type: project
---

Owner flagged (2026-09-02): "lots of clothing from japanese or ninja
times that we don't use." Revolution was pre-SE; SE/ML/SA-era items in
Scripts-X (kimono, hakama, obi, ninja tabi, kasa, etc.) should not be
craftable, sold, or dropped.

**Why:** Fidelity — a tailor bot rotating CraftFocus across the stock
menu would sew kimonos nobody on Revolution ever wore; NPC stock and loot
also leak era-wrong items into the economy.

Done 2026-09-02: NPC wardrobe pools pruned by hand — `tm_generic.scp`
random_* human/all/manly/shoes/hats/masks/aprons/tunics/robes/dresses and
`tm_misc.scp` random_clothing_* (backups in bot/uo-client/artifacts/).
Root cause of "bots in kimono": 215 NPCs (not bots) dressed from those
pools (hakama_tattsuke was 20% of human pants). Existing NPCs keep their
clothes until respawn/cleanup. Bot creation pool was never affected
(client skills 0-48, no samurai/ninja profession byte).

Owner confirmed 2026-09-02: "samurai etc stuff we don't use anywhere" —
whole SE layer is out of scope for Revolution (spawns, gear, craft, vendor,
loot, skills). 216 existing NPCs (mostly placed vendors: scribes, mageshop,
fishers, smiths, tailors, bankers) wear SE items and never respawn → needs
one-off server-side wardrobe cleanup at restart.

Also elven (ML) and gargoyle (SA) content — owner 2026-09-02: "we don't
use those either". Gargoyle vendor bodies: 0 spawned; elven items still in
the `_all` wardrobe pools after my hand-prune. Same sphere-expert slice.

**How to apply:** sphere-expert audit of tailor (and other) craft menus,
`tm_vend.scp` SELL rows and loot templates for SE+ items, disable with
evidence (client art era, Revolution item lists); prune the bot-side
recipe/CraftFocus table to match. Not yet done as of 2026-09-02.
