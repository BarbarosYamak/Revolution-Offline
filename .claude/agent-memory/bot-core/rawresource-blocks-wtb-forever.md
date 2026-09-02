---
name: rawresource-blocks-wtb-forever
description: NeedCloth's "ask the market first" gate waited on an event (no_player_seller) that market::Shortfall's rawResource filter made structurally impossible to write for wool-chain intermediates
metadata:
  type: project
---

`NeedCloth` (Needs.cpp) gates self-gathering cloth behind
`obs.NoSellerFor(item)`, which only turns true once `DoTradeWithPlayer`'s
buyer-ask loop times out unanswered and writes `no_player_seller`
(Runner.cpp:8426). That write only fires for items in
`market::PlayerMarketWants()`'s `buyable` list, which drops anything
`rawResource` -- and `Shortfall()` (Market.cpp) computed `rawResource =
WhoProduces(item).empty()`, scanning every profession's `produces`
catalogue. No profession lists "i_yarn_ball"/"i_wool"/"i_cloth" as a
*produced-to-sell* good (only the finished `i_cloth_bolt` is), so a want
for yarn was always rawResource=true, always dropped, the WTB never
announced, `no_player_seller` never written, and NeedCloth sat BLOCKED on
"the player market has not been asked for it yet" forever (85x/75x,
g_Aelia/g_Amara consoles, 2026-09-02).

**Why:** `WhoProduces()` checks a *finished-goods* catalogue
(`prof::Profession::produces`), but the thing being asked about is an
*intermediate* on the asker's own gather chain. Those are different
questions -- "does anyone sell this as an end product" vs "could another
character plausibly have surplus of this" -- and conflating them makes any
intermediate material permanently unaskable, which starves any Need that
is designed to wait for a real market answer before falling back to
self-production.

**How to apply:** When a Need is written as "ask the market, wait for a
real decline, then self-produce," verify the decline event can actually be
generated for that *exact* item string by tracing the want all the way
through `Shortfall`/`PlayerMarketWants`/`rawResource`, not just by reading
the Need's own code. Fixed in `src/economy/Market.cpp` (`IsWoolChainWant`,
local to Market.cpp -- `market` sits below `life` in the include graph so
it cannot call `life::IsWoolChainMaterial`) rather than in `Needs.cpp`,
because the Need's own logic and its unit test (`m4_life.cpp`, "NeedCloth
waits for the player market to decline") were already correct in isolation;
the break was entirely in whether the upstream event could ever exist.
See [[goal-that-did-nothing-must-stand-down]] and
[[goals-addressed-to-nobody]] for the same failure family elsewhere.
