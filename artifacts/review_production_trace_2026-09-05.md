# Production routine trace: mine -> smelt -> craft -> sell

Read-only review, working tree on top of 621d3d9, 2026-09-05.
Scope: `miner_smith` / `merchant_tinker`. All paths relative to `bot/uo-client/`.

Evidence classes: **SRC** = verified by reading the file in this working tree;
**DOC** = quoted from an in-repo comment citing a past live run (not re-run
here); **HYP** = my inference, marked as such.

---

## 1. OWNERSHIP

### 1.1 The chain, step by step

| Step | Need (Needs.cpp) | Goal + weight (Goals.cpp) | Handler |
|---|---|---|---|
| mine | `NeedOre` `Needs.cpp:1659`, urgency `base - (base-0.15)*glut`, base 0.65 at work site / 0.45 away | `Mine` 130.0 `Goals.cpp:246` | `DoMine` `runner/Gather.cpp:1542` |
| smelt | `NeedSmelt` `Needs.cpp:1692`, urgency `0.25 + 0.50*min(1, ore/20)` | `Smelt` 140.0 `Goals.cpp:252` | `DoSmelt` `runner/Gather.cpp:585` |
| craft | `NeedCraft` `Needs.cpp:1372`, urgency 0.50 (0.95 only for the archer arrow case) | `Craft` 130.0 `Goals.cpp:260` | `DoCraft` `runner/Craft.cpp:44` |
| buy inputs | `NeedSupplies` `Needs.cpp:1452`, urgency `max(0.44, 0.95 - 0.51*funded)` | `BuySupplies` 140.0 `Goals.cpp:261` | `DoBuySupplies` `runner/Economy.cpp:2119` |
| sell (NPC) | `NeedGold` `Needs.cpp:1147` | `EarnGold` 150.0 `Goals.cpp:206` | `DoEarnGold` `runner/Economy.cpp:63` |
| sell (player) | `NeedTrade` `Needs.cpp:1137` / `:1227` | `TradeWithPlayer` 145.0 `Goals.cpp:240` | `DoTradeWithPlayer` `runner/Economy.cpp:966` |
| store | `NeedBank` `Needs.cpp:982` ("put unsold stock away") 0.40, 0.60 at `surplusWorthTrip` | `Bank` 240.0 `Goals.cpp:201` | `DoBank` `runner/Bank.cpp:56` |

### 1.2 Who owns what

- **Active goal** — `Planner` (`Runner::planner_`, `Runner.h:361`). Selected once
  per Live tick at `runner/Core.cpp:1355` (`planner_.Select`). Handlers never set
  the goal; they end it with `planner_.Finish()` / `planner_.Cooldown()`, or
  defer with `Runner::HandOff`.
- **Travel toward the goal's place** — owned by `Client` / `ClientTravel`, not by
  the goal. The Runner keeps only a boolean shadow, `travelInFlight_`
  (`Runner.h`, transient slate). Handlers issue `client.TravelToPoint` /
  `TravelToServiceSkipping` (`runner/Craft.cpp:155,171`; DoSmelt's forge search
  in `runner/Gather.cpp`) and then gate every later tick on
  `if (client.TravelBusy()) return false;`.
- **Pending Client action** — owned by `Client` (`ActionBusy()`,
  `BeginAction`/`FinishAction`, `src/Client.cpp`). Handlers gate on
  `client.ActionBusy()` (`runner/Craft.cpp:88`, `runner/Gather.cpp:586`) and pace
  with `Runner::nextActionMs_`.
- **Retry counts / attempt limits** — split three ways: (a) the planner's generic
  `maxAttempts` / `maxGoalMs`; (b) per-errand `i32` counters on `Runner`;
  (c) `life::Handshake` (`craftWait_`). See 1.3.
- **Cancellation on planner switch** — `runner/Core.cpp:1394-1425`, inside
  `if (!sameErrand)`. It clears **only** `chopTargetValid_`,
  `chopCursorPending_`, `travelInFlight_`, `travelAttempts_` and seven
  `lastXxxPlan_` log sentinels; `vendorChases_` and `logsAtGoalStart_` are reset
  unconditionally just after. `client.TravelAbort` is called for exactly one
  goal, `TrainCombat` (`Core.cpp:1396-1398`). **SRC**
- **Completion** — the handler returns `true`; `Runner::RunGoal`
  (`runner/Core.cpp`, the `switch (goal)` dispatch) logs `goal_completed=`.
  Failure is `planner_.Finish(false, ...)` plus an explicit `Cooldown`.

