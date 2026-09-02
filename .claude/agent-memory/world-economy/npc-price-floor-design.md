---
name: npc-price-floor-design
description: Owner ruling 2026-09-02 — bots may sell materials to NPCs as a conditional price FLOOR, gated on a switch AND on the player-first WTS window having closed
metadata:
  type: project
---

Owner ruling, 2026-09-02: "until bots genuinely need each other's goods, bots
MAY sell materials (ingots, logs, fish, ore, cloth, hides, feathers…) to NPC
vendors. Player-first stays: shout WTS on schedule, sell to a responding player
if one answers. NPC sale is the legitimate fallback, NOT a refusal. If no NPC
class buys the item, bank it and cool the goal down. Long-term goal is organic
supply/demand where bot demand outbids the NPC floor — so implement NPC selling
as a *price floor*, not the permanent answer."

Implemented as **two conditions ANDed**, never one:
1. `econ::SalePolicy::allowMaterialsToNpc` (default ON, OFF = strict M3.7), and
2. `playersDeclined` — a complete WTS announce cycle nobody answered.

**Why the second condition matters:** a switch alone would silently convert the
floor into the market, which is the outcome the ruling explicitly guards
against. The player-first window is what keeps it a fallback.

**How to apply:**
- `faucet::AllowedForItem` stays STRICT and unchanged — it answers "is there an
  unconditional evidence-backed NPC faucet". The floor is a *separate*
  function (`faucet::NpcFloorOpenFor`) so no existing caller changes meaning.
- The floor never invents a buyer. `market::NpcBuyersFor` still has to find a
  live BUY row; if there is none the item banks. Adding a speculative row is a
  walk across town to be refused.
- The anti-arbitrage ledger test still applies to a floor sale: a floor route
  carries no HistoryEvidence, so buying an input from an NPC and selling the
  output back is still blocked.
- The signal that the window closed is the `no_player_buyer` memory event
  `DoTradeWithPlayer` writes. It existed for a long time before anything read
  it — check for already-written facts before adding new state.

See [[material-buy-rows-commented]], [[sell-goal-spins-on-return-true]].
