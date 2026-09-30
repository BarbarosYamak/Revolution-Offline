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

**Passing through:** a long route never passes through a guarded town either.
`RoutePlanner::GuardedCells()` collects every macro cell of a guarded region's
rectangles (built once). While the character is red, the world search treats
those cells as walls; the goal cell is exempt, since the trip itself was
already allowed. If the character is already standing on guarded ground, that
one plan runs without the walls, so it can walk out by the shortest way.

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

## 4. Mages mark their own runes

`src/life/runner/Runes.cpp`, `uo/recall_plan.h`:
* A character single-clicks each rune in its pack (0x09). The server's label
  is kept (`Client::ServerItemName`), and the stock name ("a recall rune")
  means blank.
* With Magery 60, 20 mana, one of each reagent, standing in its home region
  and safe, it casts **Mark** on a blank rune. It believes the result only when
  the label the server gives back afterwards has **changed** to a region name.
  A fizzle is retried after 2 minutes.
* A marked rune is remembered across sessions (a known place, "rune"). The
  travel layer can then choose **loose-rune Recall** for a trip home when the
  rune is still in the pack and landing there saves at least 150 tiles.
* Blank runes are never bought: the vendor policy blocks them ("marked, not
  bought"). This only acts on a rune the character already owns. Where
  Revolution players got blank runes is UNKNOWN.

## Verified

`life_world_harness`: 443 checks, 0 failures. `m25_world`: a red route goes
round a guarded town where an ordinary route walks straight on.
`recall_plan`: the blank-rune label and the Mark conditions.
* A red character refuses a trip to guarded ground in the real atlas; a blue
  one is not refused for that reason.
* With a real charged runebook gump (Charges: 05):
  * a Britain trip costs under a quarter of the walk;
  * the escape opens the book.

Full ctest under Wine: 62/63; the known missing TSV is the one failure.
