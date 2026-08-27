# M3.8 — Pre-M4 Foundation Closure

Date: 2026-08-27. **STATUS: PASS, with recorded debt.** All eleven phases addressed; Phase 1 had its premise disproved and the real defect fixed instead. Two world-content gaps found and closed, one authenticity switch restored on a dated primary source, and three items deliberately left UNKNOWN rather than guessed. §4 has the results, §5 what is left.

M3.7 proved the physical resource economy and real player-to-player trade.
M3.8 closes the specific problems that would make autonomous bots unreliable,
inauthentic, or accidentally world-mutating. It is deliberately **not** another
broad archaeology milestone, and **M4 is NOT STARTED**.

---

## 1. Baseline, recorded before anything changed

| Repo | Commit | Branch | Working tree |
|---|---|---|---|
| `bot/uo-client` | `d5a4711` | `revolution-sphere-m1` | **clean** |
| `runtime/scripts` | `be7c185` | `revolution-runtime` | **clean** |
| `server/Source-X` | `dd4183dd` | `master` | clean apart from untracked `build-x64/` (build output, **0 source modifications**) |
| `server/Scripts-X` | `27e78bc` | `main` | **clean** |

**`runtime/` itself is not a git repository** — only `runtime/scripts` is. So
`sphere.ini`, `runtime/save/` and `runtime/accounts/` are **untracked**, and any
change to them (for example `ReagentsRequired`, Phase 6) leaves no commit behind.
That is a real gap in this project's provenance and is registered as debt below.

Tests at baseline — **8/8 registered suites, 0 failures**:

```
m25_world          OK          m3_progression     OK
m2_actions         OK          sphere_regression  OK
m35_authenticity   OK          viewer_safety      31 checks, 0 failures
m36_progression    OK          m37_economy        96 checks, 0 failures
```

`tests/path_probe.exe` also builds but is a manual probe wanting an external
tiledata path (`td load fail (E:/uo/tiledata.mul)`) and is deliberately not
registered with CTest.

Shard `SphereSvrX64_nightly.exe` running; no stale clients.

### M3.7 facts that must remain true

The decorator output stays: 107 forges, 54 anvils, 20 spinning wheels, 33 looms,
1848 doors, 468 signs, 27358 statics, persisted through `.serv.savestatics` into
`spherestatics.scp`. Slices A–E remain PASS. Slice C's ledger still balances:
miner 558→598 gold / 43→42 ingots, smith 920→880 / 52→53, world net zero.

---

## 2. Debt register

Classification:

* **BLOCKS_M4** — no autonomous lifecycle can start until this is closed
* **BLOCKS_ARCHETYPE** — blocks one kind of character, not the foundation
* **SAFE_TO_DEFER** — real debt, does not block M4

Nothing here is silently closed.

### BLOCKS_M4

| # | Debt | Why it blocks | Phase |
|---|---|---|---|
| 1 | **Decorated-interior navigation** — the navgrid is baked from client *statics* and cannot see the decorator's ~6,200 dynamic items. All three M2.5 escape rungs failed from inside the Minoc bank (`route exhausted … no path`, `avoiding 0 block(s)`). | An autonomous bot that walks into a bank, shop or workshop can be trapped there permanently. Every profession enters buildings. | 1 |
| 2 | **Perception has world-mutating side effects** — `ActionScanMobiles` double-clicked every nearby mobile for paperdoll titles, and a double-click on a non-human NPC *mounts it*. Partially fixed in M3.7.1 (human bodies only); the rest of the perception layer is unaudited. | A bot that changes the world by looking at it invalidates every behavioural proof built on top of it. | 2 |
| 3 | **Craft execution indexes the script file, not the live menu** — Sphere filters menus by skill and inventory, so script position ≠ live index (`dialog index 3 out of range (2 options)`). | An autonomous crafter would attempt recipes the server never offered. | 10 |
| 4 | **No machine-readable economy data** — the production graph exists only as compiled-in C++. | Nothing outside the client can plan against, audit or diff the economy. | 9 |

### BLOCKS_ARCHETYPE

