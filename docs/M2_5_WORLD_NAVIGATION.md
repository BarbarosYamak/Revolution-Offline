# M2.5 — Whole-World Knowledge, Hierarchical Navigation and Authentic Travel

Date: 2026-08-26. A bot can now be given a destination anywhere in the Revolution world as a *need* rather than a coordinate, and it plans and walks the journey itself — running, using the shard's own teleporters and public moongates, replanning around what gets in the way, and putting its weapon away when it stops fighting.

Source-X modifications: **0**. Scripts-X (`server/Scripts-X`) modifications: **0**. Runtime script modifications: **1**, the pre-existing M0 map configuration, audited below and now committed rather than left dangling.

---

## 1. Baseline

| | |
|---|---|
| Branch | `revolution-sphere-m1` (`bot/uo-client`) |
| Baseline commit | `2c74859` (M2 PASS) |
| Shard | SphereServer Source-X nightly, `127.0.0.1:2593`, no encryption |
| Client data | `local/revolution-client`, client version 2.0.3 |
| Facet | map 0 (Felucca), **7168 × 4096** tiles |

---

## 2. `spheretables.scp` maps 2–5 — audit result

**Classification: A — required Revolution compatibility configuration. Kept disabled, and now committed** (`runtime/scripts`, branch `revolution-runtime`, commit `dc20378`) so the working tree is clean and the change is attributable instead of floating as an unexplained local edit.

The evidence:

1. **Revolution ships one facet.** `local/revolution-client` contains `map0.mul`, `statics0.mul` and `staidx0.mul` and no `map2..5.mul`. `runtime/mul` mirrors it. `runtime/sphere.ini` agrees: `Map0=7168,4096,-1,0,0`.
2. **Source-X disables a map whose MULs are missing** (`src/common/CUOInstall.cpp:395-412`).
3. **Points naming a disabled map are not ignored — they are moved.** `CPointBase` "auto-fixes" them onto map 0 (`src/common/CPointBase.cpp:996-1002`, `src/common/CRect.cpp:174`), and `RES_TELEPORTERS` entries become live `CTeleport`s at those coordinates (`src/game/CServerConfig.cpp:4133-4143`, `src/game/CTeleport.cpp:23-24`). Loading `maps/map2..5/*` therefore plants roughly 1,600 Ilshenar / Malas / Tokuno / Ter Mur regions, teleporters and moongates *on Felucca*.
4. **It was measured, not assumed.** Boot #2 (`runtime/logs/boot2_sphere2026-08-25.log`) produced **1,606** `Unsupported map #N specified. Auto-fixing that to 0.` errors. Boot #3, with the lines commented out, produced none.

This is operator configuration for the shipped client data: no rule, script or definition is altered, and re-enabling the lines on a server that has the other MULs restores them exactly. M2.5 re-audited it and reached the same conclusion; the world atlas generator independently confirms the shape of the world by skipping every non-map-0 row in the shard's own teleporter tables.

`maps/map1/*` (Trammel) is still enabled. Source-X reads map 1 from `map0.mul` when `map1.mul` is missing (`CUOInstall.cpp:408-409`), so a Trammel mirror exists. Whether Revolution had one is still **UNKNOWN**; M2.5 does not depend on it — the atlas is map 0 only.

---

## 3. A real defect found on the way in: the world was a third too small

`include/uo/map.h` hard-coded Britannia at **768 × 512 blocks** (6144 × 4096 tiles), the pre-ML size. Revolution's `map0.mul` is 89,915,392 bytes = **896 × 512 blocks** (7168 × 4096), which is what `sphere.ini` configures and what the shard's own AREADEFs use — they reach x = 7168, and the Lost Lands towns (Papua ≈ 5670, Delucia ≈ 5250, Heartwood ≈ 7035) live out there.

The bot could not path east of x = 6143 at all, silently.

Fixed two ways, because one of them should never have been a constant:

* `kBritWidthBlocks` is now 896, with the pre-ML width kept as a named constant.
* `Map::Open` **derives the width from the file size** when the rows divide cleanly and says so on stderr. `map*.mul` carries no header, so the file is the only authority; a caller's width is a request.

---

## 4. Architecture

```
bot brain (M3+)          "I need a banker"
      |
semantic destination     Client::TravelToService(Service::Banker, "Yew")
      |
world knowledge          world_atlas::Atlas   (shared, immutable)
      |                  travel::PersonalKnowledge (per character)
      |
world route planner      route::RoutePlanner over navgrid::NavGrid
      |                  -> a handful of legs, each <= 40 tiles
      |
journey state machine    travel::Journey  (sequencing, recovery, boundedness)
      |
tile pathfinding         Client::ActionGoto -> BotStartGoto -> M1.5 A*
      |
movement controller      Client::SubmitStep()   <-- still the ONLY 0x02 sender
      |
Sphere / Source-X
```

