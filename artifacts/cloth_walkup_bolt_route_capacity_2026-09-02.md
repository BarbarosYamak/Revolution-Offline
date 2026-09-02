# Cloth chain: station walk-up, bolt provenance route, shear to capacity (2026-09-02)

Continues artifacts/domakecloth_shear_to_target_2026-09-02.md (defects 1-3).
Build: `python tools/rev.py build test` -> ctest 43/43.
Smoke: `python tools/rev.py gates CHARS=Aelia MINUTES=5`, 22:29:11-22:34:11,
run_gates/g_Aelia.console.txt (587 lines).

## 1. WALK-UP before the wheel and the loom  (implemented, NOT exercised live)

`Runner::ReachStation(client, obs, station, what)` — Runner.h:249-257 decl,
Runner.cpp (immediately above `DoMakeCloth`). Same shape as the forge approach
in `DoSmelt` (Runner.cpp:9479-9549), NOT a new mover:

* reach test `TileDist <= kStationReach` (2 — Source-X CCharStatus.cpp:1423
  refuses a use-target on `iDist > 2`; Aelia's loom timeout was Chebyshev 6);
* route with `client.TravelToPoint(standX, standY, 0, what)` to a WALKABLE
  neighbour tile of the station, never the station's own solid tile;
* two approaches to the same station, then strike it off
  (`clothDeadStations_`) and let `FindSpinWheel/FindLoom` offer the next one —
  Britain's tailor holds two wheels and two looms. Owner rule 2026-09-02,
  "1 try max 2";
* no walkable tile beside it at all -> struck off immediately, no approach
  spent.

Wiring: `FindLoom(client, 10, clothDeadStations_)` + `ReachStation(...,"loom")`
before `ActionUseItemOn(spun, loom)`; `FindSpinWheel(client, 10,
clothDeadStations_)` + `ReachStation(..., "spinning wheel")` before
`ActionUseItemOn(raw, wheel)`.

Supporting client primitives (both new, both trivial):
* `Client::WorldItemPosition(serial, x, y, z)` — Client.h:469-478,
  Client.cpp (beside `FindWorldItemByGraphic`). `items_` lookup; the mobile
  equivalent `MobilePosition` already existed, items had no public accessor.
* `Client::FindWorldItemByGraphic(graphic, maxDist, skip)` — skip-list
  overload so a struck-off station is passed over.

NOT PROVEN AT RUNTIME. MAKE_CLOTH was never selected in the smoke (section 4),
so no `cloth: the <station> at x,y is N tiles off -- standing at ...` line
exists yet.

## 2. BOLT PROVENANCE  (proven live)

`Production.cpp:67` ALREADY carried
`{"i_cloth_bolt", 1, Provenance::WorldProcessed, Station::Loom, Tool::None, ...}`
— the brief's "mark it WorldProcessed with the loom station" was already true,
so Production.cpp is unchanged and NO test assertion was adjusted
(tests/m37_economy.cpp:136-138 and tests/m4_life.cpp:2851-2860 assert the loom
station and 4-yarn input; both were already right and still pass).

The real gap was routing. `ProducingGoalFor` (Runner.cpp:8882-8900) knew ore ->
Mine, ingot -> Smelt, fish -> Fish, log/board -> GatherLogs, and everything
else -> Craft. Added:

    if (item == "i_yarn_ball" || item == "i_cloth_bolt" || item == "i_cloth")
        return GoalKind::MakeCloth;

which is what `DoCraft`'s existing WorldProcessed hand-off branch
(Runner.cpp:10139-10146) tests against.

Runtime, g_Aelia.console.txt:
  :438 reason: i_cloth_bolt: every input is in the pack
  :444 craft: i_cloth_bolt is not a menu craft (WORLD_PROCESSED) -- MAKE_CLOTH
       is the goal that makes it
  :445 handoff=CRAFT->MAKE_CLOTH reason="not a menu craft"
Zero `REFUSE_MISSING_RECIPE` lines in the session (previously 3 in 130 ms).

## 3. SHEAR TO CAPACITY  (implemented, NOT exercised live)

Owner rule 2026-09-02, "they should work till carry capacity".

`kGathererPackFullFrac = 0.95` — the gatherers' own existing bar, not a new
number: it was written three times as a literal (`DoGatherLogs`
`req.packFullFraction`, `DoFish`, `DoMine`). All three now read the named
constant, so shearing and the other three trades stop at the same point.

`DoMakeCloth` step 3 now leaves the flock when
`obs.WeightFraction() >= kGathererPackFullFrac` (or when step 4b's existing
60 s bare-flock wait expires), not when `ceil(yarnShort/3)` wool is carried.
The batch's wool figure survives only as the MINIMUM worth having and as the
"(batch wanted N)" figure in the log lines; the old
`woolTarget = min(woolTarget, biggestFlock)` ceiling clamp is deleted because
the regrow it guarded against is now handled by the bare-flock exit.

`clothHeadingToWheel_` (Runner.h) latches the decision to leave — set when the
pack fills or when 4b gives up on a bare flock, cleared when the wool is all
spun. Without it a character that left a bare flock with half a load and
arrived in town with the wheel still outside item range would read "room left,
keep shearing" and walk straight back to the pasture.

No banking goal was added; spare wool/yarn is the existing BANK goal's job.

## 4. WHY THE SMOKE COULD NOT EXERCISE 1 AND 3 — new defect, outside this brief

Aelia's pack at the start of the run (tools/world_query.py --char Aelia):
`i_wool x5, i_yarn_ball x6, i_scissors, i_sewing_kit, i_robe, gold 0`.

With 6 yarn in the pack the tailor's craft focus is `i_cloth_bolt` itself —
`Professions.cpp:1315` lists it first in the tailor's `produces` — and every
input for it is already carried. Two consequences:

1. `Needs.cpp:857-864` builds `clothShort` from the chosen recipe's MISSING
   list. Nothing is missing, so NeedCloth is never added, so MAKE_CLOTH is not
   in the planner's list at all — it does not even appear as a BLOCKED_NEED
   (:446-455). `HandOff` only cools the origin goal down and lets the planner
   re-pick (Runner.cpp:2938-2946); it cannot pin a goal that no need scores.
   The session went CRAFT -> (handoff) -> PRACTICE_SKILL -> EARN_GOLD.
2. Even if MAKE_CLOTH were selected, `DoMakeCloth`'s own "enough for the batch"
   gate reads the same empty missing list and would `Finish(true)` before
   reaching the loom step.

So the chain is now stalled one link later than before: the bolt is correctly
identified as loom work and correctly handed to MAKE_CLOTH, and then nothing
picks MAKE_CLOTH up. The fix is in Needs.cpp / the craft-focus choice
(NeedCloth should also fire when the chosen craft OUTPUT is itself a wool-chain
item that is not in the pack), both of which this brief forbade touching.

## 5. Sewing-kit strings — still not reached

Grepped run_gates/g_Aelia.console.txt case-insensitively for "sewing kit",
"sash", "Misc.": 14 hits, ALL of them the `tools ... held=[sewing kit,scissors,]`
field inside `needs considered` dumps. None of "double-clicking the sewing
kit", "giving the sewing kit cursor", "chose 'Misc.'" or a sash craft appear.
Today's Identity/Runner sewing edits remain unexercised.

## Gates

* python tools/rev.py build test -> ctest 43/43 pass, 0 fail.
* python tools/rev.py gates CHARS=Aelia MINUTES=5 -> session_summary
  duration=300s goals=5/14, gold 0->0, no crash, logout_complete
  (g_Aelia.console.txt:578-579).
