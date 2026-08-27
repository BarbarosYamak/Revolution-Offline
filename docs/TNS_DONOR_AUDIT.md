# TNS Donor Script Audit

Date: 2026-08-27. Read-only reference audit, run inside M3.7.

**Nothing in Source-X, Scripts-X, `runtime/scripts` or `bot/uo-client` was
modified while producing this document.**

---

## 0. What TNS is, and what it is not

| | |
|---|---|
| Name | **The North Shield** (TNS), a Turkish SphereServer Ultima Online shard |
| Author | forum user **"Avatar"**, released publicly after the shard closed |
| Source | `https://github.com/tazmanyak/northshield` |
| Cloned to | `references/tns/` (depth 1) |
| Commit audited | `b8b7a9af94f55feda34846d026e0eeb0646a5a57`, dated **2016-02-09** |
| Engine target | **SphereServer 0.56b/0.56d** (`VERSION=0.56b` headers throughout) |
| Size | **983 `.scp` files**, ~811,000 lines, 36 MB |
| Language | roughly 80 % Turkish, per the author's own release thread |

**TNS is a donor, not an authority.** It is a *different Turkish Sphere shard*
from the same scene and the same engine generation as RevolutionUO. Where TNS
and Revolution agree, that is corroboration of a **scene convention**, not
proof of a Revolution rule. Where they disagree, **Revolution's own archive
wins**, always.

This document proves that distinction rather than asserting it — see §3.3,
where TNS's treasure-map thresholds turn out to be the stock Sphere Community
Pack values and **contradict** Revolution's published numbers.

### 0.1 The single most important structural fact

TNS is **two script bodies in one repository**, and they must be graded
differently:

| Tree | What it is | Value to us |
|---|---|---|
| `scripts/add-on/`, `scripts/items/`, `scripts/npcs/`, `scripts/skills/`, `scripts/speech/` | the **Sphere Community Script Pack 0.56b** (2009), lightly patched. Every file carries the `SphereServer ©1997-2009 … www.sphereserver.net` header | **Low.** This is the direct ancestor of our own Scripts-X. Novel only where Scripts-X *dropped* something |
| `scripts/Systems/`, `scripts/North_Craft/`, `scripts/North_Factions/`, `scripts/North_Spawnlar/`, `scripts/Quest/` | **TNS's own work** — 205 system files, Turkish, undated-to-2014 | **High.** This is where the shard-specific reconstruction lives |

A finding sourced from `add-on/` is a finding about *stock Sphere*, and we
already have stock Sphere. Only `Systems/` and `North_*` are genuinely new
information.

---

## 1. Inventory

```
scripts/Systems/          205 .scp   TNS-original systems (Turkish)
scripts/items/            176 .scp   community pack + TNS items
scripts/npcs/             165 .scp   community pack chardefs + TNS mounts/monsters
scripts/speech/            91 .scp   community pack NPC speech
scripts/add-on/            91 .scp   Sphere Community Script Pack 0.56b
scripts/skills/            58 .scp   skill definitions + craft functions
scripts/stones/            33 .scp   guild/town stones
scripts/Quest/             24 .scp   champion spawn + OSI quests
scripts/North_Craft/       21 .scp   TNS custom crafting engine
scripts/North_Factions/    14 .scp
scripts/spells/            13 .scp
scripts/maps/              12 .scp
scripts/North_Spawnlar/     8 .scp   spawn tables + templates
```

### 1.1 TNS-original systems that name a RevolutionUO system

These file names are the reason this audit is worth doing at all. Each is a
system RevolutionUO also documents, under the same Turkish name:

| TNS file | RevolutionUO counterpart | Revolution source |
|---|---|---|
| `System_Skilldusur.scp` | `.skilldusur` skill-lowering command | `/oyuncu_komutlari` |
| `System_Kufur.scp` | Küfür Sistemi (profanity report) | `/kufur_sistemi` |
| `System_Kelle.scp` | Head Hunters (kelle = head) | `/head_hunters` |
| `System_SehirIstila.scp` | Capture The City | `/capture_the_city` |
| `System_ReagentCrystal.scp` | Reagent Crystal → Store Crystal | update 07.11.2008 / 20.11.2010 |
| `System_ev_vendor.scp` | Tezgahtarlar Kooperatifi (player vendors + search) | `/tezgahtarlar_kooperatifi` |
| `System_Golem.scp` | Tinkering golems | `/oyun_rehberi` Tinkering |
| `System_Binekler.scp` | Binek Bilgileri (mounts) | `/binek_bilgileri` |
| `items/armors/OzelMageRobeler.scp` | Özel Mage Robe (Fire/Earth/Ice/Energy) | `/ozel_mage_robe` |
| `Araclar/trapped pouch.scp` | trapped pouch | `/oyun_rehberi` Tinkering |

That overlap is **not** evidence that Revolution ran TNS's code. It is evidence
that both shards drew on a shared Turkish Sphere scripting culture, which is
exactly what makes TNS a good *implementation* donor and a bad *rule* source.

---

## 2. System-by-system audit

Format: what TNS implements · the Revolution feature it corresponds to ·
similarity · the pattern worth taking · what breaks · recommendation.

### 2.1 Special Mage Robes — `items/armors/OzelMageRobeler.scp` (175 lines)

