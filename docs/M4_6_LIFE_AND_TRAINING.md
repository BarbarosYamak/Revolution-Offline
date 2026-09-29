# M4.6: The life continues, and training aims at the build

Date: 2026-09-29. **STATUS: BUILT AND UNIT-TESTED, NOT YET PROVEN LIVE.**

This closes the two gaps left at the end of M4.5.

## 1. The JS bots now read and write their persistent life

Before this change, the C++ client saved the character record, but the JS
scripts never saw it. What the scripts learned was lost at logout.

**New bindings (`Life`):**
* `Life.record` returns the saved character:
  * name, archetype, sessions
  * `targetBuild`, target STR/DEX/INT
  * `objective`, number of deaths, last logout point
* `Life.memory` / `Life.setMemory(json)` read and write an opaque blob the
  script owns. It is stored in the `.life` file as a new `memory` line, capped
  at 256 KB. Older builds skip the unknown line.
* `Life.setObjective(kind, target)` and `Life.save()`.

**`lib/life.js` (LifeSkill)**, used by every engine:
* **Saves and restores** what the bot learned:
  * the danger map, exhausted spots and hunted timer;
  * market knowledge (buyers, prices, refusals) and ledger totals;
  * crafters heard advertising;
  * paid orders and the next order number (unpaid quotes are not carried
    over);
  * the pet, gear already ordered, and a pending order.
* **When memory is written:** every 30 s. The C++ side writes the file every
  minute, on death and on logout.
* **Objective:** the current behaviour becomes the saved objective, so a
  session that ends mid-corpse-run starts the next one knowing it. For
  example, resurrect → `recover_corpse`, fight → `survive`, bank → `unload`,
  patrol/craft → `train`.
* **Warnings:**
  * a character running a different archetype than it was created as (its
    targets come from the record);
  * a previous session that logged out somewhere unsafe.
* **Safe failures:** unreadable memory starts fresh instead of crashing.
  Without `--life-dir`, everything is a no-op.

JS timestamps are `Date.now()`, which is wall clock, so unlike the C++ steady
clock they need no conversion across a logout.

## 2. Training toward the target build

**New binding:** `Player.setSkillLock(index, 'up'|'down'|'locked')` and
`Player.skillLock(index)`. These are the skill-gump arrows, sent as 0x3A from
the client (Source-X `PacketSkillLockChange`). **UNVERIFIED live.** The server's
next skill update reports the lock it actually holds.

**`lib/training.js` (TrainingSkill):**

| Skill state | Lock |
|---|---|
| in the build, below target | **up** |
| in the build, at or past target | **locked** |
| not in the build | **down** |

* On a capped shard the server takes points from "down" skills as build skills
  rise. That is how a player shaped a 700 build.
* Our runtime caps at 1000.0, so the bot watches the 700.0 sum itself and
  reports `OVER CAP`.
* Targets come from the saved life, or from the archetype table when there is
  no life.
* The plan is applied at startup and every 5 minutes. Locks that are already
  right are not re-sent.
* **Behaviour follows the plan:**
  * A mage whose Magery is at target stops self-casting to train, and
    meditates instead if Meditation is short.
  * A crafter at target keeps crafting, now for the market, and says so once.

## 3. Verified

* `js_bot_smoke.js`: **342 checks, 0 failures.** Covers:
  * the three lock rules, the focus order, over-cap, and a finished build with
    its unallocated remainder;
  * locks sent only when they differ;
  * the mage switching to meditation at target;
  * targets from the saved life;
  * a full memory round-trip through a fresh bot: danger, empty spots, market,
    ledger, crafters, pet, gear;
  * the objective mapping, and no rewrite when it hasn't changed;
  * crafter paid orders resuming mid-order, with unpaid quotes dropped;
  * unreadable memory, and running without a life.
* `m4_lifecycle`: **146 checks.** The memory line survives byte for byte, tabs
  and newlines included, and a blob over the cap is refused at load.
* Every other suite unchanged and passing. The client files compile under
  MinGW.

**Nothing was run against Sphere.** M4's exit test is still one character
surviving several consecutive sessions, with its life, memory and locks
carried across.
