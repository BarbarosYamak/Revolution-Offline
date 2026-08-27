# M3.7 — Revolution Resource Economy, Crafting Chains and Vendor Authenticity

Date: 2026-08-27. **STATUS: PASS.** All five mandatory live slices (A–E) run end
to end against a live server with no GM action. §13a has every slice with its
evidence, §13b the finding that corrected this report's own reasoning, §14 what
remains open.

**The headline proof — §13a, slice C.** Two ordinary characters, two sockets, no
GM action: 40 gold moved one way and an ingot the other, and all four numbers
were read back out of the server afterwards. The columns balance exactly.

Companion documents produced by this milestone:

* `REVOLUTION_VENDOR_MATRIX.md` — the NPC vendor audit
* `REVOLUTION_PRODUCTION_CHAINS.md` — every chain, with engine citations
* `TNS_DONOR_AUDIT.md` — a second Turkish Sphere shard, graded as a donor

Source-X modifications: **0**. Scripts-X modifications: **0**. Runtime script
modifications: **0** (M3.6's runebook restoration is unchanged).

---

## 1. Phase 0 — baseline and debt

| | |
|---|---|
| Client | `revolution-sphere-m1` @ `1b35cec`, clean at entry |
| Runtime scripts | `revolution-runtime` @ `be7c185`, clean, still clean |
| Source-X / Scripts-X | 0 tracked modifications, before and after |
| CTest entering M3.7 | **7/7, 0 failures** |
| CTest now | **8/8, 0 failures** (+`m37_economy`, 76 checks) |

### Debt carried in from M3.6 §14 — none of it deleted

| Debt | State after M3.7 |
|---|---|
| 1. Runebook copying and page transfer not built | **unchanged** |
| 2. Legitimate runebook crafting unproven | **still unproven — but the reason is now known precisely** (§8) |
| 3. Charged-use Magery bypass not separately proven | unchanged |
| 4. Blank-rune purchase unproven | unchanged; and blank runes now grade `STOCK_SPHERE_ONLY` |
| 5. `ReagentsRequired=0` conflict open | **unchanged, and now load-bearing** (§9) |
| 6. Skill cap / Resist enforced client-side only | unchanged |
| 7. Stat cap unknown | unchanged. Still not guessed |
| 8. Engine-era divergence (Source-X vs an era Sphere) | unchanged; more instances found (§11) |
| 9. Post-era mounts crash third-party clients | unchanged — but mount taming thresholds are now **known** (§10) |
| 10. Fleet/routing/economy not wired into a session | unchanged |
| 11. Anti-macro specified, not built | unchanged |
| 12. One action in flight; `Journey` 2-D | unchanged |
| M3.6 shortfall: travel_service does not auto-select Runebook | unchanged — deliberately not an M3.7 subject |

---

## 2. The finding that reframed the milestone

M3.7 was scoped as an audit. Phase 1 turned up something that made it a repair
job first.

> **This world contained no working craft station of any kind.**
> Zero forges, zero anvils, zero spinning wheels, zero looms.

Established three independent ways:

1. **Revolution's own map statics** (`statics0.mul`, scanned directly): on the
   whole of map 0 — **2 forges, 1 anvil, 0 spinning wheels, 2 loom pieces**,
   and none of them near a town. Britain's tailor shop had a decorative
   *"loom bench"* at (1546,1661,26) and no loom.
2. **The world save**: 9017 items, and not one forge, anvil, wheel or loom.
3. **The engine**: even a static wheel would not have worked. See §3.

So **Tailoring and Blacksmithy were unreachable on this shard**, and had been
through M1 to M3.6. That is why M3 could list mining and smelting as
`SCRIPT_VERIFIED, not run` and never discover they were impossible.

### 2.1 The fix was the shard's own, and it had simply never been run

Scripts-X ships the placements in `functions/worldgen/decoration/`. Its
`place_moongates` step **had** been run — 20 gates are in the save and
`m25_moongate` passed live in M2.5 — but the per-city furniture pass never was.

Britain's own file already contained, at coordinates nobody on this project
chose:

| | `city_britain_deco_felucca.scp` |
|---|---|
| spinning wheels | 1545,1656,26 · 1390,1604,30 · **1473,1689,0** · 1475,1689,0 |
| upright looms | 1545,1660+1661,26 · 1392+1393,1601,30 · **1473+1474,1685,0** |
| forges | 1355,1776,15 · 1361,1574,30 · 1424,1558,30 · 1356,1784,15 · 1344,1776,15 |
| anvils | 1363,1574,30 · 1423,1556,30 · 1355,1786,21 · 1346,1782,21 · 1346,1774,21 |

The ground-level pair — wheel (1473,1689,0), loom (1473,1685,0) — sits in the
Britain tailor shop the atlas already knew as `britain_tailor_2` (1467,1686,0).

### 2.2 What was run, and what was deliberately not

Run: **all 39 Felucca city and dungeon passes**, plus `place_signs_felucca`,
`place_doors_felucca` and `f_link_double_doors`.

| | before | after |
|---|---:|---:|
| forges (`i_forge` + large + bellows) | 0 | **107** |
| anvils | 0 | **54** |
| spinning wheels | 0 | **20** |
| upright looms | 0 | **33** |
| dye tubs | 0 | 12 |
| doors | 0 | 1848 |
| signs | 0 | 468 |
| statics total | 0 | **27,358** |

**4 tiledata errors** across ~27,000 placements — Scripts-X's decorator targets
a modern SA/HS tiledata and Revolution's is Renaissance-era, so a handful of
item ids exceed its maximum index. Sphere logs the bad itemdef and continues.

Not run, each for a stated reason:

* **`deco_britain_felucca` a second time.** The decorator is **not idempotent** —
  every function is a flat list of `serv.newitem` with no existence check. This
  is why the 39 remaining cities were called *by name* rather than through
  `f_decorate_facet 0`, which would have doubled every Britain item.
* **`deco_magincia_ruined_felucca`** — mutually exclusive with
  `deco_magincia_felucca` (the same tiles, intact vs destroyed). The gump offers
  them as separate buttons because the operator picks one. Intact chosen so the
  city keeps working shops. Whether Revolution ran the 2007 ruined state is
  **UNKNOWN**.
* **Trammel (facet 1)** — enabled in `sphere.ini`, but the atlas is map 0 only
  and the generator skips every non-map-0 row (M2.5 debt 9).
* **Ilshenar / Malas / Tokuno / Ter Mur (facets 2–5)** — **no client data**.
  `map2..map5` are commented out of `spheretables.scp` because Revolution ships
  no `map2-5.mul`, and Sphere **auto-fixes an unsupported map onto map 0**. Running
  them would scatter their furniture across Felucca. Confirmed live: the door
  pass logged `Unsupported map #2..#5 specified. Auto-fixing that to 0.`
* **`f_stock_bookcases`** — `foritems 9999` creating five books per bookcase
  world-wide. Books are economically irrelevant.

### 2.3 Two traps that cost real time, recorded so nobody repeats them

**`.savestatics` does nothing, and looks like it worked.**

```
ERROR:Undefined keyword 'savestatics'.
'Admin' commands 'savestatics'=1      <- logs that a command was ISSUED, not that it succeeded
```

`SAVESTATICS` is `SV_SAVESTATICS` in `CServer::r_Verb` (`CServer.cpp:2205`) — a
**server** verb. A client command resolves against the **character's** verb
table first and never finds it. The working form is **`.serv.savestatics`**.

**Decorator items are not in `sphereworld.scp`.** Every one carries
`attr_static`, and Source-X routes those to a different file:

```cpp
CSector::SaveSector   else if (!pItem->IsAttr(ATTR_STATIC))
                          r_WriteSafe(g_World.m_FileWorld);   // sphereworld.scp
CWorld::SaveStatics   if (!pItem->IsAttr(ATTR_STATIC)) continue;
                      r_WriteSafe(m_FileStatics);             // spherestatics.scp
```

Between the two, the first verification after a **successful** pass showed zero
stations, and ~6,200 placed objects existed only in memory.

---

## 3. The engine fact underneath all of it

> **A spinning wheel or loom must be a DYNAMIC item. A map static is inert.**

`CClient::Event_Target` resolves the target with `uid.ObjFind()`
(`CClientEvent.cpp:2481`). A map static has no UID, so `pObjTarg` is `nullptr`,
and every relevant case in `OnTarg_Use_Item` begins:

```cpp
case IT_WOOL:
case IT_COTTON:
    if ( pItemTarg == nullptr ) break;      // -> falls through, nothing happens
```

A forge is different: `CWorldMap::FindItemTypeNearby` (`CWorldMap.cpp:663`)
scans dynamics, **then terrain, then statics**, so map art works for smelting
and for the smith menu.

This asymmetry is encoded as `prod::StationNeedsDynamicItem()` and asserted in
`m37_economy`, because it is the difference between a chain that works and one
that silently does nothing.

---

## 4. Phase 1 — the vendor audit

Full matrix in `REVOLUTION_VENDOR_MATRIX.md`. Headline:

> **Stock Sphere NPC vendors sell nearly the entire raw and intermediate
> production chain** — iron ingots, logs, boards, wool, yarn, thread, cloth,
> bolts, cotton, flax, hides, blank scrolls, bottles, feathers, arrows, nails,
> gears and all twenty-six reagents.

| | |
|---|---:|
| Vendor CHARDEFs defined | 203 |
| Reachable (appear in a `[SPAWN]` group) | **67** — every elf and gargoyle vendor is unspawned |
| Human professions | 34, of which **32 work** |
| Distinct items on a working vendor | **608** |

| Class | Count |
|---|---:|
| `PLAYER_CRAFTED` | 235 |
| `UNKNOWN` | 177 |
| `STOCK_SPHERE_ONLY` | 103 |
| `PLAYER_CRAFTED_NO_MENU` | 49 |
| `WORLD_GATHERED` | 21 |
| `ERA_CONFLICT` | 18 |
| `PLAYER_MARKET_GOOD` | 5 |

**284 of 608 (47 %) are goods a player can make.**

Three findings worth naming:

* **The mage shop stocks 18 Necromancy reagents.** Necromancy is skill 49; this
  client ships skills 0–48 and cannot display it. A bot trusting stock would
  stand in a 2010 Renaissance mage shop buying Stygian Abyss ingredients.
* **Two professions are defined and broken.** `c_rancher` and `c_glassblower`
  reference `VENDOR_*_RANCHER` / `_GLASSBLOWER`, which exist nowhere in
  Scripts-X. Both throw on every restock and keep no shop — found in the
  server's own log during the decorator run. In stock UO the glassblower is the
  **bottle** vendor, and bottles gate all of Alchemy.
* **Exactly one item in the whole matrix has a dated Revolution NPC
  permission**: pack horses and pack llamas, animal trainer, **03.11.2010** —
  ten days inside the profile window. An update announcing that an NPC *started*
  selling something is itself evidence that stock lists were curated and narrow.

---

## 5. Phase 19 — where enforcement lives

**In the bot. Not on the shard.** `econ::CanUseNPCVendorFor(item)`:

```
REVOLUTION_NPC_VERIFIED -> allow
PLAYER_MARKET_GOOD      -> allow only where an NPC floor is documented
WORLD_GATHERED          -> refuse; gather it
WORLD_PROCESSED         -> refuse; process it
PLAYER_CRAFTED          -> refuse; craft it or buy from a player
STOCK_SPHERE_ONLY       -> refuse
ERA_CONFLICT            -> refuse
UNKNOWN                 -> refuse, and LOG AN AUTHENTICITY GAP
```

Three properties that matter more than the table:

1. **The server stays a neutral fact.** Nothing on the shard was edited, so
   "the bot refused to buy wool" is falsifiable against a server that would
   happily have sold it.
2. **Refusing is never silent.** Every ruling carries a human-readable reason,
   asserted for every row by a test. The accumulated `UNKNOWN` refusals *are*
   the research backlog.
3. **Omission cannot permit.** An item the matrix never mentions grades
   `Unknown` and is refused — tested explicitly with a fabricated item name.

### 5.1 Ammunition is deliberately not blocked

The milestone asked whether NPC arrows are forbidden. The archive says **no**,
and the reasoning is worth keeping: the cooperative listed *arrow* and
*crossbow bolt* as searchable player goods (08.11.2008), **and**
`/oyun_rehberi` Bowcraft says bows sell *"diğer oyunculara ya da
**tezgahtarlara**"* — to other players **or to NPC vendors**. A player market
and an NPC floor demonstrably coexisted. Blocking arrows would invent a
restriction, which is the failure mode this milestone exists to avoid.

---

## 6. Phases 2 / 17 — the production graph, as code

`include/uo/production.h`, `src/progression/Production.cpp`.

**Every edge carries its evidence string, and a test fails the build if one does
not.** That rule exists because this project has twice published a number
inferred from a script declaration rather than observed — M3's reagent
consumption figure and M3.5's poison rule. `evidence` is not a comment; it is
the field that says whether a rule may be trusted.

Queries, all deterministic and I/O-free:

```
FindRecipe / ProvenanceOf / IsRawResource
MissingInputs(item, capability, held)   -> typed blockers, not a bool
CanSelfProduce
ProductionOrder(item, &cycle)           -> cycle detection, not a hang
RawInputsFor(item, qty)                 -> "what would I have to go and get?"
```

**No profession enum exists anywhere in the file.** Capability is
`{skills, toolsCarried, toolsEquipped, stationsReachable}`, and the tests use a
Mining + Blacksmithy + Alchemy + Magery hybrid deliberately: it smiths without
being "a Blacksmith", makes Cure at Alchemy 30.0 and is refused Greater Cure at
65.1, and reports that it cannot tailor rather than improvising.

`MissingInputs` distinguishes **`MissingTool`** from **`ToolNotEquipped`**,
because `CClientUse.cpp:1273` reads `LayerFind(LAYER_HAND1)` — a smith hammer
in the backpack is not a smith hammer, and the two need completely different
fixes.

---

## 7. Measured chain numbers

Full detail in `REVOLUTION_PRODUCTION_CHAINS.md`.

**Textile**, all `ENGINE`:

```
sheep --(any BLADED weapon; not scissors, not a crook)--> 1 wool
1 wool --(dynamic spinning wheel, NO skill check)--> 3 yarn
4 yarn --(dynamic loom)--> 1 bolt of cloth
1 bolt --(scissors)--> 50 cloth
```

Wool regrows in **30 minutes** (`WoolGrowthTime=30`); a **killed** sheep carves
for **3 wool**. Cotton spins to **6 thread**; `IT_YARN` and `IT_THREAD` share
the loom case, so either weaves — but Tailoring recipes consume **thread**, so a
wool-only tailor can weave cloth and cannot sew it.

**Mining**, `ENGINE`: **1 ore → 1 ingot**, checked against **Mining** (not
Blacksmithy), window from the ingot's `TDATA1`/`TDATA2` (iron 20.0/50.0), and a
failed smelt **destroys up to half the ore**.

**Agreements with RevolutionUO's own training guide** (forum topic 59111), which
are the strongest authenticity evidence M3.7 found:

| Craft | Revolution band | this runtime |
|---|---|---|
| Alchemy ×6 | 15.1 / 25.1 / 35.1 / 55.1 / 65.1 / 90.1 | **all six exact** |
| Inscription | resurrection 80–100 | `SKILLMAKE=Inscription 80.0` |
| Carpentry | ship model 95–100 | `carpentry 95.0` |
| Blacksmithy | dagger 0–70.1 | `Blacksmithing 0.0` |
| Bowcraft | shaft 0–100 | `i_arrow_shaft` 9.8 |

Six-for-six on Alchemy is not coincidence: Revolution ran essentially these
stock Sphere thresholds.

**Tailoring is the one craft that diverges hard.** Revolution's items — *Body
Sash* (74.1), *Oil Cloth*, mage robes from *"Hardening crystal ve kumaş"*,
special robes needing a robe-specific crystal, special leather via a *spirit of
nitre* potion, fishing nets at Fishing 80.0 — **none exist here**. The physical
chain is faithful; the product catalogue is stock Sphere's.

---

## 8. The Runebook, resolved to its real blocker

M3.6 recorded `LEGITIMATE_RUNEBOOK_CRAFTING = no`, attributing it to
"Inscription 45 plus four purchased inputs". That was not the binding
constraint.

**Material provenance is now fully world-sourced:**

```
tree -> log -> parchment -> blank scroll     ALL Carpentry 25.7
```

Eight blank scrolls bottom out at **eight logs** — cheap and gatherable
(asserted in `m37_economy`).

**The real wall is the Gate Travel scroll.** The recipe needs one, it is
**Inscription 70.0 + Magery 60.0**, and **no vendor sells a 7th-circle scroll** —
the mage shop stocks circles 1–4. So a legitimate crafter needs Magery 60, the
same wall M3 hit, or a PvM/treasure drop.

Two further blockers, both `SCRIPT`: `i_spellbook_runebook` is in **no skill
menu**, so the recipe is unreachable even at Inscription 100; and `i_spellbook`
(Inscription 50.0) is likewise menu-less. Revolution's **13.05.2009** entry —
*"Runebook copying added to the **Inscription menu**"* — is direct evidence that
Revolution's Inscription menu carried runebook operations.

**`LEGITIMATE_RUNEBOOK_CRAFTING_PASS` = still NO**, now with the reason named
precisely.

---

## 9. Reagents — the question that stays open

The milestone forbade guessing. It remains **`UNKNOWN`**, and that is the
finding.

Searched: `/oyun_rehberi`, `/oyuncu_komutlari`, `/genel_kurallar`,
`/tezgahtarlar_kooperatifi`, `/hazine_sistemi`, `/ozel_mage_robe`,
`/spawntakip_sistemi`, `/binek_bilgileri`, the full `/guncellemeler` changelog
(1200+ entries), forum topics 59111 and 54978. Forum **search** requires a login.

What the archive proves: a **real reagent economy**. A dedicated **Reagent
Crystal** existed by **07.11.2008**, with a settable withdrawal container; it
became the **Store Crystal** on 20.11.2010; and Recall's cost was cut **from
three reagents to one** on **14.05.2009** — a change that means nothing unless
they were being consumed.

What it does not say: **where they came from**. Both readings survive. Reagents
are absent from the cooperative's categories (weak evidence against a large
player market), and our atlas already carries **31 reagent field resource
areas** on map 0.

