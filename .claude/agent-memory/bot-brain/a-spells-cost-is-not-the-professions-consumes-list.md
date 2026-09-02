---
name: a-spells-cost-is-not-the-professions-consumes-list
description: obs.pack only counts a profession's produces/consumes, so a mage's spider silk and sulfurous ash were invisible; any "do I have X" check for spells must widen the pack counter first
metadata:
  type: project
---

`Runner::Observe` builds `obs.pack` from `profession->produces` +
`consumes` (plus salvage prefixes). The mage entry names four reagents; Night
Sight, the first spell practice reaches for, costs spider silk and sulfurous
ash — neither on that list. So a pouch of 250 ash read as **zero**.

**Why:** the profession `consumes` field means "must obtain from someone else
for my CRAFTING", not "everything this life spends". A spell's cost comes from
`spells_magery.scp` `RESOURCES=`, a different table entirely. Wired naively, a
reagent check reports permanently empty and the restock errand buys forever —
a self-refilling shopping loop that looks like working behaviour in the log.
Fixed 2026-09-02 by adding an `i_reag_` salvage prefix alongside `i_ore_` /
`i_ingot_` in the pack counter (`include/uo/spellcast.h` holds the spell
costs).

**How to apply:** before writing any "does the pack hold X" gate, check that X
can even appear in `obs.pack` for that profession. `market::QtyOf` returning 0
means "not counted" just as often as it means "not held". Same trap the
coloured-ore salvage loop already exists for. Related:
[[goals-addressed-to-nobody]], [[a-grep-miss-is-not-absence]].
