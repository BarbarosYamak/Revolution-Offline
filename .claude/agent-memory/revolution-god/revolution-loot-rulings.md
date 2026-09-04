---
name: revolution-loot-rulings
description: Owner PLAYER_MEMORY loot rates 2026-09-04 — dragon 1-in-4 weapon, 1-in-5 map, small/big dragon split, crystals, low mobs +3 1-in-3, lich/skel mage 5-8th scrolls, maps dragons+balrons only
metadata:
  type: project
---

Owner rulings (PLAYER_MEMORY, 2026-09-04), implemented in runtime/scripts/revolution/revolution_loot.scp + live @CreateLoot rows (see artifacts/loot_pass_2026-09-04.md):

- Small dragon (c_dragon): 400-500 gp, magic weapon 1 in 4 (+6/+9), L4 map 1 in 5, hardening crystal.
- Big dragons (L5 fire/ice/earth/energy serpentine dragons, ancient wyrm): 1000-1200 gp, weapon 1 in 4 with +12 possible, L5 map 1 in 5, hardening + matching elemental crystal (fire/ice/earth/energy).
- Balron/infernal ≈ dragon. Treasure maps from dragons and balrons ONLY.
- Low mobs: +3 1 in 3, +6 1 in 6.
- Lich + skeleton mage drop 5th-8th circle scrolls (dungeons). NPC mages sell up to 4th only.
- Magic armor from mobs: UNKNOWN (forum check pending — guest sees one board).
- Live since Sphere restart 19:08 2026-09-04 (loaded clean). Low mobs (20 chardefs: orc/ettin/skeleton/ghoul/zombie/ogre/troll/lizardman/ratman + variants) got `random_weapon_ruin,R3` + `random_weapon_might,R6`; 44 stray R99 map lines commented. Gold rebalanced: c_dragon 400-500, serpentine/ancient wyrm/balron 1000-1200 — world-economy should watch this tap.
- Sphere restart route when console has no window and Observer is "already in use": confirm fresh autosave with 0 clients, taskkill, Start-Process from runtime/ (statics not rewritten — only OK if no static edits that day).
- Placeholders flagged OWNER_TUNE: crystal rates (hardening 1/10, elemental 1/5), scroll rate 1/3 with 5th>6th>7th>8th weights, map levels.

**Why:** creature drop tables were stock Scripts-X (magic commented, maps 1%); owner killed dragons and got nothing. Economy tap concern: low-mob +3 at 33% is generous — world-economy should watch +N supply vs player market.

**How to apply:** don't re-derive; tune only with owner. Gold tap review belongs to world-economy after a wave.
