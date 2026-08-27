# RevolutionUO Build Compendium
## Historical Player Builds + Revolution-Derived Archetypes — v1

**Project:** Revolution Offline  
**Purpose:** Provide a historically grounded library of character builds and playstyles for bot generation, progression planning, economy, PvM/PvP behavior and population diversity.  
**Research focus:** primarily 2007–2010 RevolutionUO forum material, with official Revolution system/skill documentation as supporting evidence.  
**Important:** Revolution changed over time. Do not mix builds and mechanics from different eras without an explicit rules profile.

---

# 1. How to Read This File

Every build has an evidence classification.

| Classification | Meaning |
|---|---|
| `HISTORICAL_EXACT` | Skill allocation appears directly in a Revolution forum post |
| `HISTORICAL_NEAR_EXACT` | Direct forum build with a minor ambiguity/rounding issue |
| `HISTORICAL_FAMILY` | Multiple forum discussions clearly support the build family, but there was no single canonical allocation |
| `REVOLUTION_DERIVED` | Built from verified Revolution skills/systems and player behavior; plausible for bots, but not claimed as a literal forum-posted template |
| `EXPERIMENTAL` | Useful future bot archetype requiring stronger historical verification |

### Critical rule for bot generation

Do **not** generate all characters from a single optimal template.

Revolution forum discussions repeatedly show players disagreeing about:
- Magery vs Meditation
- Poisoning vs mana sustain
- Healing/Anatomy allocation
- weapon family
- one-combat vs dual-combat
- PvP duel vs guild/group play
- utility/crafting seventh skill
- economy vs combat specialization

Bots should therefore select a **build family + variant + playstyle**, then actually progress toward it.

---

# 2. Skill Naming

This document uses normalized names:

- `SW` = Swordsmanship
- `FENC` = Fencing
- `MACE` = Mace Fighting
- `ARCH` = Archery
- `WREST` = Wrestling
- `TACT` = Tactics
- `ANAT` = Anatomy
- `HEAL` = Healing
- `MAGERY` = Magery
- `MEDI` = Meditation
- `EVAL` = Evaluating Intelligence
- `POI` = Poisoning
- `INS` = Inscription
- `ALCH` = Alchemy
- `TAILOR` = Tailoring
- `PARRY` = Parrying
- `LOCK` = Lockpicking
- `CARTO` = Cartography
- `MINING` = Mining
- `BS` = Blacksmithy
- `TAMING` = Animal Taming
- `LORE` = Animal Lore
- `VET` = Veterinary
- `HIDING` = Hiding
- `STEALTH` = Stealth
- `SNOOP` = Snooping
- `STEAL` = Stealing

**Resisting Spells is not included as a normal Revolution build skill.**  
The Revolution official guide lists it inactive, and 2008 forum users explicitly note that Resist was closed.

---

# 3. Build Families at a Glance

| Family | Typical role |
|---|---|
| Pure Mage | spell-first PvP/PvM, robes, poison, healing |
| Warlock | weapon + Magery hybrid |
| Pure Warrior | weapon-heavy PvP, poison, shields, healing |
| Multi-Combat | two or more weapon skills |
| Treasure Hunter | Lockpicking + Cartography + Mage support |
| Tamer | rare-mount hunting, pets, trade |
| Crafter Hybrid | Mining/Smith/Alchemy/etc. combinations |
| Fisher | fish/net/S.O.S. economy |
| Inscriber | scrolls, spellbooks, Runebooks |
| Tailor | special robes/leather economy |
| Tinkerer | trapped pouches, golems, utility items |
| Thief | Snooping/Stealing/Hiding/Stealth |
| PvM Farmer | loot/crystal/hide/map acquisition |
| PK / Head Hunter | murderer economy / PvP |
| Merchant Hybrid | craft/gather + player market |

---

# 4. PURE MAGE BUILDS

## PM-01 — Classic 7x Pure Mage
**Classification:** `HISTORICAL_EXACT` / common family

Forum-posted allocation:

| Skill | Value |
|---|---:|
| MAGERY | 100 |
| MEDI | 100 |
| EVAL | 100 |
| POI | 100 |
| HEAL | 100 |
| ANAT | 100 |
| INS or TAILOR | 100 |

