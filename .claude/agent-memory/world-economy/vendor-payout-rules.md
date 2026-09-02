---
name: vendor-payout-rules
description: Payout = itemdef VALUE less 15%; {a b} is restock quantity not price; and an ITEMDEF with NO VALUE line gets a COMPUTED value, not zero
metadata:
  type: project
---

- `BUY=i_x,{4 24}` — the braces are the **restock quantity**, never a price.
- Payout = the ITEMDEF's `VALUE` less `VendorMarkup=15`
  (`CServerConfig.cpp:158`, unset in sphere.ini). VALUE=3 pays 2, VALUE=6 pays 5.
- **An ITEMDEF with no `VALUE=` line does NOT pay zero.** Sphere computes one
  from `RESOURCES` + `SKILLMAKE`
  (`server/Source-X/src/game/items/CItemBase.cpp:1026` `GetMakeValue` →
  `CalculateMakeValue`, the `m_values.m_iLo == INT64_MIN` branch). The metal
  ingots (i_ingot_iron/copper/gold/silver, `items/i_provisions_ore.scp:218+`)
  are exactly this case — they carry no VALUE, only `RESOURCES=i_ore_*` and
  `SKILLMAKE=mining`. The ORE defs above them (:41-211) DO carry VALUE, which
  makes it easy to grep the wrong lines and conclude ingots are worthless.

**Why:** derived while building the NPC price floor, 2026-09-02. Stating a
computed payout as a fact would be inventing a number.

**How to apply:** for any item without a literal VALUE line, mark the payout
RUNTIME-UNVERIFIED and read it off the 0x9E vendor window at runtime. Never
predict it. Sphere scripts are case-insensitive — `SELL=i_BOTTLE_EMPTY` and
`BUY=i_bottle_empty` are the same item, so always grep -i.

See [[material-buy-rows-commented]].