**TNS implements** four robes (`i_mage_robe_earth`, `_energy`, `_fire`,
`_ice`), `TYPE=T_armor_leather`, `ARMOR=40`, `VALUE=50000`,
`SKILLMAKE=TAILORING 99.9`, `RESOURCES=72 i_cloth` (special hides commented
out). Each:

* refuses to equip unless `<src.puremage>` — *"Giymek icin puremage olmalisin."*
* attaches an `[EVENTS e_<x>robe]` block that intercepts `ON=@Spelleffect` and
  `return 1`s on a fixed list of spell ids (i.e. resists them)
* carries `HITPOINTS=100` and prints
  `[Sağlamlık: %<eval (<more1L> * 100) / <more1H>>]` on `@click`

**Revolution counterpart.** `/ozel_mage_robe`, verbatim: *"Magery, Evaluating
Intelligence, Meditation Skillerinizin en az **98.1** olması gerekmektedir.
Ayrıca **Warrior skilliniz olmaması** gerekmektedir."* Plus the four robes'
resist lists (Energy: clumsy/feeblemind/weaken/energy bolt · Fire: magic
arrow/fire ball/fire field/explosion/flame strike/meteor swarm · Earth:
poison/poison field/earthquake · Ice: paralyze/paralyze field/dispel/mass
dispel). Update 06.06.2007: *mage robe wear percentage reduces reflection
success*; robes decompose via scissors.

**Similarity: very high — structurally identical, numerically wrong.**

**Pattern worth taking.** Three, and they are the three we do not have:

1. `[function puremage]` (`add-on/sphere_general_functions.scp:1037`):
   ```
   if (<tactics> > 0) || (<wrestling> > 0) || (<swordsmanship> > 0)
      || (<fencing> > 0) || (<macefighting> > 0)
       return 0
   elseif (<meditation> < 99.9) || (<MAGERY> < 99.9) || (<EVALUATINGINTEL> < 99.9)
       return 0
   else
       return 1
   ```
   This is *exactly* the shape of Revolution's rule — a warrior-skill veto plus
   a three-skill floor — expressed as one reusable predicate. Retuned to
   **98.1** it is Revolution's rule.
2. **Equip-time gating with `unequip`** rather than a craft-time check, so a
   robe that changes hands still enforces the rule on the new owner.
3. **`MORE1L`/`MORE1H` as current/max durability**, surfaced as a percentage.
   This is the implementation of Revolution's documented "wear percentage",
   and the same pair drives the repair mechanic in §2.5.

**Incompatibilities.** `99.9` is not `98.1`. `72 i_cloth` has no Revolution
backing; Revolution's recipe is *"Hardening crystal ve kumaş"* plus a
robe-specific crystal, which TNS does not model at all. `ARMOR=40` and
`VALUE=50000` are TNS balance. The resist spell-id lists must be rebuilt from
Revolution's own page, not copied.

**Recommendation: ADAPT** the `puremage` predicate and the `MORE1L/MORE1H`
wear pattern. **REJECT** the recipe, the numbers and the resist lists.

### 2.2 Reagent Crystal — `Systems/System_ReagentCrystal.scp`

**TNS implements** `i_reag_store`, `TYPE=t_script`, `VALUE=25000`, holding
**one TAG per Magery reagent** (`tag.spider`, `tag.sulfur`, `tag.shade`,
`tag.bmoss`, `tag.ginseng`, `tag.bpearls`, `tag.mroot`, `tag.garlic`),
plus `tag.masteruid` / `tag.mastername` owner binding and `tag.bagsayisi`
for the withdrawal container. Negative-value clamping on every `@Dclick`.

**Revolution counterpart.** Reagent Crystal, in-era: update **07.11.2008** —
*"Reagent crystal kullanımını kolaylaştırmak üzere reagentların çıkarılacağı
**çantanın belirlenebilmesi** sağlandı"* (you may now designate the container
reagents are withdrawn to). Renamed **Store Crystal** on 20.11.2010, which then
also stored trapped pouches and fishing nets.

**Similarity: very high.** Eight-TAG reagent store, owner-bound, with a
designated withdrawal bag, is the same object twice.

**Pattern worth taking.** The whole storage shape: scalar TAGs rather than a
container, owner binding on first use, and a settable target bag. It also shows
the failure mode to guard — the script clamps every counter at zero on each
open, which reads as scar tissue from a real underflow bug.

**Incompatibilities.** TNS stores only the **eight Magery reagents**; that is
correct for a Renaissance-era shard and matches Revolution, but our runtime's
mage shop stocks **26** reagents including 18 Necromancy ones (see the M3.7
vendor matrix — an era conflict of our own). TNS's crystal is also being
*retired* in the code we have (`@Dclick` announces a replacement and sets a
7200-second self-delete), so this is a late snapshot, not the working version.

**Recommendation: REFERENCE.** Revolution's own dated entries define the
feature; TNS shows one clean way to hold the state. Not needed before M4.

### 2.3 Player vendors and a searchable cooperative — `Systems/System_ev_vendor.scp` (946 lines), `System_Pazarvendor.scp` (760)

**TNS implements** house-placed player vendors whose stock is walked with
`FORCONT`/`FORITEMS` and **written into a MySQL table**:

```
db.execute "insert ev_vendor_sistemi VALUES (NULL,
    '<addslashes <uid>>',
    '<addslashes <tag0.magicweapon> <name>>',
    '<addslashes <local.kordinat>>',
    '<addslashes |<hitpoints>/<maxhits>|>');"
```

Note the fourth column: **`hitpoints/maxhits` is indexed alongside the item**,
and only items with `tag0.fiyat` (a price) set are listed.

**Revolution counterpart.** Tezgahtarlar Kooperatifi. From `/tezgahtarlar_kooperatifi`:
*"Listeleme oyuncu tezgahtarlarında bir ürünün **fiyatlandırılmasıyla**
sağlanıyor"* — pricing an item is what lists it; and the weapons tab *"Damage
shows weapon **wear percentage** range"*. Category list from update 08.11.2008
+ 19.12.2008: cloth bolts, logs, iron ingots, arrows, crossbow bolts, full
potion kegs, mage robes, spellbooks, runebooks, golems.

**Similarity: very high, down to two non-obvious details** — *price-to-list*
and *wear percentage as a searchable field*. Both shards solved the same
problem the same way.

**Pattern worth taking.** The architecture: a player vendor is an ordinary
container; a separate indexer walks priced contents into an external searchable
store; the index carries name, location, price and condition. That is precisely
the shape M3.7 Phase 16 needs (`PlayerVendorListing`), and it confirms that the
cooperative was an **index over player vendors**, not a separate NPC inventory.

**Incompatibilities.** MySQL. Our runtime has `libmariadb.dll` present but no
database configured, and a bot population should not depend on one. The
equivalent for us is an in-process index in the bot's own knowledge layer,
which is where M3.7 puts it.

**Recommendation: ADAPT (design only).** Take the price-to-list rule and the
indexed-field set. Do not port the SQL.

### 2.4 Dynamic NPC buy price — `Systems/System_SellerBuro.scp` (489 lines)

This is the most directly useful thing in the entire pack for M3.7, and it is
the one system that answers the milestone's central worry — *NPC vendors as an
infinite faucet* — with running code.

**TNS implements** a **server-wide, daily-resetting fish market**:

```
var.balik_satilan += <argo.amount>            // fish sold today, shard-wide
if <eval <var.balik_satilan>> < 500000
    var.balik_fiyat 5
if (... > 500000) && (... < 1000000)
    var.balik_fiyat 3
if (... > 1000000) && (... < 2000000)
    var.balik_fiyat 2
if (... > 2000000)
    var.balik_fiyat 1
paramiz <eval <var.balik_fiyat>>*<argo.amount>
```

with a daily reset keyed on `<rtime.day>`, a shard-wide announcement
(*"Tüccar birliği balık stoklarının tükendiğini bildirdi"* — the merchants'
guild reports fish stocks exhausted), and a public gump `d_stok_balik` showing
today's volume, the **current** price and the last reset time.

**Revolution counterpart.** **None documented.** Revolution's guide says
fishing and bowcraft products are sold to NPC vendors for gold and says nothing
about a price curve. So the *mechanism* is unattested for Revolution.

**Similarity: unknown — and that has to be stated plainly.** This is a TNS
answer to a problem Revolution certainly also had, not a reconstruction of
Revolution's answer.

**Pattern worth taking.** A saturating NPC price with a shard-wide daily
counter, a visible price, and an announcement when the market moves. Three
properties matter for a bot population:

1. it is **observable** — a bot can read the current price rather than assume
   the itemdef `VALUE`;
2. it makes the same route **less profitable as more bots take it**, which is
   the only thing that stops an autonomous population from converging on one
   exploit;
3. it resets, so the world does not deadlock.

**Incompatibilities.** The thresholds (500k/1M/2M items per day) are TNS
population figures and are meaningless for us. Adopting the numbers would be
exactly the "2009 forum says X, so every 2026 bot trades at X" error M3.7
Phase 20 forbids.

**Recommendation: ADAPT the mechanism, flagged as RECONSTRUCTED, never the
numbers.** And it belongs in the *bot's* pricing model first
(`econ::PriceBook` already separates observed from historical), not in a server
script — a bot that re-reads the vendor's quote before each sale gets the
benefit with no shard change at all.

### 2.5 Repair and recycling — `skills/sphere_skill_blacksmithy.scp`

**TNS implements** `f_craft_blacksmith_repair_targ`:

* requires **both** `<isneartype t_forge 3>` **and** `<isneartype t_anvil 3>`
* refuses items whose `skillmake.1.key` is not `skill_blacksmith`
* difficulty from current damage:
  `((((more1h - more1l) * 1250) / more1h) - 250) / 10`
* and, on every repair, **permanently lowers maximum durability**:
  `decrease = 30 - (blacksmithing / 33)`, plus `{2 4}` random, applied as
  `more1h = (more1h * (100 - decrease)) / 100`

and `f_craft_blacksmith_smelt_targ`, which recycles armour/weapons back to
**60 %** of their `RESOURCES` (1 unit if not exceptional).

Separately, `add-on/typedefs/sphere_override_forge.scp` recycles at **50 %**
and carries an anti-exploit we do not have:

```
if <eval <argo.tag.vendordanaldim>> == 1
    src.sysmessage @33,,1 Vendordan alınan eşyalar eritilemez.
    return 1
```

— *items bought from a vendor cannot be smelted*. Without that, any NPC that
sells a weapon below its ingot value is a gold press.

**Revolution counterpart.** Wear percentage is documented (06.06.2007, and the
cooperative's damage field). Revolution 12.04.2009 also records **colored bows
being convertible back to their building materials with a dagger**, i.e. a
recycling path existed. The repair *formula* is not documented anywhere.

**Similarity: partial.** The concepts (wear, degrading repairs, recycling) are
attested for Revolution; the arithmetic is TNS's.

**Pattern worth taking.** Two:
* **repair costs maximum durability** — the reason Revolution players kept
  buying new robes and leather sets, and the reason a crafter economy exists at
  all. Without it, one crafted item lasts forever and demand goes to zero.
* **`TAG.vendordanaldim`** — mark vendor-bought goods so they cannot be fed
  back into a crafting recycler. This is a real, cheap, general anti-exploit
  and our runtime has no equivalent.

**Incompatibilities.** Our Source-X has its own `Use_Repair` on `IT_ANVIL`
(`CCharUse.cpp:793`, searching within 2 tiles) and its own smelt path
(`CCharSkill.cpp:1088`), both engine-level. A script override would have to sit
on top of, or replace, those. Not an M3.7 job.

**Recommendation: REFERENCE now, ADAPT the `vendordanaldim` flag when the bot
population can actually buy and sell in volume (M4).**

### 2.6 Smelting override — `add-on/typedefs/sphere_override_ore.scp`

**TNS implements** an ore `@dclick` smelt that does not use the engine path:

```
elif <src.mining> >= <eval <skillmake.1.value>-100>
    if <src.isneartype t_forge 2>
        if <R90> <= <eval (((<src.mining> - <skillmake.1.value>)*2)+70)>
            serv.newitem <tdata1>
            new.amount <eval ((<amount>*(<src.mining>/10))/100)>
            if (<src.skilltotal> < 700.0)
                if (<src.mining> < 100.0)
                    src.mining (<src.mining> + 1)
```

Three things are worth noting. The **yield scales with skill**
(`amount * (mining/10) / 100`, i.e. ~10 % of the pile at Mining 100) rather
than the engine's flat 1 ingot per ore. The **success curve** is explicit:
`70 + 2×(skill − requirement)` percent. And skill gain is **gated on
`skilltotal < 700.0`**.

**Revolution counterpart.** Revolution's **700.0 total skill cap** is
independently proven from eleven build threads and the `.skilldusur` command
(see `REVOLUTION_RULESET_PROFILE.md` §2.1). Finding the same 700.0 hard-coded
into an unrelated Turkish shard's gain check is **corroboration that 700 was a
scene-wide convention**, not a Revolution invention — useful context, and
another reason to trust the figure.

Revolution's own smelt numbers are **not documented**. Its ore economy *is*
partly documented: ore weight cut 3 → 1 (13.12.2008, again 06.04.2009) and
silver-up/iron-down spawn changes (03.04.2009, 12.04.2009).

**Similarity: mechanism yes, numbers unattested.**

**Recommendation: REFERENCE.** Do not port. Our Source-X smelt (1 ore → 1
ingot, `TDATA1`/`TDATA2` as the skill window, up to half the ore lost on
failure) is a legitimate reading of the same era and is already live. Record
TNS's 700.0 gain gate as corroborating evidence only.

### 2.7 Trapped pouch — `Systems/Araclar/trapped pouch.scp`

**TNS implements**
```
[ITEMDEF i_trappedpouch]
SKILLMAKE=TINKERING 65.0,t_tinker_tools
RESOURCES=10 i_log, 1 i_ingot_agapite
```
whose effect is to clear paralysis: `src.flags <src.FLAGS>&~statf_freeze`,
plus an explosion effect, fired automatically rather than by command.

**Revolution counterpart.** `/oyun_rehberi` Tinkering: advanced products are
*"trapped pouches (requires **iron ingot + log**)"*. Update 15.08.2007:
*trapped pouch changed to single-use*.

**Similarity: high on shape, wrong on material.** Log + ingot + Tinkering is
the same recipe family; Revolution says **iron**, TNS uses **agapite**.

**Pattern worth taking.** The paralysis-break is the whole point of the item
and the reason Revolution PvPers bought them — that is the *demand* side of the
Tinker→PvPer dependency M3.7 Phase 16 wants to prove. TNS confirms what the
item is *for*, which the Revolution guide only implies.

**Incompatibilities.** Our Scripts-X has no trapped pouch at all: `i_pouch` is
`Tailoring 10.0, 1 i_hides_cut + 1 i_thread`, an ordinary container. The item
would have to be built, and its single-use rule taken from Revolution's dated
entry, not from TNS (whose version is not single-use in this snapshot).

**Recommendation: ADAPT the recipe shape** (Tinkering, log + **iron** ingot,
single use), **REFERENCE the paralysis-break effect**, **REJECT** agapite.

### 2.8 Golems — `Systems/System_Golem.scp`

**TNS implements**
```
[ITEMDEF i_pet_c_golem_tinker]      TYPE=t_figurine
SKILLMAKE=TINKERING 98.1,MAGERY 98.1
RESOURCES=100 i_ingot_iron,3 i_crystalgolem,1 i_headgolem,1 i_scroll_summon_elem_earth
```
plus a bigger variant (200 ingots, 8 crystals, 2 heads) and matching CHARDEFs.

**Revolution counterpart.** `/oyun_rehberi`: golems need *"bronze ingots,
power crystals, clockwork (from spawn locations)"*, and update 19.12.2008 adds
**golems to the cooperative's "other" tab** — so they were a traded
player-crafted good.

**Similarity: partial and instructive.** Both are *ingots + crystals + a
head/clockwork part + a summon component*, gated at Tinkering ~98 and requiring
Magery. Materials differ (iron vs bronze; `i_crystalgolem`/`i_headgolem` vs
power crystal/clockwork).

**Pattern worth taking.** `t_figurine` + `MORE=<chardef>` is the whole
implementation of a craftable pet — cheap, and it makes the golem a *tradeable
item* until it is used, which is what listing it in a vendor search requires.
Also: the PvM-sourced components (`i_crystalgolem`, `i_headgolem`) are the
concrete form of Revolution's "from spawn locations", and are a clean worked
example of the PvM→crafter dependency M3.7 Phase 15 is mapping.

**Recommendation: ADAPT the pattern** (`t_figurine` carrying the chardef, PvM
components as the gating input). **REJECT the material list** — Revolution says
bronze.

### 2.9 Custom crafting engine — `North_Craft/` (21 files)

**TNS implements** a paged-gump crafting system replacing Sphere's
`SKILLMENU`. Entry is by **double-clicking the tool**, via `[TYPEDEF]` hooks:

```
[typedef t_sewing_kit]   on=@dclick  ... src.craftxx tailoring
[typedef t_tinker_tools] on=@dclick  ... src.craftxx tinkering
[typedef t_scroll_blank] on=@dclick  ... src.craftxx inscription
[typedef t_feather]      on=@dclick  ... src.craftxx bowcraft
[typedef t_ingot]        on=@dclick  ... dispatches per ingot colour to
                                         blacksmith_iron / _dcopper / _copper /
                                         _bronze / _golden / _shadow / _agapite /
                                         _mytheril / _verite / _valorite /
                                         _bloodrock / _blackrock
```

with the catalogue held as flat DEFNAME tables
(`tinkering_<row>_<page> <itemdef>`).

**Revolution counterpart.** Revolution's craft menus are described only
indirectly (13.05.2009 *"Runebook copying added to the **Inscription menu**"*;
25.02.2011 *"ship construction added to the Carpentry menu"*), so a menu system
existed and was extended. Nothing says it looked like this.

**Similarity: unknown.**

**But one part is immediately useful.** `North_Craft/craft_items_tinker.scp`
lists a **complete Tinkering catalogue** including the exact items our runtime's
legacy menu is missing:

```
tinkering_2_3  i_nails
tinkering_5_5  i_spoon
tinkering_6_5  i_fork
tinkering_5_3  i_mortar_pestle
tinkering_4_4  i_hammer_smith
tinkering_2_4  i_pickaxe
```

The M3.7 audit found that our Scripts-X legacy Tinkering menu offers **gears
and lockpicks but not nails or spoons** — while RevolutionUO's own training
guide (forum topic 59111) prescribes *"Tinkering 0–42.1: **Spoon, Nails,
Gears**"*. TNS confirms these are ordinary Tinkering items in this engine
generation and shows the itemdef names; the itemdefs already exist in our
runtime with `SKILLMAKE` set and are simply absent from the menu.

**Recommendation: REFERENCE the engine** (we keep `CraftingSystem=0` and the
legacy menus, which are era-correct). **ADAPT the missing menu entries** —
adding `MAKEITEM=i_nails` / `i_spoon` to the Tinkering menu restores a
Revolution-documented training path and invents nothing, since the recipes are
already in our itemdefs.

### 2.10 `.skilldusur` — `Systems/System_Skilldusur.scp`

**TNS implements** a `[plevel 1]` player command backed by a 39-entry
`[DEFNAME player_skilldown]` table mapping index → skill key.

**Revolution counterpart.** `.skilldusur`, documented on `/oyuncu_komutlari`,
with a **670.0 total floor**.

**Similarity: same command, same purpose.** TNS's table stops at 39 skills
(Renaissance-era range), which is consistent with the era.

**Recommendation: REFERENCE.** We do not need the command — our bots respect
the cap in the build generator (`rules.h`), not by lowering skills in-game. The
30-point gap between Revolution's 700.0 cap and its 670.0 floor remains
unexplained and TNS does not explain it.

### 2.11 Treasure and S.O.S. — `add-on/treasures/`, `add-on/typedefs/sphere_override_weapon_mace_pick.scp`

**TNS implements** the Sphere Community Pack treasure system: S.O.S. bottles
with sextant coordinates, `t_treasure_tile` dug with a pickaxe, chests spawning
guardians, sea serpents on a `<r4>` roll when the chest surfaces.

**Revolution counterpart.** `/hazine_sistemi`: S.O.S. bottles from *"olta
atarken, ağ atarken, maden kazarken ya da odun keserken"* (fishing, net
casting, mining, woodcutting), probability rising with skill; bottles yield
maps of **levels 2–5**; reading needs Cartography **and** Lockpicking; a
sextant shows distance; the pickaxe digs; Lockpicking opens and wakes
guardians; opening applies a severe poison. Lockpicking thresholds: **L2 40.0,
L3 60.0, L4 80.0, L5 100.0**.

