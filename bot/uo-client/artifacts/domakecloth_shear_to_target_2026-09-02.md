# DoMakeCloth: shear to target, nearest flock, throttled travel line (2026-09-02)

Brief: bot/uo-client/src/life/Runner.cpp `Runner::DoMakeCloth`.
Prior evidence: artifacts/tailor_cannot_buy_now_2026-09-02.md downstream
defects 1-3. Five 5-minute gates on Aelia (tailor, RevGen3_12), 21:44-22:17.

## 1. Shear to the wool the batch actually needs

`WoolForShortfall(item, qty)` (Runner.cpp, anon namespace above DoMakeCloth)
converts the need's shortfall into sheep at the server's own rates:
wool -> 3 yarn (Source-X CClientTarg.cpp:2053), 4 yarn -> 1 bolt (:2230-2245),
1 bolt -> 50 cloth (:2147); rounding always up. `20 x i_yarn_ball short`
therefore becomes a wool target of 7. Floor 1, ceiling = the largest flock in
data/revolution_pastures.tsv (15), so the target never demands a regrow wait.

Step 3 (wool -> wheel) no longer leaves the flock the moment it holds any wool:
with no wheel in scan range AND `wool < woolTarget` it falls through to step 4
and keeps shearing.

Step 4 now asks for `NearestMobileWithBody(kSheepBody, 12, clothShornSheep_)`
(new Client.cpp overload). The old code, on finding the nearest sheep was one it
had sheared, cleared the list and walked to ANOTHER PASTURE — which is why a
flock of fifteen yielded one wool.

Runtime, run 2 (21:51-21:56), g_Aelia.console.txt:
  :76  cloth: shearing a sheep (1 wool carried, want 7)
  :90  cloth: wool 1->2 ...      (each shear confirmed by an inventory delta)
  :418 wool 2->3   :464 wool 3->4   :542 wool 4->5   :565 wool 5->6
  :639 wool 6->7
  :640 cloth: 7 wool (target 7) and no spinning wheel in sight -- going to the
       tailor
Seven `System: You put the piles of wool in your pack.` lines, one per shear.

## 2. Bare flock: a bounded wait, never a regrow wait

New step 4b, reached only after this character has sheared here (so it cannot
fire on the way in). Regrow is `g_Cfg.m_iWoolGrowthTime`
(CClientTarg.cpp:1890; CServerConfig.cpp:1372 parses MINUTES) and this shard
sets `WoolGrowthTime=30` at runtime/sphere.ini:399 — longer than a session, so
it is never waited on. What the 60 s budget buys is the flock roaming: the sheep
spawners carry `MAXDIST=50` (runtime/save/sphereworld.scp, i_worldgem_bit with
SPAWNID=c_sheep_woolly). When the budget is spent the character leaves with what
it has.

Runtime, run 2:
  :265 cloth: every sheep in reach is shorn (2 wool of 7) -- ... waiting 60s ...
  :281 cloth: the flock is shorn out -- taking 2 wool of 7 to the wheel  (60.8s)
  :465 same line at 4 wool -> :467 a sheep wandered into range 5 s later and
       shearing resumed. Both branches exercised.

## 3. Flock choice — picker fixed, content still missing near Britain

The picker walked `pastures[idx % size]` in FILE order, and pasturegen.py:108
sorts by count, so every character set off for the biggest flock (572,1096)
regardless of where it stood. Now distance-ranked from the character's current
position (std::stable_sort, count order as tie-break).

  run 1 :60  ... walking to the flock of 15 at 681,945, 790 tiles off (trip 1)
  run 1 :450 ... walking to the flock of 15 at 677,1177, 89 tiles off (trip 2)
(previously 572,1096 both times, ~1048 travel tiles from Britain)

MISSING CONTENT — not invented, reported. Derived from
runtime/save/sphereworld.scp with tools/pasturegen.py's own reader:
* 247 c_sheep* WORLDCHARs, 4 clusters of >=4. Chebyshev from Britain
  (1470,1608): 681,945 = 789; 677,1177 = 793; 572,1096 = 898; 5159,3915 = 3689.
  Nothing closer than 789 tiles.