| # | Debt | Archetype blocked | Phase |
|---|---|---|---|
| 5 | **Thread has never been produced live.** Cotton → thread is an engine reading only, so every stitched Tailoring recipe is unproven. | Tailor, and the cotton-grower dependency it implies | 3 |
| 6 | **`ReagentsRequired=0`** (`sphere.ini:1060`) — this runtime casts free. Revolution's own history shows a reagent economy, a Reagent Crystal, and changing Recall costs. | **Mage / Warlock.** Free casting cannot be called authentic. | 6 |
| 7 | **Reagent NPC sourcing UNKNOWN** — separate question from consumption, and still unanswered. | Mage / Warlock acquisition (not consumption) | 6 |
| 8 | **Taming threshold divergence** — Revolution `/binek_bilgileri` puts a horse at **53.1**; runtime `c_horse_gray` carries **`TAMING=29.1`**. | Tamer, and anyone acquiring a mount | 7 |
| 9 | **Mounts are not selected or used by any behaviour.** M3.7.1 proved the mechanic (1.82× travel) but nothing decides to ride. | Every travelling archetype — this is a foundation primitive, not a lifestyle | 8 |
| 10 | **The travel planner will not choose its own modes.** The brief names the Runebook, but that is one instance of a wider gap: **moongates are default-off too** (`travelUseMoongates_ = false`, `Client.h:1207`) and are only considered when a scenario opts in with `use_moongates on`. M2.5 proved gates work; nothing selects them. | Mage-capable travellers; in practice **every** long trip | 5 |
| 11 | **49 `PLAYER_CRAFTED_NO_MENU` recipes** — have `SKILLMAKE`, appear in no live menu. Includes `i_nails` and `i_spoon`, which Revolution's own training guide names. | Tinker / Carpenter / Smith progression | 4 |
| 12 | **Legitimate Runebook crafting unproven** (M3.6 §7.6) — the book was restored, but no character has made one. | Mage travel self-sufficiency | 5 (partial) |

### SAFE_TO_DEFER

| # | Debt | Note |
|---|---|---|
| 13 | **Stat cap UNKNOWN** | Not guessed. Still unknown after M3.5/M3.6. |
| 14 | **Anti-macro specified, not implemented** | `REVOLUTION_ANTIMACRO_SPEC.md` exists; no runtime behaviour. |
| 15 | **Hides / leather chain unproven live** | Modelled from scripts only. |
| 16 | **Cooking chain unproven live** | Modelled from scripts only. |
| 17 | **PvM / treasure resource loops unproven** | Modelled from scripts only. |
| 18 | **Post-era mount data** | 52 of 62 mounts have no AnimID in Revolution tiledata; crashes third-party clients. Our viewer is safe, the data is not. |
| 19 | **Special robes** | cloth + Hardening Crystal + elemental crystal. Historically important; explicitly deferred. Do **not** substitute TNS recipe details for Revolution rules. |
| 20 | **Fishing net / S.O.S. content** | Deferred. |
| 21 | **Runebook copying and page transfer** | Both dated 13.05.2009. Not built. |
| 22 | **Blank-rune purchase from a mage shop** | Unproven — the Britain "mage" place holds a guildmaster who keeps no shop. |
| 23 | **Skill cap and Resist are client-enforced only** | Server-side conflict; only the profile enforces them. |
| 24 | **Engine-era divergence** | Source-X vs an era Sphere. Combat/spell/skill-gain formulas are engine-level and cannot be configured away. Raised, not investigated. |
| 25 | **One action in flight; `Journey` is 2-D** | Carried from M3. |
| 26 | ~~**`runtime/` is not version-controlled**~~ | **CLOSED in M3.8** — `runtime/` is now a repository (`revolution-runtime`, `8ae010c`). See §3. |

---

## 3. Debt #26 closed — server configuration now has provenance

`runtime/` is a repository as of `8ae010c`, branch `revolution-runtime`.

This was worth doing before Phase 6 rather than after. Phase 6 has to resolve
`ReagentsRequired=0` (`sphere.ini:1060`) — the setting that makes this shard cast
every spell for free. Changing the single most consequential authenticity switch
on the server, in a file with no commit trail, would have repeated exactly the
condition that made the conflict hard to audit in the first place.

