# Revolution Production Chains

Date: 2026-08-27 (M3.7 Phases 2–15, 17). Companion to
`REVOLUTION_VENDOR_MATRIX.md` and `M3_7_RESOURCE_ECONOMY.md`.

Every economically relevant good, and the answer to one question:
**where does this come from?**

Numbers are from the live runtime — Source-X C++ or `runtime/scripts` — and are
labelled `ENGINE`, `SCRIPT`, `LIVE` or `REVOLUTION`. Where the runtime and the
RevolutionUO archive disagree, both are printed and the conflict is named.

---

## 0. Provenance vocabulary

| Class | Meaning |
|---|---|
| `WORLD_GATHERED` | a gathering skill takes it from terrain (`REGIONRESOURCE`) |
| `ANIMAL_HARVESTED` | taken from a living or dead creature |
| `WORLD_PROCESSED` | a physical station transforms it; no craft menu |
| `PLAYER_CRAFTED` | a skill menu with a `SKILLMAKE` gate |
| `PVM_DROP` | carved from, or looted from, a corpse |
| `TREASURE_DROP` | chest, S.O.S. or map |
| `NPC_VERIFIED` | a dated Revolution entry says an NPC sold it |
| `PLAYER_MARKET` | a Revolution cooperative category |
| `UNKNOWN` | not established. Fails safe |

---

## 1. The single most important runtime fact

> **A craft station must be a dynamic ITEM. A map static will not do.**

`CClient::Event_Target` resolves the target with `uid.ObjFind()`
(`CClientEvent.cpp:2481`). A map static has no UID, so `pObjTarg` is `nullptr`,
and in `OnTarg_Use_Item` the `IT_WOOL` / `IT_COTTON` / `IT_YARN` / `IT_THREAD`
cases all begin:

```cpp
if ( pItemTarg == nullptr )
    break;                    // -> falls through, nothing happens
```

**A static spinning wheel or loom is inert by construction.**

Blacksmithy is kinder: `CWorldMap::FindItemTypeNearby` (`CWorldMap.cpp:663`)
searches dynamics, then terrain, **then statics**, so a static forge would
work — for smelting and for the smith menu.

### 1.1 What this world actually had, before M3.7

| | Revolution map statics (map 0) | `sphereworld.scp` (9017 items) |
|---|---:|---:|
| forge | 2, neither near a town | **0** |
| anvil | 1 | **0** |
| spinning wheel | **0** | **0** |
| upright loom | 2 pieces | **0** |
| "loom bench" (decorative, not `t_loom`) | 12 | — |

**Tailoring and Blacksmithy were both unreachable on this shard**, and had been
through M1–M3.6. That is why M3 could record mining and smelting as
`SCRIPT_VERIFIED, not run` and never notice.

### 1.2 The fix was the shard's own, and it had never been run

Scripts-X ships the placements in its world decorator, and the decorator's
per-city functions had never been executed — only its `place_moongates` step
had (20 gates are in the save, and `m25_moongate` passed live in M2.5).

`functions/worldgen/decoration/felucca/city_britain_deco_felucca.scp`:

| | Britain, from the shard's own file |
|---|---|
| spinning wheels | 1545,1656,26 · 1390,1604,30 · **1473,1689,0** · 1475,1689,0 |
| upright looms | 1545,1660+1661,26 · 1392+1393,1601,30 · **1473+1474,1685,0** |
| forges | 1355,1776,15 · 1361,1574,30 · 1424,1558,30 · 1356,1784,15 · 1344,1776,15 |
| anvils | 1363,1574,30 · 1423,1556,30 · 1355,1786,21 · 1346,1782,21 · 1346,1774,21 |

Run at 01:26 on 2026-08-27. **No coordinate in this project was invented.**

**`LIVE`, after the pass** — `runtime/save/spherestatics.scp`, 5767 items:
8 forges (5 `i_forge`, 1 `i_forge_large`, 2 `i_forge_large_bellows`),
5 anvils, 4 spinning wheels, 6 loom pieces, 1361 doors, 58 signs.

The ground-level pair — spinning wheel `1473,1689,0` and loom `1473,1685,0` —
sits in the Britain tailor shop the atlas knows as `britain_tailor_2`
(1467,1686,0). Ground level, so no upper-storey pocket.

### 1.3 Why the items vanished from `sphereworld.scp`, and the command that saves them

Every decorator item carries `attr_static`, and Source-X routes those to a
**different file**:

```cpp
CSector::SaveSector   else if (!pItem->IsAttr(ATTR_STATIC))
                          r_WriteSafe(g_World.m_FileWorld);   // sphereworld.scp
CWorld::SaveStatics   if (!pItem->IsAttr(ATTR_STATIC)) continue;
                      r_WriteSafe(m_FileStatics);             // spherestatics.scp
```

