# Combat routine trace: resupply -> travel -> hunt -> loot -> bank

Read-only architecture review. Working tree as-is on top of 621d3d9, 2026-09-05.
All paths relative to `bot/uo-client/`. Every line number below was grepped in
this working tree; nothing is quoted from memory.

## 1. OWNERSHIP

| Phase | Owner | file:line |
|---|---|---|
| Hunt goal exists | `AssessNeeds` NeedTraining per weapon-skill target, plus a "hunt for income" fallback when all targets are met | src/life/Needs.cpp:1789-1843 |
| Hunt goal scored | `kGoals` TRAIN_COMBAT weight 130 x urgency; modifiers -50 heavy, -30 hurt | src/life/Goals.cpp:217, 419-431 |
| Hunt goal driven | `Runner::DoTrainCombat` | src/life/runner/Train.cpp:61 |
| Travel to ground | novice -> `KnownPlace("britain_graveyard_graveyard")` + `TravelToPoint`; seasoned -> `Client::TravelToHuntingGround` | Train.cpp:33, 462-520 |
| Target selection | `combat::ChoosePrey` / `combat::Classify`, `policy.riskTolerance` from the profession | Train.cpp:334-345; src/combat/Targeting.cpp |
| Attack action | `ActionCastSpell` / `ActionAttack` | Train.cpp:356-357 |
| Active fight, retreat | ownership TRANSFERS to `DoSurvive` on the first retaliation | Train.cpp:71; src/life/runner/Survive.cpp:130+ |
| Loot | `Runner::ProcessHuntAftermath` — called from Tick BEFORE planning, so it runs under ANY active goal | Survive.cpp:17; Core.cpp:1343 |
| Bank return | `state_.huntReturnPending` -> NeedBank urgency 1.0 -> DoBank clears it | Survive.cpp:57,73; Needs.cpp:915; Bank.cpp:589-590 |
| Bandage errand | `DoReplaceEquipment` bandage branch -> `bandageBuy_` (BuyActivity) -> `VendorErrand` | Gear.cpp:1128-1305; activities/Buy.cpp:21; VendorErrand.cpp:112 |
| Which shop / counter | `req.Sell("healer", Healer)` then `req.Sell("veterinarian", Veterinarian)`, minus `DrainedShelves()` | Gear.cpp:1162-1176; Gear.cpp:29 |
| Cloth fallback | `DoMakeBandages` (cut clothing -> cloth -> bolt -> yarn -> wool -> buy cloth -> sheep) | Cloth.cpp:27-295 |

### What ends a hunt
No kill count and no hunt timer exist. A hunt ends by one of:
- `huntReturnPending`, set after a confirmed loot transfer when the pack is heavy or the coin on hand exceeds `max(goldFloor, gold/10)` (Survive.cpp:54-66, 72-73); DoTrainCombat then refuses to start (Train.cpp:137);
- weight >= `BankWeightLine` (Train.cpp:146; include/uo/life.h:1208);
- HP < `healHpFraction` (Train.cpp:143), or a second, different gate hp < 80% (Train.cpp:430);
- bandages < `bandageLow` (Train.cpp:161-172);
- 3+ hostiles inside `combat::kCrowdRadius` (Train.cpp:286-303);
- planner `Exhausted()` — attempts or `maxGoalMs` (Goals.cpp:554-569).

## 2. COUNTER TABLE — every attempt/retry limit live in this routine

