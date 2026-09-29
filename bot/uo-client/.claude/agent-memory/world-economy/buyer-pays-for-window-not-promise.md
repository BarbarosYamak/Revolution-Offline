---
name: buyer-pays-for-window-not-promise
description: A buyer must price a trade off delivered goods and the seller's real quote, never its own pre-window promise; the poison-guard factor for PriceBook.
metadata:
  type: project
---

Fixed 2026-09-07 in `bot/uo-client/src/life/runner/Economy.cpp`
(`Runner::DriveOpenTrade`, `Runner::DoTradeWithPlayer`'s Completed-phase
branch) and `src/life/Runner.h` (new `tradeOfferedQty_` field). Commit:
"economy: a buyer pays for what is in the window".

**The failure mode.** `DoTradeWithPlayer` short-circuits into
`DriveOpenTrade` the instant `Client::Trade().Active()` is true. A fast
seller can open the trade window before the buyer's own tick has processed
the seller's WTS chat line through the normal "heard a WTS" listen loop. The
old code recovered by funding the window from the BUYER's own broadcast WTB
(ceiling price x full quantity asked) rather than from what the seller
actually said or delivered. Evidence: Baelos WTB'd "118 i_bandage 4gp",
Aelia answered "WTS 10 i_bandage 3gp", and Baelos funded the window at
118 x 4 = 472 gold before either the true qty (10) or true price (3) was
ever read. The completed trade then wrote a `PriceObservation` of
472/10 = 47/unit into PriceBook, which is above the real 2-4gp band for
i_bandage and gets re-announced by whichever character next reads
`BelievedSalePrice` (a poisoned observation propagates through `WTS`/`WTB`
announcements, not just direct re-trade).

**The fix, three parts.** (1) When a window opens that this side did not
plan, search the journal (`Client::JournalHeardSince`) for the partner's own
WTS line before funding anything; if found, the agreed price is the
seller's own ask (capped at the buyer's WTB ceiling, else no deal); if not
found, wait for the give-up timer rather than fund off the buyer's own
ceiling. (2) The buyer never offers gold before `tr.TheirOffer()` is
non-empty; it counts what is actually in the seller's side of the window
(`FindContainerItemByName` against `tr.TheirContainer()`) and offers
`min(delivered, wanted) x agreed_price`. If the partner's side changes after
gold is offered (recount at accept-time mismatches `tradeOfferedQty_`), the
buyer cancels rather than accepts. (3) The seller symmetrically verifies
`goldOffered >= qty x askedPrice` (counted by graphic — gold has no defname
row in econ's `ItemNameForGraphic` table) before accepting.

**Poison guard (`kPoisonPriceFactor = 3`, Economy.cpp).** A completed
trade's real gold/goods ratio is only trusted as a `PlayerTraded`
observation up to 3x the ceiling (buyer side: `tradeWant_.pricePerUnit`, or
`tradeOfferPrice_` if no WTB is on record) or the asked price (seller
side: `tradeOffer_.pricePerUnit`) the deal was priced against. 3x is
chosen because every pre-trade tolerance this codebase already applies
(`ConsiderOffer`, `AnswerBuyWant`) caps at 1.5x a known price — a completed
trade landing beyond triple that is a bookkeeping mismatch, not a real
price. Discarding drops only the `PriceBook::Note` call; the `Ledger::Note`
gold-flow entry is unconditional, since the gold and goods genuinely moved
either way (see [[revolution-economy-rules]] on conservation).

**Cleanup of already-poisoned data.** `tools/purge_poisoned_price_observations.py`
(one-off, dry-run by default, `--apply` to write) removes a named
`PlayerTraded` observation from a character's `bot_data/<id>/state.json`
`prices` array when it matches a known-bad `(item, price, source, who)`
tuple AND is priced more than `--factor`x any other observation this same
book holds for the item. Used once, 2026-09-07, on Aelia/Baelos/Wren/Calar's
real `bot_data` (main tree, not the git-tracked worktree) to remove the
47/unit (Aelia<->Baelos) and 60/unit (Wren<->Calar) `i_bandage` entries this
bug produced. Does not touch ledger/memory/goal history — the trade itself
happened; only the bad price belief is removed.

**Regression coverage.** `tests/life_world_harness.cpp` gained two new
scenarios (via extended `uo::life::RunnerHarnessAccess`): one drives the
full window race with real trade/speech/container packets through an
offline `Client` (`SetOfflineForTest`) and asserts the offered gold is
`delivered x seller's price`, not `asked x ceiling`; the other seeds a
completed-buy state directly to isolate the poison guard from the funding
machinery. Both pass alongside the untouched `m7_market`/`trade_verify`
suites (94% pass rate unaffected by this change; the 3 pre-existing
failures — `m4_life`, `m4_economy_invariant`, `m9_service_selection` — are
missing-data-file/unrelated-need-scoring issues confirmed present on
pristine `393a3b3` too, via `git stash`).
