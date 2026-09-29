# M5.3: Turkish small talk

Date: 2026-09-29. **STATUS: BUILT AND TESTED OFFLINE, NOT YET HEARD ON SPHERE.**

RevolutionUO was a Turkish shard. Before this change the bots spoke only the
English handshake phrases that start parties, spars and trades, which read
like a protocol because that is what they are. Now they also make small
talk. It is flavour only, deterministic, and involves no AI.

## What they say (`bot/uo-client/include/uo/chatter.h`)

| Occasion | Example lines |
|---|---|
| a passer-by (once, gated by sociability) | sa · slm · selam · merhaba |
| someone said "sa" | **as** · as hosgeldin · aleykumselam |
| someone greeted | selam · selam hosgeldin; a friend (trust ≥ 2) gets: naber kanka · hosgeldin abi |
| a friend nearby | kolay gelsin · bereketli olsun · naber, iyi misin |
| after a kill, near others or in a party | gg · tamamdir · kolaydi |
| back on its feet after dying | yine oldum ya · cok kalabaliktilar · lag yedim galiba |
| idle in a guarded town with ≥ 2 players around, at most every 5 min | banka onu yine dolu · server bu aksam kalabalik · reagent fiyatlari artmis |
| a known foe, only inside guards and at 4 tiles or more | yine mi sen · bakariz · uzak dur |
| logging out with players nearby | iyi oyunlar · gorusuruz · hadi kactim ben |

## The rules it follows

* **The handshakes are untouched.** Invitations, consent, "Ready to spar.",
  "Let's stop and regroup." and the WTS/WTB lines are exactly what
  `social.h` and `market.h` parse. `tests/chatter.cpp` checks that no
  small-talk line parses as any of them.
* **No echo loops.** Each character remembers when it last exchanged words
  with each name (`chatWith_`). It answers a person at most once per 10
  minutes, and "as" is not itself heard as a greeting.
* **Personality.** How often a character speaks unprompted comes from its
  persona's sociability (M5.1), so some characters talk and others rarely do.
* **Safe only.** No small talk in combat, while hurt or while sparring. The
  existing 15-second speech gap still applies.
* **ASCII.** The client speaks with packet 0x03, so the lines are written
  without Turkish letters ("gecmis olsun"), which is how most players typed
  on an English keyboard layout anyway.

**Evidence:** Revolution-specific chat logs are UNKNOWN. The phrases are
everyday Turkish online-game chat of the period (DERIVED). Owner corrections
are welcome; the phrasebook is one table.

## Verified

* `chatter`: 17 checks. They cover:
  * ASCII only;
  * no collision with a handshake or trade line;
  * determinism, and variety over time;
  * sociability scaling;
  * hearing tolerates case and punctuation;
  * "sa" → "as";
  * a friend gets a warmer greeting;
  * no echo.
* `life_world_harness`: 390 checks, 0 failures. New checks:
  * a real 0x1C "Sa!" from a nearby player is answered "as" on the wire;
  * a second greeting within 10 minutes gets silence.
* Full Windows build plus ctest under Wine: 55/56 pass. The failure is the
  known missing `economy_arbitrage_loops.tsv`.
