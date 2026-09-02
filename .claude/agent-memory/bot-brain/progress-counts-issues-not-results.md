---
name: progress-counts-issues-not-results
description: goal progress=101/243 is not a corrupt number — it is NoteProgress crediting a busy-wait once per tick; check helpers that return true for "come back later"
metadata:
  type: project
---

`Goal::progress` is an unbounded `NoteProgress()` counter reset only when the
planner ACTIVATES a goal. A three-digit value is not corruption — it is a
per-tick credit.

**Why:** `Runner::ArmAxe` returns `true` both for "I issued an equip" and for
"an action is already in flight, come back". DoReplaceEquipment credited both,
so a 15-second in-flight `use_item_on` produced
`goal_completed=REPLACE_EQUIPMENT progress=243` having armed nothing (Xerxes,
wave 2). NoteProgress also clears `goal_.attempts`, so the failure ladder was
reset every tick and the goal could never run out of tries.

**How to apply:** any helper whose `true` means BOTH "acted" and "still busy" is
a NoteProgress trap — sample `ActionBusy()` before the call and credit only a
real issue. And a goal handler's fall-through `return true` is almost always
wrong: the genuine completion has its own early-out, so the tail should
Cooldown + Finish(false). Extends [[retry-shorter-than-timeout]] and
[[goal-that-did-nothing-must-stand-down]].
