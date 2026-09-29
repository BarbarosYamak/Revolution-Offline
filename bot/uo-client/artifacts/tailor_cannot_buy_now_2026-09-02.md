# "Cannot buy now" counts as a market decline — NeedCloth fix (2026-09-02)

## Root cause

`NeedCloth` (Needs.cpp) gated the self chain on `obs.NoSellerFor(item)`, which
is only ever written by `DoTradeWithPlayer` after a WTB window expires
unanswered (Runner.cpp:8426). Two refusals happen BEFORE anything is
announced, and neither writes that event:

1. capital gate — `PlayerMarketWants` returns an empty want list when
   `gold - blindPriceCeiling < goldReserve` (Market.cpp:290). Aelia ran the
   whole gate at gold=0 (reserve 400), so no NeedTrade row existed,
   TRADE_WITH_PLAYER never ran, and nothing could ever announce.
2. trip-time gate — `DoTradeWithPlayer` refuses on entry when
   `leftMs < kMarketTripBudgetMs` (Runner.cpp:8116). Amara (need 800 s).

Result: `BLOCKED_NEED MAKE_CLOTH: ... the player market has not been asked for
it yet (20 x i_yarn_ball short)` every tick.
Before: run_gates/g_Aelia.console.txt:83,201,276,...,710 (20 occurrences,
gold=0 start to finish, session_goals had no MAKE_CLOTH pick).

## Fix (lead's rule: WTB-first stays when affordable)

* `include/uo/market.h`, `src/economy/Market.cpp` — extracted the capital test
  as `market::CanAffordToShop(p, gold, policy)`; `PlayerMarketWants` now calls
  it (behaviour identical, one definition).
* `include/uo/life.h`, `src/life/Runner.cpp` (Observe, next to the
  `noSellerFor` fill) — new `Observation::marketTripFitsSession`, the same test
  DoTradeWithPlayer makes on entry, computed once so the pure need model can
  read it. Defaults true.
* `src/life/Needs.cpp` (cloth clause) — `declined = asked || broke || noTime`.
  Distinct reason string per branch; the broke branch's evidence names the
  purse and the reserve. Computed inside the cloth clause only: no other need
  reads either flag, so no other profession's player-first rule changes, and
  no wool/yarn/cloth is ever bought from an NPC.
* No memory event is faked. `no_player_seller` still means "we asked and
  nobody answered".

## Unit test

`tests/m4_life.cpp`, inside `TestClothIsBoughtFromPlayersBeforeItIsSheared`
("needs: NeedCloth waits for the player market to decline"): gold=0 tailor
fires NeedCloth unblocked with urgency > 0 and names the purse in evidence;
`marketTripFitsSession=false` funded tailor likewise; funded + time-to-spare
tailor still BLOCKED at urgency 0 (WTB first); a broke tailor's NeedSupplies /
NeedTrade rows stay blocked (per-need, not a global bypass).
`python tools/rev.py build test` -> 43/43, m4_life 587 checks / 0 failures.

## Live smoke — `rev.py gates CHARS=Aelia MINUTES=5`, twice

Run A (21:20-21:28, run_gates/g_Aelia.console.txt of that run):
* :404 `goal=MAKE_CLOTH` picked, score 74.2
* :408 `reason: 20 x i_yarn_ball short, 0 gold on hand, reserve 400`
* :412 `cloth: no sheep in sight -- walking to the flock of 15 at 572,1096`
* travel plan 25 legs ~1048 tiles; `pasture ARRIVED at (568,1102)` at
  21:28:10, i.e. the whole 5 minutes was the walk; clean logout at the pasture.

Run B (21:28-21:34, current run_gates/g_Aelia.console.txt):
* :59-114 `cloth: a sheep 7/3/2 tiles away -- walking up to it` ->
  `cloth: shearing a sheep (0 wool carried)`
* :125 `System: You put the pile of wool in your pack.` — the shear worked.
* :281 `cloth: 1 wool and no spinning wheel in sight -- going to the tailor`,
  22 legs ~880 tiles, `Britain tailor ARRIVED at (1471,1690)` :652 at session
  end.

CRAFT i_sash was not reached, so none of the new sewing-kit / "Misc." / "sash"
lines appear in either run — the two sessions were spent walking.

## Downstream defects observed, NOT in this brief

1. `DoMakeCloth` leaves the flock after ONE sheep (1 wool) and walks 880 tiles
   for a spinning wheel; 20 yarn needs ~7 wool, and the round trip is ~4.5 min
   of session each way. Shear to the wool target before travelling.
2. The pasture the picker chose (572,1096, near Yew) is ~1048 tiles from
   Aelia's Britain home. Two full 5-minute gates were consumed by travel.
3. `cloth: 1 wool and no spinning wheel in sight` prints once per 2.5 s tick
   for the entire walk (~110 lines). Travel is in flight; the line should be
   throttled the way LogErrandReason throttles the others.