**Consequence:** `UNKNOWN` fails safe. A Revolution bot may not silently buy
reagents from an NPC; it logs an authenticity gap and prefers the fields. This
is the single most consequential open question in the matrix, because Magery
training consumes reagents continuously.

**And it is moot in practice** until `ReagentsRequired=0` (M3.6 debt 5) is
addressed: no spell on this runtime consumes anything at all.

---

## 10. Two long-standing UNKNOWNs closed

`REVOLUTION_RULESET_PROFILE.md` §4 listed *"Spawntakip taming requirements per
mount: schedule known, thresholds not."* Both halves are now known, from
Revolution's own pages:

**Thresholds** (`/binek_bilgileri`): Horse 53.1 · Llama 55.1 · Desert/Forest
Ostard 65.1 · Mustang 65.0–80.0 · Shire 65.0–95.0 · Frenzied 77.1 · Mid 80.0 ·
Kii-Rin 90.0 · Unicorn 98.1 · Steed 99.9 · Nightmare 99.9 · Chyrsoar 100.0 ·
Pegasus 100.0.

**Supply** (`/spawntakip_sistemi`): a calendar regenerated every Monday —
**49 mounts a week, 7 a day**: 1 Steed, 1 Nightmare, 1 Unicorn, 2 Kirin,
5 Mustang, 5 Shire, 7 Frenzied, 7 Mid, 10 Forest, 10 Desert Ostard. Rare mounts
late in the week.

