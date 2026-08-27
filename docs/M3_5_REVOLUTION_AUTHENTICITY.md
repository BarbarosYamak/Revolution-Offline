# M3.5 — Revolution Authenticity, Historical Forensics and Ruleset Restoration

Date: 2026-08-26. A milestone about **truth** rather than features: what
RevolutionUO actually did, where our reconstruction differs, and which
differences are now bounded well enough to act on.

**The headline:** the RevolutionUO archive at `revolutionuo.net` is **still
online**. Every large open question from M3 — the 700-point culture, Resisting
Spells, NPC Teaching, runebooks, anti-macro — is answered from Revolution's own
guide and dated change log rather than from inference.

Companion documents produced or expanded by this milestone:

* `REVOLUTION_RULESET_PROFILE.md` — the target era, fixed and justified
* `REVOLUTION_RUNBOOK_SPEC.md` — the runebook, specified for implementation
* `REVOLUTION_ANTIMACRO_SPEC.md` — anti-macro, specified and deliberately adapted
* `REVOLUTION_GAMEPLAY_TRUTH.md` — §0 conflict register, §10 archive findings
* `REVOLUTION_UO_BUILD_COMPENDIUM_v2.md` — 11 verified builds + legality rules

Source-X modifications: **0**. Scripts-X modifications: **0**. Runtime
configuration modifications: **0**.

---

## 1. Baseline, preserved

Phase 0 ran before anything was touched.

| | |
|---|---|
| Client branch | `revolution-sphere-m1`, clean at `34257a2` |
| Runtime scripts | `revolution-runtime`, clean at `dc20378` |
| Source-X | 0 tracked modifications |
| CTest entering M3.5 | 4/4 suites, 110 checks |
| Live re-verification | `m25_service_bank` 0 errors / 2 rejects / finished · `m25_entity` 0 / 0 / finished |

The two rejects are the known Britain bank door bump, recovered by the M1.5
OpenDoor path — identical to the M2.5 baseline, so the starting point is
unchanged.

**No M3 evidence was altered.** `M3_PROGRESSION_ECONOMY.md` is untouched;
everything M3.5 learned is recorded here or in the companion documents.

---

## 2. Skill cap — RESOLVED

**Revolution's total skill cap was 700.0. Our runtime allows 1000.0.**

Three independent lines of evidence, none of them player memory:

**1. Eleven builds, three threads, two years, all exactly 700.** Every skill
value was read individually and summed. June 2008 and July 2010 agree, so this
is a stable rule and not a snapshot. One build spends its 700 across **nine**
skills — which disproves the natural reading of "7x" and establishes that the
constraint is the **sum**, not the skill count. Full table in
`REVOLUTION_UO_BUILD_COMPENDIUM_v2.md` §2.

**2. An official command documenting a skill total.** The player-command page
describes `.skilldusur` as lowering a skill *"until your skill total is
670.0"*. A 670 floor is coherent under a 700 cap and strange under 1000.

**3. Silence in the change log.** 1200+ dated entries from 2007–2016 contain
**no** entry altering a skill or stat cap — consistent with a value set once at
the start and never revisited.

Answering the brief's five hypotheses directly:

| | Hypothesis | Verdict |
|---|---|---|
| A | Revolution really had 700 total | **YES** |
| B | 1000 allowed but players chose 7x | **No** — 7x is not a convention players could break; 11/11 hit exactly 700, and the official skill-lowering floor sits 30 points under it |
| C | The cap changed by era | **No evidence** — 2008 and 2010 identical, no changelog entry |
| D | A custom system modified the cap | **Unknown mechanism**, but the *effect* is documented. How it was enforced is not. |
| E | The current Source-X value is wrong for Revolution | **YES — this is the finding** |

**Not changed.** Altering `SKILLSUM` is a live-ruleset change and belongs to a
deliberate restoration step, not to a research milestone. What matters
immediately is the design rule, and it is now unambiguous:

> The build generator imposes 700 itself. A build the current server would
> accept is **not** therefore a Revolution build.

Unresolved and deliberately not guessed: the 30-point gap between the 670 floor
and the 700 cap, and **the stat cap**, for which not one archive source exists.

