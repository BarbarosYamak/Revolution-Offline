# Atlas lumber + stale trade-NPC memory — 2026-09-06 (navigation-world)

Base HEAD 2c6b9a8. Build `python tools/rev.py build test` -> ctest 45/45.
Smoke `python tools/rev.py gates CHARS=Vorar,Kharain MINUTES=5` (18:19-18:26).

## ITEM 1 — the atlas had no lumber

### Root cause (verified, source + data)

`uo_atlasgen` has always derived forests (`DeriveForests`, AtlasGenMain.cpp),
but only inside the `if (!skipGrid)` branch. A `--skip-grid` refresh therefore
emitted a forestless atlas:

| atlas commit | lumber PLACE rows |
|---|---|
| 9b7ca0b `feat(world): semantic destinations...` | 43 |
| ad146a9 `world: ... atlas refreshed` | 0 |
| 11f4c80 (worktree before this change) | 0 |

Checked with `git show <c>:data/revolution_atlas.txt | awk -F'\t' '$1=="PLACE"{print $10}' | sort | uniq -c`.

The saved `data/revolution_navgrid.bin` (2026-08-26) still carries the foliage
flags: 6999 `kCellForest` cells, 35222 passable, 59262 water. So the data was
never lost — only the derivation step was skipped.

Second cause, why the 43 rows would not have been enough anyway: the old
clustering used 8x8 cells (128x128 tiles), min 20/64 forest cells, best 3 per
region. Measured against the atlas' own town-region centres, that put the
nearest woods 238 tiles from Britain and 230 from Yew — both outside
`life::kNewbieKnowledgeRadius` (200, newbie_knowledge.h:41), so
`SeedNewbieKnowledge` seeded a Britain lumberjack no lead at all. Only 6 of 16
towns had a wood inside the seeding radius.

### Change

`src/world/AtlasGenMain.cpp`
- `Atlas::GuardedAt(x,y)` — any region with `kFlagGuarded` covering a tile.
- `DeriveForests`: 4x4 cells (64x64 tiles), min 4/16 forest cells (same ~25%
  share as before), anchors must be passable AND have at least one measured
  crossing (`Cell::edges != 0`, i.e. not an island), anchors inside a guarded
  region are dropped, and kept clusters must be >= 48 tiles apart instead of
  capped at 3 per region. `radius` 48 -> 16 (the anchor IS the foliage cell;
  `TravelToResource` caps arrival at 24 and a wide radius licenses stopping
  short of the trees).
- `--skip-grid` now loads the saved navgrid and still derives forests, or
  prints `WARNING ... this atlas will have NO lumber places`. This is the
  durability fix: the next "atlas refreshed" cannot silently drop lumber.

`src/life/runner/Gather.cpp` (GATHER_LOGS, no-stand-no-lead branch)
- Order swapped: ask `TravelToResource(Lumber)` FIRST, fall back to
  `StepOutOfGuardZone` only when the world knows no forest. The old order was
  written when the atlas had zero lumber rows (its comment says so).

### Regeneration

    build-m1/uo_atlasgen.exe --scripts runtime/scripts --skip-grid \
      --out-atlas data/revolution_atlas.txt --out-grid data/revolution_navgrid.bin

    atlasgen: --skip-grid: forests read back from data/revolution_navgrid.bin
    atlasgen: 6999 forest cells, 59262 water cells
    atlasgen: 801 forest blocks -> 676 woods (113 crowded out, 12 guarded, 0 unreachable)
    atlasgen: 1334 places, 540 transits

Whole file regenerated, not hand-edited. Diff vs the previous atlas: the
non-lumber PLACE set is byte-identical (`diff <(grep ^PLACE old | sort)
<(grep ^PLACE new | grep -v '\tlumber\t' | sort)` -> no output); REGION/RECT/
TRANSIT/MAP rows unchanged. The navgrid was NOT rebuilt, so navigation
behaviour is otherwise untouched.

### Offline verification (scratchpad scripts, atlas + navgrid only)

- 676 lumber places; **0** of them inside a guarded region.
- Nearest woods per town-region centre, and whether that is inside the 200-tile
  seeding radius: Britain 175, Cove 53, Buccaneer's Den 70, Moonglow 78,
  Magincia 100, Skara Brae 113, Vesper 117, Trinsic 131, Minoc 138, Jhelom 159,
  Yew 166, Serpent's Hold 173 — 13 of 16 towns seeded (was 6). Nujel'm 564,
  Wind 767, Heartwood 1903 have no measured foliage nearer (island / dungeon).
- Reachability: BFS over the navgrid's measured `edges` bits from the atlas'
  own `britain_bank` (1650,1608) reaches 17518 cells, including the cell of the
  Britain woods anchor (1320,1480).