A hard-capped weekly supply of 49, contested by every tamer on the shard, is the
purest player-market good in the archive.

Also newly dated: **Special Robes require Magery, Eval Int **and** Meditation
≥ 98.1 and no warrior skill** (`/ozel_mage_robe`) — a refinement of M3.5's
"Eval Int 98.1", which named only one of the three skills and missed the warrior
veto entirely.

---

## 11. New authenticity conflicts found

| # | Conflict | Evidence |
|---|---|---|
| 16 | **Ore weight** 2 in runtime; Revolution cut it **3 → 1** | 13.12.2008, 06.04.2009 |
| 17 | **Ore spawn table** is stock; Revolution raised silver and lowered iron | 03.04.2009, 12.04.2009 |
| 18 | **Treasure-map Lockpicking**: runtime 36/76/84/92/100 with a level 1; Revolution **40/60/80/100**, levels **2–5 only** | `/hazine_sistemi` |
| 19 | **18 Necromancy reagents** on a Renaissance mage shop | client ships skills 0–48 |
| 20 | **Cooked fish has no buyer** here; Revolution sold cooked fish to vendors | `/oyun_rehberi` |
| 21 | **Cooking batch formula**: in-era Revolution is `(skill/100)*8` = 80 at GM | 12.04.2009 |
| 22 | `c_rancher` / `c_glassblower` reference undefined vendor templates | live server log |
| 23 | Tailoring product catalogue is stock Sphere's, not Revolution's | §7 |
| 24 | Fishing nets, Shell, S.O.S. entirely absent | §8 of the chains doc |

