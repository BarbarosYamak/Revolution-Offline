# Revolution Ruleset Profile

Date: 2026-08-26 (M3.5). Companion to `REVOLUTION_GAMEPLAY_TRUTH.md`.

RevolutionUO ran for roughly a decade and changed constantly. A reconstruction
that merges 2007, 2009, 2011 and 2016 rules is not RevolutionUO; it is a shard
that never existed. This document fixes **one target era**, says what is in it,
and says what is deliberately left out.

---

## 1. Target era

> **`revolution_2009_2010`** — the Revolution10-era ruleset, anchored on the
> client build shipped with this project.

Every numeric rule elsewhere in this project should carry this profile name or
an explicit exception.

### Why this window, and not the wider 2008–2010 guess

The strongest evidence is not a forum post; it is the client we were given.

| Artefact | Date | What it fixes |
|---|---|---|
| `local/revolution-client/client.dll` | **2010-10-24** | the core client build |
| `local/revolution-client/LoaderDLL.dll` | 2010-10-23 | shipped with it |
| `local/revolution-client/unrar.dll` | 2010-03-15 | packaged the same year |
| `local/revolution-client/WCP.dll` | 2011-02-17 | a later patch to the same install |
| `local/revolution-client/Revolution.exe` | 2016-01-06 | launcher only, rebuilt for the revival |
| `map0.mul` | 89,915,392 bytes = 896×512 blocks | post-ML world size, consistent with the era |

The playable client data set is **October 2010, patched to at least February
2011**. The 2016 executable is a launcher wrapper, not a different game — it is
why the archive contains 2016 material that does **not** belong to this profile.

The forum corroborates the same window from the other side: build threads from
**June 2008** and **July 2010** use identical rules (§2), so the window is
stable across it rather than a snapshot.

**Working window: 2009-01-01 → 2011-02-17**, centre of mass late 2010.

Where a rule changed inside that window, the profile takes the **latest value
at or before 2010-10-24** unless noted.

---

## 2. Included systems and values

Each row carries its evidence class and source. `OFFICIAL_*` beats forum, forum
beats memory, memory beats a Source-X default.

### 2.1 Character budget

| Rule | Value | Evidence |
|---|---|---|
| **Total skill cap** | **700.0** | `REVOLUTION_FORUM_GUIDE` + `OFFICIAL_REVOLUTION_GUIDE` — see §5 of the M3.5 report. Eleven independent builds, 2008 and 2010, all summing to exactly 700; the official `.skilldusur` command documents a **670.0** skill-total floor. |
| Per-skill cap | 100.0 | Universal in the build posts; every allocation tops out at 100. |
| Skills used per character | 7–9 | "7x" is **700 points**, not seven skills — one 2008 build spends its 700 across nine. |
| Stat cap | **225 total, 100 per stat** | Derived from ten player builds across two classes. See §4. |

### 2.2 Skills

| Rule | Value | Evidence |
|---|---|---|
| Active skills | **38** | `OFFICIAL_REVOLUTION_GUIDE` (`/oyun_rehberi`) lists them individually. |
| **Inactive skills** | Herding, Remove Trap, **Resisting Spells**, Enticement, Peacemaking, Provocation, Spirit Speak, Forensic Evaluation, Taste Identification | `OFFICIAL_REVOLUTION_GUIDE`, verbatim: *"Ayrıca sunucuda geliştirilemeyen ve aktif olmayan skiller sırasıyla: …"* |
| Magery training bands | 0–30 Night Sight · 30–40 Bless · 40–60 Greater Heal · 60–70 Magic Reflection · 70–80 Reveal/Invis/Energy Bolt · 80–90 Energy Field/Mass Dispel · 90–100 Earth Elemental/Earthquake | `REVOLUTION_FORUM_GUIDE` topic 59111 |
| Alchemy bands | 15.1–25.1 Heal · 25.1–35.1 Cure · 35.1–55.1 Gr. Agility · 55.1–65.1 Gr. Heal · 65.1–90.1 Gr. Cure · 90.1–100 Deadly Poison | same |
| Blacksmithy | Dagger to 70.1, then Short Spear | same |