New code, and what each piece is *not*:

| Unit | Is | Is not |
|---|---|---|
| `include/uo/world_model.h` | the vocabulary: Region, Place, Service, ResourceKind, TransitNode, Danger | any I/O, any protocol |
| `src/world/Atlas.{h,cpp}` | the shard's world as semantics; lookup by id/name/service/resource | mutable, per-character, or hand-authored |
| `src/world/NavGrid.{h,cpp}` | 16×16-tile walkability + **measured crossings** | a waypoint graph |
| `src/world/RoutePlanner.{h,cpp}` | cell-level A* + transit edges → a leg list | a step emitter |
| `src/world/SharedWorld.{h,cpp}` | one immutable copy per process | per-session state |
| `src/world/AtlasGenMain.cpp` | the offline generator (`uo_atlasgen`) | part of a bot session |
| `src/travel/Journey.{h,cpp}` | leg sequencing, stuck/oscillation detection, the bounded recovery ladder | anything that sends a packet |
| `src/travel/PersonalKnowledge.{h,cpp}` | what *this* character has seen, marked, died at | shared, static, or global |
| `src/travel/WarMode.{h,cpp}` | when to sheathe | a local mirror of war state |
| `src/travel/ClientTravel.cpp` | the Client glue + the 0xB0/0xB1 gump plumbing | a second movement path |

`SubmitStep()` is untouched and remains the sole `0x02` emitter. Travel walks by calling `ActionGoto`, the same public action a scenario uses.

---

## 5. World sources, and what was derived from each

`tools`-style generator: `uo_atlasgen`. Everything it emits is traceable to a file the shard reads at boot. Nothing is hand-authored geography.

| Output | Source | Count |
|---|---|---|
| Regions (+ rects, flags, centres) | `maps/map0/map0_areas*.scp` (AREADEF), `map0_rooms.scp` (ROOMDEF) | **790** |
| Teleporters | `maps/map0/map0_teleports*.scp` (`RES_TELEPORTERS`) | **450** |
| Moongates + the destination mesh | `functions/worldgen/decoration/moongates.scp` | **10** gates, 90 hops |
| Inns / start points | `maps/map0/map0_starts.scp` | 9 |
| Service places | `functions/worldgen/spawns/felucca/Vendors_spawns_felucca.scp` | 218 shops, 19 banks, 20 healers, 11 stables, 49 inns |
| Reagent fields | `Reagents_spawns_felucca.scp` | (in the 274 resource areas) |
| Hunting grounds | `WildLife_spawns_felucca.scp` | ditto |
| Mines, docks, shrines, graveyards, dungeon entrances, town centres | AREADEF names and `P=` points | 17 docks, 15 dungeon entrances, 12 graveyards, 9 shrines, 17 town centres |
| **Forests** | **measured from `statics0.mul`** — foliage density per navgrid cell | 43 lumber areas |

Totals: **790 regions, 707 places, 540 transits**, generated in ~16 s (plus ~64 s for the crossing pass, below).

Two derivations deserve their reasoning stated:

**Spawner rows are the service data.** `f_create_spawner,job1..job6,X,Y,Z,facet,min,max,walkRange,homeRange,...` — the layout is documented in the shard's own `functions/worldgen/spawns/spawner_functions.scp`. The job names map onto `wm::Service`; the spawner's **home range is the place's interaction radius**, because it is exactly how far the shard lets that NPC wander. "The Yew banker" is a place five tiles wide because Sphere says the banker may be anywhere in five tiles.

**Forests are measured, not listed.** Britannia has no forest table anywhere in the shard data. So `NavGrid::Build` counts, per cell, how many sampled tiles sit in or beside a Foliage-flagged static, and `uo_atlasgen` collapses the densest clusters into lumber areas — **the best few per region**, not the best N globally. Globally-densest was tried first and reported that the only woods on the continent are the southern jungle: the canopy there is continuous and Yew's oaks are not, so a Yew lumberjack was left with nowhere to go. Per-region density gives Yew, Britain, Minoc, Vesper, Cove and the rest their own woods at their own local density.

### Regenerating the world data

```
uo_atlasgen --scripts runtime/scripts --mul runtime/mul \
            --out-atlas bot/uo-client/data/revolution_atlas.txt \
            --out-grid  bot/uo-client/data/revolution_navgrid.bin
```

`data/revolution_atlas.txt` is committed (derived from the open Scripts-X data, tab-separated, diffable). `data/revolution_navgrid.bin` is **not** — it is measured off the copyrighted client MULs — and is regenerated by the command above. Without it a bot still walks with `goto`; it just has no semantic destinations.