Conflict 18 is also a warning about method: `TNS_DONOR_AUDIT.md` §2.11.1 shows a
second Turkish Sphere shard carrying the **same stock 27/71/81/91/100 values**,
so cross-checking against it would have "confirmed" the wrong table twice.

---

## 12. Navigation regression after the world change

The decorator added ~6,200 **dynamic** blocking items that the navgrid — baked
from client *statics* — cannot know about. Raised as a risk, then measured.

`m37nav1`, `m25_service_bank`, Britain → **Yew banker**, 28 legs, ~1104 tiles:

| | M3.6 baseline | after the decorator |
|---|---|---|
| plans | 1 | **1** |
| move rejects | 2 (cross-region) | **2** |
| lookahead patches | — | **0** |
| recovery rungs | 0 | **0** |
| errors | 0 | **0** |
| outcome | ARRIVED | **ARRIVED, off by 0** |

Both rejects were the same obstruction and self-healed in ~1.2 s:

```
move REJECTED seq=1; server says (824,1111,0)
step (824,1111,0)->(825,1110,0) rejected; OpenDoor + retry
move REJECTED seq=0
step ... rejected; avoiding edge + rerouting
replan to (824,1080): 33 steps in 13947.7us
arrived at (824,1080,0) ... ARRIVED (off by 0 tile(s))
```

