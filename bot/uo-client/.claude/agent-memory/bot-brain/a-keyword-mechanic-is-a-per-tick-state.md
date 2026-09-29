---
name: a-keyword-mechanic-is-a-per-tick-state
description: Guard calls die with the decision that considered them; a server mechanic gated on a spoken keyword must be re-asked every tick while the condition holds
metadata:
  type: project
---

A server mechanic that answers only to a spoken keyword has to be re-asked
every tick while the condition holds, not decided once when the plan is made.

`CallGuardsIfProtected` (Survive.cpp) was reached from two decision arms —
the near-death flee and DecideHeal's rest — and both are decided ONCE, on
the tile the character is standing on. A retreat that begins outside a
guarded region asks "can the guards hear me?", is told no, and never asks
again, including for the whole stretch after it crosses the town line.
Tordor 2026-09-06 03:12:17-03:13:09: FLEE_TO_GUARDS at 40% HP, forty
seconds later "You are now under the protection of the city guards",
twelve seconds after that dead inside a_townBritain, never having said the
word. Sphere summons guards on the keyword and on nothing else (Source-X
CClientEvent.cpp:1871).

**Why:** reaching protection is not the same as using it, and the code that
knows how to use it does not run again after the move.

**How to apply:** put the ask in a per-tick keeper called from `Tick`
before any goal (`Runner::KeepCallingGuards`, Core.cpp), keep the existing
throttle inside the shouting function, and make the keeper's own condition
falsifiable (full health with nothing in sight shouts at nobody). Same
shape as [[an-errands-exit-cannot-live-in-the-errand]].