**Similarity: mechanism close, numbers contradicted.**

### 2.11.1 The disagreement, stated in full

This is the case that proves TNS must not be used as authority.

| | Level 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| **Revolution** (`/hazine_sistemi`) | *no level 1* | **40.0** | **60.0** | **80.0** | **100.0** |
| TNS (`add-on/sphere_settings.scp:169-173`) | 27.0 | 71.0 | 81.0 | 91.0 | 100.0 |
| Our Scripts-X (`functions/f_treasure_map.scp`) | 27.0/36.0 | 71.0/76.0 | 81.0/84.0 | 91.0/92.0 | 100.0/100.0 |

TNS's numbers are **the stock Sphere Community Pack values**, and our own
runtime carries the same ones. Had TNS been treated as a Revolution proxy, we
would have "confirmed" the wrong table twice over and written down 71.0 where
Revolution says 60.0. Revolution's own page wins; our runtime is in conflict
with it, and that conflict is now recorded in the M3.7 report rather than
resolved by a second wrong source.

**Pattern worth taking anyway.** The pickaxe `@TargOn_Item` dispatch on
`t_treasure_tile`, guarded by a per-level Lockpicking DEFNAME lookup
(`def.scp.Locpick_TMapLevel_<level>`), is the right *shape* for Revolution's
rule — a level-indexed table consulted at dig time. Substituting Revolution's
40/60/80/100 into that shape is a two-line change.