**No measurable cost on open-country travel.** The reason is structural: the
navgrid is a coarse macro graph and the tile A* re-derives walkability at walk
time, so furniture surfaces as a refused step rather than a bad plan — precisely
what the M1.5 `OpenDoor`/reroute path was built for.

### 12.1 Correction — that conclusion was drawn too early

The run above is 1104 tiles of **open country**. An earlier revision of this
section generalised it to "no measurable cost", full stop. That was wrong, and a
later run disproved it: `m37b9`, walking a miner out of the **Minoc bank**,
could not leave the building.

```
no path to (2472,536) avoiding 0 block(s); stopping (search 23.7us)
route exhausted at (2498,548,0); trying to reach (2488,552,0) ... (escape 1/3)
no path to (2488,552) avoiding 0 block(s); stopping
route exhausted at (2498,548,0); trying to reach (2488,536,0) ... (escape 2/3)
no path to (2488,536) avoiding 0 block(s); stopping
route exhausted at (2498,548,0); trying to reach (2504,536,0) ... (escape 3/3)
no path to (2504,536) avoiding 0 block(s); stopping
```

All three M2.5 escape rungs failed from one interior tile. Note `avoiding 0
block(s)`: the planner was not routing around anything it knew about — the
decorator's dynamic furniture is invisible to the baked navgrid, so from inside
a furnished room every escape target looks reachable right up until each step is
refused.

**The corrected finding:** the decorator does not degrade open travel, and it
*can* trap a bot inside a decorated interior. Slice C's buyer scenario was
rewritten because of this — it no longer banks before trading, and says so in
its own comments. Interior routing is carried forward as open work, not closed.

`scripts/path_regression.bat` is unaffected: it runs against the client MULs,
not the server, and nothing under `src/bot/` or `World::QueryCell` changed.

---

## 13. Tests

| Suite | Checks | Covers |
|---|---|---|
| `sphere_regression` … `m36_progression` | unchanged | M1–M3.6 |
| **`m37_economy`** | **96** | graph integrity, textile, stations, mining/smithing, hybrids, missing inputs, runebook provenance, vendor policy, acquisition, needs/offers, graph queries, hybrid reasoning |

**8/8 suites, 0 failures.** (`tests/path_probe.exe` also builds, but it is a
manual probe that wants an external tiledata path — `td load fail
(E:/uo/tiledata.mul)` — and is deliberately not registered with CTest.)

Assertions worth calling out:

* every recipe carries an evidence string, and no output has two producers;
* a static spinning wheel is unusable and a static forge is usable;
* a smith hammer in the pack reports `TOOL_NOT_EQUIPPED`, not `MISSING_TOOL`;
* an item the vendor matrix never mentions **fails safe**;
* **a skill-less bot with 10,000 gold and a live 3 gp quote still may not buy
  wool from the shepherd, and reports `BLOCKED` rather than improvising.**

---

## 13a. The live vertical slices

### The harness lesson that came first

For most of this milestone the slices were being judged from `local/dev/<tag>.log`.
**That file does not contain the verdicts.** `run_m25.ps1` splits three streams:

| file | carries |
|---|---|
| `<tag>.log` | the client's own packet/action log |
| `<tag>.console.txt` | stdout — **every `[scenario] line N:` step and every assertion that passed** |
| `<tag>.err.txt` | stderr — **every `ERROR ... aborting` and the `ABORTED at line N` that ends the run** |

Each `.log` holds exactly one `[scenario]` line — the `loaded '...'` banner — so
grepping it for assertions returns nothing whether the slice passed or failed.
Two slices were reported here on that basis. Both reports were wrong, and the
corrections are below.

### Results

| Slice | Chain | Result |
|---|---|---|
| **A** textile | sheep → wool → yarn → bolt → cloth → **garment** | **PASS** |
| **B** mining/smithing | ore → ingot → dagger | **PASS** |
| **C** cross-player market | miner → trade → smith | **PASS** |
| **D** carpentry | tree → logs → board | **PASS** |
| **E** NPC authenticity | tool allowed, resource refused | **PASS** |

### B — `m37b12`, `finished (113 steps)`, 0 errors

```
item 0x19B7 gain confirmed: 0 -> 6        # mined ore, Minoc Mine1
item 0x1BEF gain confirmed: 42 -> 49      # smelted at a decorator-placed forge
System: You put the dagger in your pack.  # Blacksmithing menu 3 -> 2
```

Six ore became seven ingots; the dagger is the first player-forged item on this
shard. Every gate on the way was a real one: `REQSTR=50` on the pickaxe, the
tool having to be **equipped** for `<SRC.WEAPON.USESCUR>`, and the mine's
`AREADEF` rectangle.

### D — `m37d7`, `finished (67 steps)`, 0 errors

```
item 0x1BDD gain confirmed: 2 -> 6        # chopped a static tree
System: You put the board in your pack.   # Carpentry 0x7C menu
```

Note the repeated `There is nothing here to chop.` in the same log — individual
trees exhaust, so a lumberjack bot must move between them rather than re-target.

### E — `m37e2`, `finished (72 steps)`, 0 errors

Three different provenance classes, three different answers, all from live
vendors in one run:

| Item | Class | Outcome |
|---|---|---|
| saw | `BasicCraftTool` | **allowed** |
| logs (`0x1BDD`) | `WORLD_GATHERED` | **refused**, `unchanged at 0` |
| garlic (`0x0F84`) | `UNKNOWN` | **refused *and* gap recorded** |

