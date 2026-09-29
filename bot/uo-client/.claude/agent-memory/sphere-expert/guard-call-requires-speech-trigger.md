---
name: guard-call-requires-speech-trigger
description: Sphere guards do not auto-summon on evil-creature attack inside a guarded zone; only a literal chat "GUARD"/"GUARDS" line does, and the bot never sends one
metadata:
  type: project
---

Root cause of the 2026-09-06 fleet-100 in-town Britain deaths (Tordor,
Serena, Odessa killed by ordinary c_orc/c_lizardman/harpy wanderers while
standing inside `a_townBritain`, a REGION_FLAG_GUARDED area,
`runtime/scripts/maps/map0/map0_areas.scp:2887`).

**The rule:** `GuardsOnMurderers=1` (`server/Source-X/src/sphere.ini:764`)
only matters inside `CChar::CallGuards()` (no-arg overload,
`server/Source-X/src/game/chars/CCharFight.cpp:178-213`, scans nearby chars
for `STATF_CRIMINAL` or `Noto_IsEvil()`). That no-arg overload has exactly
ONE call site in the whole engine:
`server/Source-X/src/game/clients/CClientEvent.cpp:1871`, inside
`CClient::Event_Talk_Common`, gated on the player's chat text matching the
`guardcall` VARDEF (default `"GUARD,GUARDS"`, checked via `FindStrWord`).
There is no proximity/damage-based auto-summon — a player being beaten to
death by an "evil" monster inside a guarded zone gets NO guard unless
something actually speaks the trigger word.

(A second, unrelated overload `CallGuards(pCriminal)` also exists and fires
from `CCharFight.cpp:90` when an NPC witness sees a *player* commit a crime
against another player — irrelevant to monster-vs-player combat.)

**Why:** confirmed by reading `CClientEvent.cpp` and `CCharFight.cpp`
directly (CURRENT_SOURCE) and cross-checked against the bot's own
`bot/uo-client/src/life/runner/Survive.cpp` `RetreatToSafety()`
(lines 152-159) / `FLEE_TO_GUARDS` interrupt (lines 931-936): it only calls
`EnsurePeaceMode()` + `TravelToService(Banker)` — no speech action anywhere
in that path. Grepped `Speak`/say-with-"guard" across
`bot/uo-client/src/life/runner/*.cpp` — zero hits.

**How to apply:** this is a BOT-SIDE gap, not a shard misconfiguration — the
guarded-zone flag and rect were correct at the death tile (confirmed via the
bot's own `event death_location: ... region=a_townBritain` log line). Any
fix belongs in `Survive.cpp`'s flee/retreat path (emit a chat line matching
the "GUARD,GUARDS" guardcall trigger, e.g. via whatever ActionSay/Speak
primitive the client exposes) — not in Source-X or the region scripts. Full
evidence: `bot/uo-client/artifacts/britain_intown_deaths_2026-09-06.md`.

Separately, `CCharNPCAct.cpp:1507-1527` (`NPC_Act_GoHome`) self-destructs any
already-conjured guard whose home point's AREADEF isn't `IsGuarded()`
("no guard post" warning) — fired 1924x in one run's sphere log, mostly at
locations elsewhere on the map, not specifically tied to these deaths. Worth
a separate look at guard home-point assignment vs. Britain's 5-RECT
geometry, but it did not cause these 4 deaths since no guard was ever
conjured for them in the first place.