**Recommendation: REFERENCE the structure, REJECT every number.**

### 2.12 Textile chain — nothing to take

**TNS implements** stock Sphere. `items/profession/sphere_item_profession_tailor.scp`
has `TYPE=t_spinwheel` on an ordinary itemdef and no override; there is no
`@TargOn` handler for wool, yarn or thread anywhere in the pack, and the weaver
NPC speech (`speech/jobweaver.scp`) is the stock English community-pack file.

**Why this matters.** M3.7 found that on Source-X the wool → spinning wheel →
loom chain requires a **dynamic** station: `OnTarg_Use_Item` resolves the target
with `uid.ObjFind()`, so a map static yields `pItemTarg == nullptr` and the
`IT_WOOL` case falls through. TNS does not solve this, because TNS did not have
to — like every Sphere shard, it ran the world decorator that *places* dynamic
spinning wheels and looms as items.

**Recommendation: REJECT** — no donor value. The fix is the one M3.7 already
identified: run our own Scripts-X decorator's station placements, which exist
(`functions/worldgen/decoration/felucca/city_britain_deco_felucca.scp` places
spinning wheels at `1545,1656,26` / `1473,1689,0` and looms at `1545,1660,26` /
`1473,1685,0`) and were never executed.

### 2.13 Alchemy cauldron — `Systems/System_Kazan_Alc.scp`

**TNS implements** `i_kazan_alchemy`, a craftable station
(`SKILLMAKE=tinkering 98.1, MAGERY 98.1`, 300 mixed ingots) that boils a
reagent over a timer into a batch of the corresponding potion
(spider silk → night sight, sulfurous ash → greater explosion, nightshade →
deadly poison, mandrake → greater strength, ginseng → greater heal).

**Revolution counterpart. None.** Revolution's Alchemy is bottle-at-a-time via
mortar and pestle, and its potion *bulk* container is the **keg**, which our
runtime already implements (Tinkering 65.0; 8 boards + tap + hoops; **100
potions**; single potion type; filling and emptying both trade a bottle).

**Similarity: none — this is a TNS invention.**

**Pattern worth taking.** Only the generic one: a station that holds
`MORE1` (busy flag), `MOREX` (units remaining), `MOREY` (units loaded) and
`TAG.<x>` (what kind), driven by `@Timer`. That is the same state pattern our
own potion keg uses, and the same one the M3.6 runebook uses for pages.

**Recommendation: REJECT the system. REFERENCE the MORE/TAG/timer idiom.**

### 2.14 Travel Book — `Systems/Araclar/travelbook.scp` (5151 lines)

**TNS implements** a map-of-Britannia gump with ~30 fixed destination buttons.

**Revolution counterpart.** The Runebook — 8 **player-marked** pages, rune
insertion, Recall-scroll charges, and travel that **casts the real spell**
(M3.6 `RUNEBOOK_MECHANICS_PASS`).

**Similarity: none. Opposite design.** TNS's book is a fixed-destination
convenience teleporter; Revolution's is a container of player-created runes
whose use is gated by Magery, mana and a fizzle roll.

**Recommendation: REJECT.** Adopting it would delete the travel economy M2.5
and M3.6 built. Recorded here so nobody re-discovers it and thinks it useful.

### 2.15 Systems audited and set aside

