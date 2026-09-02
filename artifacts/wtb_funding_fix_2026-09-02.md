# Buyer-initiated (WTB) trade: why it never funded, and what changed

Follow-on to `trade_window_fix_2026-09-02.md` section 4.

## 1. Root cause — the seller never answered, so the buyer never had a deal

The blocker was recorded as "the buyer never funds a window it did not
initiate". That is the symptom. The cause is one branch earlier.

`run_gates/g_Elvar.console.txt:338-340`, 2026-09-02 12:53:57.225:

```
[chat uni  ] : WTB 8 i_ingot_iron 52gp          <- Odessa's broadcast
[life] trade:  wants our i_ingot_iron           <- Elvar's REPLY branch
```

`trade: %s wants our %s` is the *"somebody answered OUR offer"* branch of
`Runner::DoTradeWithPlayer`. Elvar had a live `tradeOffer_` for
`i_ingot_iron`, and `ParseBuyReply` is deliberately tolerant of the full
`WTB <qty> <item> <price>gp` form, so a **demand broadcast** was consumed as
a **reply to the seller's own WTS**. Consequences, in order:

1. Elvar said **nothing** back — the cold-WTB branch (`-- answering 'WTS
   ...'`) never ran, so no WTS ever reached Odessa.
2. Elvar walked over and opened a window (`g_Elvar.console.txt:365-371`).
3. Odessa's listen loop had never seen a WTS, so `tradeWantQty_` /
   `tradeOfferPrice_` were 0; `DriveOpenTrade` computed `owed = 0` and put
   nothing in (`g_Odessa.console.txt:257-284`).
4. Both sides timed out at 25 s. Repeated at 12:57 and 12:59.

`grep -c "trade: offering .* gold" g_Odessa.console.txt` = 0 for the whole
30-minute run.

The empty speaker names in the same lines are `Client::JournalHeardSince`
filling `Heard::name` from `MobileName()` (`src/travel/ClientTravel.cpp:2588`),
which is empty until something has learned the mobile's name.

## 2. What changed

**`include/uo/market.h` + `src/economy/Market.cpp`**

- `enum class BuyLineKind { NotABuyLine, Reply, Announce }` and
  `ClassifyBuyLine(said, out)`. An ANNOUNCE carries a quantity AND a price
  AND names nobody; anything else (bare form, or any WTB addressed to one
  player) is a Reply. `ParseBuyReply` is untouched, so its documented
  tolerance survives — the classifier is what the listen loop branches on.
- `struct FundingDecision { accept, qty, gold, reason }` and
  `FundOpenWindow(planned, goldOnHand, goldReserve)` — how much coin a buyer
  puts into a window it did not itself commit to. Refuses explicitly when
  there is no plan, no price, or not enough coin for one unit; clamps `qty`
  to what the purse still covers rather than promising a number the drag
  would refuse.

**`src/life/Runner.cpp` / `src/life/Runner.h`**

- Listen loop classifies once (`wtbKind`); the reply branch requires
  `Reply`, the cold-WTB branch requires `Announce`. This is the fix for §1.
- The buyer records what it shouts: `tradeWant_` + `tradeWantAskedMs_`, set
  in the WTB announce block, cleared in `ResetTradeState`.
- `DriveOpenTrade` now (a) backfills `tradePartner_` / `tradePartnerName_`
  from the 0x6F SECURE_TRADE_OPEN packet (`tr.PartnerSerial()` /
  `tr.PartnerName()`), which is authoritative where `Heard::name` is not;
  (b) if the window is unplanned on the buyer side, funds it from
  `tradeWant_` via `FundOpenWindow`, sampling `tradePackBefore_` /
  `tradeGoldBefore_` at the same moment. Freshness bound is
  `kListenMs + kAnnounceIntervalMs`, because not every stand-down path
  reaches `ResetTradeState`.

**`tests/m7_market.cpp`** — `TestBuyerFundsTheWindowItAskedFor()`:
classification of the four line shapes, and the funding decision under a
full purse, a shrunken purse, a purse below one unit, no plan, and a plan
with no price.

## 3. Gates

- `python tools/rev.py build` — ok, `uo_client.exe` 2026-09-02 13:45:19.
- `python tools/rev.py test` — **43/43 passed**.