**Total:** 700

### Playstyle
Spell-first character with full sustain, poison pressure and bandage healing. Seventh skill becomes identity.

### Variants
- `INS` → spell/scroll/Runebook utility
- `TAILOR` → Mage/Special Robe crafter
- `ALCH` → potion self-supply/merchant
- utility/Hiding variant discussed by players

### Bot behavior
- high reagent consumption
- values mana management
- prefers Mage/Special Robes
- uses player market for regs/potions unless self-producing
- builds Rune/Runebook travel network
- can become crafter-mage rather than combat-only

### Source
https://www.revolutionuo.net/forum/index.php?topic=47557.0

---

## PM-02 — Inscription Pure Mage
**Classification:** `HISTORICAL_FAMILY`

Core:
- MAGERY 100
- MEDI 100
- EVAL 100
- POI 100
- HEAL 100
- ANAT 100
- INS 100

### Why players chose it
Forum discussion connects Inscription to defensive magic behavior and profit/scroll production, although players debated how strong the combat bonus really was.

### Economy
- scroll production
- spellbooks
- Runebooks at high skill
- Runebook copying
- Recall/Resurrection scroll market

### Bot identity
Mage who can finance part of its own reagent/travel progression through Inscription.

---

## PM-03 — Special-Robe Tailor Mage
**Classification:** `HISTORICAL_FAMILY`

Core:
- MAGERY 100
- MEDI 100
- EVAL 100
- POI 100
- HEAL 100
- ANAT ~100
- TAILOR ~100

A player explicitly described using Tailoring as the side skill and crafting a large share of the shard's special robes.

### Bot goals
- gather/buy cloth
- acquire Hardening Crystal
- acquire special robe crystals
- craft Mage/Fire/Earth/Ice/Energy robes
- sell to players
- keep robes for own PvP/PvM use

### Source
https://www.revolutionuo.net/forum/index.php?topic=47557.0

---

## PM-04 — Alchemy Pure Mage
**Classification:** `HISTORICAL_FAMILY`

Core mage six:
- MAGERY
- MEDI
- EVAL
- POI
- HEAL
- ANAT

Seventh:
- ALCH

### Why it exists
Forum players explicitly suggested Alchemy as a seventh Pure Mage skill. Revolution's Alchemy system also directly supports PvP consumables.

### Bot behavior
- self-produces some Heal/Cure/Agility/Poison supplies
- sells kegs/potions
- may buy ingredients from other bots
- strong link to PvP economy

---

## PM-05 — Hiding/Utility Pure Mage
**Classification:** `HISTORICAL_FAMILY`

Core mage package plus:
- HIDING / low utility skill allocation

Forum players discussed Hiding/utility as a seventh-skill option.

### Bot behavior
- avoids dangerous encounters
- hides when low on supplies
- useful treasure/PvM/scouting behavior
- may sacrifice economy/crafting specialization

---

# 5. WARLOCK BUILDS

Warlock was not one build. Revolution players explicitly debated mana-heavy, poison-heavy, duel-oriented and group-oriented versions.

---

## WL-01 — 2007 Balanced Sword Warlock
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| MAGERY | 100 |
| SW | 100 |
| TACT | 100 |
| EVAL | 80 |
| POI | 80 |
| HEAL | 80 |
| ANAT | 80 |
| MEDI | 80 |

**Total:** 700

### Style
Balanced weapon/magic hybrid.

### Strengths
- full Sword/Magery/Tactics
- usable poison
- decent healing/mana
- broad utility

### Weakness
Does not maximize all support skills.

### Source
https://www.revolutionuo.net/forum/index.php/topic%2C23720.0.html

---

## WL-02 — 2007 Poison-Heavy Warlock
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| Combat skill | 100 |
| MAGERY | 100 |
| POI | 100 |
| TACT | 100 |
| ANAT | 80 |
| EVAL | 80 |
| HEAL | 80 |
| MEDI | 60 |

**Total:** 700

### Style
Warrior-like Warlock with strong poison pressure and lower mana sustain.

### Good for
- duels
- finishing pressure
- weapon-heavy play