`uo_atlasgen --probe <x> <y>` answers "what does the generated world think is here?" from the same files the bot loads. Every coordinate quoted in the scenarios below was chosen with it rather than by eye.

---

## 6. Hierarchical navigation

### 6.1 Why a hierarchy at all

The M1.5 tile A* was measured on town-sized problems. Britannia is ~29 M tiles; Yew → Britain is about a thousand of them. The first attempt at a cross-world route confirmed the cost precisely: a single leg of 1281 steps took **9.05 seconds** of A*, and a leg with no path burned 0.9 s before giving up.

So the world is summarised once, offline, into 16×16-tile cells: **448 × 256 = 114,688 cells, 35,222 of them holding standable ground**. Routing runs over those; the tile A* only ever runs between consecutive cell anchors, on a problem the size it was measured on. A cross-world plan expands ~1,100 cell nodes in ~1 ms.

### 6.2 The mistake worth recording: passable ≠ crossable

The first edge model was "two neighbouring cells both contain standable ground, therefore you can walk between them". That is not the same claim, and Britannia is full of counter-examples: the far bank of a river, the far side of a mountain, the inside of a building.

Live, it looked like this — a bot 700 tiles into a Yew → Britain run, stopped at (1320,904):

```
[bot] no path to (1358,898) avoiding 0 block(s); stopping (search 907134.6us)
[bot] no path to (1358,898) avoiding 0 block(s); stopping (search 922593.7us)
[bot] no path to (1358,898) avoiding 0 block(s); stopping (search 957136.8us)
[travel] Britain FAILED at (1334,936,0) -- tile route stopped short
```

The router kept proposing crossings of a river the walker could not cross, and no amount of avoid-cell feedback fixes a graph whose edges are wrong.

The fix is to **measure** the edges. `NavGrid::BuildEdges` runs the bot's own tile A*, bounded to 600 nodes, between every adjacent pair of cell anchors and records an 8-bit mask of the crossings that actually work. What the router believes is crossable is now established with the same code the walker uses. It costs ~64 s once, offline, and it turned that failure into this:

```
[travel] plan Britain: ok legs=31 ~1160 tiles transit=0 nodes=1139
... 31 legs, every one "ARRIVED (off by 0 tile(s))" ...
[travel] Britain ARRIVED at (1490,1622,16)
```

### 6.3 Leg emission

Legs are capped at 40 tiles and the cap is enforced by looking one cell **ahead**: a waypoint is emitted when continuing the run *would* exceed the budget, not after it already has. Emitting reactively leaves one leg over budget at the end of every route — including the final hop straight to the destination, which is the worst place for it.

### 6.4 Travel modes

| Mode | Status | Cost model |
|---|---|---|
| Run / walk on foot | working | 16 tiles straight, 24 diagonal, per cell |
| Teleporter (`RES_TELEPORTERS`, walk-on) | represented, 450 nodes | 4 tiles |
| Public moongate | **working live** | 120 tiles |
| Marked rune + Recall | represented; **blocked by character capability** — §10 | — |
| Runebook | **not implemented on this shard** — §10 | — |
| Gate Travel spell | audited, not implemented | — |

The moongate price of 120 tiles is why a bot walks two screens rather than performing a ceremony to save one, and takes the gate for a cross-continent trip. Both behaviours are asserted in the unit tests and both were observed live.

Moongates are only planned through when the caller says the character will use them (`SetUseMoongates` / the `use_moongates` scenario verb), because a gate the shard's worldgen never placed is a route that cannot be executed.

---

## 7. Semantic destinations

```
TravelToPoint(x, y, radius, label)      TravelToLastCorpse()
TravelToPlace("Yew Bank")               ReturnHome()
TravelToRegion("Britain")               TravelToEntity(serial, within)
TravelToService(Service::Banker, "Yew") TravelToResource(ResourceKind::Mining)
```

Each returns false only when the destination cannot be **resolved**. Whether the journey succeeds is a separate question answered later by `TravelBusy()` / `TravelSucceeded()` — the same "the packet was sent" versus "the server did it" distinction M2 established for actions.

Scenario verbs mirror them: `travel_place`, `travel_region`, `travel_service <service> [region]`, `travel_resource`, `travel_entity`, `travel_point`, `travel_corpse`, `travel_home`, `set_home`, `use_moongates`, `wait_travel`, `expect_travel ok|fail`, `ensure_peace`, `expect_peace`, `expect_war`, `expect_region`, `expect_place`, `expect_service_known`.

**Live discovery overrides stored knowledge.** `TravelToService` prefers a provider this character has actually *seen* in the last two minutes over the spawner table. Sightings come from paperdoll titles — the only client-visible carrier of an NPC's trade (M2 finding) — parsed by `NoteServiceFromTitle` into `wm::Service` values, so the live world and the stored world speak one language.

