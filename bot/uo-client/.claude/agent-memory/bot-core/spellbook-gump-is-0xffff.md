---
name: spellbook-gump-is-0xffff
description: Sphere answers a spellbook double-click with a 0x24 whose gump id is 0xFFFF, and sends no 0x3C at all when the book is empty — reading 0xFFFF as "close container" timed out every caster in the fleet
metadata:
  type: project
---

`GUMP_OPEN_SPELLBOOK = 0xFFFF`. It is an OPEN, not a close.

- `server/Source-X/src/game/uo_files/uofiles_enums.h:1093` defines it.
- `CClient::addSpellbookOpen` (`CClientMsg.cpp:2325`) sends
  `addOpenGump(book, GUMP_OPEN_SPELLBOOK)`.
- `addOpenGump` (`CClientMsg.cpp:449`) is the **only** construction site of
  `PacketContainerOpen` in the entire server, so **Sphere never sends a 0x24
  to close anything**. Any "0xFFFF closes it" comment in our client is
  imported protocol folklore, not this shard.

Second half, and the one that bites after the first is fixed:
`addSpellbookOpen` sends the 0x3C **only when the book holds a spell**
(`CClientMsg.cpp:2337-2340`, `if (count <= 0) return;`). Ordinary containers
always get one (`addContainerSetup`). So for an EMPTY book the 0x24 is the
only packet that will ever arrive, and anything keyed on `containerItems_`
— `Client::ContainerKnown`, `ContainerItemCount`, `Runner::BookHasSpell` —
stays false forever unless the 0x24 seeds the listing itself.

**Why it mattered:** `Train.cpp` and `Survive.cpp` both gate the caster
branch on `!client.ContainerKnown(obs.spellbookSerial)` and re-issue
`ActionOpenContainer`. With 0xFFFF swallowed, `open_container` timed out at
its full 4 s deadline five times, the planner abandoned TRAIN_COMBAT with
progress 0, re-picked it, and did that for the whole session — for all 35
casters. Fixed in `Client::OnDrawContainer` 2026-09-06;
`artifacts/fix_spellbook_open_2026-09-06.md` has the packet trace and the
Aurelius/Leander smoke.

**How to apply:** this is the fifth instance of
[[action-timeout-means-unrecognised-answer]] and the first where the answer
was invisible in the log because the handler returned *before* its own
`LogInfo`. When a packet handler has an early `return` above its log line,
"nothing in the log" is not "nothing on the wire" — read the handler before
believing the log. And a container-open confirmation is not always followed
by contents: prove the server sends the 0x3C for the case you care about.
