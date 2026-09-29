---
name: a-category-is-not-a-difficulty
description: Graveyard-category places now mix a weak band with lich rings; tier comes from the row id suffix and the strong gate is UNKNOWN/shut
metadata:
  type: project
---

A world-model CATEGORY answers "what kind of place is this", never "can this
character survive it". After the 2026-09-06 tier split the atlas holds, per
yard, one whole-yard band row (`britain_graveyard_graveyard`, radius 12) plus
one small ring row per strong spawner (`britain_graveyard_knights`, `_lich`,
`_lich_lord`, radius 2-5) — **all category Graveyard**. "Nearest graveyard"
therefore stopped meaning "the easy yard" the day the split landed.

**Why:** `Atlas::NearestHuntingGround` picked the nearest Graveyard place, so
`DoTrainCombat`'s seasoned branch could route a 50-skill fighter into the lich
ring. Chardefs (`runtime/scripts/npcs/c_monster_classic.scp`):
`c_skeleton` DAM 3,7 / 34-48 hits vs `c_skeleton_knight` DAM 18,43 / 118-150
hits and `c_lich_lord` DAM 30,38 / 250-303 hits, Magery 90-100.

**How to apply:**
- Tier is read off the ROW, never coordinates: the band row's id ends in
  `_graveyard`, a ring's ends in the creature. Unrecognised ⇒ Strong
  (fail-dangerous). `world_atlas::HuntTierOf`, `NearestHuntingGroundOfTier`;
  the untiered `NearestHuntingGround` means weak tier.
- The strong gate (`ClearsStrongHuntTier`, Train.cpp) returns false for
  EVERYONE and that is deliberate: a survivable-skill threshold needs the
  shard's damage-after-armour rule plus the character's AR, and `Observation`
  carries no AR at all. Derivation: `artifacts/hunt_tier_gate_2026-09-06.md`.
  Do not open it on reasoning; open it on a measured survival.
- The weak band is already at the fleet's limit: two plain skeletons killed
  Hector (51 hp, 50.7 fencing) on 2026-09-06 twice in one day.
- Known data gap: `jhelom_cemetary_graveyard` reads weak but the spawn table
  puts a knight, a lich and a lich lord in that yard with no ring rows emitted;
  same for `city_of_the_dead_graveyard` and the T2A entrance/exit graveyards.
  Indistinguishable from Yew (a genuinely weak newbie yard) in the atlas.

See also [[goals-addressed-to-nobody]], [[thresholds-are-rates-not-numbers]].
