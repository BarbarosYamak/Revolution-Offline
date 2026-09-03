# bot-core memory

- [Sphere reach is two tiles](sphere-reach-is-two-tiles.md) — CanTouch refuses beyond Chebyshev 2; take the threshold from `sphere::kTouchDist`, never a fresh literal
- [LOS is reach, not identity](los-is-reach-not-identity.md) — gating "who is this NPC" on line of sight erased a known alchemist and burned a whole errand
- [No NPC sells it is not no source](no-npc-sells-it-is-not-no-source.md) — buy-side vs sell-side refusals; a missing material routes to self-produce or the player market
- [Vendor shelves are random](vendor-shelves-are-random.md) — a mage shop rolls four random scrolls; "this one has none" is one NPC's roll, not the trade's answer
- [A timeout means an unrecognised answer](action-timeout-means-unrecognised-answer.md) — four times now the server DID reply; read the journal between request and timeout
- [State flags need the latest statement](state-flags-need-the-latest-statement.md) — "was it ever said" latches forever; the action's own outcome line restates the new state
- [Hunger ticks every 30 minutes](hunger-ticks-every-30-minutes.md) — a 5-minute gate cannot observe eating; pick a hungry subject from the world save and run 35+ min
- [A death record outlives the corpse](a-death-record-outlives-the-corpse.md) — corpses decay in 7 min but the record persists; "corpse known" is not "corpse visible", and serial 0 is nobody
- [A deferral needs a bound](a-deferral-needs-a-bound.md) — "not while busy" with no time limit is a permanent veto; the session clock must always win
- [Using a tool wields it](using-a-tool-wields-it.md) — double-click in the pack = wield; the 0x1D is a move not a consumption, it displaces the hands, and re-equipping what is worn strips it
- [A craft route is not a recipe](craft-route-is-not-the-recipe.md) — CRAFT needs a menu route as well as a recipe; WorldProcessed outputs have no menu at all and belong to another goal
- [Verdict attribution can be wrong](verdict-attribution-can-be-wrong.md) — a wave verdict named a character/item pair that is not in the console at all; re-derive from g_*.console.txt first
- [Identity.cpp in progress 2026-09-02](identity-cpp-in-progress-2026-09-02.md) — another agent is adding kCraftMenus tailoring rows; wool cloth chain verified against Source-X, DoMakeCloth already matches it
- [rawResource blocks a WTB forever](rawresource-blocks-wtb-forever.md) — Shortfall's WhoProduces-empty test made yarn/wool/cloth permanently unaskable; fixed in Market.cpp, not Needs.cpp
- [A station is a DUPELIST, not one graphic](a-station-is-a-dupelist-not-one-graphic.md) — wheels/looms are placed as dupe DISPIDs, live in spherestatics.scp, and found-within-10 is not reach-within-2
- [Function-local statics break silently when split](function-local-statics-break-silently-when-split.md) — one definition per lazy table accessor or the loader fills a copy the reader never sees; prove it with dumpbin, not a log line
