# RevolutionUO Gameplay & Bot Behavior Bible
## Historical Forum / Official Archive Research — v1

**Project:** Revolution Offline  
**Purpose:** Source-backed reference for reconstructing RevolutionUO and designing bots that behave like actual Revolution players rather than generic UO NPCs.  
**Research date:** 2026-08-26  
**Historical material found:** 2006–2016  
**Likely most relevant reconstruction window:** Revolution10-era / roughly 2008–2010, but the target era should be selected explicitly.

---

# 1. Evidence Model

Revolution changed frequently. Never merge all years into one ruleset without dates.

| Tag | Meaning |
|---|---|
| `OFFICIAL_GUIDE` | Revolution's own gameplay/system page |
| `OFFICIAL_UPDATE` | Official change log |
| `FORUM_GUIDE` | Structured player guide on Revolution forum |
| `PLAYER_DISCUSSION` | Player build/market/gameplay discussion |
| `PLAYER_MEMORY` | Direct memory from the Revolution Offline project owner |
| `LIVE_RECONSTRUCTION` | Verified against our current Source-X reconstruction |
| `SOURCE_X_DEFAULT` | Current Source-X/Scripts-X behavior, not automatically historical Revolution |
| `UNKNOWN` | Needs verification |

**Evidence priority:** official Revolution guide/system pages → official update archive → Revolution forum guides → repeated player discussions → player memory → current reconstruction → generic Sphere/UO only as fallback.

If historical Revolution evidence conflicts with current Source-X, record a **compatibility gap**.

---

# 2. Core Historical Conclusion

The archive shows RevolutionUO was deeply customized. Relevant systems included:

- custom skill training/gain methods
- active/inactive skills
- 7x/hybrid builds
- Mage Robes and special robes
- weapon-specific PvP effects
- poisoning rules
- player vendors and a searchable vendor cooperative
- direct trade taxes
- custom Runebooks
- guild Runebooks
- Reagent/Store Crystals
- potion-keg workflows
- Head Hunters
- Spawntakip and rare mounts
- mount armor
- housing/home systems
- guild tracking/death notifications
- anti-macro verification
- fishing/net/S.O.S. loops
- treasure maps/chests
- special ores/logs/leathers
- custom crafting and loot
- duel/10v10/Order-Chaos/guild systems

**Bot target:** simulated Revolution players, not stock-UO NPCs.

---

# 3. Era Profiles

## 3.1 2006–2008
Early Revolution already shows:
- macro culture
- warlock/pure warrior build debates
- 7x thinking
- custom weapon balance
- Runebooks
- houses/guilds
- player vendors
- anti-macro
- mount systems

## 3.2 2008–2010 — likely Revolution10 core
This is currently the most useful candidate profile because the archive strongly matches project memories:
- 7x builds
- special robes
- Runebooks
- vendor cooperative
- fishing/Shell/net/S.O.S.
- Head Hunters
- Spawntakip
- kegs/reagents
- player economy
- custom PvP

## 3.3 2010–2012
Later additions/changes:
- guild Runebooks
- Store Crystals
- more marketplace support
- ships/cannons
- safe boxes
- special Order/Chaos mounts
- resource removals/rebalances
- spell/meditation changes

## 3.4 2015–2016
Late/revival rules changed again:
- combat formulas
- Cure behavior
- treasure guardians
- housing/guild reactivation
- anti-macro behavior
- Revolution16 client

**Implementation rule:** every numeric mechanic should have an era tag.

---

# 4. Skills — Officially Inactive

The official Revolution gameplay guide explicitly lists these as inactive/untrainable in the documented late-era rules:

- Herding
- Remove Trap
- **Resisting Spells**
- Enticement
- Peacemaking
- Provocation
- Spirit Speak
- Forensic Evaluation
- Taste Identification

This is strong evidence that **Resisting Spells should not be inserted into Revolution builds by default**.

Source: https://www.revolutionuo.net/oyun_rehberi

---

# 5. Historical Skill Training Guide

A 2009 forum guide gives concrete training bands.

Source: https://www.revolutionuo.net/forum/index.php?topic=59111.0

## 5.1 Magery

| Skill | Typical action |
|---|---|
| 0–30 | Night Sight |
| 30–40 | Bless |
| 40–60 | Greater Heal |
| 60–70 | Magic Reflection |
| ~65–70 | Paralyze alternative |
| 70–80 | Reveal / Invisibility / Energy Bolt |
| 80–90 | Energy Field / Mass Dispel |
| ~85–90 | Flame Strike alternative |
| 90–100 | Earth Elemental / Earthquake |

**Bot rule:** Magery training should adapt spell by skill band and actual resource/spell access.

