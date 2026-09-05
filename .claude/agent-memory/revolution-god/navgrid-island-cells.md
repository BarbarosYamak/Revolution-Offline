---
name: navgrid-island-cells
description: Navgrid has passable cells with zero edges (islands); NearestPassable does not fix them, PickServicePlace silently skips unroutable candidates; Papua healer was one (fixed f65225b)
metadata:
  type: project
---

Navgrid 16-tile cells can be passable yet have no open edges ("island"
cells). `RoutePlanner::Plan` only snapped *impassable* start/goal cells, so a
start or goal on an island failed A* outright, and
`PickServicePlace` drops any candidate whose route is `!ok` **without logging**.

**Why:** Faustus' ghost at Papua (5674,3136) could not reach `papua_healer`
20 tiles away and was sent cross-map. Fix: `SnapToConnected` ring search
(3 rings) for start (needs outbound edge) and goal (needs inbound edge).
Regression test `TestRealAtlasGhostInPapua` in tests/m9_service_selection.cpp
uses the real atlas.

**How to apply:** "no route" to a place that is visibly nearby -> suspect an
island cell before suspecting the atlas. Any silent candidate skip in service
selection should be logged if it bites again.

Also learned same session: hunting ground is chosen by skill tier (<60 ->
Britain Graveyard) regardless of home city, while bank returns anchor on the
home bank; Papua is unguarded (`wilderness` flags 0) in the shard's own area
files; sphere.ini `Regen0=40` = 1 HP per 40 s.
