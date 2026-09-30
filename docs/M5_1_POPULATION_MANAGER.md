# M5.1: Population manager: personas and play hours

Date: 2026-09-29. **STATUS: BUILT AND UNIT-TESTED, NOT YET RUN AGAINST SPHERE.**

Before this change a fleet was admitted in fixed stages (20 → 50 → 100 → 122).
Every character logged in within the same minute and logged out 45 minutes
later. That is a load test, not a shard. Now every character has its own
temperament and weekly play hours, and the fleet script logs each one in and
out by those hours.

## 1. The persona (`bot/uo-client/include/uo/persona.h`)

Each persona is chosen once from the identity id, the same way the home city
is chosen. It is saved in `state.json` under `"persona"`, and a hand edit
there is honoured.

| Field | Range | Used by |
|---|---|---|
| `rhythm` | evening 40% · late_night 20% · afternoon 15% · weekender 15% · morning 10% | the play windows |
| `risk_shift` | −15 … +15 hundredths | `life::Nerve()`: the profession's riskTolerance plus this shift. Used by the flee threshold (Needs + Survive), recovery, and prey choice (Train) |
| `sociability` | 15 … 95 | how urgently the character looks for company (0.45 … 0.85; the old fixed value was 0.65) |
| `windows` | 1–2 weekly windows of 1.5–8 h | when the character logs in |

The login log prints the persona, for example:
`persona: rhythm=evening nerve=0.52 sociability=76 schedule="TuWeThFr@19:48+148 SaSu@16:21+262"`.

**Evidence: UNKNOWN.** No Revolution population log or forum census has been
found. The rhythms are DERIVED from what a Turkish shard of students and
working adults would look like, on the shard's local clock. Every number is a
tunable.

For 122 characters the schedules give:
* weekdays: about 0 at dawn, 10–18 at midday and after school, about 58 at
  22:30, and about 77 on Friday night;
* weekends: 25–50 from noon to midnight.

## 2. The manager (`bot/uo-client/tools/fleet_ramp.py`)

```text
python tools/fleet_ramp.py --plan                                # hourly population, launches nothing
python tools/fleet_ramp.py --live --directory run_gates/live --max-online 40
```

Every 60 seconds the manager:
1. Reaps clients that have finished.
2. Launches every character whose window is open and has at least 15 minutes
   left, one every 3 seconds, while memory stays at or above 2 GiB. Each is
   launched with `--life-minutes` set to the rest of its window, capped by
   `--max-session`.
3. Never kills a client. Each one ends its own session and logs out properly.
4. Keeps a character that exited within 5 minutes, or got LOGIN DENIED, out
   for an hour, so it cannot crash-loop. A normal session end rests the
   character for 20 minutes.
5. When more characters want to play than `--max-online` allows, orders them
   by a hash of name and date, so a different subset plays each day.
6. Writes `population.json` (who is on line, a count by family, and how many
   want to play) and `population_log.tsv` (one row per session).

The staged `--admit` gates are unchanged.

## 3. One schedule, two implementations

The manager must know a character's hours without starting it, so the
generator (FNV-1a plus xorshift32) is mirrored in Python.
`bot/uo-client/tests/data/persona_vectors.tsv` holds 63 pinned personas.
`tests/persona.cpp` checks the C++ generator against them, and
`tests/test_fleet_ramp.py` checks the Python one. If either side changes
without the other, a test fails. To change the generator deliberately, change
both sides, then run `persona <tsv> --write`.

## 4. Verified

* `persona`: 24 checks. They cover:
  * determinism;
  * legal ranges;
  * 5–40 hours a week each;
  * every rhythm occurs;
  * a believable population curve;
  * the week wrap on Sunday night;
  * nerve clamping;
  * the sociability scale;
  * the shared vectors.
* `test_fleet_ramp.py`: 10 tests. They cover:
  * the vectors;
  * identity ids;
  * the clock;
  * a saved persona winning over the derived one;
  * only in-window characters launching;
  * the cap, cooldowns and the end-of-window cut-off;
  * a different subset each day;
  * one `live()` tick with a mocked launcher.
* Full Windows build plus ctest under Wine: 54/55 pass. `m4_economy_invariant`
  still needs `docs/tns_exports/economy_arbitrage_loops.tsv`, which was never
  committed.
* **Not yet run against Sphere.** Live proof: run `--live` for one evening,
  then compare `population_log.tsv` with `--plan`.
