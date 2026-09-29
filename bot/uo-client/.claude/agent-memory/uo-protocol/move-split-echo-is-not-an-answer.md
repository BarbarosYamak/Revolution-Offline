---
name: move-split-echo-is-not-an-answer
description: A partial lift makes Sphere send a 0x25 for the ORIGINAL serial still in the source container - it is the split, not the outcome, and a bounce looks identical
metadata:
  type: project
---

Lifting part of a stack splits it during the **pickup**, before the drop is
judged at all:

- `CChar::ItemPickup` -> `CItem::UnStackSplit(amount)`
  (`src/game/chars/CCharAct.cpp:3007-3010`)
- `UnStackSplit` sets **THIS** item - the ORIGINAL serial, the one that goes on
  to be dragged - to `amount`, and creates a **NEW** item for the leftover
  (`src/game/items/CItem.cpp:1251-1284`). The leftover keeps the new serial.
- `SetAmountUpdate` -> `Update()` -> `addItem()`
  (`CItem.cpp:2272-2286`, `4204-4239`) emits a 0x25 for the ORIGINAL serial,
  still in the SOURCE container, carrying the amount being LIFTED.

So a partial move produces a source-container 0x25 for the subject serial on
every attempt, success or failure. A bounce (`Event_Item_Drop_Fail`) puts the
same serial back in the same container with the same amount - **byte
identical**. Only arrival ORDER separates them: echo first, bounce second.

**Why this matters:** the client used to read that echo as "the remainder was
left behind, so the move succeeded". The arithmetic only ever matched when the
requested amount was exactly half the stack (10 of 20 iron ingots), which is
why wave15 scored 9 of its 11 `move_item success` results on bounced deposits
while nothing reached a bank box.

**How to apply:** never accept a source-container 0x25 as proof a move landed.
Wait for the destination's own 0x25 or the deadline. A full-stack move produces
no echo at all (UnStackSplit is skipped), so the first source echo there IS the
bounce. Related: [[bank-box-open-tile-rule]].
