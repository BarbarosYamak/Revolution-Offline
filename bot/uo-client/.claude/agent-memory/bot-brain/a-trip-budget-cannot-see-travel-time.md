---
name: a-trip-budget-cannot-see-travel-time
description: Bounding a shopping errand by trip count lets it eat a whole session, because one shop trip is ~60s of walking; bound the shopping half by a clock instead
metadata:
  type: feedback
---

An errand that walks between shops must be bounded in TIME, not only in trips.
A trip counter of 3 sounds tight and is not: one shop trip on this shard costs
about a minute of travel, so "under the trip budget" and "has eaten the whole
five-minute session" are the same state.

**Why:** FILL_SPELLBOOK held 100% of Aurelius's session at `kMaxSpellbookTrips
= 3` — three trips, four minutes of walking, no scroll, no cast. The
accompanying 240 s cooldown was the second half of the trap: short enough to
re-arm inside the very session that had just proved the street empty.

**How to apply:** when a Do* handler both (a) travels and (b) has a
stand-down, put the clock on the *shopping* half only — reading a book,
draining the pack, anything that costs seconds and always achieves something
stays unbounded. Reset the clock on real observed progress, and guard the mark
against staleness (`gap > 60 s` means the planner took the turn away and gave
it back, so start a fresh stretch, not a blown budget — the same rule
`clothMarkMs_` uses).

Pair it with an escalating rest whose base matches how structural the shortage
is: `life::ScrollShoppingRestMs` is 15 min doubling to 60, against
BUY_SUPPLIES's 119 s, because a reagent shelf restocks and a spell-scroll
seller may simply not exist. See [[goals-that-spin]] and
[[demand-needs-a-voice]]; evidence in
`bot/uo-client/artifacts/fill_spellbook_yield_2026-09-02.md`.
