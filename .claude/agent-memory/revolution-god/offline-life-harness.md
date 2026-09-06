---
name: offline-life-harness
description: tests/life_harness — Runner::Tick with real Client offline (clock/observation/send-capture seams); reproduce control-flow bugs in ctest before any live wave
metadata:
  type: project
---

Before 2026-09-05 no test reached Runner::Tick (Client concrete, ~800 direct calls; only wall
clock Client::NowMs). Harness design chosen: OBJECT library of client sources, *ForTest seams
(SetClockForTest, offline send capture, CompleteActionForTest, Runner observation override),
scenarios step obs+nowMs and assert on GetPlanner().Current(), captured packets, handoff/goal lines.

**Why:** owner wants failures reproduced in seconds, live runs reserved for server integration
and archetype verdicts (20% weekly budget).
**How to apply:** for any planner/errand control-flow defect, write the failing harness scenario
FIRST, then fix, then a 5-min smoke on affected chars. Handler internals that read Client world
state (mobiles, containers) see an empty world in the harness — that exercises the "nobody here"
legs; feed packets via DispatchPacketForTest when a populated world is needed.
