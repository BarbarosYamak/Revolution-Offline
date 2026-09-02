# NeedCloth fires on unwoven yarn; MAKE_CLOTH is finally picked (2026-09-02)

Continues artifacts/cloth_walkup_bolt_route_capacity_2026-09-02.md section 4.
Build: `python tools/rev.py build test` -> ctest 43/43, m4_life 594 checks / 0
failures. Smoke: `python tools/rev.py gates CHARS=Aelia MINUTES=5`,
22:47:26-22:52:26, run_gates/g_Aelia.console.txt (420 lines).

## 1. Needs.cpp -- the output-is-the-cloth shortfall (PROVEN LIVE)

`AssessNeeds` built `clothShort` only from the chosen recipe's MISSING input
list. When the chosen OUTPUT is itself a wool-chain item -- the tailor's
`produces` opens with i_cloth_bolt (Professions.cpp:1315), whose only input is
four yarn -- six yarn in the pack made that list empty, so NeedCloth never
appeared at all, not even as a BLOCKED_NEED.

Added, immediately after the existing missing-input loop (Needs.cpp ~857-900):

* if no wool-chain INPUT is short, and `craft.item` is itself
  `IsWoolChainMaterial` and `craft.skillsMet`, then the shortfall is
  `max(1, cfg.craftBatch) - QtyIn(obs.pack, craft.item)` when positive;
* `clothWantQty` (new local) carries the denominator for the urgency fraction,
  so "a full batch short scores 0.55" still holds for both shapes
  (`craftBatch * 4` yarn for the input case, `craftBatch` bolts for the output
  case). No weight was re-tuned.
* Rule B is untouched: `asked || broke || noTime` still decides between
  urgency 0.0 + BLOCKED (ask the players first) and a real score.
* The gate is the OUTPUT being on the wool chain, not a profession id. Across
  the whole catalogue only the tailor `produces` a wool-chain item; every other
  trade names wool/yarn/cloth/thread under `consumes` (Professions.cpp:1229,
  1325, 1397). So this is tailor-only by construction, and nothing here buys
  cloth from an NPC.

Runtime, g_Aelia.console.txt:332-346 (previous run: NeedCloth absent entirely):

    needs considered: NeedCloth(cloth 0.55) ...
    goal=MAKE_CLOTH reason="previous goal abandoned: ..."
      reason: NeedCloth urgency 0.55 x 135 = 74.2
      reason: 5 x i_cloth_bolt short, 0 gold on hand, reserve 400
    cloth: 6 yarn and no loom in sight -- going to the tailor, where the looms are

That is the first time MAKE_CLOTH has ever been selected by the planner for
this character. Session histogram: `MAKE_CLOTH=1(50%)` (:412).

## 2. Runner.cpp -- the DoMakeCloth "enough for the batch" gate (NOT EXERCISED)

Same relaxation, immediately above the existing `if (!stillShort)`
(Runner.cpp ~13355): when the chosen output is a wool-chain item, the pack
holds fewer than `craftBatch` of it, and `yarn >= kYarnPerBolt`, `stillShort`
is forced true and `woolTarget` drops to 1 (there is nothing to shear -- the
yarn is already carried), so step 2 takes the turn instead of `Finish(true)`.
Logged through a NEW errand tag "weaving" rather than "cloth", because
`LogErrandReason` keeps one sentinel per tag and two alternating reasons under
one tag defeat the repeat throttle.

NOT REACHED in this smoke. `DoMakeCloth` asks `ChooseCraft` with
`needCfg_.craftBatch` (5), and a batch of five bolts wants 20 yarn against 6
carried -- so the ORIGINAL missing-input loop already set `stillShort`. The new
branch only matters when the batch's yarn is fully carried. Needs a live run
with >= 4*craftBatch yarn, or a Runner-level harness, to exercise.

## 3. Test (tests/m4_life.cpp, `TestYarnInThePackIsWorkNotStock`)

Aelia's real pack shape from tools/world_query.py --char Aelia: 6 yarn, 0
bolts, sewing kit + scissors, gold 0. Asserts

* NeedCloth exists, unblocked, urgency > 0, evidence names `i_cloth_bolt`;
* `Planner::Select` over the FULL AssessNeeds output lands on `MAKE_CLOTH`;
* with `craftBatch` bolts already in the pack there is no NeedCloth at all.

`obs.healPotions = 4` is part of the setup, not a fudge: without it a crafter
raises NeedEquipment("heal potions") at 0.50 and REPLACE_EQUIPMENT (family
Upkeep) outranks MAKE_CLOTH. That is the live behaviour too -- see defect 2.

## 4. Defects found, all OUTSIDE this brief

**(a) Arriving at the tailor is not arriving at the loom.** MAKE_CLOTH travels
to place "Britain tailor", target (1467,1686) r=5, and ARRIVED at (1462,1681)
(:340, :404). Britain's looms are at (1473,1685) and (1474,1685)
(Runner.cpp:895-897, measured from spherestatics.scp). Chebyshev (1462,1681) ->
(1473,1685) = **11**, and `FindLoom(client, 10, ...)` searches 10. So the loom
is one tile outside the search and the goal printed "6 yarn and no loom in
sight" at :338, :390, :406 and did nothing else for 100 s. `ReachStation` (the
walk-up added earlier today) is therefore STILL unexercised -- no
`cloth: the loom at x,y is N tiles off` line exists.
Fix belongs to the place record / travel radius or the station search radius,
not to Needs.cpp.

**(b) The goal neither retries nor stands down.** After `travel_done ok=1` the
handler re-enters, finds no loom, calls `TravelToPlace` again (which is a no-op
because it is already inside the radius), and re-throttles the same line. No
`goal_failed=`, no cooldown, no handoff. Same "goal addressed to nobody" shape
as the earlier defects.

**(c) REPLACE_EQUIPMENT at 0 gold is not blocked.** :102 REPLACE_EQUIPMENT
130.0 won the first 2m26s, walked to a healer, and only then discovered
"0 gold with a floor of 50 cannot buy one heal potion at 30" (:328-331). The
purse is known before the walk; NeedEquipment("heal potions") should carry the
same `noCapital` gate NeedSupplies already does (Needs.cpp:417-427).

## 5. Sewing-kit strings -- still not reached

Grepped g_Aelia.console.txt case-insensitively for "double-clicking the sewing
kit", "sewing kit cursor", "Misc.", "sash": ZERO hits (the earlier run's 14
hits were `tools ... held=[sewing kit,scissors,]` inside `needs considered`
dumps; this run's shorter console does not even contain those). Today's
Identity/Runner sewing edits remain unexercised -- blocked behind defect (a).

## Gates

* `python tools/rev.py build test` -> ctest 43/43 pass, 0 fail;
  build-m1/uo_client.exe relinked 2026-09-02 22:44:52.
* `python tools/rev.py gates CHARS=Aelia MINUTES=5` -> session_summary
  duration=300s goals=0/2, gold 0->0, no crash, logout_complete (:411, :420).
