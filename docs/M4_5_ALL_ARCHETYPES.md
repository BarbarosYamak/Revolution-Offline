# M4.5: Every fighter, crafter and gatherer type

Date: 2026-09-29. **STATUS: BUILT AND UNIT-TESTED, NOT YET PROVEN LIVE.**

The owner asked for every type of fighter and crafter, not a single example:
"alchemist, scribe, bowcraft etc". Each type was a hand-written script before
this. Now there is **one table** and **three engines**, and every archetype
has its own launcher.

## 1. The table

`scripts/js/lib/archetypes.js` → exported to **`data/revolution_archetypes.tsv`**,
which the C++ persistent record reads (`life::LoadArchetypes`). The smoke test
fails if the two drift apart.

| Archetype | Kind | Style / trade | Build source | Evidence |
|---|---|---|---|---|
| swordsman | fighter | melee | PW-01 | HISTORICAL_EXACT |
| archer | fighter | ranged | PW-02 | HISTORICAL_NEAR_EXACT |
| fencer | fighter | melee | PW-03 | HISTORICAL_FAMILY |
| macer | fighter | melee | PW-04 | HISTORICAL_FAMILY |
| warlock | fighter | warlock (sword) | WL-01 | HISTORICAL_EXACT |
| fencing_warlock | fighter | warlock (fencing) | WL-03 | HISTORICAL_EXACT |
| double_warlock | fighter | warlock (sword+mace) | WL-07 | HISTORICAL_EXACT |
| pure_mage | fighter | mage | PM-01 | HISTORICAL_EXACT |
| tamer | fighter | tamer | TM-02 | REVOLUTION_DERIVED |
| pk | fighter | pk | PK-02 | HISTORICAL_FAMILY |
| blacksmith | crafter | blacksmith | CR-01 | REVOLUTION_DERIVED |
| tinker | crafter | tinker | CR-06 | REVOLUTION_DERIVED |
| tailor | crafter | tailor | CR-04 | HISTORICAL_FAMILY |
| carpenter | crafter | carpenter | CR-08 | REVOLUTION_DERIVED |
| bowyer | crafter | bowcraft/fletching | CR-07 | REVOLUTION_DERIVED |
| alchemist | crafter | alchemy | CR-03 | REVOLUTION_DERIVED |
| scribe | crafter | inscription | CR-05 | HISTORICAL_FAMILY |
| cook | crafter | cooking | — | **UNSOURCED** (owner request; no compendium entry) |
| lumberjack | gatherer | lumber | M4 plan | REVOLUTION_DERIVED |
| miner | gatherer | mining + smelting | CR-01 | REVOLUTION_DERIVED |
| fisher | gatherer | fishing | FI-01 | HISTORICAL_FAMILY |

Rules for the table:
* **FAMILY/DERIVED builds name only their core skills.** Those go to 100, and
  the rest of the 700 stays UNALLOCATED rather than filled with a guess. Every
  HISTORICAL_EXACT build spends exactly 700, and a test checks that.
* **Stats are always one of the ten attested 225-point splits.** None of them
  comes from a crafter build; that is written in the file, not hidden.
* **Every row is validated** in JS and C++ against the 700 skill cap, the
  100-per-skill cap, the nine inactive Revolution skills, and 225 total /
  100 per stat. A bad row fails the whole load.
* **L4:** a warlock (Magery above 40) never wields a poisoned blade. It
  poisons with the **spell**, as the rule intends.

## 2. Fighters: `lib/fighter.js` (FighterBot)

| Style | In a fight | Out of a fight |
|---|---|---|
| melee | swing, bandage when hurt, finish the foe, flee on the personal floor | patrol its hunting ground |
| ranged | keep 4 tiles away; switches to its sword when out of arrows | restock arrows at the bowyer |
| mage | stays 6 tiles away; attack spell by Magery (Magic Arrow at 0 up to Flamestrike at GM); heals by spell first; wrestles when out of mana | trains by casting Greater Heal on itself; meditates (the proven `m36_magery.txt` flow); restocks reagents |
| warlock | melee, opens with the Poison spell once per foe, heals by spell | as mage |
| tamer | tames a pet (the server answers "no chance", and that is remembered); "all kill" plus the target; stays back | looks for something to tame |
| pk | hunts **blue players in the wild, only if `ALLOW_PVP` is set**; never near a town bank; never banks (a murderer meets the guards) | patrol |

All fighters share:
* **Hunting ground by level.** The level (1–4) comes from the primary skill.
  Grounds are wilds, then Yew graveyard, then Britain, Vesper and Jhelom
  graveyards, then Cove. The bot picks one near home, weighted by the danger
  memory.
* **Everything built so far:** looting, selling, banking, the budget,
  low-mark restock, danger and hunted memory, the schedule, the persona.
* **Gear from crafters it has heard.** Crafters announce "<Name> the
  blacksmith takes orders ..."; fighters remember who they heard and where,
  which is the only way they learn a crafter exists. A fighter orders its
  style's gear and pays and collects by secure trade:
  * melee: a ringmail set from a blacksmith
  * archer: a bow from a bowyer
  * mage: a cloth outfit from a tailor
  * tamer: a leather set from a tailor

