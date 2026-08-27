# M3.6 — Progression Closure, Navigation Recovery and Runebook Restoration

Date: 2026-08-26. A compact implementation milestone: close the correctness
gaps M3.5 left open, turn the historical rules into constraints the bot
actually obeys, and restore the Revolution Runebook.

Source-X modifications: **0**. Scripts-X (`server/Scripts-X`) modifications:
**0**. Runtime script additions: **1** — the runebook restoration, documented
below and committed on `revolution-runtime`.

---

## 1. Phase 0 — baseline and debt

| | |
|---|---|
| Client | `revolution-sphere-m1` @ `3ed9bb8`, clean |
| Runtime scripts | `revolution-runtime` @ `dc20378`, clean |
| Source-X | 0 tracked modifications |
| CTest entering M3.6 | 5/5, 966 checks |

### Debt carried in, and what M3.6 did with it

| Debt (from M3.5 §14) | Severity | Impact on M3.6 | Action |
|---|---|---|---|
| **Escape rung incomplete** — journey aborts while the escape walk is in flight | **BLOCKER** | Would corrupt every travel result and make the Runebook untestable | **FIXED** (§2) |
| Skill cap / Resist are documented conflicts, not fixes | Medium | Bot builds would be inauthentic | **ENFORCED CLIENT-SIDE** (§5) |
| Stat cap unknown | Low | — | Still unknown; deliberately not guessed |
| Runebooks specified, not built | High | The milestone's main subject | **BUILT** (§7) |
| Anti-macro specified, not built | Low | Not needed before M4 | Deferred, unchanged |
| Blank-rune purchase unproven | Low | — | Still unproven (§7.6) |
| Fleet/routing/economy not wired into a session | Medium | — | Still unwired; regression-tested only (§9) |
| `m3_magery_training` uses the wrong spell | **High** | Invalidates the headline progression number | **FIXED** (§3) |
| Only warlock builds verified value-by-value | Low | — | Unchanged |
| One action in flight; `Journey` 2-D | Low | — | Unchanged |

Two things found during Phase 0 that were **not** on the list:

* **`ReagentsRequired=0`** (`sphere.ini:1060`) — this runtime consumes no
  reagents for any spell. That withdraws a claim in the M3 report and opens a
  new authenticity conflict (§10).
* **Post-era mounts crash observing clients.** 52 of the 62 mounts in
  `i_memories.scp` have no AnimID in Revolution's tiledata; a unicorn mount
  (`0x3EB4`, still a ship "prow") crashed ClassicUO on every launch. Fixed for
  the admin character, and the class of problem is now impossible in our own
  viewer (§9.3).

---

## 2. Navigation recovery — FIXED, live-proven

### The bug

M2.5 closed "sealed into an upper storey" as a `BLOCKS_M3` blocker. M3.5 found
the fix did not cover its own primary case. M3.6 found a second half nobody had
seen.

There are two ways a journey fails, and the escape rung only handled one:

| | Where | Reason |
|---|---|---|
| plan time | `Journey.cpp:121` | `Failure::NoRoute` — cannot build a route from here |
| walk time | `Journey.cpp:200` | `Failure::Unreachable` — a route was built and ran out |

A sealed-in character fails the **first** way. Worse, even after M3.5 made the
rung fire on `NoRoute`, recovery still could not work, because M2.5's recovery
**restarted the journey** with `Begin()` — which wiped the trip, made the parent
report itself finished, and left the escape walk running with nobody waiting for
it. An orphaned recovery.

### The fix — an explicit lifecycle

A new `Phase::Recovering`, and the journey now **parks** instead of restarting:

```
active journey
  -> plan or route becomes unusable from HERE
  -> BeginPositionRecovery()   phase = Recovering, journey stays ACTIVE
  -> NextCommand() returns Wait  (the CLIENT owns movement; never two owners)
  -> escape walk runs, bounded to 3 anchors
  -> OnPositionRecovered(reached)
       reached -> phase = NeedRoute, replan for the ORIGINAL goal
       short   -> another anchor, or fail carrying the ORIGINAL reason
```

The journey keeps its label, goal, arrive radius and avoid-cell memory
throughout. `Active()` deliberately includes `Recovering` — a parent reporting
itself inactive while its recovery ran is precisely the bug.

One subtlety worth recording: recovery must be allowed to begin **from
`Phase::Failed`**, because a sealed-in character's plan has already failed by
the time anyone notices. The first version of this fix refused exactly there.