## 5.2 Alchemy

| Skill | Product |
|---|---|
| ~15–25 | Normal Heal |
| ~25–35 | Normal Cure |
| ~35–55 | Greater Agility |
| ~55–65 | Greater Heal |
| ~65–90 | Greater Cure |
| ~90–100 | Deadly Poison |

This directly supports a player-run potion economy.

## 5.3 Other important training methods

- Blacksmithy: Dagger to roughly 70, then Short Spear.
- Tailoring: Body Sash through mid-70s, then Oil Cloth; Rope used in part of the middle band.
- Tinkering: basic small items early; lock-related production later.
- Inscription: Poisoning Scroll early, Recall Scroll around 60–80, Resurrection Scroll around 80–100.
- Fishing: fish normally; moving by ship was recommended.
- Mining: mining and smelting.
- Healing: bandage healing.
- Tactics: hit players/creatures; bare-hand training was recommended.
- Wrestling: fight bare-handed.
- Poisoning: player guide suggests using Poison against a sufficiently durable/healing training partner.
- Snooping/Stealing/Tracking/Cartography etc. each had actual use-based training loops.

**Bot rule:** `TrainSkill()` should choose a legitimate strategy; never direct-set the skill.

---

# 6. NPC Teaching / Paying for Early Skill

Historical evidence confirms a **Teaching** system:

- the official update archive mentions fixes to the Teaching menu
- forum players explicitly discuss “teaching” skills such as Inscription instead of grinding them from zero

The project owner remembers NPC trainers raising some skills to about **30.0** for gold, often around **300gp or another skill-dependent amount**.

Current confidence:

| Claim | Confidence |
|---|---|
| Teaching menu/system existed | High |
| Players used it for early skill | High |
| Exact cap 30.0 | Player memory; verify |
| Exact price 300gp | Player memory; verify |
| Price varies by skill/current value | Needs verification |

**Future bot behavior:** evaluate `pay trainer` versus `train manually`.

---

# 7. 7x Builds and Hybrid Characters

Forum discussions repeatedly use “7x” language and show deliberate skill-budget builds.

Key sources:
- https://www.revolutionuo.net/forum/index.php?topic=43700.0
- https://www.revolutionuo.net/forum/index.php?topic=77029.0
- https://www.revolutionuo.net/forum/index.php/topic,23720.0.html
- https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2

Common warlock ingredients:
- one weapon skill
- Tactics
- Anatomy
- Healing
- Magery
- Meditation
- Evaluating Intelligence
- Poisoning

Players traded points between these according to style.

**Do not create rigid classes.** A character can be Mining + Blacksmithy + Alchemy + Magery, or any other coherent hybrid.

Capabilities should come from:
- actual skills
- stats
- inventory
- wealth
- world knowledge
- equipment
- relationships
- history

---

# 8. Movement Style

A 2008 build discussion explicitly describes a player going everywhere by **running**.

Source: https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2

Bot policy:
- default = RUN
- WALK only for precision/doorways/collision/mechanics
- normal travel = RUN + PEACE
- do not remain stuck in War Mode

For visual realism, do not make every bot take the identical A* tiles:
- stable per-bot route seed
- 1–2 tile lane offsets
- alternate approach tiles
- alternate equivalent entrances
- crowd avoidance
- recent-route penalty
- small bounded tie-break noise
- preferences such as shortest / road / safer / uncrowded / mixed

No machine learning is required.

---

# 9. Revolution Combat Special Moves

Official update history documents skill-gated special effects:

- **Disarm:** Wrestling + Arms Lore; drops opponent weapon to backpack.
- **Dismount:** historically tied to Tactics; knocks opponent from mount.
- **Paradarbe:** Fencing 80; Anatomy affects success; Short Spear was more effective than Spear; brief immobilization.
- **Stun:** Wrestling 80; Anatomy/Arms Lore/Wrestling affect success.
- **Shield/armor break:** Macefighting 80; Anatomy matters.
- **Bleeding:** Swordsmanship 80; Anatomy/Swordsmanship affect success; recurring bleeding.

Source: https://www.revolutionuo.net/guncellemeler

**PvP bot rule:** weapon/build choice must consider special effects, not only raw damage.

---

# 10. Weapon Meta

Forum players discussed real shard meta:
- Katana for speed
- Spear/War Fork for Fencing
- Black Staff as extremely strong in some periods
- Halberd/Bardiche as heavy options
- magical tiers such as +3/+6/+9/+12/+15, with +18 existing in some eras and later removed/rebalanced

Sources:
- https://www.revolutionuo.net/forum/index.php?topic=23379.0
- https://www.revolutionuo.net/oyun_rehberi

