---
name: bandage-market-gap
description: Bandages have no player-market catalog entry at all — no tailor WTS, no fighter WTB, only NPC counters and self-cut cloth
metadata:
  type: project
---

Confirmed 2026-09-07 (fleet122, 84 fighters vs 17 healers + 2 vets):
`i_bandage` never appears in `src/economy/Market.cpp` (`grep -in bandage`
returns zero). No profession's `Surplus()`/`PlayerMarketWants()` catalog
lists it, so `ChooseSellOffer`/`ChooseBuyWant` can never form a bandage
`TradeIntent` — the WTB/WTS handshake (`FormatSellOffer`, announce schedule,
`DoTradeWithPlayer`) is fully built and used for logs/ingots, but structurally
unreachable for bandages today.

On the fighter side, `BandageCountersAllDrained` (`src/life/runner/Gear.cpp`)
hands off straight to `MakeBandages` (self-cut) the moment NPC shelves are
empty — it never tries a WTB first, even though the general trade goal
exists.

**Why:** nobody had wired the two systems together; bandages were treated
purely as an NPC-counter-or-self-cut good, not a tradeable good, from the
start.

**How to apply:** before recommending "just buy bandages from a player,"
confirm this gap has been closed (grep `Market.cpp` for `"bandage"` and
check `Gear.cpp`'s `StandDownBandageShopping` for a `TradeWithPlayer`
hand-off) — as of this date it has not. Cutting a bolt/cloth into bandages
itself needs no Tailoring skill (Source-X hardcodes the cut,
`type_scissors.scp` hands `t_clothing`/`t_cloth`/`t_cloth_bolt` back to the
engine with no skill check), so any bot with scissors can produce sellable
bandages — the missing piece is purely the market-catalog wiring, not a
skill gate. See `docs/BANDAGE_SUPPLY_SPEC.md` for the full spec and
[[vendor-payout-rules]] for the price-band math.
