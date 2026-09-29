---
name: atlasgen-skip-grid-drops-forests
description: uo_atlasgen derives lumber places only from the navgrid; a --skip-grid refresh once emitted an atlas with zero lumber rows, and the cluster size decides whether a town gets seeded at all
metadata:
  type: project
---

`DeriveForests` in `src/world/AtlasGenMain.cpp` is the ONLY source of
`resources=lumber` PLACE rows — the shard's scripts have no forest table. It
reads `kCellForest` flags out of the navgrid, so it used to run only when the
grid was rebuilt. A `--skip-grid` refresh (commit ad146a9 "atlas refreshed")
therefore took the atlas from 43 lumber rows to 0 and no lumberjack anywhere
had a destination for months. Fixed 2026-09-06: `--skip-grid` now loads the
saved `data/revolution_navgrid.bin` and derives forests from it, or warns
loudly.

**Why:** the flags were never lost — the saved grid still holds 6999 forest
cells — only the derivation step was skipped, and nothing in the atlas said so.

**How to apply:** after ANY atlas regeneration, check
`awk -F'\t' '$1=="PLACE"{print $10}' data/revolution_atlas.txt | sort | uniq -c`
and confirm lumber is still there. Also watch the cluster size: derived places
must land within `life::kNewbieKnowledgeRadius` (200 tiles, newbie_knowledge.h)
of a town's ATLAS REGION CENTRE or `SeedNewbieKnowledge` gives that town's
gatherers no lead. 8x8-cell blocks put Britain's nearest wood 238 tiles out and
seeded 6 of 16 towns; 4x4 blocks with a 48-tile separation rule seed 13 of 16.
Guarded anchors are dropped (owner rule: no gathering in guarded zones) and
anchors with `Cell::edges == 0` are islands the walker cannot reach.
Related: [[travel-arrived-is-not-arrived]].
