# Revolution NPC Vendor Matrix

Date: 2026-08-27 (M3.7 Phase 1). Companion to `M3_7_RESOURCE_ECONOMY.md`.

**The question this document exists to answer:** for every item a stock Sphere
NPC will sell a player, is that a Revolution-authentic source, or is it a
shortcut around a chain Revolution made players walk?

Extracted mechanically from the live runtime
(`runtime/scripts/npcs/c_vendor_human.scp` → `templates/tm_vend.scp`), then
graded against the RevolutionUO archive. Nothing here is memory.

---

## 0. Headline

> **Stock Sphere NPC vendors sell nearly the entire raw and intermediate
> production chain.** A bot with gold can buy iron ingots, logs, boards, wool,
> yarn, thread, cloth, bolts of cloth, cotton, flax, hides, blank scrolls,
> bottles, feathers, arrows, nails, gears and all twenty-six reagents — and
> never gather, smelt, spin, weave or chop anything.

That is the shortcut M3.7 exists to close. It is closed in **bot policy**, not
by editing the shard (§6).

| | |
|---|---|
| Vendor CHARDEFs defined | **203** (67 human, 69 elf, 67 gargoyle) |
| Vendor CHARDEFs reachable | **67** — elf and gargoyle vendors appear in **no** `[SPAWN]` group |
| Human professions | **34**, of which **32 work** (§2.1) |
| Distinct items on a working human vendor | **608** |
| Vendor templates in `tm_vend.scp` | 94 |

### Classification of all 608

| Class | Count | Meaning |
|---|---:|---|
| `PLAYER_CRAFTED` | **235** | craftable in a live legacy skill menu — an NPC selling it competes with players |
| `UNKNOWN` | **177** | no Revolution evidence either way. Fail safe |
| `STOCK_SPHERE_ONLY` | **103** | on a stock vendor, no Revolution evidence, not craftable |
| `PLAYER_CRAFTED_NO_MENU` | **49** | has `SKILLMAKE` but is in **no** live menu — uncraftable *and* purchasable |
| `WORLD_GATHERED` | **21** | a gathering skill produces it from the world |
| `ERA_CONFLICT` | **18** | belongs to a later era than `revolution_2009_2010` |
| `PLAYER_MARKET_GOOD` | **5** | RevolutionUO's own cooperative listed it as a player good |

**284 of 608 (47 %) are goods a player can make** — `PLAYER_CRAFTED` plus
`PLAYER_CRAFTED_NO_MENU`. Every one of those is a crafter's market an NPC is
currently undercutting.

---

## 1. Method

```
c_vendor_human.scp  [CHARDEF]  ON=@NPCRestock  SELL=<template> / BUY=<template>
        │
        └──►  tm_vend.scp  [TEMPLATE VENDOR_S_x]  SELL=<item>,{min max}
                           [TEMPLATE VENDOR_B_x]  BUY=<item>,{min max}
```

`VENDOR_S_*` is what the vendor **sells to** a player; `VENDOR_B_*` is what it
**buys from** one. Confirmed live in M3: `VENDOR_B_FISHER` lists whole fish and
Shika paid 1 gp each for four of them.

Each item was then joined to its `ITEMDEF` (`VALUE`, `WEIGHT`, `TYPE`,
`RESOURCES`, `SKILLMAKE`) and to the **live** legacy skill menus
(`crafting/interface/legacy skillmenu/`, which are what this runtime uses —
`crafting_settings.scp` sets `scp.CraftingSystem=0`, so the newer
`def_*.scp` catalogues are dead).

### 1.1 Grading vocabulary

| Grade | Rule applied |
|---|---|
| `REVOLUTION_NPC_VERIFIED` | a dated Revolution entry says an NPC sold it |
| `PLAYER_MARKET_GOOD` | a Revolution cooperative category names it |
| `WORLD_GATHERED` | a gathering skill produces it (engine or region table) |
| `PLAYER_CRAFTED` | reachable in a live skill menu on this runtime |
| `ERA_CONFLICT` | the item's system postdates 2011-02-17 |
| `STOCK_SPHERE_ONLY` | present only because Scripts-X ships it |
| `UNKNOWN` | **the default.** No guessing |

---

## 2. The 34 human professions

Spawned via `spawns/spawns_city.scp`, which references only human vendors.

