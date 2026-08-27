# RevolutionUO Runebook — Historical Specification and Reconstruction Plan

Date: 2026-08-26 (M3.5). Profile: `revolution_2009_2010`
(see `REVOLUTION_RULESET_PROFILE.md`).

**Status at M3.5: SPECIFIED, NOT IMPLEMENTED.**
**Status at M3.6: IMPLEMENTED, with gaps — see §9.**

§1–§7 are the M3.5 specification, left as written so the reasoning that led to
the build stays auditable. §9 records what was actually built, what was proven
live, what changed during implementation, and every value that had to be
reconstructed.

---

## 1. The question M3 left open

M2.5 and M3 found an item called `i_spellbook_runebook` that does nothing, and
a project owner who clearly remembered Revolution runebooks. The instruction
was explicit: do not resolve this by deciding the memory is wrong.

**It was not wrong.** The official RevolutionUO update archive documents the
system in detail, with dates.

---

## 2. Historical evidence

All quotations are verbatim from `https://www.revolutionuo.net/guncellemeler`,
the official update archive, classified `OFFICIAL_REVOLUTION_UPDATE`.

| Date | Turkish | English | Meaning |
|---|---|---|---|
| **15.08.2007** | "Runebook kullanarak havada asılı kalma açığı kapatıldı." | "The exploit of hanging in mid-air using a Runebook was closed." | Runebooks already existed and were already being exploited by Aug 2007 |
| **06.04.2008** | "Runebook kullanarak deniz'in üzerine ışınlanmak ve kapı(gate) açmak engellendi." | "Teleporting onto the sea, and opening a gate there, using a Runebook was blocked." | destination validation: no water |
| **06.04.2008** | "Runebook kullanarak gideceğiniz veya kapı (gate) açacağınız noktalarda başka bir oyuncu yada büyü ile yaratılmış bir eşya olmamalı" | "At the point you travel to or open a gate to with a Runebook, there must not be another player or a magically created item." | destination validation: no player, no summoned/field item |
| **12.05.2009** | "Runebook güncellemeleri: Sayfa sayısı 8'e yükseltildi, her sayfaya ayrı ad verilebilmesi sağlandı" | "Runebook updates: page count raised to 8, each page can be given its own name." | **8 pages, per-page names** |
| **12.05.2009** | "rune ekle tuşu bulunduğu sayfa üzerine ekleme yapılacak şekilde düzenlendi" | "the 'add rune' button was changed so it adds onto the page it is on." | insertion is page-targeted |
| **13.05.2009** | "Şarj (recall scroll ekleyerek) ile runebook kullanırsanız magery yeteneğine ihtiyacınız olmayacaktır." | "If you use the runebook with charges (by adding recall scrolls) you will not need the magery skill." | **charges from Recall scrolls; charged use bypasses Magery** |
| **13.05.2009** | "Runebook kopyalama inscription menüsüne eklendi." | "Runebook copying was added to the inscription menu." | copying is an Inscription craft action |
| **13.05.2009** | "Kitabınızın aktarmak istediğiniz sayfasını açmalı ve kitabı kapamalısınız. Daha sonra hangi kitaba aktaracaksanız o kitabı ve dilediğiniz sayfasını açtıktan sonra bu sayfadaki 'rune ekle' kısmına tıklayıp bir önce sayfasını ayarladığınız kitabı seçmelisiniz." | "You must open the page of your book you want to transfer and close the book. Then, after opening the book you want to transfer into and the page you want, click the 'add rune' part on this page and select the book whose page you set earlier." | **page-to-page transfer**, using the source book as the target of "add rune" |
| **07.01.2012** | "Loncalara özel runebook eklendi, lonca taşlarından temin edilebilir." | "A guild-specific runebook was added, obtainable from guild stones." | **guild runebooks — 2012, OUTSIDE this profile** |

Supporting, `OFFICIAL_REVOLUTION_GUIDE` (`/oyun_rehberi`): Inscription "can
create Runebooks and copy filled ones to blanks at high levels."

Related travel costs in-profile (`OFFICIAL_REVOLUTION_UPDATE` 14.05.2009):
Recall reagents reduced to 1 each; Gate Travel 6 each.

---

## 3. Current runtime state

