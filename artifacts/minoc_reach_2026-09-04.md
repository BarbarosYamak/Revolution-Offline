# Minoc miner reach defects — 2026-09-04

Scope: navigation/reach layer only. No planner, goal-scoring or Sphere script edits.

## Loop 2 (real): mine interior <-> rock ping-pong — FIXED

`src/life/runner/Gather.cpp` DoMine. Two tests for "am I at the home mine"
disagreed:

- `atHomeMineInterior` (Gather.cpp ~1480) = centroid distance <= kMineReach(6)
  **OR** `client.WithinMiningRegion(...)` — correct, added 2026-09-02 for this
  exact ping-pong.
- the travel gate immediately below (`if (homeMine && TileDist(...) > kMineReach)`)
  used the **centroid distance alone**, so the 2026-09-02 fix never reached the
  branch that actually issues the trip.

Minoc Mine 1 RECT is 26x27; kMineReach is 6. A miner who walked to a real rock
near the RECT edge (e.g. stand tile 2569,478, interior anchor 2568,487 →
TileDist 9) was inside the mine but 9 > 6, so the gate sent him back to the
anchor every tick, the rock scan then walked him back out, forever.

Raw counts, 60-min wave (`run_gates/g_Draver.console.txt`,
`g_Kharain.console.txt`):

| line | Draver | Kharain |
|---|---|---|
| `going directly to the interior` | 175 | 209 |
| `the rock is at ... standing at` | 187 | 253 |
| `mine: striking the rock` | 18 | 14 |

Trace (g_Draver.console.txt:9885-9899): travel to interior (2568,487) r=3 →
ARRIVED (2568,484) → `mine: the rock is at 2569,477, 7 tiles off -- standing at
2569,478` → walk → next tick back to the interior.

**Note on the "5 tiles off" in the brief.** Gather.cpp:1696 measures
`TileDist(hereX, hereY, spot.rockX, spot.rockY)` — the distance from the
character's *current* position to the rock, not from the stand tile. The
message is correct; z / cave floor is not involved.

Fix: `if (homeMine && !atHomeMineInterior && TileDist(...) > kMineReach)`.

## Loop 1: not a loop — a log_slice --dedup artifact

`tools/log_slice.py --dedup` normalises numeric literals into a template, so
`TEMPLATE x251 ... target (2467,556); stopped short (off by 1 tile(s))` counts
**all 542 `goto finished` lines in the file**, not 251 repeats of that one.
Raw: `grep -c "target (2467,556); stopped short"` = **2** in 60 minutes.
The forge at Minoc blacksmith was reached and used (craft_made=6,
`smelt: opening the forge at 2468,557` x2). No forge loop existed.

## Real forge defect found instead: travel reports ARRIVED off-target — FIXED

`ClientTravel.cpp:1488,1525` accept a leg with
`GotoSucceeded() || off <= kLegArriveSlack` (=3) and **ignore the arriveRadius
the caller asked for**. DoSmelt asks `TravelToPoint(standX, standY, 0, "forge")`
for a tile it computed to be adjacent to the forge, so a 3-tile lie is fatal to
its own `kForgeReach = 1` test.

Trace (g_Draver.console.txt:1891-1912, 15:40:41):

    smelt: forge at 2561,501 is 19 tiles off -- standing at 2560,500
    [travel] forge -> (2560,500) r=0 ... forge ARRIVED at (2560,499,0)   ok=1
    smelt: cannot get within 1 tile of the forge at 2561,501 after 2 tries
    smelt: carrying 17 ore with no forge in reach -- walking to a smithy

(2560,499) is Chebyshev 2 from the forge — outside kForgeReach — so the mine's
own forge was struck off and the miner walked ~90 tiles to the Minoc blacksmith.

`kForgeReach` was **not** raised: RunnerInternal.h:690-694 records proven shard
behaviour that a smelt at Chebyshev 2 is refused ("You must be near a forge to
smelt"). Instead DoSmelt now:

1. picks the **nearest** walkable neighbour of the forge, not the first in
   (-1,-1)-first scan order (which routed the character around the forge);
2. closes a residual gap of <= kLegArriveSlack with a plain `ActionGoto`
   (no arrival slack), **once per forge**, without spending an approach;
3. still retires the forge under the existing 2-approach / 3-refusal rules.

## Smoke run — `rev.py gates CHARS=Draver,Kharain MINUTES=5` (18:03-18:09)

| metric | Draver 60min (before) | Draver 5min (after) | Kharain 60min | Kharain 5min |
|---|---|---|---|---|
| `going directly to the interior` | 175 | **0** | 209 | **2** (initial trips) |
| `the rock is at ... standing at` | 187 | **1** | 253 | **4** |
| `mine: striking the rock` | 18 | **16** | 14 | **11** |
| `goal_failed=MINE` | — | 0 | — | 0 |

Strike rate: Draver 0.3/min -> 3.2/min (~11x); Kharain 0.23/min -> 2.2/min (~9x).

Final hop fired as designed on both (g_Draver.console.txt:363,
g_Kharain.console.txt:249):

    smelt: travel stopped 1 tile(s) short of the stand tile beside the forge
    at 2561,501 -- stepping onto 2560,500
    smelt: opening the forge at 2561,501

The shard then answered `You can't reach that` from (2560,500), Chebyshev 1.
So the mine forge at 2561,501 is genuinely unusable from every adjacent tile —
now *proven* by a click rather than *assumed* from a false ARRIVED. The existing
3-try rule wrote it off and both bots left for the real smithy. Kharain smelted
there successfully (g_Kharain.console.txt:356-385, forge 2469,557,
`giving the forge's cursor the ore ... 1 i_ingot_iron so far`).
`cannot get within` = 0 for both; no strike-off loop.

## Open, not fixed (outside this brief)

- `g_Draver.err.txt`: `[bot] no path to (2471,564) avoiding 0 block(s);
  stopping (search 899684.7us)` from (2488,552) — A* cannot route into The
  Forgery for Draver on this attempt, while Kharain entered the same building
  fine. Indoor/door pathing, ~0.9 s search. Separate defect.
- `kLegArriveSlack` still overrides an explicit `arriveRadius = 0` for every
  `TravelToPoint` caller. Only DoSmelt is compensated here. Callers that need an
  exact tile should either compensate the same way or travel should honour the
  requested radius on the final leg — a wider change than this brief allows.