### 1.3 Counter / limit table for this routine

| Counter or limit | Where declared | Value | Guards |
|---|---|---|---|
| `PlannerConfig::maxAttempts` | `include/uo/life.h:1459` | 5 | any goal, via `Exhausted` (`Goals.cpp:556`) |
| `PlannerConfig::maxGoalMs` | `include/uo/life.h:1457` | 300000 | any goal wall-clock (`Goals.cpp:560`) |
| `PlannerConfig::minCommitMs` | `include/uo/life.h:1454` | 20000 | floor before re-evaluation |
| `kSpinCooldownMs` | `Goals.cpp:745,764` | 60000 | anti-spin backstop |
| `kExhaustedCooldownMs` | `runner/RunnerInternal.h:463` | 120000 | rest after a time-limit exhaustion |
| `kMaxMineTrips` | `runner/RunnerInternal.h:756` | 3 | `mineTrips_` |
| `kMineRefusalsBeforeAdvance` | `runner/RunnerInternal.h:770` | 3 | `mineConsecRefusals_` |
| `kMaxMineAdvances` | `runner/RunnerInternal.h:776` | 3 | `mineAdvances_` |
| `kMineResolveMs` / `kMinePollMs` | `runner/RunnerInternal.h:780-781` | 15000 / 1500 | one swing's verdict window |
| `smeltTrips_` | `Runner.h`; used `Gather.cpp:842` | inline `>= 3` | trips to a smithy |
| `smeltApproaches_` | `Runner.h`; used `Gather.cpp:764` | inline `>= 2` | walk-ups to one forge (owner "max 2" rule) |
| `smeltRefusals_` | `Runner.h`; used `Gather.cpp:649` | inline `>= 3` | "must be near a forge" replies |
| `kMaxSmeltReachFails` | `runner/RunnerInternal.h:455` | 3 | distinct stand tiles (`Gather.cpp:968-1009`) |
| `kSmeltStandGotoMs` | `runner/RunnerInternal.h:459` | 6000 | one step between forge stand tiles |
| `deadForges_` | `Runner.h` | cap 16 | forges written off (`Gather.cpp:653`) |
| `kMaxCraftAttempts` | `runner/RunnerInternal.h:741` | 3 | `craftWait_` swings with nothing made |
| `kCraftResolveMs` | `runner/RunnerInternal.h:737` | 8000 | one craft swing deadline |
| `kCraftStuckCooldownMs` | `runner/RunnerInternal.h:745` | 120000 | every craft stand-down and HandOff |
| `kMaxSupplyTrips` | `Runner.h` | 3 | `supplyTrips_` per `supplyItem_` |
| `kMaxSupplyReachFails` | `Runner.h` | 2 | `supplyReachFails_` |
| `kMaxVendorChases` | `runner/RunnerInternal.h:489` | 4 | `vendorChases_` |
| `kMaxSellTrips` | `Runner.h` | 3 | `sellTrips_` |
| `kMaxSellSweeps` | `Runner.h` | 8 | `sellSweeps_` |
| `kNoBuyerCooldownMs` | `Runner.h` | 180000 | EARN_GOLD after every buyer failed |
| `kMaxBankTrips` | `Runner.h` | 4 | bank trips |
| `kMaxBankDepositTries` | `Runner.h` | 5 | `bankDepositTries_` per item name |
| `kMaxBankItemMoveFails` | `Runner.h` | 3 | `bankItemMoveFails_` |
| `bankGoldDepositTries_` | `Runner.h` | shares 5 | gold deposits |
| `kMaxBankOpenTries` | `Runner.h` | 3 | asks to a banker |
| `kBankAskGapMs` / `kBankCooldownMs` | `Runner.h` | 7000 / 300000 | ask spacing / empty-visit rest |
| `kMaxBankShouts` | `runner/RunnerInternal.h:782` | 3 | bank shouts |
| `kMaxMarketLiftFails` / `kMaxMarketBoxReopens` | `Runner.h` | 2 / 2 | market withdrawal |
| `kMaxTradeTrips` | `runner/RunnerInternal.h:227` | 3 | `tradeTrips_` |
| `kMaxAnnounces` / `kAnnounceIntervalMs` | `Runner.h` | 6 / 8000 | WTS/WTB cycle |
| `kMaxGoldCarriedRt` / `kGoldWorthCarryingRt` | `runner/RunnerInternal.h:746-747` | 800 / 500 | pack-coin cap (runner copy) |
| `coinLiftFails_` | `Runner.h` | inline in `DoBank` | refused coin lifts |
| `kMaxDismountClicks` / `kMaxRemountClicks` | `runner/Gather.cpp:32-33` | 3 / 3 | gathering dismount/remount |
| `CraftFocus` run / no-route strikes | `include/uo/life.h` (`CraftFocus`), `Identity.cpp:503-537` | `kFocusRun`, `kNoRouteStrikes` | sitting rotation, dead-recipe strikes |

