---
name: a-short-shelf-is-not-the-trade
description: A vendor window that lacks the item is one keeper's restock roll, not the trade's stock list; skip that serial, forget the offer, ask the next keeper, then goal_failed
metadata:
  type: feedback
---

A read shop window that does not contain the wanted item is evidence about
ONE NPC, never about the trade. Record that keeper's serial on a skip list,
call `Client::ForgetVendorOffer()`, and ask
`NearestShopkeeperWithTrade(trade, svc, &skip)` for somebody else of the same
trade. Only when nobody unasked is in sight does it become
`goal_failed` + `planner_.Cooldown` + `planner_.Finish(false, ...)`.

**Why:** `goal_blocked` + an 8 s delay leaves the stale offer in place, so the
next tick reopens the SAME shelf. Odessa reopened one tinker's three-item
window 22 times and tripped `goal_spinning=GET_TOOL`
(artifacts/fleet_ramp_20260906/Odessa.console.txt:2925-3059). The trade row was
never wrong: c_blacksmith, c_blacksmith_f, c_weaponsmith_blade and
c_weaponsmith_blade_f all carry VENDOR_S_WEAPONS_BLADED, which sells i_dagger
(runtime/scripts/templates/tm_vend.scp:1794-1804). A four-item list means that
keeper's RESTOCK had barely run — and the restock timer is not saved, so it
differs per NPC and per boot.

**How to apply:** whenever a purchase goal reads an open vendor list and misses.
`ForgetVendorOffer` is load-bearing: without it `VendorOfferFrom()` keeps naming
the dead window. Approach and open the next keeper INSIDE that branch —
falling back to the goal's travel arm restarts a walk to the service the
character is already standing in, because `travelInFlight_` is already false.
See [[one-npc-is-not-the-trade]], [[goal-that-did-nothing-must-stand-down]].
