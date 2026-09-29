---
name: state-flags-need-the-latest-statement
description: Client state read as "was it ever said since login" latches forever; a changing state must be read as the NEWEST server statement, and the action's own result message is one of those statements
metadata:
  type: project
---

A server-reported state that can CHANGE must be read as "what did the server
say most recently", never as "did the server ever say it since session start".

**Why:** hunger was read with `JournalSaidSince("you are hungry",
sessionStart)`. The shard says that line once per food-decay tick and only
below 40% — `Regen3=1800` in runtime/sphere.ini, so once per 30 minutes. One
login-time line latched `hungry=1` for the whole session: seven characters ate
their packs empty, walked to the provisioner, bought more, ate that, ~200
times each, while the world save showed them at FOOD=14/15. The flag, not the
eating, was the loop.

**How to apply:** when a state comes from journal text, collect every phrase
that states it, look each one up by TIME (`Client::JournalLastSaidMs`) and let
the newest win. Crucially, the outcome message of the action that CHANGES the
state is itself a statement of the new state — Sphere's `food_full_N` lines
are hunger readings on the same STAT_FOOD as "You are hungry"
(`act::HungerStatements` maps both onto one scale). Same shape applies to any
future flag read this way: poisoned, hidden, criminal, war mode.
