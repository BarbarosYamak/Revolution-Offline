---
name: a-pickup-is-a-deletion
description: A drag/lift the client itself sent arrives on the wire as a real 0x1D Delete Object for the item's old slot, which used to wipe a container's own cached contents on every pickup
metadata:
  type: project
---

Sphere's pickup protocol sends 0x1D (Delete Object) for an item's OLD spot
before the 0x25 (container) or 0x2E (worn) that places it at the new one --
even when the "deletion" is our own outstanding lift, not the item leaving
the world. `Client::OnDeleteObject` (src/Client.cpp) used to treat every 0x1D
the same way, including `containerItems_.erase(serial)` -- which is the
cached CONTENTS of that object when the object is itself a container.

Consequence: unequip-then-re-equip a spellbook (two lifts, ~1.5s apart) wiped
its 19-item contents cache while [[state-flags-need-the-latest-statement|the
shared spellbookOpened_ latch]] stayed true, so the next reader (a different
goal, PRACTICE_SKILL) trusted the latch and reported "the book holds 0
item(s)" even though the book never actually emptied
(run_gates/g_Aurelius.console.txt:83-90 vs 596-624, 2026-09-05).

Fix: `OnDeleteObject` now checks `drag_.InFlight() && drag_.Serial() ==
serial` (an in-flight lift WE sent) before erasing `containerItems_[serial]`
or dropping the `openContainers_` entry for that serial. An own-lift is a
relocation, not a real deletion, and the container's contents survive it.
The unconditional "strip this serial from every OTHER container's listing"
loop is untouched -- that part IS stale once the item moves.

**How to apply:** any future container-cache bug where a legitimately-owned,
still-open container "goes empty" right after an equip/unequip/drag should
start here, not at the container-open code. Grep `ownLift` in
`Client::OnDeleteObject`.
