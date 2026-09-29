---
name: mage-combat-ladder
description: Owner rule 2026-09-06 — a mage fights with the full offensive ladder its Magery allows (Magic Arrow, Harm, Poison, Lightning, ...) and, once funded, trains Magery by casting on itself
metadata:
  type: feedback
---

A mage bot must use a proper combat flow "always within his abilities":
open and fight with the best offensive spells its Magery can cast (Magic
Arrow -> Harm -> Poison -> Lightning -> Energy Bolt ...), not only Magic
Arrow. After it has earned some gold it may also train Magery by casting
on itself (practice), buying the scrolls/reagents that requires.

**Why:** owner, 2026-09-06, watching Aurelius fight skeletons with Magic
Arrow only at a 25-mana pool. A Revolution mage's value in PvM/PvP is the
spell ladder; a bot stuck on circle 1 is not playing the class.

**How to apply:** (1) FILL_SPELLBOOK must actually land the next-circle
attack scrolls (mage shop / scribe / player scribes) — a mage with gold and
a missing circle should shop before hunting; (2) hunt spell choice picks
the highest castable damage spell within mana and reagents, with
poison/harm for melee-range fights; (3) self-practice (PRACTICE_SKILL) is
funded by hunting income, so it follows a gold reserve, not a constant;
(4) mana recovery (Meditation to a useful fraction) belongs in the hunt
readiness gate — see D5 in the 2026-09-06 validation notes.