---

## 3. Resisting Spells — RESOLVED

**Officially inactive.** The RevolutionUO gameplay guide lists the untrainable
skills by name, verbatim:

> "Ayrıca sunucuda geliştirilemeyen ve aktif olmayan skiller sırasıyla:
> Herding, Remove Trap, **Resisting Spells**, Enticement, Peacemaking,
> Provocation, Sprit Speak, Forensic Evaluation, Taste Identification."

Our runtime has it fully enabled, with Magery's own `ADV_RATE` curve.

M3 could only say "enabled here, memory says otherwise, open". M3.5 closes it:
the memory was right, and the conflict is real. The same source also fixes the
historical **active skill count at 38**, against 58 in our Source-X runtime and
49 in the client's `skills.mul`.

**Not changed**, for the same reason as §2. The binding rule is the generator's:
Resist and the other eight never appear in a Revolution build.

---

## 4. NPC Teaching — RESOLVED AND PROVEN LIVE

The one place where our reconstruction turned out to be **already correct**,
and where a player memory was confirmed to the digit.

Configuration (`runtime/sphere.ini`): `NPCTrainCost=1`, `NPCTrainPercent=30`,
`NPCTrainMax=420`, driving Source-X `NPC_OnTrainHear` / `NPC_OnTrainPay`.
So a trainer teaches to **30% of its own skill**, never above 42.0, at
**1 gold per 0.1 skill** — meaning 0 → 30.0 from a GM trainer costs exactly
**300 gold**.

Live proof (`m35_teach1`), `RevolutionFisher` and Georgetta the blacksmith:

| Step | Server's own number |
|---|---|
| Arms Lore before | **2.6** |
| Quote | *"For **191 gold** I will train you in all I know of ArmsLore"* |
| Handed over | 250 gold |
| Arms Lore after | **21.7** (trained sum 665.9 → 685.0) |
| Backpack gold after, from the world save | **852**, down from 1102 |

19.1 points bought for 191 gold is exactly 1gp per 0.1. The 21.7 ceiling
implies Georgetta's own Arms Lore is 72.3, inside the `{50.0 75.0}` her chardef
rolls — confirming the 30% rule from the other direction.

**New finding, not in the memory: the trainer keeps the change.** 250 handed
over against a 191 quote, 250 taken. A `TrainerDecision` must pay the quoted
amount exactly.

New client capability, both addressed by name (the M3 lesson): `ActionNpcTrain`
speaks `"<Name> train <skill>"` and classifies all six trainer replies;
`ActionNpcGive` hands over a counted stack. Scenario ops `npc_train` and `give`.

---

## 5. Runebooks — SPECIFIED, NOT IMPLEMENTED

> **M3.6 update:** now **implemented** as a scripted generic-gump item and
> proven live. See `M3_6_PROGRESSION_RUNebook.md` §7 and
> `REVOLUTION_RUNBOOK_SPEC.md` §9.

**The memory was right, and the archive is more detailed than the memory.**
Full spec, with dated verbatim quotations, in `REVOLUTION_RUNBOOK_SPEC.md`.

| Date | Established |
|---|---|
| 15.08.2007 | runebooks exist (a mid-air exploit is closed) |
| 06.04.2008 | destination rules: no water, no other player, no magic item |
| 12.05.2009 | **8 pages**, each separately nameable; rune insertion targets the open page |
| 13.05.2009 | **copying via the Inscription menu**; **charging with Recall scrolls**; a charged use **needs no Magery**; page-to-page transfer |
| 07.01.2012 | guild runebooks — **outside our profile** |

Two forensic conclusions that make this tractable:

**The stock ITEMDEF is the surviving half of the system.**
`[ITEMDEF 022c5] i_spellbook_runebook` carries `TYPE=t_normal //fixme: or
t_runebook` and a recipe of **8 blank scrolls** + 1 rune + 1 Recall scroll +
1 Gate Travel scroll. Eight is Revolution's own 2009 page count, and the two
scroll types are exactly what a runebook does and what charges it.