Future weapon evaluation:
`family + magical tier + speed + special move + poison compatibility + durability + build + price`.

---

# 11. Healing / Poison / Magery

## Healing
Official guide:
- bandages heal
- Anatomy increases healing
- Healing+Anatomy thresholds enabled poison cure and later resurrection
- Dexterity affects bandage timing

## Poison
Official guide/update history:
- Poisoning increases poison effectiveness
- warriors poisoned weapons
- poison/Magery restrictions changed over time
- high Poisoning could make poison harder to immediately cure

**Era warning:** poison weapon restrictions changed/reverted; do not mix 2016 behavior into a 2009 profile.

## Magery / Eval Int / Meditation
Official guide:
- Eval Int increases mage damage
- Eval Int is involved in robe requirements
- Meditation was a meaningful build tradeoff
- spell timings/damage were repeatedly rebalanced

Sources:
- https://www.revolutionuo.net/oyun_rehberi
- https://www.revolutionuo.net/guncellemeler

---

# 12. Mage Robes / Fire Robe

Official Revolution page confirms four special robes:
- Energy Robe
- **Fire Robe**
- Earth Robe
- Ice Robe

Source: https://www.revolutionuo.net/ozel_mage_robe

Fire Robe historically protected against a set of fire-oriented spells including Magic Arrow, Fireball, Fire Field, Explosion, Flame Strike and Meteor Swarm.

Special robes required very high mage-related skills in the documented version. Later updates explicitly allowed Wrestling without blocking special-robe use.

Crafting evidence:
- Mage Robe: Hardening Crystal + cloth + very high Tailoring
- special robes: Hardening Crystal + robe-specific crystal + cloth
- crystals/materials came from high-end PvM/chests
- robes aged/deteriorated
- protection could fail as condition worsened
- robes could be dismantled with scissors, influenced by Tailoring and condition

Forum source:
https://www.revolutionuo.net/forum/index.php?topic=84687.0

**Bot implication:** Fire Robe is a progression, PvM, crafting, market and PvP object — not just cosmetic.

---

# 13. PvP Loadouts

A 2008 player described a serious warrior loadout containing:
- multiple magical/poisoned weapons
- a large Deadly Poison keg
- Heal/Cure potion supply
- many bandages
- ammunition
- strong shield

Source:
https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2

Future PvP preparation:
```text
check weapon/armor
check bandages
check reagents
check potion kegs
check cure/heal/refresh/explosion stock
check trapped pouches
check ammo
check mount
repair/restock
then fight
```

A PvPer low on Greater Cure should be able to buy a keg from a crafter bot through real secure trade.

---

# 14. Player Economy

Revolution had a strong player market.

Official Vendor Cooperative:
https://www.revolutionuo.net/tezgahtarlar_kooperatifi

Historical searchable player-vendor items included, depending on era:
- weapons
- colored armor/leather sets
- mounts
- logs
- ingots
- arrows/bolts
- filled potion kegs
- Mage/Special Robes
- spellbooks
- Runebooks
- golems
- trapped pouches
- fishing nets
- bandages
- potions
- ships

The cooperative had locations around banks such as Britain, Delucia, Buccaneer's Den and Mintain.

Official updates also show:
- player vendors
- vendor wages
- sales history
- taxes
- direct player-trade tax
- market search systems

Around 2010, documented trade/vendor taxes were 10%; by 2016 a trade tax was reduced to 5%.

**Bot rule:** economy must support direct secure trade + player vendors + market search, not NPC-only commerce.

---

# 15. Player Service Economy

Forum evidence shows players sold **services** too.

A 2016 post offered a training character for hourly rent, with special skill setup and consumables supplied for longer sessions.

Source:
https://www.revolutionuo.net/forum/index.php?topic=94914.0

Future bot services:
- training partner
- crafting
- repair
- potion supplier
- mount supplier
- escort
- treasure help
- healer/resurrector
- Runebook copying


# 16. Fishing Economy

Official guide:
https://www.revolutionuo.net/oyun_rehberi

Verified mechanics:
- fish can be eaten
- fish can be sold to NPCs
- fish can be sold to players
- Fishing 80 was required for nets in the documented guide
- rod/net fishing could lead to S.O.S.-related treasure

## Shell → Net
Official history confirms:
- Shell comes from fishing
- Shell is used for fishing nets
- Tailoring makes nets

Forum experiment:
https://www.revolutionuo.net/forum/index.php?topic=76853.0

A player tested many nets and recorded:
- very large fish output
- Shell drops
- an S.O.S. Bottle
- better results when the ship was moving / timing changed

Forum economy discussion:
https://www.revolutionuo.net/forum/index.php?topic=53536.15