| # | Name | file:line | Value | Scope |
|---|---|---|---|---|
| 1 | `PlannerConfig::maxAttempts` | include/uo/life.h:1459 | 5 | per goal instance |
| 2 | `PlannerConfig::maxGoalMs` (via `TimeLimitFor`) | include/uo/life.h:1457; Goals.cpp:544 | 300000 ms | per goal instance |
| 3 | `PlannerConfig::minCommitMs` | include/uo/life.h:1455 | 20000 ms | transition floor |
| 4 | `PlannerConfig::incumbentBonus` | include/uo/life.h:1452 | 0.15 | hysteresis |
| 5 | `PlannerConfig::preemptScore` | include/uo/life.h:1461; Goals.cpp:635 | 900.0 | emergency bypass |
| 6 | `kNoopSpinLimit` / `kSpinCooldownMs` | Goals.cpp:744-745, 762-766 | 5 zero-progress finishes -> 60 s cooldown | per goal kind |
| 7 | `goal_spinning` log backstop | Core.cpp:1762 | log only | per goal kind |
| 8 | `kMaxBandageShops` | runner/RunnerInternal.h:240 | 2 | counters per REPLACE_EQUIPMENT |
| 9 | `VendorErrandSpec::maxTrips` | include/uo/vendor_errand.h:105 | 3 | per SELLER, per errand |
| 10 | `kMaxScans` | VendorErrand.cpp:35 | 3 | per Find leg |
| 11 | `kMaxChases` | VendorErrand.cpp:38 | 3 | per Approach |
| 12 | `RetryPolicy.maxAttempts` (`open_`) | VendorErrand.cpp:106 | 3 | the shop-open ask |
| 13 | `kVendorActionDeadlineMs` / derived gap | VendorErrand.cpp:45; interaction/handshake.h:97 | 8000 / 9000 ms | ask discipline |
| 14 | `kVerifyWindowMs` | VendorErrand.cpp:42 | 12000 ms | purchase verification |
| 15 | `kShelfRestockMs` (DrainedShelves window) | RunnerInternal.h:236 | 600000 ms | per keeper serial |
| 16 | `kMaxBandageTrips` | RunnerInternal.h:245 | 3 | pasture trips in MAKE_BANDAGES |
| 17 | `kNoBandageCooldownMs` | RunnerInternal.h:246 | 180000 ms | MAKE_BANDAGES cooldown |
| 18 | `kGearCooldownMs` | RunnerInternal.h:431 | 240000 ms | REPLACE_EQUIPMENT rest |
| 19 | `kShortRestMs` | RunnerInternal.h:434 | 30000 ms | retry rest |
| 20 | `kMaxHuntTrips` | Runner.h:972 | 3 | hunting-ground trips |
| 21 | `kHuntStandDownMs` | RunnerInternal.h:444 | 30000 ms | TRAIN_COMBAT cooldown |
| 22 | `kNoHuntingGroundCooldownMs` | RunnerInternal.h:445 | 180000 ms | no ground reachable |
| 23 | `huntLootFailures_ >= 3` | Survive.cpp:74 | 3 | loot attempts per corpse |
| 24 | `kChaseGiveUpMs` / `kFightAssessMs` | Runner.h:406, 413 | 8000 / 20000 ms | chase bound / stalemate |
| 25 | `ActivityTickResult.offerOpen` + reason substrings ("found a", "within reach", "ARRIVED", "the shop is open") | Gear.cpp:1195-1201; Cloth.cpp:203-209 | n/a | converts a poll into `NoteProgress` instead of `NoteAttempt` |

### Overlapping or contradicting pairs

- **(1) vs (10)+(12).** One VendorErrand can need 3 scans per Find leg AND 3 open asks, each of which is `acted`. Six acted ticks exceeds the planner's whole 5-attempt budget inside a single errand. (25) only rescues the case where a keeper is actually found — VendorErrand.cpp:141 returns "found a '%s'", which matches. The empty-room case returns "at the shop, asking who is here (scan N of 3)" (VendorErrand.cpp:152-155), which is `acted` and matches nothing in (25), so it burns attempts at 3 per Find leg.
- **(8) vs (9).** `kMaxBandageShops` counts *goal-level* terminal failures (2). Underneath, the errand already walks up to `maxTrips`(3) x `sellerCount`(2) = 6 travel legs before returning ONE terminal status. The real walking budget is 2 x 6 = 12 legs, not 2 counters.
- **(2) vs the loop.** `maxGoalMs` bounds one goal INSTANCE. Because the need re-fires at an unchanged score, the goal is re-selected the same tick and the 300 s clock restarts. 577 s of bandage upkeep is two-plus instances, not one overrun.
- **(18) vs (21).** REPLACE_EQUIPMENT rests 240 s after a handoff; TRAIN_COMBAT rests 30 s. TRAIN_COMBAT therefore wakes first, hits the floor gate (Train.cpp:161), and hands straight back to a goal that is still cooling.
- **(6) is defeated by any single success.** `noopCompletions_` is reset by `goal_.progress != 0` (Goals.cpp:768). One partial bandage purchase calls `NoteProgress` (Gear.cpp:1237), so the noop-spin cooldown never fires on a shopping loop that occasionally buys 16 bandages.
- **Two HP gates and two weight gates for the same decision.** HP: `healHpFraction` (Train.cpp:143) vs hardcoded 80 (Train.cpp:430) vs `huntHpPct` 80/50 (Needs.cpp:1786-1792) vs `hp < hpMax` -30 (Goals.cpp:424). Weight: `BankWeightLine` (Train.cpp:146) vs hardcoded 0.7 (Train.cpp:437) vs hardcoded 0.7 (Goals.cpp:420).

