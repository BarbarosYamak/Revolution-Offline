# Revolution Gameplay Truth

The canonical, evidence-graded reference for how this shard actually behaves. Every claim carries a category and a source. The point of the grading is to stop a Source-X default quietly becoming "a Revolution fact", and equally to stop a strong player memory being discarded because the current Scripts-X does not implement it.

Started at M3 (2026-08-26). Add to it; do not summarise it away.

## Evidence categories

| Tag | Means |
|---|---|
| `LIVE_VERIFIED` | Observed on the wire against the running Source-X shard, with a log line to point at. |
| `SCRIPT_VERIFIED` | Read from the Revolution/Scripts-X data the shard boots (`runtime/scripts`, `runtime/sphere.ini`). |
| `CLIENT_DATA_VERIFIED` | Read from the Revolution client's own files (`local/revolution-client`). |
| `SOURCE_X_DEFAULT` | Source-X engine behaviour that no shard script overrides. True here, but it is the engine's choice, not Revolution's. |
| `PLAYER_MEMORY` | How the user remembers RevolutionUO. Not verified; not dismissed. |
| `HISTORICAL_UNVERIFIED` | Claimed about the historical shard with no evidence to hand. |
| `UNKNOWN` | Not yet investigated. |

### Historical categories (added M3.5)

The categories above describe **our reconstruction**. They cannot express what
the *historical* shard did, which is a different question and often a
conflicting answer. These are ranked above reconstruction evidence, because a
Source-X default is not a Revolution decision.

| Tag | Means |
|---|---|
| `OFFICIAL_REVOLUTION_GUIDE` | RevolutionUO's own guide / system pages (`revolutionuo.net/oyun_rehberi`, `/genel_kurallar`, `/oyuncu_komutlari`, …). Highest authority. |
| `OFFICIAL_REVOLUTION_UPDATE` | The official dated change log (`/guncellemeler`). Highest authority **and** carries an era. |
| `REVOLUTION_FORUM_GUIDE` | A structured player guide on the Revolution forum. |
| `REVOLUTION_PLAYER_DISCUSSION` | Player build/market/gameplay discussion. Strong when many independent posts agree. |

**Evidence priority:** official guide → official update → forum guide →
repeated player discussion → player memory → our reconstruction → generic
Sphere/UO only as a last resort.

### A rule may carry conflicting observations

Flattening a conflict into one sentence destroys the information. Any rule that
differs between the historical shard and our runtime is recorded with all of:

| Field | Meaning |
|---|---|
| `ERA` | Which Revolution period the historical claim belongs to |
| `SOURCE` | URL or file, so it can be re-checked |
| `CONFIDENCE` | HIGH / MEDIUM / LOW |
| `CURRENT_RUNTIME_STATE` | What our shard does today |
| `TARGET_REVOLUTION_STATE` | What the profile says it should do |
| `ACTION_REQUIRED` | none / investigate / restore / adapt / defer |

A row whose two states differ is an **`AUTHENTICITY_CONFLICT`**. That is a
finding, not an error, and it is never resolved by picking whichever side is
easier to implement.

---

## 0. Authenticity conflict register (M3.5)

The live scoreboard. Target values come from
`REVOLUTION_RULESET_PROFILE.md` (`revolution_2009_2010`).

