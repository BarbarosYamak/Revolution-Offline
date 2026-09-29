# open_container timeout storm on every caster — root cause and fix

Date 2026-09-06. HEAD at start 178fe43, ctest 45/45.

## Symptom

`TRAIN_COMBAT` issues `open_container serial=0x40013046` (Aurelius' pack
spellbook), it times out at the full 4 s `kUseTimeoutMs` deadline, five times,
then the planner abandons the goal ("attempts 5 >= 5") with progress 0 and
re-picks it. Forever. 35 of the 100-bot fleet are casters and all of them go
through the same gate (`Train.cpp:171`, `Survive.cpp:724`).

Pre-fix evidence (run of 16:41): `run_gates/g_Aurelius.console.txt:58-77` and
`:105-125`. The raw log for the same window,
`run_gates/g_Aurelius.log:44-70`, shows **no `[0x24]` and no `[0x3C]` line at
all** between the request and the timeout — while the login backpack open at
`:35-36` logs both.

## Root cause — verified against Source-X

`Client::OnDrawContainer` (`src/Client.cpp`) had:

    if (gumpId == 0xFFFF) {  // close / clear
        ... erase openContainers_/containerItems_ ...
        return;              // <- before the log line and before the action hook
    }

That `return` fires *before* `LogInfo("[0x24] ...")` and before
`ActionOnContainerOpened`, which is exactly why the pre-fix log shows nothing.

**0xFFFF is not a close on this shard — it is the spellbook gump.**

- `server/Source-X/src/game/uo_files/uofiles_enums.h:1093`
  `GUMP_OPEN_SPELLBOOK = 0xFFFF`.
- `server/Source-X/src/game/clients/CClientMsg.cpp:2325`
  `CClient::addSpellbookOpen` -> `addOpenGump(pBook, GUMP_OPEN_SPELLBOOK)`.
- `CClientMsg.cpp:449` `addOpenGump` is the **only** construction site of
  `PacketContainerOpen` (`src/network/send.cpp:868`) in the whole server, so
  Sphere never sends a 0x24 meaning "close".
- `CClientUse.cpp:506-518` routes every `IT_SPELLBOOK*` double-click to
  `addSpellbookOpen`.

Second half of the cause: `addSpellbookOpen` sends `new PacketItemContents`
(0x3C) **only when the book holds at least one spell** —
`CClientMsg.cpp:2337-2340`, `if (count <= 0) return;`. A normal container
always gets a 0x3C (`addContainerSetup` -> `addContainerContents`,
`CClientMsg.cpp:457-479`). So for an empty book the 0x24 is the only answer
that will ever arrive, and `ContainerKnown()` (which reads `containerItems_`)
could never become true even once the action was fixed.

Client version is irrelevant here: the shard advertises features `0x0005`
(`g_Aurelius.log:16`), so `FEATURE_AOS_UPDATE_B` is off and the newer
`PacketSpellbookContent` (0xBF sub 0x1B) branch in `addSpellbookOpen` is not
taken. Ruled out.

## Fix

`src/Client.cpp`

1. `OnDrawContainer`: the `gumpId == 0xFFFF` branch no longer erases and
   returns. It now seeds an **empty** `containerItems_[serial]` entry (only if
   one is not already there) and falls through to the ordinary open path, so
   the book is registered, `[0x24]` is logged, and
   `ActionOnContainerOpened` answers the action. A book that does hold spells
   has its listing cleared and refilled by the 0x3C that follows.
2. `ActionOnContainerOpened`: the "adopt an unsolicited container as the bank
   box while open_bank is outstanding" branch now excludes `gumpId == 0xFFFF`.
   A spellbook is never the bank box, and admitting 0xFFFF to this function
   newly exposed that path.

Nothing in `Survive.cpp`, `Train.cpp`, `Needs.cpp` or `grade_life.py` was
touched. The action's timeout, retry policy and the callers are unchanged.

## Test

`tests/trade_verify.cpp` — `TestSpellbookGumpIsAnOpenNotAClose()`, three
cases on the real `Client` with scripted packets:

- empty book: one 0x24 gump 0xFFFF finishes `open_container` with `Success`,
  `ContainerKnown()` true, `ContainerItemCount()` 0;
- full book: the following 0x3C still fills it (3 rows) and a row's `amount`
  is still the spell number;
- a 0xFFFF container is not adopted as the bank box while `open_bank` waits.

`python tools/rev.py build test` -> **ctest 45/45 pass** (trade_verify 42
checks, 0 failures).

## Smoke — `rev.py gates CHARS=Aurelius,Leander MINUTES=5` (16:56–17:01)

| | Aurelius | Leander |
|---|---|---|
| `open_container timeout` | **0** (was 5 per goal pick, forever) | **0** |
| spellbook 0x24 | `g_Aurelius.console.txt:64` `gump=65535`, action `success (0ms)` | `g_Leander.console.txt:72` + 0x3C **21 rows** at `:75` |
| `[ACTION] cast_spell` | 0 — see below | **14**, 6 confirmed `cast_spell success` (`:255,268,281,293,308`) |
| outcome | clear hand-off reasons, no spin on TRAIN_COMBAT | `goal_completed=BANK progress=10` (`:543`) |
| deaths | none | none (`runtime/logs/sphere2026-09-06.log`, no `was killed by` for either) |

Leander is the control that proves the non-empty path end to end: 0x24 gump
0xFFFF, then a 0x3C with 21 spell rows, then `BookHasSpell` picks spell id 5
and the casts land.

## Why Aurelius still does not cast — a DIFFERENT defect, not this one

Her carried book is genuinely empty. World save
`runtime/save/spherechars.scp` (via `tools/world_query.py --item 040013046`):

    [WORLDITEM i_spellbook] SERIAL=040013046 CONT=04000ee02   <- pack, NO MORE1

while the book with the spells is in her **bank**:

    [WORLDITEM i_spellbook] SERIAL=04000edd5 MORE1=0386affff CONT=04000ee06

`MORE1=0x386affff` is the 23-spell bitmask that matches the 23 entries in
`bot_data/revgen3_22.aurelius/state.json` `known_spells`. So she is carrying
the wrong book. With the fix she now says so honestly instead of hanging:

    hunt: caster attack spell -- supplied=-1 known=-1 (none) book=0x40013046
    handoff=TRAIN_COMBAT->PRACTICE_SKILL reason="no castable attack spell:
      train or improve the spellbook first"
    goal=FILL_SPELLBOOK ...
    goal_failed=FILL_SPELLBOOK reason="no scroll seller reachable after 3 trips"

(`g_Aurelius.console.txt:134-136,325-327,337`.) The remedy — carry the full
book, or move the spells — is a gear/bank decision and belongs to
`bot-brain`, not to the action layer.

## Pre-existing defect unmasked, out of scope

`goal_spinning=GET_TOOL reason="completed 5 times in a row with progress 0"`
(`g_Aurelius.console.txt:279`). GET_TOOL is now reachable because
TRAIN_COMBAT stops eating the whole session; the spin is in GET_TOOL, not in
this change.