## 4. Smoke runs (two, the budget)

### Run 1 — `gates CHARS=Odessa,Elvar MINUTES=15` (13:46-14:01)

Did not exercise the path. Odessa reached britain_bank_2 and broadcast
`WTB 8 i_ingot_iron 52gp` 19 times (`g_Odessa.console.txt:369-427`), then
`market: nobody answered i_ingot_iron in 180s -- back to work` (:432).
Elvar never came:

```
g_Elvar.console.txt:483  goal_blocked=TRADE_WITH_PLAYER
    reason="not enough session left for the trip" left=646s need=800s
```

**A 15-minute gate structurally cannot stage a seller.** `kMarketTripBudgetMs`
is 800 s, so a market trip can only START in the first ~100 s of a 900 s
session. Measured first TRADE_WITH_PLAYER pick: Elvar +253 s, Kharain +153 s
(`g_Kharain.console.txt:274`, previous wave). Only a character already
standing inside `britain_bank_2`'s radius skips the gate.

### Run 2 — `gates CHARS=Ghalor,Cyras MINUTES=15` (14:00-14:16)

Pair chosen from the world save: both parked ~45 tiles from britain_bank_2,
Cyras (lumberjack) holding 26 `i_log`, Ghalor (full_crafter) holding none
and needing them.

**A player trade completed, goods and gold both moved:**

| t | who | line |
|---|---|---|
| 14:00:35.818 | Cyras | `market: taking 24 i_log to britain_bank_2 (trip 1)` |
| 14:00:41.999 | Ghalor | `market: going to britain_bank_2 to buy 20 i_log (trip 1)` |
| 14:00:59.179 | Cyras | `trade: announcing 'WTS 6 i_log 17gp'` |
| 14:00:59.194 | Ghalor | `trade: heard 'WTS 6 i_log 17gp' from  -> want it` |
| 14:01:01.210 | Cyras | `trade:  wants our i_log` |
| 14:01:10.402 | Ghalor | `trade: offering 102 gold for 6 i_log` |
| 14:01:14.979 | Ghalor | `[trade] window closed (both_accepted)` |
| 14:01:16.502 | Cyras | `trade: window closed complete with Ghalor` |
| 14:01:16.502 | Cyras | `trade: gave 7 i_log to Ghalor for 102 gold` |

What this proves and does not prove:

- **Proved: no regression.** The seller-initiated (WTS) path still closes end
  to end after the classifier change — the bare reply form still routes to
  the reply branch.
- **Proved: the partner-name backfill.** `g_Cyras.console.txt:264-265` names
  Ghalor. Cyras's `tradePartnerName_` came from `Heard::name`, which was
  empty (`:162 trade:  wants our i_log`, `:219 trade: opening a window with
  for 6 i_log`); the name in the completion and ledger lines can only have
  come from the new `tr.PartnerName()` backfill.
- **NOT proved: the WTB-initiated funding path.** Ghalor heard Cyras's WTS
  17 s after arriving, before its own first WTB announce, so it committed
  through the existing `ConsiderOffer` path.
  `grep -c "announcing 'WTB"` = 0 for both characters. Neither
  `ClassifyBuyLine`'s Announce branch nor `FundOpenWindow` ran at runtime.

## 5. Open, found on the way (not fixed here)

- **The buyer can miss its own completed trade.** Ghalor's window closed
  `both_accepted` at 14:01:14.979 and in the SAME millisecond the planner
  switched (`goal_changed=BANK ... BANK 198.0 superseded TRADE_WITH_PLAYER
  79.8`), so `DoTradeWithPlayer`'s `Phase::Completed` branch never ran. No
  `trade: got N i_log`, no `PriceObservation`, no ledger entry on the buyer
  side. The seller recorded both. A completed window should be drained before
  the planner is allowed to re-score.
- **The goods can land on the floor.** `[chat ascii] System: You put the logs
  at your feet. It is too heavy.` — Ghalor was at 209/215 stones. The buyer
  does not check carry room before funding.
- `Heard::name` is still empty for most speech-only encounters; the backfill
  only repairs lines emitted after a window exists.
