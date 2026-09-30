# M5.4: Era timeline (2008–2016)

Date: 2026-09-29. **STATUS: BUILT AND UNIT-TESTED.**

The shard changed over the years. `bot/uo-client/include/uo/era.h` holds
every dated change the project has evidence for, taken from Revolution's own
update archive (`/guncellemeler`), which is quoted in the docs. Each row cites
the docs line that quotes it.

| Date | Change |
|---|---|
| 15.04.2008 | anti-macro module present; campfire cooking |
| 07.11.2008 | Reagent Crystal |
| 13.12.2008 | ore weight 3 → 1 |
| 19.12.2008 | golems |
| 19.02.2009 | champion in Despise |
| 12.04.2009 | cooking batch 80 at GM |
| 12.05.2009 | runebook 8 named pages |
| 13.05.2009 | runebook charges (no Magery needed), runebook copying |
| 14.05.2009 | Recall reagents 3 → 1 |
| 18.05.2009 | S.O.S bottles from mining and lumberjacking |
| 04.03.2010 | Craftsmen city |
| 03.11.2010 | pack animals |
| 20.11.2010 | Store Crystal |
| 22.02.2011 | anti-macro code screen |
| 24.03.2011 | Gate Travel reagents 6 → 1 |
| 01.06.2011 | cooking batch 100 at GM |
| 07.01.2012 | guild runebooks |
| 22.03.2012 | 15 fixed chests, guardians removed |
| 21.01.2016 | anti-macro disconnect (Revolution16) |

**UNKNOWN:**
* anything in 2013–2015 (no archive entries found);
* when the shard opened;
* whether it wiped;
* how busy each year was.

The table stays empty where there is no evidence, and a test enforces that.

## What the date changes

Sphere decides the rules, so the date can't change what the server does. It
changes **what a character knows to do**:
* a bot living before 13.05.2009 does not rely on runebook charges;
* the Recall reagent count and the campfire batch size a bot plans with follow
  the date;
* later features (treasure, travel, guilds) check their dated rows here.

The server's scripts are the 2009–2010 profile. A date outside that window
still runs, but the bot logs that it may expect things the server lacks.

## Running

```text
uo_client ... --era-date 2009-05-01
python tools/fleet_ramp.py --live ... --era-start 2008-06-01 --era-days-per-day 30
```

* The fleet's era clock is either fixed (the default is 2010-06-01) or
  advances; at 30 era days per real day it walks 2008 → 2016 in about three
  months.
* Each client gets its date at launch.
* An owner file, `data/era_population.tsv` (`year<TAB>scale`), can shrink or
  grow the online cap per year. Without it every year is 1.0, because the real
  numbers are UNKNOWN.
* The date is shown in each bot's `status.json` and on the observer page.

## Verified

* `era`: 14 checks. They cover:
  * the table is sorted, sourced and complete;
  * 2013–2015 stays empty;
  * parsing;
  * the profile window;
  * exact switch-over days;
  * the cooking batches;
  * the 2016-only rules.
* `test_fleet_ramp.py`: the era clock and the owner population file.
* Full ctest under Wine: 56/57; the known missing TSV is the one failure.
