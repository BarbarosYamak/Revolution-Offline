---
name: threat-floor-vs-tolerance
description: combat::Classify gives any unhurt red monster ≥0.35 threat before it acts; a riskTolerance below that can never open a lawful fight (mage was 0.30, now 0.50)
metadata:
  type: project
---

Threat model (src/combat/Targeting.cpp): full bar +0.20, murderer noto +0.10, distance +0.05/0.15/0.25, war mode +0.15, attackingMe +0.45. So an idle skeleton in view scores 0.35-0.55 before it does anything.

**Why:** the mage's 0.30 tolerance meant Aurelius refused every skeleton at the graveyard for two sessions ("threat 0.60 above tolerance 0.30"). Set to 0.50 on 2026-09-05; m5_professions requires mage < swordsman (0.55).

**How to apply:** when a profession "never engages", compare its riskTolerance with this floor before touching the target picker. A quiet adjacent monster (0.54) is still refused by the mage until it swings — the attacker record then makes it a defence.
