# Two fixes: the shout while retreating, and the caster who carries her book

Date 2026-09-06. HEAD at start ad499bf, ctest 45/45 before and after.
Build dir `build-m1` (serial). Live evidence from `run_gates/` gates of
5 minutes each on the running shard (port 2593).

---

## FIX 1 — D14 remainder: the guard call is a per-tick keeper

### What was wrong

`Runner::CallGuardsIfProtected` (`src/life/runner/Survive.cpp:135`) was
reached from exactly two decision points:

- the near-death flee arm, `Survive.cpp:404` (now :434 after the insert);
- `DecideHeal`'s rest arm, `Survive.cpp:930` (now :960).

Both are decided ONCE, on the tile the character stands on at that moment.
A retreat that begins OUTSIDE a guarded region therefore asks "can the
guards hear me?", is told no, and never asks again — including for the
whole stretch after it crosses the town line.

Live shape (Tordor, fleet_ramp_20260906/Tordor.console.txt:1218-1510):

- 03:12:17 `interrupt=FLEE_TO_GUARDS` — 40% HP, 3 hostiles, no protection here
- 03:12:57 client prints "You are now under the protection of the city guards"
- 03:13:09 dead at 1447,1528, inside `a_townBritain`, having never said the word

Sphere summons guards on the spoken keyword and on nothing else
(Source-X `CClientEvent.cpp:1871`, `guardcall GUARD,GUARDS` in
`runtime/scripts/core/defs.scp:16`) — shard-side behaviour is correct.

### The change

- `Runner::KeepCallingGuards` (`src/life/runner/Survive.cpp`, immediately
  after `CallGuardsIfProtected`): every tick, whatever the goal, while
  something hostile is in sight or on us — or while a survival retreat is in
  flight and this life is still hurt — call `CallGuardsIfProtected`. The
  existing 15 s throttle inside that function is untouched and is what stops
  a decision becoming a packet storm.
- Called from `Runner::Tick`, `Phase::Live`, `src/life/runner/Core.cpp:1331`,
  after `MaintainBuildLocks` and before any goal runs.
- A stale retreat flag with full health and nothing in sight does NOT shout
  (there is nothing to call the guards about).
- Disengage thresholds, `RetreatToSafety`, and both original call sites are
  unchanged.
- The `interrupt=GUARDS` log line now states hostiles-in-sight and
  attackers-on-me instead of claiming "a hostile on me", because the keeper
  legitimately fires for a hostile merely in sight.

### Test

`tests/life_world_harness.cpp` — inside the per-family loop, against the
real `Client` with the fixture atlas (guarded town RECT 20,20-80,80):

1. retreat in flight, 3 hostiles, 40% HP, standing at (200,200) in the
   wilderness → **0** `Guards!` packets;
2. server moves the character to (40,40), inside the guarded town → exactly
   **1** `Guards!`;
3. same tick again, then +5 s → still **1** (the throttle);
4. +60 s at full HP with nothing in sight → still **1** (the shouting stops).

Asserted on the sent 0x03 ascii-speech packet (`SentForTest`), not on a log
line, because the packet is the only thing Sphere's keyword parser sees.
`life_world_harness` 73 checks, 0 failures.

### Live

Smoke `rev.py gates CHARS=Tordor MINUTES=5` (17:16-17:22, run via the
roster100 launcher shape — Tordor is not in `roster30.tsv`, which is why
`rev.py gates` rejects him by name):

- no death (sphere2026-09-06.log's last `was killed by` is 03:53, from the
  earlier fleet ramp; nothing in the 17:16-17:22 window);
- `open_container` count 0 — no storm;
- clean `logout_complete`;
- he logged in already hurt (6/50) with NO hostile near, so the keeper
  correctly stayed silent. **The crossing case itself was not reproduced
  live in this window** — it is covered deterministically by the harness.

Where the keeper DID fire live: Aurelius, 17:39:55 and 17:41:37 in the
final gate — twice in five minutes, ~100 s apart, walking through Britain
with a hostile in sight. Two shouts, not a storm.

---

## FIX 2 — a caster carries her filled spellbook

### Root cause (verified in source and in a live log)

`src/life/Professions.cpp`, the pure-mage row:

    p.tools = {{"spellbook", {kSpellbook}, false}};    // line 450
    ... 11 lines of comment ...
    p.tools = {{"dagger", {0x0F51, 0x0F52}, false}};   // line 465  <- ASSIGNMENT