| Profession | sells | buys | Note |
|---|---:|---:|---|
| alchemist | 18 | 63 | reagents, bottles, potions |
| animaltrainer | 16 | 8 | **pack horse / pack llama are `REVOLUTION_NPC_VERIFIED`** (03.11.2010) |
| architect | 21 | 21 | house deeds |
| armorer | 41 | 41 | five armour templates |
| baker | 11 | 12 | |
| blacksmith | **2** | **229** | sells only ingots and tongs; buys almost every metal good |
| bowyer | 7 | 8 | feathers, arrows, bows |
| butcher | 10 | 10 | |
| carpenter | 18 | 40 | **sells logs and boards** |
| cobbler | 4 | 9 | |
| cook | 19 | 34 | the only buyer of raw fish steaks (M3) |
| farmer | 21 | 3 | |
| fisher | 7 | 5 | sells the pole; buys whole fish only |
| furtrader | 2 | 21 | **sells hides** |
| **glassblower** | 0 | 0 | **BROKEN — §2.1** |
| healer | 5 | 6 | also sells four reagents |
| innkeeper | 37 | 30 | |
| jeweler | 21 | 20 | |
| mageshop | 33 | 32 | **26 reagents, blank scrolls, blank runes, circles 1–4** |
| mapmaker | 3 | 7 | also sells blank scrolls |
| provis (provisioner) | 45 | 75 | the general store |
| **rancher** | 0 | 0 | **BROKEN — §2.1** |
| shepherd | 2 | 19 | **sells wool** |
| shipwright | 6 | 6 | |
| tailor | 36 | 34 | **sells cloth, bolts, thread, cotton, flax** |
| tanner | 6 | 10 | |
| tavernkeeper | 27 | 27 | |
| tinker | 41 | 44 | **sells ingots, boards, nails, gears, tools** |
| vegiseller | 21 | 21 | |
| vet | 9 | 9 | |
| waiter | 22 | 6 | |
| weaponsmith_blade | 27 | 174 | |
| weaponsmith_blunt | 12 | 159 | |
| weaver | 6 | 5 | **sells yarn, cloth, bolts** |

### 2.1 Two professions are defined and broken

Found live, in the server's own log, during the M3.7 decorator run:

```
01:32:ERROR:(c_vendor_human.scp,3667)Undefined symbol 'VENDOR_S_RANCHER'
01:32:ERROR:(c_vendor_human.scp,3668)Undefined symbol 'VENDOR_B_RANCHER'
```

`c_rancher` and `c_glassblower` both reference vendor templates that **do not
exist anywhere in Scripts-X**. Both throw on every restock and keep no shop.

This is not cosmetic. In stock UO the **glassblower is the bottle vendor**, and
bottles gate the whole of Alchemy. On this runtime bottles survive only because
the alchemist and the provisioner also stock `i_bottle_empty`, and because
`SKILLMAKE=Alchemy 25.0, t_glassblowing_tool` makes them craftable.

Recorded as a **runtime defect**, not a Revolution rule. No fix applied: an
absent vendor is *more* authentic than an invented one, and M3.7 does not
modify Scripts-X.

### 2.2 136 vendor CHARDEFs are unreachable

`c_vendor_elf.scp` (69) and `c_vendor_gargoyle.scp` (67) define full vendor
sets. **No `[SPAWN]` group references any of them**, so none can appear in the
world. They are dead definitions, and they are also `ERA_CONFLICT` by
construction — playable gargoyles are Stygian Abyss (2009) and elves are
Mondain's Legacy (2005), neither of which this Renaissance-era client can
render.

Left alone. A bot that never meets them cannot be corrupted by them.

---

## 3. The matrix — economically load-bearing items

`SELL[]` is who sells it to a player. Broken professions are excluded.