### Hector: 577 s of bandage upkeep, 0 fights — mechanical order

1. `ResolveConsumableThresholds` runs every planning tick (Core.cpp:1352 -> Needs.cpp:376-401). Fencer is `WantsToHunt`, so `low := kFighterBandageFloor` = 100 (life.h:1191) and `full := low + clamp(spare/10, 20, 100)`, i.e. 200-300 for a rich purse.
2. `obs.bandages (~16) < bandageLow (100)` raises NeedEquipment at urgency 0.5 (Needs.cpp:745-798). Score = 260 x 0.5 = **130.0** (Goals.cpp:200, 345).
3. TRAIN_COMBAT's best available score is NeedTraining 0.45 x 130 = **58.5** (0.65 -> 84.5 only with hostiles already in sight), minus 30 when not at full health (Goals.cpp:424). It cannot beat 130 while the floor is unmet. Satiation (Goals.cpp:444-461) is the only mechanism that ever lets it win.
4. When it does win, DoTrainCombat's own floor gate (Train.cpp:161-172) hands it straight back to REPLACE_EQUIPMENT and cools TRAIN_COMBAT for 30 s. Round trip, no fight.
5. Inside the errand the healer's shelf is `i_bandage {5 20}`, so a Success returns with `nowHeld` ~16-20. Gear.cpp:1240-1256: `nowHeld < bandageFull` -> `NoteDrainedShelf`; the early stop `nowHeld >= bandageLow` is FALSE (16 < 100) -> `return false`, keep shopping. This is the loop that consumed the 577 s.
6. (8) allows one more counter; (15) forbids the drained one for 10 minutes; (9)/(10) then spend acted ticks hunting for a second healer that the atlas does not have (one healer place per town). The planner's (1) reaches 5 first -> `Exhausted` -> `Finish(false, "attempts 5 >= 5")` (Goals.cpp:590-593) -> re-picked at 130 on the same tick, logged `goal_changed=REPLACE_EQUIPMENT from=REPLACE_EQUIPMENT` and counted as `selfSupersessions` (Core.cpp:1373-1381). That is the repeated five-attempt abandonment.
7. Eventually a terminal failure writes the `bandage_counters_empty` event (Gear.cpp:1290-1296) and hands off to MAKE_BANDAGES (Gear.cpp:1303). `BandageCountersEmpty` (Needs.cpp:408-419) then drops the NeedEquipment urgency to 0.10 -> 26.0, which is finally below TRAIN_COMBAT.

Counter firing order: **(10)/(12) -> (1) repeatedly -> (8) -> (15) -> (9) -> (18) -> (17)**. (6) never fires because step 5 called `NoteProgress`.

## 3. GOAL-CHANGE LEAKS

`Core.cpp:1398-1428` is the ONLY reset site, and it runs only when `!sameErrand`
(`sameErrand = wasActive && previous == current`). It clears: `chopTargetValid_`,
`chopCursorPending_`, `travelInFlight_`, `travelAttempts_`, `lastCombatMove_`,
`lastHealPlan_`, `lastRestPlan_`, `lastRecoveryPlan_`, `lastTrainPlan_`,
`lastCraftPlan_`, `lastBandageAcquirePlan_`, `lastPotionAcquirePlan_`,
`lastGarmentAcquirePlan_`, `lastToolAcquirePlanByItem_`; plus `vendorChases_` and
`logsAtGoalStart_` unconditionally.