### 1.4 Overlapping limits (two counters guarding the same failure)

1. **Forge unreachability is guarded four times.** `smeltApproaches_` (>=2,
   `Gather.cpp:764`), `smeltRefusals_` (>=3, `Gather.cpp:649`),
   `smeltReachFails_` / `kMaxSmeltReachFails` (3 distinct stand tiles,
   `RunnerInternal.h:455`) and `smeltTrips_` (>=3, `Gather.cpp:842`) all bound
   "I cannot get to a forge", on top of `maxAttempts=5` and `maxGoalMs=300000`.
   Three of the four are inline magic numbers, not named constants. The owner's
   "unreachable = 1 try, max 2" rule is honoured only by `smeltApproaches_`. **SRC**
2. **Craft attempt counting is doubled.** `craftWait_` bounds swings at
   `kMaxCraftAttempts=3` (`Craft.cpp:271-274`); every `NoteAttempt` in the same
   handler also feeds `maxAttempts=5`. Either can end the sitting, and the log
   line at `Craft.cpp:305` reports only the handshake's count.
3. **Bank deposit failure is doubled.** `bankDepositTries_` (5, per item name,
   `Bank.cpp:328`) and `bankItemMoveFails_` (3, per issued drag,
   `Bank.cpp:41-47`) guard the same "deposit did not land" event; the tighter one
   fires first, so the 5 is effectively unreachable for that mode.
4. **Vendor unreachability is doubled.** `supplyReachFails_` (2) and
   `vendorChases_` (4) both bound "the shopkeeper is not in touch range".

### 1.5 State that survives `goal_changed` without being reset

The `!sameErrand` block (`runner/Core.cpp:1400-1425`) resets four transient
fields plus seven log sentinels. Everything below lives on `Runner` and is
**not** cleared there, nor anywhere else on the goal-change path. **SRC**

