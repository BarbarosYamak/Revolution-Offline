# M5.11: Red characters avoid the guards; victims recall out

Date: 2026-09-30. **STATUS: BUILT AND TESTED OFFLINE, NOT YET RUN AGAINST SPHERE.**

Three gaps left open by M5.5 and M5.7.

## 1. A murderer plans no trip into guarded ground

With `GuardsOnMurderers=1` and `GuardsInstantKill=1`, a red character that
walks into a guarded town dies on the spot. Now:
* the client reads **its own notoriety** from the server's update of its own
  mobile (`Client::PlayerNotoriety`);
* every tick the runner turns on **murderer routing** while that notoriety is
  6 (red);
* while routing is on, `TravelBegin`, the one door every errand goes through,
  refuses any destination whose region is guarded ("a murderer does not walk
  into the guards").

The goal that asked then finds another way: Buccaneer's Den (the PK's home) is
unguarded, and so are the wilderness grounds.

**Not yet:** a long route that merely *passes through* a guarded town is not
avoided. That needs the route planner to treat guarded cells as walls for a
red character.

## 2. Losing to a player: recall out

In SURVIVE, when a player is attacking and the per-fight judgement
(`pvp::ShouldBreakOff`: nerve, who is winning, the odds, bandages) says leave,
the character tries `Client::ActionEscapeByRecall()`. That recalls to the
marked runebook page **farthest** from here, at least 50 tiles away, if it can
pay for the cast. Paying means either:
* a charge (from 13.05.2009), or
* Magery 40, 11 mana and one of each reagent (at the era's cost).

The server still runs the skill check, the mana and the fizzle; blows can
interrupt the cast. If it fails, the ordinary retreat below it still runs.
There is one try every 15 seconds.

## 3. Distances count Recall

`Observation::tilesFromHome` came from `TravelTilesWithGates`, which counts
moongates only. When the character can recall to a page near home (and pay
for it), the distance is now the walk from the landing point plus 20 tiles for
the cast (`Client::RecallTilesTo`). A mage with a home rune is therefore no
longer treated as stranded.

## Verified

`life_world_harness`: 440 checks, 0 failures.
* A red character refuses a trip to guarded ground in the real atlas; a blue
  one is not refused for that reason.
* With a real charged runebook gump (Charges: 05):
  * a Britain trip costs under a quarter of the walk;
  * the escape opens the book.

Full ctest under Wine: 62/63; the known missing TSV is the one failure.