### 2.3 Magic, robes and combat gates

| Rule | Value | Evidence |
|---|---|---|
| Mage Robe | Eval Int **75.0** | `OFFICIAL_REVOLUTION_GUIDE` |
| Special Robes (Fire/Energy/Earth/Ice) | Eval Int **98.1** | same |
| Healing: cure poison | Healing **60.0** | same |
| Healing: resurrect | Healing **80.0** | same |
| Fishing nets | Fishing **80.0** | same |
| **Poisoned weapons vs Magery** | a character above **Magery 40.0** may not **wield** a poisoned weapon. Training Poisoning, casting Poison and applying poison are all unrestricted | same. **Corrected in M3.6** — the earlier wording said "applying", which would have outlawed every verified Magery-100/Poisoning-100 warlock |

### 2.4 Travel

| Rule | Value | Evidence |
|---|---|---|
| Runebook pages | **8**, each separately nameable | `OFFICIAL_REVOLUTION_UPDATE` 12.05.2009 |
| Rune insertion | targets the page currently open | 12.05.2009 |
| Page transfer between books | supported, via the open-page + "rune ekle" procedure | 13.05.2009 |
| Runebook copying | added to the **Inscription** menu | 13.05.2009 |
| Runebook charging | insert **Recall scrolls**; a charged use needs **no Magery** | 13.05.2009 |
| Runebook restrictions | no marking/gating onto water; destination must be clear of players and magic items | 06.04.2008 |
| Recall reagents | 1 each (reduced 14.05.2009 from 3 mandrake/blood moss/black pearl) | `OFFICIAL_REVOLUTION_UPDATE` 14.05.2009 |
| Gate Travel reagents | **6** each mandrake / black pearl / sulphurous ash | 14.05.2009 — the reduction to 1 is **2011-03-24, outside this profile** |

### 2.5 Economy and rules

| Rule | Value | Evidence |
|---|---|---|
| Player vendors + searchable cooperative | yes | `OFFICIAL_REVOLUTION_GUIDE` `/tezgahtarlar_kooperatifi` |
| Trade/vendor tax | ~10% around 2010 | `OFFICIAL_REVOLUTION_UPDATE` (5% is 2016, excluded) |
| NPC buy/sell interaction | `.al` and `.sat` **commands**, alongside speech | `OFFICIAL_REVOLUTION_GUIDE` `/oyuncu_komutlari` |
| Skill lowering | `.skilldusur`, down to a **670.0** total | same |
| Anti-macro | module present and being fixed by **15.04.2008**; verification-code screen on continuous production every **2–3 hours** from **22.02.2011** | `OFFICIAL_REVOLUTION_UPDATE` |
| Multi-client | more clients than humans present is forbidden | `OFFICIAL_REVOLUTION_GUIDE` `/genel_kurallar` |

---

## 3. Excluded — later than this profile

These are real Revolution systems. They are **not** in `revolution_2009_2010`
and must not be implemented as if they were.

