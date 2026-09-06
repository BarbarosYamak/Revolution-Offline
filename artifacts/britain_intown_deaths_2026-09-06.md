# Britain in-town deaths, 2026-09-06 fleet-100 run

## Identity of the killers
- Names (Korgulg, Grung, Hysil, N'Thishthes) are random name-roll strings, not
  unique defnames. `runtime/scripts/templates_special/sp_tm_name.scp`:
  Grung/Korgulg -> `[NAMES NAMES_ORC]` (lines 444, 533); Hysil/Thishthes ->
  `[NAMES NAMES_LIZARDMAN]` (lines 1069, 1235).
- Those NAMES groups are consumed by ordinary CHARDEFs:
  `runtime/scripts/npcs/c_monster_classic.scp:911` `[CHARDEF 011] DEFNAME=c_orc
  NAME=#NAMES_ORC the Orc` and `:1476` `[CHARDEF 021] DEFNAME=c_lizardman
  NAME=#NAMES_LIZARDMAN the Lizard Man`. Both are mundane low-tier wilderness
  monsters (ARMOR=28, DAM=5-7) — not uniques, not boss spawns.
- World-wide live count today: 553 `c_orc`, 75 `c_lizardman`
  (`world_query.py --count`). Spawner linkage confirmed via `SPAWNITEM=` tag
  on a sampled WORLDCHAR (uid 0xb134, `sphereworld.scp:305240`) pointing at an
  `i_worldgem_bit` spawner gem at P=1059,1166,5 with
  `TAG.spawn_array="corpser,ettin,ghoul,giantspider,lizardman,ogre,orc,
  reaper,skeleton,troll,zombie"`, MAXDIST=30 — CURRENT_SCRIPT/CURRENT_RUNTIME.
- No orc/lizardman WORLDCHAR currently sits inside or near the Britain
  guarded rect (checked radius 600 around 1495,1629 and filtered to the rect
  box; zero hits) — the specific killer instances are not in the present
  save (transient wilderness wanderers, not re-saved after the fight).
- Worldgen `f_create_spawner` lines for orc/lizardman groups
  (`runtime/scripts/revolution/f_m39_outdoors.scp`, ~225 lines) are all
  hundreds of tiles from Britain (closest checked candidate 277+ tiles away)
  with MAXDIST 30-80 — no worldgen spawner seeds monsters inside or at the
  edge of town. No Britain-adjacent orc/lizardman placement found in
  `references/tns/scripts` either (spot check, not exhaustive).
- Conclusion: these are ordinary wandering/aggro wilderness monsters that
  chased a fleeing, already-hurt bot across the region boundary into town —
  not a stale-spawn artifact and not a scripted "monsters live inside
  Britain" bug. CURRENT_SCRIPT + HYPOTHESIS (chase behavior itself is
  standard Sphere aggro, not separately proven from this evidence pass).

## Region check
`runtime/scripts/maps/map0/map0_areas.scp:2887-2898`:
```
[AREADEF a_townBritain]
FLAGS=REGION_FLAG_NOBUILDING|REGION_FLAG_GUARDED
RECT=1410,1517,1691,1778,0
RECT=1436,1494,1670,1517,0
RECT=1522,1778,1641,1798,0
RECT=1691,1532,1721,1595,0
RECT=1388,1697,1410,1766,0
```
Tordor's actual death tile was (1447,1528,35) — CURRENT_RUNTIME confirmed by
the bot's own client log: `event death_location: at=(1447,1528,35)
region=a_townBritain via=0x2C resurrect menu`
(`bot/uo-client/artifacts/fleet_ramp_20260906/Tordor.console.txt:1510`). That
point is inside RECT 1410,1517-1691,1778, i.e. genuinely inside guarded
Britain. (1432,1560 from the task brief was Tordor's post-death travel
waypoint toward the healer, not the death tile.) So guarded-flag coverage of
the death location is NOT in question — the region and flag are correct.

## Why guards did not intervene (root cause)
Sphere's auto-guard-summon against hostile/evil creatures
(`GuardsOnMurderers=1`, `server/Source-X/src/sphere.ini:764`) is wired
through `CChar::CallGuards()` (no-arg overload,
`server/Source-X/src/game/chars/CCharFight.cpp:178-213`), which scans nearby
chars and calls guards on anything `STATF_CRIMINAL` or
`Noto_IsEvil() && GuardsOnMurderers`. But that no-arg `CallGuards()` has
exactly one call site in the whole source tree:
`server/Source-X/src/game/clients/CClientEvent.cpp:1871`, inside
`CClient::Event_Talk_Common` — it fires only when the player's own chat text
matches the `guardcall` VARDEF (default `"GUARD,GUARDS"`,
`CClientEvent.cpp:1863-1869`). There is no other path that auto-summons
guards just because a player is standing in a guarded zone taking damage from
a monster.

The bot's `FLEE_TO_GUARDS` handling
(`bot/uo-client/src/life/runner/Survive.cpp:152-159` `RetreatToSafety`, used
by the interrupt at `Survive.cpp:931-936`) only calls
`client.EnsurePeaceMode()` and `client.TravelToService(wm::Service::Banker,
...)` — it navigates toward a guarded-zone NPC but never emits the "Guard"/
"Guards" speech line. Grepped for any `Speak`/say action containing "guard"
in `bot/uo-client/src/life/runner/*.cpp` — none found. So even though Tordor,
Serena and Odessa were physically inside `a_townBritain` (guarded) while
being killed, no client-side code path ever issued the one chat trigger that
turns the guarded flag into an actual guard NPC. CURRENT_SOURCE (Sphere
engine) + CURRENT_SOURCE (bot client) — this is the proven root cause, not
speculation.

## Separate, secondary finding (not the root cause of these 4 deaths)
`runtime/scripts` has no script hits for "no guard post" — the message is
hard-coded in `server/Source-X/src/game/chars/CCharNPCAct.cpp:1507-1527`
(`CChar::NPC_Act_GoHome`): a guard whose home point's `AREADEF` is not
`IsGuarded()` self-destructs (`STATF_CONJURED`, `Stat_SetVal(STAT_STR,0)`)
unless flag `OF_GuardOutsideGuardedArea` is set. This fired 1924 times over
today's run (`grep -c "no guard post" runtime/logs/sphere2026-09-06.log`),
at many locations across the map, some at/near Britain's rect edges (e.g.
1400,1627 / 1401,1628, both just outside RECT x>=1410). This shows real
guard-conjure churn elsewhere in the world (worth a separate ticket: guard
home-point assignment vs. the 5-rect Britain geometry), but no warning
directly correlates second-for-second with Tordor/Serena/Odessa's specific
death tiles, and — per the root cause above — no guard was ever conjured
*for* them in the first place, so this churn is not what let the monsters
kill them.

## Evidence trail
- Sphere log: `runtime/logs/sphere2026-09-06.log` lines 88602 (Tordor 03:13),
  93664/98767 (Serena), 13715-88012 (Odessa, killed 5x by the same name
  family), plus 1924x "no guard post" warnings.
- Bot consoles: `bot/uo-client/artifacts/fleet_ramp_20260906/Tordor.console.txt`
  lines 1357-1660 (fight, flee, death at 1447,1528); `Serena.console.txt`
  lines 3178-3310 (same pattern, corpse at 1445,1351, died further into the
  guarded approach road).
- World save queries: `python tools/world_query.py --near 1432,1560 --radius
  40|400`, `--count c_orc`, `--count c_lizardman`, `--item 4000af39`.
