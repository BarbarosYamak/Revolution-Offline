# Britain graveyard — strong tier split into three spots (2026-09-06)

Owner ruling: `.claude/agent-memory/revolution-god/graveyard-tiers-separated.md`.
Threat baseline: `artifacts/britain_graveyard_threat_2026-09-06.md`.

## 1. Yard map (CURRENT_SCRIPT + client MUL measurement)

AREADEF `a_britain_graveyard_1` — `runtime/scripts/maps/map0/map0_areas.scp:2868-2875`
  `P=1384,1492,10,0`; `RECT=1336,1443,1391,1494`; `RECT=1336,1494,1376,1511`.

Walkability measured off the Revolution client MULs:
`bot/uo-client/build-m1/uo_mul_dump.exe walk tiledata.mul map0.mul staidx0.mul
statics0.mul 1345 1440 50 58 10` (run in `runtime/mul`, 2900 cells).

Structure found: north wall row `y=1443` (x1345-1355 and x1359-1389) with a gate
gap at x1356-1358; east wall column `x=1390` continuous y1444-1493; four walled
crypt blocks in the yard interior (x1355-1364 y1455-1462; x1368-1377 y1458-1468;
x1357-1364 y1477-1487; x1373-1385 y1476-1483). The **east corridor x1379-1389,
y1444-1462** is open ground, statics 0, standZ 10-11, entirely inside RECT 1.

Bot entry: atlas `PLACE britain_graveyard_graveyard ... 1384 1492 10 12`
(`bot/uo-client/data/revolution_atlas.txt:2696`) — the south anchor. Observed bot
positions in the death logs were 1359,1486 and 1366,1476, also south. So the
gradient runs **south (entrance) -> north (far end)**.

## 2. Chosen spots — three non-overlapping rings

| tier | defname | old x,y,z r | NEW x,y,z r | ring box | dist from 1384,1492 |
|---|---|---|---|---|---|
| knights | c_skeleton_knight x2 | 1360,1459,11 r7 | **1385,1459,10 r3** | x1382-1388 y1456-1462 | 33 |
| lich | c_lich x1 | 1368,1452,11 r7 | **1385,1452,10 r3** | x1382-1388 y1449-1455 | 40 |
| lich lord | c_lich_lord x1 | 1370,1460,11 r2 | **1385,1446,11 r2** | x1383-1387 y1444-1448 | 46 |

Timers, counts and defnames unchanged (3-7 / 3-7 / 10-15 min; 2 / 1 / 1).
Only x,y,z and MAXDIST moved. Nothing deleted.

Weak band left untouched: 1357,1468 r7 (skeleton+zombie), 1378,1472 r7
(zombie+spectre), 1375,1485 r6 (skeleton+skeleton_w_axe), 1376,1463 r3
(skeleton_w_axe). Weak ring union = x1350-1385, y1460-1491.

Non-overlap proof (Sphere MAXDIST is a square half-extent):
- knight y1456-1462 vs lich y1449-1455 -> adjacent, disjoint.
- lich y1449-1455 vs lich lord y1444-1448 -> adjacent, disjoint.
- knight y1456-1462 vs nearest weak ring 1376,1463 r3 (x1373-1379) -> x disjoint
  (1379 < 1382); vs 1378,1472 r7 (y1465-1479) -> y disjoint (1462 < 1465).
- lich and lich lord sit at y <= 1455, north of every weak ring's y1460 floor.
- All three rings inside RECT 1336,1443,1391,1494.

Walkability of every tile of every ring (uo_mul_dump walk, fromZ 10):
knight 49/49 standZ 10; lich 49/49 standZ 10-11; lich lord 25/25 standZ 10-11.
(The pre-existing 2026-09-06 placement — 1384,1447 / 1383,1457 / 1386,1452 —
was walkable but its rings OVERLAPPED each other on rows 1450 and 1454, and its
distance order lich < lich lord < knight inverted the gradient. That is what
this pass corrected; it had never been applied live.)

