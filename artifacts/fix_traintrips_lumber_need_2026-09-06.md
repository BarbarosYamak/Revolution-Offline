# TRAIN_AT_NPC trip accounting + a lumberjack that logs — 2026-09-06 (bot-brain)

Base HEAD 82b30de. Build `python tools/rev.py build test` -> ctest **45/45**
before and after. Smoke `python tools/rev.py gates CHARS=Kharain,Vorar
MINUTES=5` (18:39-18:45) and a second Vorar-only gate (19:02-19:07) after the
one follow-up fix below.

---

## ITEM 1 — TRAIN_AT_NPC failed without travelling

### Root cause (verified, source + wave-2 log)

`trainTrips_` is a Runner member with no owner. Two separate faults:

1. **It survived goal supersession.** `Runner::LeaveGoal` (runner/Core.cpp)
   hands back `vendorChases_`, `travelInFlight_`, `travelAttempts_` and every
   errand, but never `trainTrips_`. `huntTrips_` has an equivalent
   (`HandOffFromHunt`, Train.cpp:120-124); the trainer errand had none.
   Kharain wave 2: BUY_SUPPLIES superseded TRAIN_AT_NPC at 02:19:19 and
   02:21:19, so the goal resumed on its ceiling and printed
   `goal_failed=TRAIN_AT_NPC reason="no 'tinker' reachable after 3 trips"` at
   02:13:44 and 02:28:28 with no travel in between (spinning x4). Cited by
   navigation-world in `artifacts/fix_atlas_lumber_stale_npc_2026-09-06.md`,
   section "Separate defect, NOT this seam".