### Source
https://www.revolutionuo.net/forum/index.php/topic%2C23720.0.html

---

## WL-03 — 2010 Fencing Duel Warlock
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| FENC | 100 |
| TACT | 100 |
| ANAT | 100 |
| POI | 100 |
| MAGERY | 85 |
| HEAL | 80 |
| EVAL | 75 |
| MEDI | 60 |

**Total:** 700

### Style
Aggressive Fencing/Poison hybrid, suited to direct fights.

### Behavior
- Spear/Short Spear pressure
- Paradarbe opportunity
- poison weapon/spell pressure
- modest mana pool/regeneration compared with pure mage

### Source
https://www.revolutionuo.net/forum/index.php?topic=77029.0

---

## WL-04 — 2010 Mana-Heavy Group Warlock
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| MAGERY | 100 |
| MEDI | 100 |
| Combat skill | 100 |
| TACT | 100 |
| ANAT | 100 |
| HEAL | 80 |
| EVAL | 75 |
| POI | 45 |

**Total:** 700

### Style
Group/guild support Warlock.

Forum discussion explicitly contrasts this with poison-heavy duel builds.

### Behavior
- more spell fizzle/support pressure
- better mana recovery
- less deadly poison capability
- strong chase/group assistance

### Source
https://www.revolutionuo.net/forum/index.php?topic=77029.0

---

## WL-05 — Sword + Wrestling Warlock
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| SW | 100 |
| WREST | 100 |
| TACT | 100 |
| POI | 100 |
| MAGERY | 100 |
| ANAT | 100 |
| HEAL | 80 |
| MEDI | 20 |

**Total:** 700

### Style
Very combat-heavy hybrid.

### Interesting behavior
- can fight armed or unarmed
- Wrestling opens Stun/Disarm family interactions
- very low Meditation means limited sustained casting

### Source
https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2

---

## WL-06 — Archery + Mace Warlock
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| MAGERY | 100 |
| ARCH | 100 |
| MACE | 100 |
| ANAT | 100 |
| HEAL | 80 |
| MEDI | 80 |
| EVAL | 40 |

**Total:** 600 shown in post; remaining budget not specified.

**Classification note:** historically posted but incomplete allocation.

### Style
Ranged + melee hybrid with magic.

### Bot usage
Treat missing ~100 points as configurable:
- TACT
- POI
- utility
depending on chosen ruleset.

### Source
https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2

---

## WL-07 — Sword + Mace Double-Combat Warlock
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| SW | 100 |
| MACE | 100 |
| TACT | 100 |
| POI | 90 |
| HEAL | 90 |
| ANAT | 90 |
| MAGERY | 80 |
| MEDI | 50 |

**Total:** 700

### Style
Dual-melee hybrid sacrificing deep Mage support.

### Bot behavior
- chooses weapon based on opponent/equipment
- Mace for armor/stamina pressure
- Sword for bleed/cutting options
- Magery primarily utility/pressure, not pure caster role

### Source
https://www.revolutionuo.net/forum/index.php?topic=43700.0

---

## WL-08 — Sword + Mace Utility Warlock
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| SW | 100 |
| MACE | 100 |
| TACT | 100 |
| ANAT | 100 |
| MAGERY | 85 |
| EVAL | 85 |
| HEAL | 80 |
| MEDI | 50 |

**Total:** 700

### Style
Dual-combat, non-poison build with stronger magic quality.

### Source
https://www.revolutionuo.net/forum/index.php?topic=43700.0

---

# 6. PURE WARRIOR BUILDS

## PW-01 — Sword + Archery Poison Parry Warrior
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| TACT | 100 |
| ANAT | 100 |
| HEAL | 100 |
| POI | 100 |
| SW | 100 |
| ARCH | 100 |
| PARRY | 100 |

**Total:** 700

### Style
No real Mage package; physical combat specialist.

### Strengths
- ranged opening/chase
- Sword bleed/weapon effects
- poisoned weapons
- strong healing
- shield defense

### Weakness
Limited magical travel/utility without external items/services.

### Source
https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2

---

## PW-02 — Low-Magery Sword + Archery Warrior
**Classification:** `HISTORICAL_NEAR_EXACT`

