# MAKE_CLOTH stuck on "market not asked yet" — root cause and fix (2026-09-02)

## Root cause

`NeedCloth` (Needs.cpp:936-952) correctly gates on `obs.NoSellerFor(clothShort)`,
which only ever becomes true once `DoTradeWithPlayer`'s buyer-ask loop times
out and writes `no_player_seller` (Runner.cpp:8426). That write only happens
for items present in `market::PlayerMarketWants()`'s `buyable` list
(Runner.cpp:8043-8046).

`PlayerMarketWants` (Market.cpp:240-271) drops every `Want` whose
`rawResource` flag is set, and `Shortfall()` (Market.cpp:178-238) computed
`rawResource = WhoProduces(item).empty()`. `WhoProduces` scans every
profession's `produces` catalogue -- and no profession lists "i_yarn_ball",
"i_wool" or "i_cloth" as something it **makes to sell** (only
`i_cloth_bolt` is in `Professions.cpp:1315`). So a want for yarn was
*always* `rawResource = true`, *always* dropped from `buyable`, the WTB
announce/listen loop never ran for it, `no_player_seller` was never written,
and `NeedCloth`'s market-decline gate could never receive an answer --
permanently BLOCKED on "the player market has not been asked for it yet",
85x/75x in `run_gates/g_Aelia.console.txt` / `g_Amara.console.txt`.

This is a goal addressed to nobody: the code *waits* for an event that the
market layer can structurally never produce for wool-chain intermediates.

## Fix

`src/economy/Market.cpp`: added a local `IsWoolChainWant()` (mirrors
`life::IsWoolChainMaterial`, duplicated because `market` sits below `life`
in the include graph) and excluded those four item names from the
`rawResource` computation in `Shortfall()`. Wool/yarn/bolt/cloth are now
eligible to enter `buyable`, get announced as a real WTB, and (when nobody
answers) correctly write `no_player_seller` -- which is exactly the event
`NeedCloth` was written to wait for. `Needs.cpp` and the wool-chain
production/gather logic (`Runner::DoMakeCloth`) are untouched.

## Live confirmation (g_Amara.console.txt, gate 2026-09-02 20:57-21:03)

Before the fix: `NeedTrade` never scored for the yarn shortfall; only
`NeedCloth(cloth BLOCKED 0.00)` appeared, forever.

After the fix (g_Amara.console.txt:143-148):

    goal_changed=TRADE_WITH_PLAYER from=EXPLORE reason="TRADE_WITH_PLAYER 79.8 superseded EXPLORE 15.0"
    reason: NeedTrade urgency 0.55 x 145 = 79.8
    reason: 20 x i_yarn_ball short

The shortfall now drives real goal selection instead of sitting inert. The
mechanism (want generation -> goal scoring -> goal pick) is proven working.

## What the 5-minute smoke did NOT reach, and why (both pre-existing, out of
this brief's scope)

1. **Amara**: the trip to announce the WTB (`Britain innkeeper` waypoint)
   failed navigation -- "sealed in; recovery exhausted"
   (g_Amara.console.txt:196-198) -- before she could actually announce and
   time out. TRADE_WITH_PLAYER went on its 600s cooldown as designed. This
   is a travel/pathing defect, not a market-classification one.
2. **Aelia**: `gold=0` the whole run. `PlayerMarketWants` refuses to even
   compute `buyable` when `gold - blindPriceCeiling < goldReserve`
   (Market.cpp:252-256, "would eat into the reserve") -- a legitimate,
   pre-existing capital gate, unrelated to the rawResource bug. A tailor
   with zero gold cannot window-shop regardless of this fix.

Both are separate, real defects worth a future brief; neither is the
"yarn shortfall treated as a market need instead of a wool step" bug this
brief targeted, and that bug is fixed and unit-tested.

## Build/test

`python tools/rev.py build test` -> 43/43 (m4_life's `NeedCloth waits for
the player market to decline` section, unchanged, still passes -- it tests
`NeedCloth` in isolation given a `no_player_seller` fact, which is exactly
the fact this fix now lets the runtime actually produce).
