# M3.9.1 — M4 Launch Gate

Date: 2026-08-27. **STATUS: IN PROGRESS.**

A short, evidence-driven gate. Not another archaeology milestone: M3.9 found and
fixed the hard problems, and the only question here is whether autonomous
character life can safely start.

---

## 1. Baseline, measured before any M3.9.1 change

| Repo | Branch | HEAD | Dirty |
|---|---|---|---|
| `bot/uo-client` | `revolution-sphere-m1` | `3a72057` | 0 |
| `runtime` | `revolution-runtime` | `e8c3b68` | 0 |
| `runtime/scripts` | `revolution-runtime` | `cd4b3e6` | 0 |
| `server/Source-X` | `master` | `dd4183dd` | **0 source modifications** (only an untracked `build-x64/`) |
| `server/Scripts-X` | `main` | `27e78bc` | 0 |

Tests at baseline: **9/9 CTest suites, 0 failures** (`m38_closure` 284 checks,
`m37_economy` 99). A tenth binary, `path_probe`, is a manual probe that wants an
external tiledata path and is deliberately not registered with CTest.

### sphere.ini values this gate cares about

| Setting | Value | Why it is set that way |
|---|---:|---|
| `SectorSleep` | **0** | M3.9: sleeping NPCs failed `Fight_CanHit` before range was checked, so attacks were acked and silently discarded. Combat did not work at all. |
| `ClientMax` | 256 | |
| `ClientMaxIP` | 1000 | raised from 16 for the fleet |
| `ConnectingMax` | 1000 | raised from 32 |
| `ConnectingMaxIp` | 1000 | raised from 8 |
| `MaxConnectRequestsPerIP` | 100000 | raised from 50 |
| `MaxPings` | 100000 | raised from **15** — this was the setting that actually banned the fleet; the three above were raised first and changed nothing |
| `ReagentsRequired` | 1 | M3.8, corroborated by the 14.05.2009 changelog |
| `HitsHungerLoss` | 1 | starvation on, at the owner's instruction |

### World population

| | |
|---|---:|
| creatures in world | **13,345** |
| player characters | 58 |
| items | 52,177 |
| spawners | 5,049 |

The creature count is worth pausing on: it was **1,765** immediately after the
M3.9 population pass. `SectorSleep=0` is why — with sectors never sleeping, every
spawner fills and stays filled. That is the intended behaviour, and it is also
the reason the CPU baseline below had to be re-measured rather than reused.

### Idle server cost, populated, `SectorSleep=0`

| | |
|---|---:|
| CPU | 5.73 cpu-seconds / 30 s wall = **19.1 % of one core** |
| working set | **171 MB** |

Earlier measurements were 1.35 % (sleep on, 1,765 chars) and 6.1 % (sleep off,
1,765 chars). The rise to 19.1 % tracks the seven-fold creature increase, not a
regression in the setting. On 16 cores this is comfortable; it is recorded so a
future fleet measurement has something honest to compare against.

---

## 2. Changes made during this gate

### 2.1 Open-container access — looting was structurally impossible

`FindBackpackItemByGraphic` hard-codes the player's own pack, and the container
cache plus `SendTakeToBackpack` were private to `friend struct js::ClientBindings`.
A scenario could therefore kill a creature and **not touch a single thing it
dropped** — and the JS layer that could is never wired to `--scenario` runs.

Added the minimum public surface: `FindContainerItemByGraphic` (graphic *list*,
because stacked resources change graphic as the pile grows),
`ContainerItemCount`, `ContainerItemAt`, `TakeFromContainer`.

Also established: **a wolf corpse is empty until carved.** `CChar::MakeCorpse`
only moves what the creature was carrying; a CHARDEF's `RESOURCES` line reaches
the corpse through `Use_CarveCorpse`. Confirmed live — a fresh wolf corpse
reported `[0x3C] container contents: 0 item(s)`.

### 2.2 Combat survival — bots no longer fight to the death

`combat_policy.h` decides, `Client::SurvivalTick` applies. Opt-in via a
`survival on` scenario op, because dozens of existing scenarios script their own
fights and would be disrupted by the client retreating on their behalf.

