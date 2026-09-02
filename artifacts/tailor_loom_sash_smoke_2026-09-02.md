# Tailor loom->sash chain smoke, radius-18 build (2026-09-02, 22:56-23:01)

Build under test: uo_client.exe with station search radius 10->18 and
today's NeedCloth/DoMakeCloth/sewing-kit edits (per task brief).
Run: `python tools/rev.py gates CHARS=Aelia MINUTES=5` -> `wait` ->
"all 1 reached logout_complete".

CAVEAT: a second, concurrent gate launch for Aelia (another agent/process)
overwrote run_gates/g_Aelia.console.txt and .err.txt after my run finished
(new "start: Aelia is a tailor..." at 23:01:58, file truncated to <130
lines, no backup found under bot_data or elsewhere). All quotes below were
captured via log_slice.py tool calls made against the file *before* the
overwrite and are reproduced verbatim with their original line numbers;
the raw file itself is no longer available for re-verification.

## Timeline (all times 2026-09-02, HH:MM:SS)

1. **MAKE_CLOTH picked.** 22:56:07.702 "restored objective KEPT: MAKE_CLOTH
   (progress 0)" (checkpoint carry-over). REPLACE_EQUIPMENT (130.0)
   preempted it at 22:56:27.703; it failed (0 gold) and MAKE_CLOTH was
   freshly re-scored at 22:57:40.436: "reason: 5 x i_cloth_bolt short, 0
   gold on hand, reserve 400".

2. **Loom found + walk-up -- FIXED.** 22:56:07.770 "cloth: the loom at
   1473,1685 is 11 tiles off -- standing at 1472,1684" (measured from
   actual pos (1462,1681); radius-18 now covers the 11-tile gap that
   radius-10 missed by 1). travel_done ok=1 at (1472,1685) at 22:56:52.017
   -- one tile from the loom. On the re-pick at 22:57:40 she had moved off
   again ("no loom in sight"), retried, and at 22:58:18.011 logged
   "cannot get within 2 tiles of the loom at 1473,1685 after 2 tries --
   striking it off" (the memory'd "unreachable = 1 try max 2" cap) --
   yet the weave line below fires 2s later, so the approach itself did
   land close enough despite the strike-off bookkeeping.

3. **Weave attempted, never progresses -- NEW DEFECT.** "cloth: weaving 6
   yarn at the loom" logged 3x from 22:58:20.627 (Runner.cpp:13480,
   `ActionUseItemOn(spun, loom)`). goal_failed=MAKE_CLOTH at 22:59:05.910:
   "3 gestures in a row moved nothing (wool 5 yarn 6 bolts 0 cloth 0)" --
   yarn count never dropped, no i_cloth_bolt ever appeared. Cooldown of
   300s then blocks MAKE_CLOTH for the rest of the window (12x
   `BLOCKED_NEED MAKE_CLOTH: on cooldown`).

4. **Bolt appears -- NOT REACHED.** bolts=0 at failure; no "You create"
   journal line, no i_cloth_bolt in pack.

5. **Scissors -> cloth -- NOT REACHED** (no bolt to cut).

6. **CRAFT i_sash / sewing kit -- NOT REACHED.** Case-insensitive search
   for "double-clicking the sewing kit", "sewing kit cursor", "Misc.",
   "sash" found zero interaction hits -- only inert
   `held=[sewing kit,scissors,]` inventory dumps. At 22:59:43.950: "craft:
   i_cloth_bolt is not a menu craft (WORLD_PROCESSED) -- MAKE_CLOTH is the
   goal that makes it" + `handoff=CRAFT->MAKE_CLOTH`, but MAKE_CLOTH was
   already on its 300s cooldown, so no further attempt ran in-window.

7. **No death/sealed-in.** err.txt only had benign pathing WARNs (move
   REJECTED / resync). Run ended via normal `logout_complete`.

8. **Shear/wool link:** N/A this run -- Aelia already carried 6 yarn from
   a prior session; no shear/flock lines observed in the captured slices
   (not exhaustively grepped for "shear"/"flock" before the overwrite, so
   treat as "not observed", not "proven absent").

## Root-cause pointer for defect 3

`Runner.cpp:13460-13486` (`DoMakeCloth`, step 2, YARN->LOOM->BOLT): issues
`client.ActionUseItemOn(spun, loom)` and only checks pack-count deltas
3000ms later. Three consecutive no-op gestures with no bolt or yarn-count
change suggests the loom interaction itself isn't consuming yarn/emitting
output the way the code assumes (possible missing gump/menu response, or
wrong verb for Sphere's loom itemtype) -- HYPOTHESIS, not confirmed
against Sphere native t_loom behavior.
