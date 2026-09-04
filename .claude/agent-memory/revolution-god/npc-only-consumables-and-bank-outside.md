---
name: npc-only-consumables-and-bank-outside
description: Owner rulings 2026-09-04 — blank scrolls and empty bottles are always bought from NPCs, never traded between players; bots should idle AROUND the bank, not stand inside it all the time
metadata:
  type: feedback
---

1. `i_scroll_blank` and `i_bottle_empty` come from NPC vendors only. No
   WTS/WTB, no player-market routing for either. (`Market.cpp` NpcSellClass
   table still lists `i_scroll_blank` as PlayerMarketGood — to fix.)
2. Bots go INSIDE the bank building to bank and then come back OUT. Social
   time, waiting, shouting, sparring happen around/in front of the bank
   (`britain_bank_2` 1425,1690 area), never a crowd standing inside.

**Why:** owner watching the 2026-09-04 60-min wave: everyone walked into
the bank and stayed; blank scrolls/bottles are cheap NPC staples on
Revolution, not a player trade.

**How to apply:** bank goal = enter, act, exit to an outside spot; idle /
social / market goals pick outside tiles. Supplies logic: those two items
never enter the player-market path. See [[living-world-chains]].