One period's players discussed approximate economics around:
- ~2k fish value from a net
- ~1.5k net purchase price
- ~500gp margin before rare outcomes

These prices are observations, not permanent constants.

**Future Fisher lifecycle:**
```text
rod fish
→ sell fish
→ accumulate Shell
→ make/buy nets
→ net fish
→ obtain S.O.S.
→ treasure/encounters
→ sell loot
→ upgrade wealth/equipment/travel
```

---

# 17. Mining → Smithing Economy

Historical skill guide:
- Mining gains through mining and smelting.

Official updates:
- ore frequencies changed
- ore weight changed
- mining speed changed
- some ore types were removed/reintroduced by era
- mining could spawn ore-related elemental creatures
- mining later had an S.O.S. Bottle chance in at least one period

This produces a real chain:
```text
mine
→ ore
→ smelt
→ ingot
→ craft / sell
```

A miner may sell ingots to another Smith, or a Mining+Blacksmithy hybrid may process its own resources.

**Do not hardcode the ore table until the target era is selected.**

---

# 18. Blacksmithy

Forum training route:
- Dagger to around 70
- Short Spear after that

Historical Revolution also had:
- colored ore/set economies
- magical weapon systems
- Black Staff enchant/combination features
- durability and repair concerns

Future Smith decisions:
- craft for skill gain
- craft for a customer
- repair
- keep an upgrade
- sell item to player
- sell excess material
- buy ore from a Miner if more efficient

---

# 19. Tailoring / Special Leather

Official guide confirms:
- cloth crafting
- high-level Mage/Special Robes
- fishing nets
- special leather armor

Special leather chain included:
- hides from strong creatures
- processing with Spirit of Nitre
- Alchemy input
- Tailoring output

Cross-player chain:
```text
PvM hunter → special hides/crystals
Alchemist → processing potion
Tailor → robe/leather set
PvPer/Mage → buys finished equipment
```

This should generate real inter-bot trade.

---

# 20. Tinkering

Official guide connects high Tinkering with:
- Trapped Pouches
- Golems

Golem production required special world/PvM components plus metal resources.

Player marketplace updates explicitly added Trapped Pouches and Golems to searchable trade.

**PvP implication:** Trapped Pouches create recurring Tinkerer demand.

---

# 21. Inscription & Runebook Economy

Official guide:
https://www.revolutionuo.net/oyun_rehberi

Inscription:
- creates scrolls/spellbooks
- was used to earn money
- contributes to defensive magic in historical rules
- at high skill creates Runebooks
- can copy filled Runebooks into empty ones

Forum training guide:
- Recall Scroll around 60–80
- Resurrection Scroll around 80–100

This means an Inscription player can supply:
- spell access
- Recall travel resources
- Runebooks
- Runebook copying

---

# 22. Runebooks — Historically Verified, Not Optional

This is a critical finding.

The current Revolution Offline runtime audit found weak/non-functional Runebook support, but official Revolution history proves **custom Runebooks were real**.

Primary source:
https://www.revolutionuo.net/guncellemeler

Historical features:

## 2008
- Runebook travel to water blocked
- blocked destination/player/wall checks
- Runebook exploit fixes
- Runebooks searchable through player-vendor cooperative

## 2009
- pages increased to 8
- per-page names
- page-aware rune insertion
- page transfer between books
- Runebook copying added to Inscription
- Runebook charging through Recall Scrolls
- charged use could bypass Magery requirement

## 2011
- Guild Runebook added
- obtained from guild stone
- member books mirrored the guild leader's book
- leader changes propagated to members

### Reconstruction status
**Historical Revolution:** PASS / very strong evidence.  
**Current reconstructed Source-X:** missing compatibility feature.

**Action:** treat Runebook as a high-priority M3.5 authenticity restoration item.

---

# 23. Mark / Recall

Mark/Recall were real and central to player travel.

Official update history shows Recall reagent rules changed by era:
- one 2009 version required multiple Mandrake Root, Blood Moss and Black Pearl
- later values were reduced
- Gate Travel resource rules also changed

**Never mix Recall costs across years.**

Future planner:
```text
destination
→ own marked rune/Runebook entry?
→ sufficient skill/resources/charge?
→ Recall if legitimate and useful
→ otherwise moongate/world route
```

No fake teleport.

---

# 24. Reagent Crystal / Store Crystal / Loadout Prep

Official updates document a Reagent Crystal that could:
- store reagents
- select destination bag
- top carried reagent counts toward targets
- return excess supplies

Later Store Crystal systems:
- stored bulk supplies
- stored potions
- created quick shortcuts for loadout preparation