**It was a server-side gump, so no Source-X change is needed.** Neither
`client.dll`, `Revolution.exe`, `WCP.dll` nor `LoaderDLL.dll` contains the
string "runebook" — and Source-X has no `IT_RUNEBOOK` type at all. A 2.0.x
client cannot draw a runebook natively, so Revolution's named pages and "rune
ekle" button must have been a **generic gump** (`0xB0`/`0xB1`). Our bot client
already speaks that protocol; M2.5 built it for the moongate menu.

**Not implemented**, per the brief's instruction not to rush a spec into code.
`REVOLUTION_RUNBOOK_SPEC.md` §6 lists honestly what would still have to be
invented (charges per scroll, maximum charges, whether copying preserves names)
and §7 is a seven-step implementation plan for the next milestone.

---

## 6. Anti-macro — SPECIFIED, DELIBERATELY ADAPTED

Confirmed real and dated: a verification module was already being **bug-fixed
on 15.04.2008**; **22.02.2011** re-activated it with a code screen every
**2–3 hours** *on continuous production*; **2016** added a one-minute
disconnect. Our runtime has nothing.

Two findings that matter more than the existence:

* **The trigger was production**, verbatim *"sürekli üretim"* — crafting and
  gathering loops, not combat or travel.
* **Nothing says it altered skill-gain formulas.** It gated unattended
  *operation*, not learning.

**The adaptation decision, stated rather than smuggled in.** A CAPTCHA exists to
prove a human is present; this project's entire purpose is characters playing
with no human present. Implementing it literally would either require a human
forever or be auto-answered by the bots — and auto-answering would reproduce
the mechanism while destroying its meaning, which is worse than absence because
it would look like fidelity.

What must be preserved is the **pressure**: production came in sessions, supply
stayed scarce, and a player had to come back every few hours. The proposal is a
`ProductionSession` that simply **ends** on the same trigger, sending the
character to bank, restock, sell or rest. Same economic effect, no prompt, no
invented gain-rate penalty. Not implemented in M3.5; the reasoning and the
rejected alternatives are in `REVOLUTION_ANTIMACRO_SPEC.md` §5.

---

## 7. Mark / Recall — mechanics probed on the fenced character

Run as `RevolutionMageGM` (session 7), whose Magery 60 was set by console.
**Two statuses, kept separate on purpose:**

| | |
|---|---|
| `MARK_RECALL_MECHANICS` | **PASS** — §7.2 |
| `LEGITIMATE_MARK_RECALL_PROGRESSION` | **STILL BLOCKED** — unchanged by anything here |

The second is worth restating: `RevolutionMage2` is at Magery **50.9**, gaining
a measured 0.53/hour, no shard vendor sells a sixth-circle scroll, and nothing
in M3.5 moved that by a point.

### 7.1 What the shard requires

`[SPELL 45]` Mark — `SKILLREQ=MAGERY 60.0`, `MANAUSE=20`, `spellflag_targ_item`.
`[SPELL 32]` Recall — `SKILLREQ=MAGERY 40.0`, `MANAUSE=11`.
Both consume black pearl, blood moss and mandrake root.
`i_rune_marker` = `0x1F14`.

### 7.2 Result

Our action layer drives both spells correctly: the client casts, the server
arms a target cursor, the client targets the rune in the pack, and the server
speaks the right words of power — **"Kal Por Ylem"** for Mark, **"Kal Ort Por"**
for Recall.

**Mark: PROVEN, server-side.** After a run of attempts the rune recovered from
the world save reads:

```
[WORLDITEM i_rune_marker]
SERIAL=040001455
NAME=Britain
MOREP=1490,1555,30
```

`MOREP` is exactly the tile the character cast from, and the server **named the
rune after the region by itself**. Before the cast it read `MOREP=-1,-1`.

**A finding worth carrying into progression planning:** at *exactly* the skill
requirement, Mark fizzles most of the time — six fizzles among the early
attempts, each still costing 20 mana. With this shard's flat mana regeneration
(~19.3 s per point, measured in M3) a Magery-60 character needs roughly **six
and a half minutes of standing still per attempt**. Marking a rune at the gate
is an afternoon, not an action. A bot that budgets one Mark per rune will be
wrong.

**Recall: PROVEN, server-side.** The full chain, from the same run:

| time | event | evidence |
|---|---|---|
| — | rune blank | `MOREP=-1,-1` |
| 21:09–21:18 | Mark, repeatedly | `Kal Por Ylem`; six fizzles, then success |
| (save) | rune marked | `MOREP=1490,1555,30`, `NAME=Britain` |
| 21:19:04 | walked away **under its own power** | `goto_done: at=(1480,1558) arrived=1 off=0` |
| 21:23:42 | Recall cast | `RevolutionMageGM: Kal Ort Por` |
| **21:23:43** | **server moved the character** | `[0x20] player @(1490,1555,30)` |

The arrival tile is **exactly** the rune's `MOREP`, reported by the server's own
0x20. No teleport command, no coordinate injection: the character walked away
and the spell brought it back.

An unmarked rune answers Recall with *"The recall rune is blank."*, which is a
clean negative the client can classify.

**`MARK_RECALL_MECHANICS = PASS.`**

### 7.3 What is still unproven

Buying a blank rune from a mage shop. The Britain "mage" place holds Caedmon
the **mage guildmaster**, who keeps no shop at all — the M3 finding, hit again
— and the actual vendor was not in range. The rune used here was placed by
console alongside this character's spellbook and reagents, as part of the same
fencing. Recorded as a gap rather than glossed over.

---

## 8. A real defect found by Phase 0 — the M2.5 escape rung

> **M3.6 update:** the remaining half of this — the parent journey aborting
> while the escape walk was in flight — is now **fixed and live-proven** on the
> same Mage Tower case. See `M3_6_PROGRESSION_RUNebook.md` §2.

M2.5 classified "sealed into an upper storey" as a `BLOCKS_M3` blocker and
fixed it. **The fix did not cover the case it was written for.**

There are two ways a journey fails:

| | Where | Reason |
|---|---|---|
| plan time | `Journey.cpp:121` | `Failure::NoRoute` — the planner cannot build a route from where we stand |
| walk time | `Journey.cpp:200` | `Failure::Unreachable` — a route was built and ran out |

The escape ladder only fired on `Unreachable`, and `TravelPlanRoute` called
`TravelFinish(false, …)` directly, bypassing it entirely. A character sealed on
an upper storey fails the **first** way. M2.5's obstacle scenario happened to
produce the second, so the fix looked proven.

M3.5 hit it head-on: the GM mage stood on the Mage Tower's upper storey at
z=30 and every destination returned *"no world route to the destination …
nodes=1"* with no escape attempt at all — exactly the permanent-stuck state the
M2.5 blocker described.

**Partly fixed.** The escape now fires on `NoRoute` as well, at both sites, and
the log shows it working: `travel_escape: from=(1490,1555,30) to=(1480,1558)
attempt=1`. **Not fully fixed:** the journey still aborts while the escape walk
is in flight, because a re-plan runs before the walk has moved anywhere.
Carried as debt (§14) with the evidence, rather than reported as closed.

This is the value of the brief's Phase 0: a debt item marked "fixed, tested
live and committed" was none of those things for its primary case.

---

## 9. Fleet connection safety — IMPLEMENTED

The M4 blocker. M3 banned its own IP and then re-banned it on every retry.

### 9.1 The actual rule, read rather than guessed

```
runtime/sphere.ini
  MaxConnectRequestsPerIP=50   // "does not decay, it resets only after
                               //  <NetTTL> seconds elapsed since last
                               //  connection attempt"
  NetTTL=60*5                  // 300 s
runtime/scripts/core/serv_triggers.scp
  f_onserver_connectreq_ex     // RETURN 2 -> reject AND ban for ~300 s
```

Three consequences the implementation is shaped by:

1. **It is not a rate limiter and not a leaky bucket.** It is a cumulative
   counter that clears only after total silence. A token bucket would model it
   wrongly and walk into the ban.
2. **A rejected attempt is still an attempt**, so retrying resets the very
   clock you are waiting on. Retry-on-failure is the one strategy that can
   never recover — which is precisely what M3 did, re-banning at 17:43, 17:44,
   17:44 and 17:45.
3. **The unit is the IP, not the account**, so this must be a fleet decision.

