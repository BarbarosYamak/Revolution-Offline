---
name: unknown-beats-proven-poor
description: When looking is free, an unexamined item must outrank every examined-but-unfinished one, or a bot keeps the poor thing it has measured
metadata:
  type: project
---

Ranking duplicate kit by a measured quantity quietly prefers whatever has
already been measured. Aurelius fetched her 23-spell book out of the bank
and went on casting from the empty one she was carrying, because that one
had two rows the client had read and two beats "unopened, score 0.5"
(g_Aurelius.console.txt 2026-09-06, 17:34:07-17:36:59).

**Why:** container contents arrive only after an open, so an unopened item
scores as its floor and never gets the chance to prove itself.

**How to apply:** when the examination is one free action, score the
unexamined candidate just below "already good enough"
(`kSpellbookComfortableRuntime - 0.5`), so it is looked at first and a
finished item still wins. Score a candidate that WAS asked and still says
nothing at the bottom, so an unopenable one cannot shadow a real one
forever — track the "asked" serial, not a bare bool
(`spellbookOpenedSerial_`), or a second item inherits the first's answer.
