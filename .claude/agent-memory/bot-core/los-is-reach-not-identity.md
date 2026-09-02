---
name: los-is-reach-not-identity
description: Never gate "who is this NPC" on line of sight; LOS belongs to the approach/reach step, and using it for identity erases known shopkeepers
metadata:
  type: feedback
---

A line-of-sight test may gate whether an action will SUCCEED. It must never
gate whether a mobile is IDENTIFIED as a candidate.

**Why:** `Client::NearestShopkeeperWithTrade` used to require
`MobileInLineOfSight`. Elara had known "Bret, the alchemist" by paperdoll
title for two minutes and stood 3 tiles from him, but the two shop-counter
tiles between them made LOS false, so the lookup returned 0 and BUY_SUPPLIES
logged "no 'alchemist' found after 4 trips" — while travel was steering at
that same serial. The character was walking to a vendor its own shop lookup
denied existed. A player behind a counter walks round it; it does not conclude
the shop is empty.

**How to apply:** in candidate lookups (shopkeeper, trainer, trade partner,
corpse, resource), prefer the LOS-visible match but fall back to the nearest
known one and let the caller's approach step close the distance. The server
still enforces real reach on the wire, so this cannot buy through a wall.
The symptom to watch for: a goal burning its trip budget while travel logs
`legs=0 plans=0` — trips that consume the allowance without moving.

Related: [[sphere-reach-is-two-tiles]], [[no-npc-sells-it-is-not-no-source]].
