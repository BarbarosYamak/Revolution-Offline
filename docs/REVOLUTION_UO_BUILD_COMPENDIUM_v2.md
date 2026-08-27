# RevolutionUO Build Compendium — v2

Date: 2026-08-26 (M3.5, amended M3.6). Profile: `revolution_2009_2010`.

> **M3.6 amendment:** legality rule **L4 was wrong as first published** and is
> corrected in §3.1. Nothing else in this document changed. The eleven verified
> builds in §2 were never affected — they were the evidence that exposed it.
Supersedes `REVOLUTION_UO_BUILD_COMPENDIUM_v1.md`, which is **kept unchanged**
so the earlier reasoning stays auditable.

v1 catalogued 52 build entries across 15 families from a first research pass.
v2 does three things v1 could not:

1. **Verifies the budget.** v1 used "7x" as a cultural label. v2 has eleven
   independent forum builds whose skill values were read individually and
   summed — and every one totals **exactly 700**.
2. **Adds hard legality rules** from the official guide, so a generated build
   can be *checked* rather than merely composed.
3. **Cross-checks against the runtime**, so the gap between what Revolution
   allowed and what our shard currently allows is explicit per-build.

---

## 1. What v2 changes

| v1 said | v2 says | Why |
|---|---|---|
| "7x language is strong evidence; the exact numeric cap should be measured" | **Total skill cap is 700.0** | 11 builds, two eras, all exactly 700; plus the official `.skilldusur` command documenting a 670.0 skill-total floor |
| "7x builds" implies seven skills | **"7x" means 700 POINTS**, spent over 7, 8 or 9 skills | One 2008 build spends its 700 across nine skills; eight is the commonest warlock shape |
| Resist "should not be assumed active" | **Resist is officially inactive** — verbatim from the guide | `OFFICIAL_REVOLUTION_GUIDE` names it in a list of nine untrainable skills |
| — | **A character above Magery 40.0 may not WIELD a poisoned weapon** (everything else about poison is unrestricted) | `OFFICIAL_REVOLUTION_GUIDE`. Corrected in M3.6 — see §3.1; the first wording of this row was wrong |
| — | **Mage Robe needs Eval 75.0; Special Robes need Eval 98.1** | `OFFICIAL_REVOLUTION_GUIDE`. Eval is not a damage stat only — it is an equipment gate |
| — | **Healing 60.0 cures poison, 80.0 resurrects** | ditto |
| — | **Fishing 80.0 for nets** | ditto — the Fisher progression has a real threshold |

---

## 2. Verified historical-exact builds

Every row below was read value-by-value from a Revolution forum post and summed
independently. **Classification: `HISTORICAL_EXACT`.** All totals = 700.

### 2.1 Dual-combat warlocks — topic 43700, 15 June 2008

Source: `https://www.revolutionuo.net/forum/index.php?topic=43700.0`

| id | Poster | Allocation | Σ |
|---|---|---|---|
| `HX-01` | #Katherina-BaradDur# | ANAT 100 · HEAL 80 · EVAL 85 · MAGERY 85 · MEDI 50 · SW 100 · MACE 100 · TACT 100 | **700** |
| `HX-02` | Inola_de_GraCe | SW 100 · MACE 100 · TACT 100 · POI 90 · HEAL 90 · ANAT 90 · MAGERY 80 · MEDI 50 | **700** |
| `HX-03` | Larouse DOOM | SW 90 · MACE 100 · TACT 100 · ANAT 100 · HEAL 80 · MAGERY 85 · (EVAL 80) · MEDI 65 | **700** |
| `HX-04` | Leusyanasi | SW 100 · TACT 100 · MACE 100 · ANAT 100 · POI 100 · HEAL 80 · MAGERY 80 · MEDI 40 | **700** |
| `HX-05` | - dRead - # mhm[T] | MAGERY 100 · MACE 100 · SW 100 · TACT 100 · HEAL 80 · ANAT 80 · EVAL 75 · MEDI 50 · POI 15 | **700** |

`HX-03` is `HISTORICAL_NEAR_EXACT`: the post lists "heal" twice; the second is
almost certainly Eval Int, since 80 there is what makes the line total 700.

`HX-05` is the important one for design: **nine skills**. It is the direct
disproof of "7x = seven skills".

### 2.2 Warlocks — topic 77029, 6–7 July 2010

Source: `https://www.revolutionuo.net/forum/index.php?topic=77029.0`

| id | Poster | Allocation | Σ |
|---|---|---|---|
| `HX-06` | drumatic — *"Tam 7x"* | MAGERY 85 · FENC 100 · TACT 100 · EVAL 75 · POI 100 · HEAL 80 · MEDI 60 · ANAT 100 | **700** |
| `HX-07` | Tyalieva Biohazard | MAGERY 100 · EVAL 75 · MEDI 100 · POI 45 · HEAL 80 · ANAT 100 · *weapon* 100 · TACT 100 | **700** |
| `HX-08` | strahtmore | MAGERY 85 · EVAL 75 · MEDI 100 · POI 60 · HEAL 80 · ANAT 100 · *weapon* 100 · TACT 100 | **700** |