| Field(s) | Declared | Cleared where | Consequence |
|---|---|---|---|
| `craftItem_`, `craftHadBefore_`, `craftJournalMs_`, `craftMade_`, `craftSittingTarget_`, `craftWait_`, `makeLastIssued_`, `makeLastRemaining_`, `makeLastDeadlineMs_` | `Runner.h` (crafting block) | only in `DoCraft` when `craftItem_ != intent.item` (`Craft.cpp:246-258`), and `craftItem_.clear()` on Done (`Craft.cpp:419`) | re-entering CRAFT on the same item minutes later compares the pack against a stale `craftHadBefore_` and reads the journal from a stale `craftJournalMs_`. If BANK deposited the output meanwhile, `packNow < packBefore`. **HYP** (mis-verdict; not observed in evidence I hold) |
| `craftCursorPending_`, `craftClickedMs_` | `Runner.h` | never on goal change | a cursor armed by CRAFT is still "pending" when SMELT next arms one |
| `smeltForgeX_/Y_`, `smeltTriedStands_`, `smeltGotoMs_`, `smeltCursorPending_`, `smeltClickedMs_`, `smeltStartedMs_`, `smeltIngotsBefore_`, `smeltIngotName_`, `smeltFinalHop_`, `smeltTrips_`, `smeltApproaches_`, `smeltRefusals_`, `smeltReachFails_`, `smeltSkipPlaces_` | `Runner.h` | scattered inside `DoSmelt` on success paths (`Gather.cpp:715,771,777,859,869,911,1009`); `smeltSkipPlaces_` also cleared by `DoCraft` (`Craft.cpp:184`) | trip / approach budgets carry across a `goal_changed`, so an unrelated goal's turn does not restore the allowance |
| `mineX_/Y_/Z_`, `mineGraphic_`, `mineCursorPending_`, `mineSwungMs_`, `mineJournalMs_`, `mineRoam_`, `mineConsecRefusals_`, `mineAdvances_`, `mineTrips_` | `Runner.h` | inside `DoMine` only | same |
| `supplyItem_`, `supplyTrade_`, `supplyService_`, `supplyTrips_`, `supplySkipPlaces_` | `Runner.h` | cleared together when `supplyItem_` changes, in `DoBuySupplies` | errand state for an item the planner has moved on from |
| `pendingBuyItem_`, `pendingBuyGoldBefore_` | `Runner.h` | settled on the next `DoBuySupplies` tick | an unsettled purchase ledger entry survives the switch; if BUY_SUPPLIES is never re-picked the gold delta is never written |
| `bankItemMovePending_`, `bankItemMoveFails_`, `bankGoldDepositPending_`, `pendingGoldDepositBefore_`, `bankDepositItem_`, `bankDepositTries_` | `Runner.h` | `SettleBankItemMove` (`Bank.cpp:19`), reachable only from `DoBank` | **an in-flight drag survives `goal_changed`** and is settled only if BANK is re-picked |
| `sellItem_`, `sellTrade_`, `sellService_`, `sellBuyerIndex_`, `sellTrips_`, `sellWanted_`, `sellLotCap_`, `sellSweeps_`, `sellSweepGold_`, `sellVerifyItem_`, `sellGoldBefore_`, `sellItemBefore_`, `sellAsked_`, `sellSent_`, `sellVendorSerial_`, `sellApproached_`, `sellReachChecked_` | `Runner.h` | inside `DoEarnGold` | an unverified sale (gold / pack baselines) is left dangling |
| trade state (`tradePartner_`, `tradeItem_`, `tradeOffered_`, `tradePackBefore_`, `tradeGoldBefore_`, ...) | `Runner.h` | `ResetTradeState()` (`Economy.cpp:1989`), called only from `DoTradeWithPlayer` | an open trade window's bookkeeping outlives the goal |
| `gatherOnFoot_`, `gatherDismountClicks_`, `gatherRemountClicks_`, `gatherMountLostMs_` | `Runner.h` | `RemountAfterWork` (`Gather.cpp:66`) | a character dismounted for mining stays on foot through every other goal until MINE or GATHER_LOGS runs again |
| `coinWanted_` | `Runner.h` | when the purchase settles | keeps `NeedBank` "withdraw for a purchase" at 0.80 alive for a purchase nobody is making any more |
| **the journey itself** (Client travel target) | `ClientTravel` | only `client.TravelAbort`, called on goal change for `TrainCombat` alone (`Core.cpp:1396`) | the new goal's first ticks are spent blocked on `TravelBusy()` while walking to the previous goal's destination |

---

## 2. INTENT PRESERVATION

### 2.1 Is there anything that says "I am mid mine->smelt->craft->sell"?

**No.** **SRC** — searched `Runner.h`, `include/uo/life.h`, `PersistentState`
(`src/life/State.cpp`, `Json.cpp`) and `Planner` (`Goals.cpp`). The nearest
constructs are all narrower:

- `Planner::goal_` — one `GoalKind`, plus `startedAtMs`, `attempts`, `progress`.
  No predecessor, no successor, no objective.
- `CraftFocus` (`Identity.cpp:503-537`; `craftFocus_` at `Runner.h:365`) —
  which *product* was last sat on and for how many sittings, plus a per-item
  no-route strike list. Explicitly session-scoped, and about rotation rather
  than about a chain.
- `HandOff(from, to, ...)` — `Runner.h` documents the `to` argument as
  **advisory only**: "`to` is advisory only -- `Planner::Select` picks the actual
  receiver." It cools `from`, logs, and returns false. Nothing records the intent.
- `state_.plan` / `state_.memory` — build targets and world knowledge, not an
  in-flight objective.

Every tick is therefore an independent utility re-score, moderated only by
`incumbentBonus = 0.15` (`life.h:1451`), `minCommitMs = 20000` (`life.h:1454`)
and the three satiation damps (`Goals.cpp:833-857`).

### 2.2 Concrete places the prerequisite chain loses its purpose

