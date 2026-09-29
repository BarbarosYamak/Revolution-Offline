---
name: demand-needs-a-voice
description: Only the seller could speak — the buyer stood silent at the market for 3 minutes; a market where one side cannot announce is half a market, and both parsers must stay one parser
metadata:
  type: project
---

Until 2026-09-02 `Runner::DoTradeWithPlayer`'s buyer half was a pure LISTENER:
it walked to `britain_bank_2` and stood there for `kListenMs` (3 min) saying
nothing. A trade could start only if a gatherer happened to announce the exact
item the buyer happened to need, in that window.

**Why:** the WTS/WTB speech protocol was built seller-first. `FormatBuyReply`
("WTB i_log") was an ANSWER, never an announcement, so demand had no way to
reach a gatherer who had not already shouted.

**How to apply:** the demand path is now `ChooseBuyWant` -> `FormatBuyWant`
("WTB 20 i_log 4gp") -> gatherer's `AnswerBuyWant` -> spoken WTS -> the existing
seller machinery. Two traps worth remembering:
- The shouted ceiling MUST be the number `ConsiderOffer` will accept at the
  window. Advertising a price you then refuse is worse than silence.
- `ParseBuyReply` and `ParseBuyWant` must be ONE parser. The old
  `ParseBuyReply` read the first word after "wtb " as the item, so the moment
  buyers learned to say "WTB 20 i_log" every seller stopped recognising answers
  to its own offer — a total, silent loss of the trade path.

Related: [[goals-addressed-to-nobody]], [[two-sale-questions]].
