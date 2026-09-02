---
name: gather-when-the-market-declines
description: Owner rule — a bot buys a material from PLAYERS first and only gathers it itself after a WTB window expired unanswered; the gate must be per-item
metadata:
  type: feedback
---

A "go make it yourself" errand must be gated on a **per-item** record that the
player market was actually asked and came back empty — never on a global
"market is quiet" flag, and never on the shortfall alone.

**Why:** owner ruling 2026-09-02 (tailor cloth): buy cloth from players first
(~17 gp/bolt basis, forum topic 94084), otherwise gather it; never buy
cloth/thread/yarn from NPCs. A bot that walks to a distant pasture while
another character stands at the bank with the material for sale has destroyed
the organic supply/demand the whole fleet exists to produce. A global flag
cannot express this: failing to buy logs teaches a tailor nothing about cloth.

**How to apply:** the sell side already had this shape —
`no_player_buyer` events + `Runner::PlayersDeclined`. The buy side is
`no_player_seller` + `Runner::SellersDeclined`, surfaced to the pure need model
as `Observation::noSellerFor`. Raise the gather need at urgency 0.0 and
`blocked=true` until the item appears there: the errand stays visible in
telemetry and cannot be selected, so the trade goal gets the trip first.

**"Cannot buy now" counts as declined** (lead ruling, 2026-09-02, owner
delegated). WTB-first only holds while the ask is *possible*. Three refusals
happen before any WTB is announced and none of them writes the decline event:
gold below `goldReserve + blindPriceCeiling` (the capital gate inside
`PlayerMarketWants`), not enough session left for the market trip, and the WTB
that did go out unanswered. The first two silently dead-ended two tailors for
whole gates. Every waiting gate needs an "and if I can never ask" branch —
compute the refusal in the need itself, keep it inside that one need's clause
so no other profession's player-first rule is loosened, and do not fake the
decline event in memory (a memory that says "we asked" when nobody asked is a
lie the next reader inherits).

Related: [[demand-needs-a-voice]], [[two-sale-questions]],
[[goals-addressed-to-nobody]].
