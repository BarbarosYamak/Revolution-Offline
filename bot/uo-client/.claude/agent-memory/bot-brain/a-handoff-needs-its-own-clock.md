---
name: a-handoff-needs-its-own-clock
description: HandOff is advice, so a "wait for the market" branch cannot rely on the market goal ever running — and DoTradeWithPlayer's no_player_seller names whichever want was first in its list, not yours
metadata:
  type: feedback
---

When a handler defers to the player market ("ask a seller before making it
myself"), it must carry its OWN bound on the wait. Two reasons, both structural:

1. `Runner::HandOff(from, to, ...)` is advisory — `to` is logged, never
   dispatched. TRADE_WITH_PLAYER still has to out-score the field and walk to
   the bank. If it never does, nothing else releases the waiting goal.
2. `DoTradeWithPlayer` writes `no_player_seller` for `buyable.front().item` —
   whichever want happened to be first in its list, which need not be the one
   your handler is waiting on. So `SellersDeclined("your_item")` may never
   become true even after a full, unanswered market trip.

Pattern used for bandages (runner/Gear.cpp, 2026-09-07): a per-Runner
`i64 <thing>WtbAskedMs_`, set when the ask route is chosen, cleared when the
cut route is taken. The bound is one announce cycle,
`kMaxAnnounces * kAnnounceIntervalMs` = 48 s — the same number the buyer's own
listen window quotes, and well inside the goal's cooldown. After the call,
"non-zero" IS "asked a player", so tests read the route off one field with no
second flag to drift from it.

**Why:** without the clock a fighter whose TRADE_WITH_PLAYER never wins would
wait for a seller for ever instead of cutting cloth it could cut in one
gesture.

**How to apply:** any new player-first gate gets three exits, all in one pure
resolver — the decline event, its own expiry, and "I could never afford to
ask" (see [[gather-when-the-market-declines]]). Both branches then hand to a
DIFFERENT route, which is what makes clearing the trigger counter safe
([[a-standdown-that-forgets-reopens]]).
