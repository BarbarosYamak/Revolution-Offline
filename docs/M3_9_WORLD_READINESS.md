# M3.9 — World Population, Dynamic Occupancy & Service Resolution

Date: 2026-08-27. **STATUS: IN PROGRESS.**

M3.8 closed most of the pre-M4 foundation and exposed the last set of problems
that would break or distort autonomous player life. **M4 is NOT STARTED.**

---

## 1. Baseline — measured, not assumed

| Repo | HEAD | Branch | Dirty |
|---|---|---|---|
| `bot/uo-client` | `d1242f8` | `revolution-sphere-m1` | 0 |
| `runtime` | `083ef1b` | `revolution-runtime` | 0 |
| `runtime/scripts` | `be26bab` | `revolution-runtime` | 0 |
| `server/Source-X` | `dd4183dd` | `master` | 0 source mods |
| `server/Scripts-X` | `27e78bc` | `main` | 0 |

Tests at baseline — **9/9 suites, 0 failures**: `m37_economy` 99 checks,
`m38_closure` 222 checks, `viewer_safety` 31 checks, the rest OK.
(`path_probe` is a manual probe wanting an external tiledata path and is not
registered with CTest.)

Data exports present: 4 TSVs, 251 rows.

### M3.8 facts that must remain true

Decorator and crop generator are now required world initialization: 107 forges,
54 anvils, 20 spinning wheels, 33 looms, 3,104 crops. Textile passes both sides
— wool → yarn → bolt → cloth → hat, and cotton → thread → stitched shirt
(`0x0DF9 0→2`, `0x0FA0 0→6`). Nails restored to the tinker menu (Parts 3 → 4).
`ReagentsRequired=1`. Taming gated on **both** Animal Taming and Animal Lore at
Revolution's thresholds. Mounted travel ~1.82×.

---

## 2. Debt register

* **BLOCKS_M4** — no autonomous lifecycle can start until closed
* **BLOCKS_ARCHETYPE** — blocks one kind of character
* **SAFE_TO_DEFER** — real debt, does not block M4

### BLOCKS_M4

| # | Debt | Why | Phase |
|---|---|---|---|
| 1 | **Stale mobile occupancy.** `IsMobileBlocking` has no expiry. M3.8 added a single soft-retry when *enclosed*, which unsticks the trapped case but leaves the underlying model wrong: a mobile seen once can wall a tile forever. Dangerous in banks, shops, doorways, crowded cities and future PvM. | A bot that poisons its own nav state cannot run unattended | 1 |
| 2 | **Service travel resolves professions, not suppliers.** `travel_service tinker` → *"Justine, the engineer guildmistress"*, who keeps no shop. Third occurrence: blank runes (M3.6), mage shop (M3.7), tinker tools (M3.8). | Autonomous acquisition silently "succeeds" at the wrong entity | 3 |
| 3 | **No supplier knowledge freshness.** Nothing ages or invalidates "this vendor sells X". | A bot revisits a vendor that no longer stocks the item, forever | 4 |
| 4 | **The world is effectively monsterless.** M3.8 placed 14 dungeon spawn groups (spawners 1,914 → 3,033, 356 hostile spawn ids) but density and composition are **stock Nerun rates, not Revolution's**, and no PvM has been proven live. | PvM, treasure and every combat archetype | 5–9 |
| 5 | **Craft-menu oracle not generalized.** Used correctly per scenario; not a rule. | An autonomous crafter can still attempt a recipe the server never offered | 13 |
| 6 | **No multi-bot soak.** Several 2–3 bot runs completed cleanly; no sustained run. | Unknown behaviour under concurrency | 14 |

### BLOCKS_ARCHETYPE

| # | Debt | Archetype | Phase |
|---|---|---|---|
| 7 | **Runebook Recall executes only from scenarios.** M3.8 wired *selection* (`travelmode::Choose` now runs per journey and picked a moongate unprompted); execution is gump-driven and not a client action. | Mage travel | 2 |
| 8 | **Reagent SOURCING.** Consumption resolved (`ReagentsRequired=1`, 14.05.2009 update). Owner testimony says mage shops and alchemists sold regs, and the eight ordinary reagents are now `RevolutionNpcVerified` — but no *dated archive* source corroborates it. | Mage/Warlock resupply | 12 |
| 9 | **No pet semantics.** Animals are modelled as mounts only. Revolution supports taming, commanding and combat pets (Taming / Lore / Veterinary). | Tamer | 10–11 |
| 10 | **Legitimate Runebook crafting unproven.** | Mage self-sufficiency | defer |

### SAFE_TO_DEFER

| # | Debt | Note |
|---|---|---|
| 11 | Special robes (Hardening Crystal + elemental crystal) | Historically important. Do not substitute TNS recipes. |
| 12 | Special leather / Spirit of Nitre | |
| 13 | Fishing nets / S.O.S. | |
| 14 | Treasure system | Depends on #4 |
| 15 | Head Hunters / murderer system | Needs PvP population |
| 16 | Housing AI, guild AI, player-vendor automation | |
| 17 | Anti-macro adaptation | Spec exists, no behaviour |
| 18 | Hides/leather and cooking chains unproven live | Inputs exist |
| 19 | Post-era mount data (52 of 62 lack AnimID) | Our viewer is safe |
| 20 | Skill cap and Resist client-enforced only | Server-side conflict |
| 21 | Engine-era divergence (Source-X vs era Sphere) | Raised, not investigated |
| 22 | One action in flight; `Journey` is 2-D | From M3 |

### Closed since the brief was written

**Stat cap is no longer UNKNOWN.** The brief lists it as outstanding; it was
resolved at the end of M3.8 as **225 total / 100 per stat**, derived from ten
player builds across two unrelated threads and classes (warlock 54877, thief
26120), every one summing to exactly 225. `DERIVED`, not quoted — nobody states
what everyone knows, which is why three milestones of searching for a
*statement* found nothing. The runtime allows 300, so it is enforced bot-side.

That correction matters beyond the number: the profile had recorded *"Build
threads discuss skills only and never post stats"*, and M3.7 had recorded
*"forum search requires a login"*. **Both were wrong.** The forum is publicly
readable — 406,665 messages, 22,870 topics. Two milestones of UNKNOWNs rested on
a search that stopped too early.

---

*Sections 3 onward are written as each phase completes.*


---

## 3. Phases 5-6 — world population forensics

Full findings in `REVOLUTION_WORLD_POPULATION.md`. What matters here:

### 3.1 A mistake M3.8 made, found by auditing afterwards

