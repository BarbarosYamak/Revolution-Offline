---
name: revolution-creature-looks
description: Owner PLAYER_MEMORY of how artless creatures looked/dropped on Revolution — swamp tentacles=kraken +9, bog thing=rotting corpse +9, savages=human hue 1425, and sibling redirects
metadata:
  type: project
---

Owner recollection (PLAYER_MEMORY, 2026-09-04) for creatures whose bodies have no frames in the Revolution client (docs/REVOLUTION_BODY_ID_CATALOGUE.tsv, 94 spawned artless bodies):

- savage warrior/rider/shaman → human body, COLOR 1425 (being applied)
- swamp tentacles → looked like a kraken, occasional +9 (vanquishing) weapon drop
- bog thing → looked like a rotting corpse, occasional +9 drop
- ratman archer/mage → ratman body; harpy_stone → harpy; lich lord/ancient → lich; dread/frost spider → giant spider; ophidian knight/archmage → ophidian; orc lord/shaman/bomber/brute/scout → orc; shadow/ancient/white wyrm → dragon body
- kraken → corpser body (0x08) — DONE 2026-09-04 (c_monster_lbr.scp)
- +9 dropper: BOTH swamp tentacles and bog thing (owner 2026-09-04)
- rotting corpse used the bog thing visual (owner); bog thing 0x30C is artless too — drawable target still UNKNOWN (asked: corpser mound?)
- orc lord / orc brute: keep DISPID=c_orc_captain (owner 2026-09-04)
- UNKNOWN still: what rotting corpse (0x9B) / bog thing drew as — both artless too; also bears, cougar, sea serpent deep, ghoul, imp, crow, elementals, daemons, evil mages, wraiths, LBR/AOS list (juka, bogling, plague beast, solen, changeling, etc.)

**Why:** owner judges bots by eye in the real client; invisible mobs break that, and the fix is content (ID= redirect + hue + loot) not code.

**How to apply:** batch redirects to sphere-expert per group; keep DEFNAME/stats/spawn identity; add +9 loot chance only with owner confirmation of rate. Mark anything not in this list UNKNOWN — don't invent. Related: [[artless-bodies-redirect]].
