# Seven quick wins - 2026-09-05 (bot-core)

Baseline: ctest 44/44 before the change; 44/44 after (life_harness now 14 checks
incl. new S4, activity_buy 29, m4_life 739).

## Item 1 - bankDepositTries_ (SKIPPED: the counter IS reachable)
The review's "unreachable behind bankItemMoveFails_" does not hold.
`SettleBankItemMove` (src/life/runner/Bank.cpp:37-53) at kMaxBankItemMoveFails=3
only RESETS itself to 0 and delays 20 s -- it does not end the bank goal, so the
deposit loop continues and `++bankDepositTries_ > kMaxBankDepositTries` (Bank.cpp:329)
is reached on the 6th ask of the same item (fails 1,2,3 -> stand-down -> 4,5,6).
Second, reachable path: bankDepositTries_ is never reset on SUCCESS (only at
Bank.cpp:334 and when the item NAME changes at :341-342), so six successful
deposits of the same item in one session also trip the "this box is not really
open" branch. Left in place; the missing success-reset is reported as a defect,
not fixed here (out of the zero-behaviour-change brief).

## Item 2 - threshold dedup (2 of 3 done)
- Train.cpp hunt gate `obs.hp*100 < obs.hpMax*80` -> `obs.HpFraction() <
  needCfg_.healHpFraction`. healHpFraction is 0.80 for every profession
  (include/uo/life.h:1122; no assignment anywhere except Survive.cpp:645 copying
  it into a tune struct), so the value is provably identical.
- Train.cpp hunt gate `>= 0.7` -> `>= needCfg_.huntWeightFrac`. huntWeightFrac is
  0.70 for every profession (life.h:1143, no assignments) and is the same field
  Needs.cpp:1793/1838 reads for "can this life hunt".
- NOT changed, mismatch reported: `BankWeightLine(cfg)` (life.h:1208) returns
  min(huntWeightFrac, bankWeightFrac)=0.70 only for a hunter/caster and 0.85
  (bankWeightFrac, life.h:1135) otherwise -- so it is NOT 0.7 for every
  profession, and DoTrainCombat's gate would move for a non-hunting life.
- NOT changed: Goals.cpp:420 `obs.WeightFraction() > 0.7`. Planner::Score
  (Goals.cpp:305) takes needs/obs/mem only -- no NeedConfig -- so this literal
  cannot read the field without a signature change (out of scope).

## Item 3 - test targets on uo_client_core
tests/CMakeLists.txt: trade_verify and moongate_gump now
`target_link_libraries(... uo_client_core)` instead of compiling
${UO_CLIENT_SOURCES} privately. Both pass (ctest 43/44, 44/44 order unchanged).

## Item 4 - the bandage shop route ends when every known counter is dry
New: `Runner::BandageCountersAllDrained` + `Runner::StandDownBandageShopping`
(src/life/runner/Gear.cpp, decls in src/life/Runner.h). The check runs where a
FRESH visit would be begun, so the existing partial-buy path just returns and the
next tick decides. Predicate: at least one bandage counter drained THIS pass
(bandageCountersDrained_, incremented at the partial-buy NoteDrainedShelf) AND
`client.NearestMobileWithTrade("healer"/"veterinarian", drained)` finds nobody
left. Knowing no counter at all is deliberately NOT this state. Both exits (dry /
every counter tried) now go through one function that raises
"bandage_counters_empty", clears bandageTopUp_ and hands off to MAKE_BANDAGES.
The 100 floor and ResolveConsumableThresholds are untouched.

LIVE EVIDENCE (g_Hector.console.txt, 5-min gate 18:52-18:57):
  :303 bought 15 (shelf held 15) -> :304 "84 of 200 after this counter"
  :305 "84 held, and every counter I know of in this town is empty"
  :306 handoff=REPLACE_EQUIPMENT->MAKE_BANDAGES
  :307 goal=TRAIN_COMBAT
  session_goals (:987) REPLACE_EQUIPMENT=1(8%) TRAIN_COMBAT=6 MAKE_BANDAGES=6
Compare 2026-09-05 baseline: 577 s, one goal, zero fights.
TEST: life_harness S4 (tests/life_harness.cpp) -- drain injected via
Runner::NoteBandageCounterDrainedForTest, then no visit is begun in 20 ticks and
the event is recorded. Non-vacuous against S1, which shows the same fencer in the
same empty world starting the errand when no counter is drained.

## Item 5 - a scan is not an attempt
New pure `life::ClassifyErrandLeg(offerOpen, acted, reason)` ->
{Landed, Attempt, Waited} in include/uo/vendor_errand.h; Gear.cpp's bandage
branch uses it. "at the shop, asking who is here (scan N of 3)"
(src/life/VendorErrand.cpp:152, Acted) is now Waited; the open ask (:274) stays
an Attempt; found a/within reach/ARRIVED/the shop is open stay Landed.
TEST: tests/activity_buy.cpp TestScanLegIsNotAnAttempt (5 checks, real strings).
NOT changed: the second copy of this classification in
src/life/runner/Cloth.cpp:203-208 (out of brief).

## Item 6 - the bank keeps the next batch's inputs
New pure `life::CraftInputReserve(profession, item, craftBatch)`
(include/uo/life.h decl, src/life/Needs.cpp def): craftBatch*2 when the item is in
`consumes` or named by a recipe for something in `produces`, else 0 -- the same
union runner/Bank.cpp's input branch builds, and the same working batch
Bank.cpp:371/409 keep. Applied in the LOADED produces branch (Bank.cpp, right
after the WoolChainWorkInProgress skip): keep the reserve, deposit the rest.
TEST: tests/m4_life.cpp TestASmithKeepsIngotsToWorkWith.
NOT PROVEN LIVE: Odessa's 5-min gate never reached a bank and held zero ingots
("i_gears x5 needs 5 x i_ingot_iron (can make 0 of 5)", g_Odessa.console.txt:92);
the run is silent on this change.

## Item 7 - SKIPPED, already handled
Train.cpp's below-floor hand-off is already gated on
`life::WantsConsumable(needCfg_, "bandage")` AND `WantsToHunt(...)`, which is the
same question Gear.cpp:978 asks (`medicineOnly` is false on the goal-level path,
so wantsBandages reduces to WantsConsumable). A pure mage's consumables are
{HealPotions(), Food()} (src/life/Professions.cpp), so the hand-off cannot fire.
Locked with tests/m4_life.cpp TestAPureMageIsNotSentShoppingForBandages.

## Smoke (rev.py gates CHARS=Hector,Odessa MINUTES=5)
Hector PASS for the brief's criterion (errand ends on drained counters; MAKE_BANDAGES
and TRAIN_COMBAT follow). Still zero fights: TRAIN_COMBAT spins with progress 0
(:776 goal_spinning) behind navigation failures in g_Hector.err.txt:7-8 ("no path
to (535,871)") -- pre-existing, unrelated to this change.
Odessa: no bank visit, no ingots; nothing to say about item 6. Errors are the
known nav/resync family (g_Odessa.err.txt:4-21), no new family.
