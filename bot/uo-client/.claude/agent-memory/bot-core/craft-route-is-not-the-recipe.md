---
name: craft-route-is-not-the-recipe
description: A craft output needs BOTH a recipe (what it is made of) and a menu route (how CRAFT reaches it); a missing route spins forever, and some outputs have no menu at all
metadata:
  type: project
---

CRAFT needs two separate facts about an output, and only one of them is in the
recipe graph.

- **Recipe** — `src/progression/Production.cpp` / `prod::FindRecipe`. What the
  output is made of, its skill gate, its tool, its station. This is what
  `ChooseCraft` (`src/life/Identity.cpp`) reads to decide what to make.
- **Route** — `kCraftMenus` in `src/life/Identity.cpp` (moved there from
  Runner.cpp on 2026-09-02 so tests can reach it; declared in `uo/life.h`).
  Up to three case-insensitive substring steps through the shard's legacy skill
  menu. `Runner::DoCraft` walks it.

An output with a recipe and no route refuses `REFUSE_MISSING_RECIPE` forever,
because ChooseCraft does not know routes exist and re-offers the same item next
tick. Wave 2026-09-02: i_board 45x, i_gears 30x, i_ingot_iron 10x, 17
`goal_spinning=CRAFT`, zero craft output fleet-wide.

**Third category: outputs with no menu at all.** `Provenance::WorldProcessed`
literally means "a station transforms it; no craft menu, no skill"
(`include/uo/production.h`). i_ingot_iron (Smelt), i_fish_cut_raw (DoFish's
blade carve), i_cloth_bolt / i_yarn_ball / i_thread / i_cloth / i_hides_cut
(MakeCloth). These belong to their own goal — DoCraft hands them off via
`ProducingGoalFor`. Never add a route row for one.

**Why:** the two tables were written by different passes and nothing checked
that a profession's `produces` list could actually be reached. `ChooseCraft`
returning an item is not evidence CRAFT can make it.

**How to apply:** when a craft spins, read the exact refusal text before the
item name — "no menu path known for X" is a missing route row, "this menu
offers none of ..." is a wrong route row (its own branch dumps what the menu
actually offered), and "X has no recipe" is a graph gap. Build a route row only
from `runtime/scripts/crafting/interface/legacy skillmenu/sm_legacy_*.scp`
(every `scp.NewCrafting_*` in `crafting/crafting_settings.scp` is 0, so the
legacy menus are what a bot is shown). An entry with no `NAME=` in its itemdef
renders as its tiledata name — check tiledata, do not guess.

A blanket "skip WorldProcessed in ChooseCraft" looks tempting and is wrong: it
pushes the tailor off i_cloth_bolt onto i_sash, which has no route either.

Related: [[a-deferral-needs-a-bound]], [[action-timeout-means-unrecognised-answer]],
[[goals-that-spin]].
