# Runtime evidence review, 2026-09-05

Read-only survey. Files: bot/uo-client/run_gates/g_{Hector,Aurelius,Odessa,Castor,
Faustus,Elara,Selene,Xerxes}.{console,err}.txt (all captured 11:36-15:44 on
2026-09-05), plus docs/SESSION_STATE_2026-09-04.md, docs/SESSION_STATE_2026-09-05.md.

## A. Failure family evidence

Counts are grep -c over the 8 named .console.txt files (err.txt files
contributed 0 hits for every pattern below).

### 1. Overlapping retry limits -- 1 clean instance, plus 3 borderline repeats
goal_changed=...attempts 5 >= 5 fired 6x total (Hector x1, Castor x1,
Xerxes x4); goal_blocked=...not enough session left fired 1x (Xerxes).

Best example -- two different limits killing two different goals 77s apart
in the same run:
- g_Xerxes.console.txt:81  13:22:14.014 goal_changed=GET_TOOL from=TRAIN_COMBAT
  reason="previous goal abandoned: attempts 5 >= 5" (attempts counter killed
  the TRAIN_COMBAT open_container loop).
- g_Xerxes.console.txt (line ~187)  13:23:31.022 goal_blocked=GET_TOOL
  reason="not enough session left for the trip" tiles=280 left=203s need=204s
  then 13:23:31.083 goal=TRAIN_COMBAT reason="previous goal abandoned: not
  enough session left for the trip" -- GET_TOOL was killed by the session-time
  budget while its own attempts counter had not exhausted. Two independent
  abandon mechanisms (attempts-count vs session-time-left) fire on
  consecutive goals without coordinating.

Borderline: g_Xerxes.console.txt lines 202, 230, 257, 466 show TRAIN_COMBAT
restarting itself 4 more times, each killed by the same attempts 5 >= 5 rule
on the same stuck action (see family 2) -- the retry limit resets every ~20s
without any different behavior occurring, i.e. it is not really limiting
anything.

### 2. Errand/action continuing after goal_changed -- 2 strong instances
a) g_Xerxes.console.txt lines 81-283 -- open_container serial=0x4000CB1D
   times out every 4s (event action_result: open_container timeout)
   continuously from 13:22:14 to 13:24:48 while goal_changed=TRAIN_COMBAT
   from=TRAIN_COMBAT fires 4 times (lines 202, 230, 257, 466, each
   "previous goal abandoned: attempts 5 >= 5"). The identical action on the
   identical serial is reissued across every goal boundary; the new
   TRAIN_COMBAT goal is indistinguishable from the old one at the
   action-stream level.
b) g_Hector.console.txt lines 602/680/754 -- goal_changed=TRAIN_COMBAT
   from=MAKE_BANDAGES reason="...superseded..." fires 3 times 30s apart
   (15:39:50, 15:40:20, 15:40:55); each time the same [travel] Yew
   provisioner FAILED ... training supersedes line appears at the identical
   timestamp, followed within 60ms by handoff=TRAIN_COMBAT->REPLACE_EQUIPMENT
   and goal=MAKE_BANDAGES, and the bandage-cloth errand trip counter
   (bandage cloth: walking to a weaver, trip 2, then trip 3) keeps
   incrementing across the flips -- the errand survives 3 goal_changed
   events untouched because the planner immediately hands the goal back.

### 3. Stale cache / delayed update -- 1 instance in scope, 1 adjacent
- g_Xerxes.console.txt:301  13:24:56.847 practice: the book reads empty but
  was already opened this session -- re-opening once before believing that
  -- the documented spellbook re-open workaround (SESSION_STATE_2026-09-05:
  PRACTICE re-opens the book once before calling it empty) firing live.
- Adjacent, not strictly pack/spellbook/bank: [0x21] stale reject seq=N
  (block already handled); pose resynced in g_Castor.console.txt:920 -- a
  movement-pose desync resync, not a pack/spellbook/bank cache case.
- pack never moved (the alchemist Craft.cpp fix from SESSION_STATE
  2026-09-05): ZERO occurrences in all 8 files. Consistent with the note
  that the fix landed before todays runs; absence stated, not assumed.

### 4. Lost intent in a chain -- 1 clear flip-flop, wool<->hunt not reproduced
- g_Odessa.console.txt:377  15:38:52.868 goal_changed=BUY_SUPPLIES from=MINE
  reason="BUY_SUPPLIES 133.0 superseded MINE 58.5", immediately (about 60ms
  later) handoff=BUY_SUPPLIES->TRADE_WITH_PLAYER reason="another profession
  makes this, not a shopkeeper" then goal=MINE reason="previous goal
  abandoned: another profession makes this, not a shopkeeper". The identical
  BUY_SUPPLIES->MINE round trip repeats at line 577 (15:40:52.903,
  BUY_SUPPLIES 133.0 superseded MINE 81.2) two minutes later -- a
  restock<->hunt-shaped flip that self-corrects each time within the same
  tick, but the underlying NeedSupplies scoring keeps re-triggering it.
- Hector TRAIN_COMBAT<->MAKE_BANDAGES flip (family 2b above) is the same
  shape applied to combat instead of gathering.
- wool<->hunt flip specifically: ZERO occurrences in Faustus/Castor (wool
  appears only inside NeedWoolIncome(... BLOCKED ...) need listings and
  HARVEST_WOOL: already carrying a load blocks, never a goal_changed flip)
  -- matches SESSION_STATE_2026-09-05 note that this flip was not
  reproduced today. Absence stated, not assumed.