| System | Date | Why excluded |
|---|---|---|
| **Guild Runebooks** (from guild stones, mirroring the leader's book) | **07.01.2012** | 14 months after the client build. *(Note: the Bible v1 dated this 2011; the update archive says 2012.)* |
| Gate Travel reagents reduced to 1 | 24.03.2011 | after the profile window |
| Store Crystals / bulk loadout shortcuts | 2010–2012 | later marketplace generation |
| Ships and cannons, safe boxes | 2010–2012 | ditto |
| 2016 combat and Cure rebalances | 2015–2016 | Revolution16, a different ruleset |
| Anti-macro one-minute disconnect | 21.01.2016 | 2016 enforcement, not 2010 |
| Trade tax reduced to 5% | 2016 | ditto |
| Revolution16 client behaviour | 2016 | our client data is 2010/2011 |

**Borderline, flagged rather than silently included:** the 22.02.2011
*"anti-makro modülü aktif edildi … 2-3 saatte bir"* entry is four months after
the client build but describes the module the 2008 entry was already fixing. It
is treated as **the best available description of the mechanism**, with its
date recorded, not as proof of the 2010 cadence. See
`REVOLUTION_ANTIMACRO_SPEC.md`.

---

## 4. Unresolved in this profile

Recorded as open rather than filled in with generic UO.

| Question | State |
|---|---|
| **Stat cap (STR/DEX/INT total)** | **225 total, 100 maximum per stat.** RESOLVED in M3.8 — see below. The runtime allows 300, so it is more permissive than Revolution and the bot must enforce 225 itself. |

### Stat cap — resolved M3.8, and the earlier entry was wrong about its own evidence

This row previously read *"Not one archive source found. Build threads discuss
skills only and never post stats."* **That second sentence was false**, and it is
why the question stayed open: the search had concluded the evidence did not
exist rather than that it had not been found.

The forum is publicly readable. Two unrelated threads, two different classes,
**ten builds, every one totalling exactly 225**:

| Thread | Build | Total |
|---|---|---|
| *En mükemmel warlock statları* (54877) | 90 / 100 / 35 | 225 |
| | 100 / 100 / 25 | 225 |
| | 90 / 90 / 45 | 225 |
| | 98 / 97 / 30 | 225 |
| | 85 / 100 / 40 | 225 |
| *Hırsız Statları* (26120) | 50 / 100 / 75 | 225 |
| | 100 / 100 / 25 | 225 |
| | 25 / 100 / 100 | 225 |
| | 90 / 100 / 35 | 225 |

**This is DERIVED evidence, not a quotation.** No poster states the cap, because
nobody states what everyone knows — which is exactly why a search for a
statement found nothing. Ten independent builds summing to the same number, none
exceeding 100 in a single stat and none below 25, is not coincidence.

Confidence **HIGH**, classification `DERIVED` rather than `OFFICIAL` — the
distinction matters, and if a dated source ever contradicts it, the source wins.

**The runtime allows 300.** Same shape as the taming divergence: the server is
more permissive than Revolution, so the rule is enforced bot-side and the server
gap is recorded as `SERVER_AUTHENTICITY_DEBT` rather than patched.
| Whether 700 was enforced by config or by a custom system | Unknown. The effect is documented; the mechanism is not. |
| Exact ore / wood / leather tables for 2010 | Changed repeatedly; not yet extracted per-era. |
| Head Hunter payout formula | System documented, numbers not. |
| Treasure map level requirements | Not extracted. |
| Spawntakip taming requirements per mount | Schedule known, thresholds not. |
| Whether Revolution ran stock Sphere Teaching or a custom one | The behaviour matches stock exactly (§7 of the M3.5 report), which is evidence *for* stock but not proof. |

---

## 5. Compatibility gaps against the current runtime

Where our Source-X + stock Scripts-X reconstruction differs from this profile.

| Rule | Profile target | Current runtime | Status |
|---|---|---|---|
| Total skill cap | **700.0** | **1000.0** (`SKILLSUM=10000`) | **AUTHENTICITY_CONFLICT** — resolved in evidence, not yet changed |
| Resisting Spells | **inactive** | fully enabled | **AUTHENTICITY_CONFLICT** — resolved in evidence, not yet changed |
| Active skill count | **38** | 58 server-side / 49 in client `skills.mul` | **AUTHENTICITY_CONFLICT** |
| Runebooks | full custom system | item exists, `TYPE=t_normal //fixme`, no `IT_RUNEBOOK` in Source-X at all | **MISSING SYSTEM** — see `REVOLUTION_RUNBOOK_SPEC.md` |
| Anti-macro | production verification every 2–3h | none anywhere | **MISSING SYSTEM** — see `REVOLUTION_ANTIMACRO_SPEC.md` |
| Poisoned weapon ↔ Magery 40 gate | enforced | not verified present | **UNVERIFIED** |
| Mage/Special robe Eval gates (75.0 / 98.1) | enforced | not verified present | **UNVERIFIED** |
| NPC Teaching | ~30.0 for ~300gp | **matches exactly** (30% of trainer's skill, 1gp per 0.1) | **MATCH — proven live** |
| **Reagent consumption** | spells consume reagents (a whole economy; Recall's reagent count was *reduced* on 14.05.2009) | **`ReagentsRequired=0`** — no spell consumes anything | **AUTHENTICITY_CONFLICT**, found M3.6 |
| **Runebooks** | 8 named pages, insertion, Inscription copying, Recall-scroll charges | **implemented M3.6** as a scripted generic-gump item; copying not yet built | **RESTORED, partially** |
| **Engine era** | an era Sphere (~0.56b, 2007–2011) | Source-X, the modern maintained fork | **OPEN AXIS** — combat, spell and skill-gain formulas are engine-level and cannot be configured away. Raised M3.6, not investigated. |
| Recall/Mark reagents | 1 each / Mark 20 mana | matches (`[SPELL 32]`, `[SPELL 45]`) | **MATCH** |

**None of these has been changed yet.** M3.5 documents and bounds them; the
decision to alter a live ruleset is a separate, deliberate act — see §"Stop
conditions" in the M3.5 report.

---

## 6. Machine-readable form

Intentionally partial: nothing uncertain is encoded.

```yaml
profile: revolution_2009_2010
anchor:
  client_build: 2010-10-24        # client.dll
  window: [2009-01-01, 2011-02-17]

skills:
  total_cap: 700.0                # OFFICIAL + FORUM; runtime currently 1000.0
  per_skill_cap: 100.0
  lower_floor: 670.0              # .skilldusur
  inactive:
    - herding
    - remove_trap
    - resisting_spells
    - enticement
    - peacemaking
    - provocation
    - spirit_speak
    - forensic_evaluation
    - taste_identification

stats:
  total_cap: UNKNOWN              # deliberately absent

training:
  magery_bands:
    - {min: 0.0,  max: 30.0, action: night_sight}
    - {min: 30.0, max: 40.0, action: bless}
    - {min: 40.0, max: 60.0, action: greater_heal}
    - {min: 60.0, max: 70.0, action: magic_reflection}
    - {min: 70.0, max: 80.0, action: [reveal, invisibility, energy_bolt]}
    - {min: 80.0, max: 90.0, action: [energy_field, mass_dispel]}
    - {min: 90.0, max: 100.0, action: [earth_elemental, earthquake]}
  npc_teaching:
    max_percent_of_trainer: 30
    absolute_max: 42.0
    gold_per_tenth: 1
    keeps_change: true            # LIVE_VERIFIED m35_teach1

gates:
  mage_robe_eval: 75.0
  special_robe_eval: 98.1
  healing_cure_poison: 60.0
  healing_resurrect: 80.0
  fishing_net: 80.0
  poisoner_max_magery: 40.0

travel:
  runebook: {pages: 8, per_page_names: true, copy_via_inscription: true,
             charge_with_recall_scrolls: true, charged_needs_magery: false,
             guild_runebook: false,   # guild books are 2012, excluded
             implemented: partial}    # M3.6; copying not built

poison:
  # Four separate questions, one restriction. See GAMEPLAY_TRUTH 11.1.
  train_poisoning:        unrestricted
  cast_poison_spell:      unrestricted   # Poisoning boosts it
  apply_poison_to_weapon: unrestricted   # the Poisoning skill suffices
  wield_poisoned_weapon:  {max_magery: 40.0}

reagents:
  required_historically: true
  required_in_runtime:   false           # ReagentsRequired=0 -- conflict
  recall:  {reagents: 1, skill: 40.0, mana: 11}
  gate:    {reagents: 6, skill: 63.0}

economy:
  player_vendors: true
  vendor_cooperative: true
  trade_tax_percent: 10
  npc_commands: [".al", ".sat"]

antimacro:
  present: true
  trigger: continuous_production
  interval_hours: [2, 3]          # dated 2011-02-22; see spec
```

---

## 7. Rule for future work

Any numeric gameplay rule added to this project must state its profile. A rule
with no era is a bug, not a default.
