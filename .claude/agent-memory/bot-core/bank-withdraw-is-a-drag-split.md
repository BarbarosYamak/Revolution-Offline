---
name: bank-withdraw-is-a-drag-split
description: Withdrawing gold is a drag-split from the bank container to the backpack (not speech), it works, and the proof is a need going quiet rather than a packet
metadata:
  type: project
---

Withdrawing coin is `ActionMoveItem(bankGoldStack, N, backpack)` — a lift/drop
split, not a spoken "withdraw N". Verified working on 2026-09-04
(`run_gates/g_Cyras.console.txt:3337`): 700 split off an 8,700 stack in 14 ms.

**Why:** the shard sends no "you withdrew N" line, so there is no journal proof
and the `move_item success` only says the destination accepted the drop. The
readable proof that the coin ARRIVED is second-order: `NeedBank(withdraw for a
purchase)` vanished from the next tick's needs list, and that need's only gate is
`coinWanted > goldOnHand` where `goldOnHand` is a live backpack count.

**How to apply:** when asked whether an item movement really landed, look for
the need or goal whose precondition it satisfies going quiet on the NEXT tick,
and check nothing else could have cleared that precondition. Also note
`PayFromPackOnly=0` — NPC purchases pay from the bank box, so a purchase that
stands down for lack of pack coin is our own policy, not poverty; the withdraw
is genuinely needed only for player-to-player payment.