The second statement REPLACED the first, so a mage's declared tool list held
a dagger and no book. `DoBank`'s dead-weight pass keeps what the profession
declares and boxes the rest, which is how Aurelius' 23-spell book left her
pack:

    artifacts/mage_bank_followup_20260906/Aurelius.console.txt:81
    "banking dead weight: i_spellbook (0x0EFA hue 0x0000) x1 --
     this life has no use for it and the pack is at 78%"
    move_item serial=0x4000EDD5 dest=0x4000EE06 (bank box)

With no book in the pack, `DoFillSpellbook`'s "no book" arm then bought her
the empty replacement 0x40013046 she could not cast from
(`artifacts/caster_spellbook_audit_2026-09-06.md`: 1 of 39 casters broken).

The same test found two more Mage-strategy rows that never declared a book
at all: `alchemist` and `warlock`.

### Changes

1. `src/life/Professions.cpp` — mage declares spellbook AND dagger;
   `alchemist` declares mortar AND spellbook; `warlock` declares spellbook.
2. `src/life/runner/Bank.cpp`, dead-weight pass — a 0x0EFA in a
   Mage-strategy life's pack is never dead weight, whatever its profession
   row says. (A rule that only holds while one table row is right is not a
   rule.)
3. `src/life/runner/Bank.cpp`, bank-open branch — while the box is open, a
   Mage-strategy life whose carried book is short of a working one takes the
   banked book out (`FindContainerItemByGraphic(box, i_spellbook)` →
   `IssueBankItemMove(... client.BackpackSerial())`, the same withdraw
   primitive the arrow and reagent passes use; no new bot-core primitive was
   needed).
4. `src/life/Needs.cpp` — new `NeedBank` at urgency 1.0, "the spellbook is
   in the bank", beside the caster's reagent-stock need: a book in the
   remembered bank plus a carried book short of `kSpellbookComfortable`
   means walk to the bank, not buy the book again one scroll at a time.
5. `src/life/runner/Core.cpp` `Observe` — the pack's BEST spellbook, not the
   first. Ranking: a book the client has read rows out of scores its row
   count; a book nobody has opened yet scores `kSpellbookComfortableRuntime
   - 0.5` (opening is one free action, so an unknown book outranks every
   unfinished known one and loses to a working one); a book that was asked
   and still says nothing scores -0.5 so it cannot shadow a real book.
6. `src/life/runner/Core.cpp` `Observe` — the remembered spell list now
   stands in only for an UNOPENED book. An opened-and-empty book reads as
   empty, which is what makes the defect observable at all.
7. `src/life/runner/Train.cpp` — `spellbookOpened_` is now qualified by
   `spellbookOpenedSerial_` (new member in `src/life/Runner.h`), so a second
   book does not inherit the first one's "already looked". Both the
   FILL_SPELLBOOK and PRACTICE_SKILL open sites.

### Test

`tests/m5_professions.cpp` — `TestEveryCasterDeclaresItsSpellbook()`: every
Mage-strategy profession must list graphic 0x0EFA among its tools. It failed
on `alchemist` and `warlock` when first written, which is how those two rows
were found; all pass after the data fix.

### Live (three gates, each 5 minutes, `rev.py gates CHARS=Aurelius`)

| run | what happened |
|---|---|
| 17:16 (fix 1 only + first cut) | `spells 0/24 book=carried` — the empty book now reads honestly (it used to report the remembered 23); still shopped for scrolls; never went to the bank |
| 17:32 (after the NeedBank) | `goal=BANK` first, 17:34:07 "there is a book in the box and the one in the pack holds 2 of 24 spells -- taking it out", `move_item 0x4000EDD5 dest=0x4000EE02` success — but she kept using the 2-row book, because a KNOWN 2 outranked an unopened 0.5 |
| 17:39 (after the ranking fix) | opens 0x40013046 → 5 rows, next tick opens 0x4000EDD5 → **23 rows**; `spells 23/24 book=carried`; TRAIN_COMBAT runs; **6 `cast_spell` actions** (Heal id 4 success, Harm id 12 at a target); **0** "no castable attack spell" handoffs (was every hunt attempt); 0 `goal_spinning` |

### Known limitations / not fixed here

- `DoFillSpellbook` treats 23 spells as short of a working book
  (`kSpellbookComfortableRuntime = 24`), so Aurelius still opened a scroll
  errand with the full standard book in hand. Pre-existing policy constant,
  fleet-wide; not touched.
- The spare empty book stays in her pack. Selling it player-first is
  unimplemented (owner rule) and out of this brief.
- "this scribe has nothing the book lacks (0 of its scrolls are already
  known)" — the scribe-side lacks-check reads oddly; not investigated.
- GET_TOOL / Poisoning dagger untouched by request.