Forum recommendation:

| Skill | Value |
|---|---:|
| MAGERY | 25 |
| SW | 100 |
| ARCH | 100 |
| TACT | 100 |
| PARRY | 100 |
| POI | 80 |
| HEAL | 95 |
| ANAT | 100 |

**Total:** 700

### Why low Magery?
Forum post explicitly notes 25 Magery as a minimum to use a particular utility spell (`Kal Ort`) in that period.

### Style
Mostly pure warrior but retains tiny magical utility.

### Source
https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2

---

## PW-03 — Fencing Poison Warrior
**Classification:** `HISTORICAL_FAMILY`

Core:
- FENC 100
- TACT 100
- ANAT 100
- HEAL high
- POI high
- PARRY or second combat/utility

Forum players praised War Fork/Spear and Fencing speed/effectiveness.

### Bot behavior
- Paradarbe
- poisoned thrusting weapons
- strong pursuit
- low reliance on mana

Sources:
https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2
https://www.revolutionuo.net/forum/index.php?topic=23379.0

---

## PW-04 — Mace Armor-Break Warrior
**Classification:** `HISTORICAL_FAMILY`

Core:
- MACE 100
- TACT 100
- ANAT 100
- HEAL
- PARRY
- optional POI / ARCH / utility

### Style
Armor/stamina pressure.

Official guide identifies:
- stamina drain
- armor break
- dismount-related effects

### Bot behavior
Prefers heavily armored opponents and values Black Staff/Mace family depending on era.

---

# 7. MULTI-COMBAT WARRIOR / HYBRIDS

## MC-01 — Sword + Archery Warrior
**Classification:** `HISTORICAL_FAMILY`

Very common forum discussion.

Use cases:
- ranged chase
- switch to Sword at close range
- poison/bleed pressure
- shield/Parry variant

---

## MC-02 — Sword + Mace Warrior
**Classification:** `HISTORICAL_FAMILY`

### Role
Switches between:
- Sword cutting/bleed
- Mace armor/stamina pressure

### Bot decision
Opponent:
- high armor → Mace
- exposed/low HP → Sword
- distance → chase/magic/other if available

---

## MC-03 — Fencing + Archery Warrior
**Classification:** `HISTORICAL_FAMILY`

Combines:
- ranged opening
- fast Fencing close pressure
- Paradarbe

Useful for mobile PvP bots.

---

# 8. TREASURE HUNTER BUILDS

Forum source:
https://www.revolutionuo.net/forum/index.php?topic=66975.0

---

## TH-01 — Balanced Treasure Mage
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| LOCK | 100 |
| CARTO | 100 |
| MAGERY | 100 |
| EVAL | 100 |
| MEDI | 100 |
| POI | 80 |
| HEAL | 60 |
| ANAT | 60 |

**Total:** 700

### Style
Can locate/open treasure, kill/handle threats with Magery, poison and heal.

### Bot behavior
- decode map
- travel
- dig/open
- handle guardians
- loot
- identify/sell valuable goods
- recover if killed

---

## TH-02 — Alchemist Treasure Hunter
**Classification:** `HISTORICAL_EXACT`

| Skill | Value |
|---|---:|
| LOCK | 100 |
| CARTO | 100 |
| MAGERY | 100 |
| EVAL | 100 |
| MEDI | 100 |
| POI | 100 |
| ALCH | 100 |

**Total:** 700

### Style
No bandage package; potion/caster economy hybrid.

### Bot behavior
Produces consumables and treasure hunts.

---

## TH-03 — Inscription Treasure Hunter
**Classification:** `HISTORICAL_FAMILY`

Forum player says same Treasure setup but prefers Inscription instead of Alchemy.

Likely:
- LOCK
- CARTO
- MAGERY
- EVAL
- MEDI
- POI
- INS

### Economy
- maps/treasure
- scrolls
- Runebooks
- magical loot

---

## TH-04 — Mining Treasure Hunter
**Classification:** `HISTORICAL_FAMILY`

Forum discussion notes approximately **30 Mining** was required to open/dig treasure in that period.

### Bot rule
Treasure system should validate the actual era's Mining requirement rather than assuming it universally.