M3.8 ran `spawn_dungeons_felucca` and reported it as **"fourteen dungeons"**.
It calls **eighteen** functions — the list was read from a truncated terminal
output and the last four were never seen. Four are **Mondain's Legacy (2005)**
content: Blighted Grove, Painted Caves, Palace of Paroxysmus, Prism of Light.

Eleven spawners of unrenderable ML creatures are now live:

| SPAWNID | count |
|---|---:|
| `c_slime_corrosive` | 6 |
| `c_monstrous_interred_grizzle` | 3 |
| `c_abscess` | 1 |
| `c_hydra_crystal` | 1 |

This is not cosmetic. M3.6 established that post-era bodies with no AnimID in
Revolution's tiledata **crash third-party clients** — a unicorn mount took down
ClassicUO on every launch.

It also inverted the order the brief specifies: audit first, populate
selectively, never run "all spawns" as one irreversible operation. M3.8
populated first.

**Remediation is written and committed** — `f_m39_prune_ml` names creatures
rather than coordinates, so it is correct wherever they landed and safe to run
twice, and it calls `SPAWNKILL` before `REMOVE` so no orphaned ML creature is
left wandering with nothing to clean it up.

**APPLIED.** The shard was restarted (world backed up first; `Startup complete
(items=47248, chars=1595, accounts=15)`) and the prune run:

| | before | after |
|---|---:|---:|
| ML spawners (`abscess`, `hydra_crystal`, `interred_grizzle`, `slime_corrosive`) | 9 | **0** |
| total spawners | 3,033 | **3,024** |
| hostile spawn ids | 359 | **350** |

Exactly nine removed, and the classic dungeons are untouched — orc 10, zombie 24,
lich 12, earth elemental 24, skeleton knight 14, balron 17.

Two things had to be corrected on the way, both worth keeping:

* **`SPAWNKILL` does not exist.** These are SCRIPT spawners
  (`EVENTS=t_custom_spawner_char`), not the engine's `IT_SPAWN_CHAR`. Checking
  first also showed **zero living ML creatures** — every spawner was still on
  its first timer, so `REMOVE` alone strands nothing.
* **The run reported "removed 00" while deleting nine.** Inside `FORITEMS` the
  default object is the ITEM, so `LOCAL.removed` addressed the item's own
  variable and died with it. The count is verified by SPAWNID diff in the world
  save, not by the loop's own report — a counter that cannot count is worse than
  no counter, so it was removed rather than fixed.

### 3.2 Evidence found — the changelog is the real archive

The dated official changelog is far richer than earlier milestones assumed:

* **Dragon/Balron loot buffs**, 14.05.2009
* **Custom Sphere creatures**: Infernal, Ice Dragon, Energy Vortex
* **Özel deri** (special leather) ×3 from Dragon / Balron / Infernal corpses,
  Feb–Mar 2009 — this is the special-leather chain, previously deferred as
  unknown
* **Magic weapon loot split** 25/25/25/12.5/12.5 by weapon school, 12.04.2009
* **Champion system**: Barracoon in Despise activated **19.02.2009** — in-window,
  HIGH confidence. Neira/Deceit and Semidar/Fire are documented but dated 2016,
  so their in-window status is **UNKNOWN**
* **Treasure**: map levels 2–5 from S.O.S. bottles, Lockpicking 40/60/80/100,
  progressively stronger guardians, poisoned chests
* **World is Sosaria + Ilshenar only** — Revolution had no Trammel
  * **CORRECTED in §4.** The client data disproves the Ilshenar half.

### 3.3 What must never be run

| Function / set | Why |
|---|---|
| `spawn_quest_npcs_felucca` | 327 ML Heartwood elf NPCs + a necromancer trainer. Client ships skills 0–48 and cannot render elves |
| all of `trammel/` | **Revolution had no second facet** |
| `malas/`, `tokuno/`, `termur/` | AoS / SE / SA |
| `spawn_twisted_weald_ilshenar` | ML |
| `spawn_uoclassic` on top of the Nerun set | 1,770 duplicate spawners |
| Sea Market / Heartwood blocks | inside otherwise-fine Vendors/TownsLife functions |

### 3.4 Density stays UNKNOWN

The twelve classic dungeons now live are **generic Sphere, not
Revolution-verified**. The runtime's 2–10 minute respawn values are **Nerun's
Distro defaults**, not Revolution's, and no archive source gives per-dungeon
composition for 2008–2010 — only Despise is directly evidenced, via Barracoon.

Recorded as debt rather than guessed. Classic Felucca dungeons in 2009 are a
defensible base; an empty PvM world is definitely wrong; and inventing a density
number would be worse than admitting we lack one.

---

## 4. Phases 5–9 — the world is populated

The shard owner's instruction was direct: *"we should spawn all of the places
graveyards, destard, ice area, deceit etc whole world"*. This section records
what "whole world" turned out to mean, because the answer is narrower than it
sounds and the evidence for that is unusually clean.

### 4.1 Ilshenar does not exist on this shard — correcting §3.2

§3.2 recorded *"World is Sosaria + Ilshenar only"*. The Ilshenar half is **wrong**,
and the client data settles it without needing a forum search:

```
local/revolution-client/  →  map0.mul   (and nothing else)
runtime/mul/              →  map0.mul   (and nothing else)
```

The real Revolution client ships **one map file**. `sphere.ini` declares Map0–Map5,
but `map1.mul` … `map5.mul` are simply absent, so Ilshenar (map2) cannot load at
all. The five `spawn_*_ilshenar` sets are therefore unrunnable, not merely
undesirable.

`map0.mul` is 89,915,392 bytes, which is exactly 7168 × 4096 at ML block size
(896 × 512 blocks × 196 bytes). That is the *expanded* map0 — Britannia **plus**
the Lost Lands block at x > 5000. So on this shard "the whole world" means map0,
and the Lost Lands are part of it rather than an addition to it.

### 4.2 What was already live, and what was missing

Facet 0 has nine sets. Vendors, Towns Life, Wild Life and Dungeons went in during
earlier milestones. **Five had never been run**, which is why the world felt
empty in exactly the places a player would notice first.

The clearest symptom: the Britain→Yew corridor — one of the most travelled roads
in the game — contained *nothing but fauna and wandering healers*. No orcs, no
brigands, no humanoids of any kind. The shard owner independently remembered
humanoids on that road (*"on the way from brit to yew … I remember humanoids"*),
which is precisely what the missing Outdoors set places there: **37 orcs, 34
trolls, 27 lizardmen, 21 ogres, 21 ettins, 20 ratmen, 19 harpies**, an orc camp
and an orc captain. Memory and data agreed.

A second symptom: the entire world held **one** undead — a single mummy. The
graveyards had never been spawned.

### 4.3 The audit came first this time