It clears **no errand state at all**. Persisting across every goal change:

| Field | Runner.h | Consequence |
|---|---|---|
| `bandageBuy_` (BuyActivity, owns a live `VendorErrand`) | 579 | resumes mid-step; see the path below |
| `bandageClothBuy_` | 604 | same, for the cloth route |
| `potionBuy_`, `clothingBuy_`, `weaponBuy_` | 606, 655, 659 | same |
| `bandageShopFails_` | 596 | the 2-counter budget is not per goal instance |
| `bandageTopUp_` | 601 | latched true until `bandages >= bandageFull` (Gear.cpp:990-993) |
| `drainedShelves_` | 589 | deliberate — town knowledge, not errand state |
| `huntTrips_` | 971 | reset only inside DoTrainCombat on arrival/failure |
| `currentFoe_`, `currentFoeName_`, `fightStartedMs_`, `chaseBestDist_` | 404, 439, 414, 407 | intentional — ProcessHuntAftermath needs them |
| `huntLootCorpse_`, `huntLootFailures_`, `huntLootMovePending_` | 886+ | intentional |
| `reagentWants_`, `reagentWantQty_` | 862 | intentional cross-goal request (Train.cpp:118 -> Economy.cpp:2205) |

Neither `BuyActivity::Cancel()` (Buy.cpp:16) nor `VendorErrand::Cancel()`
(vendor_errand.h:151) is called from any goal-change or preemption site; grep
shows `Cancel` invoked only from `Begin()` itself.

### Concrete "errand continues across goal change" path

1. REPLACE_EQUIPMENT ticks `bandageBuy_`. The VendorErrand reaches `Step::Buy`, records `packBefore_`, `goldBefore_`, `verifyDeadlineMs_ = now + kVerifyWindowMs` (VendorErrand.cpp:347) and moves to `Step::Verify`.
2. A hostile appears or HP drops. StayAlive scores 1000 (Goals.cpp:196) >= `preemptScore` 900, so it preempts regardless of `minCommitMs` (Goals.cpp:635). DoSurvive consumes bandages via SurvivalTick and may spend gold.
3. The goal-change reset clears `travelInFlight_` but leaves `bandageBuy_.Running() == true` with `step_ == Verify` and stale expectations.
4. REPLACE_EQUIPMENT is re-selected later. Gear.cpp:1128-1129 admits the branch on `bandageBuy_.Running()` alone, independent of the plan. `Verify` (VendorErrand.cpp:373-400) compares a pack count reduced by healing against `packBefore_`, and a purse reduced by other spending against `goldBefore_` -> `Verdict::Contradicted` -> `Failed("... pack +N, purse +N")` -> Gear.cpp:1268 `NoteDrainedShelf` on a shopkeeper that never refused anything, blacklisting it for 10 minutes.
5. If the interruption lasted longer than 12 s, the resume instead returns `NoProgress` immediately (`obs.nowMs >= verifyDeadlineMs_`), with the same drained-shelf consequence.