The third row is the one that matters most, because it is the behaviour the
milestone was told to prefer over guessing:

```
[policy] REFUSED NPC purchase of i_reag_garlic (0x0F84): no Revolution
         evidence either way; refusing and recording an authenticity gap [UNKNOWN]
[policy] AUTHENTICITY GAP: no Revolution evidence either way for whether
         an NPC sold i_reag_garlic
```

It does not quietly buy, and it does not quietly decline — it declines **and
says the evidence is missing**, which is what turns §4's open reagent question
into something a later milestone can find rather than something silently
decided here.

#### The earlier run's refusal was right; its assertion was impossible

The proof itself had already passed, at the client's own gate:

```
event action_result: vendor_buy rejected took=0ms
    Revolution vendor policy refuses this NPC purchase
[scenario] expect rejected: ok
```

The saw (a `BasicCraftTool`) was allowed and the logs (`WORLD_GATHERED`) were
refused **at the same vendor, in the same run** — which is the whole point of
the slice: the policy discriminates by provenance, not by vendor.

The run then died on my own assertion:

```
ERROR [scenario] EXPECT item 0x1BDD to drop: 0 -> 0 (line 103); aborting
```

The character owns no logs, so the baseline is 0 and `expect_item_drop` demands
a *decrease* — it could only ever fail. Proving a refusal needs "the count did
not move", which no op expressed. **`expect_item_same` was added** (`Scenario.h`
`Op::ExpectItemSame`) and slice E now asserts with it.

### C — the cross-player market proof

Two real characters, two sockets, no GM action, gold physically counted out as
coins because this client version has no virtual gold ledger.

`m37c6`, both sides, every figure read back out of the server after the window
closed:

| | before | after | Δ |
|---|---|---|---|
| **Miner** gold | 558 | 598 | **+40** |
| **Miner** ingots | 43 | 42 | **−1** |
| **Smith** gold | 920 | 880 | **−40** |
| **Smith** ingots | 52 | 53 | **+1** |

```
# seller, m37c6_traveller.console.txt
mark_gold = 558
mark_item 0x1BEF,... = 43
trade completed, as expected (both_accepted)
gold gain confirmed: 558 -> 598
item 0x1BEF drop confirmed: 43 -> 42

# buyer, m37c6_blocker.console.txt
mark_gold = 920
mark_item 0x1BEF,... = 52
trade completed, as expected (both_accepted)
item 0x1BEF gain confirmed: 52 -> 53
mark_gold  = 880
```

**The columns balance exactly.** Forty gold left one purse and arrived in the
other; one ingot did the reverse. Nothing was created and nothing evaporated —
which is the entire claim M3.7 exists to support, made against a live server by
two ordinary characters with no GM involvement.

#### The optional tail failed safe, and found something

Slice C's buyer then tries to forge with the metal it bought. It could not, and
the scenario stopped exactly where it was designed to:

```
ERROR [scenario] REQUIRED 'hammer' is not bound; aborting scenario
```

The forge step is deliberately placed **after** the trade assertions, on the
reasoning that a vendor with nothing in stock must not be able to cost us the
trade proof. That ordering paid for itself here.

The Britain blacksmith's entire stock at that moment:

```
[VENDOR] offer from 0x00000AC5: 2 item(s)
  0x40002A4E iron ingots            x6   10 gp
  0x40002A4F tongs                  x16  15 gp
```

Two items, and **neither is usable by a new smith**: the ingots are
`PLAYER_CRAFTED` (the policy refuses them, correctly — this run's smith had just
bought ingots from a player), and tongs sit on equip layer 0 in Revolution's
tiledata, so `LayerFind(LAYER_HAND1)` can never find them. No smith's hammer.

And the smith cannot make one either:

```
i_hammer_smith  RESOURCES=6 i_ingot_iron,1 i_log
                SKILLMAKE=Tinkering 40.0,t_tinker_tools
```

`i_hammer_smith` appears in the **Blacksmithing** legacy menu as well as the
Tinkering one, but the `SKILLMAKE` gate is Tinkering 40.0 either way, and this
character has Tinkering 30.0. It also appears in several `tm_vend.scp` BUY
lists, so vendors *can* carry it — this one did not.

**The finding: a smith's first hammer is a player-to-player dependency.** A
fresh Blacksmith either finds a vendor whose restock happens to include one, or
buys it from a Tinker with 40+. That is exactly the kind of inter-player
requirement M3.7 exists to surface, and it was invisible until a bot tried to
buy one. Not treated as a blocker: slice B's smith already owns a hammer and
forged with it.

#### It took four attempts, and the first three failures are the useful part

The **first** run's buyer aborted on its own assertion even though the goods had
arrived:

```
ERROR [scenario] EXPECT item 0x1BEF to increase: 0 -> 51 (line 78); aborting
```

`ExpectItemGain` compares its graphic list against the list `mark_item` stored,
and that buyer had called `mark_gold` **only** — so an unmarked list aborts a
run in which the count plainly rose from 0 to 51. The assertion was wrong, not
the trade.

#### Two client behaviours this slice exposed

Both were found by trying to trade a realistic quantity, and both are properties
of the client rather than of the scenario:

1. **The entity resolver answers from a container snapshot that only an `0x3C`
   refills.** Re-resolving a pack graphic straight after `trade_start` returns
   the uid of the stack that just moved into the trade window, and the server
   answers `cannot lift that (code 0)`. Re-opening the pack first fixes it.
   *The same bug had silently killed slice A* — see below.