Two years after §2.1 the budget is identical. That is what makes 700 a
**profile-stable** rule rather than a snapshot.

### 2.3 Warlocks — topic 23720

Source: `https://www.revolutionuo.net/forum/index.php/topic,23720.0.html`

| id | Poster | Allocation | Σ |
|---|---|---|---|
| `HX-09` | Schoulzen | MAGERY 100 · SW 100 · TACT 100 · EVAL 80 · POI 80 · HEAL 80 · ANAT 80 · MEDI 80 | **700** |
| `HX-10` | Sahin | SW 100 · TACT 100 · MAGERY 100 · ANAT 100 · MEDI 60 · POI 80 · EVAL 80 · HEAL 80 | **700** |
| `HX-11` | 2-Pac | ANAT 80 · EVAL 80 · HEAL 80 · *weapon* 100 · MAGERY 100 · MEDI 60 · POI 100 · TACT 100 | **700** |

### 2.4 What the eleven have in common

The warlock skeleton, stated as data rather than lore:

| Skill | Appears in | Typical range |
|---|---|---|
| Tactics | 11 / 11 | always **100** |
| Anatomy | 11 / 11 | 80–100 |
| Healing | 11 / 11 | 80–90 |
| Magery | 11 / 11 | 80–100 |
| Meditation | 11 / 11 | 40–100 |
| a weapon skill | 11 / 11 | 90–100 |
| Eval Int | 8 / 11 | 75–85 |
| Poisoning | 7 / 11 | 15–100 |
| a **second** weapon skill | 5 / 11 | 90–100 |

Design consequences:

