---
name: caster-reagent-working-set
description: Casters keep kReagentCarry=50 of each reagent in the pack; reagents are kit, never dead-weight banked; NeedBank "reagent stock" + hunt hand-off withdraw them
metadata:
  type: project
---

Reagents are a caster's kit: Bank.cpp withdraws up to kReagentCarry (life.h, 50 each) while the box is open and never deposits `i_reag_*` as dead weight for Mage-strategy lives; Needs.cpp raises NeedBank "reagent stock" when the bank has what the pack lacks; DoTrainCombat hands off to BANK before BUY_SUPPLIES when the missing reagent is banked.

**Why:** Aurelius owned 570 reagents in the bank and cast nothing for two sessions because the generic dead-weight deposit only spares what the profession `consumes` (potions, food).

**How to apply:** the 50 is the arrow rule's twin and equally un-dynamic; owner's "thresholds must be dynamic" still applies — derive from spell cost x casts per session when someone touches it.