---

# 9. TAMER BUILDS

Exact numeric Tamer builds were harder to recover in the first research pass. These are therefore marked `REVOLUTION_DERIVED` unless later forum evidence gives an exact template.

Official guide verifies:
- Animal Taming
- Animal Lore
- Veterinary
- mount riding/taming
- rare mount requirements
- Spawntakip rare-mount system

---

## TM-01 — Rare Mount Hunter
**Classification:** `REVOLUTION_DERIVED`

Core:
- TAMING high
- LORE high
- VET high
- MAGERY useful
- MEDI useful
- HEAL/ANAT or defensive utility
- optional combat skill

### Goal
Use Spawntakip, search the world, survive protectors and tame valuable mounts.

### Bot behavior
- check spawn schedule
- decide expected value
- travel/search
- avoid killing target mount
- manage protectors
- tame
- stabilize/heal pet
- stable/use/sell

---

## TM-02 — Combat Tamer
**Classification:** `REVOLUTION_DERIVED`

Core:
- TAMING
- LORE
- VET
- MAGERY
- MEDI
- EVAL / HEAL
- combat/utility skill

### Role
Uses pets for PvM and supports them with magic/healing.

---

## TM-03 — Merchant Tamer
**Classification:** `REVOLUTION_DERIVED`

Core:
- TAMING
- LORE
- VET
- travel/support skills
- optional economic/crafting skill

### Economy
- tame mounts
- stable them
- sell/contract mounts
- buy feed/supplies
- build reputation as reliable mount supplier

---

## TM-04 — Pack-Animal Resource Tamer
**Classification:** `REVOLUTION_DERIVED`

Core Taming package plus gathering/crafting skill.

### Role
Uses pack animals/llamas for heavy resource transport.

Potential combinations:
- Taming + Mining
- Taming + Lumberjacking
- Taming + merchant activity

---

# 10. CRAFTER & RESOURCE HYBRIDS

These are deliberately **not rigid classes**.

---

## CR-01 — Miner + Blacksmith
**Classification:** `REVOLUTION_DERIVED`, strongly system-supported

Core:
- MINING
- BS
- supporting travel/economy skills

### Loop
```text
mine
→ smelt
→ craft
→ repair
→ sell
```

### Customers
Warriors, PvPers, other crafters.

---

## CR-02 — Miner + Smith + Alchemist + Mage Hybrid
**Classification:** `PLAYER_MEMORY + REVOLUTION_DERIVED`

This matches the project owner's memory of real Revolution characters combining several economic/combat skills.

### Identity
Self-sufficient but still economically rational.

### Bot behavior
- mine if ore needed/profitable
- smith if equipment/order needed
- craft potion when worthwhile
- buy from other player if cheaper/faster
- use Magery for travel/survival

---

## CR-03 — Alchemist Combat Supplier
**Classification:** `REVOLUTION_DERIVED`

Core:
- ALCH high
- optional MAGERY/POI
- economic/travel support

### Products
- Greater Cure
- Greater Heal
- Agility
- Refresh/Total Refresh if available in era
- Poison/Deadly Poison
- Explosion products depending rules

### Customers
PvPers, Warlocks, Mages, treasure hunters.

---

## CR-04 — Special Robe Tailor
**Classification:** `HISTORICAL_FAMILY`

Core:
- TAILOR high
- often MAGERY-oriented character
- economic/travel support

### Products
- Mage Robe
- Fire Robe
- Earth Robe
- Ice Robe
- Energy Robe
- special leather sets

### Inputs
- cloth
- Hardening Crystal
- robe-specific crystals
- special hides/process materials

---

## CR-05 — Inscriber / Runebook Maker
**Classification:** `HISTORICAL_FAMILY`

Core:
- INS high
- MAGERY required
- travel/economy support

### Products
- scrolls
- spellbooks
- Runebooks
- Runebook copies
- Recall scroll supply

### Bot economy
May become a strategic travel-infrastructure supplier.

---

## CR-06 — Tinkerer PvP Supplier
**Classification:** `REVOLUTION_DERIVED`

Core:
- Tinkering high
- resource procurement/support