M3.8 ran an aggregate spawn function without reading it and placed eleven
unrenderable ML spawners that had to be pruned afterwards. So every creature
token in all five remaining sets was resolved mechanically —
`spawner_defs.scp` → CHARDEF → the file that defines it — and anything defined in
`aos/se/ml/sa/hs/tol` was flagged. **787 spawners audited; exactly two bad
creatures found**, both in Outdoors:

| token | CHARDEF | defined in |
|---|---|---|
| `changeling` | `c_changeling` | `npcs/c_monster_ml.scp` |
| `plaguespawn` | `c_plague_spawn` | `npcs/c_monster_ml.scp` |

`plaguebeast` was *kept*: despite the near-identical name it resolves to
`c_plague_beast` in `c_monster_lbr.scp` and is era-legal. The two were checked
separately rather than assumed to share an era.

Two things nearly made that audit miss real content, both worth keeping:

* **The mapping table is inconsistently cased** — `{C_skeleton_knight}` sits
  beside `{c_ghoul}`. A case-sensitive lookup silently dropped rows.
* **Several creatures are reached through `defname2=` aliases.** `GreatHart`
  maps to `c_stag`, which is a *second* name for `c_great_hart`. The first pass
  reported five era-legal animals as MISSING — and the world save disproved it
  instantly, because it already held 62 great harts. A classifier that
  contradicts the running world is wrong about the classifier, not the world.

### 4.4 What was run

| Set | Function | Note |
|---|---|---|
| Graveyards | `f_m39_graveyards` | 7 spawners. **Not** the stock function, which also places one at "Haven" — a Trammel settlement whose Felucca coordinates land on Ocllo island |
| Outdoors | `f_m39_outdoors` | 445 spawners, stock list minus the two ML tokens, nothing else changed |
| Sea Life | stock | audited CLEAN |
| Reagents | stock | 60 item spawners — load-bearing, since `ReagentsRequired=1` |
| Lost Lands | stock | T2A (1998), audited CLEAN |

Still deliberately **not** run: `spawn_quest_npcs_felucca` (327 ML Heartwood
elves — the client cannot render elves), everything under `trammel/`,
`malas/ tokuno/ termur/`, and `spawn_uoclassic`.

### 4.5 Verified by world-save diff, not by the function's own report

| | before | after |
|---|---:|---:|
| spawners (`i_worldgem_bit`) | 3,024 | **5,049** |
| distinct SPAWNIDs | 288 | **320** |

+2,025 spawners from 794 script lines. That ratio is correct rather than
alarming: `f_create_spawner` loops `for 0 5` and creates **one gem per non-empty
creature list**, not one per line.

Britain cemetery, confirmed in the save at the exact spawn coordinate:

```
c_zombie @1369,1475 · c_spectre @1369,1475 · c_skeleton_mage @1369,1475
```

Thirty-two creature types entered the world that had never existed on it before,
including the ostards that the mount work in M3.8 assumed were obtainable —
`c_ostard_desert`, `c_ostard_forest`, `c_ostard_frenzied` — plus `c_llama`,
`c_dolphin`, `c_titan`, `c_reaper`, the savage tribe and the two reagent item
spawners.

### 4.6 Pre-existing debt this surfaced

Server startup logs 13 `ITEMDEF has invalid ID=… (value is greater than the
tiledata maximum index)` errors from `spherestatics.scp` and `sphereworld.scp`.
These are **not** new — they are decorator items whose graphics postdate
Revolution's tiledata, the same class of era mismatch as the ML creatures.
Recorded as debt; not addressed in this milestone.

---

## 5. Phases 7 & 9 — PvM, proven the hard way

### 5.1 The world is lethal, and the bot lost

The first cemetery run is the proof Phase 9 asked for, arriving from an
unexpected direction. Within eight seconds of the bot reaching 1369,1475:

```
Zombie: *Zombie is attacking you!*
Spectre: *Spectre is attacking you!*
Skeletal Mage: *Skeletal Mage is attacking you!*
[STATE] dead (0x2C resurrect menu)          <- 27 seconds after arrival
```

Real creatures, real aggression, real damage, real death, and — because death
here is full loot loss — a real consequence. What was **not** proven is the other
half: the bot never killed anything, so corpse and loot remain unproven.

**The cause was the scenario, not the shard.** It slept sixty seconds scanning
for a target before engaging, because that dwell was written when the graveyard
was empty and `SectorSleep=10` meant arriving was what woke the sector. Once the
graveyard was actually populated, sixty stationary seconds among three hostile
undead was a death sentence. The dwell is now one scan and one second.

That is a lesson worth more than the phase: **a scenario tuned against an empty
world can be actively unsafe against a populated one.** Every timing constant
written before this milestone is suspect for the same reason.

### 5.2 A latent client bug that only a lethal world could expose

Twenty-seven seconds after dying, the scenario aborted with:

```
ERROR travel_corpse could not resolve a destination: this character has not died
```

The character had very obviously died. The cause is in `Client.cpp`:
`knowledge_.NoteDeath()` was called **only** from the body-change handler — but
as the code's own comment in `OnResurrectionMenu` states, Source-X sends `0xAF`
only to bystanders and *emits no packet at all* when the ghost body is swapped
in, so `0x2C` is the only notification we ever get. The body-change path
therefore never fires for our own death, `NoteDeath` was never called, and
**`travel_corpse` could never have worked at all.**

Corpse recovery has been dead code since it was written. Nothing caught it
because until M3.9 populated the graveyards, nothing on this shard had ever
killed a bot outside a controlled M2 test.

Fixed by extracting `Client::RecordOwnDeath(const char* how)` and calling it from
**both** death paths. It is idempotent by intent: if both fire, the ghost has not
moved yet, so the second call records the same spot.

## 6. Phase 14 — multi-bot soak, and the ceiling it found

The milestone asked for 5–10 bots. The shard owner asked for 30–50 *"just to
test"*, which is the more useful number: concurrency bugs do not appear at five.
Provisioning is cheap — each `uo_client` is ~25 MB resident, because `map0.mul`
is memory-**mapped** rather than copied, so forty clients share one physical
copy of the 89 MB file.

The first 38-bot launch found a hard ceiling immediately, and it was not ours:

```
ERROR: Blocked connection from '127.0.0.1'
       [IP history: blocked=1, ttl=300, pings=16, connecting=0, connected=16]
ERROR: Outcome (default): requested kick + IP block allowed by script
       'f_onserver_connectreq_ex'
```

**Sphere's own anti-flood limits cap a single-IP fleet at 16 clients.** Fifteen
bots reached the world; the other twenty-three were kicked, and the IP was banned
for 300 seconds — which also killed an unrelated run that happened to be starting.