**Bot implication:** serious PvP/Mage bots should eventually return home/bank and prepare a real loadout rather than purchasing tiny quantities every trip.

---

# 25. Mounts and Spawntakip

Official system page:
https://www.revolutionuo.net/spawntakip_sistemi

One documented weekly schedule:
- 1 elemental Steed
- 1 Nightmare
- 1 Unicorn
- 2 Kii Rin
- 5 Mustang
- 5 Shire
- 7 Frenzied Ostard
- 7 Mid Ostard
- 10 Forest Ostard
- 10 Desert Ostard

Total: 49 per week.

System behavior:
- schedule showed spawn timing
- valuable mount location required world searching
- mount had protectors/guards
- bot/player needed enough Animal Taming
- system tracked future/spawned/tamed/dead state
- resolved spawn location could later be exposed

Future Tamer:
```text
check schedule
→ choose target
→ search world
→ find mount/guards
→ protect mount while handling guards
→ tame
→ use/stable/sell
```

Bots must not use hidden admin coordinates.

Other official updates show:
- special mounts could add armor
- horse price changed over time
- pack horses/llamas sold by trainers
- some special mounts were added/removed by era

---

# 26. Anti-Macro and Unattended Play

Official rules/updates prove Revolution fought unattended macroing.

Timeline examples:
- 2008: simple macro prevention
- later anti-macro modules
- 2011-style production verification every few hours
- 2016: failure to answer code within about a minute could disconnect player
- multi-client rules required human responsiveness

Sources:
- https://www.revolutionuo.net/guncellemeler
- https://www.revolutionuo.net/genel_kurallar

**Bot design:** skill farming should not become an infinite perfect macro machine.

For an offline bot simulation, we can later model anti-macro pressure without requiring a human CAPTCHA, but only after the chosen-era rules are understood.

---

# 27. Head Hunters — Criminal Economy

Official system:
https://www.revolutionuo.net/head_hunters

Documented behavior:
- experienced murderers could join
- innocent-player heads could be delivered for gold
- payout related to victim fame / hunter experience
- players could purchase assassination contracts
- contracts could place a reward on another player
- Head Hunters completed these jobs

Official updates also document monthly ranking rewards and system resets.

Future criminal bot:
```text
build murderer status
→ join Head Hunters
→ hunt eligible target/contract
→ collect head
→ deliver
→ earn gold
→ restock PvP supplies
```

This creates a real PK economy rather than meaningless random murder.

---

# 28. Criminal / Karma / Kill Count

Forum example:
https://www.revolutionuo.net/forum/index.php?topic=11714.0

Players discussed:
- negative karma
- PK status
- monster killing to recover karma
- poison-related consequences
- difficulty becoming blue/innocent again

Official updates show kill-count decay and pardon systems changed by era.

Future PvP logic needs:
```text
innocent
criminal
murderer
kill_count
karma
fame
guard_zone_risk
guild/war exemptions
```

---

# 29. Guilds and Social Life

Evidence includes:
- guild wars
- Order/Chaos
- guildonline lists
- tracking guildmates
- guild-member death notifications including location/killer
- guild colors/flags
- guild Runebooks
- guild house access
- war cooldowns
- player recruitment based on build and social reputation

Forum guild culture:
https://www.revolutionuo.net/forum/index.php?topic=64321.0

Player-created guild rules included:
- no attacking AFK/macro players
- behavior after death
- respect toward enemies
- craft characters sometimes kept separate from active war rosters

Future bots should eventually support:
- guild allies/enemies
- shared travel knowledge
- requests for help
- group PvP
- shared economy
- social reputation

---

# 30. Housing / Home

Official updates document:
- house placement rules
- location-dependent upkeep
- permissions/friends
- guild access
- player home registration
- `home home home`
- property resale
- house-linked vendors
- later safe-box systems

Future bot home behavior:
```text
return home
→ store loot
→ restock
→ craft
→ prepare PvP kit
→ meet/trade
→ manage vendor
→ maintain travel book
```

Home is infrastructure, not just spawn coordinates.

---

# 31. Treasure Hunting / PvM

Official guide and update history support:
- Cartography
- Lockpicking
- treasure maps
- levelled treasure chests
- fixed chests
- guardians
- magical weapons
- gold
- rare materials

Loot was repeatedly adjusted.

Forum Treasure Hunter builds:
https://www.revolutionuo.net/forum/index.php?topic=66975.0

Players used hybrid combinations such as:
- Lockpicking
- Cartography
- Magery
- Eval Int
- Meditation
- Poisoning
- Healing/Anatomy
- sometimes Mining/Alchemy/Inscription

