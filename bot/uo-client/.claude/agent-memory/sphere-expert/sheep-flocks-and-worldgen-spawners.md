---
name: sheep-flocks-and-worldgen-spawners
description: How Sphere worldgen spawners are defined vs. created, the f_create_spawner arg layout, and where the shard's sheep flocks (Yew/Britain/Jhelom/Skara) live
metadata:
  type: project
---

Sheep flocks come from `i_worldgem_bit` spawner items, not hand-placed
WORLDCHARs. The three Yew flocks are declared in
`runtime/scripts/functions/worldgen/spawns/felucca/TownsLife_spawns_felucca.scp`
("Yew Town" section) as `f_create_spawner,Sheep,,,,,,X,Y,Z,...` with
AMOUNT=15 / TIMELO=5 / TIMEHI=10 / MAXDIST=15.

**Why:** TownsLife/WildLife/Outdoors `*_spawns_*.scp` are one-shot *worldgen*
scripts. Editing a line does NOT retro-create a spawner on an already
generated world — the gem must also be created once on the live server.

**How to apply:**

- `f_create_spawner` arg layout (`functions/worldgen/spawns/spawner_functions.scp`):
  `argv[0..5]` six spawn groups (a group may be `A:B:C`, picked at random per
  tick), `argv[6..8]` x,y,z, `argv[10..12]` MOREP = timelo,timehi,maxdist,
  `argv[15+n]` amount for group n. `argv[9]`, `[13]`, `[14]` are unused.
  Group names resolve through `[defname world_spawner]` /
  `[DEFNAME TOWNLIFE_SPAWNER]` in `spawner_defs.scp` (`Sheep {c_sheep_woolly}`).
- Live creation, server running: say `.f_create_spawner Sheep,,,,,,X,Y,0,2,5,10,15,10,1,15,0,0,0,0,0`
  as Admin via `local/dev/run_admin.ps1 -Scenario <name>`. The gem is ATTR=080
  (not attr_static) so plain `.save` writes it to `sphereworld.scp`.
- `CCSpawn::OnTickComponent` spawns ONE creature per tick, then re-arms for
  TIMELO..TIMEHI **minutes**, so a new AMOUNT=15 flock needs ~2h to fill.
  `.serv.uid.<serial>.start` = `SetTimeout(0)`, i.e. bring the spawner's own
  next tick forward. It caps at AMOUNT, so repeating it is idempotent and
  changes only the schedule, never the placement rules.
- Flock locations after 2026-09-02: Yew 676,1178 / 570,1099 / 675,939 (15 each);
  Britain North Pasture 1323,1351 and Jhelom farm 1183,3607 (added 2026-09-02,
  15 each); Skara Brae has three mixed `Chicken:Sheep:Cow` spawners
  (813,2164 / 818,2269 / 830,2353, amount 3) that never make a real flock.
- `bot/uo-client/data/revolution_pastures.tsv` is derived — rerun
  `python bot/uo-client/tools/pasturegen.py` after any flock change
  (MIN_FLOCK=4, JOIN_DIST=24, so a thin flock will not appear).

See [[tailoring-legacy-menu-and-cloth-chain]] for what happens to the wool.