Order matters and is driven by the runtime, not by taste: a potion is instant, a
bandage is `SKILL 17 DELAY=3.0`. So **potion before retreating, disengage before
bandaging.** Standing still for three seconds beside something that hits is
exactly how a bot died at 17 HP with ten bandages in its pack.

Unknown health returns **−1, never a cheerful 100**, and unknown health in combat
breaks contact rather than hoping.

Thresholds are **DERIVED** — no archive states what health a Revolution player
retreats at — and are named constants in one place rather than dressed up as
fidelity.

---

*Sections 3 onward are written as each gate completes.*

---

## 3. Invalid world graphics — audited, and much smaller than it looked

M3.9 reported ~13 startup warnings of the form:

```
ITEMDEF has invalid ID=17610 (044ca) (value is greater than the tiledata maximum index)
```

Measured properly: **213 warnings across 8 startups yesterday (~27 per startup),
covering 24 distinct graphic IDs.**

### The ceiling is real and computable

The Revolution client's `tiledata.mul` is 1,036,288 bytes. Land occupies
`512 × (4 + 32×26)` = 428,032 bytes, leaving 512 item groups of 32 — so the
client knows item IDs **0x0000–0x3FFF**. Every warned ID is above that ceiling,
which is why none of them can render. This is the same class of problem as the
Mondain's Legacy creatures M3.9 pruned, where an unrenderable body crashed
ClassicUO on every launch.

### But almost none of them are in the world

| | |
|---|---:|
| distinct invalid IDs warned | 24 |
| **actually placed in the world** | **1** |

The single placement:

| Item | Graphic | Where | Source | Era |
|---|---|---|---|---|
| `i_soulforge_small` | `044ca` | (4552,2303,−2) | `spherestatics.scp:123141` | **Stygian Abyss (2009)** |

Everything else is an inert **definition**: soulforge variants
(`i_profession.scp`), the aquarium multi and its two components
(`i_aquarium.scp`, `ID=0a3b5`, `COMPONENT=0a3ba/0a3b0`), and a `random_jewelry`
loot-list entry (`c_human_citizens.scp`). They warn when the scripts load and are
never placed, so nothing a client can see is affected by them.

That distinction matters for the gate: **unrenderable definitions are noise;
unrenderable placements are a client crash risk.** There is exactly one of the
latter, and removing one Stygian Abyss decoration costs the world nothing.

### Two search mistakes worth recording

Both cost real time, and both are the same mistake this project keeps making.

* I first searched for `^DEFNAME=…` to resolve the IDs and found nothing. These
  `ITEMDEF`s put the name in the **section header** — `[ITEMDEF
  m_aquarium_wall_mounted_south]` — exactly like the heal potions did an hour
  earlier. Third occurrence.
* My first placement check sampled seven of the twenty-four IDs and reported
  zero. `044ca` was not in the sample. A partial query returning zero is not an
  answer; the full sweep found the one real hit immediately.

---

## 4. Gate results

### 4.1 Multi-bot soak (§2) — **PASS**

38 concurrent real clients, five laps of a contested circuit that *deliberately
included* `1336,1928` — the waypoint whose neighbours produced 59,249 and 40,017
failures in M3.9.

| | M3.9 | **M3.9.1** |
|---|---:|---:|
| reached world | 15/38 | **38/38** |
| circuits completed | 11/38 | **33/38** (5 still mid-circuit at measurement) |
| `travel_done ok=1` | 731 | **874** |
| `travel_failed` | **99,290** | **197** |
| `why='none'` | all of them | **0** |
| max failures, single bot | **8,024** | **13** |
| scenario aborts | — | **0** |
| anti-flood bans | 23 blocked, IP banned | **0** |

Failure reasons — every one a real diagnosis, none repeating:

```
182  why='sealed in; recovery exhausted'
 10  why='tile route stopped short'
  5  why='oscillating between tiles'
```

Concentrated at (1349–1360, 1778): the **blacksmith**, an indoor destination.
That is the known interior-routing debt surfacing as a *bounded, named* failure —
which the gate explicitly counts as correct behaviour, not a blocker.

Every FAIL condition checked and clear: no journey re-fires, no ~16 Hz loop, no
`why='none'`, no bans, no desync, no crashes.

