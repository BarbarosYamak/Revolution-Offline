---
name: two-sale-questions
description: "Will an NPC pay" and "would anyone buy" are different questions; conflating them silently disabled three whole professions
metadata:
  type: project
---

`faucet::AllowedForItem` answers **"may this be sold to an NPC"**. It is not the
question a crafter asks before working, which is **"would anyone buy this"**.
`Policy::RefusePlayerMarket` and `Policy::RefuseAuthenticity` refuse the NPC and
keep the player market open — their own reason strings say so.

**Why:** ChooseCraft used AllowedForItem as the craft gate, so tailor,
merchant_tinker and lumberjack_swordsman — none of whom make anything an NPC may
buy, by design — produced BLOCKED_NEED CRAFT "this life makes nothing sellable"
for whole sessions (wave 2, 2026-09-01, Aelia x44). The justifying comment said
the player market "cannot yet" complete a sale; it could, from 2026-08-30.

**How to apply:** when a life reports it has nothing to do, check whether some
gate is asking the NPC question on behalf of the player economy. Related traps:
a faucet row's `profession` field must be the CATALOGUE id (the tinker row said
"tinker" where the catalogue says "merchant_tinker"), and an item with no row at
all is Unrecorded, not refused — but Unrecorded must be paired with independent
evidence that it is that trade's own work before it counts. See
[[goals-addressed-to-nobody]], [[grader-covers-17-families]].
