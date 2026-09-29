# Archetype certification

`tools/grade_life.py` is a fleet-health score. It checks that a bot made
progress and exited safely, but a generic score cannot prove that every
profession completed its own work loop.

`tools/grade_archetype.py` is the reliability gate. A character passes only
when its console log proves all of these facts in a single completed session:

1. it did not die or spin;
2. it received an acknowledged logout; and
3. it completed the primary work stages in the table below.

| Profession | Required primary evidence |
|---|---|
| miner_smith, mage_blacksmith | mine ore; smelt ingots or craft a smith item |
| lumberjack_swordsman | gather logs; craft or sell a wood item |
| full_crafter | mine ore; craft or sell an item |
| fisher | catch a fish; cook or sell fish |
| mage, warlock | confirm a kill; cast a maintained defensive buff |
| fencer, macer, pk | confirm a kill |
| archer | craft an arrow, shaft, or bow; confirm a kill |
| scribe | craft a scroll |
| alchemist | craft a potion |
| tailor | weave yarn into cloth; craft a tailored item |
| merchant_tinker | craft a tinker item |
| tamer | receive a successful tame result |
| treasure_hunter | **blocked** until map, dig, and chest handling are implemented |

## Gate procedure

1. Start two freshly reset bot characters of each non-blocked profession in a
   controlled cohort. Do not mix this with a 122-character load test.
2. Let every character finish its session and snapshot its state at the first
   acknowledged logout.
3. Grade each console using `grade_archetype.py`. A profession is certified
   only when both characters pass its contract. A failed or blocked row keeps
   the reliability gate open.
4. Fix the failing contract, rerun only that pair from a fresh reset, then
   repeat until the profession has two passes.
5. After every non-blocked profession is certified, run the mixed 122-bot
   fleet and use both graders: `grade_life.py` for fleet health and
   `grade_archetype.py` for per-profession regressions.

The grader consumes runner log lines, rather than inferred movement or packet
activity. When an implementation changes a loop, add a precise evidence line
to the runner first, then change the contract and this table together.
