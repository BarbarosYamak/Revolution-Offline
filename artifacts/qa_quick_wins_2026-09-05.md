# QA verification of quick_wins_2026-09-05.md (bot-core)

## ctest
44/44, exact per-suite counts confirmed by direct exe run:
- life_harness.exe: "PASSED: 14 checks, 0 failures"
- activity_buy.exe: "29 checks, 0 failures" (incl. TestScanLegIsNotAnAttempt)
- m4_life.exe: "739 checks, 0 failures" (incl. TestASmithKeepsIngotsToWorkWith,
  TestAPureMageIsNotSentShoppingForBandages)
Source mtimes (18:42-18:51) precede exe mtimes (18:48-18:51:49) -- exes are current.

## Policy preservation confirmed by code read
- bandageFull / ResolveConsumableThresholds: no diff touches these values.
- BandageCountersAllDrained (Gear.cpp) requires bandageCountersDrained_>0 AND
  DrainedShelves() nonempty AND NearestMobileWithTrade() fails for BOTH
  "healer" and "veterinarian" -- matches claim (b).
- ClassifyErrandLeg (vendor_errand.h) returns Waited only for offerOpen=false +
  "asking who is here" reason; open-ask and buy stay Attempt -- matches (c),
  confirmed by tests/activity_buy.cpp TestScanLegIsNotAnAttempt (5 real-string
  checks).
- CraftInputReserve (Needs.cpp) applied only inside Bank.cpp's LOADED
  `produces` branch (~line 329), gated on consumes/recipe-input membership --
  matches (d).
- Train.cpp substitutions: life.h defaults healHpFraction=0.80 (line 1138),
  huntWeightFrac=0.70 (1159), no per-profession assignment anywhere except
  Survive.cpp:645 copying the same field -- value-identical, confirmed (e).
  BankWeightLine's documented mismatch (bankWeightFrac=0.85 for non-hunters)
  is real and correctly left unfixed/reported.

## Item 1 (bankDepositTries_) -- confirmed as described
Bank.cpp:349 `++bankDepositTries_ > kMaxBankDepositTries`; resets only at
:354 (failure branch) and :362 (item name change) -- NEVER on success.
SettleBankItemMove (Bank.cpp:37-53) confirmed to return true (goal continues)
after 3 fails, only delays 20s. grep "not really open" across run_gates/*.console.txt
= 0 hits -- correctly reported as not live-observed.

## Live evidence -- verbatim match
g_Hector.console.txt lines 303-307 and 987 match the artifact's quotes exactly
(buy 15 -> "84 of 200" -> stand-down -> handoff -> TRAIN_COMBAT;
session_goals REPLACE_EQUIPMENT=1 TRAIN_COMBAT=6 MAKE_BANDAGES=6).
goal_spinning count=1 (line 776, TRAIN_COMBAT, matches claim). deaths=0 both
Hector and Odessa. g_Odessa.console.txt:92 ingot-shortage line matches claim.

## DEFECT FOUND: Item 5's "NOT changed" claim is false
quick_wins.md states: "NOT changed: the second copy of this classification in
src/life/runner/Cloth.cpp:203-208 (out of brief)."
`git diff -- src/life/runner/Cloth.cpp` shows this file WAS modified in the
same working tree. Before: `if (r.acted) { ...legLanded ? NoteProgress :
NoteAttempt... }` -- doing nothing at all when r.acted==false. After: legLanded
is now checked unconditionally, so `legLanded && !r.acted` now fires
NoteProgress (previously fired nothing). This is a real, undisclosed
behavioral change to a live goal (MAKE_BANDAGES) in a file the artifact
explicitly claims is untouched.

## Scope creep beyond the 7 documented items
The same reviewed files (Train.cpp, Needs.cpp, Runner.h, Client.h/.cpp,
Core.cpp) also carry a large, separately-artifacted set of changes not
described in quick_wins.md at all:
- Train.cpp: PickPracticeSpell manaNeeded handling + meditation loop
  (selfPracticeRuns_, kSelfPracticeBeforeRethink) -- new mana-recovery path.
- Needs.cpp: NeedPractice's `mana >= 10` readiness gate removed (meditation
  now assumed available), changing when NeedPractice reports "ready".
- Runner.h/Client.h/.cpp/Core.cpp: a ~250-line LeaveGoal/HandOff refactor and
  offline-test seams (SetObservationOverrideForTest, SetOfflineForTest),
  covered by separate artifacts (life_harness_slice_2026-09-05.md,
  goal-exit-is-one-hook.md) not requested for this verification pass.
These are real, uncommitted, live-path changes; ctest 44/44 covers them too,
but "each change is one rule, policy preserved" cannot be fully claimed for
these files as a unit -- the artifact under review only accounts for a
subset of what actually changed in them.

## err.txt / new-failure-family note
g_Hector.err.txt diff: old baseline had "no path to" + "0x21 move REJECTED"
resync family; new run replaces some with a new message shape "goal (x,y) is
not walkable; snapping to nearest standable tile" -- a self-recovering WARN,
not previously present in the tracked baseline. Same nav/pathing subsystem,
not obviously caused by this diff's files, but technically a new log-line
shape not called out. g_Odessa.* files are NOT git-tracked (no prior commit),
so "no new family" for Odessa rests on the agent's judgment against memory,
not a mechanical diff -- weaker evidence than for Hector.
