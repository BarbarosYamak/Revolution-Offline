---
name: no-npc-sells-it-is-not-no-source
description: Buy-side and sell-side refusals are different; "no NPC sells this material" routes to self-produce or the player market, never to goal failure
metadata:
  type: project
---

`REFUSE_NO_KNOWN_BUYER` is the SELL side (we hold a thing, no NPC will take
it). `REFUSE_NO_KNOWN_SUPPLIER` is the BUY side (we are short of an input, no
NPC sells it). They are separate `faucet::Refusal` values and must stay so.

**Why:** the buy errand logged the seller's word for it, so
`REFUSE_NO_KNOWN_BUYER item=i_ingot_iron` read as a broken lookup when it was
a smith who should have gone mining. Three of the 2026-09-01 wave's characters
were shopping for their own supply chain: a fisher short of fish, a smith
short of ingots, an archer short of logs. Refusing the NPC was CORRECT
(materials never come from shopkeepers) — stopping there was the bug.

**How to apply:** `market::RouteForInput` (`include/uo/market.h`) decides:
NpcVendor (a known shopkeeper trade — keeps precedence, so working errands are
untouched) / SelfProduce (in this life's own `produces`) / PlayerMarket
(another profession's `produces`) / NoKnownSource. When a supply lookup comes
back empty, ask this before failing a goal. Never widen the vendor table to
make a material buyable from an NPC — the whole point is that the route
changes, not the rule.

Related: [[los-is-reach-not-identity]].