### Products
- Trapped Pouches
- Golems
- utility items

### Customers
PvPers and other specialized players.

---

## CR-07 — Lumberjack + Bowyer
**Classification:** `REVOLUTION_DERIVED`

Core:
- Lumberjacking
- Bowcraft/Fletching
- optional ARCH

### Loop
```text
harvest logs
→ special logs
→ craft bows/arrows
→ sell to Archers
```

Official guide explicitly connects special Vesper logs to special bows.

---

## CR-08 — Carpenter Merchant
**Classification:** `REVOLUTION_DERIVED`

Core:
- Carpentry
- Lumberjacking optional
- market/travel support

### Products
Furniture, decorative/home goods and other craftables.

### Behavior
More peaceful economic bot; interacts heavily with housing economy.

---

# 11. FISHER BUILDS

## FI-01 — Commercial Fisher
**Classification:** `HISTORICAL_FAMILY`

Core:
- Fishing high
- travel/boat support
- optional economy utility

### Progression
```text
rod fish
→ sell fish
→ acquire Shell
→ nets
→ S.O.S.
→ treasure
```

Sources:
https://www.revolutionuo.net/forum/index.php?topic=76853.0
https://www.revolutionuo.net/forum/index.php?topic=53536.15

---

## FI-02 — Fisher + Tailor
**Classification:** `REVOLUTION_DERIVED`

Why:
Tailoring is involved in fishing-net production from Shell + cloth.

### Behavior
Self-produces nets when efficient, otherwise buys from player market.

---

## FI-03 — Fisher + Treasure Hybrid
**Classification:** `REVOLUTION_DERIVED`

Skills may combine:
- Fishing
- Cartography
- Lockpicking
- Magery/support

### Role
Turns S.O.S./treasure results into a broader treasure-hunting career.

---

# 12. THIEF / SCOUT BUILDS

Official guide verifies:
- Snooping
- Stealing
- Hiding
- Stealth

Exact numeric forum templates remain a research target.

---

## TF-01 — Classic Thief
**Classification:** `REVOLUTION_DERIVED`

Core:
- SNOOP
- STEAL
- HIDING
- STEALTH
- survival/travel support

### Behavior
- inspect packs
- evaluate item weight/value
- steal when risk/reward makes sense
- hide/escape
- sell stolen goods carefully

---

## TF-02 — Combat Thief
**Classification:** `REVOLUTION_DERIVED`

Thief core plus:
- weapon skill
- TACT / HEAL / MAGERY utility

### Role
More dangerous criminal player rather than pure pickpocket.

---

## TF-03 — Scout / Information Broker
**Classification:** `REVOLUTION_DERIVED`

Core:
- Hiding
- Stealth
- Tracking/Detecting
- travel utility

### Future social economy
Can provide:
- enemy sightings
- rare mount sightings
- dangerous-area information
- guild reconnaissance

---

# 13. PVM / LOOT BUILDS

## PV-01 — Pure Mage PvM Farmer
**Classification:** `HISTORICAL_FAMILY`

Based on Pure Mage core.

### Goals
- Dragon/Balron/etc.
- magical weapons
- special crystals
- hides
- treasure maps
- gold

### Economy
Keeps upgrades, sells high-value drops to other players.

---

## PV-02 — Robe-Material Hunter
**Classification:** `REVOLUTION_DERIVED`

Combat-capable build optimized around farming:
- Hardening Crystal
- robe crystals
- special hides
- high-end PvM materials

### Economic relationship
Supplies Tailor/Mage robe crafters.

---

## PV-03 — Magical Weapon Hunter / Item-ID Trader
**Classification:** `REVOLUTION_DERIVED`

Combat + Item Identification.

### Loop
```text
kill
→ loot "a magical ..."
→ Item ID
→ compare +3/+6/+9/+12/+15
→ keep / sell / trade
```

---

# 14. PK / HEAD HUNTER BUILDS

## PK-01 — Warlock Head Hunter
**Classification:** `REVOLUTION_DERIVED`

Uses a historical Warlock template but behavior goal differs:
- murder/contract hunting
- collect eligible heads
- turn in for gold
- maintain PvP supplies
- avoid guarded areas when necessary