| # | Rule | Current runtime | Target Revolution | Era | Confidence | Action |
|---|---|---|---|---|---|---|
| 1 | **Total skill cap** | **1000.0** (`SKILLSUM=10000`) | **700.0** | 2008–2010 | **HIGH** — 11 forum builds all exactly 700; official `.skilldusur` floor of 670.0 | **restore** (not yet done) |
| 2 | **Resisting Spells** | enabled, `ADV_RATE` and all | **inactive** | documented era | **HIGH** — official guide names it in a list of inactive skills | **restore** (not yet done) |
| 3 | Active skill count | 58 server / 49 client | **38** | documented era | HIGH | investigate — the delta is mostly stock skills Revolution never used |
| 4 | **Runebooks** | item exists, does nothing; no `IT_RUNEBOOK` in Source-X | full system: 8 pages, names, copying, charges | 2007–2009 | **HIGH** | **defer with a spec** — `REVOLUTION_RUNBOOK_SPEC.md` |
| 5 | **Anti-macro** | none | production verification every 2–3 h | 2008 / 2011 | **HIGH** | **adapt, not copy** — `REVOLUTION_ANTIMACRO_SPEC.md` |
| 6 | **NPC Teaching** | 30% of trainer's skill, 1gp per 0.1, keeps change | same | — | **HIGH — proven live** | **none: match** |
| 7 | Mark / Recall gating | Magery 60 / 40, 1 reagent each | same | 2009 | HIGH | none |
| 8 | Poisoned-weapon Magery ceiling | not verified present | **Magery ≤ 40.0 to WIELD a poisoned weapon.** Training Poisoning, casting Poison and applying poison are unrestricted | documented era | HIGH (official guide) | **resolved M3.6** — see §11 |
| 9 | Robe Eval gates | not verified present | Mage Robe 75.0, Special 98.1 | documented era | HIGH (official guide) | investigate |
| 10 | Fishing net gate | not verified present | Fishing **80.0** | documented era | HIGH (official guide) | investigate |
| 11 | Stat cap | 300 | **UNKNOWN** | — | — | **do not change** — no evidence either way |
| 12 | Guild Runebooks | absent | absent **in this profile** (they are 2012) | 2012 | HIGH | none — correctly absent |
| 13 | **Reagent consumption** | **`ReagentsRequired=0`** — spells cost no reagents at all | Revolution had a whole reagent economy: vendors, Reagent Crystals, and a dated 14.05.2009 update *reducing Recall's reagent count* | 2009 | HIGH | **restore** — see §11.2 |
| 14 | Runebooks (was #4) | **implemented in M3.6** as a scripted generic-gump item | 8 named pages, insertion, charges | 2009 | HIGH | **done, with gaps** — see §11.4 |
| 15 | **Engine era** | Source-X (modern maintained fork) | Revolution ran an era Sphere, roughly 0.56b, 2007–2011 | — | **UNKNOWN impact** | **audit** — combat/spell/gain formulas are engine-level and cannot be configured away. Raised in M3.6; not yet investigated. |

---

## 1. The skill list

### 1.1 What the client knows — `CLIENT_DATA_VERIFIED`

`local/revolution-client/skills.idx` + `skills.mul` hold **49 skills, indices 0–48**, the classic UO:R/LBR set. Parsed directly from the shipped files:

```
 0 Alchemy          13 Cooking            26 Resisting Spells   39 Veterinary
 1 Anatomy          14 Detecting Hidden   27 Tactics            40 Swordsmanship
 2 Animal Lore      15 Enticement         28 Snooping           41 Mace Fighting
 3 Item Identif.    16 Evaluating Intel.  29 Musicianship       42 Fencing
 4 Arms Lore        17 Healing            30 Poisoning          43 Wrestling
 5 Parrying         18 Fishing            31 Archery            44 Lumberjacking
 6 Begging          19 Forensic Eval.     32 Spirit Speak       45 Mining
 7 Blacksmithy      20 Herding            33 Stealing           46 Meditation
 8 Bowcraft/Fletch. 21 Hiding             34 Tailoring          47 Stealth
 9 Peacemaking      22 Provocation        35 Animal Taming      48 Remove Trap
10 Camping          23 Inscription        36 Taste Identif.
11 Carpentry        24 Lockpicking        37 Tinkering
12 Cartography      25 Magery             38 Tracking
```

The per-skill `useButton` byte marks which ones the client shows a "use" button for — that is a *client* hint and, as §3.1 shows, it does not match what the server will actually start from the skill list.

### 1.2 What the server has — `SCRIPT_VERIFIED` + `LIVE_VERIFIED`

`runtime/scripts/skills/` defines **58 skills, 0–57**: the 49 above plus Necromancy (49), Focus (50), Chivalry (51), Bushido (52), Ninjitsu (53), Spellweaving (54), Mysticism (55), Imbuing (56) and Throwing (57).

Live, a character's `0x3A` reports **all 58** (`m3_audit2`: `58 reported`). Skills 49–57 therefore exist server-side and are **invisible to the Revolution 2.0.3 client** — a bot can hold them, and no player could ever see them.

**Do not build Revolution characters on 49–57.** They are post-AoS content that this client cannot display, and there is no evidence Revolution used them.

### 1.3 Resisting Spells — resolved

The user does not remember Resisting Spells being part of RevolutionUO. The evidence:

| Source | Finding |
|---|---|
| `CLIENT_DATA_VERIFIED` | Present in the client's own `skills.mul` at index 26, named "Resisting Spells". |
| `SCRIPT_VERIFIED` | Present and fully defined: `skills/skill26_magicresistance.scp`, `[SKILL 26] DEFNAME=Skill_MagicResist KEY=MagicResistance TITLE=Warder`, with `ADV_RATE=10.0,200.0,800.0` — the same gain curve as Magery — and `EFFECT=0.0,90.0`. |
| `SCRIPT_VERIFIED` | Capped at 100.0 like every other skill in `[SKILLCLASS 0]`. |
| `LIVE_VERIFIED` | Reported by the server in the live skill list, at a random starting value like any unchosen skill. |

**Conclusion: Resisting Spells IS enabled on this runtime**, with no Revolution-specific alteration. The memory that it was absent is **not corroborated by anything in the current data set** — but nor is it refuted, because the runtime is stock Scripts-X and a historical Revolution customisation would have lived in scripts we do not have. Recorded as an open discrepancy in §9.

**Rule for builds:** do not put Resisting Spells in a bot build *merely because stock UO templates do*. Put it in only if a build genuinely wants it. That satisfies both readings.

---

## 2. Caps, creation and stats

### 2.1 Caps — `SCRIPT_VERIFIED`

`runtime/scripts/skills/skillclasses.scp` defines exactly one skill class, `[SKILLCLASS 0] Class_Undeclared`, and every player uses it:

| Limit | Value | Source |
|---|---|---|
| Per-skill cap | **100.0** | every skill line, e.g. `Magery=100.0` |
| Total skill cap | **1000.0** | `SKILLSUM=10000` (tenths). `CSkillClassDef` defaults to the same `10*1000`. |
| Per-stat cap | **100** | `STR=100 INT=100 DEX=100` |
| Total stat cap | **300** | `STATSUM=300` |

`CChar::Skill_GetSumMax` reads that class (or an `OVERRIDE.SKILLSUM` tag, which nothing sets). `runtime/sphere.ini` adds `OverSkillMultiply=2`: exceed the class limits by more than 2× and the server drops you back to them.

**This is a 1000.0 total, i.e. ten skills at GM — not 700.**

### 2.2 Character creation — `LIVE_VERIFIED`

Creating `RevolutionFisher` with the ordinary `0x00` packet asking for Fishing 50 / Mining 30 / Blacksmithy 20 produced exactly that:

```
[skills]  7 base=20.0   (Blacksmithy)
[skills] 18 base=50.0   (Fishing)
[skills] 45 base=30.0   (Mining)
[skills] 58 reported, trained sum 665.0, STR 30 DEX 30 INT 20 (sum 80)
```

* Each requested skill is clamped to **50** and the three to **100 total** (`CChar::InitPlayer`).
* Every *other* skill is handed a **random value up to 20.0** — `runtime/sphere.ini` `MaxBaseSkill=200`. That is why the trained sum starts at **665–745** across characters (`RevolutionMage2`: 743.5) rather than at 100.
* Stats start at **30 / 30 / 20 = 80** of the 300 allowed.

The consequence matters for every build decision: **a brand-new character already carries two thirds of the total skill cap in skills it did not choose.** A five-GM-skill build needs 500.0 and the headroom is only ~250–335, so reaching it requires *losing* the junk, not just gaining. `prog::PlanProgression` reports this as `needsSkillLoss`.

### 2.3 Stat gain — `SOURCE_X_DEFAULT`

`CChar::Skill_Experience` dishes out stat gains after every skill use, success or failure, toward the stats the skill's `STAT_*`/`BONUS_*` lines favour, bounded by `Stat_GetSumLimit()`. Magery is `STAT_INT=100 BONUS_INT=100 BONUS_STATS=15`, so casting pushes INT up. `runtime/scripts/skills/skill.scp` `[ADVANCE] STR=10000,4000,600` sets the rates.

Not yet measured live. `UNKNOWN`: how many casts move INT by one point in practice.

---

## 3. Using skills

### 3.1 Not every skill can be started from the skill list — `SOURCE_X_DEFAULT` + `LIVE_VERIFIED`

`CClient::Event_Skill_Use` (`src/game/clients/CClientEvent.cpp:595`) switches over only the skills a player clicks in the skills gump: Arms Lore, Item ID, Anatomy, Animal Lore, Eval Int, Forensics, Taste ID, Begging, Taming, Remove Trap, Stealing, Enticement, Provocation, Poisoning, Stealth, Hiding, Meditation and the bard skills. Everything else falls through to:

> `There is no such skill. Please tell support you saw this message.`

Confirmed live for **Fishing** (`m3_income1`) and, in M2, for **Healing**.

**The tool is the verb.** Fishing is started by double-clicking a fishing pole, mining by a pickaxe, healing by bandages, smithing by a smith hammer on an anvil. A bot that reasons "I know Fishing, therefore I can use skill 18" is wrong on this shard.

### 3.2 Skill gain — `SOURCE_X_DEFAULT`, arithmetic checked

`CChar::Skill_Experience` (`src/game/chars/CCharSkill.cpp:363`):

1. **No gain in a `REGION_FLAG_SAFE` area** — 21 AREADEFs and 4 ROOMDEFs on map 0 carry it, so where a bot trains matters.
2. **No gain once the total cap is reached**: `if (Skill_GetSum() >= Skill_GetSumMax()) iDifficulty = 0`.
3. `GAINRADIUS` gates gain from tasks far below your skill. It defaults to **0** (`CSkillDef.cpp:79`) and Magery does not set it, so Magery gains from *any* castable spell.
4. The chance comes from `ADV_RATE` alone, **not from the difficulty of the task**: `iChance = m_AdvRate.GetChancePercent(skillLevel)`, and `GetChancePercent` returns `100000 / uses-per-tenth` (`CValueDefs.cpp:162-178`). Roll `g_Rand.GetVal(1000) <= iChance` → **+0.1**.
5. **Failure trains too.** `Skill_Done` and `Skill_Fail` both call `Skill_Experience`.
6. Every use also rolls a **decay**: `if (iRoll * 3 <= iChance * 4) Skill_Decay()`, which deducts from some other skill.

Worked for Magery, whose `ADV_RATE=10.0,200.0,800.0` (uses per 0.1 gain at 0/50/100 skill):

| Magery | uses per 0.1 | gain chance per use |
|---|---|---|
| 50.0 | 2000 | **5.0 %** |
| 60.0 | 3200 | **3.1 %** |
| 100.0 | 8000 | 1.25 % |

50.0 → 60.0 is 100 gains, i.e. roughly **2,500 casts**.

### 3.3 Mana is the real limit — `LIVE_VERIFIED`

`runtime/sphere.ini` `Regen1=20`, and `CChar::Stats_GetRegenVal` returns `max(1, regenVal)` with **no INT term**. So mana comes back at a flat **1 point per 20 seconds for everyone** — a bigger pool buys burst, not throughput: 180 mana an hour, whoever you are.

Measured live (`m3_train1`): after the opening pool drained, the loop settled into one Night Sight (4 mana) every **80 seconds**, the waits landing at 15:30:24, 15:31:44, 15:33:04, 15:34:25, 15:35:44 — 80 s apart to the second.

**45 casts an hour × 5 % = 0.225 Magery an hour. Magery 50 → 60 is about 44 hours of casting.** That is the "skill farming was hard" memory, quantified, and it is a *mana* limit rather than a dice limit.

### 3.4 Meditation is the intended way out, and it is gated — `SCRIPT_VERIFIED` + `LIVE_VERIFIED`

`CChar::Skill_Meditation` grants **+1 mana per stroke** on the skill's `DELAY=2.0,1.0` timer — roughly a **tenfold** improvement on passive regen.

But two things gate it:

* The OSI-style **passive** meditation bonus exists in `skills/skill46_meditation.scp` as `f_meditation_setup`, and **every call site is commented out** (`//SRC.f_meditation_setup` on `@Start`, `@Success`, `@Fail`, `@Abort`). So there is no passive RegenMana bonus on this shard at all — only the active trance.
* The trance **fails at low skill**. Measured live (`m3_medit`) at Meditation 3.6: `You attempt a meditative trance.` → `You lose your concentration.` two seconds later, every time, and mana still crawled back at 19.3 s/point (0 → 20 in 385 s).

The bootstrap out of that hole is legitimate and free: a failed skill use still earns experience, so **repeated failed meditation attempts train Meditation**, which eventually makes the trance work, which then makes Magery trainable at a sane rate. At Meditation 3.6 the gain chance is ~42 % per attempt (`GetChancePercent(36)` ≈ 422/1000) and an attempt costs nothing but two seconds.

### 3.5 Anti-macro — `SOURCE_X_DEFAULT`

Searched for: gain windows, repeated-target suppression, per-target memory, AFK checks, captchas, unattended-macro detection, script-level `@SkillGain` limiters. **None found**, in Source-X or in `runtime/scripts`.

What does exist and is easily mistaken for anti-macro:

* `GAINRADIUS` — refuses gain from tasks far below your skill (unset for Magery).
* `REGION_FLAG_SAFE` — no skill gain at all in those regions.
* The decay roll, which takes 0.1 off some other skill on most uses.
* The `ADV_RATE` curve itself, which is punishing enough not to need help.

> `PLAYER_MEMORY`: RevolutionUO had restrictions around unattended skill farming.
> `CURRENT_RUNTIME`: no implementation found.

Recorded as a compatibility gap. **Do not invent a replacement.**

---

## 4. Fishing

`SCRIPT_VERIFIED` unless noted.

* Started by using an `i_fishing_pole` (graphic **0x0DBF**, `TYPE=t_fish_pole`), not from the skill list (§3.1).
* The newbie kit **equips** the pole rather than packing it — `i_fishing_pole` has `SKILL=Fencing`, so Sphere treats it as a weapon. A backpack-only search does not find it (`LIVE_VERIFIED`, `m3_income4`).
* `[SKILL 18]` — `FLAGS=skf_gather`, `DELAY=8.0`, `RANGE=4`, `ADV_RATE=2.5,50.0,200.0` (far kinder than Magery's), and it refuses while mounted.
* Sphere rolls **1–2 strokes** per gather (`m_atResource.m_dwStrokeCount = rand(2)+1`), so a cast can take **16 seconds**, and cutting it short cancels it: *"You pull your line back in and stop fishing."* (`LIVE_VERIFIED`)
* **Fish are per-tile, and most tiles have none.** `[REGIONTYPE r_default_water t_water]` is `RESOURCES=60.0 mr_nothing` against `10.0` for each of `mr_fish1..4` (`core/regionresources.scp:92-97`). Each `mr_fishN` holds `AMOUNT=9,30` fish, yields `REAPAMOUNT=1,3`, needs `SKILL=1.0,100.0`, and regenerates on `REGEN=60*60*10` — **ten hours**.
* So *"There are no fish here."* is not a misconfiguration; it is a 60 % chance that this particular tile rolled the empty resource, and it will keep saying it for ten hours. **A fisherman must move.** (`LIVE_VERIFIED`: 36 casts across 12 tiles → 11 barren, 8 fished-and-failed, **4 caught**.)
* The catch is `i_fish_big_1..4` (**0x09CC–0x09CF**), `VALUE=2`, `WEIGHT=5.0`.
* `VENDOR_S_FISHER` both **sells poles** and **buys fish** (`templates/tm_vend.scp:1012-1023`), so one NPC closes the loop. The Britain docks have two.
* **Five stones apiece is a real constraint.** On a STR 30 character a dozen fish is an overweight pack, and Sphere drops the rest on the ground: *"You put the fish at your feet. It is too heavy."* (`LIVE_VERIFIED`, `m3_income7` — eleven of the run's catches landed on the dock.)
* **Cutting a fish quadruples it.** Using any blade — `IT_WEAPON_FENCE`/`SWORD`/`AXE`/`MACE_SHARP`/`CARPENTRY_CHOP` — on an `IT_FISH` runs `pItemTarg->SetID(ITEMID_FOOD_FISH_RAW); pItemTarg->SetAmount(4 * pItemTarg->GetAmount())` (Source-X `CClientTarg.cpp:1948-1951`). No skill check, no tool wear, no failure case. The fishing pole itself will **not** do it: it is `TYPE=t_fish_pole`, not a weapon type. A dagger will, and `MALE_DEFAULT` hands every new character one.
* Dclicking the blade arms the target cursor whether or not it is equipped, because `OF_NoDClickTarget` (0x01) is **not** set in this runtime's `OptionFlags=08|080|0200` (`CClientUse.cpp:435-445`).
* Economics of the processing step, from the shard's own tables:

  | item | `VALUE` | `WEIGHT` | appears in any `BUY=` line? |
  |---|---|---|---|
  | `i_fish_big_1..4` (whole) | 2 | 5.0 | `VENDOR_B_FISHER`, `VENDOR_B_COOK` |
  | `i_fish_cut_raw` (steak, raw) | **3** | 0.1 | **`VENDOR_B_COOK` only** (`tm_vend.scp:730`) |
  | `i_fish_cut_cooked` (steak, cooked) | 3 | 0.1 | **none in the entire ruleset** |
  | `i_fish_cooked_small` | **1** | 0.2 | none |

  Measured live (`m3_cut1`): whole fish **1 gp**, raw steaks **2 gp** — so one fish is worth 1 gp sold whole and **8 gp** carved, at one twelfth the weight. **Cooking is a loss**: it does not raise `VALUE` and removes every buyer, and cooked small fish is worth less than raw. On this shard Cooking is a food and skill-gain activity, not an income multiplier. Note the buyer changes too — the fisherman buys only whole fish.

---

## 5. Magery, spells and travel

`SCRIPT_VERIFIED` unless noted.

* `MagicFlags` is unset in `sphere.ini`, so **precast is off**: the target cursor arrives first and the cast follows (M2).
* `ReagentsRequired=0` — no reagents are consumed. `ManaLossFail=0` and `ManaLossAbort=0` — a failed or aborted cast costs **no mana**.
* A **scroll** bypasses the spellbook and the skill requirement entirely (`CChar::Spell_CanCast`, the non-character-source branch) and halves the cast difficulty (`Spell_CastStart`). It only requires `ATTR_MAGIC` and being on your person.
* The newbie spellbook is `MORE1=0x382A8C38` — **three spells in each of circles 1–4 and nothing above**: spells 4, 5, 6 (Heal, Magic Arrow, Night Sight), 11, 12, 16, 18, 20, 22, 28, 29, 30. **Recall (32) is not among them.**
* Recall = `[SPELL 32]`, Magery 40, 11 mana. Mark = `[SPELL 45]`, **Magery 60**, 20 mana. Gate Travel = `[SPELL 52]`, Magery 70, 40 mana.
* A recall rune is `i_rune_marker` (**0x1F14**), `TYPE=t_rune`, created blank (`MOREP=-1,-1`) with 10 charges, sold by `VENDOR_S_MAGE_SHOP` at 2–10 gold. Mark writes the destination and renames the rune to the region; each Recall spends a charge.
* **No vendor on this shard sells a 6th-circle scroll.** `random_sixth_circle` (which contains `i_scroll_mark`) appears only in monster loot, dungeon chests, and Inscription crafting at `Inscription 60.0, Magery 50.0`.
* Public moongates: ten Felucca gates, full destination mesh, gump-driven. See `docs/M2_5_WORLD_NAVIGATION.md` §9.

**The Mark gate, stated plainly:** creation clamps Magery to 50, the newbie book has no Mark, and no shop sells the scroll. The only honest routes to Mark are training Magery to 60, or obtaining a 6th-circle scroll from loot or from a scribe. Both are gameplay, and both are slow (§3.3).

---

## 6. Runebooks — historical discrepancy

> `PLAYER_MEMORY`: RevolutionUO players made and used runebooks.

Current runtime, `SCRIPT_VERIFIED` + source-read:

* `i_spellbook_runebook` exists as itemdef **0x22C5** with `TYPE=t_normal //fixme: or t_runebook`, `WEIGHT=1.0`, `RESOURCES=8 i_scroll_blank,1 i_rune_marker,1 i_scroll_recall,1 i_scroll_gate_travel`, `SKILLMAKE=Inscription 45.0`, and `ATTR=attr_magic|attr_newbie` on create. A larger variant at `i_unsorted.scp:4350` carries the same `//fixme`.
* `tm_magic.scp` has `random_spellbooks { i_spellbook 2 i_spellbook_runebook 1 }`, so the item does drop.
* **Source-X has no `IT_RUNEBOOK` type at all.** `t_runebook` is not in `item_types.h`, and the engine has no runebook handling of any kind.
* No gump, no entry storage, no charge mechanic, no recall-from-book path exists anywhere in the runtime.

So on *this* runtime a runebook is a craftable, tradeable, decorative object that does nothing. The `//fixme` comments are the giveaway: Scripts-X itself considers this unfinished, which is consistent with the memory being about a **Revolution-specific custom system** built on top of a shard that did not have one natively.

| Question | Answer |
|---|---|
| Actual historical Revolution feature? | `HISTORICAL_UNVERIFIED` — strong player memory, no evidence to hand. Runebooks are ubiquitous in UO generally, so the memory is entirely plausible. |
| Current runtime support? | **None.** Item exists, behaviour does not. |
| Missing custom script? | **Most likely.** A Sphere shard of that era would implement runebooks as a scripted `t_script` item with a dialog, exactly as `d_moongates` implements moongates. |
| Native Source-X limitation? | Yes — no `IT_RUNEBOOK`. Any implementation must be script-side. |
| Reconstruction requirements | A `t_script` itemdef with `@DClick` opening a gump listing entries; storage of marked destinations as contained `i_rune_marker` items or as TAGs; a per-entry "recall" button invoking the Recall spell with that destination; optionally charges and a recharge. All of it is script work — no Source-X change. |

**Not implemented in M3.** It is a future Revolution-compatibility restoration task, and it should not be built until the historical behaviour is established with more confidence than "UO generally had these".

---

## 7. Economy

`SCRIPT_VERIFIED` unless noted.

* Sale price tracks the itemdef's `VALUE`. M2 measured a candle (`VALUE=6`) selling for **5 gold**. Fish are `VALUE=2`.
* Vendors answer a **shop keyword spoken within 3 tiles**, and by default **every vendor in earshot replies** (M2). The alchemist next door will happily answer a request meant for the mage (`LIVE_VERIFIED`, `m3_rune4`).
* **Naming the NPC fixes that, and it is a server rule, not a convention.** `CClientEvent.cpp:1962` calls `NPC_OnHearName` on each character in earshot; a match sets `bNamed` and **`break`s** the listener loop, so exactly one NPC answers. An unnamed keyword falls through to *"pick closest NPC"*, and the closest is not necessarily the one you walked to — `m3_sell2` stood one tile from the provisioner, said "buy", and got the **shoemaker's** stock (`LIVE_VERIFIED`). `NPC_OnHearName` also accepts the trade name and the literal words `VENDOR ` and `GUARD `.
* **An NPC will not understand a keyword until it is in conversation.** Speaking to a vendor that has not yet opened with `e_Human_ConvInit` routes the line to `e_Human_HearUnk` — *"Hmm?"*, *"Ye talk confusing."* — and no shop opens (`LIVE_VERIFIED`, `m3_income7`). Approach, let it greet, say hello, then trade.
* **Reach for any use, trade or shop is two tiles.** `CChar::CanTouch` ends in `if (iDist > 2) fCanTouch = false` (`CCharStatus.cpp:1423`), over a 3-D top-level distance, after an LOS check. Vendors wander within their spawner's `walkRange` (5), so a position read seconds ago is stale: *"You can't reach the Vendor"* (`LIVE_VERIFIED`, `m3_sell1`).
* **Connections are rate-limited per IP, not per account, and the limiter does not decay.** `MaxConnectRequestsPerIP=50` counts every connection *attempt* from an address and, per its own comment, *"resets only after `<NetTTL>` seconds elapsed since last connection attempt"* — `NetTTL=60*5`. On overflow `f_onserver_connectreq_ex` rejects **and bans the IP** for 300 s. Because a rejected attempt also resets the clock, **retrying makes the ban permanent for as long as you keep trying**; sessions die at the socket with `WSA=10054` before any packet is exchanged (`LIVE_VERIFIED` — the ban observed renewing itself at 17:43, 17:44, 17:44, 17:45). A bot population must stagger its logins and back off *silently* on failure.
* A vendor's **trade** is only visible in its paperdoll title, and a substring match is not enough: "Caedmon, the mage guildmaster" contains "the mage", and the **guildmaster keeps no shop at all** (`LIVE_VERIFIED`, `m3_rune5`).
* Britannia's shops are **multi-storey**, and speech range is three-dimensional. Standing on the gallery above a vendor is standing nowhere useful (`LIVE_VERIFIED`, `m3_rune8`).
* Newbie kits: `MALE_DEFAULT` gives **1000 gold**, a book, a candle and a dagger. `[NEWBIE FISHING]` adds a pole and a floppy hat; `[NEWBIE BLACKSMITHING]` adds tongs, a pickaxe, **50 iron ingots** and an apron; `[NEWBIE MINING]` a pickaxe; `[NEWBIE HEALING]` 50 bandages and scissors.
* Secure player trade: opened by **dropping an item on another player** — the only route a 2.0.x client has. No virtual gold ledger for this client version, so **gold is traded as coins in the window**. Full protocol notes in `include/uo/trade.h`.

---

## 8. Movement, travel and the world

Covered in full by `docs/M2_5_WORLD_NAVIGATION.md`. The load-bearing facts:

* Revolution ships **one facet**; maps 2–5 stay disabled (that document, §2).
* `map0.mul` is the **ML size, 7168 × 4096**.
* Running is the normal gait; `SubmitStep()` is the only `0x02` sender.
* A teleporter sits **inside Yew** at (535,992) and sends you to Heartwood; unplanned pads are kept out of the walker's path.

---

## 9. Where memory and runtime disagree

Neither column is dismissed. These are the open questions this project should keep chasing.

| Topic | `PLAYER_MEMORY` | Current runtime | Status |
|---|---|---|---|
| Total skill cap | 7x / ~700-point completed builds | **1000.0** (`SKILLSUM=10000`), ten GM skills | **Open.** The project's own `CLAUDE.md` states 700. The runtime says 1000. Nothing in the shipped data supports 700. |
| Resisting Spells | Not part of RevolutionUO | Fully defined and enabled (§1.3) | **Open.** Likely a stock Scripts-X default rather than a Revolution choice; do not build around it either way. |
| Runebooks | Players made and used them | Item exists, behaviour does not (§6) | **Open, and most likely a missing custom system.** |
| Unattended-macro restrictions | Existed | None found anywhere (§3.5) | **Open, likely a missing custom system.** |
| Skill farming was hard | Yes | Confirmed, and quantified: **0.53 Magery/hour measured** over 1 h 45 m of continuous casting-and-meditating, so ~17 hours for Magery 50→60 — ~44 hours without meditation, at flat mana regen (§3.3) | **Corroborated.** |
| Fire Robes, mounts, head-hunter rewards, S.O.S. chains | Remembered as real | `UNKNOWN` — not yet audited | To do. |

A general caution learned the hard way in M2.5 and again here: **this runtime is stock Scripts-X**. Where the memory describes something distinctive, the likely explanation is a Revolution customisation that this data set does not contain — not that the memory is wrong.

---

## 10. Historical Revolution rules recovered from the official archive (M3.5)

The archive at `revolutionuo.net` is **still online**, so these are primary
sources rather than reconstruction. Era tags matter: see
`REVOLUTION_RULESET_PROFILE.md`.

### 10.1 The active/inactive skill list — `OFFICIAL_REVOLUTION_GUIDE`

Source: `https://www.revolutionuo.net/oyun_rehberi`

The guide names **38 active skills** individually, then states, verbatim:

> "Ayrıca sunucuda geliştirilemeyen ve aktif olmayan skiller sırasıyla:
> Herding, Remove Trap, **Resisting Spells**, Enticement, Peacemaking,
> Provocation, Sprit Speak, Forensic Evaluation, Taste Identification."
>
> *"Additionally, the skills that cannot be developed and are inactive on the
> server are, in order: …"*

**This settles the Resisting Spells question as far as the historical shard is
concerned: it was inactive.** Our runtime has it fully enabled with Magery's own
gain curve. `AUTHENTICITY_CONFLICT` #2.

It also means the historical skill list was **38**, against 58 in our Source-X
runtime and 49 in the client's `skills.mul`. Most of the difference is stock
Sphere skills Revolution simply did not run.

### 10.2 The 700-point budget — `REVOLUTION_PLAYER_DISCUSSION` + `OFFICIAL_REVOLUTION_GUIDE`

Eleven independent player builds, from three threads across **June 2008 to July
2010**, every one summing to **exactly 700**. Full table in
`REVOLUTION_UO_BUILD_COMPENDIUM_v2.md` §2. One of them spends its 700 over
**nine** skills, which is the direct disproof of "7x means seven skills" —
**7x means 700 points**.

Independently, the official player-command page documents:

> `.skilldusur` — "Herhangi bir yeteneğinizi yetenek toplamınız **670.0** olana
> kadar düşürmenizi sağlar"
> *"lets you lower any of your skills until your skill total is 670.0"*

Source: `https://www.revolutionuo.net/oyuncu_komutlari`

A 670.0 floor is a coherent design under a 700 cap and an odd one under 1000.

**Conclusion: the Revolution total skill cap was 700.0.** Our runtime is
1000.0 (`SKILLSUM=10000`). `AUTHENTICITY_CONFLICT` #1.

Note the archive contains **no** update entry in 1200+ changes that alters a
skill or stat cap — consistent with the cap being set once, at the start, and
never announced again.

### 10.3 Gates the official guide states outright

| Rule | Value |
|---|---|
| Mage Robe | Eval Int **75.0** |
| Special Robes (Fire / Energy / Earth / Ice) | Eval Int **98.1** |
| Bandage cures poison | Healing **60.0** |
| Bandage resurrects | Healing **80.0** |
| Fishing nets | Fishing **80.0** |
| **Applying poison to weapons** | the warrior's **Magery may not exceed 40.0** |

The last is structural: it makes the poisoned-weapon warrior and the
Magery 80–100 warlock mutually exclusive families.

### 10.4 Player commands — `OFFICIAL_REVOLUTION_GUIDE`

Source: `https://www.revolutionuo.net/oyuncu_komutlari`

Revolution players bought and sold with **commands**, not only speech:
`.al` (buy from vendors), `.sat` (sell to vendors), plus `.bnk`, `.cek`
(withdraw), `.transfer` (between your own characters), `.stat`, `.regs`,
`.potions`, `.ingots`, `.ores` (inventory counters), `.spawntakip`,
`.nerdeyim` (web map), and a full set of player-vendor and ship commands.

Our bot uses the speech keywords, which also work. Worth recording because a
command interface implies the shard expected a lot of vendor traffic.

### 10.5 Runebooks — `OFFICIAL_REVOLUTION_UPDATE`

Dated, quoted and analysed in full in `REVOLUTION_RUNBOOK_SPEC.md`. Headline:
runebooks existed by **15.08.2007**, reached **8 named pages on 12.05.2009**,
gained **Inscription copying and Recall-scroll charges on 13.05.2009**, and
guild runebooks arrived **07.01.2012** (outside our profile).

**The player memory was right, and more detailed than expected.**

### 10.6 Anti-macro — `OFFICIAL_REVOLUTION_UPDATE`

Dated and analysed in `REVOLUTION_ANTIMACRO_SPEC.md`. Headline: a verification
module was already being bug-fixed on **15.04.2008**; on **22.02.2011** it was
re-activated with a code screen every **2–3 hours** *on continuous production*;
in **2016** failing to answer within a minute disconnected you.

The trigger was **production**, not play in general, and nothing says it
altered skill-gain formulas.

### 10.7 NPC Teaching — `LIVE_VERIFIED`, and it matches memory exactly

The stock Sphere Teaching system is present and working, and this runtime's
configuration reproduces the remembered Revolution numbers precisely.

Configuration (`runtime/sphere.ini`): `NPCTrainCost=1`, `NPCTrainPercent=30`,
`NPCTrainMax=420`. Mechanism: `CChar::NPC_OnTrainHear` /
`NPC_OnTrainPay` (Source-X `CCharNPCAct_Vendor.cpp:370` / `:273`).

Live proof, `m35_teach1`, `RevolutionFisher` and Georgetta the blacksmith:

| | |
|---|---|
| Arms Lore before | **2.6** |
| Quote | *"For **191 gold** I will train you in all I know of ArmsLore."* |
| Handed over | 250 gold |
| Arms Lore after | **21.7** |
| Backpack gold after (server save) | **852**, from 1102 |

The arithmetic is exact: 21.7 − 2.6 = 19.1 points = **191 tenths = 191 gold**,
i.e. `NPCTrainCost=1` gold per 0.1 skill. The ceiling of 21.7 implies
Georgetta's own Arms Lore is 72.3, inside the `{50.0 75.0}` her chardef rolls,
i.e. `NPCTrainPercent=30`.

So the remembered "**about 30.0, around 300gp**" is exactly right: a GM trainer
teaches to 30.0, and 0 → 30.0 costs 300 gold.

**One finding that is not in the memory: the trainer keeps the change.** 250
was handed over against a 191 quote and 250 was taken. A bot must hand a
trainer the quoted amount and not a round number.

### 10.8 Mark and Recall — `LIVE_VERIFIED` (fenced probe)

Proven with `RevolutionMageGM`, whose Magery 60 was set by console. This is a
mechanics result only and changes nothing about legitimate progression.

* Mark is `[SPELL 45]`, `SKILLREQ=MAGERY 60.0`, `MANAUSE=20`; Recall is
  `[SPELL 32]`, `SKILLREQ=MAGERY 40.0`, `MANAUSE=11`; both consume black pearl,
  blood moss and mandrake root.
* At **exactly** the skill requirement, Mark fizzles most of the time — six
  fizzles among the first attempts. That is the shard's own difficulty curve,
  and it is worth knowing before a bot budgets reagents for marking runes.
* On success the server sets the rune's `MOREP` to the caster's tile **and
  names the rune after the region automatically** — the recovered rune reads
  `NAME=Britain`, `MOREP=1490,1555,30`.
* An unmarked rune answers Recall with *"The recall rune is blank."*

---

## 11. M3.6 findings

### 11.1 The poison rule, resolved exactly — `OFFICIAL_REVOLUTION_GUIDE`

M3.5 recorded this as "a poisoner may not exceed Magery 40.0". **That was a
paraphrase, and it was wrong.** Going back to the source settles it:

> "Zehirleyebilme becerisidir. **Magery(büyücü) yeteneği ile yaptığınız Poison
> (zehir) büyüsünün gücünü arttırabilirsiniz.**"
> *"It is the ability to poison. With Magery you can increase the power of the
> Poison spell you cast."*

> "Warriorların silah sürmesi için bu skill yeterlidir. Poison şişesinin
> seviyesine göre silah sürülebilmektedir. **Büyücü yeteneği 40.0 ın üstündeki
> savaşçılar zehirli silahı KULLANAMAZLAR.**"
> *"This skill is sufficient for warriors to apply poison to weapons… **Warriors
> with Magery above 40.0 CANNOT USE poisoned weapons.**"*

| Question | Answer |
|---|---|
| Train Poisoning at high Magery? | **Yes** |
| Cast the Poison spell? | **Yes** — and Poisoning *increases its power* |
| Apply poison to a weapon? | **Yes** — "this skill is sufficient" |
| **Wield** a poisoned weapon above Magery 40.0? | **No** |

The restriction is on **wielding**, and only that. This vindicates the eleven
verified warlock builds (Magery 85–100 *with* Poisoning 45–100): they poison
with the **spell**, which is precisely why Poisoning is worth points to a mage.

Encoded as four separate predicates in `include/uo/rules.h` so the distinction
cannot collapse back into one rule.

### 11.2 This runtime does not require reagents at all — `SCRIPT_VERIFIED`

`runtime/sphere.ini:1060` — **`ReagentsRequired=0`**, with `ReagentLossFail=0`
and `ReagentLossAbort=0`.

Two consequences:

**A correction to the M3 report.** M3 stated its Magery block "consumed roughly
214 spider silk and 214 sulfurous ash". It consumed none — the character owns
no reagents at all, which is why it could cast 214 times. The claim was
inferred from the spell's `RESOURCES` line rather than measured, and it is
withdrawn.

**An authenticity conflict.** Revolution's reagent economy was real: reagent
vendors, Reagent Crystals for restocking, and a dated update
(`OFFICIAL_REVOLUTION_UPDATE` 14.05.2009) *reducing Recall's reagent count from
three to one* — a change that is meaningless unless reagents were consumed.
Conflict register #13.

### 11.3 Recall runes wear out — `LIVE_VERIFIED`

Sphere runes carry charges in `MORE1`, decremented per Recall. The server warns
*"The recall rune is starting to fade"*, and when they run out **the rune is
destroyed** — confirmed against the world save, where a rune present before a
runebook travel was gone afterwards while the page's stored point survived.

This is why a runebook is worth carrying, and it is the reason the M3.6
implementation makes **the page**, not the rune, the destination of record.

### 11.4 Runebooks, implemented — `LIVE_VERIFIED`

Implemented as a scripted generic-gump item in
`runtime/scripts/revolution/revolution_runebook.scp`. **Source-X unmodified.**
Full detail and the remaining gaps are in `REVOLUTION_RUNBOOK_SPEC.md` §8.

Proven live on the fenced probe: the book opens as an 8-page gump; a marked
rune inserts into a chosen page and the page takes its name and destination
("Page 01 now holds Britain"); the state survives a relog; a Recall scroll adds
a charge; and travelling from a page casts the **real** `[SPELL 32]` Recall —
*"Kal Ort Por"* — landing the character on the page's exact stored tile
(`player @(1490,1555,30)`).

### 11.5 Sphere scripting notes worth keeping

Each of these cost a live iteration and none is documented anywhere obvious:

* A point's components are **`MOREX`/`MOREY`/`MOREZ`**; `MOREP` is only the
  formatted string, and `MOREP.X` silently yields nothing.
* **`BASEID` is a string** (`g_Cfg.ResourceGetName`, `CBase.cpp:183`) and must
  be compared with `STRCMP`, not `!=`.
* Inside a `[DIALOG]` — display *or* button handler — the script object is the
  **player**, not the item that opened the gump. `<UID>` there is the character.
* **`LOCAL` does not propagate into a called `[FUNCTION]`**; pass values as
  arguments and read `<ARGN>`.
* A `t_container` needs **`TDATA3`** (gump) and **`TDATA4`** (capacity) or the
  server warns and misbehaves.
* `SERV.TRIGGER` does not exist; call a function by naming it.
* **`SRC.CAST=<spell>`** runs the genuine spell through
  `CClient CV_CAST -> Cmd_Skill_Magery`, so skill checks, mana and fizzles all
  apply — the legitimate way for a script to make a player cast.
