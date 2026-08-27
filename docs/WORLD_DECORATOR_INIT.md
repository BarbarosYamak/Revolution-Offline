# World Decorator Initialization — reproducible procedure

Date: 2026-08-27 (M3.7). Operator runbook, not gameplay.

This records how the Revolution Offline world was given its craft stations,
doors and signs, so the step can be repeated on a fresh world save without
rediscovering the three traps that cost this milestone several false
verifications.

---

## 0. Why this is needed at all

Before M3.7 the world contained **no working craft station of any kind**:

| | Revolution map statics (all of map 0) | `sphereworld.scp` (9017 items) |
|---|---:|---:|
| forge | 2, neither near a town | **0** |
| anvil | 1 | **0** |
| spinning wheel | **0** | **0** |
| upright loom | 2 pieces | **0** |

That is normal for UO — shop forges, wheels and looms are **server-placed
items**, not map art — and Scripts-X ships the placements in its own world
decorator. The decorator had never been run past its `place_moongates` step,
which is why moongates worked (20 gates in the save, `m25_moongate` green in
M2.5) while Tailoring and Blacksmithy were unreachable.

**Nothing in this procedure is invented.** Every coordinate comes from
`runtime/scripts/functions/worldgen/decoration/`.

---

## 1. The three traps

### 1.1 `.savestatics` silently does nothing

```
ERROR:Undefined keyword 'savestatics'.
'Admin' commands 'savestatics'=1        <- logs that a command was ISSUED, not that it worked
```

`SAVESTATICS` is `SV_SAVESTATICS` in `CServer::r_Verb` (`CServer.cpp:2205`) — a
**server** verb. A client command resolves against the **character's** verb
table first and never finds it.

> **Use `.serv.savestatics`.**

### 1.2 Decorator items are not written to `sphereworld.scp`

Every decorator item carries `attr_static`, and Source-X routes those to a
separate file:

```cpp
CSector::SaveSector   else if (!pItem->IsAttr(ATTR_STATIC))
                          r_WriteSafe(g_World.m_FileWorld);   // sphereworld.scp
CWorld::SaveStatics   if (!pItem->IsAttr(ATTR_STATIC)) continue;
                      r_WriteSafe(m_FileStatics);             // spherestatics.scp
```

So an ordinary `.save` writes **none** of them, and verifying against
`sphereworld.scp` after a successful pass shows zero stations.

> **Verify against `runtime/save/spherestatics.scp`.**

### 1.3 The decorator is not idempotent

Every city function is a flat list of `serv.newitem` with no existence check.
Running one twice places a duplicate of every item on the same tile.

> **Never re-run a city that has already been done.** This is why the M3.7
> pass calls the 38 remaining cities *by name* instead of using
> `f_decorate_facet 0`, which would have doubled Britain.

---

## 2. Prerequisites

* Sphere running, and reachable **either** through its console window **or**
  through a privileged in-game account.
* A world backup. `runtime/save_backup_pre_deco/` holds the pre-decorator state.

**If the Sphere process was launched detached it has no window handle**
(`MainWindowHandle = 0`), so `local/dev/sphere_console.ps1` cannot find its
EDIT control, and unless a telnet port is open there is no console route at
all. That was the case during M3.7, which is why the whole procedure runs
through an in-game admin character instead — `local/dev/run_admin.ps1`, which
logs in as the `Admin` account's character `Observer` and issues the commands as
speech.

---

## 3. Procedure

Run in this order. Each step is a scenario under
`bot/uo-client/scripts/scenarios/`.

| # | Scenario | What it does |
|---|---|---|
| 1 | `m37_deco_britain` | `.deco_britain_felucca`, the anchor city |
| 2 | `m37_deco_doors_signs` | `.serv.savestatics`, then `.place_signs_felucca`, `.place_doors_felucca`, `.f_link_double_doors` |
| 3 | `m37_deco_cities` | the other 38 Felucca entries, by name |
| 4 | `m37_savestatics` | `.serv.savestatics` + `.save` |

```
powershell -ExecutionPolicy Bypass -File local\dev\run_admin.ps1 `
    -Scenario m37_deco_britain      -Tag deco1 -TimeoutSec 180
powershell -ExecutionPolicy Bypass -File local\dev\run_admin.ps1 `
    -Scenario m37_deco_doors_signs  -Tag deco2 -TimeoutSec 300
powershell -ExecutionPolicy Bypass -File local\dev\run_admin.ps1 `
    -Scenario m37_deco_cities       -Tag deco3 -TimeoutSec 900
