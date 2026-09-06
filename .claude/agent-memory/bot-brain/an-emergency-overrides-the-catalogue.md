---
name: an-emergency-overrides-the-catalogue
description: A profession's consumables list is an upkeep rule, not a rule about dying; the override needs an HP line (the life's own fleeHpFraction) or it breaks the tamer/Veterinary case
metadata:
  type: project
---

The `consumables` catalogue ("so crafter do not buy bandages",
"a bandage on an untrained healer barely helps") answers **what this life
stocks**, not **what it may buy while dying**. A character below its own flee
line with no bandage, no potion and no heal spell buys whatever medicine the
counter in front of it actually stocks.

**Why:** Odessa (merchant_tinker) was resurrected at 6/50 beside the Britain
healer on 2026-09-06. Her catalogue drops bandages in favour of heal potions;
the potion shelf was empty for the third time; so the medical errand had
nothing it was permitted to ask for and she walked to a provisioner at 14% HP
and then north toward the mine. `i_bandage {5 20}` was on that same healer's
list the whole time.

**How to apply:** the override must carry an HP condition. `hp < heal line`
is too loose — it makes a tamer at 66% with Veterinary and no Healing buy
bandages it cannot use, which is a different, correct owner rule and is
guarded by `ScenarioMedicalSuppliesAndDepositBudget` in tests/life_harness.
The line that separates them is the life's own `needCfg_.fleeHpFraction`: the
health at which it already breaks off a fight. In that same emergency,
`HealTuning::minHpToShop` must drop to 0 — regenerating to a safe margin
before shopping is only a plan when the character has something to heal with.

Related: [[a-supplier-is-not-a-customer]], [[thresholds-are-rates-not-numbers]].