* 41 sheep spawners (i_worldgem_bit, SPAWNID=c_sheep_woolly). The three nearest
  Britain are 1865,1379 (395), 1268,2018 (410), 1020,1360 (450) — but each
  carries `TAG.spawn_array="Boar,Cougar,Goat,Horse,Panther,Pig,Sheep"` with
  AMOUNT=7, so its slots are shared across seven species. Resolving each
  spawner's ADDOBJ serials against the save's WORLDCHARs: 3, 1 and 2 sheep
  respectively. No site within 780 tiles of Britain holds 4 sheep.
=> A wool flock near Britain is a sphere-expert spawn job. Not faked here, and
   pasturegen.py's MIN_FLOCK was left alone.

## 4. Travel line throttled

Both the "no spinning wheel" and "no loom" travel branches now go through
`LogErrandReason` (60 s / reason-change window, Runner.cpp:2843). Run 3
(21:57-22:02, the full Yew -> Britain walk carrying 7 wool): 2 occurrences of
`no spinning wheel in sight`, at 21:57:07 and 22:01:38. Previously ~110 lines
for the same walk.

## 5. Extension made to reach the verification: station DISPIDs

`FindWorldItemByGraphic(0x1015)` could not see Britain's spinning wheels. Sphere
keeps one itemdef per station and lists the other facings in a DUPELIST
(runtime/scripts/items/i_profession_tailor_tanner.scp:199 for i_spinning_wheel,
:267 for i_loom_upright); the DISPID that reaches the client is the dupe.
runtime/save/spherestatics.scp: the Britain tailor's wheels are 0x101C
(1473,1689 / 1475,1689) and its looms 0x1061 / 0x1062 (1473,1685 / 1474,1685).
Run 4 (22:03-22:08): Aelia stood at 1471,1690 — three tiles from a wheel — and
logged "no spinning wheel in sight" for the whole session.

Fixed with `kSpinWheelGraphics[] / kLoomGraphics[]` + `FindSpinWheel/FindLoom`
(Runner.cpp, near the graphic constants). Applied to both DoMakeCloth and
DoMakeBandages, which had the identical single-graphic lookup.

Run 5 (22:12-22:17), same character, same spot:
  :59  cloth: spinning wool into yarn (7 wool, 0 yarn)
  :69  System: You create some yarn.   :70 You put the balls of yarn in your pack
  :75  cloth: wool 7->6 yarn 0->3      (server-confirmed conversion, 3 per wool)
  :76/:85/:86 second spin, wool 6->5 yarn 3->6

## Open defects found, NOT fixed (outside this brief)

1. THE LOOM IS OUT OF REACH. Run 5 :368/:383/:397 `cloth: weaving 6 yarn at the
   loom` three times, each `use_item_on timeout` with NO server reply at all
   (:22:14:49.847, :15:04.949, :15:20). Target accepted: `[target] object
   0x40000472 (1473,1685,0) model 0x1061`. The character was at (1471,1691):
   Chebyshev 6, and Sphere's CanTouch refuses past 2. `FindLoom(client, 10)`
   finds the loom but DoMakeCloth never walks to it — the wheel happened to be
   2 tiles away, the loom is 6. Steps 2 and 3 need the same walk-up step 4
   already has. Goal stood down correctly (:411 `3 gestures in a row moved
   nothing`), so this is a stall, not a spin.
2. CRAFT i_cloth_bolt has no menu route. Run 5 :307/:316/:325
   `goal_failed=CRAFT reason="REFUSE_MISSING_RECIPE" no menu path known for
   i_cloth_bolt`, three times in 130 ms. A bolt is a LOOM output, not a craft
   menu row — the same class of mistake as WorldProcessed outputs. Identity.cpp
   / Production, not this brief.
3. CRAFT i_sash was never reached, so none of "double-clicking the sewing kit",
   "giving the sewing kit cursor", "chose 'Misc.'" or "sash" appear in any of
   the five runs. The chain stops at the loom (defect 1).

## Gates

python tools/rev.py build test -> ctest 43/43.
python tools/rev.py gates CHARS=Aelia MINUTES=5, five consecutive sessions.