So an ordinary `.save` writes **none** of them. And a bare `.savestatics` fails:

```
ERROR:Undefined keyword 'savestatics'.
'Admin' commands 'savestatics'=1        <- logs that a command was ISSUED, not that it worked
```

`SAVESTATICS` is `SV_SAVESTATICS` in `CServer::r_Verb` (`CServer.cpp:2205`) — a
**server** verb, not a character verb. The working form is **`.serv.savestatics`**.
Until that ran, ~6200 placed objects existed only in memory and would have been
lost on the next restart.

---

## 2. Textile — sheep → cloth

### 2.1 The chain, with engine citations

```
   sheep (c_sheep_woolly)
     │  bladed weapon on the sheep          ENGINE CClientTarg.cpp:1878
     ▼
   1 x i_wool           (t_wool, WEIGHT 3.0, VALUE 3)
     │  use wool on a DYNAMIC t_spinwheel   ENGINE CClientTarg.cpp:2053
     ▼
   3 x i_yarn_ball      (t_yarn, WEIGHT 0.2)
     │  use 4 yarn on a DYNAMIC t_loom      ENGINE CClientTarg.cpp:2186
     ▼
   1 x i_cloth_bolt     (t_cloth_bolt, WEIGHT 50.0, VALUE 150)
     │  scissors                            ENGINE CItem.cpp:4287
     ▼
  50 x i_cloth          (RESOURCES=50 i_cloth on the bolt)
     │  sewing kit on cloth -> sm_tailor_cloth
     ▼
   Tailoring products
```

### 2.2 Every measured number

| Step | Fact | Source |
|---|---|---|
| Shearing tool | **any bladed weapon** — `IT_WEAPON_AXE`, `_SWORD`, `_FENCE`, `_MACE_SHARP`, `IT_CARPENTRY_CHOP`. **Not** scissors, **not** a shepherd's crook | `ENGINE` CClientTarg.cpp:1866-1890 |
| Shear yield | **1** pile of wool | `ENGINE` |
| After shearing | `CREID_SHEEP` → `CREID_SHEEP_SHORN`; a second attempt says *"wait"* | `ENGINE` |
| Wool regrowth | **30 minutes** (`WoolGrowthTime=30`) | `SCRIPT` sphere.ini:399 |
| Kill + carve a sheep | **3 wool + 3 lamb legs** — a second, faster wool source | `SCRIPT` c_monster_classic.scp:4513 |
| Wool → yarn | 1 wool → **3** yarn. **No skill check at all** | `ENGINE` CClientTarg.cpp:2066 |
| Cotton → thread | 1 cotton → **6** thread. No skill check | `ENGINE` CClientTarg.cpp:2075 |
| Loom | **4** yarn *or* 4 thread → **1** bolt, accumulated over 4 uses in `m_itLoom` | `ENGINE` CClientTarg.cpp:2230 |
| Mixing | putting yarn on a loom holding thread **ejects** the thread first | `ENGINE` CClientTarg.cpp:2205 |
| Bolt → cloth | **50** cloth per bolt | `SCRIPT` `i_cloth_bolt RESOURCES=50 i_cloth` |
| Sheep spawners live | **60** | `LIVE` sphereworld.scp |

**Net: 2 shears → 6 yarn → 1 bolt (4 used) → 50 cloth.** Wool is not the
bottleneck; reaching a station is.

### 2.3 Yarn or thread? — answered

The milestone asked whether Revolution's intermediate was yarn or thread.
**Both exist, from different plants, and they are interchangeable at the loom:**

* **wool → yarn** (`i_yarn_ball`, from sheep)
* **cotton → thread** (`i_thread`, from the cotton crop)

`IT_YARN` and `IT_THREAD` share one `case` in the loom handler, so either
weaves cloth. Tailoring recipes then consume **`i_thread`** as the stitching
input (`i_sash` = 4 cloth + 1 thread), never yarn — so a wool-only tailor can
weave cloth but cannot sew it.

**A `SCRIPT` conflict worth recording:** `i_thread` carries
`RESOURCES=1 i_flax_bundle`, but `i_flax_bundle` has **no `TYPE`** — it is
`t_normal`, not `IT_COTTON` — so the engine will not spin it. The flax line is
decorative; thread comes from cotton. `i_thread` has no `SKILLMAKE` and is in no
menu, so that `RESOURCES` line is never executed.

### 2.4 Revolution's own Tailoring, against this

