---
name: using-a-tool-wields-it
description: Double-clicking a weapon/tool in the pack makes Sphere WIELD it (0x1D then 0x2E), displacing whatever was in the hands — so "use item on" is never free for a fisher/lumberjack
metadata:
  type: feedback
---

Source-X wields a bladed weapon that is double-clicked in the pack. Two
consequences that both bit at once (wave 2026-09-02, fisher equip storm):

1. The item leaving the pack arrives as a plain **0x1D delete**. Treating that
   as "the item was consumed" finishes the action before the tool's own target
   cursor arrives — and then nothing answers the cursor, so the gesture never
   happens at all. Guard: for `UseItemOn`, a subject deletion while
   `awaitingTarget` is still true is a WIELD, not a consumption.
2. The wield displaces the previous hand item (a TWOHANDS=Y fishing pole goes
   straight back to the pack). So "use the dagger" costs a re-arm of the pole.
   Any per-item use of a bladed tool must be **batched**, and the tool must be
   looked for in the hands as well as the pack (`FindBlade`, not `FindAny` over
   the pack) or the runner cannot see the tool it just wielded.

Related: equipping something ALREADY worn does not no-op server side — Sphere
strips it into the pack ("You put the X in your pack"), which reads back as an
empty hand and starts the loop again. `act::EquipWouldBeNoOp` in
`include/uo/actions.h` is the guard.

**Why:** these three together produced 1183/1113 equip actions in 30 minutes
across two fishers, and the fish were never cut once in 322 attempts.

**How to apply:** before adding any "use tool on thing" action, ask what the
hands hold, whether the tool must be wielded, and what that displaces. See
[[state-flags-need-the-latest-statement]] — the equipment list the server sent
is the authority on what is in hand, never a local flag. Full write-up:
`artifacts/fish_equip_fix_2026-09-02.md`.