Strong monsters such as Dragon, Balron, Infernal and others supplied:
- gold
- magical weapons
- treasure maps
- special hides
- crystals
- rare crafting inputs

Future PvM economy:
```text
hunter loots magical weapon
→ Item ID
→ keep upgrade OR sell
special hide → tailor
crystal → robe crafter
map → treasure hunter
```

---

# 32. Begging

Official guide and project memory treat Begging as a legitimate low-level income method.

This is ideal for:
- poor new character
- no tool requirement
- low risk
- low income ceiling

It gives early bots different paths to first capital.

---

# 33. Resource Pressure

Never give bots infinite:
- gold
- reagents
- bandages
- bottles
- potions/kegs
- tools
- ore/ingots
- cloth/logs
- ammo
- equipment durability
- mounts

Authentic progression loop:
```text
want skill gain
→ training needs consumables
→ consumables cost gold/resources
→ choose income
→ earn
→ buy/trade
→ train
```

---

# 34. Bot-to-Bot Economy

Player demand should create trade.

Example:
```text
PvPer needs Greater Cure keg
→ check own Alchemy/resources
→ if inconvenient or insufficient:
   find known alchemist/crafter
→ compare price/distance
→ rendezvous
→ secure trade
→ verify keg/gold
→ return to PvP
```

Natural chains:
- Miner → Smith
- Smith → Warrior/PvPer
- Fisher → NPC/player buyer
- Alchemist → PvPer/Mage
- Tailor → Mage/PvPer
- Tinkerer → PvPer
- Tamer → mount buyer
- PvMer/Treasure Hunter → crafter/collector

A single hybrid character can occupy several roles.

Future prices should consider:
- NPC baseline
- recent player trades
- rarity
- quality
- supply/demand
- location
- urgency
- wealth
- relationship/reputation
- production time
- tax/travel cost

No ML is necessary.

---

# 35. Corpse Recovery

Project-owner requirement:

After resurrection, bot should evaluate:
- corpse location/age
- item value
- nearby mobs/PKs
- current HP/mana
- current equipment
- route/travel time
- Recall/moongate options
- repeat-death probability

Possible outcomes:
- recover now
- restock first
- ask allies
- return later
- abandon low-value corpse

Never:
`die → resurrect → run back → die → repeat forever`.

---

# 36. War/Peace and Human-Like Movement

Bot rules:
- Enter War only for legitimate combat intent
- Exit War when target/combat is gone
- banking/travel/crafting/gathering should not inherit stale War Mode
- use a bounded stale-War watchdog
- normal travel is RUN + PEACE

Route realism:
- do not make all bots walk exactly the same path
- controlled route variation, not ML
- alternate lanes/approaches/entrances
- crowd avoidance
- personal route preferences
- bounded noise only

---

# 37. Future Need/Utility Model

Useful need categories:

```text
SURVIVAL: HP, poison, mana, safety
SUPPLY: bandages, reagents, kegs, ammo, tools
PROGRESSION: skill/stat goals, trainer, training resources
ECONOMY: gold, sale inventory, purchase/crafting need
EQUIPMENT: weapon, armor, robe, durability, mount
TRAVEL: destination, moongate, rune, Runebook, home
SOCIAL: trader, guild, ally, training partner
RISK: criminal state, PKs, hostile mobs, corpse
```

Use Utility AI / GOAP / state machines. ML is optional and not needed for core gameplay.

---

# 38. Example Bot Lifecycles

## New character
```text
spawn
→ inspect starting build/gold
→ use Teaching if worthwhile
→ buy basic supplies/tools
→ choose low-risk income
→ train
→ bank
→ develop hybrid build
```

## Mage/Warlock
```text
check reagents
→ finance restock
→ train current Magery band
→ manage mana/Eval
→ acquire scroll/spell access
→ obtain robes
→ develop Rune/Runebook network
→ PvM/PvP/trade
```

## Miner + Smith + Alchemist hybrid
```text
mine
→ smelt
→ craft skill items/orders
→ make own potion supply where useful
→ sell excess
→ buy what is inefficient to self-produce
```

## Fisher
```text
rod fish
→ sell
→ Shell
→ net
→ S.O.S.
→ treasure
→ market loot
```

## Tamer
```text
Spawntakip
→ hunt mount
→ handle protectors
→ tame
→ stable/use/sell
```

## PvPer
```text
repair/restock
→ bandages
→ reagents
→ Cure/Heal/Refresh/Explosion
→ trapped pouches
→ ammo
→ mount
→ guild/rendezvous
→ fight
→ loot/recover
→ resupply
```

## Head Hunter
```text
maintain murderer build
→ contract/head hunt
→ kill
→ head turn-in
→ gold
→ restock
```