| Revolution says | Our runtime |
|---|---|
| *"kumaşla türlü eşyalar yapabilirsiniz"* — make items from cloth | matches |
| Training **0–75.1 Body Sash**, 65.1–75.1 Rope, **75.1–100 Oil Cloth** | **neither "body sash" nor "oil cloth" exists.** Closest is `i_sash`, Tailoring **4.5** |
| **74.1** body sash threshold | no such item |
| **98.1** Mage/Special robes; Magery + EvalInt + Meditation ≥ 98.1 and **no warrior skill** | `i_robe` is Tailoring 59.0, 16 cloth + 1 thread. No gate, no special robes |
| Mage robe = *"Hardening crystal ve kumaş"* | **not implemented** |
| Special robe = Hardening Crystal **+ that robe's own crystal** | **not implemented** |
| Special leather: *"spirit of nitre potionunu derinin üzerine dökmeniz"* | **not implemented** — see §7 |
| Fishing nets at Fishing **80.0** | `i_fishing_net` has `RESOURCES` but **no `SKILLMAKE`** and is in no menu — **uncraftable** |

**Conclusion: Tailoring is the craft where this runtime diverges most from
Revolution.** The physical chain is faithful; the product catalogue is stock
Sphere's, not Revolution's.

---

## 3. Mining → smelting → Blacksmithy

```
   rock terrain (t_rock)
     │  pickaxe/shovel, REGIONRESOURCE roll
     ▼
   i_ore_<type>   (1-3 per swing)
     │  double-click ore near a t_forge (statics COUNT here)
     ▼
   i_ingot_<type> (1 ingot per ore)
     │  equip a t_weapon_mace_smith in HAND1, dclick it, target ingots
     ▼
   sm_blacksmith menu
```

### 3.1 Ore table — `SCRIPT` `core/regiontypes.scp` + `regionresources.scp`

`r_default_rock`, weights out of ~99.9:

| Ore | weight | Mining band | pile |
|---|---:|---|---|
| iron | 50.0 | 1–30 | 9–30 |
| *(nothing)* | 10.0 | — | — |
| rusty | 8.0 | 1–30 | 8–21 |
| old copper | 6.0 | 1–30 | 7–18 |
| dull copper | 6.0 | 1–30 | 5–25 |
| bronze | 5.0 | 1–30 | 4–12 |
| copper | 5.0 | 30–60 | 5–20 |
| gold | 2.0 | 30–60 | 3–10 |
| rose | 2.0 | 30–60 | 3–9 |
| agapite | 2.0 | 30–60 | 3–8 |
| bloodrock | 1.0 | 30–60 | 3–6 |
| **silver** | **1.0** | 30–60 | 1–2 |
| verite | 0.5 | 30–60 | 2–6 |
| valorite | 0.2 | 60–110 | 2–4 |
| mytheril | 0.1 | 60–110 | 2–3 |
| blackrock | 0.1 | 60–110 | 1–3 |
| diamond | 0.1 | 60–110 | 1–4 |

Dungeons (`r_dungeon`) are 100 nothing / 30 iron / **5 shadow** — shadow ore is
dungeon-only. Swamps yield nothing. All resources regenerate on a **ten-hour**
timer (`REGEN=60*60*10`).

**`REVOLUTION` conflicts, both dated:**

| Revolution | our runtime |
|---|---|
| **13.12.2008** and **06.04.2009**: ore weight cut **3 → 1** | `i_ore_iron WEIGHT=2` |
| **03.04.2009**: silver spawn **increased**, iron **decreased**; **12.04.2009** repeats it | stock table above (silver 1.0, iron 50.0) |
| *"Bu madenler ile özel setler yapabilirsiniz"* — special ore makes special sets, warrior-only, blocking spellcasting | special ore armour exists per-metal, **no such restriction** |
| S.O.S. bottles while mining | **not implemented** — §8 |

Revolution demonstrably ran a **modified** ore table. Its actual values are
`UNKNOWN`; only the direction of two changes is known.

### 3.2 Smelting — `ENGINE` `CCharSkill.cpp:1075-1260`

| Fact | Value |
|---|---|
| Station | a `t_forge` within Blacksmithing's `RANGE` (statics count) |
| Ratio | **1 ore → 1 ingot** (`iResourceQty = 1`) |
| Skill checked | **Mining**, not Blacksmithy |
| Window | ingot `TDATA1`/`TDATA2` — iron is **20.0 / 50.0** |
| Difficulty | `(min + rand(max-min)) / 10` |
| On failure | **loses up to half the ore** (`rand(amount/2)+1`) |
| Magic items | refused |

### 3.3 Blacksmithy — `ENGINE` `CClientUse.cpp:1265-1290`

Three requirements, all enforced:

1. the ingots must be in the player's own pack;
2. a **`t_weapon_mace_smith` equipped in `LAYER_HAND1`** — smith hammer, tongs,
   club, mace, maul or sledge;
3. a `t_forge` **within 3 tiles** (`IsItemTypeNear(..., IT_FORGE, 3, false)`).