- SMELT completed then EXPLORE/TRAIN instead of CRAFT: ZERO occurrences.
- BANK deposited what the next step needed: ZERO direct evidence; found the
  inverse instead -- BANK entered and immediately bounced with "nothing to
  deposit" (g_Castor.console.txt:76, g_Faustus.console.txt:339,
  g_Elara.console.txt:99), i.e. the goal churns into BANK and back out
  within one tick rather than actually depositing something needed.

### 5. goal_spinning backstop firing -- 1 occurrence
- g_Xerxes.console.txt:294  13:24:48.697 goal_spinning=TRAIN_COMBAT
  reason="completed 5 times in a row with progress 0 -- cooled off for a
  minute; this is a bug in that goal, not pacing" -- fires at the end of
  the family-2/family-1 open_container loop described above; the planner
  anti-spin guard catching the exact scenario families 1+2 produced live.
- No goal_spinning in Hector/Aurelius/Odessa/Castor/Faustus/Elara/Selene
  today -- absence stated, not assumed.

## B. Test harness survey

Entry points (file:line):
- bot/uo-client/tests/m4_life.cpp (3738 lines, add_test NAME m4_life at
  tests/CMakeLists.txt:157) -- the only test that touches the planner/needs
  decision layer.
- src/life/Runner.h:69  void Tick(Client& client, i64 nowMs); -- the
  production orchestration entry point. Client& is a concrete class, not
  an interface; there is no virtual seam to substitute a fake.
- grep -rn "life::Runner" tests/ src/ matches ONLY src/Client.cpp and
  src/Client.h -- no test file ever constructs a Runner. The whole
  goal-lifecycle orchestration (RunGoal, LogGoalChange, handoff, the
  attempts-exhausted path, LogGoalHistogram) is exercised only by live runs
  under run_gates/, never by ctest.
- src/Client.cpp:476  lifeRunner_->Tick(*this, NowMs()); is the sole
  production call site; Client::NowMs() reads std::chrono::steady_clock
  (Client.cpp:125,375,513) -- real wall clock, not injectable in the live
  path.

What m4_life.cpp actually exercises:
- Hand-built life::Observation structs (plain field assignment, e.g.
  obs.nowMs = 5000000; at line 319) fed straight into pure functions:
  life::AssessNeeds(plan, mem, obs, needCfg) and Planner::Select/Score/
  NoteAttempt/Cooldown/Exhausted/Finish/TakeSpinDetected.
- nowMs is a manually-incremented i64 on the tests own Observation/Planner
  calls (nudged.nowMs += 3000; at line 507, tie.nowMs += 40000; at line 558,
  etc) -- a genuinely injected clock, but only at the Planner-unit level,
  never wired through a Runner::Tick loop.
- The goal_spinning bookkeeping test (m4_life.cpp lines 2100-2170) places a
  GoalState directly via pl.Mutable() and calls Finish()/NoteProgress() by
  hand, explicitly bypassing Select(); the tests own comment says Select
  needs a whole need list and observation, so what is under test here is
  only the bookkeeping in Finish().
- No RNG/seed usage found anywhere in m4_life.cpp.
- Runner::Do* handlers under src/life/runner/*.cpp are reachable only from
  Runner::RunGoal (Core.cpp switch(goal), around line 1690), which is
  reachable only from Runner::Tick, which is never called in any test.
  0 of the Do* handlers are unit-reachable; all are live-run-only today.

Other test files, for contrast:
- tests/activity_train.cpp, activity_gather.cpp, activity_craft.cpp,
  activity_buy.cpp -- pure single-call decision functions (e.g.
  DecideTrain(TrainRequest, have) -> TrainPlan with a step and reason,
  activity_train.cpp lines 44-52). One snapshot in, one decision out; no
  Client, no act::Result, no multi-tick sequence, no scripted outcome
  injection.
- tests/moongate_gump.cpp, tests/trade_verify.cpp -- the closest thing to
  integration testing that exists: they construct a real Client object and
  feed it hand-crafted server packets over a real loopback socket (the
  loopback listener only exists so Client::Send has a live socket, per
  moongate_gump.cpp line 23) via Client::DispatchPacketForTest. This proves
  packet-dispatch/gump-state bugs (documented against a live defect in
  g_Xerxes.console.txt:119-171 from 2026-09-02, in a code comment) but
  never touches goal selection, retries, or handoff.

What is MISSING for a deterministic multi-tick scenario harness:
1. No fake/stub Client exists or is injectable -- Runner::Tick takes a
   concrete Client&; every action call (BeginAction, use_object, goto, etc.)
   goes straight to the real networked implementation. A scripted-outcome
   fake would need either (a) a new virtual interface Runner calls through
   instead of Client directly, or (b) subclassing/wrapping Client itself,
   which today is one large concrete class with no seam cut for it.
2. No harness steps Runner::Tick N times with a hand-fed Observation and a
   scripted act::Result per action (Success/Timeout/Rejected/...).
   act::Result (include/uo/actions.h) is never referenced by any test.
3. No injected clock reaches Runner::Tick -- production time comes from
   steady_clock unconditionally; there is no RunnerConfig or Tick overload
   that accepts a test-supplied nowMs sequence independent of
   Client::NowMs().
4. Consequently, none of the five live failure families above (goal
   thrashing across two retry limits, stale-action-survives-goal_changed,
   cache staleness, lost intent across a goal chain, spin-detection) can be
   reproduced or regression-locked by ctest today -- they are provable only
   by a live run_gates wave and read back through .console.txt/.err.txt.
