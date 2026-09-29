---
name: review-2026-09-05-open-slices
description: Open architecture slices after the 2026-09-05 review, in order — errand-owned attempts, Objective struct + Bank keep, ContainerSync, thresholds to profession table
metadata:
  type: project
---

Sequence agreed in docs/ARCHITECTURE_REVIEW_2026-09-05.md (steps 2-5 open):
2. Attempt budget belongs to the errand; planner counts failed LEGS not acted ticks (maxAttempts 5
   < VendorErrand's 6-tick budget). Delete duplicate forge counters (4 of them, only
   smeltApproaches_>=2 honours owner rule), bankDepositTries_=5 unreachable vs bankItemMoveFails_=3.
3. Objective{goal,item,prereq,prereqItem,prereqQty,setMs}: HandOff sole writer (Work->non-Emergency),
   readers Planner::Score, Goals satiation skip, Bank keep prereqQty (mirror WoolChainWorkInProgress),
   Needs unsoldStock skip. Bank keep is load-bearing: no craft-input withdrawal exists, smith with
   550 banked ingots reads "stocked" and "no inputs" same tick. Also bandage floor 100 vs shelf {5 20}
   makes REPLACE_EQUIPMENT(130) permanently beat TRAIN_COMBAT(58.5): errand must stop on "no
   undrained counter left" + cap bandageFull.
4. ContainerSync{Unknown,OpenPending,Known} beside containerItems_; Sphere sends no 0x3C for empty
   containers; delete spellbookOpened_/practiceRecheckedBook_. §18 of BOT_ARCHITECTURE is stale:
   buy/sell/craft/trade already verify world deltas.
5. Thresholds: six bandage constants, weight line x3, HP line x4, ingot keep x3; Needs.cpp:1648
   hardcodes kBlacksmithing.

**Why:** owner asked for fewer coordinated edits per archetype and second-scale repro.
**How to apply:** do these in order, each with a harness scenario first; don't reopen the ranking
without new live evidence.

Quick wins landed 09-05 evening (bandage stand-down on drained counters, scan leg not an attempt,
CraftInputReserve on loaded deposits, Train literals). Still open from that batch: bankDepositTries_
never reset on success (one line + Bank harness step); Cloth.cpp legLanded copy -> ClassifyErrandLeg;
item 6 needs a live miner_smith bank visit; Hector TRAIN_COMBAT spins on "no path to (535,871)".
Lesson: two review claims were wrong (item 1 reachability, item 7 mage gate) — implementer skipped
them correctly; a stacked uncommitted tree makes QA attribute earlier-layer diffs to the new batch.