### Live proof (run_gates/g_Vorar.console.txt, 5-min gate)

    :61  gather: no stand and no lead left; asking the world for lumber
    :62  [travel] Britain Territory woods -> (1320,1480) r=16 from (1411,1602)
    :69  [travel] plan Britain Territory woods: ok legs=6 ~216 tiles transit=0 nodes=347
    :136 [travel] Britain Territory woods ARRIVED at (1334,1496,10)

Start tile (1411,1602) is inside guarded regions Britain / Britain Public
Library; the arrival tile (1334,1496) is inside no guarded region. Counts in
this run: `no known source of that resource` **0** (was 131 in the wave-2 run),
`past_guard_line` **0** (was 107).

Logs gathered: **0**. Not for want of a destination — after arrival the planner
had already moved on (`REPLACE_EQUIPMENT 130.0 superseded GATHER_LOGS 72.0` at
18:19:45, bandages 84/100) and GATHER_LOGS was never re-picked in the remaining
four minutes (NeedLogs 0.40 sat under NeedTraining 0.65 / NeedEquipment 0.50).
`session_summary duration=409s goals=1/7 logs=+0 deaths=0`.

### New defect surfaced (NOT fixed here)

`run_gates/g_Vorar.err.txt`: repeated `no path to (1359,1456) avoiding 0
block(s); stopping (search ~1.6-2.0 s)` from (1392,1496), and later `no path to
(1416,1592)` from (1432,1560). The macro route's cells are connected by the
measured edge data but the tile A* exhausts its budget on those legs, near the
Britain graveyard wall. Vorar still completed the trip via other legs. This is a
tile-A*/leg-budget question, not an atlas one.

## ITEM 2 — stale trade-NPC memory

### Root cause (verified, wave-2 logs)

Two different shapes were behind "walks to the same spot three times":

1. Remembered buyer. `Runner::DoEarnGold` only dropped a stale
   `buyer:<item>` note AFTER `kMaxSellTrips` (Economy.cpp, the
   `no '%s' reachable after %d trips` block) — `run_gates/g_Kharain.console.txt
   :4075-4076`, "no 'blacksmith' reachable after 3 trips" immediately followed
   by "forgetting the 'blacksmith' noted at 2562,502".
2. No memory at all. With no note, every trip called
   `TravelToService(sellService_, home)`, which answers with the SAME nearest
   provider each time — `run_gates/g_Odessa.console.txt:737,774,782,788,792,798`:
   trip 1 arrived at 1427,1658; trip 2 "arrived at 1427,1658" four seconds
   later; trip 3; trade written off. Trips 2 and 3 were no-ops.

`Runner::DoTrainAtNpc` (Train.cpp:1288-1302) already had the first-miss rule
and the skipping lookup — this is the same rule, applied to selling.

### Change (`src/life/Runner.h`, `src/life/runner/Economy.cpp`)

- `sellKnownX_/sellKnownY_` record the noted spot while walking to it. Standing
  within `kStaleNoteMissWithin` (3 tiles, same as Train.cpp) with no vendor of
  the trade in sight forgets the supplier immediately, does not spend a trip,
  and rescans. One miss = one strike.
- The atlas fallback now uses `TravelToServiceSkipping(..., &sellShopsTried_)`,
  so trip 2 is a different shop rather than the same tile.
- Both lists reset when the trade or buyer changes.

### Status: NOT PROVEN LIVE

Kharain's 5-minute window never entered either path: EARN_GOLD stood down at
18:22:06 ("the bank holds a surplus but no NPC route for it"), and TRAIN_AT_NPC
was never picked. `session_summary duration=343s goals=1/6 deaths=0`. The change
compiles and ctest is 45/45, but no runtime evidence of the new behaviour exists
yet. It needs a character actually carrying sellable stock (Odessa/i_gears) to
exercise.

### Separate defect, NOT this seam

Kharain's wave-2 `goal_failed=TRAIN_AT_NPC reason="no 'tinker' reachable after 3
trips"` (02:21:25) is not stale memory: `g_Kharain.console.txt:1309` shows trip 3
using the atlas lookup with no remembered trainer, and `trainTrips_` is carried
across goal supersessions (BUY_SUPPLIES superseded TRAIN_AT_NPC at 02:19:19 and
02:21:19), so the goal resumed already at its trip ceiling and failed without
travelling. That is trip accounting across goal changes — Train.cpp / planner,
not the memory/lookup seam.

## Cross-checks

- `grep "was killed by" runtime/logs/sphere2026-09-06.log` — 15 deaths on the
  day, none for Vorar or Kharain; the last is Aurelius at 17:57, before the
  18:19-18:26 window.
- ctest 45/45 before and after both items.