**Tracked** — 5 files, the configuration surface:

| Path | Why |
|---|---|
| `sphere.ini` | the authenticity surface itself |
| `sphere.ini.baseline` | untouched original, for diffing |
| `sphereCrypt.ini` | client encryption table (public Sphere data) |
| `save/spherestatics.scp` | see below |
| `.gitignore` | the rules themselves |

**Deliberately not tracked:**

* **`accounts/` — never.** `sphereaccu.scp` holds **15 plaintext account
  passwords**, including every bot account, because the shard runs
  `Md5Passwords=0`. Verified excluded before the first commit.
* **`mul/`** — ~120 MB of copyrighted Ultima Online client data.
* **`save/`, `logs/`, binaries** — the world save rotates ~2.7 MB per file on
  every save tick; tracking it would add tens of megabytes of noise per day and
  record nothing a human would read.
* **`scripts/`** — its own repository (`revolution-runtime`, `be7c185`).
  Tracking it from here would duplicate it or force a submodule.

**One deliberate exception.** `save/spherestatics.scp` is committed. It holds
the 27,358 decorator statics — 107 forges, 54 anvils, 20 spinning wheels, 33
looms, 1,848 doors, 468 signs — that made crafting reachable at all. It is
reproducible (`WORLD_DECORATOR_INIT.md` is a runbook), slow to reproduce, and
catastrophic to lose silently. Unlike the rest of `save/` it changes only when
someone runs `.serv.savestatics`. 2.2 MB is cheap insurance for the single most
consequential change ever made to this world.

**Security note, recorded rather than acted on:** `Md5Passwords=0` means the
shard stores account passwords in plaintext. That is a local-only development
shard on `127.0.0.1`, so the exposure is small, but it is why `accounts/` can
never be committed and why it is worth knowing before this server is ever
exposed beyond localhost.

---

*Sections 4 onward are written as each phase completes.*


---

## 4. Results

| Phase | Result | Evidence |
|---|---|---|
| **0** debt register | **PASS** | §2, 26 items classified |
| **1** interior navigation | **FIXED (different defect)** | mobiles now soft; §4.1 |
| **2** side-effect-free perception | **PASS** | `finished (34 steps)`, 6 assertions |
| **3** cotton → thread → stitched | **PASS** | `0 → 6` thread, a shirt made |
| **4** missing craft recipes | **PASS** | nails restored, Parts menu 3 → 4 |
| **5** travel-mode auto-selection | **PASS (selection)** | planner chose a moongate unprompted; Recall execution is debt |
| **6** reagent authenticity | **PASS (consumption)** | `ReagentsRequired 0 → 1`, `bed3a93` |
| **7** taming authenticity | **PASS** | 16 rules, bot refuses illegal tames |
| **8** mount primitives | **PASS** | `ShouldUseMountForTravel`, tested |
| **9** machine-readable data | **PASS** | 4 TSVs, drift test |
| **10** live menu as oracle | **PASS** | `ChooseDialogByName` / `menu_pick`, by name not index |
| **11** regression / soak | **PARTIAL** | 9/9 suites; no multi-bot soak |

### 4.1 Phase 1 — the brief's premise did not survive contact

The brief describes decorated interiors trapping a bot because dynamic
furniture is invisible to a statics-derived navgrid. **That is not what is
wrong.** `PathRequest.dynamicItems` is already populated from the client's own
item cache on every plan, both overlays already skip doors so they stay
interactable, and the architecture the brief asks for already exists.

The M3.7 failure does not reproduce. A run standing on the exact stuck tile
(2498,548), asking for the exact unreachable target (2472,536), and walking all
three failed escape rungs finished in 44 steps with zero errors.

What has no expiry is `IsMobileBlocking` — deliberately, because a stationary
mobile never resends `0x77` and expiring it would blind A* to a real blocker.
So the real defect is narrower and different: **a transient mobile can act as a
permanent wall**, which a crowded bank produces and a quiet one does not.