powershell -ExecutionPolicy Bypass -File local\dev\run_admin.ps1 `
    -Scenario m37_savestatics       -Tag stat1 -TimeoutSec 200
```

Each scenario opens with `.show p`, whose reply proves the account is
privileged *before* anything is placed. A non-GM account gets no reply.

### 3.1 What is deliberately NOT run

| Skipped | Why |
|---|---|
| `deco_britain_felucca` twice | not idempotent — §1.3 |
| `deco_magincia_ruined_felucca` | **mutually exclusive** with `deco_magincia_felucca`: same tiles, intact vs destroyed city. The gump offers them as separate buttons because the operator picks one. Intact chosen so the city keeps working shops. Whether Revolution ran the 2007 ruined state is **UNKNOWN** |
| Trammel (facet 1) | enabled in `sphere.ini`, but the atlas is map 0 only and the generator skips every non-map-0 row (M2.5 debt 9) |
| Ilshenar / Malas / Tokuno / Ter Mur (facets 2–5) | **no client data.** `map2..map5` are commented out of `spheretables.scp` because Revolution ships no `map2-5.mul`, and **Sphere auto-fixes an unsupported map onto map 0** — confirmed live during the door pass: `Unsupported map #2..#5 specified. Auto-fixing that to 0.` Running them would scatter their furniture across Felucca |
| `f_stock_bookcases` | `foritems 9999` creating five books per bookcase world-wide; books are economically irrelevant |

---

## 4. Verification

```bash
cd runtime/save
grep -c '^\[WORLDITEM' spherestatics.scp
for n in i_forge i_forge_large i_forge_large_bellows i_anvil \
         i_spinning_wheel i_loom_upright i_dye_tub; do
    printf '%-26s %s\n' "$n" "$(grep -c "^\[WORLDITEM $n\]" spherestatics.scp)"
done
grep -cE '^\[WORLDITEM i_door' spherestatics.scp
grep -cE '^\[WORLDITEM i_sign' spherestatics.scp
```

**Result of the 2026-08-27 run:**

| | before | after |
|---|---:|---:|
| `i_forge` | 0 | 21 |
| `i_forge_large` | 0 | 60 |
| `i_forge_large_bellows` | 0 | 26 |
| `i_anvil` | 0 | 54 |
| `i_spinning_wheel` | 0 | **20** |
| `i_loom_upright` | 0 | **33** |
| `i_dye_tub` | 0 | 12 |
| doors | 0 | 1848 |
| signs | 0 | 468 |
| **statics total** | 0 | **27,358** |
| `spherestatics.scp` | absent | 2.29 MB |

### 4.1 Expected errors

**4 tiledata errors across ~27,000 placements**, of the form:

```
ERROR:(city_britain_deco_felucca.scp,8253)ITEMDEF has invalid ID=22328 (05738)
       (value is greater than the tiledata maximum index).
```

Scripts-X's decorator targets a modern SA/HS tiledata; Revolution's client data
is Renaissance-era and stops short of those ids. Sphere logs the bad itemdef and
carries on. Four is noise; a large count would mean a city file is mostly
post-era art and its pass is not worth keeping.

Also expected and **pre-existing, not caused by the decorator**: `Guard … has no
guard post` warnings (159 the previous day, 38 earlier the same day).

---

## 5. The station coordinates that matter for M3.7

Ground level, inside the Britain tailor shop the atlas calls
`britain_tailor_2` (1467,1686,0) — the pair the textile slice uses:

```
spinning wheel   1473,1689,0
upright loom     1473,1685,0  +  1474,1685,0
```

And the forge the mining slice uses, in the blacksmith quarter
(`britain_territory_blacksmithguildmaster`, 1349,1778,15):

```
forge            1355,1776,15
```

**A spinning wheel or loom must be a dynamic item.** `CClient::Event_Target`
resolves the target with `uid.ObjFind()` (`CClientEvent.cpp:2481`); a map static
has no UID, so `pItemTarg` is `nullptr` and the `IT_WOOL` / `IT_YARN` /
`IT_THREAD` cases in `OnTarg_Use_Item` break out having done nothing. A forge is
different — `CWorldMap::FindItemTypeNearby` scans statics too — but with no
forge static near any town it was unreachable anyway.

---

## 6. Reverting

There is **no scripted undo**. Restore the backup:

```
runtime/save_backup_pre_deco/   sphereworld.scp, spherechars.scp,
                                spheredata.scp, spheremultis.scp
```

and delete `runtime/save/spherestatics.scp`. Stop the server first, or the next
autosave will overwrite the restore.