| Layer | Finding |
|---|---|
| **Source-X C++** | **No `IT_RUNEBOOK` and no `t_runebook` anywhere.** `grep -rn "RUNEBOOK\|t_runebook" src/` returns nothing. There is no server-side runebook type to configure. |
| **Scripts-X** | The item exists and is honest about it: `[ITEMDEF 022c5] DEFNAME=i_spellbook_runebook`, **`TYPE=t_normal //fixme: or t_runebook`**, `WEIGHT=1.0`, `SKILLMAKE=Inscription 45.0,i_pen_and_ink`, `ON=@Create ATTR=attr_magic|attr_newbie`. Double-clicking it does nothing. |
| **Its recipe** | `RESOURCES=8 i_scroll_blank, 1 i_rune_marker, 1 i_scroll_recall, 1 i_scroll_gate_travel` |
| **Revolution client** | **No runebook strings in any binary.** `client.dll`, `Revolution.exe`, `WCP.dll` and `LoaderDLL.dll` contain no occurrence of "runebook", "rune book" or "runbook". |

### 3.1 Two conclusions that shape everything below

**The recipe is corroborating evidence, not a coincidence.** It already asks
for exactly **8 blank scrolls**, which is the 12.05.2009 page count, plus a
rune, a Recall scroll and a Gate Travel scroll — the two travel spells a
runebook performs and the scroll type that charges it. Stock Sphere content
carrying Revolution's own page count is a strong hint that this ITEMDEF is the
surviving half of the system, with the behaviour removed.

**It was a server-side gump, so this is a scripts problem, not a protocol
problem.** A UO 2.0.x client has no native runebook UI, and this one has no
runebook strings at all — yet Revolution's book had named pages and buttons
("rune ekle"). The only way to draw that on this client is a **generic gump**,
`0xB0` out and `0xB1` back. That means:

* **no Source-X change is required**, and
* **our bot client already speaks the protocol** — M2.5 built 0xB0/0xB1
  handling for the public moongate menu and it is proven live.

---

## 4. Reconstructed behaviour model

Confidence per line. `HIGH` = an official dated entry says so.

### 4.1 The item

| Property | Value | Confidence |
|---|---|---|
| Graphic | `0x22C5` | HIGH (present) |
| Weight | 1.0 | MEDIUM (stock value, unchallenged) |
| Craft | Inscription; stock says 45.0 | MEDIUM — the guide says "at high levels", which does not obviously mean 45 |
| Ingredients | 8 blank scrolls + 1 rune + 1 Recall scroll + 1 Gate Travel scroll | MEDIUM-HIGH (present, and the 8 matches) |

### 4.2 State the server must hold per book

| Field | Notes | Confidence |
|---|---|---|
| `pages[8]` | fixed at 8 from 12.05.2009 | HIGH |
| `pages[i].point` | marked location; the same thing `i_rune_marker.MOREP` holds | HIGH |
| `pages[i].name` | per-page name, player-set | HIGH |
| `charges` | count of inserted Recall scrolls | HIGH |
| `defaultPage` | UNKNOWN — no source | LOW |
| owner / guild binding | guild books are 2012, excluded here | n/a |

### 4.3 Operations

| Operation | Behaviour | Confidence |
|---|---|---|
| Open | generic gump: 8 rows, each showing its name, plus per-row travel/gate buttons and an "add rune" control | HIGH that it existed; MEDIUM on exact layout |
| Add rune to page | targets **the page currently open** | HIGH |
| Name a page | player-supplied text per page | HIGH |
| Recall from page | consumes reagents/charge, moves the caster | HIGH |
| Gate from page | opens a gate at the page's point | HIGH |
| Charge | insert Recall scrolls; **a charged use requires no Magery** | HIGH |
| Copy book | via the **Inscription** craft menu, filled → blank | HIGH |
| Transfer a page | open source page, close book, open destination page, use "add rune" and select the source book | HIGH (the procedure is quoted verbatim) |

### 4.4 Destination validation (all HIGH, 06.04.2008)

A travel or gate is refused when the destination is:

1. water,
2. occupied by another player,
3. occupied by a magically created item (summon/field).

Plus the 15.08.2007 fix against ending up suspended in mid-air.

---

## 5. What a bot needs, and what it already has