| TNS system | Why not now |
|---|---|
| `System_SehirIstila.scp` (2740 lines, city invasion) | Revolution's Capture The City is a documented system, but it is PvP content, not economy. M5+ |
| `System_Kelle.scp` (Head Hunters) | Revolution documents the system; the payout formula is UNKNOWN in both sources |
| `System_Acikartirma.scp` (4159 lines, auctions) | MySQL auction house; Revolution used player vendors + cooperative, not auctions |
| `System_Kufur.scp` | Moderation tooling; irrelevant to a bot population |
| `System_Paragon.scp`, `System_Achievement.scp` | Post-era / no Revolution evidence |
| `Systems/Farming/` (7 files) | Custom plant-growing; Revolution has crops in the client data but no documented farming system |
| `add-on/crafting/Bulk_Order.scp` (1073 lines) | BODs are an AoS-era OSI system. Outside `revolution_2009_2010` |
| `add-on/sphere_skillgain_new.scp` | A RunUO-style difficulty-based gain model. Interesting against M3.6's finding that Sphere's Magery gain does **not** scale with spell difficulty — but replacing the gain model is an engine-behaviour decision, not an M3.7 one |
| `add-on/sphere_house_system.scp` (4835 lines) | Our runtime has `housing/`; no gap identified |
| `System_Binekler.scp` | Mount properties; Revolution's own `/binek_bilgileri` gives exact taming thresholds per mount, which is better evidence |

---

## 3. Answering the question that was asked

> Which TNS implementations can save us work in M3.7 without contaminating
> RevolutionUO authenticity?

### 3.1 Take now (ADAPT)

| # | What | Saves | Authenticity risk |
|---|---|---|---|
| 1 | **`puremage` predicate** — warrior-skill veto + three-skill floor, checked at `@Equip` with `unequip` | The Special Robe / Mage Robe gate, retuned to **98.1** from Revolution's own page | **None.** The rule is Revolution's; only the shape is TNS's |
| 2 | **`MORE1L`/`MORE1H` wear percentage** | Revolution's documented robe and leather-set wear, and the cooperative's damage field | None. Revolution documents the effect, not the storage |
| 3 | **Missing Tinkering menu entries** (`i_nails`, `i_spoon`) | Restores Revolution's documented *"Spoon, Nails, Gears"* 0–42.1 training band. The itemdefs and `SKILLMAKE` already exist in our runtime | **None** — this adds a menu line, not a recipe |
| 4 | **Price-to-list + indexed condition** for player-vendor listings | The `PlayerVendorListing` design in M3.7 Phase 16 | None; independently confirmed by `/tezgahtarlar_kooperatifi` |
| 5 | **Trapped-pouch recipe shape** (Tinkering, log + iron ingot, single use) with the paralysis-break purpose | The Tinker→PvPer dependency proof | Low, if the **iron** ingot and the single-use rule come from Revolution and only the *effect* from TNS |

### 3.2 Keep on the shelf (REFERENCE)

Reagent-crystal TAG storage · repair-degrades-max-durability · `TAG.vendordanaldim`
(vendor-bought goods cannot be recycled) · saturating daily NPC price ·
`t_figurine` + `MORE=<chardef>` craftable pets · level-indexed treasure
thresholds · the MORE/TAG/timer station idiom.

None of these is needed to pass M3.7. All of them are needed eventually, and
all of them are cheaper to copy than to invent.

### 3.3 Refuse (REJECT)

The Travel Book · the Alchemy cauldron · Bulk Order Deeds · every TNS **number**
— treasure thresholds, robe skill values, golem and trapped-pouch materials,
smelt yields, fish price tiers, `ARMOR=40`, `VALUE=50000`.

### 3.4 The contamination rule this audit ran under

Three tests, applied to every row above:

1. **Does Revolution's own archive state the rule?** If yes, Revolution's value
   is used and TNS is at most an implementation. *(Robes: 98.1, not 99.9.
   Trapped pouch: iron, not agapite. Golem: bronze, not iron.)*
2. **Does TNS state a number Revolution does not?** Then the number is TNS's
   own balance and is refused, whatever it would save. *(Treasure thresholds,
   fish price tiers, smelt yields.)*
3. **Is the TNS file from `add-on/`?** Then it is the Sphere Community Pack,
   which is our own Scripts-X's ancestor — it is not independent evidence about
   anything, and agreement with it means only that two copies of the same file
   agree.

§2.11.1 is the worked example of all three firing at once, and is the reason
this audit's headline is *"useful donor, dangerous authority"*.

---

## 5. Supplementary pass (M3.7 continuation)

A second, targeted read against the search list the continuation brief supplied.
Same rules as §3.4.

### 5.1 Special-robe crystals — the exact trap the brief warned about

TNS has **two** robe families, and the second one is the interesting one:

```
scripts/items/armors/ProtectiveMageRobe.scp:151
    RESOURCES=100 i_cloth, 20 i_reag_dragon_blood, 1 i_crystal_puremagerobe
scripts/items/armors/ProtectiveMageRobe.scp:178
    RESOURCES=100 i_cloth, 20 i_reag_dragon_blood, 1 i_crystal_warlockrobe

scripts/items/crafting/Craft_items_Resources.scp:38
    [ITEMDEF i_crystal_puremagerobe]  NAME=Crystal Mage  ID=i_crystal_purple
        ON=@DCLick  "Robe yapiminda kullanilir."   (used in robe crafting)
```

