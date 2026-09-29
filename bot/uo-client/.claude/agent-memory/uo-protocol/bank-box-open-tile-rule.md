---
name: bank-box-open-tile-rule
description: Sphere stamps the bank box with the exact tile it was opened on and silently bounces every drop/lift from anywhere else - one step is enough
metadata:
  type: project
---

A Sphere bank box only answers from the **exact tile** the character stood on
when the box was opened. Not a radius - `CPointBase` equality.

**Where it lives in Source-X (read-only, verified source):**
- stamped at open: `src/game/items/CItemContainer.cpp:1119`
  (`m_itEqBankBox.m_pntOpen = pCharOpener->GetTopPoint()`)
- checked on DROP: `src/game/clients/CClientEvent.cpp:448-467`
  -> `Event_Item_Drop_Fail` (`CClientEvent.cpp:248-271`)
- checked on LIFT: `CChar::CanTouch`, `src/game/chars/CCharStatus.cpp:1063-1069`

**The refusal is silent.** No 0x27 drag-cancel, no sysmessage. The only thing
on the wire is a 0x25 putting the item back in the source container - wire-
identical to "the destination was full" or "the item is not allowed there".

**Why:** wave15 (2026-08-31) Kharain issued 1083 deposits and Titus 1626, all
while walking between tiles; every one bounced and the whole session hung on
the BANK goal. It read as a server refusal of the ITEM when it was a refusal
of the POSITION.

**How to apply:** any bank deposit/withdrawal must be issued standing still on
the open tile. Re-saying "bank" re-opens the box and re-stamps the tile, so
"forget the container and open it again from here" is the recovery. The client
tracks this in `Client::BankOpenTileHeld()` and refuses a doomed move before
sending it. Related: [[move-split-echo-is-not-an-answer]].
