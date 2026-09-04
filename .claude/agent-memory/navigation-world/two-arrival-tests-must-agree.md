---
name: two-arrival-tests-must-agree
description: Ping-pong bug class -- an "am I there yet" predicate and the travel gate that acts on it drift apart, so arriving at a resource re-triggers the trip that took you there.
metadata:
  type: project
---

Whenever a goal handler has both (a) a predicate like `atHomeMineInterior` and
(b) a gate that issues the trip, **the gate must consume the predicate**. If
the gate re-derives its own, looser test, arriving cancels itself.

Instance (`src/life/runner/Gather.cpp`, DoMine, fixed 2026-09-04): the
predicate was `centroid distance <= kMineReach(6) OR
client.WithinMiningRegion(...)`; the trip gate right underneath tested only
`TileDist(anchor, here) > kMineReach`. Minoc Mine 1's RECT is 26x27, so a
miner standing on a genuine rock 9 tiles from the interior anchor was inside
the cave *and* past the gate: travel to anchor -> rock scan walks back out ->
travel to anchor, forever. 60-minute wave: Draver 175 interior trips / 187
rock walks / **18 strikes**; Kharain 209 / 253 / **14**. After the fix, 5-minute
gate: 0 and 2 interior trips, 16 and 11 strikes.

**Why this recurs:** a fix gets applied to the predicate (the thing that reads
wrong in the log) and not to the branch that actually moves the character. The
2026-09-02 comment in that same function describes this exact ping-pong and
claims to have fixed it — it fixed the predicate only.

**How to apply:** when triaging a "goes somewhere, comes back, repeat" log,
count the two lines against each other (`going directly to ...` vs `... standing
at ...`) and then check that the travel `if` and the arrival predicate reference
the *same* expression. Grep for a predicate computed but used only once.

Related: [[travel-arrived-is-not-arrived]], [[resources-own-tile-bug-class]].
