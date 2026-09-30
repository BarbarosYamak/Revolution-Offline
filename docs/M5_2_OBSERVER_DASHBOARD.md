# M5.2: Observer dashboard

Date: 2026-09-29. **STATUS: BUILT AND UNIT-TESTED, NOT YET RUN AGAINST SPHERE.**

This is one page that shows what every bot on the shard is doing right now.
It is read-only: it never talks to Sphere and never touches a bot.

## Where the data comes from

* **Each client** writes `<bot-data>/<identity>/status.json`:
  * every 10 seconds while live, and once more at logout, marked `offline`
    (`Runner::PublishStatus` / `PublishOffline`);
  * the content is what the character itself observed: goal and goal family,
    position, HP and mana, stats, gold now and at login, total skill, the
    session's kills, deaths and goals, party size, bandages, friend and foe
    counts, the persona rhythm and schedule, and the last 8 goal picks with
    their reasons;
  * the bot never reads the file back.
* **The population manager** (`fleet_ramp.py --live`) writes
  `population.json`: who it has on line and how many want to play.

## The page (`bot/uo-client/tools/observer.py`)

```text
python tools/observer.py --run run_gates/live           # http://127.0.0.1:8765, refreshes every 5 s
python tools/observer.py --snapshot shard.html          # one self-contained file to share
```

* Headline counts: on line, known, crashed or stale (a client that claims to
  be online but has not written for 60 seconds), in a party, session kills and
  deaths, gold carried and gold earned.
* A map with each live character as a dot: fighter, crafter, gatherer or
  dead. Town labels are approximate and only there for orientation.
* "What they are doing": the live goals. "Who is on line": the families.
* A sortable, filterable table of characters, with each one's recent goals and
  reasons.

## Verified

* `tests/test_observer.py`: 3 tests. They cover:
  * only live characters are counted;
  * a stale client is flagged as crashed;
  * broken files are skipped;
  * population.json is merged in;
  * the snapshot embeds its data safely.
* `life_world_harness`: a logout writes status.json marked offline, with the
  character's family and rhythm.
* The page was rendered in headless Chromium with 40 sample characters, at
  1300 px and 390 px wide: no script errors and no horizontal scroll.
* Full Windows build plus ctest under Wine: 54/55. The one failure is
  `m4_economy_invariant`, which needs the uncommitted
  `docs/tns_exports/economy_arbitrage_loops.tsv`.
