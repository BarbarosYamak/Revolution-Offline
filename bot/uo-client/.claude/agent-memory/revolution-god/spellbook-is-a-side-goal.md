---
name: spellbook-is-a-side-goal
description: Owner rule 2026-09-04 — every caster/scribe has a standing side goal to complete the spellbook: buy scrolls from NPC scribes/mages when stocked (circles 1-4), otherwise hunt monsters for scroll drops; scribing a scroll requires the spell in the book
metadata:
  type: feedback
---

Completing the spellbook is a standing SIDE goal for mages, scribes, warlocks, tamers (any spellbook carrier): buy the scroll from an NPC scribe/mage if the shop has it, otherwise hunt monsters and loot scroll drops. Never granted.

**Why:** owner 2026-09-04. Also a hard dependency: the shard's inscription craft menu only offers spells already in the scribe's book (`REFUSE_MISSING_RECIPE ... none of 'Spell Circle 4' / 'recall'`, g_Lyra/g_Thalia 13:03, 2026-09-04) — a scribe cannot climb to the Recall rung without Recall in the book. Mage shop stocks circles 1-4 only (Production.cpp note); circles 5-8 come from PvM loot or player scribes.

**How to apply:** ChooseCraft skips scroll rungs whose spell is not in the book; NeedSpells targets the next rung's spell first; FILL_SPELLBOOK buys when stocked else hands to a hunt goal that loots scrolls. Side goal — must not outbid the trade while the trade is workable.