What this milestone added is the ability to tell those apart. A failed search
now classifies all eight neighbours of the start cell as
open / terrain / mobile / dynamic / door / blacklist, and counts closed doors
separately because `BotStepNeedsDoorOpen` can open one — a start whose only
exits are doors is a knock, not a trap. M3.7 could only guess.

**The fix itself is not written.** Carried as a blocker.

### 4.2 Two world-content discoveries

Both are the same shape as M3.7's "the decorator had never been run", and both
were invisible until something tried to use them.

**No crops existed.** Zero `t_crops` in the world — not few, none. Phase 3 was
impossible: the only cotton was 18 stray piles on unreachable north-west cliffs,
and a weaver sent there could not route to any of three candidate tiles.
`f_worldgen_create_crops3k` planted 3,104 crops including 355 cotton, nearest
229 tiles from Britain. World save 2.75 → 3.01 MB.

**No monsters exist.** 1,479 creatures and 1,914 spawners, all peaceful fauna
and town NPCs. **Two** hostile spawners in the entire world (one red dragon, one
balron); zero orc/skeleton/lizardman/elemental spawners. The shard ships 28
Felucca dungeon spawn files — Covetous, Deceit, Destard, Hythloth, OrcCaves,
Khaldun — and `spheretables.scp:151` loads them, so every function is callable.
They were never called.

This is **not** deferred laziness: PvM, treasure hunting and every combat
archetype are *impossible*, not merely unproven, and monster density is an
authenticity question (Revolution's rates, not stock Nerun's) that deserves its
own scoped decision. Running them now would also have sabotaged this milestone —
`IsMobileBlocking` treats any mobile as a blocker, so a wandering orc in a
doorway is indistinguishable from the navigation bug under investigation, and
our crafters have near-zero combat skill on a full-loot shard.

### 4.3 Service names resolve to guild halls that do not trade

Third occurrence, now registered as its own blocker. `travel_service tinker`
arrives at Britain (1422,1654) to find one mobile: *"Justine, the engineer
guildmistress"*. `Vendors_spawns_felucca.scp:28` places **only**
`tinkerguildmaster` there — Britain has a tinker guild and no tinker. The real
tinker is Rhyssa at (2458,455) in Minoc.

M3.6 hit this with blank runes, M3.7 with the mage shop, M3.8 with tinker tools.
**Autonomous acquisition cannot rely on `travel_service` alone.**

### 4.4 The resolver's world is the client's cache

Cost two runs in this milestone and one in M3.7, in two different subsystems.

`world_graphic` resolved `wheel = 0x00000000` standing at the shop door six
tiles from a spinning wheel, where M3.7 got `0x40000478` from the same spot. An
item the server has not sent is an item that does not exist to the bot, and
which objects are cached depends on what happened during the walk. The M3.7
sibling was `pack_graphic` returning a consumed stack because only an `0x3C`
refills the container snapshot.

**Rule for scenarios: stand ON the station, not near it.**

---

## 5. What is left

1. **Phase 1's actual fix** — transient mobiles must not act as permanent walls.
   Diagnosis and instrumentation are in; the behaviour change is not.
2. **Phase 5** — travel-mode auto-selection. Wider than the brief states:
   moongates are default-off too (`travelUseMoongates_ = false`,
   `Client.h:1207`) and are only considered when a scenario opts in. The planner
   owns modes it will not choose.
3. **Phase 10 as a general path** — the live menu is used as an oracle in
   scenarios and proven repeatedly, but craft execution is not routed through it
   as a rule.
4. **Phase 11 soak** — 9/9 suites pass; no multi-bot soak has run.
5. **Player hunger** — requested by the owner, not in the brief. `MAXFOOD=15`
   (`c_man FOODTYPE=15`), warning below 40%, `HitsHungerLoss` currently
   commented out and `Regen3` at one point per real day. Not landed, because
   **no bot eats**: enabling starvation without an eating primitive would kill
   the characters carrying the economy proofs on a full-loot shard.
6. **Monster/dungeon spawns** — see §4.2.
7. **Reagent SOURCING** — consumption is resolved; where a bot legitimately buys
   reagents stays UNKNOWN, and the policy still refuses NPC reagent purchases
   while recording the gap.


