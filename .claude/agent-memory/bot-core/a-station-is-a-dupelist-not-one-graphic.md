---
name: a-station-is-a-dupelist-not-one-graphic
description: World-item lookups by a single graphic miss craft stations; Sphere places a DUPELIST facing, and the stations live in spherestatics.scp not sphereworld.scp
metadata:
  type: project
---

`FindWorldItemByGraphic(<base id>)` cannot see most craft stations. Sphere keeps
ONE itemdef and lists its other facings in `DUPELIST`; the DISPID that reaches
the client is whichever facing the decorator placed, never the base id. Look for
the whole dupe list.

Measured 2026-09-02: `i_spinning_wheel` is 0x1015 with
`DUPELIST=01016,01017,01019,0101a,0101b,0101c,0101d,0101e,010a4,010a5,010a6`,
`i_loom_upright` is 0x105F with `DUPELIST=01060..01066`
(runtime/scripts/items/i_profession_tailor_tanner.scp). Britain's tailor shop
holds wheels at **0x101C** and looms at **0x1061/0x1062**. Aelia stood three
tiles from a spinning wheel and logged "no spinning wheel in sight" for a whole
session.

Two traps that go with it:

* **The stations are in `runtime/save/spherestatics.scp`, not
  `sphereworld.scp`.** Grepping the world save for a wheel or a loom returns
  zero and looks like missing content. Same lesson as
  world-save-keys-by-defname: check the other save file before declaring
  absence.
* **Finding a station is not reaching it.** `FindWorldItemByGraphic(g, 10)`
  scans ten tiles; CanTouch refuses past two (`sphere-reach-is-two-tiles`). The
  Britain wheel is 2 tiles from the arrival tile and works; the loom is 6 and
  the server answers the target with nothing at all — the action just times out.
  Approach before using.

**Why:** three runtime sessions were spent on "the station is not there" when
it was three tiles away, and a fourth on a loom that answered silence.
**How to apply:** any goal that uses a world fixture (forge, anvil, wheel, loom,
oven) needs a graphic LIST and a walk-to-2-tiles step, not one constant and a
scan radius. See [[action-timeout-means-unrecognised-answer]] — here the server
did not even reply, which is the CanTouch signature.
