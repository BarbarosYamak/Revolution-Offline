# Player-trade handshake: cancel spam + two-seller race (2026-09-02)

## 1. The original sequence (run_gates, wave 09:08-09:39)

All times from `run_gates/g_*.console.txt` of the defect wave.

| t | who | line |
|---|---|---|
| 09:11:38.223 | Kharain | `trade: announcing 'WTS 10 i_ingot_iron 35gp'` (Kharain.console:586) |
| 09:11:38.244 | Odessa | `trade: heard 'WTS 10 i_ingot_iron 35gp' from Kharain -> want it` (Odessa.console:546), says bare `WTB i_ingot_iron` (Odessa.console:547-548) |
| 09:11:38.524 | Kharain | hears it, `trade: Odessa wants our i_ingot_iron` (Kharain.console:599) |
| 09:11:39.925 | Elvar | hears the SAME bare line, `trade: Odessa wants our i_ingot_iron` (Elvar.console:592) |
| 09:11:48.320 | Kharain | `trade: opening a window with Odessa for 10 i_ingot_iron` |
| 09:11:49.100 | Odessa | `trade: offering 350 gold for 10 i_ingot_iron` |
| 09:11:50.688 | Elvar | `trade: opening a window with Odessa for 28 i_ingot_iron` |
| 09:11:51.152 / 53.739 | Odessa | `trade: partner offered 1 line(s); accepting` — twice, two different windows |
| 09:12:14.784 | Odessa | `trade: Kharain put nothing in after 25s -- cancelling` |
| 09:12:14.785 -> ~09:14 | Odessa | `trade:  cancelled (partner_cancelled)` ~200x at 60 ms |
| 09:12:15.991 | Kharain | `trade: Odessa put nothing in after 25s -- cancelling` |

Three independent defects:

1. **The reply named nobody.** `FormatBuyReply` emitted `"WTB i_ingot_iron"`.
   Every seller with a live `tradeOffer_` for that item read it as the answer
   to its own WTS, so two sellers committed to one buyer.
2. **The second OPEN clobbered the first.** `Client::OnSecureTrade` case 0
   called `trade_.OnOpened(...)` unconditionally, overwriting
   `myContainer_`/`theirContainer_`. Gold already in window 1 and the accept
   tick that followed addressed containers the state no longer named.
3. **The cancel was not idempotent.** `trade::TradeState` latches
   `Phase::Cancelled` until something clears it. `DoTradeWithPlayer`'s
   Cancelled branch reset only the Runner's own bookkeeping and returned
   `false`, so it re-fired every tick — partner name blanked by the first
   pass, hence the empty name in the spam. The goal was never finished, so
   the sellers stayed on TRADE_WITH_PLAYER standing at the bank.

## 2. What changed

- `market.h` / `Market.cpp`: `FormatBuyReply(item, toWhom)`,
  `SpeechAddressee`, `AddressedTo`, `FormatDecline`, `ParseDecline`.
- `Runner.cpp` listen loop: buyer names the seller it accepts; a reply
  addressed to another player is ignored; a second seller offering the same
  item while committed is told `"<name>, sorry -- sorted"` once and skipped;
  a decline from our own partner ends the seller's goal.
- `ClientTrade.cpp`: a second OPEN from a different partner is closed on the
  wire (`SECURE_TRADE_CLOSE` on the new container), logged as
  `event trade_declined`; the live window is untouched.
- `Client.h`: `TradeForget()` (clears only a non-active window),
  `TakeDeclinedTrade()`.
- All three end-of-trade paths (Completed / Cancelled / give-up timeout) now
  `TradeForget()` + `NoteAttempt` + `Cooldown(kTradeRetryRestMs)` +
  `Finish(false, reason)`. `kTradeRetryRestMs = kTradeGiveUpMs +
  2 * kAnnounceIntervalMs` = 41 s, derived from the handshake's own turn
  times rather than a global constant.

## 3. Runtime evidence, 30-minute run 12:50-13:20

`trade:.*cancelled` line counts, defect wave -> this run:

| char | before | after |
|---|---|---|
| Odessa | 2562 `trade:` lines, ~200 cancel spam | 0 |
| Kharain | spam | 2 (one per episode, the first pass only) |
| Elvar | spam | 2 |

No `goal_spinning=TRADE_WITH_PLAYER` in any of the three consoles. No
character sat on TRADE_WITH_PLAYER after a cancel: Odessa picked
TRADE_WITH_PLAYER twice in 30 min and spent the rest on EXPLORE (36) /
BUY_SUPPLIES (8); Kharain and Elvar both reached
`trade: nobody answered 6 offers ... -- back to work` and stood down.

## 4. Still broken — NOT the assigned defect

**The buyer never funds a window it did not initiate.** Zero
`trade: offering N gold` lines in the whole 30-minute run
(`grep -c "trade: offering .* gold" run_gates/g_Odessa.console.txt` = 0).

Sequence, g_Odessa.console.txt 12:53:57-12:54:29:

- Odessa broadcasts `WTB 8 i_ingot_iron 52gp` (the demand-side ANNOUNCE
  path, `FormatBuyWant`).
- Elvar answers by opening a window: `[trade] window open with 'Elvar'`
  12:54:02.974, and puts an item in.
- Odessa's `tradePartner_` is still 0 and `tradeWantQty_` /
  `tradeOfferPrice_` are still 0 — they are set only in the "heard a WTS"
  branch of the listen loop, which never ran for this deal.
- `DriveOpenTrade` computes `owed = tradeWantQty_ * tradeOfferPrice_ = 0`,
  offers nothing, accepts an empty-on-our-side window twice, and times out
  at 25 s. Both sides cancel. Same shape at 12:57 / 12:59 / 13:02.

**Speaker names arrive empty.** `trade:  wants our i_ingot_iron`
(Kharain.console:521, Elvar.console:340), `trade: opening a window with  for
47 i_ingot_iron` (Kharain.console:539), `trade: heard 'WTS ...' from ` — the
journal `Heard::name` is empty for most of these events and populated for
some (`trade: Odessa wants our i_ingot_iron`, Elvar.console:947). An empty
name makes `FormatBuyReply(item, "")` fall back to the unaddressed form, so
the addressing fix silently degrades to the old broadcast behaviour whenever
the name has not resolved yet. The addressee mechanism is correct; its input
is not reliable.

Neither was reproducible against the two-run budget after the fix landed.
