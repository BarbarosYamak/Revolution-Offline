# Minoc Mine 1 — no route out to a banker (navigation-world), 2026-09-06

Base HEAD 1877518. `python tools/rev.py build test` -> ctest 45/45.
Smoke `python tools/rev.py gates CHARS=Kharain MINUTES=5` (19:48-19:53).

## Symptom

`[travel] no place offers banker` x58 in `run_gates/g_Kharain.err.txt`
(previous gate, 18:39-19:07) from (2569,479) inside Minoc Mine 1, while the
same lookup had worked from Minoc town twenty minutes earlier.

## Root cause (verified offline against the real navgrid)

Not the atlas and not the service filter. `minoc_bank` was in the candidate
list every time; `RoutePlanner::Plan` could not produce a route from that
tile, and `PickServicePlace` drops an unroutable candidate with `continue`,
so the caller could only say "no place offers banker".

Measured from `data/revolution_navgrid.bin` (16-tile cells, BFS over the
stored `edges` bits):

| cell | anchor | flags | edges | component |
|---|---|---|---|---|
| (160,29) Kharain's cell | (2566,474,**40**) | passable | 0x00 | 1 |
| (161,29) | (2578,472,60) | passable | 0x00 | 1 |
| (159,27) | (2554,440,-3) | passable | 0x00 | 1 |
| (159,28) | (2554,456,15) | passable | 0x10 (S only) | 2 |
| (159,29) | (2552,470,15) | passable | 0x01 (N only) | 2 |
| (159,30) | (2552,488,15) | passable | 0x7C | 17518 |
| (160,30) | (2566,484,40) | passable | 0x40 | 17518 |
| minoc_bank (156,34) | — | passable | — | 17518 |

So the cells over Minoc Mine 1 are a **five-cell pocket** whose anchors sit on
the mountain at z 15/40/60, while the miner stands on the cave floor at z 0
(`mine: striking the rock at 2566,484,0 (cave floor…)`). This is exactly the
`NavGrid.h` KNOWN DEBT item 2: one anchor per cell, no notion of floors.

The Papua island fix (`SnapToConnected`, f65225b) asked only *"does this cell
have an outbound edge to a passable neighbour"*. (159,28) satisfies that — it
has one edge, to (159,29), which has one edge back. The snap therefore moved
the start from a 1-cell island onto a 2-cell dead end and A* still had
nowhere to go. The moongate fallback in `Plan` recurses through the same
snap, so it failed too.

**One edge is not connectivity.**

## Changes

`src/world/RoutePlanner.h` / `.cpp`
- `HasOutboundEdge` / `HasInboundEdge` / the free `SnapToConnected` replaced by
  members `ComponentReach(cx, cy, cap, inbound)` and `SnapToConnected(...)`.
  `ComponentReach` is a bounded flood over the measured edges (cap 24 cells,
  `kConnectedComponentCells`) that stops early; `inbound` walks the edges
  backwards for a goal. A cell touching a transit node counts as connected
  outright (new `transitTouchedCells_` index, both ends of every transit) so a
  teleporter-only room is not mistaken for a pocket.
- `Plan` now snaps a start/goal whose component is smaller than 24 cells, not
  only one with zero edges. When no better cell exists within 3 rings the
  coordinates are left alone, exactly as before — worst case is unchanged
  behaviour.

`src/world/ServiceSelection.cpp` / `.h`
- When **no** candidate could be routed to, the first 3 unroutable candidates
  now go into `rejections` carrying the planner's own `failure` string, so the
  caller prints `[travel] place: skipping X -- no world route to the
  destination` instead of only `no place offers banker`. While a winner exists
  the behaviour is unchanged (unroutable is still not a policy rejection).

`src/life/runner/Core.cpp`, `src/life/Runner.h`
- New `windDownBlockedLogged_`. The blocked wind-down branch now emits **one**
  `goal_failed=WIND_DOWN reason="no safe logout from X,Y (navgrid cell CX,CY):
  <planner text>"` plus one `stuck` memory note, then keeps retrying every 30 s
  **silently**. The 30 s re-arm is deliberate and unchanged; what is gone is
  the 58 copies of the same three lines.

## Regression tests (`tests/m9_service_selection.cpp`, ctest `m9_service_selection`)

- `TestRealAtlasMinerInMinocMine` — real atlas + real navgrid, plans from
  (2569,479) and requires `PickServicePlace(Banker)` to resolve to
  `minoc_bank`.
- `TestUnroutableLookupExplainsItself` — synthetic two-island grid; the pick
  is null AND `rejections` is non-empty with a non-empty reason.

Proof the first test bites: with `kConnectedComponentCells` temporarily set to
2 (which reproduces the old "has an edge" semantics) the suite reports
`27 checks, 2 failed` — `the planner routes out of Minoc Mine 1 to minoc_bank`
and `a banker is found from inside Minoc Mine 1`. At 24 it is `28 checks,
0 failed`.

## Live proof (run_gates/g_Kharain.console.txt, 5-min gate 19:48-19:53)

    :549  mine: striking the rock at 2572,481,0 (cave floor, 1 tiles)
    :564  mine: ORE at 2572,481                       (x6 strikes, ore each time)
    :634  wind-down: no bank learned yet; asking the world for one (attempt 1)
    :636  [travel] place: skipping Wind banker -- 1892 tiles and 2 gates ...
    :638  [travel] Minoc banker -> (2503,552) r=5 from (2571,480)
    :644  [travel] plan Minoc banker: ok legs=3 ~112 tiles transit=0 nodes=62
    :659  goto finished at (2552,504,15) ARRIVED       <- out of the pocket
    :670  goto finished at (2532,536,0)  ARRIVED       <- Draver's proven exit
    :683  [travel] Minoc banker ARRIVED at (2504,557,0)
    :685  wind-down: arrived somewhere safe at 2504,557
    :686  session_summary duration=343s goals=0/4 gold=8950->8922 deaths=0
    :695  event logout_complete: acked

Counts this run: `no place offers` **0** (was 58); `wind-down: safe logout
blocked` **0** (was 28); `goal_failed=WIND_DOWN` **0** (never needed).
The route leaves the mine at (2552,504,15) then (2532,536,0) — the same
cave-exit line Astra proved for Draver, reached here by the macro planner on
its own rather than by an escape heuristic.

Cross-check: `grep "was killed by" runtime/logs/sphere2026-09-06.log` -> 17
lines for the day, **none** naming Kharain.

## Not fixed here (surfaced, out of brief)

`run_gates/g_Kharain.err.txt` 19:51:41: `[bot] no path to (2552,473) avoiding
0 block(s); stopping (search 956574.5us)` from (2565,485) — the tile A* burned
~1 s and gave up on a 14-tile hop to a rock across the cave. Same family as
the Britain-graveyard leg noted in `fix_atlas_lumber_stale_npc_2026-09-06.md`:
a tile-A*/leg-budget question, not a navgrid-connectivity one. Kharain simply
picked a different rock and carried on.