### 4.2 `SectorSleep=0` under load (§3) — **PASS**

| | idle | under 38-bot fleet |
|---|---:|---:|
| server CPU | 19.1 % of one core | **13.2 %** |
| server working set | 171 MB | 173 MB |
| bot fleet RSS | — | 949 MB (38 × ~25 MB) |

World: 13,345 → 14,130 creatures, 80,812 items. NPC AI kept ticking and hostile
mobs kept initiating combat throughout — the cemetery survival run happened on
this same shard.

Reported as measured: load CPU came out *lower* than idle. The likely reason is
that the idle sample was taken shortly after a restart while spawners were still
filling. Recorded rather than explained away.

### 4.3 Invalid world graphics (§4) — **PASS, one residual**

| | before | after |
|---|---:|---:|
| warnings per startup | ~27 (213 over 8) | **1** |
| unrenderable items placed in the world | **157** | **0** |

Removed:

| What | Count | Era |
|---|---:|---|
| gargoyle jewellery (necklace/bracelet/ring/earrings) | **136** | Stygian Abyss |
| misc post-era loot (ichor bottle, gift boxes, gargish sash, pirate chest, lava serpent crust, hooch jug, gothic chest, delicate scales, gargoyle flute, aud char) | 20 | SA / HS |
| `i_soulforge_small` | 1 | Stygian Abyss |

**Root cause fixed, not just the instances.** `tm_generic.scp`'s ordinary
jewellery lists each contained a gargoyle variant —
`random_rings { i_ring_gold 1 i_ring_silver 1 i_ring_gargoyle 1 }` — so every NPC
rolling jewellery had a one-in-three chance of carrying something no player can
see. The gargoyle entries are removed from the *human* lists;
`random_jewelry_gargoyle` is left intact because it is what a gargoyle NPC would
use and is unreachable in this era anyway.

**One residual**, honestly unresolved: `spherestatics.scp` reports one item with
ID `0x5738`. That value appears nowhere in the file as a graphic, so it resolves
through a defname I could not isolate cheaply. It is a single decorative static.
It does not block M4, and it is recorded here rather than quietly dropped.

### 4.4 Craft-menu oracle (§5) — **PASS**

```
live menu offers 2 option(s)
  expect_menu_has  Parts       -> pass
  expect_menu_lacks Platemail  -> pass
  menu_pick Parts
live menu offers 4 option(s)
  menu offers 'Nails', as expected
  menu_pick Nails
System: You put the nails in your pack.
skill sum 6533 -> 6680
```

Selected **by name** at both levels, never by index. Sphere filters menus by
skill and inventory, so index and name routinely disagree — and when they do, the
index is wrong.

Not the suggested Shirt: `i_shirt_plain` needs 8 `i_cloth`, and `i_cloth` is
`WorldProcessed` in our own vendor policy, so an NPC purchase is refused **by
design**. The refusal was correct; the recipe was the wrong choice for a short
gate.

### 4.5 Combat / death lifecycle regression (§6) — **PASS**

At the Britain cemetery, the same spawners that killed RevolutionSpar:

```
Skeletal Mage: *Skeletal Mage is attacking you!*
hp 55 -> 36 (65%) -> 27 (49%) -> fight
hp 18/55 (32%) -> disengage      event survival_disengage
hp 18/55 (32%) -> bandage        event survival_bandage x2
say: the fight is over and I am still here
```

Hostile creature active, bot innocent, target accepted, war mode held while real
combat events flowed, damage exchanged both ways, and the character **survived**.
Kills remain proven from M3.9 (Timber Wolf, grey wolf). The death loop —
die → corpse located → healer → resurrect → return — remains proven.

New this gate: **carve and loot**. `hides 0 -> 7` taken from a carved cougar
corpse into the bot's own pack.

### 4.6 Supplier resolution (§7) — **PASS**

`travel_service tailor` → the shop → `mobile_trade` → vendor `0x000012C5` → its
**actual 41-item stock list** with serials and prices. The supplier came from
*observed stock*, never from a profession tag.

Two failure modes proven along the way, both desirable:

* `mobile_name` cannot find a vendor — the NPCs are called Felicite, Kyler,
  Kaysa. Trade lives in the paperdoll title.
