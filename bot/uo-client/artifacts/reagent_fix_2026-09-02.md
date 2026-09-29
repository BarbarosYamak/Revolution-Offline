# Practice casting pays for its reagents (2026-09-02)

Defect source: `artifacts/wave_2026-09-02_verdict.md` sec e -- four mages spun
PRACTICE_SKILL casting with an empty reagent pouch.

## Root causes (three, not one)

1. **Nothing knew what a spell costs.** `Runner::PickPracticeSpell` held a bare
   list of twelve spell numbers and cast the first one the book contained.
   `include/uo/rules.h:13` still asserts "reagents runtime not required
   (ReagentsRequired=0)"; `runtime/sphere.ini:1136` reads `ReagentsRequired=1`.
   The comment is stale, and the client believed it.
2. **The refusal was unreadable.** `You lack Sulfurous Ash for this spell` was
   classified only as a generic `ServerFailure`
   (`include/uo/actions.h:283 IsSpellCastRefusal`), so the goal re-sent the same
   cast every 6 s: Aurelius 156, Illyria 298, Elara 322, Selene 310.
3. **The pack could not COUNT most reagents.** `Runner::Observe` fills
   `obs.pack` only from the profession's `produces`/`consumes` lists. The mage
   entry (`src/life/Professions.cpp:452-454`) names four reagents; Night Sight
   -- the first spell practice reaches for -- costs spider silk and sulfurous
   ash, neither of them listed. Left unfixed, a pouch of 250 ash would have read
   as EMPTY and the new restock errand would have bought ash forever.

## Ground truth

Reagent lists are the `RESOURCES=` lines of each `[SPELL n]` block in
`runtime/scripts/spells/spells_magery.scp` (line numbers cited per row in
`include/uo/spellcast.h`). No quantity prefix there, so one of each per cast.

## Change

* `include/uo/spellcast.h` (new, header-only, pure): the twelve self-safe
  spells with their reagent costs, `ChoosePracticeSpell` (book x pack x
  session-refusals -> a spell, or the shortest shopping list),
  `LackNeedleFor` (journal needle per reagent), `ExpectedPracticeCasts` and
  `PlanReagentBuy` (the dynamic quantity).
* `DoPracticeSkill`: reads the server's answer to the previous cast first --
  a named reagent strikes that spell off for the session and re-plans; "you
  lack sufficient mana" deliberately does not. Then chooses a spell the pack
  can pay for; if none, publishes a shopping list and stands down on
  `kNoReagentCooldownMs`.
* `Observe`: publishes `obs.practiceReagentsShort` / `practiceReagentQty`, and
  counts every `i_reag_` in the pack whatever the profession lists.
* `AssessNeeds`: a `NeedSupplies` row at 0.46 ("buy spell reagents"), placed
  BEFORE the craft-supplies row because `FindNeed` returns the first match.
  Blocked when the purse is under the same 100 gold working floor the craft
  clause uses.
* `DoBuySupplies`: the reagent list jumps the queue and reuses the existing
  vendor path unchanged (`SupplierTradeFor` already answers "mage" for
  `i_reag_`); no second vendor flow.
* `Planner::ClearCooldown`: the stand-down ends when the pouch is stocked.

## Quantity: a rate, not a constant

`target = casts still expected this sitting`, one per cast. Rate is observed
(casts so far / minutes elapsed) once there is a minute to observe; before that
the prior is derived from the cadence itself (one cast per
`kPracticeCastPeriodMs`, a quarter share of the tick). Horizon is a whole
session, because reagents keep and the walk is the expensive part. Capped by
the purse at the price this character has actually SEEN
(`state_.prices.Latest(..., NpcVendorSells)`); unknown price defers to the
vendor errand, which reads the shelf.

## Runtime evidence (5 min gate, 2026-09-02 10:07-10:12)

The console for this run has since been OVERWRITTEN by the later gates -- the
runner reuses `run_gates/g_<char>.console.txt` -- so the lines are transcribed
here verbatim, with their line numbers as they stood at 10:12.

```
:626 practice: out of reagents for spell 6 -- wants 3 of each of
     [i_reag_spider_silk, i_reag_sulfur_ash] (3 cast(s) left this session, one
     of each per cast, ...) and is standing down so BUY_SUPPLIES can go to a mage
:627 goal=BUY_SUPPLIES reason="previous goal abandoned: no reagents for any
     castable spell"
:628 needs considered: ... NeedSupplies(buy spell reagents 0.46) ...
:630   reason: NeedSupplies urgency 0.46 x 140 = 64.4
:631   reason: 3 x each of i_reag_spider_silk,i_reag_sulfur_ash
:635 supplies: buying 3 i_reag_spider_silk at 3 each from 'Spider's Silk'
     [VENDOR] buy item=0x40013153 qty=3 from vendor=0x0000F133 gold=9330
     [ACTION_RESULT] vendor_buy success (0ms) purchased item delivered
     Ilona: Here you are, Aurelius That will be 9 gold coins.
:645 supplies: the server took 9 gold for i_reag_spider_silk (purse 9330 -> 9321)
:646 supplies: i_reag_spider_silk is in the pack now -- off the reagent list
:717 supplies: buying 3 i_reag_sulfur_ash at 3 each from 'Sulfurous Ash'
     [ACTION_RESULT] vendor_buy success (0ms) purchased item delivered
:729 goal_changed=EXPLORE from=BUY_SUPPLIES ...
```

`grep -c "You lack" run_gates/g_Aurelius.console.txt` = **0** (was 156).

Two follow-ups were found IN this run and fixed afterwards: the quantity
horizon (three of each, with 9,330 gold in the purse) and the four-minute
stand-down that outlived the purchase (:729, EXPLORE with a full pouch).
Both are unit-covered (`tests/m4_life.cpp`
`TestPracticeChecksTheReagentPouch`) but NOT re-proven live -- see below.

## Runs after the follow-up fixes (10:44 and 10:49)

`grep -c "You lack"` = 0 for Aurelius in both. Neither reached PRACTICE_SKILL:
`NeedSpells` scored 0.48-0.70 x 110 against practice's 0.38 x 120, so
FILL_SPELLBOOK held the whole session and re-selected itself four times --
`goal_changed=FILL_SPELLBOOK from=FILL_SPELLBOOK reason="previous goal
abandoned: attempts 5 >= 5"` (run_gates/g_Aurelius.console.txt:702, 1000, 1300,
1599). That is a separate defect (a goal abandoning itself with no cooldown),
outside this task's scope, and it is why the horizon and cooldown-clear
refinements have unit evidence only.

## Still open

`run_gates/g_Elara.console.txt` has 82 `You lack Mandrake Root` lines, and NONE
of them are practice: they are `food: casting Create Food rather than shopping`
(`DoGetFood`, Runner.cpp ~10600) -- Create Food is spell 2 and costs garlic,
ginseng and mandrake root. Same defect, different goal. That region is owned by
another agent this session and was left untouched. The fix is one call:
`spell::DefForSpell(2)` and the same pack check before the cast.
