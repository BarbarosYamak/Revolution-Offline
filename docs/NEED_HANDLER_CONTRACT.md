# The need/handler contract

Status 2026-09-06. Base 11f4c80. Written after the first live validation wave
(artifacts/validation_wave_2026-09-06.md) produced seven separate defects with
one shape, and each was patched on its own.

NOTE ON LOCATION: this file was written inside the agent worktree
(bot/uo-client/docs/) so it merges with the code it describes. Every other
project doc lives in the repo-root `docs/`; move it there on merge if that is
the convention the lead wants to keep.

## 1. The defect this document ends

`AssessNeeds` (src/life/Needs.cpp) decides WHAT is missing. `Runner::Do*`
(src/life/runner/*.cpp, dispatched by the `switch` in Core.cpp `RunGoal`)
decides whether anything can be DONE about it. Those two ask different
questions of different state, and nothing made them agree.

The failure mode is always the same three lines in a log:

    goal_changed=<GOAL> ... score=<high>
    handoff=<GOAL>-><OTHER> reason="<a fact the need never saw>"
    (repeat every cooldown, until goal_spinning)

Seven instances, all from the 2026-09-06 wave:

| # | Need (score) | Handler's refusal, in its own words | Where |
|---|---|---|---|
| D7 | NeedSupplies 0.95 x 140 = 133, 5 picks | "another profession makes this, not a shopkeeper" | Economy.cpp:2237 |
| D10 | NeedEquipment(potions) 0.5 x 260 = 130, re-picked each cooldown | shelf empty, `potion_counters_empty` | Gear.cpp |
| D4 | NeedWoolIncome 0.30 x 140 = 42, 2 picks | "no pasture within 400 tiles of home" | Cloth.cpp:984 |
| — | NeedMount 0.80 x 255 = 204, every session | "no way to an animal trainer" | Gear.cpp:635 |
| D5 | NeedTraining(hunt) 0.45 x 130 = 58.5 at mana 1 | "no castable attack spell" / readiness gates | Train.cpp:134-204 |
| — | RecoverCorpse 0.75 x 950 = 712, handed off in 3 s | too hurt to walk back; heal first | Survive.cpp:1012 |
| D13 | Heal 700, abandoned "attempts 5 >= 5" at 6/50 HP | `HealStep::Stuck` / `Rest` never notes progress | Survive.cpp:922 |

The planner's three backstops -- `Planner::Cooldown`, the 5-attempt/300 s
`Exhausted` check, and the `goal_spinning` detector (Core.cpp
`LogSpinIfDetected`) -- only bound the damage. They cannot fix it, because none
of them tells the NEED anything.

## 2. The contract

> **A need may score only if its handler would act on it.**
>
> There is exactly one place that question is asked -- `life::CanAct` -- and
> exactly two ways a handler may answer it:
>
> **Arm A (pure).** The precondition is computable from
> `(NeedKind, subject, Memory, Observation, NeedConfig)`. It lives in
> `CanAct` and is called by BOTH the need site in `AssessNeeds` and the
> handler's first tick. One copy of the rule, two callers.
>
> **Arm B (observed).** The precondition needs the `Client`, a runner-private
> table or a travel result, so the need model cannot compute it. Then the
> handler's refusal WRITES a need-level block carrying the same words, and
> `CanAct` reads it back. One mechanism, not two.
>
> A refusal that does neither is a contract violation. It is the bug above.

`CanAct` returns `{ok, why}`. `why` is the handler's own sentence, so the
existing telemetry is unchanged:

    BLOCKED_NEED <GOAL>: <reason> (<evidence>)

emitted from Core.cpp:2019 through `Planner::Score` (Goals.cpp:338), which
already turns `Need::blocked` into an infeasible `ScoredGoal`.
`tools/grade_life.py` and the wiki table stay valid: this contract adds no new
log line, it only makes more needs reach the line that already exists.

### 2.1 How the existing mechanisms fit

* **`Need::blocked`** stays the single expression of "scores, but nothing
  legitimate satisfies it". `CanAct` is how a need site decides it.
* **`Runner::HandOff(from, to, restMs, why, nowMs)`** (Core.cpp:1929) is
  unchanged and still the only legal way a handler ends its turn in favour of
  another goal. It stays ADVISORY (`to` is logged, never dispatched). What
  changes is that a hand-off may no longer be the ONLY record of a refusal:
  if the reason will still be true at the next assessment, the same site must
  also block the need.
* **`planner_.Cooldown(kind, untilMs, why)`** now records the reason. A
  cooldown is a per-process brake; the block is the durable, need-visible
  fact. The `COOLING` branch in `Planner::Score` is untouched and still wins
  over the blocked branch, because it is the more recent fact.
* **`LeaveGoal` (Core.cpp:1813)** is untouched. It runs on a real change of
  kind and cleans the departing goal's errands, actions and sentinels; the
  contract adds no state that outlives a goal inside the Runner. Blocks live
  in `Memory`, which `LeaveGoal` does not own.
* **`Exhausted` / `goal_spinning`** remain backstops. Under the contract they
  should fire less; if one fires, the pair it names is a contract violation.
* **The life harness (tests/life_harness.cpp)** drives the same
  `AssessNeeds -> Score -> RunGoal` path, so a migrated pair is testable
  offline.

### 2.2 The block record

Arm B writes a `LifeEvent` through `life::NoteNeedBlocked`:

    kind   = "need_blocked"
    detail = need=<NeedKindName> session=<N> scope=<session|window> reason=<text>

`life::NeedBlockActive` reads it. Two scopes, both already in use before this
document existed:

`NeedBlockActive` also drops a block whose FACT has since been undone, and that
test lives in the reader rather than in `CanAct` because `AssessNeeds` reads
blocks through the reader: a staleness rule kept one level up is a rule the
need site never runs. Today there is exactly one such tell -- a bandage or a
potion in the pack ends HEAL's "nothing to heal with and nothing on the way".
The other kinds have no cheap tell (a pasture does not appear in the pack) and
run their scope out, which is the point of them.

* `Session` -- true for the rest of this session (`NeedConfig::sessionIndex`;
  the only durable clock a need has -- ms clocks are per-process, see
  bot-brain memory `ms-stand-downs-die-with-the-process`).
* `Window` -- true for `kBlockWindowMs` from `atMs`, for facts the world
  undoes on its own (a drained shop shelf, a flock that will wander back).

Three pre-existing readers are the same mechanism written three times. They are
CONFORMANT and are deliberately left alone rather than migrated, because their
event kinds are already persisted in `bot_data/*/memory` on disk and two of
them DAMP rather than block (policy, not plumbing):

| Event kind | Written by | Read by | Effect |
|---|---|---|---|
| `bandage_counters_empty` | Gear.cpp | Needs.cpp `BandageCountersEmpty` | damps to 0.10 |
| `potion_counters_empty` | Gear.cpp | Needs.cpp `PotionCountersEmpty` | damps to 0.10 |
| `mount_unavailable` | Core.cpp (on BUY_MOUNT cooling) | Needs.cpp | blocks 4 sessions |

## 3. Audit: every NeedKind -> GoalKind pair

Weights from `kGoals`, src/life/Goals.cpp:196-300. "Handler-only
preconditions" counted from the `HandOff|goal_blocked=|goal_failed=|goal_stuck=`
sites in each handler. Status: **A** migrated to arm A, **B** migrated to arm B,
**C** conformant already, **open** = still disagrees.

| Need | Goal (weight) | Handler | Need checks | Handler-only preconditions | Status |
|---|---|---|---|---|---|
| StayAlive | Survive 1000 | Survive.cpp:159 | dead / disengage / fight started | none that outlive a tick | C |
| RecoverCorpse | RecoverCorpse 950 | Survive.cpp:931 | corpse known, attempts < 3 | HP below the return line; threatened; corpse decayed | **A** (HP, threat), in `DecideRecovery`'s ORDER -- see 3.1; corpse-gone and abandonment stay handler facts, since they clear the death record and the need dies with it |
| Heal | Heal 700 | Survive.cpp:689 | HP < healHpFraction | nothing to heal with AND nothing on the way (`HealStep::Stuck`) | **B** |
| NeedTool | GetTool 520 | Gear.cpp:193 | tool absent from pack/hands | tool is in the BANK; no shop reachable; vendor list lacks it; too poor | open |
| NeedEquipment | ReplaceEquipment 260 | Gear.cpp:868 | consumable/armour thresholds, drained shelves | vendor policy refusal; carry weight; no shop reachable | C (shelves) / open (rest) |
| NeedBank | Bank 240 | Bank.cpp:58 | load, loot, reagent and gold lines | no bank reachable | open |
| NeedFood | GetFood 250 | Gear.cpp:1674 | hunger | shop stocks nothing edible | open |
| NeedGold | EarnGold 150 | Economy.cpp:63 | surplus + a known buyer (route lookup) | tried every trade that buys it | C |
| NeedLogs | GatherLogs 130 | Gather.cpp:116 | stock line, can work | no axe to arm; no tree reachable | open |
| NeedTraining | TrainCombat 130 | Train.cpp:70 | skill gap, danger, mana for one opening cast (D5) | hunt-return pending; HP; carry weight; arrows; bandage floor; no castable spell; no weapon; no hunting ground | **A** (the first three); the rest need the Client or a region read |
| NeedTravel | TravelToRequiredPlace 90 | Core.cpp:2133 | no work possible here | three trips did not arrive | open |
| NeedSkillTraining | TrainAtNpc 110 | Train.cpp:924 | buyable skill below the NPC ceiling, gold | trainer unreachable after 3 trips; no Sphere key for the skill; the NPC refused | open (see revolution-god memory `one-npc-is-not-the-trade` before widening a refusal here) |
| NeedTrade | TradeWithPlayer 145 | Economy.cpp:966 | surplus, marketQuiet, session budget, cooldown | no banker; stock still in the bank; market unreachable | C |
| NeedCatch | Fish 130 | Gather.cpp:1028 | profession fishes | no pole; no dock reachable | open |
| NeedSupplies | BuySupplies 140 | Economy.cpp:2099 | vendor ruling, capital, route==PlayerMarket (D7) | route==SelfProduce; no trade sells it; vendor unreachable twice; list lacks the item | **A** (route + supplier); reach failures stay a cooldown |
| NeedCraft | Craft 130 | Craft.cpp:44 | inputs held, recipe known | no fire; no smith hammer; no menu path; cursor refused | open |
| NeedPractice | PracticeSkill 120 | Train.cpp:2180 | skill gap, reagents | nothing here to practise on | open |
| NeedSpells | FillSpellbook 110 | Train.cpp:1972 | book read, budget, scroll rows | shop does not stock the circle; scribe unreachable | open |
| NeedMakeBandages | MakeBandages 145 | Cloth.cpp:27 | poor branch, cloth held | no scissors; no sheep after N trips | open |
| NeedGear | UpgradeGear 100 | Gear.cpp:2063 | a better tier exists for the Wear class | nothing offered; weight | open |
| NeedOre | Mine 130 | Gather.cpp:1542 | stock line | no pickaxe; mine unreachable; no rock in reach | open |
| NeedSmelt | Smelt 140 | Gather.cpp:585 | ore in pack | no forge reachable; the forge refused | open |
| NeedPet | TameAnimal 115 | Tame.cpp:38 | tamer with no pet | nothing tamable; the animal resisted N times | open |
| NeedCloth | MakeCloth 135 | Cloth.cpp:438 | `noSellerFor` asked and declined | no pasture table; no pasture near home; no sheep after N trips | **B** (shares the writer with wool) |
| NeedMount | BuyMount 255 | Gear.cpp:556 | purse, mounted, dismountedForWork, `mountAskOnCooldown`, `mount_unavailable` | every refusal cools the goal, and Core.cpp turns cooling into the durable event | C |
| NeedWoolIncome | HarvestWool 140 | Cloth.cpp:438 | sells wool, load, hurt, funded (D4) | no pasture table; no pasture within `kMaxPastureTilesFromHome` of home | **B** |
| NeedStrength | StatFarm 118 | Train.cpp:721 | reachable STR ceiling over target | nothing here to spar with | open |

### 3.1 An arm-A gate copies the handler's ORDER, not just its rules

`DecideRecovery` (src/life/activities/RecoveryPlan.cpp) tests `threatened`,
then the spent-attempts abandonment, and only THEN the health line. Written
without that order the gate blocked a character raised at a fifth of its
health with its trips already spent -- telling it to heal first when the
handler's next step was `Abandon`, which is the step that CLEARS THE DEATH
RECORD. Blocking there meant the record was never cleared and the corpse was
carried for the rest of the character's life. `TestRecoveryForEveryArchetype`
in tests/m4_life.cpp caught this for all 17 archetypes.

The rule this leaves: **a gate may block only on the handler steps that are
refusals. A terminal step -- abandon, give up, mark it gone -- is WORK, and
blocking the need starves the handler of the turn it needs to close the
errand** (bot-brain memory `an-errands-exit-cannot-live-in-the-errand`).
`TestContractCorpseGateMatchesTheHandler` asserts this over the 72-case
matrix of (HP x threatened x attempts x distance) by running `DecideRecovery`
itself, not a copy of its rules.

## 3.2 Tests

`tests/m4_life.cpp` (target links `src/life/NeedGate.cpp` and
`src/life/activities/RecoveryPlan.cpp`):

| Test | Pair | Asserts |
|---|---|---|
| `TestContractCorpseGateMatchesTheHandler` | RecoverCorpse (A) | over 72 observations, `Need::blocked == (DecideRecovery(...).step == Recover)`; plus the hurt and trips-spent ends named |
| `TestContractArmAMatchesTheErrand` | NeedSupplies (A), NeedTraining/hunt (A) | over every profession's `consumes`, `CanAct` refuses exactly the inputs `market::RouteForInput` does not route to `NpcVendor`, and always with a reason; `huntReturnPending` blocks the need with the gate's own sentence |
| `TestContractArmBSilencesTheNeed` | Heal (B), NeedWoolIncome (B) | a written block silences the need and its `reason` IS the handler's text; a bandage reopens HEAL; a `Window` block ages out at `kBlockWindowMs`; a `Session` block ends at the next `sessionIndex` |

Not migrated in this pass, in the order they should be taken (each is one
handler fact the need cannot see, and each has a live symptom in the wave
ledger or an earlier artifact): NeedTool (tool in the bank), NeedBank (no bank
reachable), NeedSmelt (no forge reachable), NeedOre / NeedCatch (no site
reachable), NeedSkillTraining (trainer unreachable), NeedCraft (no fire / no
hammer). All six are "the place is not reachable", which suggests one shared
arm-B `place_unreachable` block rather than six more predicates.

## 4. What arm A must never become

`CanAct` is a legality question, not a planner. It must not read hidden server
state, must not rank goals, and must not encode anything the character could
not observe. Every rule in it was already being applied by a handler on the
same observation -- this pass MOVED rules, it did not add any. A rule that only
the simulator can evaluate belongs in arm B, where a real observation (a failed
walk, an empty shelf, an absent shopkeeper) is what writes it.