| Item | Current Source-X vendor | Historical Revolution source | Target class | Action |
|---|---|---|---|---|
| `i_ore_iron` | **nobody** | Mining (`/oyun_rehberi`) | `WORLD_GATHERED` | **correct already** |
| `i_ingot_iron` | SELL[blacksmith, tinker] | cooperative "iron ingot" category, 08.11.2008 | `PLAYER_MARKET_GOOD` | **block NPC purchase** |
| `i_log` | SELL[carpenter] | Lumberjacking; cooperative "log" category | `WORLD_GATHERED` + `PLAYER_MARKET_GOOD` | **block NPC purchase** |
| `i_board` | SELL[carpenter, tinker] | Carpentry from logs | `PLAYER_CRAFTED` | **block NPC purchase** |
| `i_wool` | SELL[shepherd] | shear a sheep | `WORLD_GATHERED` | **block NPC purchase** |
| `i_yarn_ball` | SELL[weaver] | 1 wool on a spinning wheel → 3 yarn | `WORLD_PROCESSED` | **block NPC purchase** |
| `i_thread` | SELL[tailor] | 1 cotton on a spinning wheel → 6 thread | `WORLD_PROCESSED` | **block NPC purchase** |
| `i_cloth` | SELL[tailor, weaver] | bolt cut with scissors → 50 cloth | `WORLD_PROCESSED` | **block NPC purchase** |
| `i_cloth_bolt` | SELL[tailor, weaver] | cooperative "Cloth of bolt", 08.11.2008 | `PLAYER_MARKET_GOOD` | **block NPC purchase** |
| `i_cotton` | SELL[tailor] | cotton crop | `WORLD_GATHERED` | **block NPC purchase** |
| `i_flax_bundle` | SELL[tailor] | flax crop | `WORLD_GATHERED` | **block NPC purchase** |
| `i_hide` | SELL[furtrader] | carve an animal corpse | `WORLD_GATHERED` | **block NPC purchase** |
| `i_hides_cut` | — (BUY only) | cut hides with scissors | `WORLD_PROCESSED` | correct |
| `i_scroll_blank` | SELL[mageshop, mapmaker] | **UNKNOWN** | `UNKNOWN` | **fail safe — block** |
| `i_bottle_empty` | SELL[alchemist, provis] | **UNKNOWN** | `UNKNOWN` | **fail safe — block** |
| `i_feather` | SELL[bowyer] | bird flocks (forum 54978, weak) | `WORLD_GATHERED` | **block NPC purchase** |
| `i_arrow` | SELL[bowyer, provis] | cooperative "arrow" category, 08.11.2008 | `PLAYER_MARKET_GOOD` | **§3.2 — allowed** |
| `i_xbolt` | — | cooperative "crossbow bolt" | `PLAYER_MARKET_GOOD` | §3.2 |
| `i_nails` | SELL[carpenter, tinker] | Tinkering (Revolution training band) | `PLAYER_CRAFTED_NO_MENU` | **§3.3** |
| `i_gears` | SELL[tinker] | Tinkering | `PLAYER_CRAFTED` | block |
| 8 Magery reagents | SELL[alchemist, healer, mageshop, provis] | **UNKNOWN** — §4 | `UNKNOWN` | **§4** |
| 18 Necromancy reagents | SELL[mageshop] | post-AoS | `ERA_CONFLICT` | **block** |
| `i_rune_marker` | SELL[mageshop] | Revolution runes are marked, not bought | `STOCK_SPHERE_ONLY` | block |
| pack horse / pack llama | SELL[animaltrainer] | **03.11.2010, verbatim** | `REVOLUTION_NPC_VERIFIED` | **allow** |

### 3.1 The one item with a dated Revolution NPC entry

> **03.11.2010** — *"Pack horse ve pack llama artık animal trainer
> tezgahtarları tarafından satılmaktadır."*
> (Pack horses and pack llamas are now sold by animal trainer vendors.)

This is the **only** `REVOLUTION_NPC_VERIFIED` row in the whole matrix, and it
is worth dwelling on: an update entry announcing that an NPC *started* selling
something in November 2010 is evidence that NPC stock lists were curated and
narrow, not that they were generous. It also lands 10 days inside the profile
window.

### 3.2 Ammunition is deliberately NOT blocked

Phase 8 asked whether NPC arrows are forbidden. The answer from the archive is
**no, and the reasoning is worth keeping**:

* The cooperative's "other" tab listed **arrow** and **crossbow bolt** as
  searchable player goods (08.11.2008) — so players sold ammunition.
* But `/oyun_rehberi` Bowcraft says: *"Yaptığınız yayları diğer oyunculara ya da
  **tezgahtarlara** satıp altın kazanabilirsiniz"* — you sell your bows to other
  players **or to NPC vendors**. NPCs are an explicit, documented sink.
* Nothing anywhere says NPCs stopped stocking basic ammunition.

