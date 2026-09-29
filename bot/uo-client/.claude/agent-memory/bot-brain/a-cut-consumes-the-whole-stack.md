---
name: a-cut-consumes-the-whole-stack
description: Scissors on cloth delete the ENTIRE targeted stack, so a "keep for the bench" that lives in the pack is not kept at all -- size the withdrawal, never the cut
metadata:
  type: project
---

Source-X's `IT_SCISSORS` handler takes `iOutQty = pItemTarg->GetAmount()` and
then `pItemTarg->Delete()` (`server/Source-X/src/game/clients/CClientTarg.cpp`
:2152-2179), creating one bandage per cloth. The shard does not intercept it:
`runtime/scripts/types/type_scissors.scp:41` returns 0 for `t_cloth`,
`t_cloth_bolt` and `t_clothing` and hands the use straight back to the engine.

So **there is no such thing as cutting twenty of the thirty cloth in the pack**.
One gesture converts and destroys the whole stack, and loose cloth in a pack
auto-merges into one stack.

Consequence for any "keep N for the bench, cut the rest" rule: the keep has to
survive somewhere the scissors cannot reach — the BANK BOX. What gets sized is
the WITHDRAWAL, never the cut. `life::ClothCuttableForSale` (include/uo/life.h)
is written that way: it returns the size of the cut, which is always
`packCloth + take`, and the box is left holding at least `keep`.

`CutClothForSale`'s own comment ("this runs only on what is left above the
sitting this life is funded for") predates this and is optimistic: it cuts
everything carried, keep included. Harmless where it runs today (MAKE_CLOTH's
"batch covered" exit, where the pack is by definition surplus), but do not build
a partial-cut rule on it.

Related: [[wool-makes-cloth-not-thread]], [[a-sitting-size-is-not-the-stock-size]].