Two selectors were added because two live runs needed them:

* `mobile_trade <text>` binds by the **exact** trade after the last " the " in a paperdoll title. A substring match is not good enough: "Caedmon, the mage guildmaster" contains "the mage", and the guildmaster keeps no shop, so a bot shopping for reagents stands in front of him saying "buy" to no answer.
* `vendor_graphic:<hex>` names the exact item to buy, mirroring the existing `vendor_sell_graphic:`.

---

## 8. Behaviour policies

### 8.1 Running

`Gait::Auto` remains the session default and resolves to Run. Nothing in the travel layer pins a gait; the run bit and the 200 ms cadence are still resolved inside `SubmitStep`, so cadence and wire form cannot disagree. Scenario 4 ran two full Yew round-trips — ten legs — with every leg logged `[bot] running from …` and **zero `0x21` rejects**. Across every M2.5 live run in this document, total move rejects: **0**.

### 8.2 War and peace

War mode is the server's state, learned from `0x72` and changed by sending `0x72`. `travel::WarModeWatchdog` decides only *when to ask* to sheathe:

* no target and a peaceful intent (travelling, banking, shopping) → drop it;
* the target has not been seen for `targetLostMs` (8 s) → drop it;
* no combat event for `idleTimeoutMs` (15 s) → drop it;
* one request, then wait ~3 s for the server's answer — Sphere punishes retry storms (M2).

Two bugs the live runs found, both now regression-tested:

* **`ActionWarMode` bypassed the watchdog.** `war on` went straight to `SetWarMode`, leaving a stale peaceful intent standing, and the watchdog dutifully sheathed the weapon **one millisecond** after the caller drew it. There is now one path in and out.
* **Zero-as-unset.** The watchdog used `0` to mean "no combat yet", which silently disables the idle timeout for the opening moments of a session — exactly when a bot is most likely to be handed a fight. Timestamps now carry explicit `has` flags.

### 8.3 The recovery ladder

Every counter has a ceiling and every ceiling ends in a clean failure:

1. re-run the tile A* for the same leg (a transient blocker moved) — 2 retries;
2. rule the failed macro cell out and replan the world route — 4 plans, avoid-list capped at 32 cells;
3. fail cleanly.

A **transit** leg retries as a transit, never as a walk. Degrading it to a walk makes the bot "arrive" instantly at the tile it is already standing on and advance past the hop it never took — which is how one failed gate turned into a nine-minute walk across the continent.

A leg whose target is more than 160 tiles from the character is refused outright as a stale plan. That is the safety net which caught the above the second time it happened, before the tile A* spent nine seconds finding a way to walk it.

### 8.4 Stuck and oscillation

Position is sampled every 500 ms. Twelve samples with no improvement toward the leg target (six seconds of standing still) trips recovery. Separately, a tile that recurs inside a six-sample window three times counts as oscillation — the two-cells-that-reroute-into-each-other shuffle, which a pure distance check misses because the distance is identical every other sample.

### 8.5 World transitions

A position jump of ≥ 24 tiles between two samples is the server moving us, not walking. If it lands on the goal, the trip is finished; if we were at a transit, the route continues from the far side; otherwise the local plan is thrown away and replanned from where we now are. Observed live on the moongate hop: `0x20 player @(1336,1997,5)` → `0x20 player @(771,752,5)`, route continued without a hitch.

### 8.6 Floors

Britannia's shops are two and three storeys, and "arrived" measured in two dimensions is not arrival: the bot reached (1488,1548,**70**) while the Britain mage stood on the ground floor at z 30, one tile away in x/y and out of earshot in the way that matters. Sphere's speech and shop-keyword checks are three-dimensional.

The final leg now pins the destination's floor (`ActionGoto(x, y, hasZ, z)` → the existing `BotStartGoto` goal-z), taken from the mobile's own z when chasing an entity and from the spawner row's z when going to a place. Intermediate legs stay floor-free — a goal z applied a hundred tiles out drags the whole route toward it. `TravelFinish` refuses to report success from the wrong storey:

```
[travel] reached (1488,1548,70) but the target is on another floor; not arrived
```

---

## 9. Moongates

**Findings, from the shard's own data:**