### 9.2 What was built

`include/uo/fleet.h`, `src/fleet/Fleet.cpp`:

* `AdmissionController` — pure logic, injected clock and seed. Budget **30**
  against the server's 50, an explicit safety margin. Staggered spacing,
  exponential backoff, a circuit breaker, upward-only jitter, and a hard rule
  that **no attempt is emitted while a ban is believed to be in force**.
* `FleetLedger` — a line-oriented file so separate bot processes share one
  budget, since they share one IP.

`Refusal` reports *why*, so a caller can tell "wait your turn" from "you are
banned", and every refusal carries a positive `retryAfterMs` so a caller that
honours it cannot spin.

Tested with the shard's real numbers: the counter does not decay, a late
attempt destroys an almost-served wait, the ban is refused at **every** point
inside the window, and recovery is clean afterwards. Fixed seeds, reproducible.

---

## 10. Human-like movement variation — IMPLEMENTED

`include/uo/route_style.h`, `src/travel/RouteStyle.cpp`. Sits between the world
route and the tile A*, so `SubmitStep()` remains the only 0x02 sender:

```
semantic goal -> world route -> [route variation] -> local A* -> SubmitStep()
```

A character's `Style` is derived from its **name**, so habits are stable across
sessions and processes with no stored state and no coordination. From it:

* a bounded, stable per-tile bias — two bots prefer different sides of the same
  street without either being wrong;
* `PickApproach` — which of several equivalent approach tiles is *mine*;
* a decaying recent-path penalty, so a bot varies against **itself** between
  trips, not only against its neighbours;
* five preferences (shortest / road / uncrowded / safe / mixed), which actually
  change behaviour: a `Shortest` character takes the nearest approach tile and
  ignores its own history; the others do not.

No learning, no wandering, no unreachability: every tile offered is one the
caller already established is legal. Fixed seeds, so tests are reproducible.

Proof that different bots reach the same destination differently: over a
100-tile stretch, `RevolutionMage2` and `RevolutionFisher` disagree about the
preferred ground on more than 20 tiles, and across 200 generated characters all
five preferences occur.

---

## 11. Economic knowledge — IMPLEMENTED

`include/uo/economy.h`, `src/progression/Economy.cpp`. M3's carving discovery
becomes data a bot can reason with instead of a comment in a scenario.

Two rules the module exists to enforce:

**A price has a provenance.** `PriceBook::Best()` returns only prices observed
on **this** shard. Forum-era figures are stored but reachable only through
`HistoricalBaseline()` — named so it cannot be called by accident. A bot cannot
trade on a 2010 forum price by mistake.

**Profit is per-unit *and* per-stone.** M3's fisher left eleven catches on the
dock because value it cannot carry is not value.

`KnownTransformations()` records carving with its evidence
(`CClientTarg.cpp:1948-1951`, and the live `m3_cut1` result). Given the measured
prices, `EstimateTransformation` reproduces the live outcome exactly: 12 fish →
48 steaks, 96gp against 12gp, **+7 gold per fish**, and a positive margin per
stone. When a price is missing it says so rather than inventing one.

---

## 12. Tests

| Suite | Checks | Covers |
|---|---|---|
| `sphere_regression` | — | packet layer |
| `m2_actions` | — | action layer |
| `m25_world` | — | world model, routing, journey |
| `m3_progression` | 110 | progression, economy, secure trade |
| **`m35_authenticity`** | **856** | fleet admission, route variation, economic knowledge |

**5/5 suites, 966 checks, 0 failures.**

The connection tests deserve a note: they assert against the shard's real
configured numbers (50 / 300 s / 300 s), not invented ones, because a test that
models a different server proves nothing about this one.

---

## 13. Live scenario results

All against Source-X on `127.0.0.1:2593`, ordinary accounts except where the
fenced probe is named.