**DERIVED:** spell choice follows circle availability only. Revolution's
Eval/Magery damage is UNKNOWN, as CLAUDE.md says. Level bands, ranges and mana
thresholds are bot tuning.

## 3. Crafters: `lib/crafterbot.js` (CrafterBot)

| Craft | Flow | Evidence | Material and policy |
|---|---|---|---|
| blacksmith | hammer → target ingots → menu, at a forge and anvil | **PROVEN** `m37_slice_b_minoc.txt` | buys ingots (allowed); can also mine and smelt |
| tinker | tools → menu | **PROVEN** `m38_tinker_craft.txt` | buys ingots |
| tailor | cuts bolts into cloth with scissors, kit → target cloth → menu | **PROVEN** `m37_slice_a_sew/finish.txt` | buys cloth **bolts** (cloth itself is not sold by NPCs) |
| carpenter | saw → menu | **PROVEN** `m37_slice_d_carpenter.txt` | **chops its own logs** (logs cannot be bought) |
| bowyer | dagger → target logs → menu | UNVERIFIED | chops its own logs |
| alchemist | mortar → target reagent → menu | UNVERIFIED | buys reagents; **BLOCKED on empty bottles** (no NPC may sell them under the policy) |
| scribe | pen → menu | UNVERIFIED | **BLOCKED on blank scrolls** (policy UNKNOWN, so refused) |
| cook | raw food → target an oven or fire | UNVERIFIED | catches its own fish |

Every crafter:
1. Reads its **live** craft menu.
2. Trains on what the menu offers and vendors buy.
3. Gets its material the legal way: buy, buy and process, or gather.
4. Advertises itself every 4 minutes.
5. Takes orders for one piece, a quantity, or a set. Each craft has its own
   sets, for example blacksmith ringmail/chainmail/platemail, tailor cloth
   outfit/leather, bowyer archer kit, alchemist pvp kit, scribe travel kit.
6. Is paid and delivers by secure trade.

**An UNVERIFIED flow that opens no menu is logged, and the crafter backs off**
(5, 10, … up to 60 minutes) instead of looping. That log line is the request
for a live check.

## 4. Gatherers: `lib/gathering.js`

* **Logs:** proven (hatchet on a tree).
* **Ore:** proven at the Minoc spot (stand 2564,499, target 2563,499,20).
* **Smelting:** proven (the ore used at the Minoc forge).
* **Fish:** UNVERIFIED.
* The miner and fisher run on `GathererBot`. The lumberjack keeps its own
  proven script.

## 5. Running one

```text
uo_client --life-dir lives --archetype archer --session user:pass:Ayse
   then:  run scripts/js/archer.js
```

`--archetype` decides what a **new** life becomes. The persistent record stores
the archetype id and takes the build targets from the table. An existing life
keeps its archetype. Old records made before the table existed still load.

## 6. Client bindings added

`Player.cast(spellId, target)` and `Player.useSkill(skillId, target)`, over the
client's existing `ActionCastSpell` / `ActionUseSkill`.

## 7. Verified

* `js_bot_smoke.js`: **308 checks, 0 failures.** Covers:
  * the build, launcher, home town and legality of every archetype;
  * TSV drift;
  * that every fighter, crafter and gatherer constructs;
  * each style's range and melee hooks;
  * the mage's spell choice and heal-first, the warlock's Poison opener, the
    tamer's "all kill";
  * ground choice by level;
  * PvP gating: none without `ALLOW_PVP`, never near a bank, never started by
    a normal fighter;
  * material policy per craft;
  * the blacksmith targeting its ingots, the cook targeting an oven;
  * the tailor's bolt → cloth step;
  * the bowyer backing off when no menu opens;
  * crafter adverts heard and remembered, gear routed to the right craft;
  * that no mixin shadows another.
* `m4_lifecycle`: **144 checks.** The real TSV loads and validates. Refusals
  are tested for over 700, an inactive skill, over 225, an unknown kind and a
  duplicate id. The archetype survives the text format, and old records still
  load.
* `m2`, `m3`, `m35`, `m37` (99) and `m38` (293): all still pass. `Client.cpp`,
  `ClientTravel.cpp`, `ClientBindings.cpp`, `ClientLife.cpp` and `main.cpp`
  compile cleanly under MinGW.
* A trap guarded in code: libraries load in sorted order, so the engines apply
  their mixins on first construction. `applyMixins` **throws** if a mixin would
  overwrite a bot's own method, which is the M4.2 onFlee bug, now impossible.

**Nothing was run against Sphere.**

## 8. What each UNVERIFIED item needs

1. **bowyer, alchemist, scribe, cook:** one live craft each, to confirm the
   tool → target → menu sequence, and to record the real menu labels (which
   also fixes the set piece names).
2. **alchemist / scribe:** a bottle or blank-scroll source. That means a
   Revolution vendor ruling, or buying from players.
3. **fisher:** confirm water targeting at a dock.
4. **Mining outside Minoc:** each spot needs its mountain tile proven.
5. **Spell circle bands and Revolution spell damage:** UNKNOWN.