2. **`wait_trade_mine` / `wait_trade_offer` count stacks, not amounts**
   (`Trade().MyOffer().size()`). A second stack of the same graphic **merges**
   into the first, so a scenario waiting for `2` waits forever. Both sides
   deadlocked until the harness timeout.

`trade_start` has no amount parameter, so the traded quantity is whatever stack
the resolver returns. That is a recorded DSL limit, not a gap in the proof — the
proof is the four numbers either side of the exchange.

A fourth attempt died on an operator error worth writing down too: a stale
background job fired late and raced a fresh one onto the same account.

```
13:02:77 Login for account 'revolutionsmith01' ... Character startup
13:02:79  Account 'revolutionsmith01' already in use.
```

`LOGIN DENIED (1): account already in use` is not always the five-minute
`DeadSocketTime` lock it looks like. Here it was two of our own runs colliding,
and `runtime/logs/sphere<date>.log` is what distinguishes the two cases —
it names every login, startup and disconnect per account.

### A — sheep to finished garment, every step live

The whole chain, each link measured against the server's own state:

| Step | Evidence | Run |
|---|---|---|
| sheep → wool | `0x0DF8 0 -> 1` | `m37a8` |
| wool → yarn | `0x0E1D 0 -> 3` — the engine's stated rate exactly | `m37a8` |
| yarn → bolt | `System: The bolt of cloth is finished.` | `m37a8` |
| bolt → cloth | `0x175D 0 -> 50` — `RESOURCES=50 i_cloth` paid out in full | `m37afin` |
| cloth → **hat** | `0x1714 0 -> 1`, `You put the wide-brim hat in your pack.` | `m37asew` |

A character sheared a sheep, spun the wool, wove the yarn, cut the bolt and
sewed a wide-brim hat, on a live server, with no GM action at any point.

#### Why it is three scenario files and not one

`m37_slice_a_tailor.txt` walks to the sheep field and back — twelve minutes of
road each way — and then needs to find **two** woolly sheep near each other. It
does not reliably do so, and the reason is a real fact about the world rather
than a scenario defect: shearing turns a sheep into `CREID_SHEEP_SHORN`, so
working a flock thins it, and the next scan returns the animal you already
sheared or nothing at all.

```
m37a8  wool 0 -> 1, yarn 0 -> 3, one bolt finished, then out of yarn
m37a9  wool 0 -> 1, the second shear yielded nothing whatsoever,
       ending at `REQUIRED 'wool2' is not bound`
```

Both endings are the fixed `require` doing its job — the earlier version of this
scenario **hung at `wait_target` forever** instead. Re-proving the walk costs
half an hour per attempt and proves nothing new, so the closing links were split
into `m37_slice_a_finish.txt` (cut) and `m37_slice_a_sew.txt` (sew), which run
in Britain in about two minutes.

A scenario that consumes a non-renewable input cannot be idempotent, and that is
worth stating plainly: `m37afin` cut the weaver's only bolt, so re-running it
aborted at `REQUIRED 'bolt' is not bound`. Correct behaviour, not a regression.

---

### 13b. The craft menu is a live requirements oracle

The most useful thing slice A produced was not the hat. **Sphere filters a craft
menu down to what the character can actually make at that moment**, so the menu
itself reports recipe requirements without reading a single script.

`sm_tailor_cloth` **defines five entries** — Shirts, Pants, Headwear, Misc., bolt
of cloth. A weaver holding 50 cloth and zero thread was shown two:

```
[0x7C] "Cloth" (2 options)
        1) Headwear
        2) bolt of cloth (50 folded cloth)
[0x7C] "Headwear" (2 options)
        1) wide-brim hat (12 folded cloth)
        2) straw hat (10 folded cloth)
```

Shirts, Pants and Misc. are absent because they stitch. Inside Headwear the
bandana — `RESOURCES=2 i_cloth,1 i_thread` — is absent for the same reason,
while the two cloth-only hats remain.

**This corrected a claim made earlier in this very report.** §13a had said every
garment a tailoring menu can reach spends thread. It does not:

```
i_hat_wide_brim (0x1714)  RESOURCES=12 i_cloth
                          SKILLMAKE=Tailoring 6.2,t_sewing_kit
```

Cloth only. A wool tailor is **not** shut out of clothing — it is shut out of the
recipes that stitch. Narrower, and true.

Two consequences worth carrying into M4:

* **Menu indices are into the FILTERED list, not the script file.** Choosing
  Headwear by its script position earned
  `[0x7D] dialog index 3 out of range (2 options)`. A bot must read the menu it
  was sent, never the one the .scp defines.
* **A bot can ask the shard what it can make** by opening a menu and reading the
  options, instead of modelling requirements it might get wrong. That is a
  cheaper and more authentic source of truth than the compiled-in graph, and it
  is the one that caught this report's own error.

---

### A — the earlier partial runs, and what they cost

`m37a7` reached the loom and then went silent until the harness killed it:

```
item 0x0E1D gain confirmed: 0 -> 3        # 1 wool -> 3 yarn
System: The bolt of cloth is finished.
System: The bolt of cloth is finished.
[scenario] line 285: wait_target          # ...and nothing after this
```

Cause: resolver bug (1) above. `remember yarn3` handed back the uid of yarn the
previous pass had **consumed**; `use` on a dead item draws no target cursor, and
`wait_target` blocks forever. It never reached a single assertion. The repeat
passes now re-open the pack and `require`, which turns "no yarn left" into a
named abort instead of a hang.

**The thread gate is real but narrower than this report first claimed.**
`REVOLUTION_PRODUCTION_CHAINS.md` §2.3 already answered "yarn or thread?" from
the engine: cloth comes from wool, thread from cotton. An earlier revision of
this section went further and said *every* garment a tailoring menu can reach
spends thread. **That was wrong, and the shard said so itself** — see §13b.

