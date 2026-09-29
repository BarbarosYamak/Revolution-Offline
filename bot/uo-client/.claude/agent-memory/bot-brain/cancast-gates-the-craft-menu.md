---
name: cancast-gates-the-craft-menu
description: every inscription leaf is TESTIF=<cancast s_x 00>, so Sphere hides a whole circle the character cannot cast — for MANA as often as for a missing spell; a scroll in the book is not enough
metadata:
  type: project
---

Scribing is gated twice, and the second gate is the one that actually bit.

`sm_legacy_inscription.scp` puts `TESTIF=<cancast s_x 00>` on every leaf, and
Sphere hides a submenu whose entries all fail their TESTIF. CANCAST answers for
the spellbook AND for mana:

- circles 1/2/3 cost 4, 6 and 9 mana; circle 4 costs 11
  (`data/revolution_spells.tsv`, MANAUSE from `spells_magery.scp`);
- Thalia's INT is 10, so her pool is 10;
- the live menu offered exactly `first circle / second circle / third circle`
  — and kept doing so after a Recall scroll went into her book (book 22 -> 23
  rows, menu unchanged; run_gates/g_Thalia.console.txt, 2026-09-04).

**Why:** the brief was written as "the menu offers only known spells", which is
true and incomplete. Acting on the book half alone bought the scroll, put it in
the book, and still failed at the menu three times a session.

**How to apply:** a scroll rung is a candidate only when the book holds the
spell AND the mana pool can pay for the cast. Both live in `ChooseCraft`
(`CraftIntent::wantSpell` = go and buy it; `lowManaSpell` = a stat wall, never a
shopping errand — INT is the only fix and no stat-farming goal exists). Measure
the pool as `max(obs.intel, obs.mana)`: INT is the Sphere default but the
observed value is the honest witness. Generalise before assuming this is
scribe-only — any TESTIF-gated menu behaves this way. See
[[a-menu-title-is-not-a-menu-option]].