1. **Two moongate systems exist; only one is real here.** Source-X has a hardcoded `IT_MOONGATE` (type 39) driven by the `[MoonGates]` resource list in `maps/map0/map0_moongates.scp`, with moon-phase destination rotation (`src/game/chars/CCharUse.cpp:217-240`). **No `t_moongate` itemdef exists anywhere in Scripts-X.** What the shard actually ships is `core/dialogs/d_moongates.scp` — "OSI style moongate system by Soulless" — an `i_moongate` itemdef of `TYPE=t_script` displaying `id=i_moongate_blue` (0x0F6C).
2. **The destinations are a flat list, not a rotation.** `functions/worldgen/decoration/moongates.scp` defines `moongates_facet0_0..9` — Moonglow, Britain, Jhelom, Yew, New Magincia, Minoc, Trinsic, Skara Brae, Buccaneer's Den, Ocllo — and only facet 0 is `_active`. Every gate offers every destination in one hop, so the atlas models the network as a **full mesh: 10 gates, 90 ordered hops**, each carrying the destination name exactly as the gump prints it.
3. **Interaction is a gump, not a walk-on.** `on=@DClick` (refused beyond distance 2) and `on=@step` both `sdialog d_moongates`. Sphere sends that as packet **0xB0**; the answer is **0xB1**.
4. **Restrictions:** none in the dialog beyond the distance-2 check. Region flags still apply to *magical* travel (`REGION_ANTIMAGIC_*`, modelled in the atlas), but the gate script itself performs a direct `src.p=` move and does not consult them. No criminal/murderer check, no pet handling. The script sets `statf_hidden` on arrival.
5. **The gates were not in the world.** `place_moongates` is defined but called by nothing — not by `spawn_*`, not by any worldgen entry point — and the world save held zero `i_moongate` items.

**World preparation (operator action, not gameplay).** The ten Felucca gates were placed through the shard's own console, the same route M2 used for worldgen, at the coordinates from the shard's own `moongates.scp`:

```
serv.newitem i_moongate
new.p=<x>,<y>,<z>,0
new.attr=010
new.update
```

(`place_moongates` itself was tried first and created nothing when run from the console — its `for`/`def0` indirection does not resolve without a `src` — so the gates were created explicitly instead. No script, rule or definition was modified.)

**Client work.** Generic gumps were previously logged and ignored. `Client::OnGenericGump` now parses 0xB0 properly: Sphere writes the layout as `{control}{control}…` plus a separate UTF-16BE text table, where `dtext` becomes a `text` control holding an *index* into that table. Radios and buttons are therefore paired with the label that follows them, which is what lets the bot read a destination list it has never seen before:

```
[gump] 0x400028D6 context=0x80A003EB: 13 option(s)
[gump]   button 1000 = 'OKAY'
[gump]   choice 2 = 'Britain'
[gump]   choice 4 = 'Yew'
...
```

The affirmative button and the destination are both matched **by label**, not by hard-coded id: 1000 belongs to `d_moongates`, not to us.

**Two bugs the live runs found:**

* The gate's `@step` trigger opens the gump while the *approach* leg is still running, and Sphere will not open a second one for the same context — so the double-click that followed was answered with silence and the trip stalled. The client now answers a gump that is already open, and arms the answer from the *next* leg as well as the current one.
* A route whose **first** leg is the transit — what the planner emits when the bot is already standing on the gate — started in the Walking phase, "arrived" at the tile it was on, and advanced straight past the hop. `SetRoute` and `Advance` now share one `EnterLegPhase`.

**Live result** (`m25_moongate_back`, tag `m25_s3e`): standing on the Britain gate, asked for "a banker, in Yew".

```
[travel] plan Yew banker: ok legs=6 ~344 tiles transit=1 nodes=140
[travel] using moongate 0x400028D6 for 'Yew'
[travel] gate gump: choosing 'Yew' (choice 4, button 1000)
[0x20] player @(1336,1997,5)  ->  [0x20] player @(771,752,5)
[travel] Yew banker ARRIVED at (647,822,0)
```

80 seconds, for a journey that is about seven minutes on foot. Zero move rejects.

---

## 10. Runes, Mark, Recall, runebooks

Audited in full; **Mark and Recall cannot be demonstrated by any character this project can currently create**, and the reason is a game rule, not an architecture gap. Nothing was faked and no skill, item or coordinate was injected.

### The rune

`i_rune_marker`, graphic **0x1F14** (dupes 0x1F15–17), `TYPE=t_rune` = `IT_RUNE` (`runtime/scripts/items/i_magic_magery.scp:154`). Created blank — `MOREP=-1,-1` — with `MORE1=10` charges. Sold by `VENDOR_S_MAGE_SHOP` (`templates/tm_vend.scp:632`) at 2–10 gold.

### Mark

`[SPELL 45]`, `SKILLREQ MAGERY 60.0`, mana 20, `spellflag_targ_item`. The effect is in `src/game/items/CItem.cpp:5721-5745`: the item must be `IT_RUNE` or `IT_TELEPAD` **and on the caster's person**, then `m_itRune.m_ptMark` is set to the caster's position and the rune is renamed to the region.

Why no character we can create casts it:

* character creation clamps any one skill to 50 and the sum to 100 (`CChar::InitPlayer`);
* the newbie spellbook holds `MORE1=0x382A8C38` — three spells each in circles 1–4, and **not Recall** (bit 31 is clear) — with circles 5–8 empty, so no Mark;
* **no vendor on this shard sells a 6th-circle scroll.** `random_sixth_circle` (which holds `i_scroll_mark`) appears only in monster loot, dungeon chests, and Inscription crafting at `Inscription 60.0, Magery 50.0`;
* casting from a scroll bypasses the spellbook and the skill *requirement* entirely (`CCharSpell.cpp:2452-2500`) and halves the difficulty (`:3556-3560`) — but there is no scroll to buy.

So the honest route to Mark is real skill training to Magery 60, or PvM/crafting for a scroll. Both are M3 work.

### Recall

`[SPELL 32]`, `SKILLREQ MAGERY 40.0`, mana 11, `spellflag_targ_item`. `CChar::Spell_Recall` (`CCharSpell.cpp:388-435`) requires an `IT_RUNE`/`IT_TELEPAD` with a **valid** `m_itRune.m_ptMark` and `m_itRune.m_Strength > 0`; a blank rune answers `DEFMSG_SPELL_RECALL_BLANK`, and each use decrements the charge. Recall scrolls *are* buyable — `i_scroll_recall` is 4th circle and the mage shop sells `random_fourth_circle` — but a Recall without a marked rune is a Recall to nowhere, so Recall is blocked behind Mark.

Region flags are respected by the shard (`src/game/CRegion.cpp:730-750`) and are modelled in the atlas: `Atlas::AllowsRecallInto/OutOf/GateAt` read `REGION_ANTIMAGIC_RECALL_IN/OUT`, `_GATE` and `_ALL` straight from the AREADEFs, and are unit-tested against a dungeon that bans them.

### Runebooks

**Not implemented on this shard.** `i_spellbook_runebook` (0x22C5) carries `TYPE=t_normal //fixme: or t_runebook` (`items/i_unsorted.scp:2007`), and **Source-X has no `IT_RUNEBOOK` type at all**. It is a craftable, decorative item with no travel function. `DetectRunebook/InspectEntries/UseEntry/RecallToEntry` were deliberately **not** written against a system that does not exist.

### What was attempted live

`m25_rune.txt` asks for "a mage" from Yew — and the nearest one the shard spawns is in **Britain**, which the travel layer worked out by itself. The mage was found by paperdoll trade and approached. The blank-rune purchase did not complete, and the blocker was not the rune system: it was the multi-storey approach problem in §8.6, which the run surfaced and which is now fixed for the approach path but not re-proven end to end. Recorded as-is rather than papered over.

`PersonalKnowledge` carries the rune model (`KnownRune` with `marked` / `destinationKnown` set only from what the server says) and `BestRuneFor(x, y, maxDist)` for the planner. A rune's destination is the shard's to know; inventing one would be exactly the synthetic travel this milestone forbids.

---

## 11. Personal knowledge and session isolation

`travel::PersonalKnowledge` is a plain `Client` member — never static, never shared:

| | |
|---|---|
| Visits | which places this character has actually stood in, and when |
| Service sightings | serial, paperdoll title, position, timestamp; trusted for 2 minutes |
| Runes | server-reported state only |
| Home | set explicitly (`set_home`) |
| Death | where and when it died, the corpse serial once seen, recovery attempts |
| Danger | coarse `Unknown` / `Normal` / `RecentlyDangerous`, radius-bounded, expiring |

The shared half — atlas, navgrid, route planner — is immutable and loaded once per process (`world_atlas::AcquireSharedWorld`). Per-trip mutable state (the route, the leg cursor, the avoid list, stuck counters) lives in the per-session `travel::Journey`.

`Danger::Unknown` is deliberately distinct from `Normal`: "nothing bad has happened here" is not "this is safe", and the difference matters to a planner weighing an unknown route.

Threat is never inferred from what a creature *is*. M2 found wildlife that chases and never attacks; the war-mode watchdog is driven only by accepted attacks, observed swings and health changes.

Isolation is unit-tested directly (`TestKnowledgeIsolation`) and was exercised live by the two-session obstacle run.

---

## 12. Corpse location

Death now records where it happened, from the server's own body change:

```
[STATE] dead (body 0x0192)
event death_location: at=(689,753,0) region=a_Yew_Ter
```

A 2.0.x client is never told which corpse is its own — `0xAF` explicitly excludes the dying client (M2 finding 3) — so identification is circumstantial and says so in the code: a corpse graphic 0x2006 that appears within three tiles of where we just died, within a minute, before we have claimed one. The corpse's own tile then wins over the death tile, because the server decides where the body lands.

