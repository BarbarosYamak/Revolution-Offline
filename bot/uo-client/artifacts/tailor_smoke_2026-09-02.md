# Tailor smoke gate — Aelia + Amara, 5 min, exe 20:57:17 2026-09-02

Runs: run_gates/g_Aelia.{console,err}.txt, run_gates/g_Amara.{console,err}.txt
Both reached logout_complete (rev.py wait). No deaths, no disconnects.

## Aelia (Britain tailor) — minute-by-minute
- 21:04:24-25 login, start of session; gold=0 at state_before (last_server_report/gold=0)
- 21:04:30 needs tick: MAKE_CLOTH BLOCKED_NEED "player market has not been asked for it yet
  (20 x i_yarn_ball short)"; BUY_SUPPLIES blocked, purse empty; TRAIN_AT_NPC blocked (no
  Tinkering, fee ~300gp unaffordable)
- 21:05:48 TRADE_WITH_PLAYER picked (superseded EXPLORE); 21:05:53 resolved with
  "trade: nothing to announce (no observed price for what is spare) and nothing to buy
  (would eat into the reserve this life keeps for tools)" -> goal_completed progress=0.
  Market.cpp:290 gate: gold(0) - blindPriceCeiling < goldReserve -> always fails at 0 gold.
- 21:05:57 onward: EXPLORE/IDLE_BRIEFLY loop for remainder of the 5 min (7x EXPLORE,
  3x IDLE_BRIEFLY completions, all progress 0/1 wandering)
- 21:07:53 goal_spinning=EXPLORE "completed 5 times in a row with progress 0"
- err.txt: pathing warnings only (no path to 1468,1611 near end), no crash

Session summary line (21:09:25): picks=19, wander=53%, upkeep=26%, work=11%,
MAKE_CLOTH never entered CRAFT.

## Amara (Britain tailor) — minute-by-minute
- 21:04:27-28 login
- 21:04:50 needs tick: same MAKE_CLOTH BLOCKED_NEED yarn-ball message; market resolved to
  britain_bank_2 (Britain banker) at 1425,1690
- 21:04:50 TRADE_WITH_PLAYER picked, but 21:04:54 goal_blocked "not enough session left
  for the trip" left=272s need=800s (5-min gate too short for this trip, not a reserve block)
- 21:05:19 travel_failed: "Yew provisioner FAILED at (1124,362,5) -- sealed in;
  recovery exhausted" — first of repeated seal-ins at the same coordinate
- 21:05:56-21:06:35 GET_FOOD abandoned after 5 attempts, then failed "no 'provisioner'
  answered after 3 trip(s)"
- 21:06:35 BUY_SUPPLIES goal_failed reason=REFUSE_NO_KNOWN_SUPPLIER item=i_yarn_ball
  route=NO_KNOWN_SOURCE (no vendor knowledge for yarn either)
- 21:07:06-21:09:07 four more goal_failed=TRAVEL_TO_REQUIRED_PLACE "three trips did not
  arrive", interleaved with one more BUY_SUPPLIES REFUSE_NO_KNOWN_SUPPLIER at 21:08:43
- 21:09:33 travel_failed again "Yew banker ... sealed in; recovery exhausted"
- err.txt tail: character boxed in at (1124,362,5) with open=0 terrain=8 exits; all 3
  escape attempts (1074,370 / 1176,312 / 1176,322) failed with 0 path search time
  (instant fail, not a real pathfind attempt)

Session summary line (21:09:59): picks=14, wander=50%, TRAVEL_TO_REQUIRED_PLACE=43%
(all failed), MAKE_CLOTH never entered CRAFT.

## Criterion answers
1. MAKE_CLOTH "player market has not been asked" BLOCKED_NEED: Aelia x19, Amara x14 —
   still logs every tick for the full 5 min in both runs; never transitions to an
   announced WTB in this window.
2. WTB for yarn/cloth: not announced by either character in this run.
   - Aelia: TRADE_WITH_PLAYER goal ran and explicitly declined — Market.cpp:290 capital
     reserve gate (gold=0, "would eat into the reserve this life keeps for tools").
   - Amara: TRADE_WITH_PLAYER never got a trade attempt — blocked by session-time
     estimate (need=800s > left=272s) before the reserve check was ever reached; separately
     BUY_SUPPLIES failed REFUSE_NO_KNOWN_SUPPLIER for i_yarn_ball (no vendor route known).
3. Shear->spin->yarn->loom->bolt->scissors->cloth pipeline: NOT observed for either
   character — no wool/yarn/loom/spin/bolt/shear/scissors action lines beyond the
   recurring BLOCKED_NEED text itself. Craft chain never started.
4. CRAFT of i_sash/i_robe/i_leather_tunic (sewing-kit cursor/menu fix): NOT exercised —
   zero "craft"-tagged lines in either console log. MAKE_CLOTH never got past the yarn
   shortfall, so the CRAFT/menu/target-cursor code path in Identity.cpp/Runner.cpp was
   never reached this run. (Aelia does hold 1 spare i_robe carried over from an earlier
   run/session, per earn_gold refusal line — not evidence from this window.)
5. Navigation failures: Amara only — "sealed in; recovery exhausted" at (1124,362,5),
   repeated 3x (Britain innkeeper 21:04:54, Yew provisioner 21:05:19, Yew banker
   21:09:33), plus 4x goal_failed=TRAVEL_TO_REQUIRED_PLACE "three trips did not arrive".
   Aelia: only late pathing warnings near (1468,1611), no seal-in/goal failure.
6. Deaths/disconnects: none for either character.
