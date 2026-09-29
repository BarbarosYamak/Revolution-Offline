# M4.2: Combat retune, and a graveyard patrol

Date: 2026-09-29. **STATUS: BUILT AND UNIT-TESTED, NOT YET PROVEN LIVE.**

The owner reported three problems: bots "always ask for bandages", "disengage
way too quickly", and "do not wander around the cemetery". All three were real.
Each one traces to specific code.

---

## 1. "Always ask for bandages"

| Cause | Where | Fix |
|---|---|---|
| Bandaged mid-melee whenever HP dropped below **80%**, every 6 s. The fight was mostly double-clicking bandages, the stock ran out, and the bot walked to the healer to buy more. Those were the repeated vendor ("trading") requests | `lumberjack.js` `HEAL_HP_FRAC` | Now below **50%** only, at most every **10 s**, and never against a foe that is nearly dead |
| `rest()` bandaged until HP was **exactly** full, so one point of damage cost a bandage | `lib/survival.js` | Now rests up to `REST_UNTIL_FRAC` = **90%** |
| C++ survival: after leaving war mode, a bot that was **still being hit** counted as "out of contact" and started a 3-second bandage. The next hit wasted it, and the loop repeated | `SurvivalTick` / `combat_policy` | New `underAttack` signal: our own HP dropped within 3 s. Being hit counts as contact whether or not war mode is on |

## 2. "Disengage way too quickly"

| Cause | Where | Fix |
|---|---|---|
| Fled after **4 s** if it had lost 10% HP and the foe's bar had moved less than 2%. Four seconds is about two swings, and a new character misses most of them | `assessFight` | Assesses only after **10 s**, and only once **20%** has been lost |
| The foe's health bar was requested **once**. A stale reading looks exactly like a foe we cannot hurt | `assessFight` | Re-requests the bar every 5 s (0x34, an ordinary client request) |
| The "losing race" check fled on a **tie** | `assessFight` | Flees only if clearly losing (`LOSE_MARGIN` 0.75) |
| Walked away from foes on their last legs | JS and C++ | **Finish rule:** a foe at ≤25% is fought on, unless we are below the flee line |
| JS hard floor at 40%; C++ disengaged at 35% and drank potions at 60% | JS / `combat_policy.h` | Now **30% / 30% / 50%**. Still DERIVED tuning, not Revolution-documented |
| **A C++ disengage only left war mode.** The bot stood still, out of war mode and no longer hitting back, which is worse than fighting on | `SurvivalTick` | A disengage now also **walks** 12 tiles directly away from the attacker (`SurvivalRetreat`, ordinary `ActionGoto`) |
| **Any HP drop while walking aborted the walk.** That included the escape itself: the bot decided to flee, the next hit cancelled the walk, and the bot stood still | `OnMobileHp` → `BotInterruptForThreat` | Interrupts once per 10 s episode, and never during a survival retreat. The first hit still halts an oblivious journey |

## 3. "Do not wander around the cemetery"

Nothing did. The only autonomous script was the lumberjack, which fights only
what attacks it in the forest. Every cemetery visit so far was a one-shot M3.9
scenario.

New: **`scripts/js/graveyard.js`**, a GraveyardHunter for the Britain
Graveyard. Priority order: resurrect > fight > bank > eat > recover > patrol.

* **Patrol:** a random tile inside the graveyard's AREADEF rectangles (from
  `data/revolution_atlas.txt`), a pause of 1.5–4 s (not M3.9's fatal 60 s),
  then a look for undead within 10 tiles.
* **Hunts only wild hostiles:** notoriety 3–6 **and** a graveyard body
  (zombie, skeleton, bone mage, lich, ghost). It never targets a blue or green
  human. It starts a fight only at ≥80% HP, and still defends itself below that.
* **Recover:** when hurt or after fleeing, it leaves the graveyard to bandage,
  buys bandages from the Britain healer when out, and waits for natural
  regeneration if it has nothing to heal with.
* It remembers where it fled from for 2 minutes and does not path straight back
  there.

**UNVERIFIED:** the rest spot (1398,1480, just east of the graveyard's east edge
at x=1390) has not been checked for walkability. If the log shows `could not
reach the rest spot`, move it.

## 4. A trap found on the way

The combat loop moved out of `lumberjack.js` into **`lib/combat.js`**
(`CombatSkill`), so the lumberjack and the graveyard hunter share one fixed
copy. The smoke test then caught this: mixins are applied with
`Object.assign(Bot.prototype, …)`, which **overwrites** a class method of the
same name. The mixin's empty `onFlee()` placeholder had replaced each bot's own
`onFlee()`, so the lumberjack would never have rotated forests after fleeing.
The mixin no longer defines anything a bot is expected to provide, and the rule
is written at the top of `combat.js`.

## 5. Verified

| Test | Result |
|---|---|
| `m38_closure` (combat policy + new cases: 36% keeps fighting, being hit out of war mode is contact, finish rule, flee line wins) | 293 checks, 0 failures |
| `tests/js_bot_smoke.js`: new, run under Node with stubbed bindings, registered with ctest when Node is present | 13 checks, 0 failures |
| `Client.cpp`, `ClientTravel.cpp` | MinGW syntax check, no new warnings |

**Nothing was run against Sphere.** Live proof:

1. Run `graveyard.js` for 30 minutes and confirm kills, no death loop and
   bandage use well below one per fight.
2. Run the lumberjack against a wolf and confirm it no longer flees inside the
   first 10 seconds.
