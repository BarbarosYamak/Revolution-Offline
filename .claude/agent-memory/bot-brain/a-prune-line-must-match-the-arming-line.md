---
name: a-prune-line-must-match-the-arming-line
description: A shopping list armed at a low-water mark but pruned at qty > 0 only ever restocks from exactly zero -- 13 nightshade against a mark of 20 was struck off and never bought
metadata:
  type: project
---

`DoBuySupplies` took a reagent off `reagentWants_` as soon as
`market::QtyOf(obs.pack, item) > 0`, while the band that PUT it there
(`spell::ReagentBandFor` / `ReagentRestockFloor`, include/uo/spellcast.h) arms
at a third of the band. One leaf answered the prune test, so a pouch holding 13
nightshade against a mark of 20 was struck off the list and the errand only ever
fired from exactly zero. Aurelius walked into a graveyard on it, 2026-09-07.

**Why:** two thresholds for one list. Whichever is looser wins, and a prune line
is the loose one by construction -- it runs on every tick of the errand, while
the arming line runs once. Fixed 717fee1: both sides call
`spell::ReagentRestockFloor`, and the shortfall rule itself moved into
spellcast.h so the need model, the errand and the hunt gate read one function.

**How to apply:** whenever a handler prunes a list it did not build, name the
predicate ONCE and have both ends call it. Three further shapes fell out of the
same fix and are worth carrying:

- **A reactive raiser is not a standing one.** The reagent need only existed
  once a cast had already been refused (`obs.practiceReagentsShort`, written by
  the runner). A standing stock -- an owner rule of 60-70 of every reagent --
  needs a need of its own that reads the pack, or the character discovers the
  hole at the moment it needs the rung.
- **One errand, one need row.** `FindNeed` returns the FIRST row of a kind
  (Goals.cpp), so a second NeedSupplies row for the same shop only decides which
  sentence the telemetry prints. Gate the new raiser on the old one being
  silent. Related: [[a-blocked-need-shadows-a-ready-one]].
- **A handler log is once per PICK, not once per tick.** The band line printed
  359 times in twenty seconds of walking on its first live run; keying it to
  `supplyItem_ != wants.front()` made it one line.

`life::BuildCastsSpells` (uo/life.h, defined in Needs.cpp) is the caster test
the band uses: Magery planned at Primary or Secondary. A dexxer's Utility Recall
and a crafter's creation roll are not casters, and must not buy a 500-gold pouch
they will never burn. Related: [[thresholds-are-rates-not-numbers]],
[[a-spells-cost-is-not-the-professions-consumes-list]].