| setting | was | now | why |
|---|---:|---:|---|
| `ClientMaxIP` | 16 | 1000 | the hard cap that produced `connected=16` |
| `ConnectingMax` | 32 | 1000 | total simultaneous not-in-game connections |
| `ConnectingMaxIp` | 8 | 1000 | the login burst is all one IP |
| `MaxConnectRequestsPerIP` | 50 | 100000 | the gate that *triggers* `f_onserver_connectreq_ex` |

`f_onserver_connectreq_ex` itself has an **empty body** — it is comments only, so
it falls through to the default outcome, "reject and ban for 5 minutes". The
threshold, not the script, is where the behaviour lives.

This is operator configuration on a local offline shard, confirmed by the owner
(*"it will be offline server so there should be no limits"*), and `sphere.ini`
was backed up to `sphere.ini.bak_m39` first. It changes no gameplay rule.

**It is also a genuine M4 planning input**: an autonomous population run from a
single host hits a shard-side connection ceiling long before it hits a hardware
one, and the failure mode is an *IP ban*, not a polite refusal.

---

## 7. Phase 15 — M4 readiness matrix

The question this matrix answers is narrow: **can an autonomous character be left
running unattended without corrupting its own state, losing its equipment to a
bug, or silently doing nothing?** It is not "is the bot good at the game".

Legend: **PROVEN** live on the shard · **BUILT** code exists and unit-tests pass,
no live proof · **PARTIAL** works with a caveat named in the row · **OPEN** not
addressed.

| # | Capability | State | Evidence / what is missing |
|---|---|---|---|
| 1 | Stale mobile occupancy expires | **PROVEN** | Time-based purge (2 s) added to `PurgeOutOfRange`; the stationary case no longer walls a tile forever. Exercised by the soak |
| 2 | Runebook Recall *selection* | **PROVEN** | `travelmode::Choose` runs per journey and picked a moongate unprompted |
| 2b | Runebook Recall *execution* | **OPEN** | Gump-driven, still not a client action. Mage travel falls back to walking/gates |
| 3 | Suppliers resolve to verified stock | **BUILT** | `Supplier::Registry` only ever creates a supplier from a shop list actually read — never from an atlas tag or profession name |
| 4 | Supplier knowledge ages out | **BUILT** | `VerifiedCurrent` 5 min / `Recent` 45 min / `Stale` / `Invalid` after 3 absences |
| 5 | World is populated | **PROVEN** | 3,024 → 5,049 spawners; 320 distinct spawn ids; graveyards, outdoors, sea life, reagents, lost lands |
| 6 | Spawn content is era-legal | **PROVEN** | 787 spawners audited by CHARDEF→file; 2 ML creatures removed; 9 ML spawners pruned earlier |
| 7 | World is hostile to bots | **PROVEN** | Three undead killed a bot in 27 seconds |
| 8 | A bot can kill a creature | **PROVEN** | Timber Wolf `0x0000A01C` killed in a mutual 45 s melee: *“You have gained a bit of fame”*, Swordsmanship +0.1, bot alive and armed afterwards. Looting the corpse remains unproven. See §15 |
| 9 | Death, corpse recovery, resurrection | **PROVEN** | Full loop live: died at the cemetery, `travel_corpse` resolved (`expect travel ok`), a wandering healer named Dale raised the ghost, then walked back to the corpse. See §9 |
| 10 | Pet command semantics | **BUILT** | Command words, `all ` prefix, 14-tile hearing, vet range, danger threshold |
| 11 | Pet safety | **BUILT** | `HealthPercent` returns −1 when unknown, never a false 100 |
| 12 | Reagent consumption | **PROVEN** | `ReagentsRequired=1`, corroborated by the 14.05.2009 changelog |
| 12b | Reagent *sourcing* | **PARTIAL** | 60 reagent spawners now live, so field-gathering is possible. Vendor sourcing rests on owner testimony, no dated archive |
| 13 | Craft menu as execution authority | **BUILT** | `CraftableNow()`, `expect_menu_has/lacks`, `menu_report`; closed menu now aborts instead of reporting "not offered" |
| 14 | Multi-bot concurrency | **FAILS** | 38 bots connect and path, but only 11/38 finished the circuit. 99,290 travel failures against 731 successes, and the failure path spins at ~16 Hz. See §12 |
| 15 | Bots survive unattended | **OPEN** | Depends on 8 and 9 |

### What actually blocks M4

Three things, and only three:

1. **A bot cannot yet win a fight** (#8). Everything about autonomous life —
   income, equipment replacement, risk — assumes combat can be survived. Right
   now the evidence says it cannot.
2. **Corpse recovery is unproven live** (#9). The fix is written and the bug is
   understood, but a bot that dies and cannot recover its equipment is a bot that
   degrades permanently on its first death, and death is now routine.
3. **Recall execution** (#2b) blocks the mage archetype specifically, not M4 as a
   whole.

Everything else in the BLOCKS_M4 list from §2 is either PROVEN or BUILT with
tests. Notably, the two debts that looked worst at the start of the milestone —
stale occupancy and profession-not-supplier resolution — are closed.

### Honest note on what "BUILT" means here

Six rows say BUILT rather than PROVEN. Those are unit-tested (`m38_closure`, 267
checks) but have not been driven end-to-end by a live bot. M3.7 established
exactly why that distinction matters: three separate "failures" that milestone
turned out to be bad assertions rather than shard bugs, and two documented
UNKNOWNs were searches that stopped too early. A passing unit test proves the
model is self-consistent, not that the shard agrees with it.

---

## 8. Phase 2 — Runebook Recall, scoped precisely

This debt is smaller than the register made it sound, and worth restating so it
is not re-investigated from scratch a third time.

**The mechanic is already proven live.** M3.6's `m36_runebook_travel` recalls
from a runebook page with `use @book` followed by `gump_button 11`, and the
server treats it as a real cast: the book surfaces the page's rune to the
backpack and asks the character to cast `[SPELL 32] Recall` at it, so the skill
check, the 11 mana, the fizzle roll and every destination rule still apply.
Nothing is bypassed.

**What is missing is wiring, not mechanics.** `travelmode::Choose` already
selects `Mode::RunebookRecall` when the capability says a page exists, but
`TravelBegin` has no branch that executes it — so a mage that *decides* to recall
still walks. Closing it means one client action: open the book, wait for the
gump, map destination → page index, press that page's travel button, then wait
on the cast the way `cast` already does.

Left OPEN deliberately. It blocks the **mage archetype**, not M4 — every other
archetype travels by foot, mount or moongate, all of which are proven.

---

## 9. Corpse recovery — proven live, end to end

`m39_corpse_proof` deliberately gets the bot killed and then proves it can get
back. RevolutionSpar was sent because it is the M2 sparring fixture and carries
no economy evidence, unlike the miner or the weaver.

```
say   I am here to die on purpose
Zombie: *Zombie is attacking you!*     Spectre: *Spectre is attacking you!*
[STATE] dead
say   I am a ghost
travel_corpse  ->  expect travel ok: ok          <- the line that used to abort
say   my corpse is here
System: Your ghostly hand passes through the object.
Dale: Live again, ghost! Thy time in this world is not yet done.
[STATE] alive (body 0x0190)
travel_corpse  ->  expect travel ok: ok
say   standing over my own corpse, alive
```

Every step is an ordinary player action. **Dale is a wandering NPC healer** —
one of the sixty the world spawns — not a GM resurrection.

One detour worth recording, because it looked like a failure and was not. An
earlier re-test reported `resurrect: invalid_state ... not dead` and then
`travel_corpse: this character has not died`. Both were correct: the shard had
been restarted between the death and the test, so the world rolled back to the
last save, the death was undone, and the character was alive with its gear. The
death record lives in per-run knowledge, so **the death and the corpse run must
happen in the same login** — which is why the proof scenario does both.

That closes matrix row #9 and BLOCKS_M4 debt around unattended survival: a bot
that dies can now find its corpse and be raised.

### Still open: winning

Two cemetery runs, two deaths. `m39_hunt` asks the smallest honest version of
the remaining question — *can a bot kill anything at all?* — by sending the
now-unequipped Spar against a rabbit it can beat bare-handed on Wrestling. The
target coordinate came out of the world save rather than from reasoning about
where rabbits ought to be; that correction has now been needed three times in
this milestone.

---

## 10. Phase 13 — the craft menu as execution authority

The rule: **a bot may only attempt a recipe the server actually offered.** Until
now that was a per-scenario habit, not something the client could assert, and
M3.7 shows the cost when the habit slips — a recipe was recorded as "not
offered" when in truth the menu had never opened, and that produced a false
claim about thread which survived into a milestone report.

`CraftableNow()` plus `expect_menu_has` / `expect_menu_lacks` / `menu_report`
make the distinction assertable. **The guard fired correctly on its very first
real use:**

```
live menu offers 0 option(s)
ERROR expect_menu_has 'Shirt': no craft menu is open (line 47); aborting
```

That is the intended behaviour, not a failure. The sewing kit asks for a cloth
target before the server sends any option list, so the menu genuinely was not
open — and the client refused to answer "is Shirt offered?" for a menu that did
not exist, instead of reporting a false negative. That refusal is precisely what
M3.7 lacked.

**Honest limit:** the *positive* half is still unproven live. The follow-up run
aborted at `require cloth` — RevolutionTailor's cloth was consumed by M3.7 slice
A, so there is nothing to target and the menu cannot be opened without buying
stock first. `expect_menu_has` has therefore never returned true against a real
menu. Recorded as PARTIAL rather than PROVEN.

### Two parser bugs found on the way

`expect_menu_has` could never load at all: it called `getline()` before
`need()`, which swallowed the operand and left `st.a` empty, so every use died
with `expect_menu_has needs <name>`.

The first fix was also wrong. `need()` **reads from the stream itself** — it does
not merely validate a value already parsed — so `ls >> st.a` followed by
`getline` followed by `need()` consumed the operand twice and left `need()`
nothing. Both wrong orderings fail with the *identical* message, which made the
second attempt look like the rebuild had not happened. Correct order is
`need()` first, `getline()` for the remainder.

`menu_pick` carried the same latent bug. It has never been used by any scenario,
which is why nobody noticed: **an op that is written but never exercised is not a
feature, it is an untested claim.**

---

## 11. Phase 9 continued — four attempts to prove a bot can kill

The remaining M4 blocker is the simplest question in the milestone: *can a bot
kill anything at all?* Four runs, and the value is mostly in why the first three
failed — **none of them was a shard bug, and none was a combat bug.**

| # | What happened | Actual cause |
|---|---|---|
| 1 | `travel_entity @prey` failed `why='none'`, then `[0xAA] attacking 0x4FFFFFFF` | Travel plans a route to the mobile's position **at planning time**; a rabbit does not stay there. Replaced with `goto_mobile`, which re-resolves as it moves |
| 2 | Same `0x4FFFFFFF` even while chasing | **The character was a GHOST.** `System: Your ghostly hand passes through the object` |
| 3 | Resurrected correctly, walked to the farms, then aborted on `require prey` | The fauna had wandered. One scan samples one instant |
| 4 | Arrived, scanned 3x, aborted on `require prey` | Only Bull, Grizzly Bear, Horse, Llama and Timber Wolf were present -- every one excluded on purpose. **A target list and a destination have to agree** |
| 5 | **Attack ACCEPTED** -- `[0xAA] attacking 0x000008BC`, `attack success ... server accepted the target`, five times. Chicken alive in the final scan | Target acquisition works; the kill does not |
| 6 | Attack sent ONCE and never re-issued, bot kept in range for ~20 s. Dog `0x00000856` **still alive** | Disproves the swing-reset theory from attempt 5. The likely remaining cause is damage, not targeting: Spar lost its weapon to full loot loss and is punching at Wrestling with STR 30 |

`0x4FFFFFFF` is Sphere **clearing the attack target**, not a miss. Reading it as
combat failure is what made attempts 1 and 2 look like the same bug when they
were two different ones.

### The ghost, and the scenario that caused it

`m39_corpse_proof` resurrected Spar and then **logged out standing in a graveyard
full of hostile undead**, freshly raised and stripped of equipment. It was killed
again immediately, so every later run began as a ghost — and a ghost cannot
fight, gate, or pick anything up.

That is a general rule, not a one-off: **a scenario that ends in a hostile place
undoes its own result and silently invalidates every run after it.** Character
state persists between logins, so where a scenario leaves the bot becomes the
next scenario's starting condition. `m39_hunt` now opens with
`travel_service healer` + `resurrect` + `wait_alive`, which is harmless when
already alive (`resurrect` returns `invalid_state`) and establishes state rather
than assuming it.

### Deliberately not stacking the deck

The prey list is rabbit, chicken, bird, rat, cat, dog, eagle, crow. Wolves,
bears and great harts are excluded **even though they are common at that
coordinate**, because an unarmed character can lose to a Great Hart and the
question being asked is whether a bot can kill *anything*, not whether it can win
a hard fight. Answering the small question honestly beats answering a bigger one
ambiguously.

---

## 12. Phase 14 — the soak FAILED, and finding that took two mistakes

### 12.1 A reporting error of my own, corrected

Throughout the run I reported the soak as **"0 failures"**, repeatedly, to the
shard owner. That was wrong. I was counting `travel_done ... ok=0`, but a failed
journey does not emit `travel_done` at all — it emits a **different event**,
`travel_failed`. Grepping for the success event's failure flag will never find
the failure event.

The true numbers, once counted correctly:

| | |
|---|---:|
| bots that completed the circuit | **11 / 38** |
| travel successes (`travel_done ok=1`) | 731 |
| travel failures (`travel_failed`) | **99,290** |
| scenario aborts | 0 |

A second, smaller miscount sat on top of it: I counted "bots that finished" by
grepping for the phrase `soak circuit complete`, which appeared in **26** files.
Bots *hear each other's speech* — a bot standing near a finisher logs the phrase
as ambient chat. Counting the bot's own `[action] say:` line gives 11.

Both errors ran the same direction: they made a failing run look clean. The
lesson is narrow and worth keeping — **grep for the event that means failure, not
for a failure flag on the success event** — and it belongs beside the earlier
rule about reading verdicts from `.console.txt` rather than `.log`.

### 12.2 What actually failed

Two destinations account for essentially all of it:

```
59,249 x  at=(1335,1925)     why='none'  plans=4
40,017 x  at=(1335,1926)     why='none'  plans=4
```

(An earlier revision of this section printed 55,481/38,128. Those were a
mid-run snapshot written up beside the *final* total, which made the section
disagree with itself. The figures above are the completed run: 99,266 of 99,290
events from two tiles.)

Both sit one to three tiles from the circuit's waypoint `travel_point 1336 1928`
— a tile chosen *because* bots had demonstrably stood on it. It is walkable but
not **shareable**: with 38 bots converging, the destination is occupied.

**But crowding is only the trigger.** The defect is a hole in the journey state
machine, found by the agent that fixed it and NOT what §12.3 originally guessed:
each bot reached `(1335,1925)`, Chebyshev 3 from the waypoint — exactly
`kLegArriveSlack` — so the leg counted as ARRIVED, `Journey::Advance()` ran off
the end of the route into a pseudo-Walking phase, and after four instant replans
`NextCommand()` returned `Fail` **without ever setting `failure_` or moving the
phase to `Failed`**. The journey stayed `Active`, `TravelFinish(false, "none")`
re-fired every client tick, and `wait_travel` never woke. That is the
`why='none' plans=4` signature.

It is not an interior-navigation failure and not an oscillation loop, which is
what the symptom looked like from outside.

### 12.3 The real defect: the failure path spins

Failures repeat every **~62 ms** — about 16 attempts a second, indefinitely, with
no backoff and no give-up. One bot logged 8,024 failures for a single waypoint.

That is the finding worth having, and it is a **BLOCKS_M4** one. It is not that a
bot occasionally cannot reach a tile; it is that when it cannot, it burns CPU at
16 Hz forever instead of re-targeting an adjacent tile, waiting, or abandoning
the leg. A population of autonomous characters will hit occupied destinations
constantly — banks, forges, vendors, moongates are exactly where they all want to
stand.

Note what this is *not*: not the M3.8 stale-occupancy bug (that purge works — the
bots see each other correctly, 1,238 mutual sightings), and not a shard problem.
The server is fine. The client's travel layer has no failure budget.

### 12.4 What the soak did prove

* 38 concurrent real protocol clients connect, create characters, and log in
* `travel_service banker` resolved correctly for all 38
* 731 journeys completed, including the whole first half of the circuit
* No crashes, no scenario aborts, no protocol desync
* Bots perceive each other properly under load

The concurrency *substrate* holds. The travel layer's error handling does not.

### 11.1 Where the kill proof actually stands

Six runs. **Targeting is solved and the server accepts it** — `[0xAA] attacking
0x00000856`, `attack success ... server accepted the target`. Nothing about
acquiring or holding a combat target is broken.

What has never happened is a creature dying. Attempt 6 ruled out the most
plausible mechanical explanation (re-issuing `attack` resetting the swing timer)
by sending the command exactly once and simply staying in range: the dog lived
anyway.

The remaining explanation is the mundane one, and it is a **test-fixture
problem rather than a client defect**: RevolutionSpar is unarmed. It died at the
cemetery, full loot loss put its weapon on a corpse, and it has been punching
ever since with Wrestling at STR 30 inside a ~20-second window. That is
plausibly just not enough damage.

Recorded as **OPEN and precisely characterised** rather than guessed at. The next
attempt should equip the character first — buy a weapon, or recover the one still
on its corpse — and give the fight longer. It should not add more chase logic.

---

## 13. M3.9 final status

**STATUS: PARTIAL PASS.** The world is populated and durable, three BLOCKS_M4
debts are closed, one previously invisible defect was found and fixed, and one
new BLOCKS_M4 defect was found and is NOT fixed.

### Closed and proven live

| Debt | Was | Now |
|---|---|---|
| #1 stale mobile occupancy | a mobile seen once walled a tile forever | time-based purge; 38 bots saw each other correctly, 1,238 mutual sightings |
| #4 world effectively monsterless | 1 undead in the entire world | 5,049 spawners, era-audited, persisted to disk |
| — corpse recovery | **never worked at all** | died → `travel_corpse` resolved → NPC healer raised the ghost → returned to the corpse |

### Closed as BUILT (unit-tested, not driven live)

Supplier resolution from verified stock (#2), knowledge freshness (#3), pet
command semantics and safety (#9). 267 checks in `m38_closure`; 9/9 suites pass.

### Open

| # | Item | State |
|---|---|---|
| 14 | **Multi-bot concurrency** | **FAILS.** 99,290 travel failures vs 731 successes; the failure path spins at 16 Hz with no backoff. §12 |
| 8 | A bot winning a fight | targeting proven, kill not. Six attempts. Most likely cause: the test character is unarmed. §11.1 |
| 2b | Runebook Recall execution | mechanic proven in M3.6, wiring absent. Blocks the mage archetype only. §8 |
| 13 | Craft-menu positive assertion | guard half proven, positive half needs cloth to target. §10 |
| 12b | Reagent vendor sourcing | 60 reagent spawners now live; vendor claim still rests on owner testimony |

### What blocks M4

**One thing, and it is new: the travel layer has no failure budget.** Everything
else on the original BLOCKS_M4 list is closed or built. A population of
autonomous characters will contend for bank steps, forges and moongates
constantly, and today each such contention burns a core at 16 Hz forever.

### Corrections made to earlier records

* **Ilshenar was never on this shard.** §3.2 said "Sosaria + Ilshenar". The real
  Revolution client ships only `map0.mul` — no map1–5 exist — so Ilshenar cannot
  load. "The whole world" means map0, which at 7168×4096 already includes the
  Lost Lands.
* **The soak was reported as "0 failures" during the run. It was not.** Failures
  emit `travel_failed`, not `travel_done ok=0`. See §12.1.
* **Completion was miscounted 26 vs 11**, because bots hear each other's speech.

### The recurring lesson, now four milestones old

Three failures this milestone were constants chosen by *reasoning* rather than
read from *evidence*: a wilderness coordinate with no wildlife, a 60-second dwell
that was fatal once the graveyard filled, and soak waypoints that were walkable
but not shareable. Each was fixed by reading the world save or the previous run's
own logs.

And one new form of the same error: **grepping for the wrong event.** A metric
that cannot observe failure will always report success.

---

## 14. Why a bot could not kill anything — the real answer

Seven runs. The answer is not damage, not the weapon, and not the swing timer.
**The client sheathes its own weapon fifteen seconds into every fight.**

```
21:20:18.305  [war] entering war mode
21:20:19.124  [0xAA] attacking 0x000028CB          <- target accepted
21:20:34.161  [war] dropping war mode: war mode idle with no target
21:20:34.162  [war] leaving war mode
```

The chain, all client-side:

1. `Client::OnAttackAck` correctly arms the watchdog — `war_.OnCombatIntent()`
   sets `intent_ = Fighting`. This part works.
2. `Client::WarModeTick` declares the target **gone the first tick it is not in
   `mobileCache_`**:
   `if (target && !FindMobileBySerial(target)) war_.OnTargetGone(target, now);`
3. `OnTargetGone` sets `intent_ = None`.
4. `ExitReason` then matches `intent_ == None && hasCombat_ && idle > 15 s`,
   returns *"war mode idle with no target"*, and the bot leaves war mode.

### This is a regression M3.9 introduced

M3.9 Phase 1 made `PurgeOutOfRange` more aggressive to fix stale occupancy — it
now purges on a 2-second timer even while the player is stationary, not only on
movement. A fleeing animal that steps briefly out of view is purged within ~2 s,
which trips step 2.

**Neither change is wrong on its own.** The occupancy fix was correct and is
proven by the soak; the watchdog is sensible. Together they make a bot disarm
itself whenever its prey breaks line of sight — which is what prey does.

The watchdog already has the right rule for this and it is being bypassed:
`WarMode.h` carries both `idleTimeoutMs = 15000` and `targetLostMs = 8000`, and
`ExitReason` has a dedicated "combat target has not been seen for a while" branch
driven by `lastSeenTargetMs_`. The eager `OnTargetGone` in `WarModeTick`
short-circuits it, converting a momentary loss of sight into a permanent
"no target".

Handed to the Fable agent, which owns those files. The other `OnTargetGone`
caller (`Client.cpp:1219`, the 0x1D remove handler) must stay: there the server
really did destroy the object.

### What the seven runs bought

Every failed attempt eliminated a real hypothesis, and the last one was only
findable once the earlier four were cleared:

| eliminated | by |
|---|---|
| stale-position chase | `goto_mobile` instead of `travel_entity` |
| the bot was dead | ghost messages in the log; healer preamble added |
| no prey present | three scans instead of one |
| target list vs destination disagreed | livestock list + a farm the save says has chickens |
| swing timer reset by re-issuing attack | attack sent once; dog still lived |
| damage too low / unarmed | fresh Swordsmanship character, katana equipped, STR 55 |

Only after all six were gone was the war-mode log line the obvious suspect. It
had been in every log since the first attempt.

### 14.1 The war-mode fix worked, and revealed the next layer

After the fix the exit reason **changed**, which is how you know the first
diagnosis was right:

```
before:  [war] dropping war mode: war mode idle with no target
after:   [war] dropping war mode: no combat event within the timeout
```

Combat intent now survives a target stepping out of view. What did not arrive
was any combat event.

### 14.2 The bot was not failing to fight. It was committing a crime.

The next run's log answered a question nobody had asked:

```
System: Guards can now be called on you!
Basia: Help! Guards a Criminal!
Dog: *Dog is attacking you!*
```

The chickens, dogs and sheep at a Britain farm are **innocent, owned animals
inside a guard zone**. Attacking one flags the character CRIMINAL and an NPC
calls the guards. The bot was adjacent (player `(1378,1822)`, prey
`(1377,1822)`), armed, in war mode, with the attack accepted — and the whole
premise was wrong.

Every earlier "harmless prey" list in this milestone made the same mistake,
because "harmless" was chosen for *how hard the animal hits back* and never for
*whether killing it is legal*.

**This matters more than the kill proof.** A bot population that hunts livestock
near towns would be executed by guards, and the criminal flag would follow it. An
autonomous character must hunt **wild, already-hostile creatures away from guard
protection** — where no crime is committed and the creature is attacking anyway.
That is now a standing constraint on any hunting behaviour M4 builds.

It also explains, in hindsight, why the cemetery runs produced real combat
immediately: undead are hostile, so fighting them is lawful and the server had
every reason to swing. The one place a bot could legitimately fight was the place
it kept dying.

### 14.3 The law works, and it killed the bot

The next run never reached the wolves. The criminal flag from attacking the farm
animals **persists across logins**, so NPCs kept calling guards as the character
crossed Britain, and a guard finished the job:

```
Azora:   Help! Guards a Criminal!
Ancelin: Take this vile criminal!
Ancelin: *Ancelin is attacking you!*
[STATE] dead (0x2C resurrect menu)
EXPECT travel ok but it failed (died); aborting
```

Full loot loss took the katana too, so session 51 could not be reused.

This is a **good** result for the milestone even though it failed the scenario:
the shard's law system is live, criminal status is sticky across sessions, and
guards enforce it. Any M4 hunting behaviour has to model that, not discover it.

It also closes the loop on §14.2 with a consequence rather than a warning: a bot
that hunts the wrong creature does not merely fail to eat — it gets executed and
loses everything it carried.

### 14.4 Ten runs: the wall is the swing itself

With an innocent, armed character attacking a wild hostile wolf far from any
guard zone, everything up to the swing works and the swing never happens:

```
[war] entering war mode
[0xAA] attacking 0x0000B475            <- server ACCEPTED the target
... 15 seconds ...
[war] dropping war mode: no combat event within the timeout
```

Grepping the whole fight window for packet tags yields exactly one: `[0xAA]`.
**No 0x2F Swing packets at all**, in either direction. `Client::OnSwing`'s own
comment states the server sends 0x2F "on every weapon swing" and that it is "the
earliest serial-bearing combat signal (long before HP changes)". None arrive, so
`war_.OnCombatEvent()` never fires and the watchdog exits correctly — the
watchdog is now telling the truth.

Confirmed not the cause, each by measurement: character alive and innocent
(no criminal flag, no guard aggro), katana equipped on layer 1 with STR 55 and
Swordsmanship 50, war mode on, target accepted, target wild and hostile
(it had been attacking the bot), bot adjacent (player `(1378,1822)`, prey
`(1377,1822)` in an earlier run), and range maintained by `goto_mobile`.

The server acknowledges the combat target and then does nothing. Handed to the
agent, pointed at the Source-X source (`Fight_Attack`, `Event_Attack`, and the
0x05/0xAA/0x2F paths) as ground truth.

### 14.5 A fifth constant taken from reasoning instead of data

The run before that failed for a duller reason. A grey wolf was standing next to
the bot, attacking it, and `remember prey` bound `0x00000000`:

| token | I used | this runtime |
|---|---|---|
| grey wolf | `0x001C` | **`0x0019`** (`c_wolf_grey` = `[CHARDEF 019]`) |
| panther | `0x00D5` | **`0x00D6`** |
| brown bear | `0x00D3` | **`0x00A7`** (`0x00D3` is a BLACK bear) |

`0x001C` is what generic UO documentation gives. It is not what this shard ships,
and the very first cemetery scenario of this milestone carried the same bad list.

That is the fifth time in M3.9 that a constant reasoned about rather than read
from the shard's own data has cost a live run — after the wilderness coordinate
with no wildlife, the dwell written for an empty graveyard, the soak waypoints
that were walkable but not shareable, and prey chosen for how hard it hits back
rather than whether killing it is legal.

---

## 15. A bot killed a creature — and why ten runs could not

```
remember prey = 0x0000A01C
[0xAA] attacking 0x0000A01C
Timber Wolf: *Timber Wolf is attacking you!*
System: You have gained a bit of fame.        <- Sphere's kill reward
skill sum 6870 -> 6871                        <- +0.1 Swordsmanship
```

An ordinary character — Swordsmanship 50, katana from the shard's own
`[NEWBIE SWORDSMANSHIP]` template — fought a **mutual** 45-second melee and won,
holding war mode throughout with zero watchdog drops, and logged out alive and
still armed. Nothing was granted.

### The cause: the target was asleep, and the server said nothing

Every attack was accepted and then **discarded inside the same call**.

In Source-X every object is *born sleeping* (`CTimedObject.cpp`,
`_fIsSleeping(true)`), and both ticking lists silently skip sleeping objects — no
AI, no swings, no timers, and no error. `Fight_CanHit`
(`chars/CCharFight.cpp:1681`) returns `WAR_SWING_INVALID` as its **first** check
when the target `IsSleeping()`, *before* range or line of sight. So:

```
0x05 -> Event_Attack -> Fight_Attack returns true -> 0xAA ECHOES THE SERIAL
     -> Skill_Start -> Fight_HitTry -> Fight_Hit -> Fight_CanHit -> INVALID
     -> Fight_Clear
```

The fight dies synchronously inside the very call that acknowledged it, emitting
no packet whatsoever. That is the "0xAA then eternal silence" of §14.4.

Confirmed both directions live: the catatonic grey wolf `0xB475`, stationary
beside the bot for twenty minutes, produced silence; console-verified **awake**
wolves (`issleeping=0`, visibly wandering) produced real combat — one killed a
bot in eleven seconds, the other died to this one.

### A client comment that was wrong, and the watchdog built on it

`OnSwing` claimed the server sends `0x2F` *"on every weapon swing"*. That is
**Sphere 0.56b** (`combat.c`). **Source-X sends `0x2F` once per fight**, at
fight-memory creation (`CCharMemory.cpp:506`), and only to the attacker. Zero
`0x2F` during a fight is *normal*, so a watchdog fed only by `0x2F` was always
going to time out mid-fight.

§14.4 of this document treated "no 0x2F" as proof that no swings occurred. It was
proof of nothing. Per-blow evidence arrives as `0x6E` animations and HP updates
(`0xA1`/`0x2D`) — which log only under `verboseConsole_`, which is why grepping
the fight window appeared to show an empty wire. Those three handlers now feed
`war_.OnCombatEvent`, which is what `WarMode.h` always promised counted.

### Four of my hypotheses, disproven rather than dropped

The 0x05 packet is byte-exact for `PacketAttackReq::onReceive`; nothing in
Source-X clears the fight target on movement or resync (the fight was dead before
the first `goto_mobile`); `Fight_Attack` sets `STATF_WAR` server-side itself; and
range/LOS is checked *after* the sleep check that was firing.

### `SectorSleep=0`

`SectorSleep=10` left NPCs stuck asleep beside connected clients, because the
wake paths have holes — `_GoAwake` ends by calling `_OnTick`, which can
immediately re-sleep a just-woken adjacent sector with a stale
`m_iTimeLastClient`; nothing wakes an *individually* asleep char in an *awake*
sector; and the ticking lists skip sleepers silently, which the shard's own log
spams as `CanTick=0, SleepingState=1`.

Cost measured rather than assumed: **1.35% → 6.1% of one core** (0.27 cpu-seconds
per 20 s wall → 1.84 per 30 s), working set 96 → 206 MB, against 49,794 items and
1,765 chars. Negligible on 16 cores.

Config rather than a source patch on purpose: the baseline records **0
modifications to Source-X** and CLAUDE.md makes Sphere authoritative. The
surgical engine fix would be `Fight_Attack` waking its target; that belongs
upstream, and the three engine holes are recorded in `sphere.ini` so they can be
reported rather than rediscovered.

### The correction this forces on the rest of the milestone

M3.9 repeatedly used *"the sector is asleep"* to explain empty dungeons and
absent fauna, and treated it as **benign waiting** — §5's scenario header says so
in as many words. It was not benign. It also made those creatures unfightable,
and that reasoning is why the first PvM scenario avoided dungeons in favour of a
wilderness animal that turned out to be equally asleep.

### 15.1 Independently reproduced, on the species that never worked

A second run after applying `SectorSleep=0` killed a **grey wolf** — the exact
type that sat catatonic through all ten earlier attempts:

```
22:16:39  [war] entering war mode
22:17:01  System: You have gained a bit of fame.     <- kill, 22 s in
22:17:16  [war] dropping war mode: war mode idle with no target
```

**Zero watchdog drops during the fight**, and the one drop afterwards is correct:
the target is dead, so there is nothing to fight. The wolf had also opened the
fight itself (`*grey wolf is attacking you!*`), which the old configuration made
impossible.

No skill gain on this one (6871 → 6871). Skill gain is probabilistic; the first
kill gained 0.1. Reporting it rather than quietly picking the run that gained.