**(a) Smelt completes and nothing carries the ingots forward.** `DoSmelt` ends
with `planner_.Finish(true, ...)` at `Gather.cpp:596` when no ore remains. On
that tick `NeedSmelt` vanishes (the `add` at `Needs.cpp:1692` is inside
`if (ore > 0)`), `NeedOre` re-scores from a `stock` that now *includes* the new
ingots, and `NeedCraft` offers a flat 0.50 x 130 = 65. Competing that same tick:
`BuySupplies` 0.44..0.95 x 140 = 61.6..133, `TradeWithPlayer` up to
0.55 x 145 = 79.8, `EarnGold` x150, `PracticeSkill` 120, `TrainAtNpc` 110, and —
for a smith still short of its Blacksmithing target — `Mine` at up to
0.65 x 130 = 84.5. Nothing gives CRAFT credit for the smelt that just happened.
**SRC** for the weights and formulas; the ranking is arithmetic on them.

**(b) Craft is satiation-damped by the very steps that fed it.** `Goals.cpp:840-857`
applies goal satiation, *family* satiation and `FamilyShareDamp`. Mine, Smelt,
Craft, GatherLogs and EarnGold are all `GoalFamily::Work` (`FamilyOf`,
`Goals.cpp`), so a productive mine/smelt stretch **lowers** the score of the
craft it was for, by up to `kSatiationMax = 0.45` (`life.h:1630`). This is the
clearest structural disincentive against completing a chain. **SRC**

**(c) BANK stores the ingots CRAFT needs, and nothing withdraws them.**
Four facts compose into a dead end:
- `DoBank` `runner/Bank.cpp:293-348`: when `loadDemandsIt`, it deposits every
  entry of `profession->produces` at the **full amount, with no keep**. For
  `miner_smith`, `produces` includes `i_ingot_iron` (`Professions.cpp:364`).
- `DoBank` `runner/Bank.cpp:408-449`: the inputs branch deposits recipe inputs
  down to `keep = craftBatch * 2 = 10`. `i_ingot_iron` is an input of `i_dagger`
  (`progression/Production.cpp:105-106`, 4 ingots), so it is reached here too.
- `ChooseCraft` `Identity.cpp:762` counts inputs with
  `market::QtyOf(obs.pack, in.item)` — **pack only**. `DoCraft`'s
  `inputsAvailable` loop (`Craft.cpp:355`) likewise reads `obs.pack`.
- Withdrawal paths exist for arrows (`Bank.cpp:103`), reagents (`Bank.cpp:133`),
  gold (`Bank.cpp:215`, `Economy.cpp:2649`) and stock-to-sell (`Economy.cpp:387`,
  `Economy.cpp:1635`). **There is no craft-input withdrawal.** **SRC**

Meanwhile `NeedOre` (`Needs.cpp:1631-1637`) counts pack **plus bank**, so a smith
with 550 banked ingots correctly stops mining and simultaneously reads as having
0-10 ingots for crafting. The mine -> smelt -> bank -> craft path cannot close.
The only "this is work in progress, do not bank it" carve-out is
`WoolChainWorkInProgress` (`Needs.cpp:905`; `Bank.cpp:307,437`), which names
wool / yarn / bolt / cloth only. No metal equivalent exists.

**(d) "Put unsold stock away" outranks the trade it is waiting for.**
`Needs.cpp:982` scores 0.40 (0.60 at `surplusWorthTrip = 20`) x weight 240 =
96..144, against `TradeWithPlayer` at most 0.55 x 145 = 79.8 and `Craft` at 65.
The comment at `Needs.cpp:975-980` says 0.40 was chosen to sit *below*
NeedTrade — but that comparison is on **urgencies**, and the two goals carry
different weights (240 vs 145). On score, BANK wins. **SRC** (arithmetic on the
two rows).

**(e) HandOff chains drop the objective at every link.** `DoCraft` hands off to
`BuySupplies` (`Craft.cpp:84`, `:113`, `:443`), `GetTool` (`Craft.cpp:210`),
`Mine`/`GatherLogs` (`Craft.cpp:452`) or `IdleBriefly` (`Craft.cpp:311`) — each
with `kCraftStuckCooldownMs = 120000`. The `to` argument is discarded, CRAFT is
cooled two minutes, and when the prerequisite finishes the chain has to be
re-derived from scratch — by which time (b) is working against it.

**(f) `craftSittingTarget_` is the one cross-tick piece of intent, and it is
unprotected.** Set once per sitting (`Craft.cpp:392-400`) from `inputsAvailable`,
it survives a `goal_changed` and is never re-validated. If BANK deposits the
material mid-sitting the target still names the old size.

### 2.3 Smallest data structure that would carry the objective

One struct on `Runner`, transient (never persisted — same rule as the rest of
the transient slate):