Official system:
https://www.revolutionuo.net/head_hunters

---

## PK-02 — Pure Warrior Head Hunter
**Classification:** `REVOLUTION_DERIVED`

Uses strong Warrior allocation:
- combat
- Tactics
- Anatomy
- Healing
- Poisoning
- Parry/second combat

### Behavior
Lower reagent dependency; heavy potion/bandage/weapon dependency.

---

# 15. ECONOMY / MERCHANT BUILDS

## EC-01 — Resource Merchant
**Classification:** `REVOLUTION_DERIVED`

Skills:
- one or more gathering skills
- market/travel utility

### Behavior
Does not necessarily craft.
Buys cheap resources from gatherers and resells where demand is higher.

---

## EC-02 — Multi-Craft Merchant
**Classification:** `REVOLUTION_DERIVED`

Could combine several:
- Mining
- BS
- Alchemy
- Tailoring
- Tinkering
- Inscription

### Important
Not all at GM simultaneously if cap prevents it. Character chooses a realistic subset and may change goals over time.

---

## EC-03 — Player Vendor Operator
**Classification:** `REVOLUTION_DERIVED`

Any production/gather build can specialize in:
- stocking a player vendor
- pricing
- monitoring sales
- restocking
- purchasing from other players for resale

Revolution's Vendor Cooperative makes this especially relevant.

---

# 16. LOW-INCOME / EARLY CHARACTER ARCHETYPES

## ER-01 — Beggar Starter
**Classification:** `REVOLUTION_DERIVED`

Core:
- Begging
- whatever intended future build requires

### Use
Very low-capital start.
Begging can generate initial gold for:
- trainer
- tool
- bandages
- first reagents

Then transitions into intended build.

---

## ER-02 — Trainer-Funded New Character
**Classification:** `PLAYER_MEMORY + HISTORICAL_SYSTEM`

Behavior:
```text
earn/save gold
→ find NPC teacher
→ pay for early skill up to allowed threshold
→ continue manual training
```

This is not a permanent build, but a progression archetype.

---

# 17. Build Personality Variants

Two characters with identical skills should not necessarily behave identically.

Use a separate `playstyle_profile`.

Examples:

## Duelist
- values burst and poison
- accepts lower Meditation
- seeks 1v1
- keeps aggressive loadout

## Guild Fighter
- higher mana/support preference
- stays with allies
- helps fizzle/control enemies
- values Recall/Runebook readiness

## Risk-Averse Crafter
- avoids PvP zones
- banks often
- recalls/runs from threats
- prefers stable profit

## Opportunistic Merchant
- compares player/NPC prices
- travels to shortages
- buys for resale

## Self-Sufficient Hybrid
- crafts own items when convenient
- trades only when time/resource efficiency favors it

## Specialist
- maximizes one production/combat niche
- depends heavily on other players

---

# 18. Build Selection for Bots

Do not do:

```text
random class = Mage
skills = canonical_mage
```

Prefer:

```text
choose build family
→ choose historical/derived variant
→ choose playstyle profile
→ choose home/wealth/background
→ determine desired final skills
→ begin below target
→ legitimately progress toward build
```

Example:

```text
family: Warlock
variant: WL-04 mana-heavy
playstyle: guild fighter
home: Yew
starting wealth: low
known trader: none
goal: join guild PvP
```

versus:

```text
family: Warlock
variant: WL-03 fencing duel
playstyle: duelist
home: Britain
starting wealth: medium
goal: 1v1 / Head Hunter later
```

Same general family, very different player.

---

# 19. Progression Should Change Build Viability

Bots should not start as finished 7x characters unless the simulation explicitly creates an established veteran population.

A new character may have:
- 2 skills at creation values
- some NPC-taught skills
- unfinished 20–60 support skills
- incomplete equipment
- no Rune/Runebook network
- limited gold

Over time:
```text
train
→ hit total cap
→ lower unwanted skill
→ reshape build
→ obtain better equipment
→ establish income
→ build travel network
→ specialize
```

Established bots can enter the world at different life stages to make the shard feel populated.

---

# 20. Build-Driven Economic Needs