---

## 6. Closed after the first report

### 6.1 Phase 1 — the real defect, fixed

Mobiles are now **soft** obstacles and terrain and furniture stay **hard**.

A cached mobile never expires, deliberately: a stationary one never resends
`0x77`, so a stale `seenMs` does not mean it left, and expiring it would blind
A* to a real blocker. But it also walls a doorway forever behind a bystander who
has since wandered off — which is how M3.7 lost a miner in the Minoc bank.

The resolution is not a timeout. It is that **being wrong about a mobile is
cheap** — the server rejects one step and the existing reject/reroute path
absorbs it — while being wrong about a wall costs the run. So a failed plan that
finds the character enclosed with mobiles among the walls replans **once** with
them ignored. The latch clears only on a successful plan, so a genuinely
unreachable goal still fails in finite time, and because furniture stays hard
the retry can never walk a bot into a building.

### 6.2 Phase 5 — the planner chooses, and explains

`travelmode::Choose` had existed since M3.6 and **nothing called it**. Wired now,
and proven with no `use_moongates` anywhere in the scenario:

```
[travel] mode moongate         usable   <- chosen
[travel] mode walk             usable
[travel] mode loose_rune_recall no: no rune marked at that destination
[travel] mode runebook_recall  no: no runebook page for that destination
```

That is also the fallback proof: the Recall arms are correctly unusable and the
journey fell through to a gate. **Recall execution is not wired** — runebook
interaction is gump-driven in scenarios, not a client action — and is recorded
as debt rather than claimed.

### 6.3 Phase 10 — by name, never by index

`ChooseDialogByName` and the `menu_pick` op resolve a craft entry against the
list **the server actually sent**, case-insensitively and by substring so a
caller asks for `nails` rather than for `nails (1 iron ingot)`. A miss logs the
entire live menu, because the interesting question is never "it failed" but
"what was on offer" — and the answer is usually that a material is missing and
Sphere filtered the entry out.

### 6.4 Hunger — enabled, and labelled as an owner decision

`HitsHungerLoss=1`, `Regen3=1800`. **No Revolution evidence either way** on
player starvation, so this is recorded as the owner's call, not a finding.

Two settings, because one alone does nothing: at the old `Regen3` of 86400s a
character with `MAXFOOD=15` took fifteen days to grow hungry, so enabling
starvation alone would have looked like a working feature and changed nothing.
At 1800s it is hungry in ~4.5 hours and empty in ~7.5.

And an engine fact that settles a Phase 8 question: **pets are exempt**
(`IsStatFlag(STATF_PET)` returns early), so a tamed animal never gets hungry and
owning a mount needs no feeding behaviour.

### 6.5 Dungeons populated

`spawn_dungeons_felucca` — fourteen dungeons. Spawners **1,914 → 3,033**, with
**356 hostile spawn ids** (zombies, earth/fire/water elementals, balrons,
skeleton knights, liches, orcs). PvM, treasure and combat archetypes are now
*possible* where before they were impossible.

Deliberately **excluded**: `Outdoors_spawns_felucca`, which would put monsters on
the roads our crafters walk on a full-loot shard, and `Reagents_spawns_felucca`,
because planting reagent nodes would quietly answer the sourcing question that is
still open.

**Density remains an authenticity question.** These are Nerun's stock UO rates,
not Revolution's. Running them makes PvM possible; it does not make it authentic.

---

## 7. Deliberately still UNKNOWN

Recorded rather than guessed, per the brief's stop conditions.

1. **Reagent sourcing.** Consumption is resolved on a dated primary source.
   *Where* a bot legitimately buys reagents is not, and the vendor policy still
   refuses NPC reagent purchases while logging the gap. Planting reagent nodes
   would have manufactured an answer.
2. **Stat cap.** Unknown since M3.5.
3. **Monster density.** Stock rates in place; Revolution's own rates unknown.
4. **Runebook Recall execution**, and legitimate Runebook crafting.
5. **Multi-bot soak.** 9/9 suites pass and several two- and three-bot runs
   completed cleanly this milestone, but no sustained soak has been run.