`TravelToLastCorpse()` and the `travel_corpse` verb navigate to it, and `recoveryAttempts` is counted so a future policy can bound the die → resurrect → corpse-run → die loop. A journey that *begins* dead is allowed (walking a ghost to its corpse or to a healer is what a player does); a journey that is *interrupted* by death or resurrection is aborted, because the plan belonged to a different character state.

The decision policy — is the corpse worth it, is the area still dangerous, re-equip first, ask for help — is explicitly M3.

---

## 13. Live scenarios

All against Source-X on `127.0.0.1:2593`, ordinary player accounts, no GM commands. Logs in `local/dev/<tag>.{log,console.txt,err.txt}`.

| # | Scenario | Tag | Result | Evidence |
|---|---|---|---|---|
| 1 | Semantic local service | `m25_final_s1` | **PASS** | `travel_service banker Yew` → 2 legs, ~88 tiles, 19 macro nodes → arrived (647,820); `Basia, the banker` identified by paperdoll; `expect_place` / `expect_region` / `expect_service_known` all pass |
| 2 | Cross-region Yew → Britain | `m25_s2c` | **PASS** | 31 legs, ~1160 tiles, 1139 macro nodes; every leg "ARRIVED (off by 0)"; arrived `a_townBritain` |
| 2b | Cross-region Britain → Yew | `m25_s2back` | **PASS** | 42 legs, 0 rejects, 0 stopped-short legs, arrived `a_townYew` |
| 3 | Moongate travel | `m25_s3e` | **PASS** | §9 — gump read, destination chosen by label, server transition observed, journey continued |
| 4 | Run policy | `m25_s4` | **PASS** | 10 legs across two round-trips, every one `[bot] running from …`, **0** `0x21` rejects |
| 5 | War-mode cleanup | `m25_final_s5` | **PASS** | peace → `war on` → server confirms war → travel begins → watchdog drops it → server confirms peace |
| 6 | Dynamic obstacle (2 sessions) | `m25_s6b` | **PASS** | §13.1 |
| 7 | Stuck / unreachable | `m25_s7` | **PASS** | open water: planner expanded 23,708 macro nodes, concluded "no world route", failed in 7 ms; the next trip succeeded |
| 8 | Semantic entity approach | `m25_s8` | **PASS** | `travel_entity` at a banker inside the bank building: 6 stopped-short legs, 3 replans, arrived (652,818) within radius 2 |
| 9 | Recall | — | **BLOCKED** | §10 — no character can obtain a marked rune |
| 10 | Mark | — | **BLOCKED** | §10 — Magery 60 required; creation caps at 50; no 6th-circle scroll for sale |

Regression, same build: `m15_nav` clean (0 rejects), `m2_bank` **passed on the second run** — the first timed out because the banker had wandered out of the 3-tile speech range of the hard-coded `goto 651 822`. That is not a regression; it is the exact fragility this milestone replaces, and Scenario 1 walks to the same banker by *need* and finds it every time.

### 13.1 The obstacle run, and the rendezvous debt

M2 left two sessions synchronised by `sleep`, and lost four runs to the race. `local/dev/run_m25_pair.ps1` now starts the traveller only after the blocker's **own log** reports `event travel_done` — observable state, one event, not a synchronisation framework.

The blocker parked beside the single tile leading into a one-tile alcove in the Empath Abbey (chosen from the walkability dump, not by eye). The traveller walked in, and then could not get out:

```
plan Yew healer: ok legs=4 ...          (3 leg attempts, all "stopped short")
plan Yew healer: ok legs=5 ...          (3 more)
plan Yew healer: ok legs=5 ...          (3 more)
plan Yew healer: ok legs=5 ...          (3 more)
[travel] Yew healer FAILED at (622,830,0) -- tile route stopped short
[travel] scenario point ARRIVED at (622,830,0)     <- session still healthy
```

**4 world plans, 3 leg retries each, done in 14 seconds, 0 move rejects, no oscillation, no packet spam** — and the very next destination was accepted and reached. That is the requirement: not that the bot wins, but that losing is bounded and leaves the session usable.

---

## 14. Tests

`ctest`: **3/3 suites, 0 failures** — `sphere_regression` (50), `m2_actions`, and the new `m25_world` (**139 checks**).

`tests/m25_world.cpp` links `uo_world` plus the travel state machines — the same objects the client links — and needs no server, no MULs and no generated atlas: the world under test is synthesised inline. It covers atlas parsing and the smallest-region rule; name/id/substring lookup widening; service, resource and region-scoped queries; recall/gate legality from region flags; short-hop and cross-world routing; the leg budget; gate-versus-walk cost; avoidance and the impossible route; journey sequencing; no-route failure; the full recovery ladder; stuck detection with progress resetting the counter; oscillation; world transitions; transit legs including a route that *starts* on one; the war-mode watchdog including both bugs from §8.2; personal knowledge; and multi-session isolation.

