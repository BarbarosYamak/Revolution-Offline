---
name: interim-npc-sales
description: Owner ruling 2026-09-02 — until bots genuinely need each other's goods, materials MAY be sold to NPC vendors as fallback; player-first stays preferred; must be a VendorPolicy switch so it can be re-tightened
metadata:
  type: feedback
---

Interim rule (owner, 2026-09-02, refined same day): materials (logs,
ingots, cloth, hides…) are for CRAFTING, not selling. Gatherers keep what
their own craft needs, offer the rest to crafter bots (WTS), bank unsold.
NPC sale of materials only for surplus above a plan-derived bank cap —
last resort, never default. Crafters buy materials from gatherers (WTB),
craft, and sell FINISHED GOODS to NPC (the interim floor) or players.
If no NPC class buys an item, bank it and cool the goal down.

**Why:** "Materials never to NPCs" produced REFUSE_NO_KNOWN_BUYER for 4+
chars in the 2026-09-01 wave — goals addressed to nobody, gold never moves,
bots stand idle. The player economy where bots need each other's goods
(order books, WTB) is not built yet; until it is, the strict rule starves
everyone.

End goal (owner, 2026-09-02): an *organic* supply-and-demand economy —
demand arises from bots' real needs (smith needs ingots, tailor needs cloth,
mage needs reagents/scrolls, warrior needs armour/weapons), supply from
gatherers/crafters, prices from scarcity. NPC sales are the floor that
keeps gold moving until that loop closes; do not design NPC-selling as the
permanent answer, design it so bot demand outbids it naturally.

Shard fact (2026-09-02): the Aug-30 TNS vendor swap brought in TNS's
commented-out `BUY=i_log/i_board/i_hide*/i_ingot_iron` rows in
`runtime/scripts/templates/tm_vend.scp`, even though
`docs/TNS_WORLD_ECONOMY_DONOR_AUDIT.md` §3.5 had REJECTED that change.
Restored the 23 stock rows by hand (backup in
`bot/uo-client/artifacts/tm_vend_pre_restore_2026-09-02.scp`). Lesson: a
wholesale script swap can re-import a change the audit rejected — diff the
donor against audit verdicts after any swap.

**How to apply:** Implement as a VendorPolicy switch
(`allowMaterialsToNpc`-style, default on), unit-tested both ways, so the
Revolution-authentic rule returns without code surgery once the inter-bot
demand loop exists. Supersedes the strict reading of
[[tailor-cloth-source]] "never NPC" for selling; buying cloth still
player-first then self-gather.
