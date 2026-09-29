# CRAFT REFUSE_MISSING_RECIPE — root cause and fix (2026-09-02)

## 1. The brief's item list was partly wrong

The wave verdict (`artifacts/wave_2026-09-02_verdict.md` section g) attributes
`i_fish_cut_raw` x110 to Dorvar. That is not in the run logs. Measured over the
30 `run_gates/g_*.console.txt` files of that wave:

    45  REFUSE_MISSING_RECIPE" no menu path known for i_board    (Cyras 20, Vorar 15, Halain 10)
    30  REFUSE_MISSING_RECIPE" no menu path known for i_gears    (Serena 30)
    10  REFUSE_MISSING_RECIPE" no menu path known for i_ingot_iron (Draver 10)
     0  any other REFUSE_MISSING_RECIPE text
     0  REFUSE_MISSING_RECIPE anywhere in g_Dorvar.console.txt

Dorvar's five `i_fish_cut_raw` lines are all SELLING it ("selling 24 in 4
lot(s)", "for 48 gold (2 each) to a 'fisher'") — one of the wave's positives.
The fisher/cook path was never the defect. Corrected target set:
**i_board, i_gears, i_ingot_iron.**

## 2. Why the lookup missed

One emitting branch: `Runner::DoCraft`, `CraftMenuFor(craftItem_) == nullptr`.
`kCraftMenus` is a static route table of 12 rows; none of the three had one.
The bot recipe graph (`src/progression/Production.cpp`) DID have all three —
the graph says what an output is made of, the route table says how CRAFT
reaches it, and only the graph was consulted by `ChooseCraft`. So the planner
handed CRAFT the same unreachable output every tick: 17 `goal_spinning=CRAFT`
flags, 0 `craft_success` fleet-wide.

Two different underlying causes:

**a) i_board, i_gears — genuinely craftable, route row missing.**
Runtime is on the LEGACY skill menus (`crafting/crafting_settings.scp:26-33`,
every `scp.NewCrafting_*` is 0), so the menus a bot is shown are the
`sm_legacy_*` ones:

    crafting/interface/legacy skillmenu/sm_legacy_carpentry.scp:15-16
        ON=i_board boards
        MAKEITEM=i_board
      -> flat, top level of sm_carpentry. Every other top-level option opens a
         submenu; "boards" is the only leaf. No other top-level option contains
         that substring ("bulletin board" is singular and lives in sm_wood_misc).

    crafting/interface/legacy skillmenu/sm_legacy_tinkering.scp:18
        ON=i_clock_parts Parts   -> SKILLMENU=sm_parts
    crafting/interface/legacy skillmenu/sm_legacy_tinkering.scp:201-202
        ON=i_gears <name> (<resmake>)
        MAKEITEM=i_gears
      -> two levels. i_gears carries no NAME= (items/i_profession.scp:1055-1064),
         so <name> resolves off tiledata for 0x1053 = "gears", same pattern the
         existing i_fish_cut_cooked row relies on. "gears" is unique in sm_parts;
         "Parts" is unique at the top level.

**b) i_ingot_iron — not a menu craft at all.**
`Provenance::WorldProcessed` = "a station transforms it; no craft menu, no
skill" (`include/uo/production.h:53`). Ore becomes ingots by double-clicking it
beside a t_forge — that is `GoalKind::Smelt`, and the enum comment already says
so. The request was addressed to the wrong goal, not missing a recipe.

Checked and deliberately NOT changed: `i_fish_cut_raw` (carved by DoFish,
Runner.cpp `fish: cutting a whole fish`) and `i_cloth_bolt` (MakeCloth) are
also WorldProcessed but were never picked by CRAFT in this wave; a blanket
`ChooseCraft` skip of WorldProcessed would have pushed the tailor from
i_cloth_bolt onto i_sash, which has no route either — a regression. Rejected.

## 3. The fix

1. Two new `kCraftMenus` rows, cited above:
   `{"i_board", "boards", nullptr, nullptr}` and
   `{"i_gears", "Parts", "gears", nullptr}`.
2. `DoCraft`, no-route branch: if the recipe is `WorldProcessed` and
   `ProducingGoalFor()` names a different goal, `HandOff` to it instead of
   refusing.
3. Anti-respin: `CraftFocus::NoteNoRoute` / `Unreachable`, threshold
   `kNoRouteStrikes = 3`. Both refusal branches record a strike; `ChooseCraft`
   skips an output with three. Session-scoped. Three rather than one because
   the menu-content branch can fire on a half-read dialog, whereas the
   table-miss branch is deterministic. `Needs.cpp` already passes the same
   `craftFocus_`, so the need stops being raised too.
4. `CraftMenuPath` + `CraftMenuFor` moved from Runner.cpp to Identity.cpp with
   the declaration in `uo/life.h`, so a no-server test can assert a route.
   The table body is unchanged apart from the two new rows.

## 4. Gates

    python tools/rev.py build   -> ok
    python tools/rev.py test    -> 43/43 passed
    build-m1/tests/m5_professions.exe -> 618 checks, 0 failures
      new sections:
        [craft: the three outputs that spun on REFUSE_MISSING_RECIPE]
        [craft: three refusals and the bench moves to something reachable]

## 5. Smoke — run_gates, 5 min, Vorar/Serena/Draver (one run, no retry)

`REFUSE_MISSING_RECIPE` count after the fix: **0 in all three consoles.**

**Serena (merchant_tinker, i_gears) — PROVEN, real output.**
g_Serena.console.txt:65-134: "using its own tool to open the menu" ->
"chose 'Parts'" -> "chose 'gears'" -> `craft: made i_gears pack 0->1` ... `3->4`.
`.makelast 5` repeat fired. Server-side confirmation:

    python tools/world_query.py --char Serena
    PACK i_gears x4
    PACK i_ingot_iron x6

The run ends on a DIFFERENT typed outcome —
`goal_failed=CRAFT status=no_progress "the attempts are spent and the pack
never moved"` at 14:53:05 — which is the correct refusal for exhausted
material, not a route miss.

**Vorar (lumberjack_swordsman, i_board) — typed refusal changed, as accepted.**
g_Vorar.console.txt:135-139:

    reason: i_board: every input is in the pack
    goal_blocked=CRAFT reason="REFUSE_MISSING_TOOL" nothing carried or worn to
      open the i_board menu with (i_log)

MISSING_RECIPE is gone; the honest remaining blocker is that he has no saw
(0x1034, Tool::CarpentryTool). Once, not spinning — 5-minute run then moved on
to TRAIN_COMBAT/BANK. The i_board ROUTE itself is proven by script citation and
unit test, not yet by a live menu walk, because no carpenter in the smoke set
carried a saw.

**Draver (miner_smith, i_ingot_iron) — no longer reached.**
The planner picked SMELT directly this run (`goal_changed=SMELT` x2,
`smelt: +10 i_ingot_iron (26 in the pack)`), so the new WorldProcessed hand-off
branch was never entered. It is defensive and unit-covered
(no route + WorldProcessed asserted), **not runtime-proven.**

## 6. Limits

- i_board menu walk: unit-tested and script-cited, not observed live (missing saw).
- The WorldProcessed -> owning-goal hand-off: not observed live.
- Other outputs with no route row remain (e.g. tailor i_sash/i_robe/
  i_leather_tunic). They now stand aside after 3 strikes instead of spinning,
  but they still cannot be made. Out of this brief's scope; worth a follow-up
  audit of every `produces` entry against `kCraftMenus`.
