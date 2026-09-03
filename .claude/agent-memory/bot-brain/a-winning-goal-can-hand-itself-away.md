---
name: a-winning-goal-can-hand-itself-away
description: TRAIN_COMBAT won every fighter session at 84.5 and still produced 0 kills, because the handler handed off on entry and cooled itself; a handoff is only advice, so the receiver must also out-score the field
metadata:
  type: project
---

A goal winning the scoring proves nothing about whether it ran. TRAIN_COMBAT
scored 84.5 and was SELECTED on four fighters (Hector, Castor, Faustus, Titus,
2026-09-03) and all four ended `kills=0` — because `DoTrainCombat` handed off to
UPGRADE_GEAR on entry whenever `HasBasicArmor()` was false, cooling itself for
240 s (a whole gate session) in the process.

**Why:** `HandOff(from, to, restMs, ...)` cools `from` and only *logs* `to`.
The receiving goal is re-chosen by `Planner::Select` from the ordinary scores.
NeedGear was a flat 0.22 -> UPGRADE_GEAR 22.0, which lost to HARVEST_WOOL (42)
and REPLACE_EQUIPMENT (72.8). So the winner stood down in favour of a goal that
was never picked, and the character did a third thing entirely.

**How to apply:** when reading a "goal never happened" defect, check the
handler's first twenty lines for a handoff before touching any weight. Two
follow-on rules, both paid for live:
- a handoff's advised goal must be able to WIN the next Select — if it cannot,
  either raise its need for exactly the condition that triggered the handoff
  (starter-armour 0.75 vs the standing browse 0.22) or do not hand off at all;
- the rest given to the goal that steps aside must be short enough for it to
  come back inside the same session (`kShortRestMs`, not `kGearCooldownMs`).
And give the handler an escape: if the advised errand has already cooled itself
after failing, the original goal must proceed with what it has rather than
asking again by proxy forever.

Related: [[goal-that-did-nothing-must-stand-down]], [[goals-addressed-to-nobody]].