A player market and an NPC floor **coexisted**. Blocking NPC arrows would
invent a restriction, which is the failure mode §1.1 forbids. Recorded as
`PLAYER_MARKET_GOOD` with NPC purchase **permitted**, and flagged for
re-examination if evidence appears.

### 3.3 49 items are purchasable but not craftable

The `PLAYER_CRAFTED_NO_MENU` class is the most interesting artefact of the
audit: items carrying a real `SKILLMAKE` that appear in **no live skill menu**,
so a player cannot make them — only buy them.

`i_nails` is the clean example. RevolutionUO's own training guide (forum topic
59111) prescribes:

> **Tinkering 0–42.1: "Spoon, Nails, Gears"**

Our live Tinkering menu offers **gears and lockpicks, and neither spoon nor
nails**, while `i_nails` (`Tinkering 5.0`) and `i_spoon` sit in the itemdefs
fully specified. The recipe exists; the door to it does not. Same story for
`i_barrel_lid` (`Tinkering 36.8`), which Revolution names as the Carpentry
0–95 training item.

**This is a menu gap, not a missing recipe**, and restoring the menu entries
adds nothing that Scripts-X did not already define. Carried into
`M3_7_RESOURCE_ECONOMY.md` as a scoped restoration; see also
`TNS_DONOR_AUDIT.md` §2.9, where a second Turkish Sphere shard's Tinkering
catalogue lists exactly these items and confirms they are ordinary for the era.

---

## 4. Reagents — the question M3.7 was told not to guess

The milestone was explicit: *"Do NOT assume ordinary spell/alchemy reagents were
or were not sold by NPCs. This remains an explicit historical research
question."*

### What was searched

`revolutionuo.net` `/oyun_rehberi`, `/oyuncu_komutlari`, `/genel_kurallar`,
`/tezgahtarlar_kooperatifi`, `/hazine_sistemi`, `/ozel_mage_robe`,
`/spawntakip_sistemi`, `/binek_bilgileri`, the full `/guncellemeler` changelog
(1200+ entries), and forum topics 59111 and 54978. Forum **search** requires a
login and was not available.

### What was found

| Fact | Date | Source |
|---|---|---|
| A **Reagent Crystal** existed, and players could designate which container reagents were withdrawn to | **07.11.2008** | `OFFICIAL_REVOLUTION_UPDATE` |
| It was renamed **Store Crystal** and extended to hold trapped pouches and fishing nets | 20.11.2010 | same |
| Recall's reagent cost was **reduced from 3 to 1** of each | 14.05.2009 | same |
| Gate Travel costs **6** each mandrake / black pearl / sulphurous ash | 14.05.2009 | same |
| Reagents are **not** a cooperative search category | 08.11.2008, 19.12.2008 | same |

### The verdict

**`UNKNOWN`, and deliberately so.**

The evidence proves a **real reagent economy** — you do not build a dedicated
storage item for a resource nobody accumulates, and you do not announce a
reduction from three to one unless the three were being consumed. It does
**not** establish where the reagents came from. Both readings survive:

* NPC mage shops stocked them, as in nearly every UO shard, and the crystal
  existed to hold bulk purchases;
* or reagent fields were gathered and the crystal held the harvest.

Two supporting observations, neither decisive:

* Reagents are absent from the cooperative categories, which lists cloth bolts,
  logs and ingots. That is *weak* evidence against a large player reagent
  market — but the cooperative was an index over player vendors, and absence
  from a search tab is not absence from the economy.
* Our own atlas already carries **31 reagent field resource areas** on map 0,
  derived from the shard's own spawner tables. Gathering is physically possible
  today.

**Consequence for bot policy:** `UNKNOWN` fails safe. An autonomous Revolution
bot may **not** silently buy reagents from an NPC; it must log an authenticity
gap and prefer the reagent fields. This is the single most consequential
`UNKNOWN` in the matrix, because Magery training consumes reagents continuously
— and it is recorded as a gap rather than resolved by invention.

**Separately, and not the same question:** `runtime/sphere.ini:1060` sets
`ReagentsRequired=0`, so *no spell on this runtime consumes a reagent at all*
(found M3.6). The historical sourcing question is moot in practice until that
conflict is addressed.

---

## 5. Era conflicts found on live vendors

