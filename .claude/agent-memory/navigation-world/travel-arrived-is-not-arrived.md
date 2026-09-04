---
name: travel-arrived-is-not-arrived
description: TravelToPoint reports ARRIVED up to kLegArriveSlack (3) tiles off and ignores the caller's arriveRadius, so any caller needing an exact stand tile must close the gap itself with ActionGoto.
metadata:
  type: project
---

`Client::TravelToPoint(x, y, arriveRadius, label)` does **not** honour
`arriveRadius` on the leg-arrival test. `src/travel/ClientTravel.cpp`
accepts a leg with `GotoSucceeded() || off <= kLegArriveSlack` where
`kLegArriveSlack = 3` (ClientTravel.cpp:47), regardless of what the caller
asked for. A radius-0 trip therefore logs `ok=1 / ARRIVED` from up to three
tiles away.

**Why this bites:** callers that pick an exact tile *beside* a solid object
(forge, anvil, loom, vein) then apply a tight reach test of their own and
conclude the object is unreachable. Draver 2026-09-04: `travel forge ->
(2560,500) r=0 ... ARRIVED at (2560,499,0)`, one tile short, Chebyshev 2 from
the forge at (2561,501), `kForgeReach = 1` -> the mine's own forge was struck
off and the miner walked ~90 tiles away. See
`artifacts/minoc_reach_2026-09-04.md`.

**How to apply:** never treat `travel_done ok=1` as "I am on that tile".
Re-measure from `client.PlayerX()/PlayerY()`. If the caller needs the exact
tile, close a residual gap of <= 3 with a plain `client.ActionGoto(x, y)`
(no arrival slack) once, and do not let that count against the caller's
approach/strike-off budget. Do not raise the reach constant to paper over it —
`kForgeReach = 1` is backed by a shard refusal at Chebyshev 2
(`src/life/runner/RunnerInternal.h:690-694`).

Related: [[two-arrival-tests-must-agree]], [[dedup-templates-numbers]].