`RevolutionFisher` already carries **tongs** from its newbie kit, so the tool is
not a blocker.

Repairs need a `t_anvil` within 2 (`CCharUse.cpp:793`).

**Revolution's training band matches the runtime:** *"0–70.1 Dagger, 70.1–100
Short Spear"* against `i_dagger` = Blacksmithing **0.0**, 4 iron ingots and
`i_spear_short` = **45.3**, 6 ingots + 1 log. Available from 45.3, prescribed
from 70.1 — consistent.

### 3.4 Tool constraints found live, and they are economy mechanics

Three separate rules, each of which stopped a live run before it was understood.
None is in the engine where you would look for it.

**1. A gathering tool must be WORN, and the check is in the SCRIPT.**

```
skills/skill45_mining.scp:30      @PreStart
    IF (<SRC.WEAPON.USESCUR> < 1)
        SRC.SYSMESSAGE The tool is out of charges.
        return 1
```

`SRC.WEAPON` is the **equipped** weapon. A shovel in the backpack therefore
reads as zero charges and every swing is refused with a message that sounds
like a worn-out tool. `skills/skill44_lumberjacking.scp:26` carries the
identical guard, so a chopping axe must be worn too.

**Shearing is the exception**, and the asymmetry is instructive: shearing runs
through `OnTarg_Use_Item` on the animal (`CClientTarg.cpp:1878`) and never
enters a skill script, so the blade only has to be held.

**2. Tools are consumed, and destroyed.**

```
skills/skill45_mining.scp   ON=@Success
    SRC.WEAPON.USESCUR --
    IF (<SRC.WEAPON.USESCUR>==0)
        SRC.WEAPON.DESTROY 1
```

with `i_shovel ON=@Create UsesMax=50`. **A shovel is worth fifty successful
digs and then it ceases to exist.** That is a standing, recurring cost on every
miner, and it is the demand that keeps a tinker in business — `i_shovel` is
`SKILLMAKE=Tinkering 20.0` from `4 i_ingot_iron, 2 i_log`.

**3. A tool can be too heavy to lift.**

`i_pickaxe` carries `REQSTR=50`. A character created as Fishing 50 / Mining 30 /
Blacksmithy 20 starts at **STR 30** — so the shard's own `NEWBIE MINING` and
`NEWBIE BLACKSMITHING` kits hand out a pickaxe its owner cannot equip:

```
System: Not strong enough to equip pickaxe.
```

`i_shovel` has **no `REQSTR`** and is the same `t_weapon_mace_pick`, so it mines
identically. A low-STR miner must use a shovel, and only the **tinker** sells
one.

**Consequence for the vendor policy.** These three together are why
`econ::VendorClass::BasicCraftTool` exists and is permitted. Refusing tool
purchases would deadlock the craft tree — tinker tools need Tinkering 35 and
four ingots, ingots need a forge, a forge needs a smith hammer — and would also
strand any legitimately-built low-STR miner permanently. Tools are buyable; the
goods they produce are not.

### 3.5 A guildmaster is not a shopkeeper

Britain's only `tinker` place (1422,1654,10) spawns **"Justine, the engineer
guildmistress"**, who keeps no shop — the same trap M3 hit with *"Caedmon, the
mage guildmaster"*. Minoc's tinker place spawns both **"Pembroke, the engineer
guildmaster"** and **"Rhyssa, the tinker"**, and only the second trades.

Practical rule for a bot: an atlas trade tag names a *place*, not a guaranteed
vendor. Scan on arrival and match the paperdoll title.

---

## 4. Lumberjacking → Carpentry

```
   tree (t_tree)   40.0 mr_tree / 60.0 nothing, 1-3 logs, Lumberjacking 1-80
     ▼
   i_log    (VALUE 1, WEIGHT 2.0)
     │  Carpentry 0.0 + t_carpentry tool
     ▼
   i_board  (VALUE 2, WEIGHT 1.0)   1 log -> 1 board
     ▼
   sm_carpentry
```

**The board step is real and required.** Phase 7 asked whether Revolution had a
board/plank step; on this runtime `i_board` is `RESOURCES=1 i_log`,
`SKILLMAKE=Carpentry 0.0`, and **most Carpentry recipes consume boards, not
logs**. It is not a sawmill — it is a menu entry at skill 0.

**`UNKNOWN` for Revolution.** `/oyun_rehberi` says only *"tahtaları
işleyebilirsiniz"* — you work wood — and the cooperative traded **logs**, not
boards (08.11.2008). Not enough to decide, so the runtime's board step stands as
the runtime's, unlabelled as Revolution's.

