# FILL_SPELLBOOK yields when nobody sells a scroll (2026-09-02)

## The defect, as measured

`run_gates/g_Aurelius.console.txt` (13:23-13:29 run, before this fix):
FILL_SPELLBOOK held the whole five-minute session — 16 mentions, three shop
trips, ~4 minutes of walking, no scroll, no cast. `session_goals` for that run
was 100% FILL_SPELLBOOK.

Two stand-downs existed and neither could fire inside a session:

* `kMaxSpellbookTrips = 3` — a trip counter cannot see that one trip costs
  ~60 s of travel, so three trips is a whole session while still "under budget".
* `kNoSpellbookCooldownMs = 240000` — four minutes, short enough to re-arm
  inside the same session that had just proved the street empty.

Supply is structurally thin, which is why a retry rate tuned for a restocking
shelf is wrong here: the NPC mage sells four RANDOM circle 1-4 scrolls
(`templates/tm_vend.scp:721-724`), the scribe sells named ones it may not have,
circles 7-8 are loot only.

## The change

1. `life::ScrollShoppingRestMs(int standDowns)` — new pure function in
   `src/life/Goals.cpp`, declared in `include/uo/life.h`. 15 min base, doubling
   per consecutive empty errand, capped at 60 min. Compare BUY_SUPPLIES's 119 s.
2. `Runner::StandDownFromScrollShopping` (`src/life/Runner.cpp`) — one exit for
   the errand: bump the consecutive count, cool FILL_SPELLBOOK for the
   escalating rest, clear the shop tour. FILL_SPELLBOOK only; the gear and
   scissors errands that borrow `BuyScrollFrom` keep their 240 s rest.
3. A TIME budget on the shopping half of `DoFillSpellbook`
   (`kScrollShopBudgetMs = 90000`, stale-gap guard `kScrollShopGapStaleMs =
   60000`). Reading the book and putting a looted scroll in it are never
   blocked; only the walking-to-shops half is bounded.
4. Any spell that actually enters the book clears `scrollStandDowns_` and
   restarts the clock, so a stocked scribe is never damped.
5. Purse fix found in the first smoke run: `BuyScrollFrom` now also skips a
   graphic already in `scrollBookRefused_`. `BookHasGraphic` reads container
   rows, and a spellbook row's graphic is not the scroll's, so the shelf check
   could say "the book lacks this" about a spell the book silently refuses.
   Selene bought the same Cunning Scroll four times for 84 gold in 90 s
   (`g_Selene.console.txt:1274-1753`, 14:30 run).

Not changed: `kGoals` weight for FillSpellbook (still 110), `NeedSpells`
urgency, any spell list. A cooling goal is already reported-but-infeasible in
`Planner::Score` (`Goals.cpp`, the `Cooling()` branch), so it scores below what
it scored a moment earlier and cannot be picked.

## Gates

* `python tools/rev.py build` — ok.
* `python tools/rev.py test` — 43/43.
* `m4_life` 581 checks / 0 failures (was 566), new
  `TestScrollShoppingStandsDownWhenNobodySells`
  (`tests/m4_life.cpp`): rest arithmetic (>= 15 min, > 4 min, escalating,
  capped, defensive at 0), FILL_SPELLBOOK reported-but-infeasible with a
  "cooldown" reason 60 ms after standing down, PRACTICE_SKILL still feasible,
  NeedSpells still raised, next Select is not FillSpellbook, still cooling at
  +4 min and free again at +16 min.

## Live smoke — 2 runs, 5 min, Aurelius + Selene

Run 2 (14:36 build, after the duplicate-buy fix):

| | Aurelius | Selene |
|---|---|---|
| stand-down | `g_Aurelius.console.txt:327` — "90s of shopping and not one spell added to the book ... resting 900s" | `g_Selene.console.txt:1060` — "102s ... resting 900s" |
| next goal | `:328` `goal=PRACTICE_SKILL reason="previous goal abandoned: nobody selling scrolls"` | `:1061` same |
| practice ran | no cast — `:334` "out of reagents for spell 29" | yes — `:1068,1112,1375,1454,1534` five Meditation uses, `:1611` bounded stand-down |
| session_goals | families=4 picks=4 top=25% varied=1 | families=4 picks=8 top=25% varied=1 |

Before: Aurelius families=3 top=73% varied=0 in run 1, and 100%
FILL_SPELLBOOK in the 13:24 run. Selene's four purchases in run 2 were four
DIFFERENT scrolls (`:452,626,805,980`), not one bought four times.

## Limitation

Aurelius's PRACTICE_SKILL got the turn and could not cast: he has no reagents
and `BUY_SUPPLIES` fails with `REFUSE_VENDOR_NOT_OBSERVED this 'mage' does not
stock i_reag_garlic` (`g_Aurelius.console.txt`, both runs). That is the
pre-existing reagent-supply defect recorded in
`artifacts/reagent_fix_2026-09-02.md`, not this change. A Magery cast is
therefore still unproven live; a self-use practice action is proven (Selene).