* **Tactics 100 is not a choice.** Eleven for eleven.
* **The tension is Meditation vs Poisoning.** Builds that take POI 100 pay for
  it out of MEDI (down to 40); builds that take MEDI 100 cut POI to 45–60.
  Players argued this openly — *"100 medi şart"* ("100 meditation is
  mandatory") against *"Meditation 100un altında olursa manan cok agır doluyor"*
  ("below 100 meditation your mana refills very slowly").
* **Eval Int clusters at 75–85, not 100** — and 75 is exactly the Mage Robe
  gate. Players were buying a robe, not damage.
* **Dual combat is a real, common shape** (5 of 11), not an exotic variant.

---

## 3. Build legality rules

A generated build is **invalid** unless all of these hold. These are checkable
and should be enforced by the build generator rather than trusted to templates.

| # | Rule | Source | Confidence |
|---|---|---|---|
| L1 | Σ skills ≤ **700.0** | 11 builds + `.skilldusur` floor 670.0 | HIGH |
| L2 | Every skill ≤ **100.0** | universal in the posts | HIGH |
| L3 | **No** Herding, Remove Trap, **Resisting Spells**, Enticement, Peacemaking, Provocation, Spirit Speak, Forensic Evaluation, Taste Identification | `OFFICIAL_REVOLUTION_GUIDE` | HIGH |
| L4 | If the build **wields a poisoned weapon**, **Magery ≤ 40.0**. Training Poisoning, casting the Poison spell and *applying* poison are all unrestricted. | `OFFICIAL_REVOLUTION_GUIDE` | HIGH — **corrected in M3.6, see §3.1** |
| L5 | Mage Robe user ⇒ Eval ≥ **75.0** | `OFFICIAL_REVOLUTION_GUIDE` | HIGH |
| L6 | Special Robe user ⇒ Eval ≥ **98.1** | `OFFICIAL_REVOLUTION_GUIDE` | HIGH |
| L7 | Cures poison by bandage ⇒ Healing ≥ **60.0**; resurrects ⇒ ≥ **80.0** | `OFFICIAL_REVOLUTION_GUIDE` | HIGH |
| L8 | Uses fishing nets ⇒ Fishing ≥ **80.0** | `OFFICIAL_REVOLUTION_GUIDE` | HIGH |
| L9 | Crafts Runebooks ⇒ Inscription high (stock recipe says 45.0; the guide says "high") | mixed | MEDIUM |
| L10 | Stat allocation | **SUPERSEDED — see note below.** Resolved in M3.8 as **225 total / 100 per stat**. | DERIVED |

### 3.1 L4, corrected — this document had it wrong

**v2 as first written said "a poisoner may not exceed Magery 40.0". That was
wrong**, and it would have outlawed the single best-attested build family on
the shard: every warlock in §2 carrying POI 100 alongside MAGERY 85–100 would
have been rejected as illegal. The error was caught in M3.6 by going back to
the exact wording rather than the paraphrase.

The official guide (`revolutionuo.net/oyun_rehberi`), verbatim:

> "Zehirleyebilme becerisidir. **Magery(büyücü) yeteneği ile yaptığınız Poison
> (zehir) büyüsünün gücünü arttırabilirsiniz.**"
> *"It is the ability to poison. With Magery you can increase the power of the
> Poison spell you cast."*

> "Warriorların silah sürmesi için bu skill yeterlidir. Poison şişesinin
> seviyesine göre silah sürülebilmektedir. **Büyücü yeteneği 40.0 ın üstündeki
> savaşçılar zehirli silahı KULLANAMAZLAR.**"
> *"This skill is sufficient for warriors to apply poison to weapons. Weapons
> can be poisoned according to the poison bottle's level. **Warriors with
> Magery above 40.0 CANNOT USE poisoned weapons.**"*

Four separate questions, four separate answers:

| Question | Answer | Why |
|---|---|---|
| May a high-Magery character **train Poisoning**? | **Yes** | no Magery term anywhere near it |
| May they **cast the Poison spell**? | **Yes** — and Poisoning *boosts* it | the guide says so outright |
| May they **apply poison to a weapon**? | **Yes** | "this skill is sufficient" |
| May they **wield a poisoned weapon**? | **No, above Magery 40.0** | `kullanamazlar` — "cannot use" |

So the rule is a restriction on **wielding**, and nothing else. The warlocks in
§2 are entirely legal: they poison with the **spell**, which their Poisoning
skill makes stronger. That is not a loophole — it is the design. Poisoning is
valuable to a mage *because* it powers the spell, which is exactly why seven of
the eleven verified builds carry it.

The practical consequence for a generator is narrower than v2 first claimed:
do not hand a Magery 85 warlock a **poisoned blade** — but a poisoned blade is
the only thing denied. Enforced as four separate predicates in
`include/uo/rules.h`: `CanTrainPoisoning`, `CanCastPoisonSpell`,
`CanApplyPoisonToWeapon`, `CanUsePoisonedWeapon`.

---

## 4. Cross-check against our current runtime

| Rule | Revolution | Our shard today | Effect on generation |
|---|---|---|---|
| Skill budget | 700 | **1000** | Generator must impose 700 itself. **Do not** design builds around ten GM skills because the runtime permits it. |
| Resist | inactive | **enabled** | Generator must exclude it (L3) whatever the server offers. |
| Active skills | 38 | 58 | Generator must draw from the 38-skill list, not the server's. |
| Per-skill cap | 100 | 100 | match |
| Stat cap | **225 total / 100 per stat** (DERIVED, M3.8) | 300 | runtime allows 300; the cap is enforced BOT-SIDE |

**This is the single most important line in the document:** the runtime is more
permissive than Revolution was, so authenticity here is the *generator's*
responsibility, not the server's. A build that the server would accept is not
therefore a Revolution build.

---

## 5. Classification counts

| Classification | v1 | v2 | Notes |
|---|---|---|---|
| `HISTORICAL_EXACT` | a handful, unverified sums | **10** | HX-01,02,04–11 — every value read and summed |
| `HISTORICAL_NEAR_EXACT` | — | **1** | HX-03, one ambiguous skill name |
| `HISTORICAL_FAMILY` | most of v1's 52 | **52 carried forward** | v1's catalogue stands; families are unchanged |
| `REVOLUTION_DERIVED` | several | unchanged | |
| `EXPERIMENTAL` | several | unchanged | |
| **Total catalogued** | 52 | **63** | 52 families/variants + 11 verified exact allocations |

---

## 6. Generator specification

```text
choose family          (from v1's 15 families)
choose variant         (from v1's 52 entries)
choose personality     (duelist / guild fighter / risk-averse crafter / ...)
allocate               respecting L1..L9
validate               reject and re-roll on any violation
assign training path   using the profile's Magery/Alchemy/Smithy bands
assign economy role    supplier / consumer / both
```

Two rules that keep a population from collapsing into one optimum:

* **Never generate every character from the best allocation.** The eleven
  historical builds disagree with each other; that disagreement *is* the data.
* **Bias the Meditation/Poisoning trade per character**, because that is the
  axis Revolution players actually argued about.

---

## 7. Research backlog for v3

* Stat allocations — still not one archive source. Highest-value open question.
* Pure Mage, Treasure Hunter, Tamer and crafter threads verified value-by-value
  the way §2 verifies warlocks (v1's families are sound; their *numbers* are
  not yet summed).
* Whether the 700 budget applied per-character or per-account.
* Whether `.skilldusur`'s 670 floor implies a 700 cap or a 670 one — the
  30-point gap is unexplained.
* Weapon-family balance per era (Katana vs Spear vs Black Staff).


---

## SUPERSEDED: stat allocation is no longer UNSPECIFIED

This document twice recorded the stat cap as unknown and told future readers not
to encode one. **That is out of date.** M3.8 resolved it as **225 total / 100 per
stat**, and `bot/uo-client/include/uo/rules.h` has encoded it since
(`totalStatCap = 225`, `perStatCap = 100`).

It was DERIVED, not quoted: ten player builds across two unrelated forum threads
and two different classes (a warlock and a thief), every one summing to exactly
225. Nobody ever states what everyone already knows, which is why three
milestones of looking for a *statement* found nothing while the evidence sat in
plain sight in the builds themselves.

The runtime still allows 300, so the cap is enforced **bot-side** rather than by
the server.

Two separate things that are easy to confuse:

* **The long-term cap** — 225 total / 100 per stat. This is what a finished
  build is planned against.
* **Character creation** — Source-X clamps each requested stat to 60 and the sum
  to 80 in `CChar::InitPlayer`. A creation split is a *request*, and it is not
  the same number as the cap.