* the purchase was **refused** because `i_cloth` is `WorldProcessed`. A bot
  cannot buy its way past a player-processed good.

### 4.7 Nobles disabled (owner request)

44 `c_noble` / `c_noble_f` and their **28 spawners** removed — they do nothing,
and removing the spawners means they do not come back. This also cleared the last
invalid-graphic warning: a noble was carrying the final unrenderable item, which
is why searching the statics for its graphic could never find it.

Final state after remediation: **0 invalid-ID warnings**, 14,086 chars, 79,789
items.

---

## 5. M4 launch matrix

`PROVEN` = demonstrated live on the shard · `BUILT` = code + unit tests, no live
proof · `PARTIAL` = works with the caveat named · `OPEN` = not addressed ·
`FAIL` = measured broken.

| Capability | State | Live proof | Blocks M4? |
|---|---|---|---|
| Real client connectivity | PROVEN | every run since M1 | no |
| Multi-client connectivity | PROVEN | 38/38 reached world | no |
| Movement | PROVEN | 874 journeys this gate | no |
| Cross-world travel | PROVEN | Yew→Britain, moongates | no |
| Crowded-destination handling | PROVEN | bounded arrival/failure at contested tiles | no |
| Terminal travel failures | PROVEN | 197 failures, all with real reasons, max 13/bot | no |
| Stale occupancy expiry | PROVEN | 2 s purge; 1,238 mutual sightings under load | no |
| Moongates | PROVEN | M2.5, still used | no |
| World population | PROVEN | 5,049 spawners, 14,086 creatures | no |
| Era-safe population | PROVEN | ML spawners pruned; 157 unrenderable items removed; 0 warnings | no |
| Legal PvM | PROVEN | wild hostiles; criminal flagging understood and avoided | no |
| Bot kills a creature | PROVEN | Timber Wolf, grey wolf | no |
| Damage perception | PROVEN | HP tracked 55→18 through a fight | no |
| Death | PROVEN | repeatedly, with full loot loss | no |
| Corpse tracking | PROVEN | `travel_corpse` resolves after `RecordOwnDeath` | no |
| Healer resolution | PROVEN | wandering healer Dale | no |
| Resurrection | PROVEN | ordinary NPC healer | no |
| Corpse return | PROVEN | walked back, alive | no |
| Banking | PROVEN | M2/M3 | no |
| Vendor buy | PROVEN | M3.7 slices | no |
| Vendor sell | PROVEN | M3 income slice | no |
| Concrete supplier resolution | PROVEN | vendor `0x12C5`, 41 observed stock entries | no |
| Supplier freshness | BUILT | bands unit-tested; not aged out live | no |
| Crafting menu authority | PROVEN | both halves; craft driven by name | no |
| Gathering | PROVEN | mining, lumberjacking, shearing (M3.7/3.8) | no |
| Corpse looting | PROVEN | carve → take; hides 0→7 | no |
| Food / hunger | PROVEN | `HitsHungerLoss=1`; starvation and eating shown in M3.8 | no |
| Reagent consumption | PROVEN | `ReagentsRequired=1` | no |
| Skill gain | PROVEN | 6533→6680 from one craft | no |
| Skill / stat cap policy | BUILT | 700 skill, 225/100 stat, enforced bot-side | no |
| Combat survival | PROVEN | disengage at 32 %, bandage, survived | no |
| Taming rule policy | BUILT | Taming + Lore gates at Revolution values | no |
| Mount travel | PROVEN | 1.82× measured | no |
| Pet semantics | PARTIAL | commands obeyed live; no *fresh* in-session tame | no (Tamer only) |
| Runebook execution | PROVEN | travel layer recalled unaided, one cast per journey | no |
| z-aware journeys | BUILT | unit-tested; no bridge walked live | no |
| Interior macro routing | OPEN | scoped in `NavGrid.h`; fails **bounded** | no |
| Reagent vendor sourcing | PARTIAL | owner testimony, no dated archive | no (Mage only) |
| Spawn density fidelity | OPEN | stock Nerun rates, not Revolution's | no |
| Anti-macro adaptation | OPEN | spec only | **no — but see below** |