and the elemental four (`OzelMageRobeler.scp`) use `72 i_cloth` with special
hides commented out.

**Compare Revolution's own structure**, from `/oyun_rehberi` Tailoring:
mage robes need *"Hardening crystal ve kumaş"*; special robes need
*"Hardening crystalin yanında o robenin crystali"* — the Hardening Crystal
**plus that robe's own crystal**.

| | Revolution | TNS |
|---|---|---|
| base material | cloth | cloth (72 or 100) |
| universal component | **Hardening Crystal** | **absent** |
| per-robe component | that robe's crystal | `i_crystal_puremagerobe` / `_warlockrobe` |
| extra | — | **20 dragon blood** |

So the *shape* is a partial match — cloth plus a robe-specific crystal — and the
**materials are not**. TNS has no Hardening Crystal at all, and Revolution's
archive never mentions dragon blood in a robe.

**Verdict: `TNS_REJECTED_NOT_REVOLUTION` for the recipe.** This is precisely
the substitution the continuation brief named in advance (*"do not substitute
TNS recipes such as Dragon Blood, Craft Points, Crystal Mage"*), and reading the
files confirms the warning was well aimed. What remains adoptable is only what
§2.1 already listed: the `puremage` equip predicate and the `MORE1L/MORE1H`
wear percentage.

Also found, and rejected for the same reason: `i_crystal_30/60/90/120`,
`i_crystal_of_com_t1..t3`, `i_power_crystal`, `i_crystalgolem`. None has a
Revolution counterpart by name. `i_crystal_reagent`
(`Systems/System_ReagentCrystal.scp:578`) is the exception already graded in
§2.2 as a `REFERENCE`.

### 5.2 Runebook — nothing to take, confirmed a second time

Re-searched for `runebook`, generic gumps, TAG/MORE page storage, charges and
rune containers. TNS's only travel book is `Systems/Araclar/travelbook.scp`,
already graded `TNS_REJECTED_NOT_REVOLUTION` in §2.14: a 5151-line gump of
~30 **fixed** destinations that teleports directly.

`Systems/System_Runebook_Ekleme.scp` (1226 lines) turns out, on reading, to be
mostly a **sextant/coordinate** library (`[function sextant2]`, guard-zone
checks) rather than a runebook. It contains no page storage, no charge
mechanic and no rune container.

**Our M3.6 restoration is strictly ahead of TNS here** — 8 named pages, rune
insertion, Recall-scroll charges, and travel that casts the real `[SPELL 32]`.
Nothing to import.

### 5.3 Housing and ships — not an M3.7 subject, briefly graded

`add-on/sphere_house_system.scp` (4835 lines) is the Sphere Community Pack
house system with TNS edits. The one technique worth noting is its access
model: friend lists are migrated off per-house `TAG.friend_<n>` values into
**server-side lists** (`serv.list.house_<uid>_friend`), with the tags cleared
after the push — a tags-to-list migration pattern that is a clean way to hold
unbounded per-object collections in Sphere script.

`add-on/typedefs/sphere_override_ship_tiller.scp` covers ship tillers; there is
no drydock implementation in the pack.

**Verdict: `TNS_IMPLEMENTATION_REFERENCE`, deferred.** Our runtime already has
`housing/`, Revolution's own `/ev_cesitleri` and `/ev_kurallari` pages are the
authority for house rules, and ships are 2010–2012 content that
`REVOLUTION_RULESET_PROFILE.md` §3 explicitly excludes from this profile.

### 5.4 The other listed searches, resolved

| Brief's search | Result |
|---|---|
| textile: sheep / wool / wheel / loom / cloth | **nothing** — TNS runs stock Sphere here (§2.12). It never hit our static-station problem because it ran the decorator |
| blacksmith: mining / ore / smelting / forge / anvil / repair | §2.5, §2.6. Repair-degrades-max-durability and `TAG.vendordanaldim` remain the two worth having |
| tinkering: trapped pouch / golem / tools / components | §2.7, §2.8. Recipe *shapes* match Revolution; materials do not |
| carpentry: logs / boards / furniture / containers / ship models | stock Sphere catalogue; no divergence worth importing |
| alchemy: bottles / potion keg / keg components | §2.13. TNS's keg typedef is near-identical to our runtime's; its cauldron is an invention |
| special robes | §5.1 — **rejected** |
| runebook | §5.2 — **rejected** |
| housing / ships | §5.3 — deferred |

**Net effect of the supplementary pass on M3.7: zero imports.** It confirmed
two rejections that were already provisional and added one implementation
reference (the tags-to-list migration) that nothing in this milestone needs.

---

## 6. Provenance and reproducibility

```
git clone --depth 1 https://github.com/tazmanyak/northshield.git references/tns
# HEAD b8b7a9af94f55feda34846d026e0eeb0646a5a57  (2016-02-09)
```

`references/tns` is a third-party repository with its own history and licence
(GPL text present as `LICENSE`); it sits beside `reference/uo-offline` as a
read-only donor and is not part of the runtime. Nothing in it is loaded by
`spheretables.scp`, and no file from it has been copied into the project.

Every TNS quotation in this document carries its file path, and every
Revolution rule quoted against it carries its archive page or dated update
entry.