| What | Count | Why it conflicts |
|---|---:|---|
| **Necromancy reagents on the mage shop** | **18** | batwing, blackmoor, blood spawn, blood vial, bone, brimstone, daemon bone, fertile dirt, dragon blood, executioner's cap, eye of newt, obsidian, pig iron, pumice, nox crystal, grave dust, dead wood, wyrm heart. Necromancy is skill 49; this client ships skills 0–48 and cannot display it |
| Elf / gargoyle vendor CHARDEFs | 136 | ML (2005) and SA (2009) races; unspawned, unrenderable |
| Craftable AoS/SA armour on armourer templates | — | not enumerated; `VENDOR_B_COLOURED_ARMOR` alone spans 320 lines |

The eighteen necromancy reagents are the sharpest instance of the whole
milestone's thesis: **stock Sphere vendor stock is not Revolution stock**, and a
bot that trusted it would stand in a 2010 Renaissance mage shop buying
Stygian-Abyss ingredients.

---

## 5.1 What a live vendor actually had in stock

Everything above is read from templates. M3.7's slice C put a bot in front of a
**spawned** Britain blacksmith and asked for a smith's hammer. The whole offer:

```
[VENDOR] offer from 0x00000AC5: 2 item(s)
  0x40002A4E iron ingots            x6   10 gp
  0x40002A4F tongs                  x16  15 gp
```

Two lines, and a new Blacksmith can use neither:

* **iron ingots** are `PLAYER_CRAFTED` — the policy refuses them, and it is
  right to: a player had just sold this same character ingots minutes earlier;
* **tongs** sit on equip layer 0 in Revolution's tiledata, so
  `LayerFind(LAYER_HAND1)` never finds them. They cannot be wielded, and
  `skill45_mining.scp` gates smelting on `<SRC.WEAPON.USESCUR>`.

**No smith's hammer**, though `i_hammer_smith` appears in several `tm_vend.scp`
BUY lists — template presence is not stock presence, and the matrix above should
be read with that caveat throughout.

Nor can a low-Tinkering smith make one:

```
i_hammer_smith  RESOURCES=6 i_ingot_iron,1 i_log
                SKILLMAKE=Tinkering 40.0,t_tinker_tools
```

It is listed in the **Blacksmithing** legacy menu as well as the Tinkering one,
but `SKILLMAKE` gates it at Tinkering 40.0 either way. A Blacksmith's first
hammer is therefore normally a **player-to-player purchase from a Tinker** —
an inter-player dependency that no reading of the templates would have revealed.

---

## 6. What is done about it

**Nothing is changed on the shard.** No Source-X modification, no Scripts-X
modification, no vendor stock edited.

The enforcement lives in the bot, as `CanUseNPCVendorFor(item, profile)`:

```
REVOLUTION_NPC_VERIFIED  -> allow
PLAYER_MARKET_GOOD       -> allow only where an NPC floor is documented (arrows, §3.2)
WORLD_GATHERED           -> refuse; gather it
WORLD_PROCESSED          -> refuse; process it
PLAYER_CRAFTED           -> refuse; craft it or buy from a player
STOCK_SPHERE_ONLY        -> refuse
ERA_CONFLICT             -> refuse
UNKNOWN                  -> refuse, and LOG AN AUTHENTICITY GAP
```

Two properties that matter more than the table:

1. **Refusing is not silent.** Every refusal names the item and its grade, so
   the list of things a bot cannot legitimately obtain is a *report*, not a
   mystery — and it is exactly the list of gaps still to research.
2. **The shard stays honest.** A human player on this shard can still buy wool
   from the shepherd. Only the autonomous Revolution bots are held to the
   reconstruction, which keeps the server a neutral fact and the authenticity
   claim falsifiable.

---

## 7. Open questions

| Question | State |
|---|---|
| Did Revolution mage shops sell the 8 Magery reagents? | **UNKNOWN** — §4. The most consequential gap |
| Did Revolution vendors sell blank scrolls or bottles? | **UNKNOWN**. Both gate a whole craft |
| Was `i_wool` on Revolution's shepherd? | **UNKNOWN**. Sheep are shearable, so a bot never needs it |
| Revolution's actual per-vendor stock lists | **not recoverable** from the archive. The guide describes skills, not shop inventories |
| Whether NPC prices moved with volume | **UNKNOWN** for Revolution. A second Turkish shard did exactly that (`TNS_DONOR_AUDIT.md` §2.4) |
| Ore / wood / leather tables per era | **UNKNOWN**; two dated ore-spawn changes exist (03.04.2009, 12.04.2009) but no table |