# 39. Implementation Roadmap from Historical Evidence

## M3 — Progression & Economy Truth
Priorities:
1. choose target Revolution era profile
2. audit actual enabled skills/caps/stats
3. verify NPC Teaching
4. verify historical training bands live
5. implement one real income loop
6. implement secure bot-to-bot trade
7. progress Magery honestly
8. unlock Mark/Recall honestly
9. track real resource depletion
10. update `REVOLUTION_GAMEPLAY_TRUTH.md`

## M3.5 — Revolution Authenticity Restoration
High-confidence historical systems to compare with current runtime:
1. **Runebooks** — historical system definitely existed
2. Mage/Special Robes
3. Reagent Crystal
4. Store Crystal / loadout shortcuts
5. Vendor Cooperative / player vendors
6. Head Hunters
7. Spawntakip / mount ecosystem
8. Fishing/Shell/net/S.O.S.
9. anti-macro
10. treasure/chest rules
11. custom crafting/resource chains
12. period-correct PvP formulas

## M4+
Only after the ruleset is stable:
- autonomous skill progression
- autonomous hybrid professions
- dynamic player economy
- PvP supply procurement
- mount hunting
- treasure hunting
- guild/group behavior
- corpse recovery
- social systems
- population scheduling

---

# 40. Current High-Confidence Compatibility Gaps

## Runebooks — HIGH CONFIDENCE
Historical Revolution: definitely present and heavily customized.  
Current reconstruction: weak/non-functional native support.

**Conclusion:** missing Revolution-specific system; do not dismiss player memory.

## NPC Teaching — CONFIRMED SYSTEM, NUMBERS OPEN
Teaching existed. Exact target-era cap/cost remains to verify.

## Skill/stat cap
7x culture is strong evidence, but exact numeric runtime cap should be measured.

## Poison/Magery restrictions
Changed over time; select target era.

## Anti-macro
Changed over time; select target era.

## Ores/logs/leathers
Changed over time; do not merge all historical resource sets.

## Loot/gold
Frequently rebalanced; must be era-specific.

---

# 41. Canonical Bot Design Rules Supported by Current Research

1. Bots are normal UO players through the real client protocol.
2. Characters are builds, not rigid classes.
3. Hybrid characters are normal.
4. 7x-style deliberate builds were part of Revolution culture.
5. Resisting Spells should not be assumed active.
6. Skill farming should use real historical training actions.
7. NPC Teaching existed and should be an early-game option when verified.
8. Players normally ran.
9. Navigation should have bounded per-bot route variation.
10. Bots should exit stale War Mode.
11. Weapon choice includes special moves/meta, not just damage.
12. Bandages/potions/reagents/ammo are recurring real needs.
13. PvP requires preparation/restocking.
14. Greater Cure keg demand should create real crafter/PvPer trade.
15. Player-to-player economy is central.
16. Player vendors/market search existed.
17. Player services existed.
18. Fishing is a multi-stage economy, not “cast rod forever.”
19. Mining feeds Smithing and can create danger.
20. Tailoring/Alchemy/PvM form cross-profession chains.
21. Runebooks definitely existed.
22. Mark/Recall must remain legitimate skill/resource-gated travel.
23. Mount hunting was a world-scale activity.
24. Head Hunters created a murderer/bounty economy.
25. Fire Robe is a real gameplay/progression/economy object.
26. Guilds influence combat, information, trade and travel.
27. Repetitive skill farming should respect anti-macro authenticity.
28. Corpse recovery should be risk/value aware.
29. Bot needs should drive trade and activities.
30. Era selection is mandatory before locking numeric rules.

---

# 42. Primary Official Sources

### RevolutionUO Gameplay Guide
https://www.revolutionuo.net/oyun_rehberi

Use for:
- skill descriptions
- inactive skills
- Eval Int
- Healing
- Fishing
- Inscription/Runebooks
- Tailoring
- Tinkering
- combat mechanics

### Official Update Archive
https://www.revolutionuo.net/guncellemeler

Use for:
- chronological rule changes
- Runebooks/Recall
- player economy/taxes
- anti-macro
- Head Hunters
- housing/guilds
- mounts
- crafting
- loot
- robes
- resources
- combat balance

### Special Mage Robes
https://www.revolutionuo.net/ozel_mage_robe

### Spawntakip
https://www.revolutionuo.net/spawntakip_sistemi

### Tezgahtarlar Kooperatifi
https://www.revolutionuo.net/tezgahtarlar_kooperatifi

### Head Hunters
https://www.revolutionuo.net/head_hunters

### General Rules
https://www.revolutionuo.net/genel_kurallar

---

# 43. Key Forum Sources