Three of those checks exist because a live run failed first. That is the intended relationship between the two layers.

---

## 15. `uo-offline` travel audit

`reference/uo-offline` (ModernUO + server-side PlayerBots). Classified as required:

| Idea | Verdict | Why |
|---|---|---|
| Destination catalog with type, city and multiple **arrival** tiles | **Portable data** — adopted in spirit | `Place` carries a category, an owning region and an interaction radius; arrival is a radius, not a tile list |
| HPA*-style hierarchical graph | **Portable algorithm** — adopted, differently | We derive cells from the MULs instead of curating a graph |
| Hand-curated waypoint graph | **Rejected** | Their own notes record 541 duplicate/stacked waypoints and 176 disconnected components needing a cleanup pass. Derived data does not rot that way, and the brief rules it out |
| Precomputed distance fields toward common destinations | **Portable, deferred** | A genuinely good idea for many bots sharing one immutable world; noted as future work |
| "Recall is the transport"; scroll supply scales with wealth; newbies walk | **Inspiration — adopted** | Matches what the shard's own economy forces (§10) and informs the travel-mode cost model |
| Stuck-bot rescue by teleport, fleet-level rerouting of bad road sections | **Rejected** (rescue) / **adapted** (rerouting) | Teleporting a stuck bot is server-side manipulation. Per-trip avoid cells are the legitimate half |
| Moongate "young player" bug — gate menu never opened for normal characters | **Inspiration** | A useful warning: verify the gump actually opens for the character class you are testing. Ours does |
| Ferries removed as non-T2A | **Inspiration** | Revolution fidelity is decided from Revolution data, not from their era call |

Profession, economy and social reuse remains for M3+.

---

## 16. Performance

| | |
|---|---|
| Atlas + navgrid generation | ~16 s (sampling) + ~64 s (crossing measurement), offline, once |
| Navgrid | 114,688 cells, 6 bytes each, 688 KB on disk |
| Atlas | 790 regions / 707 places / 540 transits, 224 KB of text, parsed once per process |
| Cross-world plan (Yew → Britain) | ~1,100–1,140 cell nodes, ~1 ms |
| Short-hop plan | 0 nodes — under one leg length it skips the macro search entirely |
| Tile A* per leg | 5–141 steps, 0.1–7 ms |
| Shared per process | atlas, navgrid, route planner (immutable) |
| Per session | journey, knowledge, avoid list, war state |

Route planning runs synchronously on the session tick. At ~1 ms for a cross-continent plan inside a 50 ms tick budget that is comfortable; if a future fleet makes it visible, the planner is already stateless and const and can move to the existing worker-thread pattern.

---

## 17. Technical debt

> **Triaged at the start of M3.** Every item below was classified BLOCKS_M3 /
> FIX_DURING_M3 / SAFE_TO_DEFER with reasons in
> `docs/M3_PROGRESSION_ECONOMY.md` §2. Items **5** and **8** were blockers and
> were fixed, proven live and committed (`512d5dc`) before any progression work
> began; item **4** is partly mitigated; the rest are deferred with reasons.

1. **Cell crossings are symmetric.** `BuildEdges` records one bit per direction pair. A genuine one-way drop is therefore modelled as two-way; the journey recovers, but the route is briefly wrong.
2. **The navgrid regenerates wholesale.** No incremental update when client data changes.
3. **Route planning is synchronous.** See §16.
4. **`Journey` is 2-D.** Floors are handled at the client edge (§8.6) rather than in the plan. A building whose storeys need different macro routes is not modelled.
5. **The blank-rune purchase is unproven end to end** (§10), and the Britain mage shop's upper storey is a place the tile A* can climb into and not reliably back out of.
6. **`expect_region` is containment-only.** Correct, but it means "am I in Yew?" is false while standing on the Yew moongate, which sits in its own AREADEF outside the town. Scenario 3b asserts the gate instead.
7. **Danger is unused by the planner.** It is recorded and queryable; no route cost consumes it yet.
8. **Teleporters are represented but not proven live.** 450 nodes are in the graph and the walk-on model is implemented; no scenario has crossed one yet.
9. **Trammel (map 1) remains enabled and unexamined** (§2).
10. **One action in flight per session** (M2 debt, unchanged).

---

## 18. Deferred to M3+

Profession AI, skill training, mining/fishing/smithing loops, PvP and PvM combat AI, player-to-player economy and secure trade, autonomous runebook management, guilds, parties, reputation, personality, population scheduling, and the full corpse-recovery decision policy. M2.5 provides the world and travel foundations those depend on and nothing above them.
