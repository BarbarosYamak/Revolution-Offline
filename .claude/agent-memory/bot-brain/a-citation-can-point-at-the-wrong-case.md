---
name: a-citation-can-point-at-the-wrong-case
description: A code comment citing engine file:line can be right about the line and wrong about which switch case it sits in — scissors "sheared" a sheep for weeks
metadata:
  type: feedback
---

When a comment cites `Engine.cpp:NNNN` for a mechanic, open the file and read
**upward to the enclosing `case`/`if`**. A correct line number inside the wrong
branch is a citation that looks verified and is not.

**Why:** `Runner.cpp` shipped a tailoring-chain comment reading
"scissors 0x0F9E on a woolly sheep -> wool, CClientTarg.cpp:1878, case
CREID_SHEEP", and `DoMakeBandages` used scissors accordingly. `CREID_SHEEP` at
:1880 is real — but it sits inside `case IT_WEAPON_SWORD / _AXE / _FENCE /
_MACE_SHARP / IT_CARPENTRY_CHOP` (:1866-1900). A sheep is a CHARACTER, so
`pItemTarg` is null and the `IT_SCISSORS` case (:2135) falls through to
"Scissors cannot be used on that to produce anything". The gesture could never
have worked, and the wrong citation is exactly why nobody re-checked it.
Sphere shears with a BLADE (weapon or knife); scissors cut the bolt.

**How to apply:** for any use-item-on-target mechanic, confirm three things,
not one — the item's TYPE branch, whether the target is a char or an item, and
whether a `runtime/scripts/types/type_*.scp` overlay intercepts it (an overlay
hooking `@TargOn_Item` only does not see char targets).

Related: [[a-grep-miss-is-not-absence]], [[absence-is-not-evidence]].
