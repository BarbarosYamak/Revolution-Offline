---
name: vet-npcs-missing-from-felucca-worldgen
description: Root cause and fix for 0 veterinarian NPCs in the live world despite the shard defining the profession
metadata:
  type: project
---

**Root cause (CURRENT_SCRIPT, confirmed 2026-09-07):** the "veterinarian"
spawn-group alias (`{c_vet 1 c_vet_f 1}`) exists in
`runtime/scripts/functions/worldgen/spawns/spawner_defs.scp` only under
`[DEFNAME MalasSpawns]` (line ~471) and is used in
`runtime/scripts/functions/worldgen/spawns/malas/Vendor_spawns_malas.scp`
(paired with `animaltrainer` in one `f_create_spawner` call). Felucca's own
`Vendors_spawns_felucca.scp`/`TownsLife_spawns_felucca.scp` never contained a
single `veterinarian` line — not a mis-keyed entry, a flat omission. All
other Felucca shopkeeper professions (healer, alchemist, tinker, etc.) live
in the general `[defname world_spawner]` block (lines 2-74) and ARE placed.

**Why the alias still worked from Felucca:** Sphere's `[DEFNAME sectionname]`
header is a grouping label for the resource editor, not a namespace — a key
declared under `MalasSpawns` is a flat global DEFNAME reachable by its bare
name from any facet's script. Confirmed empirically: calling
`.f_create_spawner veterinarian,,,,,,X,Y,Z,...` from Felucca coordinates via
the live server console produced real `c_vet`/`c_vet_f` characters with no
engine error.

**Fix applied:** added one `f_create_spawner,veterinarian,,,,,,X,Y,Z,1,5,10,5,0,1,1,0,0,0,0,0`
line directly after the existing `animaltrainer` line in
`runtime/scripts/functions/worldgen/spawns/felucca/Vendors_spawns_felucca.scp`,
co-located with each town's animal-trainer stable (same pattern Malas already
uses, animaltrainer+vet at one stable):
- Britain: 1296,1759,10 (first Britain stable, line ~25)
- Skara Brae: 570,2121,0 (Skara Brae's only stable, line ~270)

Applied live via `local/dev/sphere_console.ps1 -Commands ".f_create_spawner ..."`
(server console accepts the same `.`-prefixed verb syntax as in-game GM
speech — `CServerConfig::IsConsoleCmd` strips `.`/`/` then runs `r_Verb`;
`pSrc==this` on the console so it is always privileged, no admin login
needed). `##` saves world+statics. A freshly created `i_worldgem_bit` gem
doesn't spawn its NPC until its timer fires (TIMELO..TIMEHI minutes); use
`.serv.uid.<hex-uid>.start` to force the first tick immediately, matching
the sheep-flock trick in [[sheep-flocks-and-worldgen-spawners]].

**Verified 2026-09-07:** `c_vet` 0->1 (Skara Brae, 569,2117), `c_vet_f` 0->1
(Britain, 1293,1756) via `tools/world_query.py --count` and `--near`. Only
Britain and Skara Brae were added (only two attested by TNS donor rosters
per the brief); other Felucca towns' vet coverage is UNKNOWN/not attested
and was left alone.

**Unrelated, checked while here:** `crafting_functions.scp:299`'s
"Blacksmith/Taming skill doesn't have a value for RANGE, defaulting to 3"
spam is `CCharSkill.cpp` (`g_Log.EventError`) firing whenever the
Blacksmithing/Taming `[SKILL]` def in scripts has no `RANGE=`; it just
defaults to a 3-tile reach check and does not affect crafting success.
Cosmetic log noise only, source-level, not a script bug — left untouched.
