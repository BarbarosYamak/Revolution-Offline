---
name: thresholds-are-rates-not-numbers
description: The owner's "500-600 ingots before training" is a RATE (5.5 units per skill point), not a constant; per-character thresholds must scale with what the plan has left to do
metadata:
  type: feedback
---

When the owner states a stocking or threshold number, store it as a RATE against
the work remaining, not as a literal constant.

**Why:** "you cant train with only 15-20 iron first you need to stock some maybe
500-600 then you start train blacksmith" describes a FULL 0->100.0 Blacksmithing
climb — 100 skill points, ~550 units, i.e. 5.5 units per point. A flat 550 is
exactly the global constant the owner's own rule forbids ("keep/bank/surplus
counts derive from plans, wealth and prices per character, not global
constants"). A smith at 70.0 aiming for 100.0 should bank 165, and hoarding 550
would stop it selling anything for the rest of its life.

**How to apply:** any new keep/bank/surplus threshold takes the build plan and
the observed sheet as inputs and produces a different number for two different
characters. Prove it in the test by printing both caps and asserting they
differ. Worked example: `market::MaterialSurplusCap` — lumberjack/i_log 85 vs
miner_smith/i_ingot_iron 630, each term traceable to a recipe row or a
catalogue count. Related: [[craft-focus-rotates]].