```cpp
// Runner.h, beside craftFocus_
struct Objective {
    GoalKind    goal   = GoalKind::Count;  // what this chain is FOR (Craft / EarnGold)
    std::string item;                      // the product, e.g. "i_dagger"
    GoalKind    prereq = GoalKind::Count;  // the step being done right now
    std::string prereqItem;                // what that step must yield, "i_ingot_iron"
    i32         prereqQty = 0;             // how much of it
    i64         setMs   = 0;               // staleness mark, same shape as clothMarkMs_
};
Objective objective_;
static constexpr i64 kObjectiveStaleMs = 10 * 60 * 1000;
```

**Written in exactly one place.** `Runner::HandOff` already receives `from`, `to`
and `why` — record
`objective_{from, craftItem_, to, missingItem, missingQty, nowMs}` there.
**Cleared in three:** on completion of `objective_.goal`, on staleness, and when
`DoCraft` picks a different `intent.item` (the existing reset at
`Craft.cpp:246-258`).

**Read by four existing sites**, each a small edit:

1. `Planner::Score`, inside the per-goal modifier `switch` that already ends at
   `Goals.cpp:435` — `+25` when `spec.kind == objective_.prereq`. One extra case
   in a switch that already has five.
2. `Goals.cpp:840-857` — skip `famSat` / `shareSat` for `objective_.prereq` and
   `objective_.goal`. This is the fix for 2.2(b).
3. `DoBank` `Bank.cpp:293` and `:408` — treat `objective_.prereqItem` exactly the
   way `WoolChainWorkInProgress` is already treated at `Bank.cpp:307` and `:437`:
   keep `objective_.prereqQty`, bank only above it. Smallest fix for 2.2(c) that
   does not invent a metal-specific rule.
4. `Needs.cpp:868-890` (`unsoldStock`) — skip `objective_.prereqItem`, mirroring
   the existing `WoolChainWorkInProgress` skip at `Needs.cpp:880`.

`Survive` / `Heal` / `RecoverCorpse` still interrupt freely: they are
Emergency-family and clear `preemptScore = 900.0` (`life.h:1461`) regardless of a
+25, and the objective is never consulted on the emergency path. A death or a
session end simply lets `kObjectiveStaleMs` retire it.

Deliberately **not** proposed: a plan stack, a behaviour tree, persistence, or a
generic prerequisite graph. One struct, one writer, four readers.

---

## 3. DATA vs CODE

### 3.1 What `Professions.cpp` already parametrizes

Fields on `prof::Profession` (`include/uo/professions.h:128-266`), with the
`miner_smith` (`Professions.cpp:315-383`) and `merchant_tinker`
(`Professions.cpp:1422-1474`) values:

| Field | header | miner_smith | merchant_tinker |
|---|---|---|---|
| `id` / `label` | :129,:130 | `miner_smith` | `merchant_tinker` |
| `startSkillA` / `B` / `startZeroSkill` | :134,:135,:167 | Mining / Blacksmithing / Tinkering | Tinkering / Mining / — |
| `startStr/Dex/Int` | :170 | 50/20/10 | 50/20/10 |
| `targets` (id, tenths, priority, capped, role) | :174 | BS 1000 p5 Secondary; Mining 1000 p4 Primary; Tinkering 500 p2 capped Utility | Tinkering 1000 p2 Primary; Mining 500 p1 Utility |
| `unresolvedTenths` | :175 | 4500 | 5500 |
| `targetStr/Dex/Int` | :177 | 100/100/25 | 100/75/50 |
| `income` | :179 | Craft, Process, Gather | Craft |
| `tools` (name, graphics, wielded) | :180 | pickaxe (wielded), smith hammer | tinker tools, pickaxe (wielded) |
| `consumables` | :181 | CrafterHealPotions, Food | CrafterHealPotions, Food |
| `gathers` | :185 | `"ore"` | `"ore"` |
| `produces` | :189 | i_dagger, i_ingot_iron, i_spear_short, i_cutlass | i_gears, i_lockpick, i_tinker_tools, i_pickaxe, i_scissors, i_sewing_kit, i_pen_and_ink, i_barrel_tap, i_barrel_hoops, i_keg_potion |
| `consumes` | :192 | i_ore_iron, i_log | i_ingot_iron, i_board, i_bowl_wood, i_feather, i_ink_well, i_sewing_needle, i_thread, i_yarn_ball |
| `trainingCrafts` | :197 | *(empty)* | *(empty)* |
| `riskTolerance` | :204 | 0.35 | 0.30 |
| `wears` / `maysShield` | :219,:224 | Metal / true | Cloth / false |
| `combatStrategy` | :237 | AvoidCombat | AvoidCombat |
| `goldReserve` | :250 | 500 | 250 |
| `homeCities` | :265 | Minoc, Britain, Vesper | Britain, Minoc |