### The one constraint carried into M4

**Anti-macro is not implemented.** M4 skill training may proceed, but it **must
not be described as Revolution-authentic** until anti-macro adaptation is built
or explicitly accounted for. This is a live constraint on M4, not a blocker for
starting it.

---

## 6. Verdict

Every REQUIRED condition in §11 of the brief:

| Required | Result |
|---|---|
| no infinite failure spin in the post-fix soak | **met** — max 13 per bot, none repeating |
| terminal failures bounded and truthful | **met** — 0 `why='none'`, three real reasons |
| no protocol/client instability at fleet size | **met** — 0 aborts, 0 crashes, 0 desync |
| no local-IP anti-flood blocker | **met** — 0 bans |
| hostile world works with `SectorSleep=0` | **met** — combat live, 13.2 % of one core under fleet |
| at least one real legal PvM kill proven | **met** — two |
| death/resurrection/corpse recovery usable | **met** — full loop, plus carve-and-loot |
| invalid world graphics removed or shown safe | **met** — 157 removed, 0 warnings |
| craft menu positive assertion works live | **met** |
| supplier resolution cannot silently mis-send bots | **met** — supplier only from observed stock |
| all registered deterministic tests pass | **met** — 9/9 suites |
| repositories clean or intentional changes committed | **met** |

# M3.9.1 PASS — M4 GO

---

## 7. Post-gate findings from live observation

All four raised by the shard owner looking at the running world — none by a test.

### 7.1 Wildlife stacked on one tile

Tameable spawners were allocated in piles: **horses at AMOUNT=7**, frenzied
ostard at 8, llamas up to 4. Ten reapers were found on a single tile.

Set to **AMOUNT=1** across 238 tameable spawners (96 changed, 142 had no `AMOUNT`
line at all). The behaviour the owner described — *one animal, next spawns only
after it is killed or tamed* — is then the engine's own, with no custom logic:
Sphere respawns only when a child dies, and `CChar::NPC_PetSetOwner`
(`CCharNPCPet.cpp:601`) calls `pSpawn->DelObj(GetUID())`, so **taming frees the
slot too**.

### 7.2 Wyvern and greater dragon had no image — and the era audit is why

Removed 45 creatures and 28 spawners: `c_wyvern` (body 0x3E) and
`c_dragon_greater` (0x69) have **no animation in Revolution's `anim.mul`** at any
action, while every control body returns frames.

**This is a hole in M3.9's era audit.** That audit classified creatures by the
*file* defining them — `classic`/`t2a`/`lbr` accepted, `aos`/`se`/`ml`/`sa`
rejected. Both of these sit in `c_monster_lbr.scp` and passed. *Which expansion a
creature belongs to* is not the same question as *can this client draw it*, and
only the second one matters to a player looking at the screen.

My first check tested action 0 only and reported the grey wolf as broken too. A
control failing is how you learn the method is wrong, not the world.

### 7.3 Hostiles carry no loot — correct, not a bug

`CObjBase.h:1080`: `CTRIG_CreateLoot, // Create the loot (called on death)`. An
orc's `i_gold {50 100}`, random weapon and treasure map are created **into its
corpse**. A living orc carrying nothing is right. The earlier carve proof used a
cougar — hides, no gold — so it never surfaced this.

### 7.4 Revolution's custom creatures are missing, and the art is still there

The client can draw **299 bodies. Our runtime defines only 119 of them — 180 are
drawable and undefined.**

Against the owner's own mount table:

| Mount | Body | Defined | Client art |
|---|---|---|---|
| 4 horses, llama, 3 ostards, 2 pack animals | — | yes | **yes** |
| Kii Rin | 0x84 | yes | **NO ART** |
| Unicorn | 0x7A | yes | **NO ART** |
| Nightmare | 0x74 | yes | **NO ART** |
| Ridgeback | 0xBB | yes | **NO ART** |
| Mid Ostard, Mustang, Shire, Steed | — | **no CHARDEF** | n/a |

Only the ordinary mounts work. Kii Rin, Unicorn and Nightmare are defined at
**stock Sphere body ids the Revolution client has no art for** — which is exactly
why M3.6 watched a unicorn crash ClassicUO on every launch, and why M3.7.1
deferred "52 of 62 mounts lack AnimID" as debt.

