# Armorer / practice-spellbook fix — 2026-09-05

## Defect A — fencer buys armour from the weaponsmith

Root cause: `src/life/runner/Train.cpp` (BuyScrollFrom's armour errand) called
`NearestShopkeeperWithTrade("armorer", svc)` with `svc = wm::Service::Blacksmith`
for the non-leather case. `ServiceForPaperdollJob` in
`src/travel/ClientTravel.cpp:1654-1658` maps `blacksmith`, `smith`, `armourer`,
`armorer` AND `weaponsmith` all onto `wm::Service::Blacksmith`, so the service
fallback inside `NearestShopkeeperWithTrade` (src/Client.cpp:2940) happily
matched a weaponsmith standing closer than the actual armourer.

Fix: primary lookup always passes `wm::Service::None` for the armour errand
(title must literally say "armorer"), and the existing blacksmith fallback
(only tried when no armorer is found and the piece isn't leather) also passes
`wm::Service::None` against the title `"blacksmith"` — so it can no longer
re-admit a weaponsmith either.

Live confirmation, `run_gates/g_Castor.console.txt` (2026-09-05 10:34-10:36):
- line 114-118: both "Rudd, the armorer" and "Eulalia, the weaponsmith" seen
  nearby.
- line 148-150: "asking the armorer to show a piece of armour" opens vendor
  `0x000011DA` = Rudd (the armorer), not Eulalia.
- line 194-202: 36-item offer including leather cap/gorget/sleeves/tunic/
  leggings/gloves, studded armor, leather armor.
- line 213, 293, 373...: multiple successful armour buys through the rest of
  the run.

## Defect B — mage practice reads an empty spellbook

Root cause (packet-level), traced in `run_gates/g_Aurelius.console.txt`
(2026-09-05, original evidence run):
- 10:22:18.110 STAT_FARM unequips the spellbook `0x4000EDD5` to go
  bare-handed (`[ITEM] drag serial=0x4000EDD5` then drop into backpack).
- 10:22:19.564 STAT_FARM re-equips it (`[ITEM] drag serial=0x4000EDD5`
  again, then `equip success`).
- Both the unequip and the re-equip are picks, and Sphere's pickup protocol
  sends a 0x1D (Delete Object) for the item's OLD slot before the new
  0x25/0x2E lands. `Client::OnDeleteObject` (src/Client.cpp) unconditionally
  ran `containerItems_.erase(serial)` for the picked-up item's OWN serial —
  which is exactly the cache of the SPELLBOOK'S contents, since the book is
  itself a container. Two lift/drop round-trips inside 1.5 seconds wiped the
  19-item cache FILL_SPELLBOOK had read at session start, and
  `spellbookOpened_` (shared across FILL_SPELLBOOK/PRACTICE_SKILL) stayed
  `true`, so PRACTICE_SKILL's open-gate never re-fired: "the book holds 0
  item(s) ... nothing safe to cast at myself -- standing down" at 10:22:24.

Fix (root cause), `src/Client.cpp` `OnDeleteObject`:
- Detect `ownLift = drag_.InFlight() && drag_.Serial() == serial` before
  resetting the drag state.
- Skip `containerItems_.erase(serial)` and the `openContainers_` removal for
  that serial when `ownLift` is true — the object is relocating (our own
  pickup), not leaving the world, so its own contents cache must survive.
  The unconditional "remove this serial from every OTHER container's list"
  loop is untouched (that part is genuinely stale once the item moves).

Fix (defense-in-depth), `src/life/runner/Train.cpp` `DoPracticeSkill` /
`src/life/Runner.h`:
- Added `practiceRecheckedBook_`. If the shared `spellbookOpened_` gate is
  already true but `ContainerItemCount(spellbookSerial) == 0`, PRACTICE_SKILL
  now re-opens the book once before believing it is genuinely empty, instead
  of trusting the stale latch. A book still empty after the re-open falls
  through to the existing, unweakened "nothing safe to cast" reasoning.

Verification:
- `python tools/rev.py build` — clean, `uo_client.exe` relinked.
- `python tools/rev.py test` — 43/43 ctest passed.
- `python tools/rev.py gates CHARS=Castor,Aurelius MINUTES=5` — Castor's run
  reproduces the armour fix live (above). Aurelius's 5-minute window did not
  route the planner through STAT_FARM's unequip-then-PRACTICE_SKILL sequence
  again (planner picked TRAIN_COMBAT/HEAL/FILL_SPELLBOOK/STAT_FARM-without-
  a-spar-partner instead), so the exact repro path was not re-exercised live
  in this window. The spellbook cache stayed correct (20 items) across the
  `use_object` opens that did occur (lines 238-243 of
  `run_gates/g_Aurelius.console.txt`, this run). The fix itself is a direct,
  minimal correction of the traced packet-order bug and does not weaken any
  existing check; ctest coverage of drag/equip/container-contents passed
  unchanged.