2. **It counted non-trips.** `++trainTrips_` fired at the TOP of the
   no-trainer branch — before a route was planned — and the stale-note branch
   then had to `--trainTrips_` itself back out. A `TravelToPoint` /
   `TravelToServiceSkipping` that the router refused still spent an allowance.
   That is the D9 rule (`Train.cpp:577-585`, "a trip is a journey that was
   actually started") applied to the hunt but never to the trainer.

### Change

`src/life/runner/Core.cpp` — `LeaveGoal`: `trainTrips_ = 0` when a REAL goal
change (the Corran same-kind re-pick returns earlier) has TrainAtNpc on either
side. `trainerShopsTried_` is deliberately NOT cleared: a shop already visited
and found empty is knowledge this session earned, not an allowance.

`src/life/runner/Train.cpp` — `DoTrainAtNpc`:
- the increment moved to the ARRIVAL branch (`++trainTrips_` beside
  `training: arrived at %d,%d -- asking who is here (trip %d of %d)`), so a
  trip is spent only when the character genuinely stood somewhere and found
  nobody of the trade;
- the `--trainTrips_` unwind on the stale-note branch is gone (nothing to
  unwind);
- `trainTrips_ = 0` when a new target NPC is adopted (`trainerSerial_ !=
  trainer`) — the search succeeded, so the allowance is handed back, the same
  thing the refusal path already did.

### Proof

`tests/life_harness.cpp`, new scenario
`ScenarioTrainTripsAreHandedBackOnAGoalChange` (`D1 the trainer trip allowance
does not survive a goal change`), driving the real `Runner::LeaveGoal` through
`RunnerHarnessAccess`:

    trips 3 before TRAIN_AT_NPC->BANK, 0 after
    trips after BANK->TRAIN_AT_NPC: 0
    trips after a same-kind re-pick: 2

Four checks: allowance handed back both directions, a resumed goal is not on
its ceiling, and the Corran same-kind re-pick still keeps what it spent.

### NOT PROVEN LIVE

Kharain (miner_smith, Minoc) never picked TRAIN_AT_NPC in the 5-minute window
— MINE / SMELT / EARN_GOLD / BANK only (`run_gates/g_Kharain.console.txt`
:135,145,154,207,423,467,477). Same outcome navigation-world reported for the
same character at 18:19-18:26. The change is unit-proven, not runtime-proven.

---

## ITEM 2 — a lumberjack that never logged

### (a) the gather need at the stand

Root cause, from `run_gates/g_Vorar.console.txt` (18:19 run, cited by
navigation-world): Vorar walked 216 tiles to Britain Territory woods, ARRIVED
at (1334,1496) at 18:20:26 with a hatchet in hand and an empty pack, and cut
nothing. `NeedLogs` was a flat 0.40 whatever the character was standing on:
0.40 x 130 = 52, +20 axe = 72.0, against REPLACE_EQUIPMENT 130 for a
sixteen-bandage top-up at full health. `session_summary ... logs=+0`.

Change, both halves modelled on rules this codebase already states:

- `src/life/Needs.cpp` (NeedLogs) — `obs.atWorkSite ? 0.65 : 0.40`, tapered to
  a 0.15 floor by the glut, exactly as NeedOre does thirty lines below. The
  glut line is not a new constant: it is the same `keepOfOwnOutput` x2 that
  GATHER_LOGS' own surplus damper reads (Goals.cpp), pack AND bank.
- `src/life/Goals.cpp` (GatherLogs modifier) — `+60` for "standing in the
  trees with the axe and room to carry", gated on `atWorkSite && axeEquipped
  && !surplusDamped && WeightFraction() <= 0.7`. Same claim, same number and
  the same shape as the Bank case's "already standing at the bank +60"
  directly below it: the walk is already paid for.

### (b) the seed marker was not per-category

`Runner::SeedNewbieKnowledge` (runner/Core.cpp:1001+) skipped on
`newbie_knowledge_seeded` + a known bank. The bank clause exists because the
marker predated bank seeding; the resource lead needed the same treatment and
never got it. `data/revolution_atlas.txt` carried ZERO lumber PLACE rows for
two atlas revisions, so every lumberjack seeded in that window wrote the
marker having learned no forest, and the marker then locked the omission in
for the life of the character.

Change: the gate now also requires that, when the profession gathers
something, at least one `KnownResourceSource` of that category exists. Seeding
is idempotent (NotePlace/HintResource dedupe on kind+location), so a re-run
costs a few atlas lookups and cannot duplicate anything. Applies to the
minocMiner branch too.

### (c) follow-up surfaced BY the fix, and fixed

Vorar's first post-fix gate (18:39-18:45) died. TRAIN_COMBAT started the
graveyard walk at 18:39:13; GATHER_LOGS took the goal at 18:39:33 (94.5 vs
58.5) and armed the axe, but the hunting walk was never cancelled — it
replanned at 18:39:55, carried him 168 more tiles, ARRIVED at the graveyard
at 18:40:52 and he was dead at 18:40:58, losing the hatchet and with it every
log the session could have cut (`:63-66,145,225,300,307`;
`sphere2026-09-06.log:514071  18:40:P'Vorar' was killed by N'Lich'`).

`src/life/runner/Core.cpp` — `LeaveGoal`'s existing "the walk to a fight is
never nearly there and worth finishing" rule (previously Bank / Heal /
ReplaceEquipment) now includes GatherLogs, with its own abort text. Still a
list, not "any goal change": a shop or a practice errand can legitimately be
served on the way.

### Live proof (`run_gates/g_Vorar.console.txt`, 19:02-19:07 gate)

    :43   newbie knowledge: 0 place(s) and 2 resource hint(s) seeded near home (Britain)
          (18:39 run -- an already-seeded life re-seeded because it knew no lumber)
    :102  goal_changed=GATHER_LOGS from=REPLACE_EQUIPMENT
          reason="GATHER_LOGS 174.5 superseded REPLACE_EQUIPMENT 130.0"
    :117  gather: nothing proven yet -- trying Britain Territory woods at 1320,1480
    :319  first logs gathered at 1326,1483 (pack now holds 2)
    :709  session_summary duration=325s goals=1/7 logs=+4 kills=0 deaths=0

174.5 = 84.5 need + 20 axe + 10 known-forest + 60 at the stand. The boost is
position-dependent, not a blanket raise: while WALKING the same character
scored 82.0 and correctly yielded to REPLACE_EQUIPMENT (`:157`).

### Cross-check

`grep "was killed by" runtime/logs/sphere2026-09-06.log` for the two
characters: one hit only, Vorar / Lich at 18:40 — the pre-(c) run. Nothing in
the 19:02-19:07 window and nothing for Kharain.

---

## Tests touched (and why, in full)

`tests/m4_life.cpp` — two existing expectations encoded the flat 0.40 that
this brief deliberately replaces. Neither invariant was weakened:

- `TestGatherLogsSurplusYieldsToTrade`: 113 logs is past 2x keep, so the need
  now reads its 0.15 floor (was 0.40) and GATHER_LOGS scores 19.5 (was 52).
  Every assertion the test exists for — TRADE outscores GATHER, the damper
  explains itself — still holds and holds harder.
- the planner hysteresis block: a character standing in the trees is
  deliberately no longer a near-tie with anything. The tie observation now
  sets `atWorkSite=false` ("between stands"), which restores the original
  numbers exactly; both hysteresis assertions (`bank > gather` AND
  `bank < gather * 1.25`) still bind, so the near-tie is real.

New: `TestStandingInTheWoodsOutscoresAComfortErrand` — the Vorar case as a
unit test. `GATHER_LOGS 164.5 vs REPLACE_EQUIPMENT 130.0`, asserted to beat it
by more than `PlannerConfig::incumbentBonus` (0.15), plus the negative half:
step off the stand and the need is back to 0.40.

---

## Open, NOT touched (outside this brief)

- **Kharain cannot wind down from inside Minoc Mine 1.** From (2569,479),
  `PickServicePlace` answers `[travel] no place offers banker` 56 times and
  wind-down loops "safe logout blocked ... remaining online and retrying"
  (`g_Kharain.err.txt`, `g_Kharain.console.txt:985-990`). The same bank
  lookup succeeded from Minoc town twenty minutes earlier (`:164`, "Minoc
  banker at 2503,552"). Mine-interior reachability, navigation-world's seam.
  The 5-minute gate never terminated and was killed manually.
- Vorar's `.err.txt` still shows the tile-A* leg budget exhausting near the
  Britain graveyard wall (navigation-world's own note, ITEM 1 "New defect").
