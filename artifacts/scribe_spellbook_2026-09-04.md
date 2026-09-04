# Scribe spellbook + craft ladder — 2026-09-04

Three 5-minute gate waves on Lyra/Thalia. Consoles are truncated on relaunch, so
line numbers below refer to the wave named beside them.

## What the shard actually does (verified, not assumed)

1. **The inscription top menu offers ORDINAL circle names, not "Spell Circle N".**
   `sm_legacy_inscription.scp:12-30` lists `ON=i_spell_circle_1..8`, and a
   skillmenu option is rendered as the itemdef's `NAME`, which is
   `NAME=third circle` / `NAME=fourth circle`
   (`runtime/scripts/items/i_magic_magery.scp:1893-1907`). "Spell Circle 3" is
   the *submenu title* (`sm_legacy_inscription.scp`, the line under each
   `[SKILLMENU sm_inscrip_N]`), never an option.
   Live dialog, wave 1 `g_Thalia.console.txt:788-791`:
   `[0x7C] dialog menu=690: "Spell Circles" (3 options) 1) first circle
   2) second circle 3) third circle`.
   Our route table asked for "Spell Circle 3"/"Spell Circle 4", so **every**
   scribe sitting ended at `REFUSE_MISSING_RECIPE` — poison included.

2. **Sphere hides a circle whose every leaf fails `TESTIF=<cancast s_x 00>`.**
   CANCAST answers for the spellbook *and* for mana.
   - Circles 1/2/3 cost 4/6/9 mana; circle 4 costs 11
     (`data/revolution_spells.tsv`, MANAUSE from `spells_magery.scp`).
   - Thalia INT = 10 → 10 mana (wave 2 `g_Thalia.console.txt:47`).
   - The menu offered exactly circles 1-3, and kept doing so **after** a Recall
     scroll went into her book (wave 1 lines 745-794: bought 0x1F4C, book 22 →
     23 rows, menu still 3 options).
   So the owner's "the menu offers only known spells" is half the rule. The
   missing half is mana, and it is the half that was actually blocking Recall.

3. **A container's contents only arrive after it is opened.** A carried
   spellbook reads as 0 rows at login. NeedSpells read that as an empty book:
   `FILL_SPELLBOOK 77.0` won the first goal of every session, opened the book,
   found 23 spells, fell to `50.6` and lost the turn 17 s later — wave 0
   `g_Lyra.console.txt:79-117`. There was no supersession bug; the score drop
   was correct and the *input* was wrong.

## Changes

- `include/uo/life.h` — `Observation::knownSpells` + `KnowsSpell`/
  `SpellbookRead`; `CraftIntent::wantSpell`/`wantSpellItem`/`lowManaSpell`/
  `lowManaCost`; `PersistentState::knownSpells`/`knownSpellsSeenMs`.
- `src/life/State.cpp` — persist/load `known_spells`.
- `src/life/runner/Core.cpp` — `Observe` reads the book's rows into
  `knownSpells` (amount field = spell number) and falls back to the remembered
  reading; `LearnFromObservation` writes a fresh reading back.
- `src/life/Identity.cpp` — `SpellTaughtByScroll` (defname `i_scroll_x` →
  `s_x`); ChooseCraft skips a scroll rung whose spell the book lacks, and one
  whose cast costs more mana than the pool holds, before the top-rung rule;
  inscription menu rows corrected to `^third circle` / `^fourth circle`.
- `src/life/Needs.cpp` — NeedSpells capped below CRAFT (65) while the bench has
  material, raised above it when the next rung is blocked *solely* by a
  buyable missing spell, blocked (score 0) for a circle-5+ spell or a mana wall
  once there is nothing left to shop for.
- `src/life/runner/Train.cpp`, `src/life/Runner.h`,
  `src/life/runner/RunnerInternal.h` — `ScrollGraphicForSpell`; `BuyScrollFrom`
  gains a `prefer` graphic (two-pass, preference not filter).
- `tests/m5_professions.cpp` — 11 new checks.

## Runtime evidence

| wave | fact |
|---|---|
| 1 | `g_Thalia:745` `spellbook: buying a spell this book lacks ('Recall Scroll', 0x1F4C, spell 32) at 46 gold ... (this is the scroll the craft ladder wants)` — the preference pass bought the right scroll |
| 1 | `g_Thalia:762,769` book 22 → 23 rows, but the "would not take" check fired before the re-read landed (pre-existing false negative) |
| 2 | `g_Thalia:229` `craft: chose '^third circle'` → `:258` `chose 'poison'` → 36 poison scrolls; `.makelast 73` bulk repeat |
| 2 | `g_Thalia:132` login needs line shows `NeedSpells(spells 0.46)`, not 0.70 — the remembered book works |
| 3 | 26 scrolls made, **0** `making i_scroll_recall`, **0** `REFUSE_MISSING_RECIPE` (was 3/session), gold 9940 → 11540, skills 131.0 → 131.1, no `goal_spinning` |
| 3 | `g_Thalia:84` `.makelast 31`; `:406` `BANK 81.1 superseded EARN_GOLD 0.0` |

## Gates

- `ctest`: pass 43/43 (648 checks in m5_professions, 0 failures).
- Smoke Thalia: PASS.
- Smoke Lyra: BLOCKED, unrelated cause (below).

## Open defects (not fixed here)

1. **Lyra loses every session to one travel goal.** Restored objective BANK →
   `seeded_bank -> (4471,1156)` from (4456,1160), 15 tiles, and the pathfinder
   returns `no path to (4471,1156) avoiding 0 block(s)` for ~440 s until
   wind-down. `goals=0/0 picks=0` in both waves
   (`g_Lyra.console.txt:57-60`, `.err.txt` throughout). Thalia starts on the
   same tile and escapes only because CRAFT out-scores BANK. Navigation, not
   behaviour: the Moonglow bank tile is unreachable from (4456,1160).
2. **`scrollBookRefused_` false negative.** DoFillSpellbook decides "the book
   would not take it" from `obs.spellsKnown` on the tick *before* the re-open
   completes, so a scroll that DID go in is recorded as refused and never
   offered again this process (wave 1 `g_Thalia:762-769`).
3. **Recall remains unreachable for these two scribes** until INT rises above
   10. There is no stat-farming goal, so this is a permanent BLOCKED_NEED. The
   scribe build targets INT 100; nothing raises it.
4. Circle 5+ scrolls have no purchasable source and no hunt goal can carry a
   "loot scrolls" intent, so that path is a BLOCKED_NEED by design for now.