## 3. Files changed (CURRENT_SCRIPT)

- `runtime/scripts/functions/worldgen/spawns/felucca/Graveyards_spawns_felucca.scp`
  — Britain strong lines + header comment.
- `runtime/scripts/revolution/f_m40_graveyards.scp` — same three vectors in the
  live mirror, plus a new `[function f_m40_gy_britain_strong]` (surgical
  clear+place for the Britain strong tier only).
- `bot/uo-client/scripts/scenarios/m40_gy_britain_strong.txt` (new),
  `bot/uo-client/scripts/scenarios/m40_gy_strong_settle.txt` (new).

`server/Scripts-X/...` deliberately NOT touched — pristine upstream checkout
(revolution-god memory `shard-ops-restart-and-saves`: runtime/scripts is truth).

## 4. Live application (CURRENT_RUNTIME)

Save backed up to `save_backup_2026-09-06_gytier/` first.

`local/dev/run_admin.ps1 -Scenario m40_gy_britain_strong -Tag gy_strong1`
-> `local/dev/gy_strong1.console.txt`:
  `19:58:49 System: Resync complete!`
  `19:59:19 [chat uni] : m40 britain strong: removed 03, placed 3`
  `19:59:34 System: World save has been initiated.`
Exactly 3 gems removed (the three pre-move strong gems in clear rect
1358,1443-1374,1465) and 3 placed. `f_m40_gy_clearrect` also kills the gem's
children (CCSpawn::Delete -> KillChildren). No weak gem is inside either clear
rect (nearest weak gem 1376,1463, x > 1374 and x < 1380). Minoc untouched —
`f_m40_gy_britain_strong` only ever addresses two rects inside the Britain
graveyard AREADEF.

Second pass `-Scenario m40_gy_strong_settle -Tag gy_strong2` waited 5 min for a
full knight/lich spawn cycle and re-saved (`local/dev/gy_strong2.console.txt`).

## 5. Verification — tools/world_query.py (CURRENT_RUNTIME)

    --near 1385,1459 --radius 6 --type c_skeleton_knight -> n=2 (1384,1460; 1383,1460)
    --near 1385,1452 --radius 6 --type c_lich            -> n=1 (1386,1454)
    --near 1385,1446 --radius 6 --type c_lich_lord       -> n=1 (1385,1446)
    --near 1366,1476 --radius 12 --type c_skeleton_knight -> n=0
    --near 1366,1476 --radius 12 --type c_lich            -> n=0
    --near 1366,1476 --radius 12 --type c_lich_lord       -> n=0

Gems in the save: `i_worldgem_bit` at 1385,1459 (0x4000a0a9), 1385,1452
(0x4000a08f), 1385,1446 (0x4000a081); all seven weak gems still present at
their original coordinates.

## 6. Atlas recommendation (navigation-world owns the file — NOT edited)

The atlas has exactly ONE row inside the yard box: line 2696,
`PLACE britain_graveyard_graveyard graveyard a_britain_graveyard_1 1384 1492 10 12`.
It is derived from the AREADEF `P=` point, not from the spawner table, so this
change does not make it stale.

Recommendation: **yes, add a strong-side marker.** With one anchor at 1384,1492
r12 (covering y1480-1504) a bot homing on "Britain Graveyard" lands in the weak
south end — correct for novices by accident, but there is no way to address the
strong tier at all, and no way for a geared fighter to say "hunt the knights".
Concretely, three extra `PLACE` rows would express the gradient the shard now
has:

    britain_graveyard_knights    1385 1459 10  r4
    britain_graveyard_lich       1385 1452 10  r4
    britain_graveyard_lich_lord  1385 1446 11  r3

plus a `tier=weak|strong` marker on the existing yard row. `uo_atlasgen` would
have to learn to emit per-spawner tier rows from the `Graveyards_` tables
(the header already lists them as a `PLACE resrc` source). Handing that to
navigation-world.