### 3.2 Hardcoded per-profession / per-item thresholds outside the table

| Constant | file:line | Value | What it decides |
|---|---|---|---|
| `kFirstSmithBatch` | `Needs.cpp:1192` | 20 | below this ore+ingot count a `gathers=="ore"` life may not raise a player-buy `NeedTrade` |
| `kEnoughToSmith` | `Needs.cpp:1621` | 20 | NeedOre glut denominator, and NeedSmelt's "a batch" denominator |
| `kMiningFloor` | `Needs.cpp:1622` | 0.15 | NeedOre urgency floor at full glut |
| `kSmithTrainingStock` | `Needs.cpp:1645` | 550 | ore+ingot stock wanted before Blacksmithing training; applied **only** when the plan holds a `kBlacksmithing` target (`Needs.cpp:1648-1653`) |
| NeedSmelt urgency | `Needs.cpp:1692` | `0.25 + 0.50*ready` | literal, no profession input |
| NeedCraft urgency | `Needs.cpp:1372` | 0.50 | literal |
| NeedSupplies floor / span | `Needs.cpp:1443` | `max(0.44, 0.95-0.51*funded)` | literal |
| BUY_SUPPLIES capital floor | `Needs.cpp:1268`, `Needs.cpp:1404` | 100 | `(obs.gold - 100) <= 0` -> no shopping trip |
| `NeedConfig::craftBatch` | `include/uo/life.h:1120` | 5 | sitting floor, every profession |
| `NeedConfig::surplusWorthTrip` | `include/uo/life.h:1150` | 20 | when unsold stock becomes a bank trip |
| `NeedConfig::logsWorthBanking` | `include/uo/life.h:1145` | 20 | logs |
| `NeedConfig::goldFloor` | `include/uo/life.h:1144` | 100 | NeedGold trigger |
| `kMaxGoldCarried` / `kGoldWorthCarrying` | `Needs.cpp:221-222` | 800 / 500 | pack-coin cap: `min(goldReserve,800) + 500` |
| duplicate `kMaxGoldCarriedRt` / `kGoldWorthCarryingRt` | `runner/RunnerInternal.h:746-747` | 800 / 500 | the runner's own copy of the same two numbers |
| `kReagentCarry` | `include/uo/life.h:890` | 50 | mage working set; `/5` at `Needs.cpp:844` |
| `kArrowCarry` / `kArrowReserve` | `include/uo/life.h:884,891` | 100 / 400 | archer ammo |
| `TradePolicy::keepOfOwnOutput` | `include/uo/market.h:54` | 20 | what `Surplus()` holds back of self-consumed output (`Market.cpp:164`) |
| `TradePolicy::restockConsumablesTo` | `include/uo/market.h:55` | 20 | `Shortfall` / `PlayerMarketWants` target (`Market.cpp:252-255`); `market` term of the surplus cap (`Market.cpp:429`) |
| `PolicyForPurse` | `include/uo/market.h:124-128` | `minimumSurplusToOffer = 1` when gold < 200 | the only purse-driven bend |
| `keepToWorkWith` | `runner/Bank.cpp:371` | `craftBatch * 2` = 10 | ingots kept in the pack by the unsold-stock branch |
| `keep` (inputs) | `runner/Bank.cpp:409` | `craftBatch * 2` = 10 | recipe inputs kept in the pack |
| produces deposit | `runner/Bank.cpp:295-348` | **no keep** | every produced item deposited in full when loaded |
| `.makelast` cap | `Needs.cpp:450`, `Craft.cpp:365` | 500 | mirrors `revolution_makelast.scp:59` |
| `craftSittingTarget_` | `runner/Craft.cpp:392` | `max(craftBatch, inputsAvailable)` | sitting size, pack-only |
| `req.minimumMaterialsReserve` | `runner/Craft.cpp:403-407` | 0 | crafting consumes inputs to zero; the comment records this as UNKNOWN because no profession field carries a working reserve |
| `kAtOreDistance` / `kMineReach` / `kMineScanRadius` / `kMineKnownSpotWithin` | `runner/RunnerInternal.h:752,755,764,769` | 45 / 6 / 24 / 60 | mining geometry |
| `kVendorReach` | `runner/RunnerInternal.h:487` | `sphere::kTouchDist` (2) | mirrors the server rule — correctly sourced, listed for completeness |