| Capability | Status |
|---|---|
| Send/receive generic gump `0xB0`/`0xB1` | **already built and live-proven** (M2.5 moongate menu) |
| Double-click an item to open it | already built (`ActionUseObject`) |
| Target an item with a spell | already built — proven this milestone by the Mark/Recall probe |
| Read a rune's marked point | server-side; the client sees only the effect |
| Model "which destinations do I own?" | **`travel::PersonalKnowledge` already exists** and is the natural home |
| `TravelMode::Recall` in the planner | **not built** — see the M3.5 report, deferred with the runebook |

The client-side gap is therefore small. The server-side gap is the system.

---

## 6. What would have to be invented — the stop-condition test

The brief says to stop rather than invent undocumented mechanics. Honestly
listing what is *not* in the archive:

| Unknown | Why it matters |
|---|---|
| Gump layout, button ids, art ids | Cosmetic for a bot, but any reconstruction is a guess and must be labelled one |
| Charges per Recall scroll (1:1? a stack?) | Directly changes travel economics |
| Maximum charges | ditto |
| Whether copying preserved names and charges | Changes the Inscriber's product |
| Exact Inscription skill for crafting vs copying | The guide says "high"; stock says 45.0 |
| Whether a marked page could be overwritten in place | Affects UX and bot logic |
| Book weight when full, and whether charges add weight | Minor |
| Whether Recall from a book had a different failure/fizzle rule than the spell | Could matter in PvP |

**Verdict:** the *skeleton* (8 pages, names, insertion, copying, charging,
charge-bypasses-Magery, destination rules) is documented well enough to build.
The *numbers* around charges and crafting are not. A faithful implementation
would therefore ship with several values marked `RECONSTRUCTED` — which is
acceptable, but it is a real decision, not a detail.

---

## 7. Implementation plan (next milestone)

Deliberately staged so each step is provable on its own.

**Step 1 — item becomes real.** A Scripts-X `[ITEMDEF 022c5]` with an
`@DClick` that opens a generic gump. No travel yet. Prove: the bot double
clicks it and receives `0xB0`.

**Step 2 — page storage.** Eight `TAG.page<N>.point` / `.name` pairs. Prove:
set from script, read back after a world save.

**Step 3 — insertion.** "Add rune" targets a `t_rune` in the pack, copies its
`MOREP` into the open page, consumes the rune. Prove: rune's `MOREP` appears in
the book's tag; rune gone.

**Step 4 — travel.** Page button casts the existing `[SPELL 32]` / `[SPELL 51]`
at the stored point, honouring the 06.04.2008 destination rules. Prove: the
character's server-reported position equals the stored point — the same bar the
Mark/Recall probe cleared this milestone.

**Step 5 — charges.** Insert Recall scrolls; a charged use skips the Magery
requirement. Prove: a Magery-0 character travels using a charged book, and the
charge count falls.

**Step 6 — copying and page transfer,** via the Inscription menu and the quoted
transfer procedure.

**Step 7 — bot layer.** `TravelMode::Recall` in the route planner, gated on
actually owning a marked destination, having the skill or a charge, and having
reagents. Never as a shortcut when those are absent.

**Explicitly out of scope for `revolution_2009_2010`:** guild runebooks
(07.01.2012).

### Constraint carried forward

No Source-X modification is needed or permitted for any of this. If a step
appears to require one, that is the signal to stop and re-examine, because
Revolution ran this on a Sphere server too.

---

## 8. Bottom line

| | |
|---|---|
| Did Revolution have custom runebooks? | **Yes. Proven, with dates, from the official archive.** |
| Was the player memory right? | **Yes, and more detailed than expected** — 8 pages, naming, copying, charging and guild books all check out. |
| Where is the gap? | Entirely in our reconstruction's scripts. Source-X never had the type; the Revolution client never needed it. |
| Is it implementable? | **Yes**, as a scripted generic-gump item, with several numbers marked as reconstructed. |
| Was it implemented in M3.5? | **No.** Specified only, per the brief's instruction not to rush an evidence-backed spec into code. |

---

## 9. Implementation status — M3.6

**Implemented**, as `runtime/scripts/revolution/revolution_runebook.scp`, loaded
via a new `revolution/` entry in `spheretables.scp`.
**Source-X modifications: 0.** Scripts-X (`server/Scripts-X`) modifications: 0 —
the file is a runtime restoration that overrides `[ITEMDEF 022c5]` by loading
after the stock definitions.

### 9.1 What works, proven live

Probe: `RevolutionMageGM` (session 7, fenced — console-provisioned skills, so
this is a mechanics result and not a progression claim).