This is the same failure family already recorded live for the cloth route on
2026-09-05 (Cloth.cpp:97-103 comment: "gold left the purse and no goods arrived
(pack +0, purse -48)"), caused there by a concurrent cut rather than by a goal
change — which is evidence the seam is real, not hypothetical.

## 4. INTENT

There is **no** "I am hunting; I stepped out to restock; return to hunt" record.
Every tick re-scores from scratch: `AssessNeeds` -> `Planner::Score` ->
`Planner::Select` (Core.cpp:1352-1358). The only objective-like state in the
system:

- `state_.huntReturnPending` — bool, persisted (State.cpp:291, 613): "finish the hunt at the bank";
- `bandageTopUp_` — transient bool: "the restock is open, run to `full`, not to `low`";
- `reagentWants_` / `reagentWantQty_` — the ONE existing cross-goal prerequisite request (Train.cpp:118-123 -> Economy.cpp:2181-2210, cleared Core.cpp:1293-1296). This is the precedent for the fix below;
- planner cooldowns, which express "not this, for now" and never "that, next".

### The restock <-> hunt flip, exactly

`bandages < bandageLow` is read in three places that do not agree on what to do:

1. Needs.cpp:745 raises NeedEquipment at 0.5 -> REPLACE_EQUIPMENT 130.0, unconditional on any hunting intent.
2. Train.cpp:161-172 refuses to hunt below the floor unless BOTH REPLACE_EQUIPMENT and MAKE_BANDAGES are cooling.
3. Needs.cpp:1862-1890 raises NeedMakeBandages at 0.25 + 0.45 x shortfall (up to 145 x 0.70 = 101.5), but only when `!canAffordToBuy` or `countersEmpty` (Needs.cpp:1874-1879).

With floor 100 against a town holding ~16, (1) sits permanently at 130, (2)
bounces every TRAIN_COMBAT win straight back, and (3) is suppressed while the
character has gold. Nothing in the loop can raise the count to 100, so the flip
is stable rather than transient. The wool <-> hunt flip is the same shape with
MAKE_BANDAGES in slot (1).

### Smallest fix: a current-objective / next-prerequisite record

Add to `Runner` (transient is sufficient; move to `LifeState` only if it should
survive a session):

    struct Objective {
        GoalKind objective    = GoalKind::IdleBriefly; // what I am actually doing
        GoalKind prerequisite = GoalKind::IdleBriefly; // what I stepped out to get
        i64      setAtMs   = 0;
        i64      expiresMs = 0;   // hard bound; nothing here is open-ended
    };

- **Written in exactly one place.** `Runner::HandOff(from, to, ...)` already knows both halves. When `FamilyOf(from) == GoalFamily::Work` and `FamilyOf(to) != GoalFamily::Emergency`, set `{objective = from, prerequisite = to, expiresMs = now + 2 * maxGoalMs}`.
- **Read in exactly one place.** `Planner::Score`, as one printed modifier: a bonus to `prerequisite` while it is unmet, and a bonus to `objective` once the prerequisite's need has cleared or its goal has cooled. That is the mechanism that lets a 58.5 TRAIN_COMBAT beat a 130 REPLACE_EQUIPMENT *after the restock has done everything the town allows*. It keeps the existing `ScoredGoal::reasons` contract — the bonus is explainable in the log line.
- **Cleared** when the prerequisite's need vanishes from `AssessNeeds`, when the prerequisite goal cools (its own stand-down means "this is as good as it gets"), or at `expiresMs`.
- **Survive keeps its interrupt for free.** StayAlive scores 1000 >= `preemptScore` 900 and preempts before any modifier is consulted (Goals.cpp:635). The rule must be: never WRITE the record when `FamilyOf(to) == Emergency`, and never READ it to suppress an Emergency. A Survive/Heal detour then leaves the objective intact and the character returns to the yard afterwards, which is exactly the behaviour the flip currently prevents.
- **Second, independent half of the same fix.** Make the errand stop when it has done what the town allows. The existing early stop (Gear.cpp:1247-1256) only fires at `nowHeld >= bandageLow`; with a floor of 100 against a 20-bandage shelf it can never fire. Either cap `bandageFull` (Open defect 1 in docs/SESSION_STATE_2026-09-05.md) or let the stop fire on "no counter left that is not drained".

## 5. DATA vs CODE

### On the Profession record (src/life/Professions.cpp, include/uo/professions.h)

`combatStrategy` (Melee / Ranged / Mage / Tamer / AvoidCombat), `riskTolerance`
(0.20-0.85 across the catalogue), `consumables` (`Bandages()` low 8 / restockTo
30, Professions.cpp:96-103; `HealPotions()`; `CrafterHealPotions()` low 2 /
restockTo 4, Professions.cpp:140-146), `wears` / `maysShield`, `income`
(`Income::Hunt`), `targets` (the weapon-skill tenths that `WantsToHunt` reads),
`goldReserve`, `homeCities`, `unresolvedTenths`.

### `if (isCaster)`-style branches

| file:line | branch |
|---|---|
| Train.cpp:72 | `combatStrategy == Ranged` -> arrows < 20 before hunting |
| Train.cpp:79 | `caster = WantsSpellCombat(...)` |
| Train.cpp:81-131 | caster only: open spellbook, pick attack spell, bank/buy reagents, meditate |
| Train.cpp:187 | `!caster && !obs.weaponEquipped` -> REPLACE_EQUIPMENT |
| Train.cpp:213 | `!caster && !HasBasicArmor` -> UPGRADE_GEAR |
| Train.cpp:356 | `caster ? ActionCastSpell : ActionAttack` |
| Train.cpp:464 | `caster ? SkillTenths(kMagery) : school skill` for the novice/seasoned tier |
| Needs.cpp:663 | `!WantsSpellCombat` in the heal-potion clause |
| Needs.cpp:829 | `Ranged` -> arrow need |
| Needs.cpp:839 | `Mage` -> reagent-from-bank need |
| Needs.cpp:1791, 1831, 2003 | `WantsToHunt \|\| WantsSpellCombat` readiness / income gates |
| Bank.cpp:96 | `Ranged` -> withdraw arrows |
| Bank.cpp:123 | `Mage` -> top the pack up to `kReagentCarry` |
| Bank.cpp:296, 374 | `Ranged` -> never deposit arrows |
| Bank.cpp:545 | `Mage` -> reagents are kit, not surplus |
| Gear.cpp:1915 | metal armour refused for casters |
| Gear.cpp:1990 | caster upgrade tier = best leather |
| include/uo/life.h:1208 | `BankWeightLine`: `WantsToHunt \|\| WantsSpellCombat` |

**Asymmetry worth flagging.** The bandage floor is gated on `WantsToHunt` ONLY
(Needs.cpp:390 and Train.cpp:161). `WantsToHunt` (Identity.cpp:286-296) requires
a weapon-skill target above 500, which the pure mage (Professions.cpp:393,
`p.consumables = {HealPotions(), Food()}`) does not have. So for a hunting mage:
no 100-bandage floor; `WantsConsumable(cfg, "bandage")` is false, so
NeedEquipment(bandages) never fires (Needs.cpp:745) and `wantsBandages` is false
(Gear.cpp:978) — yet Train.cpp:150-153 still hands off to REPLACE_EQUIPMENT when
the mage has zero bandages, zero potions and no heal spell, into a goal whose
bandage branch is closed to it. Only the potion branch (Gear.cpp:1060+) can
satisfy that handoff. Warlock (Professions.cpp:963) does carry `Bandages()` and
is Mage strategy, so the two mage-family professions behave differently here.

### Thresholds duplicated across Needs / execution / banking

| Resource | Copies |
|---|---|
| bandages | `NeedConfig` defaults 8 / 30 (life.h:1133-1134); `Bandages()` low 8 / restockTo 30 (Professions.cpp:100-101); `kFighterBandageFloor` 100 (life.h:1191); dynamic `full = low + clamp(spare/10, 20, 100)` (Needs.cpp:392-396); `kBandagesWanted` 60 (RunnerInternal.h:231); `kBandagesBuyable` 200 (Needs.cpp:200) |
| hunt weight | `huntWeightFrac` 0.70 (life.h:1143) via `BankWeightLine` (Train.cpp:146; Survive.cpp:54, 72; Bank.cpp:291) vs hardcoded 0.7 (Train.cpp:437) vs hardcoded 0.7 (Goals.cpp:420) |
| hunt HP | `healHpFraction` (Train.cpp:143) vs hardcoded 80 (Train.cpp:430) vs `huntHpPct` 80/50 (Needs.cpp:1786-1792) vs `hp < hpMax` -30 (Goals.cpp:424) |
| reagents | `kReagentCarry` 50 (life.h:890); pack trigger `kReagentCarry / 5` = 10 (Needs.cpp:843); `PlanReagentBuy(0, 20, ...)` with `expectedCasts` hardcoded 20 (Train.cpp:117) |
| cloth | `kClothMaxPrice` 6 (RunnerInternal.h:244); the cloth order is `want - obs.bandages` (Cloth.cpp:188), where `want` is `bandageFull` — so the 200-300 figure propagates into the cloth route as well |
