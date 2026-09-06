---
name: an-allowance-resets-knowledge-does-not
description: Per-errand counters (trips, chases, approaches) are handed back when the goal is left; lists of places already tried are not — trainTrips_ survived supersession and failed TRAIN_AT_NPC without travelling
metadata:
  type: feedback
---

Runner holds two kinds of per-errand state and they have opposite lifetimes.
An **allowance** (`trainTrips_`, `huntTrips_`, `sellTrips_`, `vendorChases_`,
`trainApproaches_`) is reset when the goal is genuinely left. **Knowledge**
(`trainerShopsTried_`, `sellShopsTried_`, `trainerSilent_`) is not — it was
earned this session and clearing it walks the same road again.

**Why:** 2026-09-06, Kharain. `trainTrips_` was never reset by
`Runner::LeaveGoal`, so BUY_SUPPLIES superseding TRAIN_AT_NPC left the errand
standing on its ceiling; it printed `no 'tinker' reachable after 3 trips`
without a single travel_start and spun x4. Same file already did it right for
the hunt (`HandOffFromHunt` zeroes `huntTrips_`). Second, smaller fault in the
same counter: it incremented before the route was even planned, so a refused
`TravelTo*` still spent an allowance and the stale-note branch had to
`--trainTrips_` back out. Both are the D9 rule — a trip counter counts real
trips — and the honest place to increment is the ARRIVAL branch.

**How to apply:** when adding any bounded-attempt counter, decide its owner
before writing it. If it bounds an errand, reset it in `LeaveGoal` (guarded on
the goal kind, after the Corran same-kind early return) and increment it only
where the thing it counts demonstrably happened. Grep the failure text against
the log: `"after N trips"` with no matching `travel_start` between the picks
is this bug every time. See [[an-errands-exit-cannot-live-in-the-errand]].