| Feature | Status | Evidence |
|---|---|---|
| Book opens as a generic gump | **PASS** | `0xB0` with 18 options; buttons 11–18 travel, 21–28 insert, 40 charge |
| **8 pages** | **PASS** | eight travel and eight insert buttons, `TDATA4` capacity |
| Per-page names | **PASS** | page takes the rune's name; the rune is auto-named by region on Mark |
| Insert a rune into a chosen page | **PASS** | *"Which rune do you wish to add to page 1?"* → *"Page 01 now holds Britain."* |
| Destination stored | **PASS** | `TAG.p01.pt="1490,1555,30"` in the world save |
| Survives a relog | **PASS** | the page was still populated in a later session |
| Charge from a Recall scroll | **PASS** | *"The runebook now holds 01 charge(s)."*, scroll moved into the book |
| **Travel from a page** | **PASS** | *"Recall to Britain: target the rune."* → real cast **"Kal Ort Por"** → `player @(1490,1555,30)`, the page's exact stored tile |

The book never moves anybody. Travel runs `SRC.CAST=s_recall`, which is
Source-X's `CV_CAST` → `Cmd_Skill_Magery`, so the Magery 40.0 requirement, the
11 mana, the fizzle roll and every destination rule apply exactly as they do for
a hand-cast Recall.

### 9.2 A live finding that changed the design

**Sphere runes wear out.** `MORE1` counts down per use; the server warns *"The
recall rune is starting to fade"*; then the rune is **destroyed** — confirmed
against the world save, where the rune was gone after a travel while the page's
point survived.

A historical Revolution page was not disposable — permanence is most of why
players carried books instead of pockets full of runes. So **the page, not the
rune, is the destination of record**: when a page's rune is missing or worn
out, the book re-cuts one from the point it stored. The destination still came
from a rune the player marked and inserted; only the stationery is replaced.

Marked as **RECONSTRUCTED** — the archive does not describe rune wear inside a
book — but the alternative (a page that dies after six uses) contradicts what
runebooks were *for*.

### 9.3 Charges: what the evidence supports, and what it does not

13.05.2009 says a charged use needs no Magery. A bare counter cannot deliver
that, because Source-X's Recall always checks its own `SKILLREQ`. The
resolution that needs no invention: **a charge is the scroll**. Adding a Recall
scroll stores it in the book; spending a charge surfaces that scroll and uses
it, and a scroll cast has never required the caster's own skill. That is most
likely why the historical rule reads as it does.

| Aspect | Status |
|---|---|
| Adding a scroll adds a charge | **implemented, proven** |
| The scroll is kept, not destroyed | **implemented** (reconstruction: the archive says neither) |
| A charged use casts from the scroll | **implemented** — `SRC.USE=<scroll>` |
| Charge count decrements | **implemented** |
| Magery bypass on a charged use | **implemented by construction** (scroll cast), **not yet proven live** |
| One scroll = one charge | **RECONSTRUCTED** — no source states a rate |
| Maximum charges | **RECONSTRUCTED** — none enforced; no source states one |

### 9.4 Not implemented

| Feature | Why |
|---|---|
| **Runebook copying** (Inscription menu, 13.05.2009) | Needs the crafting-menu integration; nothing about it is guessed here |
| **Page-to-page transfer** (13.05.2009) | The procedure is quoted in §2 but not built |
| **Legitimate crafting** | The recipe is unchanged and plausible (8 blank scrolls + rune + Recall scroll + Gate Travel scroll, Inscription 45.0). The book used in testing was **console-created**, so this is `RUNEBOOK_MECHANICS_PASS`, **not** `LEGITIMATE_RUNEBOOK_CRAFTING_PASS` |
| Destination legality (no water / no player / no magic item) | Inherited from the real Recall spell rather than re-implemented — the server already enforces what it enforces. Whether Source-X's rules match Revolution's 06.04.2008 rules is **unverified** |
| **Guild runebooks** | 07.01.2012 — outside the target profile, deliberately absent |

### 9.5 Reconstruction ledger

Everything invented, in one place, so it can be challenged:

1. Gump layout, art ids and button numbering.
2. The rune is **kept** on insertion rather than consumed.
3. A page re-cuts a worn rune from its stored point (§9.2).
4. One scroll = one charge; no maximum.
5. The book is a `t_container` with capacity 20 (8 pages plus charge scrolls).
