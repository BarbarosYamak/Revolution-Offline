# Hector — corpse loop + session limit, 2026-09-02

## Root cause A — RECOVER_CORPSE opened container 0 forever

Hector died before the wave and reconnected as a ghost. His death record
(`travel::DeathRecord`, x=1457 y=1602) is persisted and was restored at login
(`corpse run: restored pending death at 1457,1602`), but `corpseSerial` is
deliberately session-local and was therefore 0.

`runtime/sphere.ini:576  CorpsePlayerDecay=7` — a player corpse is gone seven
minutes after death. By the time he was resurrected (~09:21) the corpse itself
had decayed, so the rebind at `src/life/Runner.cpp:3537-3544`
(`FindWorldItemByGraphic(0x2006, 8)`) never found anything.

`DecideRecovery` then returned `Loot` (distance <= 2, corpse "known" because
the record is valid), and the handler called
`client.ActionOpenContainer(0)`:

    run_gates/g_Hector.console.txt:3227..3400
      [ACTION] open_container start
      event action_result: open_container invalid_state took=0ms null serial
    x73 in the 2-minute slice, 200+ over the goal's life

Nothing in that path called `NoteAttempt` / `NoteCorpseRecoveryAttempt`, so
`see.attemptsSoFar` stayed 0 and the `attemptsSoFar >= maxAttempts` abandon rule
was unreachable. The goal only ended on the 300 s planner deadline and was
immediately re-picked at urgency 712.5.

## Root cause B — the session limit never fired

`src/life/Runner.cpp:2280-2287` (pre-fix) deferred the deadline whenever the
character was dead OR the active goal was `RecoverCorpse` — with no bound. A
corpse run stuck in a retry loop held both conditions true permanently, so
`EndSession` was never reached and Hector was still connected 5 min past his
30-minute window.

## Fixes

1. New typed outcome `RecoveryStep::CorpseGone`
   (`include/uo/activities/recovery.h`). `RecoverySight` gains
   `corpseVisible` (a serial is actually bound) and `probesAtSite`;
   `RecoveryTuning` gains `maxProbesAtSite = 5`. Rule in
   `src/life/activities/RecoveryPlan.cpp`: standing within 2 tiles with no
   serial bound for the probe budget => `CorpseGone` with a reason.
2. `Runner::DoRecoverCorpse` counts probes, refuses to send
   `open_container` on serial 0 at all, and on `CorpseGone` clears the death
   record, notes `corpse_lost`, logs `goal_failed=RECOVER_CORPSE reason=...`
   and hands off to `REPLACE_EQUIPMENT` with a 10-minute planner cooldown.
   The loot is gone; that is Revolution death.
3. `kSessionOverrunGraceMs = 60 * 1000` (`src/life/Runner.h`). The deferral is
   now bounded: past limit + grace the runner aborts travel, `Finish(false)`s
   the active goal and calls `EndSession` — the ordinary WindDown ->
   walk-to-safety -> logout path. No process kill.

## Runtime evidence (5-minute smoke, 2026-09-02 11:33)

`run_gates/g_Hector.console.txt`

    45   corpse run: restored pending death at 1457,1602
    102  goal_changed=RECOVER_CORPSE from=REPLACE_EQUIPMENT ... 712.5
    108  [11:34:03.968] plan=loot reason="standing over it"
    109  [11:34:08.031] plan=the corpse is gone reason="stood on the death
         tile and there is no corpse there -- decayed or already looted"
    110  goal_failed=RECOVER_CORPSE reason="stood on the death tile ..."
    111  handoff=RECOVER_CORPSE->REPLACE_EQUIPMENT
    112  goal=REPLACE_EQUIPMENT
    870  [11:38:41.062] session_end_requested reason="session time limit reached"
    936  session_goals ... picks=5 ... RECOVER_CORPSE=1(20%)
    945  [11:39:28.113] event logout_complete: acked

- Corpse goal resolved in 4.1 s, once, with a stated reason. Not re-picked
  (1 of 5 picks in the whole session).
- Zero `open_container invalid_state` lines in the whole run (grep found none).
- Session limit fired on the NORMAL path at +5 min; wind-down walk took 47 s.

## Not proven

- The `kSessionOverrunGraceMs` branch (`session_overrun` line) did not execute
  this run, because the corpse fix meant the deadline was never deferred. It is
  covered by reasoning and by the deferral condition only, not by runtime.
- `.err.txt` shows unrelated pathing noise during wind-down
  (`no path to (1416,1592)`, `move REJECTED` resyncs, lines 56-67); the
  wind-down still completed.

## Tests

`tests/activity_recovery.cpp::TestADecayedCorpseIsGivenUpOn` — asserts one
quiet tick on arrival still loots (the world-item stream needs a moment), the
probe budget yields `CorpseGone` with a non-empty reason, and a late-bound
serial still loots. `ctest` 41/41.