### Live proof — the Mage Tower, reproduced

`m36_recovery`, session 7 standing on the Mage Tower's upper storey at z=30 —
the exact position that produced the M3.5 failure.

```
[travel] Britain banker -> (1425,1690) r=5 from (1490,1555)
[travel] plan Britain banker: no world route to the destination legs=0 nodes=1
[travel] recovery walk reached its anchor at (1480,1558,30) (off 0); attempt 1/3
event travel_recovery_done: reached=1 at=(1480,1558,30) off=0 attempt=1
[travel] plan Britain banker: ok legs=4 ~152 tiles transit=0 nodes=21
[travel] avoiding 2 teleporter pad(s) on this leg
[travel] Britain banker ARRIVED at (1429,1690,0)
expect travel ok: ok
```

The original failure signature, then recovery, then **the same destination**
reached. **0 errors, 0 movement rejects, scenario finished.** The M2.5
teleporter guard is still active in the same run.

---

## 3. Magery training — corrected, and the result is not what was expected

### The correction

M3 trained Magery 50.0 → 50.9 by casting **Night Sight**, and derived
~0.53 skill/hour and a ~17-hour projection to 60 from it. Night Sight is the
**0–30** band. The RevolutionUO training guide (forum topic 59111) puts
**40–60 on Greater Heal**.

The M3 measurement is therefore reclassified, not deleted:

> **`LEGITIMATE_SKILL_GAIN_PROOF_BUT_WRONG_HISTORICAL_TRAINING_ACTION`**

It remains a valid proof that legitimate gain works. It is **not** an authentic
Revolution rate for that band, and the 17-hour figure derived from it is not an
authenticity estimate.

### The policy

`include/uo/training.h` holds the historical bands as data, with every spell
number read from `spells_magery.scp`. `ChooseAction` picks the first action in
the band the character can actually perform — known spell, affordable mana,
legal target, safe — and returns **nothing, with a reason**, when it cannot.
Nothing in a band is sprayed at random.

### Measured, live — `m36_magery`, RevolutionMage2

| | M3 Night Sight | **M3.6 Greater Heal** |
|---|---|---|
| Historical band for Magery 50 | wrong (0–30) | **correct (40–60)** |
| Elapsed | 105 min | **26.4 min** |
| Magery | 50.0 → 50.9 | 50.9 → **51.1** |
| **Gain events** (0.1 each) | **9** | **2** |
| Successful casts | 214 | **46** |
| Out-of-mana refusals | 246 | **74** |
| Meditation trances | 245 | **236** |
| Reagents consumed | **0** (§10 — the M3 figure was wrong) | 0 |
| **Gain per cast** | **0.0421** | **0.0435** |
| Casts per hour | 122 | 105 |
| Skill per hour | 0.51 | 0.45 |

### The result: no measurable difference — and that is the finding

**Using the historically correct spell did not change the gain rate.** Gain per
cast is 0.0421 against 0.0435 — indistinguishable. Casts per hour and skill per
hour are likewise within noise of each other.

This is not the result that was expected. The training guide's bands imply the
spell matters, and an early ten-minute reading during this milestone appeared to
show Greater Heal gaining several times faster per cast. **That was wrong**, and
it is worth saying plainly: at that point the sample contained *two* gain
events, and any rate computed from two events is noise. The figures above are
what a fuller sample says.

**The honest limits of this measurement.** Two gain events is still a very small
sample. It can exclude a *large* effect; it cannot exclude a modest one, and no
confidence interval worth printing can be built from it. **No projection to
Magery 60 is offered** — offering one from a thin sample is precisely the M3
mistake being corrected here.

### What it means, and what it does not

It does **not** mean the bands are wrong or that the guide was mistaken. It
means that *on this runtime*, Sphere's Magery gain does not visibly scale with
the difficulty of the spell cast. Two candidate explanations, neither tested:

1. Source-X's gain roll for Magery keys off the skill's own `ADV_RATE` curve
   rather than the spell's circle, so any castable spell trains the same.
2. Revolution's era Sphere behaved differently, and the bands are advice that
   was true *there*.

The second lands squarely on the **engine-era divergence** axis raised in §14
item 8 — combat, spell and skill-gain formulas are engine-level and cannot be
configured away. This measurement is the first concrete evidence that the axis
has real consequences, and it is the reason it is now tracked as conflict
register #15 rather than as a footnote.

