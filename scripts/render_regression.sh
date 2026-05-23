#!/usr/bin/env bash
# Renderer visual-regression harness.
#
# Every scene below is a world coordinate that once exposed a draw-order /
# layering bug in src/render/Renderer.cpp. After ANY renderer change, run this
# and eyeball each PNG in build/regression/ against the official 2.0.7 client —
# the whole point is to never silently re-break a scene a previous fix nailed.
#
#   ./scripts/render_regression.sh
#
# Args to world_viewer.exe: camX camY width height scale camZ outPng
# (camZ matters for the roof/floor cutoff; pick the player's z in that scene.)

set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/regression

# Compile the viewer once (build_viewer.bat recompiles + runs the first scene).
cmd.exe //c "scripts\\build_viewer.bat 2000 2803 800 600 1 0 build/regression/01_coast.png"

EXE="build/world_viewer.exe"

# 02 beige building — wooden upper floor must NOT bleed over the beige stucco
#    wall below it (same-cell wall z10/h20 + floor z30).
"$EXE" 1510 1619 420 340 1 30 build/regression/02_beige_wall.png

# 03 stone keep — interior pavers stay bounded by the exterior walls.
"$EXE" 1515 1616 380 300 1 10 build/regression/03_stone_keep.png

# 04 castle battlement — wooden floor smooth; tall sandstone walls stay BELOW the
#    floor (no wall tops poking up through the planks); crenellations only at edges.
"$EXE" 1805 2818 800 600 1 40 build/regression/04_castle_battlement.png

# 05 three-storey house, 2nd floor — floor planks must not bleed over the table
#    (z20/h6 surface) or the staircase base; 3rd floor (z40) culled by roof cutoff.
"$EXE" 1810 2806 800 600 1 20 build/regression/05_floor2_table_stairs.png

# 06 forest — tree leaf canopies (Foliage flag 0x20000) draw OVER their trunks;
#    trees look lush, not bare.
"$EXE" 1706 2651 800 600 1 0 build/regression/06_forest_foliage.png

echo "regression scenes dumped to build/regression/  (compare against the official client)"