### 3.3 Do Needs, execution and banking agree? — `i_ingot_iron`, `miner_smith`

| Question | Answer | Where |
|---|---|---|
| How many does the NEED want? | 550 (pack + bank) while Blacksmithing < 1000; 20 once trained | `Needs.cpp:1645-1653`, `:1631-1637` |
| How many does the NEED call "spare"? | everything above **20 in the pack** (`keepOfOwnOutput`) | `market.h:54`, `Market.cpp:164`, read at `Needs.cpp:872` |
| When does that spare become a bank trip? | at 20 (`surplusWorthTrip`), raising 0.40 -> 0.60 | `Needs.cpp:981` |
| How many does DoCraft see / keep? | **pack only**, consumed to 0 (`minimumMaterialsReserve = 0`) | `Identity.cpp:762`, `Craft.cpp:355`, `Craft.cpp:403-407` |
| How many does DoBank deposit? | **all of them** when `loadDemandsIt`; otherwise down to 10 in the unsold-stock branch | `Bank.cpp:295-348` (no keep), `Bank.cpp:371` (keep 10) |
| How many does DoEarnGold withdraw? | only stock with an NPC route above `MaterialSaleGateFor`; ingots are `PlayerMarketGood` (`VendorPolicy.cpp:60`) and `smith_output_to_vendor` refuses (`Faucets.cpp:242`), so **never** | `Economy.cpp:286-307` |
| Any craft-input withdrawal at all? | **no** — only arrows, reagents, gold and sell-stock | `Bank.cpp:103,133,215`; `Economy.cpp:387,1635,2649` |

**Disagreements, ranked:**

1. **550 vs 20 vs 10 vs 0 vs "all".** Five different answers to "how many ingots
   should this character be holding". 550 is a *stock* target counting the bank;
   20 is a *pack* keep from the trade policy; 10 is a different pack keep in
   DoBank; DoCraft's reserve is 0; the produces branch keeps none. Only the 550
   derives from the build plan — the rest are global constants.
2. **Pack-vs-bank basis mismatch on the same resource.** `NeedOre` counts
   pack+bank (`Needs.cpp:1631-1637`); `NeedSmelt` counts pack only
   (`Needs.cpp:1688`); `Surplus` / `unsoldStock` count pack only
   (`Needs.cpp:872`); `ChooseCraft` counts pack only (`Identity.cpp:762`);
   `MaterialSaleGateFor` counts pack+bank (`Runner.h`, market surplus cap).
3. **`keepOfOwnOutput = 20` vs `craftBatch*2 = 10`.** The need declares
   everything above 20 spare; the action banks down to 10. The need can be
   satisfied while the pack ends up below what that same need called the working
   reserve.
4. **`kSmithTrainingStock` is Blacksmithing-only.** `Needs.cpp:1648` scans
   `plan.skills` for `rules::kBlacksmithing` alone. `merchant_tinker` has a
   Tinkering 1000 target and consumes `i_ingot_iron` but gets `wantStock = 20`.
   Its recipes need 1-4 ingots each (`Production.cpp:191-216`), so 20 happens to
   be adequate — right by accident, not by data. Any future ore-using trade gets
   the wrong answer silently.
5. **`kMaxGoldCarried` / `kGoldWorthCarrying` exist twice** (`Needs.cpp:221-222`
   and `runner/RunnerInternal.h:746-747`) with identical values in two
   translation units, free to drift.

### 3.4 What the table would need to absorb these

The smallest data-side change that removes disagreements 1-3 for every ore trade
at once: two `i32` fields on `prof::Profession` —

- `workingInputReserve` — what stays in the pack to craft with (miner_smith ~40 =
  ten daggers; merchant_tinker ~20). Read at `Bank.cpp:371`, `Bank.cpp:409` and
  `Craft.cpp:403`, replacing `craftBatch * 2` and the literal 0.
- `stockBeforeTraining` — replacing the literal 550 at `Needs.cpp:1645`, with the
  skill scan at `Needs.cpp:1648` keying off the profession's own Primary /
  Secondary target rather than a hardcoded `kBlacksmithing`.

Not proposed as a change here; this review is read-only.