| Scenario | Result | Evidence |
|---|---|---|
| `m25_service_bank` (Phase 0) | **PASS** | 0 errors, 2 rejects (known door bump), finished |
| `m25_entity` (Phase 0) | **PASS** | 0 errors, 0 rejects, finished |
| `m25_service_bank` (final build) | **PASS** | 0 errors, 0 rejects, finished |
| `m25_entity` (final build) | **PASS** | 0 errors, 0 rejects, finished |
| `m25_war_peace` (final build) | **PASS** | 0 errors, 0 rejects, finished |
| `m25_escape` (final build) | **PASS** | 0 errors, 0 rejects, finished — no regression from the escape-rung change |
| **`m35_teach`** | **PASS** | Arms Lore 2.6 → **21.7** for a quoted 191 gold; backpack gold 1102 → **852** in the world save |
| `m35_mark_recall` (mr3) | finding | client drives both spells; Mark fizzled at exactly SKILLREQ; *"The recall rune is blank."* |
| `m35_mark_recall` (mr4–mr6) | finding | exposed the M2.5 escape-rung defect (§8); escape now fires at plan time |
| **`m35_mark_recall`** (mr7) | **PASS** | Mark: rune `MOREP=-1,-1` → **`MOREP=1490,1555,30`**, `NAME=Britain`, from the server's own save. Recall: walked to (1480,1558), cast, and the server's 0x20 reported **(1490,1555,30)** — the marked tile exactly |

---

## 14. Technical debt

1. **The M2.5 escape rung is still incomplete** (§8). It now fires at plan time,
   but the journey aborts while the escape walk is in flight. A character
   sealed in a building can still fail a trip it should recover from.
2. **Skill cap and Resisting Spells are documented conflicts, not fixes.** The
   runtime still allows 1000.0 and still enables Resist.
3. **Stat cap is unknown.** No archive source. Deliberately not guessed.
4. **Runebooks specified, not built.**
5. **Anti-macro specified, not built**, and the adaptation is a proposal.
6. **Buying a blank rune from a mage shop is still unproven** (§7.3).
7. **`FleetLedger` is not wired into `main.cpp`.** The controller and the
   ledger are tested; no session consults them yet.
8. **Route variation is not wired into the planner.** The policy layer is
   built and tested; `RoutePlanner` does not consult it yet.
9. **The economy module is not wired into a bot.** `PriceBook` is populated by
   nobody; vendor observations are still only logged.
10. **`m3_magery_training` uses the wrong spell for its band.** The historical
    guide puts Night Sight at 0–30 and **Greater Heal at 40–60**; our loop
    casts Night Sight at Magery 50. Authenticity gap, and possibly a
    throughput one.
11. **Only warlock builds are verified value-by-value.** v1's other families
    are sound as families; their numbers are not yet summed.
12. Carried from M3: one action in flight; `Journey` is 2-D.

---

## 15. Deferred, with reasons

| Item | Why it is safe to defer |
|---|---|
| Changing `SKILLSUM` to 700 | A live-ruleset change. The binding constraint is the generator's, and it can be enforced today without touching the server. |
| Disabling Resisting Spells | Same. Builds simply never include it. |
| Runebook implementation | Spec is complete; the work is a milestone, and several numbers would ship as `RECONSTRUCTED`. |
| Anti-macro implementation | The adaptation needs agreement before code; getting it wrong bakes a lie into the economy. |
| Stat cap | No evidence. Guessing would be worse than the gap. |
| Poisoner Magery-40 gate, robe Eval gates, fishing-net gate | Documented; verifying each is a live test, and none blocks M4 prep. |
| Wiring fleet/routing/economy into sessions | Each is an integration with its own live proof; M3.5's job was to make them exist and be correct. |
| Ore/wood/leather tables, Head Hunter payouts, treasure levels | Era-specific research, not blockers. |

---

## 16. What M3.5 changed about how this project should work

**The archive is a primary source and it is reachable.** Future milestones
should consult `revolutionuo.net` before inferring anything. Three questions
that had been open since M2.5 were answered in an afternoon.

**"The server allows it" is not "Revolution did it."** The runtime is *more
permissive* than Revolution was — 1000 points against 700, 58 skills against
38, Resist enabled against officially inactive. Authenticity is therefore the
**generator's** responsibility, not the server's, and that inverts the natural
assumption.

**A debt item marked fixed may not be.** §8 is a blocker that was closed on
the strength of a scenario that exercised the wrong path.
