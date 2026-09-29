# Moongate stall at Magincia 0x4000289E — root cause and fix (2026-09-02)

## Symptom (pre-fix, run_gates/)

- `g_Xerxes.console.txt:119` `[09:10:47.002] [gump] 0x4000289E context=0x80A0041C: 13 option(s), 15 text(s)`
  — the gate dialog, with `choice 10 = 'Ocllo (Newplayers)'`.
- `g_Xerxes.console.txt:161` `[09:10:48.149] pre-armed moongate destination 'Ocllo (Newplayers)'`
  — the pre-arm happened **1.1 s AFTER** the dialog arrived, so
  `travelGateDestination_` was still empty at 47.002 and neither the current
  nor the next leg was `LegKind::Moongate` (the gate was two legs away).
  Both answer branches in `OnGenericGump` were therefore skipped.
- `g_Xerxes.console.txt:171` `[09:10:49.414] using moongate 0x4000289E ... (gump active=0 serial=0x00000000)`
  — the held dialog was **gone** 2.4 s later. Repeated 158×, ~10 s apart,
  through to `09:15:34` and beyond. No second `[gump]` line exists anywhere in
  the file (grep over the whole console: exactly ONE `[gump] 0x...` line), so
  no legitimate gump replaced it.
- `g_Vorar.console.txt`: same gate, destination `'Britain'`, 18×.

## Root cause — verified in source

`src/Client.cpp`, packet dispatch. The block

```
case 0x23: case 0x53:
case 0x54: case 0x5B: case 0x65: case 0x6D:
case 0x70:
case 0x8B: case 0x97:
case 0xB0: OnGenericGump(data, size); break;
```

is commented "Common in-world packets we just log + ignore for M1" but has **no
`break;`** — every one of those nine packet types fell through into
`OnGenericGump`. `OnGenericGump`'s first statement was `gump_ = ActiveGump{};`,
followed by `if (size < 23) return;`.

So the gate's own **0x54 sound** (12 bytes) and **0x70 graphical effect**
(28 bytes) — precisely the packets Sphere sends alongside a moongate dialog —
cleared the held gump and returned before logging anything. Sphere will not
open a second dialog while one is unanswered, so every later double-click was
met with silence. Faustus survived only because his gump happened to arrive in
the same instant the use step ran, before any effect packet.

This is a *verified source* + *verified runtime* finding: the fall-through is
literal in the file, and the runtime log shows exactly one gump arriving and
being silently dropped with no other `[gump]` line to explain it.

## Fix

1. `src/Client.cpp` — the ignore list gets its own `break;`; `case 0xB0` stands
   alone.
2. `src/travel/ClientTravel.cpp` `OnGenericGump` — `gump_` is now cleared only
   *after* both bounds checks pass. A malformed 0xB0 no longer discards a
   dialog the server still holds open.
3. `src/travel/ClientTravel.cpp` `TravelUseTransit` — bounded retry per gate
   (`kMaxGateTries = 3`, counter keyed on the gate serial, reset by
   `TravelToPoint`):
   - try 1: answer a held dialog if present, else double-click;
   - a use step that finds no dialog cancels the last one the server sent for
     *this gate* (`SendGumpResponse(serial, context, 0, ...)`) to clear Sphere's
     pending state;
   - try > 1 also steps off the gate tile (2 tiles east) so `@step` can fire
     again on the replanned approach;
   - try 3: `moongate ... giving up after N tries`, `LogEvent("moongate_giving_up")`,
     `journey_.OnLegFailed(...)` → the route replans (walk is always usable).
4. `AnswerGump` / `CloseGump` now go through `ForgetAnsweredGump()`, so a dialog
   we have already replied to is not treated as stale by (3). Without it the
   smoke run produced one spurious cancel (`g_Vorar.err.txt:6`, gate
   0x400028A0, same millisecond as its successful `moongate_use`).

## Regression test — `tests/moongate_gump.cpp` (ctest `moongate_gump`)

Links the real `Client` (as `trade_verify` does) and feeds
`DispatchPacketForTest`:
- a hand-built 0xB0 moongate dialog parses to `button 1000 'OKAY'` +
  `choice 10 'Ocllo (Newplayers)'`;
- all nine log-and-ignore packet ids, then a real 0x54 and a real 0x70, leave
  `GumpActive()` true and `GumpContext()` intact — this fails on the old code;
- a truncated 0xB0 and one with an over-long `ctrlLen` also leave it intact.

`python tools/rev.py test` → 43/43 pass.

## Smoke — `gates CHARS=Xerxes,Vorar MINUTES=5` (12:25–12:33)

| Bot | Gate | Destination | Reply | Arrival |
|---|---|---|---|---|
| Xerxes | 0x4000289E | Ocllo (Newplayers) | `choice 10, button 1000` (console:105) | `travel_done ... ok=1 at=(3669,2619)` (:314) |
| Xerxes | 0x40002899 | Skara Brae | `choice 8, button 1000` (:520) | `travel_done ... ok=1 at=(591,2150)` (:562) |
| Vorar | 0x4000289E | Britain | `choice 2, button 1000` (:470), **`gump active=1`** at the use step (:468) | `travel_done ... ok=1 at=(1430,1691)` (:590) |
| Vorar | 0x400028A0 | New Magincia | `choice 5, button 1000` (:833) | `travel_done ... ok=1 at=(3649,2169)` (:887) |

Four moongate crossings, four replies, four arrivals. Every `using moongate`
line reads `try 1/3`. Zero `giving up`, zero repeated-`gump active=0` streams.
`g_Vorar.console.txt:468` is the held-gump seam firing exactly as intended.

Remaining `.err.txt` noise is unrelated: `0x21` move rejects on the Magincia
hillside (3623,2139,86 / 3632,2152,68), which is the fastwalk pipeline
recovering normally.

## Item 3 — Rhea's walk over a moongate: the cost model is NOT the fault

`src/travel/TravelMode.cpp:52` scores a moongate as
`EstimateWalkSeconds(walkTiles / 6) + 30`. At 714 tiles that is 79 + 30 = 109 s
against walking's `(714*2)/3 + 1 = 477 s` — the gate wins comfortably, and
`Choose` would pick it. The moongate arm is gated only on
`c.moongateRouteKnown`, hard-wired true at `ClientTravel.cpp:1032`, and on
`c.dead`.

Every `mode moongate no:` line for Rhea in `run_gates/g_Rhea.console.txt`
(:70, :126) reads **"dead characters cannot use gates"** — she was a ghost at
plan time. Her later plans (:189, :210) rank moongate `usable`. The specific
714-tile plan to (677,1177) is not in `run_gates/` (different wave directory),
so *whether* that one was a dead-plan or a planner miss is UNPROVEN here; what
is proven is that the cost model does not undervalue gates. A `transit=0` plan
while `mode moongate usable` would be a `RoutePlanner` gate-pair-selection
issue, not a mode-choice one — untouched by this brief.
