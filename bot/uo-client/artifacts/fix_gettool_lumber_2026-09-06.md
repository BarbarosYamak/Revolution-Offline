# GET_TOOL spin + lumberjack log source — 2026-09-06 (bot-brain)

Base HEAD 63ec014. Build+ctest 45/45. Smoke: `rev.py gates CHARS=Aurelius,Vorar MINUTES=5` (17:56-18:03).

## DEFECT 1 — GET_TOOL spins when a shop list lacks the tool  (FIXED)

### Evidence of the defect
The 16:57 `run_gates/g_Aurelius.console.txt` named in the brief was overwritten
by later gates; the same failure survives verbatim for another character:

- `artifacts/fleet_ramp_20260906/Odessa.console.txt:2925,2930,2939,2945,2957,
  2982,2997,3008,3020,3031,3048,3059,3188,3190,3193,3202,3209`
  `goal_blocked=GET_TOOL reason="REFUSE_VENDOR_NOT_OBSERVED" this 3-item list has no tinker tools`
  — one keeper's window reopened 22x at 8 s, plus `goal_spinning=GET_TOOL`.
- Same family, money arm: `run_gates/g_Cyras.console.txt:2954`,
  `run_gates/g_Delras.console.txt:3233` `goal_spinning=GET_TOOL`.

### Addressee check (the "one NPC is not the trade" half)
The table row is already right; the shard confirms it:
- `runtime/scripts/templates/tm_vend.scp:1794` `[TEMPLATE VENDOR_S_WEAPONS_BLADED]`,
  `:1804` `SELL=i_dagger,{10 15}`.
- carried by `runtime/scripts/npcs/c_vendor_human.scp:1238` (`[CHARDEF c_blacksmith]`,
  1150), `:1338` (`c_blacksmith_f`, 1255), `:6035` (`c_weaponsmith_blade`, 5956),
  `:6117` (`c_weaponsmith_blade_f`, 6040).
- `src/life/runner/Gear.cpp` kToolVendors: `{"dagger","weaponsmith",Service::Blacksmith}`
  — correct trade, correct service. So a 4-item window is ONE keeper's short
  restock, not "the trade cannot sell it".

The bug was therefore not the trade lookup but the terminal branch: it logged
`goal_blocked`, kept the stale offer, waited 8 s and asked the SAME keeper
again, forever, and never failed the goal.

### Fix
`src/life/runner/Gear.cpp` (`Runner::DoGetTool`) + `src/life/Runner.h`:
1. new `toolVendorsTried_` / `toolVendorsTriedFor_` skip list, cleared when the
   tool sought changes and when one is bought;
2. the keeper lookup now passes that skip list to the existing
   `Client::NearestShopkeeperWithTrade(trade, svc, skip)` (`src/Client.h:446`);
3. the terminal branch records the keeper, calls `Client::ForgetVendorOffer()`
   (`src/Client.h:408`) so the read-and-useless window stops naming itself,
   then walks up to / opens the NEXT keeper of that trade;
4. when no unasked keeper of the trade is in sight it is now
   `goal_failed=GET_TOOL reason="REFUSE_VENDOR_NOT_OBSERVED" N <trade> asked and
   none stocked a <tool>` + `planner_.Cooldown(GetTool, +kNoToolCooldownMs)` +
   `planner_.Finish(false, ...)` — the same stand-down BUY_SUPPLIES already uses
   for the identical fact (`src/life/runner/Economy.cpp:2577-2589`).

### Priority half — already in at HEAD, not re-done
`src/life/Needs.cpp:785-786` damps a mage's Poisoning dagger to urgency 0.10
(vs 0.9), landing GET_TOOL at ~52 instead of 468 against
`{GoalKind::GetTool, NeedKind::NeedTool, 520.0}` (`src/life/Goals.cpp:199`).
Landed at df1fe75; the practice ladder is kept.

### Smoke result
`run_gates/g_Aurelius.console.txt`: `goal_spinning` count 0; GET_TOOL picked
once (line 552) and stood down cleanly (566, 573). Aurelius died to a skeletal
knight/zombie at 17:57 (`runtime/logs/sphere2026-09-06.log`), so the session was
SURVIVE/HEAL/RECOVER_CORPSE dominated and the new next-keeper branch was NOT
exercised at runtime — the fix is proven by build+ctest and by absence of the
spin, not by a positive second-vendor purchase.

## DEFECT 2 — lumberjack with no logs source  (BLOCKED, hypothesis corrected)

`SeedNewbieKnowledge` is not the cause, and the prescribed data-only fallback
is not possible.

- `data/revolution_atlas.txt` has ZERO PLACE rows yielding lumber. The only
  values in the resources column are `hunting` (178), `reagents` (60),
  `mining` (25), `fishing` (17):
  `awk -F'\t' '$1=="PLACE"{print $10}' data/revolution_atlas.txt | sort | uniq -c`.
  So `Atlas::NearestPlaceWithResource(Lumber)` (`src/world/Atlas.cpp:432`) is
  null for EVERY home city, Yew included — `SeedNewbieKnowledge`
  (`src/life/NewbieKnowledge.cpp:78-99`) could not seed a lumber lead for
  anyone, and a nearest-forest fallback has no atlas category to read.
  `src/life/runner/Gather.cpp:212-218` already says this in a comment.
- Halain was not seeded either; he simply stood in Yew forest
  (`artifacts/fleet_ramp_20260906/Halain.console.txt:1123`
  `first logs gathered at 1320,1846`).
- The real Britain failure is the guard-line escape being too short. In
  today's smoke, `run_gates/g_Vorar.console.txt:262,285,307,331,459,566`
  `walking out to where trees can actually be worked` -> a 7-tile hop
  (`past_guard_line -> (1475,1604) r=4 from (1475,1611)`, ramp log line 261)
  -> `gather: trip reported success but there are no trees within 24 tiles`
  (306,329,447,565). Every landing is charged as a dead target, the leads run
  out, and `Client::TravelToResource` then answers
  `no known source of that resource` (`src/travel/ClientTravel.cpp:851`) —
  131x in the ramp run, logs=+0.
- Fixing it needs either new atlas lumber PLACE rows (atlasgen / navigation-world)
  or a longer guard-zone escape (`Client::StepOutOfGuardZone`,
  `src/Client.h:826`, `world/GuardZoneAdvance.h`). Both are outside a bot-brain
  data-only brief and would require inventing coordinates. Stopped per the rule.