### Skill training guide
https://www.revolutionuo.net/forum/index.php?topic=59111.0

### Warlock builds / Teaching reference
https://www.revolutionuo.net/forum/index.php/topic,23720.0.html

### 3-combat / 7x warlock
https://www.revolutionuo.net/forum/index.php?topic=43700.0

### Warrior/warlock builds + PvP loadout + running
https://www.revolutionuo.net/forum/index.php?topic=33941.20%3Bwap2

### 2010 warlock build/playstyle discussion
https://www.revolutionuo.net/forum/index.php?topic=77029.0

### Weapon meta
https://www.revolutionuo.net/forum/index.php?topic=23379.0

### Fishing-net experiment
https://www.revolutionuo.net/forum/index.php?topic=76853.0

### Fishing economy
https://www.revolutionuo.net/forum/index.php?topic=53536.15

### Shell acquisition
https://www.revolutionuo.net/forum/index.php?topic=73399.0

### Special robe crafting
https://www.revolutionuo.net/forum/index.php?topic=84687.0

### Treasure Hunter builds
https://www.revolutionuo.net/forum/index.php?topic=66975.0

### Treasure chest discussion
https://www.revolutionuo.net/forum/index.php?topic=57593.0

### Player marketplace + training-character rental
https://www.revolutionuo.net/forum/index.php?topic=94914.0

### Karma / PK
https://www.revolutionuo.net/forum/index.php?topic=11714.0

### Guild culture
https://www.revolutionuo.net/forum/index.php?topic=64321.0

---

# 44. Highest-Value Research Backlog for v2

The Revolution forum/archive is large; this v1 is intentionally a strong foundation, not a claim that every historical thread has been exhausted.

Next research pass should target:

- exact NPC Teaching cap/cost/formula
- exact Revolution10 total skill/stat caps
- Fire Robe acquisition/item IDs
- Runebook recipe/item IDs/gump behavior
- Reagent Crystal acquisition/recipe
- potion keg recipe/capacity/use
- 2008–2010 ore table
- special wood table
- special leather table
- mount Taming requirements
- Head Hunter payout formula
- treasure map level requirements
- exact fishing S.O.S. rules
- Shell/net recipe by era
- player vendor deed/upkeep
- target-era housing costs
- target-era anti-macro timings
- historical custom moongate destinations
- Zanaatkarlar city
- target-era spell delays
- special-move probabilities
- special robe durability/protection formula
- magical weapon drop chances
- corpse/loot timers
- murder-count decay
- stat gain mechanics
- Order/Chaos/guild rewards
- duel/10v10 mechanics
- reward/rare dye systems

---

# 45. Recommended Machine-Readable Rules Profile

Before large autonomous bot work, derive a versioned rules profile from verified research.

Example concept:

```yaml
profile: revolution_2009_2010

skills:
  resisting_spells:
    enabled: false

training:
  magery:
    source: FORUM_GUIDE
    bands:
      - {min: 0, max: 30, action: night_sight}
      - {min: 30, max: 40, action: bless}
      - {min: 40, max: 60, action: greater_heal}

travel:
  runebook:
    enabled: true
    pages: 8
    evidence: OFFICIAL_UPDATE
  recall:
    reagent_rule: ERA_SPECIFIC

economy:
  player_vendors: true
  vendor_cooperative: true
  direct_trade: true

bots:
  default_gait: run
  hybrid_builds: true
  need_driven_trade: true
  route_variation: bounded
  stale_war_mode_cleanup: true
```

Do not encode uncertain values until verified.

---

# 46. Bottom Line

The historical RevolutionUO archive strongly supports the core design direction of Revolution Offline.

Revolution players:
- built deliberate hybrid/7x characters
- trained skills through specific grind methods
- spent money/resources to progress
- normally ran
- prepared combat loadouts
- depended on bandages, reagents, potion kegs and equipment
- fished, mined, crafted, hunted treasure and farmed PvM
- bought and sold to other players
- used player vendors and market search
- sold services
- used moongates, Mark, Recall and **custom Runebooks**
- hunted rare mounts
- pursued Head Hunter/PK systems
- crafted and traded special robes including Fire Robe
- joined guilds and participated in a socially connected player economy
- lived under anti-macro and anti-abuse constraints

The bot target should therefore not be:

> NPC + profession script

It should be:

> **A simulated RevolutionUO player with a real build, real inventory, real money, real skill progression, real travel knowledge, real relationships, real economic needs and player-like goals constrained by the same shard systems.**

---

**Document status:** v1 — suitable as an implementation/research baseline.  
**Next recommended action:** choose the exact Revolution era/profile, then expand v2 with target-era numeric rules and direct script/runtime cross-checks.
