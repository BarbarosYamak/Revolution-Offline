# M5.5: Bots travel by Recall

Date: 2026-09-29. **STATUS: BUILT AND TESTED OFFLINE, NOT YET RUN AGAINST SPHERE.**

The client could already recall from a runebook (M3.9), but the life runner
never used it, for three reasons:
1. **It never opened its book**, so the book's pages were unknown and a
   runebook Recall could never be chosen.
2. **Pages matched only by name against the trip's label.** Runner trips are
   labelled "supplier", "forge" or "bank", never "Britain", so nothing
   matched.
3. **Reagents were assumed**, with `haveReagents` hardcoded to true.

## Now

* **Read once per session.** When the bot is safe and idle it opens its
  runebook, notes the pages and closes the gump again (`Runner::TickRunebook`,
  `Client::ActionReadRunebook`). A player knows what is in their own book
  because they looked.
* **Pages are matched by map point.** Each page's gump text carries the point
  it was marked at. The travel layer picks the page that lands nearest the
  trip's goal (`uo/recall_plan.h`), but only when:
  * the landing is within 120 tiles of the goal, and
  * it saves at least 150 tiles of walking.

  Short trips still walk. Name matching is still tried first.
* **Reagents are counted.** Black pearl, blood moss and mandrake root are
  counted from the pack, at the era's per-cast cost: 3 each before
  14.05.2009, then 1 (`uo/era.h`).
* **Charges follow the era.** A charged book (no Magery needed) is relied on
  only from 13.05.2009.
* The server still decides everything: skill check, mana, reagents, fizzle.
  If a recall fails, the journey plans from wherever the bot stands and walks.

## Not done

* **Loose runes and Mark:** `haveMarkedRune` is still never set. Revolution
  vendor policy says runes are *marked, not bought*, and a bot marking its
  own runes (Magery 60, frequent fizzles at 60) needs a live check first.
* **Need estimates** (`TravelTilesWithGates`) still count moongates only, so
  the planner may overrate how far a recall-capable bot is from home. This is
  conservative, never optimistic.
* How a bot obtains a runebook: Inscription 45 with 8 blank scrolls, a rune,
  a Recall scroll and a Gate scroll. No bot crafts one yet.

## Verified

* `recall_plan`: 14 checks. They cover:
  * point parsing;
  * nearest landing;
  * short trips walk;
  * no rune near the goal means walk;
  * reagent counts before and after 14.05.2009;
  * read once, never in danger.
* `life_world_harness`, using a real 0xB0 runebook gump:
  * both pages are read;
  * the read-only open closes with button 0;
  * a Britain-shop trip picks the Britain page by its point;
  * Minoc picks Minoc;
  * a short trip walks;
  * reagents are counted from the pack.
* Full ctest under Wine: 57/58; the known missing TSV is the one failure.
