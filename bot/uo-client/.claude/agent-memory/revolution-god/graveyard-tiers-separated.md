---
name: graveyard-tiers-separated
description: Owner rule 2026-09-06 — strong undead (skeletal knight, lich, lich lord) stay in the Britain graveyard but in a separate part from the weak band, so novices can focus on the weak side
metadata:
  type: project
---

Owner ruling 2026-09-06 on the Britain graveyard: keep the strong tier
(c_skeleton_knight x2, lich, lich lord — our HYPOTHESIS fill of TNS
SPAWN_Undead_Strong) but move it to a different part of the graveyard,
spatially separated from the skeleton/zombie band, "so it is easy to focus".
Refinement (same day): the strong tier is THREE separate spots, not one
cluster -- skeletal knights, lich, and lich lord each get their own
non-overlapping ring, a difficulty gradient across the yard (weak band near
the bot entrance, knights further, lich further, lich lord at the far end).

**Why:** two novices (Aurir 03:28, Aurelius 17:57) died to the knight inside
the yard the project calls its early hunting ground; 17 undead in a 30x40
band made 3-on-1 the normal case (artifacts/britain_graveyard_threat_2026-09-06.md).

**How to apply:** shard-side spawner coordinates in
runtime/scripts/functions/worldgen/spawns/felucca/Graveyards_spawns_felucca.scp
(and the f_m40 mirror) — do not delete the strong tier; bots' hunting-ground
knowledge should learn the two halves (weak side for novices, strong side
for geared fighters), never a global "graveyard is safe" flag.