| Carpentry, notable | value |
|---|---|
| `i_board` | Carpentry 0.0, 1 log |
| `i_club` | 65.0, 10 logs |
| **`i_model_ship`** | **95.0**, 10 boards |
| `i_parchment` | 25.7, 1 log |
| `i_scroll_blank` | 25.7, 1 parchment |
| `i_barrel_open` | 55.0, 18 logs + 10 iron ingots |
| `i_spinning_wheel` | **73.6** Carpentry + 65.0 Tinkering; 20 boards + wire + hinge + axle + gears |
| `i_loom_upright` | **75.1** Carpentry + 83.7 Tinkering; 38 boards + 2 wire + 10 nails + 2 string + 4 hinge |

**Two things fall out of that table.**

**Revolution's Carpentry training band is reproduced exactly at the top end:**
*"95–100 Ship Model"* against `i_model_ship` = Carpentry **95.0**. But the other
half, *"0–95 Barrel Lid"*, is **`i_barrel_lid` (Tinkering 36.8) — which is in no
menu at all** (§10).

**And blank scrolls are a Carpentry product**, not an Inscription one:
tree → log → parchment → blank scroll, all at Carpentry 25.7. That makes the
Runebook's eight blank scrolls cheap and world-sourced (§9).

---

## 5. Lumberjacking → Bowcraft

```
   i_log ──► i_arrow_shaft   Bowcraft 9.8,  1 log
   i_arrow_shaft + i_feather ──► i_arrow    Bowcraft 0.0
   i_arrow_shaft + i_feather ──► i_xbolt    Bowcraft 17.8
   7 x i_log ──► i_bow        Bowcraft 30.0
   7 x i_log ──► i_crossbow   Bowcraft 60.0
```

Revolution's band is *"0–100 Shaft"*, and `i_arrow_shaft` at Bowcraft 9.8 from
one log is exactly that item. **Match.**

**Feathers** are `WORLD_GATHERED` in principle but have **no gathering path on
this runtime** — no bird corpse yields them, and the bowyer is the only source.
Forum topic 54978 describes harvesting feathers from bird flocks near Trinsic,
but that thread discusses an *8x* server and is weak evidence. Recorded
`UNKNOWN`, NPC purchase left permitted (§3.2 of the vendor matrix).

**Special logs — `REVOLUTION`, and absent here.** `/oyun_rehberi` names four:
*"Ulrika, Quakin, Ekroan ve Aqun"*, found at the **Vesper borders**, used for
special bows; and 12.04.2009 adds coloured bows (*Chopa*, *Ulrika* variants) to
the cooperative, **convertible back to their materials with a dagger**. None of
the four exists in Scripts-X. `/ozel_bow` returned HTTP 504 on every attempt, so
the recipes remain unread. **Recorded as a known, named gap.**

---

## 6. Tinkering — a first-class branch

```
   i_ingot_iron ──► i_gears        14.7
                ──► i_barrel_tap   52.0
                ──► i_barrel_hoops 42.0  (4 ingots)
                ──► i_lockpick     48.5
                ──► i_tinker_tools 35.0  (4 ingots)
   i_board + i_ingot ──► i_keg_small    45.0  (8 boards + 2 ingots)
   i_board + tap + hoops ──► i_keg_potion 65.0
   i_sewing_needle + bowl + yarn + thread ──► i_sewing_kit 18.8
   i_feather + i_ink_well ──► i_pen_and_ink 44.1
```

Tinkering is the craft **other crafts depend on**, which is the economic point:
it makes the tailor's sewing kit, the scribe's pen, the alchemist's keg, the
miner's pickaxe and shovel, and the smith's own tools.

**Revolution's band, against the runtime:** *"0–42.1 Spoon, Nails, Gears →
42.1–100 Lockpicking"*. `i_gears` (14.7) and `i_lockpick` (48.5) are in the live
menu; **`i_spoon` and `i_nails` are not** (§10).

**Trapped pouch — `REVOLUTION`, not implemented.** *"trapped pouches (requires
iron ingot + log)"*, and **15.08.2007: changed to single-use**. Our `i_pouch` is
an ordinary Tailoring container (10.0, 1 cut hide + 1 thread) with no trap. The
PvP demand it represents is the clearest Tinker→PvPer dependency in the archive.
See `TNS_DONOR_AUDIT.md` §2.7.

**Golems — `REVOLUTION`, not implemented.** *"bronze ingots, power crystals,
clockwork (from spawn locations)"*, and **19.12.2008** adds golems to the
cooperative — a traded, player-crafted good with PvM-gated inputs.

**Potion keg — `SCRIPT`, implemented.** Tinkering 65.0; 8 boards + 1 tap +
1 hoops. Holds **100** potions of **one** type; `MOREX` counts, `MORE1` stores
the potion's baseid, `MORE2` the average make-skill. Filling returns an empty
bottle; drawing consumes one. Revolution's keg **capacity is `UNKNOWN`** — the
cooperative required kegs to be *"dolu"* (full) to list (08.11.2008), and the
2011 Store Crystal wanted potions in **250**-unit lots, but 250 is post-era.

