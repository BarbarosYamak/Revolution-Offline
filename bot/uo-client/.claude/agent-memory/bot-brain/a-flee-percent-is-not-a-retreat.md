---
name: a-flee-percent-is-not-a-retreat
description: A bail threshold in percent ignores the reaction window; express it in expected hits to live, and count the whole board, not just attackersOnMe
metadata:
  type: feedback
---

A flee threshold expressed as a percentage of the health bar is not a
retreat policy. It has to buy enough hp to survive the blows that land
*after* the decision — and the decision is acted on up to one observation
poll late.

**Why:** Hector (fencer, 51 hp, Fencing 50.7) died twice on 2026-09-06 in the
weak band of the Britain graveyard with the flee interrupt firing correctly by
its own rule. 02:16: `HP 25%; 2 attacker(s); bail at 30%`, dead one second
later. 23:38: last observed 21/51 (41%) with `bail at 22%`, dead 3.1 s later.
22% of 51 is 11 hp — under two landed blows. The percent was never wrong about
the fraction; it was wrong about the unit. See
`artifacts/novice_engagement_2026-09-06.md` for the hp-per-exchange rows.

**How to apply:** state the line as `attackers * hp-per-blow * blows-of-margin`
and convert to a fraction of *this* character's bar. Two things follow that are
easy to miss:

- **Count the board, not `attackersOnMe`.** Everything inside the crowd radius
  is about to be an attacker; the single-attacker phase of that fight cost
  0.4 hp/s and the three-attacker phase 6.8 hp/s — a 17x difference from the
  same creatures. The group is what kills a novice, not the creature.
- **Combine with `max()`, never `min()`.** Layering an hp-derived floor on top
  of an existing nerve/percent line with `max` is monotone: it can only make a
  character flee earlier, so no life that survives today is made bolder by it.
  A 100-hp veteran with one attacker floors at 16% and its 0.32 nerve line
  still wins — the change is invisible except where it is needed.

Do not derive per-blow damage from a Sphere formula: the shard's
damage-after-armour rule is UNKNOWN and `Observation` carries no AR field
(`artifacts/hunt_tier_gate_2026-09-06.md` s.3). Read it off hp deltas in the
death trace and mark the band it was measured in. Related:
[[an-emergency-overrides-the-catalogue]], [[a-category-is-not-a-difficulty]].