The recipes that do spend thread:

* `i_bandana` — the cheapest garment on the shard, Tailoring 0.1 —
  `RESOURCES=2 i_cloth,1 i_thread`;
* 264 recipes under `runtime/scripts` spend `i_cloth`, and the ones that spend
  **no** thread are building and decoration items, not clothing.

And thread does not come from wool. `CClientTarg.cpp:2053` handles `IT_WOOL` and
`IT_COTTON` on a spinning wheel and nothing else:

> "1 pile of wool yields three balls of yarn" — matched live, `0 -> 3`
> "1 pile of cotton yields six spools of thread"

`i_thread`'s own itemdef says `RESOURCES=1 i_flax_bundle`. **That line is a
value declaration for `<resmake>`, not a production path** — the engine never
reads it to make thread. A self-sufficient tailor therefore needs sheep *and* a
cotton field; wool alone weaves cloth it can never sew. Slice A now ends by
asserting `expect_item_same 0x153F` — no bandana — so the stopping point is
recorded as evidence rather than as a failure.

### Loom state is shared and persistent

Measured while reconciling A's numbers: three yarn appeared to finish a bolt,
which contradicts the graph's 4-yarn recipe. The graph is right.
`sm_Txt_LoomUse[]` has five entries and the loom needs `ARRAY_COUNT - 1 = 4`,
but `m_itLoom.m_iClothQty` **persists on the loom item between uses**. A partly
loaded public loom therefore carries one player's yarn into the next player's
weave.

Targeting it with a *different* material does not destroy what is stored: the
handler sends `DEFMSG_ITEMUSE_LOOM_REMOVE`, rebuilds the stored material as an
item and `ItemBounce`s it — **to whoever is using the loom now**, not to whoever
left it there. Nothing is lost from the economy; it changes hands. Bots sharing
a town loom must not assume it starts empty, and may find themselves holding a
stranger's yarn.

### An era-authenticity flag found on the way

`i_profession_tailor_tanner.scp:700` defines an **AutoLoom**: target it with 5
wool, cotton or flax and it returns cloth bolts directly, skipping the spinning
wheel and the loom entirely (`f_autoloom_use` → `serv.newitem=i_cloth_bolt`).
It is an AoS-era convenience item and it collapses the whole textile chain into
one click. Flagged `ERA_CONFLICT`; it is not used by any slice.

---

## 14. What has NOT been done

Stated plainly, because the milestone is not finished.

1. **No thread has ever been made on this shard.** Slice A reaches a garment
   without it (§13b), but the cotton → thread half of the textile chain is
   still only an engine reading — `CClientTarg.cpp:2075`, "1 pile of cotton
   yields six spools of thread". Nothing has picked cotton, and every stitched
   recipe therefore remains unproven live.
2. **`data/revolution_*.tsv`** generated exports are not written; the graph
   currently lives only as compiled-in data.
3. **The 49 `PLAYER_CRAFTED_NO_MENU` recipes** — including `i_nails`, `i_spoon`
   and `i_barrel_lid`, which RevolutionUO's own training guide names — are
   diagnosed but the menu entries are not restored.
4. **Phases 12–15 are modelled from scripts, not exercised**: hides/leather,
   cooking, taming and PvM/treasure have no live proof.
5. **The world change is not reversible by script.** `runtime/save_backup_pre_deco/`
   holds the pre-decorator world; there is no "undo Britain" function.
6. **Interior routing can trap a bot** (§12.1). All three M2.5 escape rungs
   failed from inside the Minoc bank. Scenarios currently avoid the problem by
   staying outdoors; the planner is not fixed.
7. **Trade quantity is not controllable from the DSL.** `trade_start` takes no
   amount and same-graphic stacks merge in the window, so a scenario cannot
   negotiate "20 ingots" — it trades whatever stack the resolver returns
   (§13a).

---

## 15. Corrections to earlier reports

* **M3 §6.5** listed "Mining → ore → smelt → ingots" as `SCRIPT_VERIFIED, not
  run`. It was not merely unrun — it was **impossible**: no forge existed within
  reach of any town. Same for every Tailoring row.
* **M3.5 / the ruleset profile** recorded Special Robes as *"Eval Int 98.1"*.
  Revolution's own page requires **Magery, Eval Int and Meditation all ≥ 98.1
  and no warrior skill**. Two of the three skills and the veto were missing.
* **The ruleset profile** listed *Spawntakip taming requirements per mount* as
  unknown. They are published; §10 has them.

### Corrections to earlier statements made *within* this milestone

* **Slices A and E were reported as passing. Neither had.** Both verdicts came
  from `local/dev/<tag>.log`, which contains no assertion output at all —
  results are in `.console.txt` and aborts in `.err.txt` (§13a). E had died on
  an impossible `expect_item_drop`; A had hung at `wait_target` and never
  reached an assertion. Both are fixed and re-run; the habit that produced the
  error was grepping the convenient file rather than the correct one.
* **§12 said the decorator had "no measurable cost".** True of open country,
  false of interiors — §12.1 has the Minoc evidence.
* **Slice C was reported as "trade completed, results unconfirmed".** The trade
  had completed and the seller's numbers were correct; the buyer's assertion was
  malformed. Both sides now assert.
* **§13a claimed every tailoring-menu garment spends thread.** It does not.
  `i_hat_wide_brim` is `RESOURCES=12 i_cloth` and the shard offered it to a
  weaver holding no thread at all. The claim was inferred from a scan of 264
  `i_cloth` recipes; the shard's own filtered menu disproved it in one line
  (§13b). Reading the menu the server actually sends beats reasoning about the
  scripts behind it.