## Pure Mage
Needs:
- reagents
- bandages
- Mage/Special Robes
- scrolls/spellbook
- Recall supplies
- potions

## Warlock
Needs:
- magical/poisonable weapon
- regs
- bandages
- potions
- mount
- armor/robe depending rules

## Warrior
Needs:
- weapons
- armor/set
- shield
- poison/kegs
- bandages
- potions
- mount
- repairs

## Treasure Hunter
Needs:
- maps
- lockpicks
- regs
- combat supplies
- storage
- buyer for loot

## Tamer
Needs:
- travel
- healing salves/supplies
- stable access
- spawn information
- buyers

## Crafter
Needs:
- raw materials
- tools
- recipes/components
- market/buyers
- safe storage

This is how builds create player-to-player trade naturally.

---

# 21. Source Index

## Pure Mage
https://www.revolutionuo.net/forum/index.php?topic=47557.0

## 2007 Warlock
https://www.revolutionuo.net/forum/index.php/topic%2C23720.0.html

## 2010 7x Warlock
https://www.revolutionuo.net/forum/index.php?topic=77029.0

## Multi-Combat Warlock
https://www.revolutionuo.net/forum/index.php?topic=43700.0

## Warrior / Archery / Sword / Fencing builds
https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2

## Weapon meta
https://www.revolutionuo.net/forum/index.php?topic=23379.0

## Treasure Hunter builds
https://www.revolutionuo.net/forum/index.php?topic=66975.0

## Skill training
https://www.revolutionuo.net/forum/index.php?topic=59111.0

## Official gameplay guide
https://www.revolutionuo.net/oyun_rehberi

## Official updates
https://www.revolutionuo.net/guncellemeler

## Spawntakip
https://www.revolutionuo.net/spawntakip_sistemi

## Head Hunters
https://www.revolutionuo.net/head_hunters

## Vendor Cooperative
https://www.revolutionuo.net/tezgahtarlar_kooperatifi

---

# 22. Research Backlog for v2

The build forum archive is large. Highest-value next searches:

- exact Animal Tamer skillcaps
- Tamer + Mage builds
- PvP Tamer builds
- merchant Tamer builds
- thief skillcaps
- Hiding/Stealth builds
- Lumberjack/Bowyer exact player builds
- Smith/Miner exact 7x build posts
- Alchemy crafter combinations
- Tinker builds
- Tailor/robe crafter skillcaps
- Fisher character builds
- Inscription merchant builds
- PvM Dragon/Balron builds
- Head Hunter player skillcaps
- Order/Chaos build preferences
- Archer-specific builds
- Mace-only builds
- Fencing-only builds
- Wrestling/Stun/Disarm specialists
- Item ID treasure/PvM specialists
- exact STR/DEX/INT distributions by build
- mount/Taming skill requirements by target-era mount
- whether 2008–2010 target rules permit every late-guide mechanic

---

# 23. Recommended Machine-Readable Build Record

Future implementation should convert verified builds into data.

Example:

```yaml
id: wl_2010_fencing_duelist
era: 2010
evidence: HISTORICAL_EXACT
family: warlock

skills:
  fencing: 100
  tactics: 100
  anatomy: 100
  poisoning: 100
  magery: 85
  healing: 80
  eval_int: 75
  meditation: 60

playstyle_tags:
  - pvp
  - duel
  - poison_heavy
  - melee_first

economic_needs:
  - fencing_weapons
  - poison
  - bandages
  - reagents
  - potion_kegs
  - mount

source:
  - https://www.revolutionuo.net/forum/index.php?topic=77029.0
```

Then bot generation can select from historically grounded records while still progressing legitimately toward them.

---

# 24. Final Principle

The purpose of this compendium is **not** to find one “best Revolution build.”

The forum shows there was no single best build.

Players made meaningful trade-offs:
- mana vs poison
- magic vs weapon strength
- support vs offense
- one combat vs multiple combat
- crafting vs PvP utility
- self-sufficiency vs specialization
- solo vs guild play

That disagreement is a feature.

For Revolution Offline, bot diversity should come from those same choices.

> **A Revolution bot should look like a player who chose and developed a character, not an NPC instantiated from a class template.**
