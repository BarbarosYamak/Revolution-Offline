---
name: craft-focus-rotates
description: satiation exists one level below GoalKind now — CraftFocus rotates a crafter's product after 4 sittings, session-scoped, bounded
metadata:
  type: project
---

Owner rule, 2026-09-01: "full crafters should not only do 1 craft always through
the day; maybe different craft focuses."

`Planner::Satiation` / `FamilySatiation` damp a GOAL and a FAMILY, and CRAFT is
one GoalKind — so neither can see the choice made inside it. `life::CraftFocus`
is the same shape one level down: 4 consecutive **sittings** on one product
satiate it, a different product breaks the streak, and it fades over the same
3-minute window. Bounded — if nothing else is workable the satiated recipe is
still chosen.

**Why:** ChooseCraft returns the first workable entry of `produces` in list
order, so a full_crafter made the same thing all session and the goal histogram
could not tell.

**How to apply:** `NoteMade` is called when a SITTING ends (`CraftStep::Done`),
never per piece — counting pieces flips the focus mid-batch. It is session-
scoped on purpose: the rule is about the shape of a day, and a preference that
survived logout would decide tomorrow's first sitting from yesterday's mood. The
telemetry line is `craft_focus=<item> sittings_in_a_row=<n>`.