---

## 7. Hides, leather and PvM materials

```
   creature corpse
     │  bladed weapon -> Use_CarveCorpse   ENGINE CCharUse.cpp:49
     ▼
   corpse CHARDEF's RESOURCES
     │  scissors on the hide (TDATA1)      ENGINE CClientTarg.cpp:2159
     ▼
   cut leather ──► sm_tailor_leather
```

| Creature | carve yield | `SCRIPT` |
|---|---|---|
| cow | 8 ribs + **12 hide** | c_monster_classic.scp:5153 |
| bull | 10 ribs + 15 hide | :5231 |
| llama / horse | 1–3 ribs + 10–12 hide | :4913 / :4435 |
| **sheep** | **3 wool** + 3 lamb legs | :4513 |
| pig | 1 rib, no hide | :4398 |
| **red dragon** | 19 ribs + **20 barbed hide** + 7 dragon scales | :2479 |
| **white wyrm** | 10 ribs + **10 horned hide** + 8 blue scales | :3844 |
| ancient wyrm | **40 barbed hide** + 19 ribs + 12 scales | c_monster_lbr.scp:136 |
| various drakes/serpents | 6–15 **spined hide** | passim |

Special hides carry `TDATA1` to their cut form: `i_hide_spined` →
`i_hides_cut_spined`, and so on. **`PVM_DROP` → scissors → special leather** is
implemented and works.

**Revolution's version is a step longer, and that step is missing:**

| Revolution | here |
|---|---|
| *"spirit of nitre potionunu derinin üzerine dökmeniz"* — pour a **spirit of nitre** potion on the hide | **no such potion, no such step** |
| **12.03.2008**: spirit of nitre requires **volcanic ash** | not implemented |
| **06.06.2007**: spined/horned/barbed leather armour from **dragon and infernal corpses** | corpse sources match |
| **12.03.2008**: Tailoring requirements **barbed 82.0, spined 92.0, horned 100.0** | studded tunic 95.4; no special-leather tier |
| Leather sets and mage robes **decompose via scissors**, success by Tailoring and wear % | not implemented |

So Revolution inserted an **Alchemy dependency into the Tailoring chain** —
PvM hide + alchemist's potion → special leather. That is a three-profession
dependency (hunter → alchemist → tailor) and it is one of the strongest
arguments in the archive for a player-driven economy.

**Hardening Crystal** and the four robe crystals: named by `/oyun_rehberi`,
**source not stated on any page read**. `UNKNOWN`.

---

## 8. Fishing, Shell, nets, S.O.S.

```
   water (t_water)   60.0 nothing / 10.0 each of 4 fish, 1-3 per cast
     ▼
   i_fish_big_1..4   (VALUE 2, WEIGHT 5.0)
     │  any blade                          ENGINE CClientTarg.cpp:1950
     ▼
   4 x i_fish_cut_raw  (VALUE 3, WEIGHT 0.1)  -- x4 amount, 1/12 weight
```

`LIVE` (M3): whole fish sold for **1 gp** to the fisherwoman; raw steaks for
**2 gp** to the cook, and **only** the cook buys them. Carving is worth **8×**
per fish at one twelfth the weight. Cooking them *removes* every buyer.

**Do not hardcode "carve is always best."** The M3 finding holds for *this*
price pair on *this* shard; `econ::EstimateTransformation` recomputes it from
observed prices and per-stone margin.

### 8.1 What Revolution had and this runtime does not

| Revolution | here |
|---|---|
| Fishing nets at **Fishing 80.0** | `i_fishing_net` has `RESOURCES=10 rope + 10 thread` but **no `SKILLMAKE`**, in no menu — **uncraftable** |
| **Shell** as a net input | `i_shell` is a decorative conch (`t_normal`, VALUE 1) with **no fishing link** |
| S.O.S. bottles from *"olta atarken, ağ atarken, maden kazarken ya da odun keserken"* — pole, net, mining **and** woodcutting; chance rises with skill | **no S.O.S. system at all** |
| Bottle → treasure map, **levels 2–5**; bottles may break empty | — |
| Sextant shows distance; pickaxe digs; **no Mining skill needed** | — |
| Lockpicking to open: **L2 40.0, L3 60.0, L4 80.0, L5 100.0** | `f_treasure_map.scp`: **L1 36.0, L2 76.0, L3 84.0, L4 92.0, L5 100.0** — and a level 1 Revolution does not have |
| Opening applies *"çok büyük bir zehir"* | — |

The Lockpicking row is a **numeric conflict with a primary source**, and it is
also the trap `TNS_DONOR_AUDIT.md` §2.11.1 documents: a second Turkish shard
carries the same stock 27/71/81/91/100 values, so cross-checking against it
would have "confirmed" the wrong table.