The strong implication: **Revolution assigned its custom creatures different body
ids**, and that art is sitting in the 180 undefined bodies. Ice Dragon, Fire /
Earth / Energy dragons and Infernal — named in the 2009 changelog and by the
owner — have **no CHARDEF anywhere** in this runtime, so they were never
spawnable at all.

This is recoverable rather than lost: the 180 bodies can be rendered with
`uo_viewer` and identified, then given CHARDEFs at the ids Revolution actually
used. That is content work for after M4's kernel, and it is the single largest
remaining fidelity gap.

### 7.5 On resetting the world

Asked, and measured rather than assumed. Test debris: **61 characters, 2
corpses**. What a wipe would destroy: **5,021 spawners, 14,056 creatures, 27,357
statics, 52,596 items** — the ML prune, population pass, decorator, savestatics
and the 157-item graphics cleanup, all of which would have to be redone and
re-audited.

**Recommendation: do not reset the world.** Reset the *characters* before M4, so
a persistent identity does not inherit test-noise history. Note also that
`sphereworld.scp` is **not** in git — only `spherestatics.scp`, `sphere.ini` and
the scripts are — so world edits are backed up to `save_backup_m39_populate/`
rather than versioned.

### 7.6 What the 13 bodies actually are — and two problems found checking

The shard owner identified eleven of them on sight, and the answer **reframes the
finding**: most are ordinary animal variants the distro never defined, not exotic
mounts.

| Body | Identified as |
|---|---|
| `0x20`, `0x22`, `0x54` | **custom hostile humanoids Revolution added** |
| `0xE0` | grey or timber wolf variant |
| `0xE3` | horse variant |
| `0xE5` | cow |
| `0xEB`, `0xEC` | hind / deer |
| `0x120`, `0x121` | pig |
| `0xDE` | sheared lamb / sheep |
| `0x18E`, `0x18F` | still unidentified |

The three humanoids are the genuine find. The rest are colour/variant bodies of
creatures already in the world — worth defining for visual variety, not a content
gap.

Also corrected: my claim of "180 creature bodies we never define" was wrong.
**167 of the 180 are human-class** (≥ 0x190, 35 actions) — human and equipment
animation groups, not creatures. The real pool is 13.

#### Mount body ids do not match this client's art

Checking the owner's note that `0xDA` looks like a *forest* ostard, not the
frenzied one the runtime calls it:

| Body | Runtime says | Client actually draws |
|---|---|---|
| `0xD2` | `c_ostard_desert` | **a bear** |
| `0xDB` | `c_ostard_forest` | a llama-like animal |
| `0xDA` | `c_ostard_frenzied` | (owner reads it as forest ostard) |

M3.7.1 proved mounts **work** — 1.82× travel speed, measured — but never checked
what they **look like**, because headless bots never render a frame. Riding the
right mount and *seeing* the right mount are two different claims, and only the
first was ever tested.

#### Special logs exist but cannot be harvested

`i_log_ash`, `i_log_oak`, `i_log_yew`, `i_log_heartwood`, `i_log_bloodwood`,
`i_log_frostwood` are all defined, with matching boards. But
`core/regionresources.scp` contains exactly one wood resource:

```
[REGIONRESOURCE mr_tree]
REAP=i_log
```

Every tree yields plain logs. **The items exist; the harvesting side does not.**
The same shape as the ore system, which *is* complete — thirteen ore tiers
(rusty, bronze, old copper, dull copper, shadow, copper, gold, rose, agapite,
bloodrock, silver, verite, valorite) each with a `REGIONRESOURCE` and a real
ITEMDEF, plus a colored blacksmithing menu.

#### A search habit that failed four times today

`i_ore_*`, `i_potion_Heal`, the invalid-graphic ITEMDEFs, and now `i_log_*` were
each briefly recorded as "missing" because a `^DEFNAME=` grep found nothing —
while the definitions sat in **`[ITEMDEF <name>]` section headers**. Four
false absences in one session, all the same shape.

**Search the section headers too, and confirm an absence from a second angle
before writing it down.**