**What is solid regardless:** the bot now trains with the historically correct
action, which is the authenticity requirement; and the mana arithmetic is real.
Greater Heal costs 11 mana against Night Sight's 4, and `RevolutionMage2` has
INT 20 — a 20-mana pool. It affords one cast and then waits, which is why 74 of
its attempts were refused for mana. That is exactly the constraint the
historical build record argues about — *"100 medi şart"* ("100 meditation is
mandatory") — and it is why every one of the eleven verified warlocks carries
Meditation. A properly built mage is a different measurement, not yet taken.

---

## 4. NPC Teaching as a progression primitive

M3.5 proved teaching live. M3.6 makes it something a bot decides and performs.

**Client actions** (both name-addressed, the M3 lesson):
`Client::ActionNpcTrain(npc, skillKey)` speaks `"<Name> train <skill>"` and
classifies all six trainer replies — including *"You already know as much as I
can teach"*, which is an answer rather than an error. `Client::ActionNpcGive`
hands over a counted stack. Scenario ops `npc_train` and `give`.

**Decision layer:** `rules::DecideTraining` weighs current skill, the build's
target, the trainer's own skill, gold minus an untouchable reserve, whether the
skill is Revolution-active at all, and the remaining 700-point headroom. It
returns the exact quote and a verdict — `Pay`, `NothingToTeach`,
`CannotAfford`, `SkillInactive`, `ExceedsBuildBudget`.

Two rules it enforces that a naive implementation would miss:

* **Never buy past what the build wants.** A build targeting Mining 20.0 buys
  to 20.0, not to the trainer's 30.0 ceiling.
* **Pay the quote exactly.** The trainer keeps the change — measured in M3.5,
  250 gold handed over against a 191 quote, 250 taken.

---

## 5. Revolution rules profile, enforced

`include/uo/rules.h` turns the profile into checks:

| Rule | Value |
|---|---|
| Total skill cap | **700.0** — checked as a **sum**, never as a skill count |
| Per-skill cap | 100.0 |
| Skill-lowering floor | 670.0 (recorded; the 30-point gap is still unexplained) |
| Inactive skills | the nine the official guide names, **Resisting Spells** among them |
| Teaching | 30% of the trainer's skill, capped 42.0, 1gp per 0.1, keeps the change |
| Stat cap | **absent by design** |

`ValidateBuild` accepts 700 across seven, eight or **nine** skills and rejects
700.1 — because "7x" is 700 points, and a documented 2008 build spends its 700
across nine.

**The load-bearing point:** the runtime allows 1000.0 and enables Resist. A
build the server accepts is therefore **not** a Revolution build, so
authenticity lives here, in the generator's constraints, not in Source-X.
Changing `SKILLSUM` server-side remains separate, deliberate, and undone.

---

## 6. The poison rule — resolved, and M3.5 had it wrong

M3.5's Build Compendium v2 published: *"a poisoner may not exceed Magery
40.0"*. That was a paraphrase, and it was wrong. It would have declared every
verified Magery-85–100 **with** Poisoning-45–100 warlock illegal — which is
the best-attested build family on the shard.

The official guide, verbatim:

> "Zehirleyebilme becerisidir. **Magery yeteneği ile yaptığınız Poison
> büyüsünün gücünü arttırabilirsiniz.**"
> *"…With Magery you can increase the power of the Poison spell you cast."*

> "Warriorların silah sürmesi için bu skill yeterlidir… **Büyücü yeteneği
> 40.0 ın üstündeki savaşçılar zehirli silahı KULLANAMAZLAR.**"
> *"This skill is sufficient for warriors to apply poison to weapons… Warriors
> with Magery above 40.0 CANNOT USE poisoned weapons."*

| Question | Answer |
|---|---|
| `CanTrainPoisoning` | **yes**, at any Magery |
| `CanCastPoisonSpell` | **yes** — Poisoning *increases* its power |
| `CanApplyPoisonToWeapon` | **yes** — the Poisoning skill suffices |
| `CanUsePoisonedWeapon` | **no above Magery 40.0** |

The restriction is on **wielding**, and only that. Four separate predicates, so
the distinction cannot collapse back into one rule. Corrected in
`REVOLUTION_UO_BUILD_COMPENDIUM_v2.md` §3.1, `REVOLUTION_RULESET_PROFILE.md`
§2.3 and `REVOLUTION_GAMEPLAY_TRUTH.md` §11.1, with the amendment flagged at the
top of the compendium rather than quietly edited in.

---

## 7. The Runebook — implemented

`runtime/scripts/revolution/revolution_runebook.scp`, loaded through a new
`revolution/` entry in `spheretables.scp` that sorts after the stock content so
it overrides `[ITEMDEF 022c5]`. **No Source-X change; no Scripts-X change.**

Full detail, including every reconstructed value, is in
`REVOLUTION_RUNBOOK_SPEC.md` §9.

### 7.1 Proven live

| Feature | Evidence |
|---|---|
| Opens as a generic gump, 8 pages | `0xB0`, 18 options: 11–18 travel, 21–28 insert, 40 charge |
| Insert a rune into a chosen page | *"Which rune do you wish to add to page 1?"* → *"Page 01 now holds Britain."* |
| Page stores name and destination | `TAG.p01.name="Britain"`, `TAG.p01.pt="1490,1555,30"` in the world save |
| Survives a relog | page still populated in a later session |
| Charge from a Recall scroll | *"The runebook now holds 01 charge(s)."* |
| **Travel from a page** | *"Recall to Britain: target the rune."* → real cast **"Kal Ort Por"** → `player @(1490,1555,30)` — the page's exact tile |

### 7.2 The book never moves anybody

Travel runs `SRC.CAST=s_recall`, which is Source-X's `CV_CAST` →
`Cmd_Skill_Magery`. The Magery 40.0 requirement, the 11 mana, the fizzle roll
and every destination rule the server has all apply exactly as for a hand-cast
Recall. There is no scripted teleport anywhere in the file.

### 7.3 A live finding that changed the design

**Sphere runes wear out.** `MORE1` counts down per use, the server warns *"The
recall rune is starting to fade"*, and then the rune is **destroyed** —
confirmed against the world save, where a rune present before a runebook travel
was gone afterwards while the page's point survived.

A historical Revolution page was not disposable; permanence is most of why
players carried books. So **the page, not the rune, is the destination of
record**: when a page's rune is worn out, the book re-cuts one from the point it
stored. Proven live — a subsequent travel used a freshly-cut rune
(`0x40002B3F`) and still landed on `(1490,1555,30)`.

Marked **RECONSTRUCTED**: the archive does not describe rune wear inside a book.

### 7.4 Charges are stored scrolls, not a counter

13.05.2009 says a charged use needs no Magery. A bare counter cannot deliver
that — Source-X's Recall always checks its own `SKILLREQ`. The resolution that
invents nothing: **a charge is the scroll**. Adding a Recall scroll stores it in
the book; spending a charge surfaces that scroll and uses it, and a scroll cast
has never needed the caster's own skill. That is very likely why the historical
rule reads as it does.

Implemented and the charge path is taken; the Magery bypass follows by
construction but is **not yet separately proven live**.

### 7.5 Not built

Runebook **copying** and **page-to-page transfer** (both 13.05.2009), and
**guild runebooks** (07.01.2012, outside the profile and deliberately absent).

### 7.6 Crafting — status stated precisely

The stock recipe is unchanged and plausible: 8 blank scrolls + rune + Recall
scroll + Gate Travel scroll, `SKILLMAKE=Inscription 45.0`. The eight is
Revolution's own page count.

The book used in testing was **console-created**. So:

* **`RUNEBOOK_MECHANICS_PASS`** — yes.
* **`LEGITIMATE_RUNEBOOK_CRAFTING_PASS`** — **no.** Not attempted end-to-end:
  it needs Inscription 45 (teaching caps at 30) plus four purchased inputs, and
  the mage-shop purchase is the same gap M3.5 recorded (§7.6 there).

---

## 8. Runebook and semantic travel

`include/uo/travel_mode.h` chooses **how** to get somewhere, above the M2.5
planner, so `SubmitStep()` remains the only 0x02 sender:

```
semantic goal -> [travel mode choice] -> world route -> local A* -> SubmitStep()
```

Four modes — `Walk`, `Moongate`, `LooseRuneRecall`, `RunebookRecall` — each
evaluated against what the character actually owns and can do. `Rank()` returns
**every** mode with its reason for rejection, because a planner that only
reports what worked cannot be debugged.

Decisions it gets right, and why:

* **A runebook page beats a loose rune** at equal speed — the rune wears out,
  the page does not.
* **A charged book works at Magery 20**, where an uncharged one does not.
* **Six tiles away, it walks** — a spell is not worth it.
* **Dead, in combat, or out of mana** falls back to walking.
* **Walk is always usable**, so the planner cannot fail to answer.

---

## 9. Regressions

### 9.1 Movement variation — green

Unchanged from M3.5 and re-run: stable per-character styles, all five
preferences present across a population, bounded biases, reproducible under
fixed seeds, and two named bots still disagreeing about preferred ground.
Recovery movement goes through `ActionGoto` like every other walk, so the sole
movement emitter is intact.

### 9.2 Connection safety — green

Unchanged and re-run: budget 30 against the server's 50, the counter that does
not decay, no attempt emitted inside a believed ban, exponential backoff,
breaker, seeded jitter. **No live ban was provoked**, deliberately.

### 9.3 A new hazard, fixed at the source

52 of the 62 mounts defined in `i_memories.scp` have no AnimID in Revolution's
Renaissance-era `tiledata.mul`. A unicorn mount (`0x3EB4` — still a ship
"prow") crashed ClassicUO with `IndexOutOfRangeException` on every launch,
because it indexes the static table unguarded.

Two responses: the stuck character was dismounted, and **our own viewer**
(`uo_viewer`) now routes every client-data lookup through `uo::safegfx`, which
is total over any 32-bit input and falls back to a visible placeholder. It
renders from Revolution's own MULs with `verdata` applied. That removes the
dependency on ClassicUO entirely.

**Still open:** a bot that tames a nightmare or ridgeback would still be
rendered by anything else watching. Restricting rideable creatures to the ten
mounts this client data supports is the Revolution-fidelity fix, and is debt.

---

## 10. `ReagentsRequired=0` — a correction and a new conflict

`runtime/sphere.ini:1060` sets `ReagentsRequired=0`, with `ReagentLossFail=0`
and `ReagentLossAbort=0`. **No spell on this runtime consumes a reagent.**

**Correction to the M3 report.** M3 stated its Magery block "consumed roughly
214 spider silk and 214 sulfurous ash". It consumed none; the character owns no
reagents at all, which is how it cast 214 times. That figure was inferred from
the spell's `RESOURCES` line rather than measured, and is withdrawn. The M3
document is annotated rather than rewritten.

**New authenticity conflict.** Revolution's reagent economy was real — reagent
vendors, Reagent Crystals, and a dated update (14.05.2009) *reducing Recall's
reagent count from three to one*, a change that means nothing unless reagents
were consumed. Conflict register #13.

---

## 11. Mark / Recall status — kept separate

| | |
|---|---|
| `MARK_RECALL_MECHANICS` | **PASS** — unchanged from M3.5, and now also exercised through the Runebook |
| `RUNEBOOK_MECHANICS` | **PASS** (§7) |
| `LEGITIMATE_RUNEBOOK_CRAFTING` | **NOT PROVEN** (§7.6) |
| `LEGITIMATE_MARK_RECALL_PROGRESSION` | **STILL BLOCKED** |

`RevolutionMage2` is at Magery **51.1**. It did not reach 60, no shard vendor
sells a sixth-circle scroll, and **session 7 was not used to promote this
status** — every Runebook and Mark/Recall result above is explicitly a
mechanics result on a console-provisioned probe.

The probe was upgraded during M3.6 (Magery/Eval/Meditation/Inscription 100,
STR 100 / DEX 25 / INT 100) at the project owner's instruction, to make
non-progression mechanics testable without fighting resource limits. It remains
fenced and is never cited as progression.

---

## 12. Tests

| Suite | Checks | Covers |
|---|---|---|
| `sphere_regression` | — | packet layer |
| `m2_actions` | — | action layer |
| `m25_world` | — | world model, routing, journey |
| `m3_progression` | 110 | progression, economy, secure trade |
| `m35_authenticity` | 856 | fleet admission, route variation, economic knowledge |
| **`m36_progression`** | **131** | recovery lifecycle, training bands, rules, poison, teaching, travel modes |
| `viewer_safety` | — | crash-proof client-data lookups |

**7/7 suites, 0 failures.**

---

## 13. Live scenario results

| Scenario | Result | Evidence |
|---|---|---|
| **`m36_recovery`** | **PASS** | Mage Tower reproduced; recovery attempt 1/3; original destination reached; 0 errors, 0 rejects |
| **`m36_magery`** | **PASS (measured)** | Greater Heal, the correct band; 46 casts over 26.4 min; 50.9 → 51.1; gain/cast indistinguishable from Night Sight (§3) |
| `m36_runebook` | **PASS** | gump opens; rune inserted; *"Page 01 now holds Britain."* |
| `m36_runebook_charge` | **PASS** | *"The runebook now holds 01 charge(s)."* |
| **`m36_runebook_travel`** | **PASS** | real Recall from a page → `player @(1490,1555,30)`, twice, once with a re-cut rune |
| `m35_teach` (M3.5) | PASS | carried forward unchanged |

---

## 14. Debt

1. **Runebook copying and page transfer** are not built (both 13.05.2009).
2. **Legitimate runebook crafting** unproven (§7.6).
3. **The charged-use Magery bypass** follows by construction but is not
   separately proven live.
4. **Blank-rune purchase from a mage shop** still unproven — the Britain "mage"
   place holds a guildmaster who keeps no shop.
5. **`ReagentsRequired=0`** — conflict open, not restored (§10).
6. **Skill cap and Resist** remain server-side conflicts; only the client
   enforces the profile.
7. **Stat cap** still unknown. Not guessed.
8. **Engine-era divergence** — Source-X vs an era Sphere. Combat, spell and
   skill-gain formulas are engine-level and cannot be configured away. Raised
   here, not investigated. Conflict register #15.
9. **Post-era mounts** crash third-party clients; our viewer is safe, the data
   is not (§9.3).
10. **Fleet, routing and economy modules are still not wired into a session.**
11. **Anti-macro** specified, not built.
12. Carried from M3: one action in flight; `Journey` is 2-D.

---

## 15. What M3.6 changes about how to read earlier reports

Two published numbers were wrong and are corrected here rather than deleted:

* M3's **~0.53 skill/hour and the 17-hour projection** are a valid gain proof
  but the wrong training action — reclassified, not removed (§3). Note the
  correction is about *authenticity*, not about the number: re-measuring with
  the correct spell produced the same rate.
* An **intermediate M3.6 claim that Greater Heal gains ~5× more per cast** was
  published to the project owner mid-milestone and is **withdrawn**. It came
  from a ten-minute window containing two gain events. The fuller sample shows
  no measurable difference (§3).
* M3's **reagent consumption figure** was inferred, not measured, and is
  withdrawn (§10).
* M3.5's **poison legality rule** was a wrong paraphrase of a source we had
  quoted correctly elsewhere (§6).

The pattern in all three: a number derived from a script's declaration rather
than from an observation. Worth watching for.

---

## 17. Addendum — the observer client, resolved after the report

Raised by the project owner immediately after M3.6 was reported, and closed the
same session. None of it changes any M3.6 result; it is recorded here because
it corrects a standing project rule and removes a dependency.

### 17.1 The standing rule was over-broad

The project carried a blanket instruction never to execute the Revolution
client binaries. The question put was whether that had to apply even for purely
local use. It did not, and the rule can now be stated precisely.

Static analysis of `local/revolution-client`:

| File | What it actually is |
|---|---|
| `Revolution.exe` | **launcher/patcher.** Imports only Windows DLLs (`urlmon`, `gdiplus`, `COMDLG32`, `WS2_32`); phones `http://www.revolutionuo.net/updt/`; references `client.dll` |
| `LoaderDLL.dll` | **injector.** References `wcp.dll` |
| `WCP.dll` | **"Anticheat".** Can fetch and run `.../updt/wcp/Revolution1062update.exe` |
| **`client.dll`** | **the actual UO 2D client.** `IMAGE_FILE_EXECUTABLE_IMAGE` set, `IMAGE_FILE_DLL` **clear**, entry point `0xE0F9A`, no exports, imports `DDRAW`/`DSOUND`/`WSOCK32` — an **EXE renamed**. Sections are stock and unpacked |

**`client.dll` contains no reference to WCP, LoaderDLL or anticheat.** Its only
embedded URLs are the dead EA ones every 2001 UO client carries (`uo.com`,
`ultima-registration.com`, `207.71.15.69`) — inert menu links, not startup
traffic. It made zero TCP connections while watched.

So the anti-cheat lives entirely in the launcher chain, and running `client.dll`
directly skips it. **Revised rule: never run `Revolution.exe`, `LoaderDLL.dll`
or `WCP.dll`. `client.dll` is safe to run locally.**

Staged as `uoclient.exe` (a copy; the original is untouched) with a `login.cfg`
pointing at `127.0.0.1,2593`. `runtime/sphere.ini` has both `UseCrypt=1` and
`UseNoCrypt=1`, so the server accepts it either way.

### 17.2 Two things it needed, neither obvious

**The registry key is shard-rebranded.** The client refuses to start without a
registered install ("Ultima Online does not appear to be installed correctly").
The stock `Origin Worlds Online` path does nothing — read out of the binary, the
real key is:

```
SOFTWARE\Revolution UO Shards\Ultima Online\1.0
SOFTWARE\Revolution UO Shards\Ultima Online\1.0\HWProfile
```

values `ExePath` / `InstCDPath` / `Patch` / `Language`. A 32-bit process reads
`HKLM\SOFTWARE` through `WOW6432Node`, so both views are written.
`local/dev/setup_real_client.ps1` does it (elevated, once) and also adds an
outbound firewall block for the client — Windows does not filter loopback, so
`127.0.0.1:2593` still works. `undo_real_client.ps1` reverses everything.

**The password obfuscation is a Caesar +13 over printable ASCII.**

```
enc(c) = ((c - 32 + 13) mod 95) + 32
```

Derived by decoding the value the client had already written for itself, which
came back as exactly the known Admin password. `local/dev/set_uocfg.ps1` writes
credentials in the client's own format, byte-identical to a manual "save
password". It is obfuscation, not encryption: `uo.cfg` holds the password.

### 17.3 The resolution limit is real, and hard-coded

The play window cannot be enlarged. Established twice:

* **Static:** the string
  `"GraphicManager::setGameplayWindowPixelWidthAndHeight: Currently only
  supports 640x480 & 800x600."` is referenced **exactly once** in `.text`, at
  VA `0x44902D` — live code, not leftover stock text.
* **Runtime:** setting `GamePlayWindowSize=1280x720` produced that dialog on
  startup.

Revolution rebranded the registry key and bumped the client version to
**1.46.0.3**, but left the resolution check stock. `set_uocfg.ps1` now refuses
any value other than `640x480`/`800x600` rather than writing one that makes the
client unlaunchable.

### 17.4 What to actually use

| Client | Art | Window | Notes |
|---|---|---|---|
| **CrossUO 1.0.7** (`--crossuo`) | Revolution's own MULs + verdata | **any size** | The daily driver. `tools/CrossUO/`. Stores its password in **plaintext** |
| Real client (default) | native | **800×600 max** | Era-exact reference |
| `uo_viewer` (`--viewer`) | Revolution's own MULs | any size | Ours; crash-proof client-data layer |
| ClassicUO (`--classicuo`) | — | any size | Legacy. Indexes tiledata unguarded |

`tools/launch_admin.bat` and `tools/launch_observer.bat` were regenerated with
all four paths, per-account credential pre-fill, and `--res WxH` for CrossUO.

### 17.5 A live hazard this exposed

ClassicUO crashed on **every** launch with
`IndexOutOfRangeException` in `Item.ItemData → GetGraphicForAnimation →
Mobile.IsMounted`. Cause: the Admin character was permanently mounted on a
**unicorn**, and `0x3EB4` is still a ship **"prow"** in Revolution's
Renaissance-era `tiledata.mul` (`AnimID=0`). Dismounted; it recovered.

**52 of the 62 mounts** in `runtime/scripts/items/i_memories.scp` have no AnimID
in this client data. A bot that tames a nightmare or ridgeback would crash any
third-party client watching it. Our `uo_viewer` is immune by construction
(`uo::safegfx` bounds-checks every lookup), but the data is still wrong.
Carried as debt — the fidelity fix is to restrict rideable creatures to the ten
mounts this client data actually supports.

### 17.6 Repository note

`tools/`, `docs/` and `local/` sit at the project root, which is **not** a git
repository — only `bot/uo-client` and `runtime/scripts` are. The launchers, the
setup scripts and every document in `docs/` therefore have no version history.
`local/` is deliberately outside version control because it holds credentials;
`tools/` and `docs/` are not, and arguably should be tracked. Flagged rather
than changed unilaterally.