**Phase 11 verdict:** the Shell → net → S.O.S. chain is **modelled and
`NOT IMPLEMENTED`**. It cannot be live-proven on this runtime.

---

## 9. Inscription, scrolls and the Runebook

```
   i_log ──Carpentry 25.7──► i_parchment ──Carpentry 25.7──► i_scroll_blank
   i_scroll_blank + reagents ──Inscription──► spell scroll
```

Entry is by **double-clicking a blank scroll** → `sm_inscription`, eight
circles.

| Scroll | Inscription | Magery | reagents |
|---|---:|---:|---|
| magic arrow | 10.0 | 5.0 | sulfurous ash |
| magic trap | 20.0 | 10.0 | garlic + spider silk + sulfurous ash |
| **poison** | **30.0** | 20.0 | nightshade |
| **recall** | **40.0** | 30.0 | black pearl + blood moss + mandrake |
| mark | 60.0 | 50.0 | black pearl + blood moss + mandrake |
| **gate travel** | **70.0** | **60.0** | black pearl + mandrake + sulfurous ash |
| **resurrection** | **80.0** | 70.0 | blood moss + garlic + ginseng |

**Revolution's Inscription bands land on these exactly:** *"0–60 Poisoning
Scroll, 60–80 Recall Scroll, 80–100 Resurrection Scroll"* — poison available
from 30.0, recall from 40.0, resurrection at **80.0 on the nose**.

### 9.1 The Runebook, precisely

`revolution/revolution_runebook.scp` (M3.6):

```
RESOURCES = 8 i_scroll_blank, 1 i_rune_marker, 1 i_scroll_recall, 1 i_scroll_gate_travel
SKILLMAKE = Inscription 45.0, i_pen_and_ink
```

**Material provenance is now fully resolved:**

| Input | Provenance |
|---|---|
| 8 × blank scroll | **`WORLD_GATHERED` → `PLAYER_CRAFTED`** — trees → logs → parchment → scrolls, Carpentry 25.7 |
| 1 × blank rune | mage shop, `STOCK_SPHERE_ONLY` (Revolution marked runes; buying blanks is `UNKNOWN`) |
| 1 × Recall scroll | Inscription **40.0** + Magery 30.0, self-craftable |
| 1 × Gate Travel scroll | Inscription **70.0** + Magery **60.0** |
| `i_pen_and_ink` (tool) | **Tinkering 44.1**, feather + ink well |

**The binding constraint is not Inscription 45 — it is the Gate Travel scroll.**
Making one needs **Inscription 70 + Magery 60**; buying one is impossible
because the mage shop stocks circles 1–4 only and Gate Travel is 7th circle. So
a legitimate Runebook crafter needs Magery 60 — the same wall M3 hit — or a
PvM/treasure drop.

`LEGITIMATE_RUNEBOOK_CRAFTING` therefore stays **NOT PROVEN**, now with the
reason named precisely rather than as "four purchased inputs".

Two further blockers, both `SCRIPT`:

* `i_spellbook_runebook` is in **no skill menu**, so the recipe is unreachable
  even at Inscription 100. Revolution's 13.05.2009 entry — *"Runebook copying
  added to the **Inscription menu**"* — is direct evidence that Revolution's
  Inscription menu carried runebook operations.
* `i_spellbook` has `SKILLMAKE=Inscription 50.0` and is likewise in no menu.

---

## 10. 49 recipes exist with no door to them

`PLAYER_CRAFTED_NO_MENU`: a real `SKILLMAKE` and `RESOURCES`, absent from every
live skill menu. Three of them are items **RevolutionUO's own training guide
names**:

| Item | `SKILLMAKE` | Revolution band |
|---|---|---|
| `i_nails` | Tinkering 5.0 | *"Tinkering 0–42.1: Spoon, **Nails**, Gears"* |
| `i_spoon` | Tinkering | *"…**Spoon**, Nails, Gears"* |
| `i_barrel_lid` | Tinkering 36.8 | *"Carpentry 0–95: **Barrel Lid**"* |

The recipes are already in Scripts-X. Only the `MAKEITEM` lines are missing —
a menu gap, not a content gap.

---

## 11. Taming

`WORLD_TAMED`, never NPC-bought. `/binek_bilgileri` gives exact thresholds,
which closes an `UNKNOWN` standing open since `REVOLUTION_RULESET_PROFILE.md` §4:

| Mount | Taming | STR | DEX | INT |
|---|---:|---:|---:|---:|
| Horse | 53.1 | 44 | 39 | 9 |
| Llama | 55.1 | 36 | 344 | 16 |
| Desert Ostard | 65.1 | 115 | 53 | 7 |
| Forest Ostard | 65.1 | 164 | 55 | 9 |
| Mustang | 65.0–80.0 | 187 | 676 | 10 |
| Shire | 65.0–95.0 | 80 | 114 | 53 |
| Frenzied Ostard | 77.1 | 140 | 100 | 8 |
| Mid Ostard | 80.0 | 179 | 169 | 7 |
| Kii-Rin | 90.0 | 271 | 105 | 113 |
| Unicorn | 98.1 | 250 | 103 | 113 |
| Steed | 99.9 | 950 | 94 | 15 |
| Nightmare | 99.9 | 617 | 95 | 14 |
| Chyrsoar | 100.0 | 246 | 111 | 113 |
| Pegasus | 100.0 | 247 | 120 | 112 |

**Supply is fixed and scheduled.** `/spawntakip_sistemi`: a calendar generated
every Monday, **49 mounts a week, 7 a day** — 1 Steed, 1 Nightmare, 1 Unicorn,
2 Kirin, 5 Mustang, 5 Shire, 7 Frenzied, 7 Mid, 10 Forest, 10 Desert Ostard.
Rare mounts late in the week; spawn point is any non-water, non-guardzone,
non-dungeon tile; `.spawntakip` shows the calendar.

A **hard-capped weekly supply of 49** contested by every tamer on the shard is
the purest player-market good in the archive, and it is why rare mounts must
never be NPC-purchasable.

**`NPC_VERIFIED`, in-era:** *pack horse and pack llama*, animal trainer,
**03.11.2010**.

**Hazard, from M3.6:** 52 of 62 mounts in `i_memories.scp` have no AnimID in
Revolution's tiledata. A tamed nightmare crashes third-party clients. Our
`uo_viewer` is immune; the data is still wrong.

---

## 12. Cooking

`i_fish_cut_raw` → cooked. **Economically negative on this shard**:
`i_fish_cut_cooked` (VALUE 3) appears in **no `BUY=` line anywhere**, and
`i_fish_cooked_small` (VALUE 1) is worth less than raw.

Revolution disagrees, and dates its formula: *"pişirmeden ya da pişirerek
tezgahtarlara satıp altın kazanabilirsiniz"* — sell raw **or cooked**. Batch
size by skill:

| date | formula | at 100 |
|---|---|---:|
| 14.04.2008 | `skill/10 * 4` | 40 |
| **12.04.2009** | `(skill/100) * 8` | **80** ← in era |
| 01.06.2011 | `(skill/100) * 10` | 100 (post-era) |

Campfire cooking (Camping + kindling) with per-second batches; **15.04.2008**
adds cookable items from creature corpses and says vendors buy prepared food.

**Our runtime's cooked fish has no buyer.** Conflict recorded; Revolution's own
buyer list is `UNKNOWN`.

---

## 13. The graph, as data

`data/revolution_production.tsv` (generated, committed) — one row per edge:

```
output  qty  process        station      tool             skill:min        inputs                  provenance        evidence
```

Queries it answers, which is the Phase 17 requirement:

* **What do I need to make X?** — walk inputs transitively
* **Where does Y come from?** — its provenance class and producing edge
* **Who could supply Z?** — vendor matrix class + cooperative categories
* **Can I self-produce this?** — every edge's skill against actual skills
* **Which missing skill prevents it?** — first failing edge
* **May an NPC sell it?** — `CanUseNPCVendorFor`

---

## 14. Chain status summary

| Chain | Modelled | Runtime supports | Revolution-faithful |
|---|---|---|---|
| sheep → wool → yarn → cloth | yes | **yes, since the decorator pass** | physical chain yes; product list no |
| cotton → thread | yes | yes | yes |
| flax → thread | yes | **no** — flax is `t_normal` | itemdef line is decorative |
| mine → ore → ingot → smith | yes | **yes, since the decorator pass** | ore table and weights conflict |
| tree → log → board → carpentry | yes | yes | board step `UNKNOWN` |
| log → shaft → arrow/bow | yes | yes | special logs missing |
| ingot → tinker goods | yes | yes | spoon/nails/pouch/golem missing |
| reagents → potions → keg | yes | yes | reagent sourcing `UNKNOWN`; keg capacity `UNKNOWN` |
| log → parchment → scroll → spell scroll | yes | yes | bands match exactly |
| scrolls + rune → runebook | yes | **recipe unreachable (no menu)** | Gate-scroll wall is real |
| fish → steaks → gold | yes | yes, live-proven | yes |
| fish → Shell → net → S.O.S. | yes | **no** | whole system missing |
| corpse → hide → leather | yes | yes | nitre step missing |
| PvM → crystal → special robe | **partly** | **no** | crystal sources `UNKNOWN` |
| tame → mount → market | yes | yes | thresholds now known |
