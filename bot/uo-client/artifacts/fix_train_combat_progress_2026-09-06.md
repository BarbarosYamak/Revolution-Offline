# Fix: a confirmed kill is TRAIN_COMBAT's progress (fleet-100 Cause A)

Date 2026-09-06. Brief: artifacts/fleet100_triage_2026-09-06.md Cause A only.

## Root cause, as read in source

* `Runner::ProcessHuntAftermath` (src/life/runner/Survive.cpp:17) confirms the
  kill (`++session_.kills`, `hunt: confirmed kill target=`) and it is called
  once per tick from `Runner::Tick` (src/life/runner/Core.cpp, "--- decide"),
  BEFORE the goal is chosen. Whatever goal happens to be current at that
  moment would receive a `NoteProgress()` written there -- usually SURVIVE,
  which is why the fleet logs read `goal_completed=SURVIVE progress=0`.
* `Runner::DoTrainCombat` (src/life/runner/Train.cpp) had exactly one
  `return true` for the whole body, gated on "neither hunts nor casts", so no
  fighter or caster could ever reach a success return. Every termination was
  `Finish(false, ...)` or the planner's `attempts 5 >= 5` abandon, both with
  `progress == 0`, which is what `Planner::Finish` (src/life/Goals.cpp:755)
  counts toward `kNoopSpinLimit` -> `goal_spinning=TRAIN_COMBAT`.

## Change

1. `Runner::huntKillsPending_` (src/life/Runner.h): the kill is RECORDED by
   the aftermath (which cannot know whose trip it was) and CONSUMED by
   `DoTrainCombat` on its next tick -- the goal that owns the trip. One
   `NoteProgress()` per kill, then `return true`. Looting cannot be cut short:
   while `ProcessHuntAftermath` returns true, `Tick` returns before `RunGoal`.
2. `Runner::huntEmptyArrivals_`: `huntTrips_` is cleared by arriving, so the
   existing "no hunting ground reachable after N trips" bound can never fire
   for a character that keeps arriving at an empty yard. Arrivals that produce
   no fight now bound the practice window at the same `kMaxHuntTrips`
   allowance and end with
   `goal_failed=TRAIN_COMBAT reason="nothing to fight after N arrivals at a
   hunting ground"` + the short `kHuntStandDownMs` cooldown (transient: the
   yard repopulates). Cleared on engage, on kill credit, and by
   `HandOffFromHunt`.
3. `Runner::NoteCombatSkillGains` (Train.cpp, called from Tick): compares two
   ticks of `Observation::skills` -- which `Runner::Observe`
   (Core.cpp:240-243) fills from `Client::PlayerSkillBase`, i.e. from the
   server's 0x3A packet (`Client::OnSkills`, src/Client.cpp:5593) -- and logs
   `train: <skill> <old>-><new> gained in combat` for weapon schools, Tactics,
   Anatomy, Parrying, Magery and Eval Int, only inside a fight / under
   TRAIN_COMBAT. Baseline refreshed every tick, so a trainer's lesson cannot
   be re-reported later as a combat gain.

Files: src/life/Runner.h, src/life/runner/Survive.cpp,
src/life/runner/Train.cpp, src/life/runner/Core.cpp, tests/life_harness.cpp.

## Verification

* `python tools/rev.py build test` -- 45/45 ctest pass.
* New life_harness scenario "fleet-100 Cause A a confirmed kill is
  TRAIN_COMBAT progress": one credited kill -> `done=1 progress+1 spin=0`;
  credit consumed once; a tick with no kill does NOT return success; five
  credited kills in a row raise no spin.
* Smoke `python tools/rev.py gates CHARS=Hector,Aurelius MINUTES=5`
  (run_gates/g_Hector.console.txt, g_Aurelius.console.txt):
  - Hector: 7 TRAIN_COMBAT picks, two `engage=yes` on Skeletons
    (:925-926, :1064-1065), no kill landed inside the 5-minute window
    (session_summary :1185 kills=0), `goal_spinning` count 0.
  - Aurelius: still `goal_spinning=TRAIN_COMBAT` twice (:122-:206, :448) --
    a DIFFERENT cause, see below.
  - Deaths: `grep "was killed by"` over runtime/logs/sphere2026-09-06.log has
    no line in the 16:41-16:47 smoke window for either character (the only
    Hector line is 02:16, already flagged as noise by the triage).

## Limitations, stated rather than fixed

* The kill-credit path is proven in the harness only. Neither smoke character
  landed a kill in five minutes, so `hunt: N confirmed kill(s) this trip` and
  `goal_completed=TRAIN_COMBAT progress=1` are NOT yet observed live.
* `tools/grade_life.py` TRAIN-2 (line 211-213) accepts only
  `train: \S+ [\d.]+->[\d.]+ bought from a trainer`,
  `practice: ...` and `goal_completed=(TRAIN_GOALS) progress=1`. The new
  `... gained in combat` line does NOT match its `train:` regex, and a pick
  that both opens a fight (NoteProgress at the engage site) and credits a kill
  completes with `progress=2`, which the exact `progress=1` literal also
  rejects. Not edited -- outside this brief.
* `Observation::skills` carries only the skills named in the character's PLAN
  (Core.cpp:240), so a gain in an unplanned combat skill is invisible to the
  new line.
* Aurelius's remaining spin is a spellbook defect, not this one: the caster
  gate in DoTrainCombat waits on `ContainerKnown(spellbookSerial)` and
  `open_container serial=0x40013046` times out every 4 s
  (g_Aurelius.console.txt:105-125), so the goal is abandoned by the planner's
  `attempts 5 >= 5` with progress 0. Separate owner/brief.
