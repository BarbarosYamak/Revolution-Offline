---
name: travel-to-service-prefers-sightings
description: Client::TravelToService checks a recent NPC sighting BEFORE the region hint, so it cannot be used to leave the town you are stuck in
metadata:
  type: project
---

`Client::TravelToServiceSkipping` (src/travel/ClientTravel.cpp, the body
`TravelToService` forwards to) consults `knowledge_.RecentService(s, ...)`
FIRST and returns a trip to that remembered NPC. The `regionHint` argument is
only reached at the `try_atlas:` label below it.

**Why:** it matters whenever the errand's point is to go somewhere ELSE.
`TravelToService(Banker, "Britain")` issued by a bot standing in Papua walks
to the Papua banker it saw ten seconds ago, not to Britain -- which would
defeat the whole RETURN_HOME goal ([[return-home-goal]]).

**How to apply:** for any errand whose destination is the point (go home, go
to a specific city, leave here), resolve the place from the atlas yourself and
call `TravelToPoint`. Use `TravelToService` only when "the nearest one that
does this job" is genuinely what is wanted.
