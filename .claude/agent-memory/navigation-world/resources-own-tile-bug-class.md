---
name: resources-own-tile-bug-class
description: Recurring bug class -- code that remembers a resource's own (often unwalkable) tile as a travel target instead of the character's stand tile. Check every NoteResource/proven-location write for this.
metadata:
  type: project
---

Found twice in the same wave-2 triage pass (2026-09-01/02, see
`artifacts/nav_fix_2026-09-01.md`): `src/life/Runner.cpp`'s fishing code
recorded `fishX_/fishY_` (the open-water tile targeted by the cast) as the
"proven fish resource" location, and the chop-wood code recorded
`chopX_/chopY_` (the tree's own Impassable tile) the same way. Both then
fed straight into `TravelToPoint`/`BestProvenResource` on a later trip,
producing an instantly-and-repeatedly-failing "goal not walkable" (dozens
of retries against the same dead coordinate: Dorvar x60, Halain x34,
Titus x12).

**Why this recurs:** the natural variable to reach for at the moment of
"I succeeded here" is whatever coordinate the action targeted (the cast
target, the chop target), not the character's own position -- but the
character's own position is what's actually walkable and reusable.
`Runner.cpp:9718` already had the correct pattern (`client.PlayerX(),
client.PlayerY()` on dock arrival) right next to the wrong one
(`fishX_/fishY_` on catch) -- the bug was inconsistency within one file,
not an unknown pattern.

**How to apply:** any future `NoteResource`/`BestProvenResource` call (or
equivalent "remember this place" write) should be reviewed for whether it
stores the actor's stand tile vs. the interacted-with object's tile.
Prefer the former unless the object's tile is independently known
walkable. Navigation.cpp now also has a generic backstop
(`nav::FindWalkableNearGoal`, `include/uo/nav_rules.h`) that snaps an
unwalkable literal goal to the nearest standable tile once per trip, but
that is a safety net, not a substitute for storing the right coordinate
in the first place.
