# Need/handler contract — completion evidence, 2026-09-06

Worktree `.claude/worktrees/agent-a606b9ff7182c36e8` (branch
`worktree-agent-a606b9ff7182c36e8`, base 11f4c80). Design doc:
`docs/NEED_HANDLER_CONTRACT.md` (in the worktree, alongside the code).

## 1. What was inherited and kept

A previous agent had built the mechanism and migrated five of the seven pairs.
The design was sound and was kept unchanged in shape:

* `include/uo/needgate.h` + `src/life/NeedGate.cpp` — `life::CanAct` (arm A,
  pure rules both callers run) and `NoteNeedBlocked` / `NeedBlockActive`
  (arm B, a handler's observed refusal recorded where the need can read it).
* `Runner::BlockNeed` (`src/life/runner/Core.cpp:1955`) — the handler-side
  writer: log, cooldown, `Finish(false)`, and the block record. Emits
  `goal_blocked=<GOAL> reason="..."`, a prefix already used at ~10 sites.
* `AssessNeeds`'s `add` lambda (`src/life/Needs.cpp:596`) reads arm B for
  every kind, so a migrated handler needs no per-need reader.

Already migrated on arrival: NeedSupplies (A), NeedTraining/hunt (A),
RecoverCorpse (A), Heal (B, `HealStep::Stuck`), NeedCloth + NeedWoolIncome
(B, the three pasture refusals in `Cloth.cpp`).

## 2. What this pass added

1. **Build wiring.** `NeedGate.cpp` was in neither `CMakeLists.txt` nor
   `tests/CMakeLists.txt`; `m4_life` and `m8_market_trip` failed to link
   (`LNK2019: CanAct / NeedBlockActive`). Added to all three source lists,
   plus `src/life/activities/RecoveryPlan.cpp` to `m4_life` for the
   cross-check test.

2. **Corpse gate ordering defect (real regression, caught by ctest).**
   `TestRecoveryForEveryArchetype` failed for all 17 archetypes:
   `attempt limit can clear persisted corpse`. The gate applied the health
   line unconditionally, but `DecideRecovery`
   (`src/life/activities/RecoveryPlan.cpp`) tests `threatened`, then
   spent-attempts `Abandon`, and only then the health line. A character raised
   at a fifth of its health with its trips spent was told to heal first when
   the handler's next step was `Abandon` — the step that clears the death
   record. Blocked there, the record is never cleared and the corpse is
   carried forever. Fixed by mirroring `DecideRecovery`'s order in `CanAct`
   (`RecoveryTuning{}.maxAttempts` and the `> 2` tile clause are read from the
   handler's own tuning struct, not re-typed).

   Rule now written into the doc (§3.1): a gate may block only on handler
   steps that are REFUSALS; a terminal step (abandon, give up, mark gone) is
   work, and blocking it starves the handler of the turn that closes the
   errand.

3. **Arm-B staleness rule moved into the reader.** It sat in `CanAct`, but
   `AssessNeeds` reads blocks through `NeedBlockActive` directly — so the
   escape never ran at the need site and a character with 5 bandages in the
   pack stayed under a HEAL stand-down. Moved into `NeedBlockActive`; `CanAct`
   now just calls it, so both callers get the same answer.

4. **Three contract tests** in `tests/m4_life.cpp` (+18 checks, 795 -> 813).

## 3. Tests

| Test | Pair | What it proves |
|---|---|---|
| `TestContractCorpseGateMatchesTheHandler` | RecoverCorpse (A) | over a 72-case matrix of (HP x threatened x attempts x corpse distance), `Need::blocked == (DecideRecovery(...).step == Recover)`. Runs the handler's own decision function, not a copy of its rules |
| `TestContractArmAMatchesTheErrand` | NeedSupplies (A), NeedTraining/hunt (A) | for every profession's `consumes`, `CanAct` refuses exactly the inputs `market::RouteForInput` does not route to `NpcVendor`, always with a reason; `huntReturnPending` blocks NeedTraining with the gate's own sentence, and that sentence is gone once the loot is banked |
| `TestContractArmBSilencesTheNeed` | Heal (B), NeedWoolIncome (B) | a written block silences the need and `Need::reason` IS the handler's text; a bandage reopens HEAL; a `Window` block ages out at `kBlockWindowMs`; a `Session` block ends at the next `sessionIndex` |

## 4. Gate results

`python tools/rev.py build` — clean (Ninja/Release, matching the main
`build-m1` generator; the first `reconfigure` in a fresh worktree picks the
Visual Studio generator, whose `ctest` then needs `-C Debug`).

`python tools/rev.py test` — **43/44 pass**.

* `m4_life`: 813 checks, 0 failures (was 795/17 on arrival).
* The one failure, `m4_economy_invariant`, is a worktree-LOCATION artifact,
  not a regression: the test's data path is
  `${CMAKE_SOURCE_DIR}/../../docs/tns_exports/economy_arbitrage_loops.tsv`,
  which from `.claude/worktrees/<name>/` resolves inside `.claude/`. Run
  against the real path it passes: `8 checks, 0 failures`.
* `m9_service_selection` failed the same way (untracked, generated
  `data/revolution_navgrid.bin`, `.gitignore:29`). Copied from the main
  working copy into the worktree `data/` — still gitignored, nothing to
  commit — after which it passes.

## 5. Telemetry unchanged

`BLOCKED_NEED <GOAL>: <reason>` is emitted from `Core.cpp` via
`Planner::Score`'s handling of `Need::blocked`; the contract adds no new log
line, it only makes more needs reach the line that already exists.
`tools/grade_life.py` does not parse `goal_failed=` / `goal_blocked=` /
`goal_stuck=` / `BLOCKED_NEED` at all, so the three refusal sites in
`Cloth.cpp` that changed prefix from `goal_failed=` to `goal_blocked=` break
no grading.

## 6. Not migrated (unchanged from the inherited list)

NeedTool (tool is in the bank), NeedBank, NeedSmelt, NeedOre, NeedCatch,
NeedSkillTraining, NeedCraft. All seven reduce to "the place is not
reachable", which wants ONE shared arm-B `place_unreachable` block rather than
seven more predicates — a design step, not a mechanical migration, so it was
left out of this pass. NeedEquipment/NeedMount were already conformant through
the pre-existing `bandage_counters_empty` / `potion_counters_empty` /
`mount_unavailable` events (they damp rather than block, which is policy and
was not touched).

## 7. Not proven here

No live run. `rev.py gates` was explicitly out of scope for this brief; every
claim above is from source reading and ctest, not from bot behaviour on the
shard. The behavioural claim the contract makes — fewer `goal_spinning` and
fewer pick/hand-off pairs per session — needs the lead's smoke wave to confirm.
