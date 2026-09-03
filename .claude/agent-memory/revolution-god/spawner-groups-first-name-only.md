---
name: spawner-groups-first-name-only
description: Sphere worldgen spawner with a:b:c group spawns only the first name shard-wide; graveyards rebuilt on TNS mix 2026-09-02 with single-name gems
metadata:
  type: project
---

Proved 2026-09-02 (sphere-expert, artifacts/graveyards_tns_2026-09-02.md): an `f_create_spawner` gem holding a `Boar:Cougar:Sheep` group only ever spawns the FIRST name; `worldgen_typedefs.scp:17 ERROR: 0 DOES NOT EXIST` each tick. Every mixed TownsLife/WildLife/Outdoors spawner is effectively single-species. Fix options: `t_custom_spawner_char` @timer typedef, or split all mixed spawners. Undecided as of 2026-09-02 (owner not yet asked).

Graveyards: 7 Felucca yards rebuilt on TNS worldspawn mix (docs/tns_exports/tns_spawns.tsv), single-name gems, MAXDIST ≤7 inside AREADEF rects, 58 gems / 95 undead, Yew kept weak as newbie yard. Lich lord art redirect ID=c_lich (runtime c_monster_classic.scp). TNS `SPAWN_Undead_Strong/Weak` member lists are NOT in the dump — Britain strong points are hypothesis.

Also: `.serv.resync` won't load a script file created after boot; `SERV.LOAD` does.

**Why:** owner asked for skel knight + lich lord in graveyards and noticed spawns outside yard rects; TNS is the closest Revolution-era evidence.
**How to apply:** any future spawn content must use one name per gem until the typedef is fixed; check `docs/REVOLUTION_BODY_ID_CATALOGUE.tsv` id_redirect column (stale) before declaring a creature undrawable.
