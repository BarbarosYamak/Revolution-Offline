# NPC material buyers — re-derived after the tm_vend.scp restore (2026-09-02)

Supersedes the buyer half of `artifacts/npc_floor_2026-09-02.md`. That artifact
read the 23 material `BUY=` rows in `runtime/scripts/templates/tm_vend.scp` as
commented out and concluded the shard buys no logs, boards, ingots or hides.
The commenting was a TNS donor artefact, already REJECTED in
`docs/TNS_WORLD_ECONOMY_DONOR_AUDIT.md` §3.5. The owner restored the rows on
2026-09-02; backup of the pre-restore file at
`artifacts/tm_vend_pre_restore_2026-09-02.scp`.

Scope note: only those 23 rows are now identical to
`server/Scripts-X/templates/tm_vend.scp`. The runtime file as a whole is still
TNS's, not the stock Scripts-X file.

## Method

1. Parse every `[TEMPLATE VENDOR_B_*]` in `runtime/scripts/templates/tm_vend.scp`,
   skipping `//`-prefixed lines, and record the enclosing template for each live
   `BUY=` row.
2. Resolve template→template `BUY=VENDOR_B_*` wholesale includes.
3. Map each template back to the `[CHARDEF]`s that carry `BUY=<template>` in
   `runtime/scripts/npcs/*.scp` (case-insensitive).
4. Count those chardefs in the live world save with
   `python tools/world_query.py --count <defname>` (repo-root `tools/`, current
   five save files, backup generations excluded).

## Live rows → template → chardef → spawned

All line numbers `runtime/scripts/templates/tm_vend.scp`, verified 2026-09-02.
`{a b}` is restock quantity, never a price.

| line | row | template | human chardefs (c_vendor_human.scp) | spawned |
|---|---|---|---|---|
| 293 | `BUY=i_log,{5 15}` | VENDOR_B_CARPENTER (@284) | c_carpenter :1798, c_carpenter_f :1867 | 3 + 6 = 9 |
| 294 | `BUY=i_board,{5 15}` | VENDOR_B_CARPENTER | same | 9 |
| 342 | `BUY=i_hides_cut,{2 6}` | VENDOR_B_COBBLER (@338) | c_cobbler :2054, c_cobbler_f :2128 | 12 + 6 = 18 |
| 343 | `BUY=i_hides_cut_2,{2 6}` | VENDOR_B_COBBLER | same | 18 |
| 344 | `BUY=i_hide,{2 6}` | VENDOR_B_COBBLER | same | 18 |
| 406 | `BUY=i_hide,{7 30}` | VENDOR_B_FURTRADER (@402) | c_furtrader :2636, c_furtrader_f :2710 | 6 + 2 = 8 |
| 480 | `BUY=i_hides_cut,{5 55}` | VENDOR_B_TANNER (@473) | c_tanner :5170, c_tanner_f :5244 | 6 + 3 = 9 |
| 481 | `BUY=i_hides_cut_2,{5 55}` | VENDOR_B_TANNER | same | 9 |
| 482 | `BUY=i_hide,{5 55}` | VENDOR_B_TANNER | same | 9 |
| 1084 | `BUY=i_ingot_iron,{4 34}` | VENDOR_B_TINKER (@1057) | c_tinker :5466, c_tinker_f :5536 | 1 + 6 = 7 |
| 1085 | `BUY=i_log,{4 34}` | VENDOR_B_TINKER | same | 7 |
| 1086 | `BUY=i_board,{4 34}` | VENDOR_B_TINKER | same | 7 |
| 1421 | `BUY=i_ingot_iron,{5 38}` | VENDOR_B_PROVISIONER (@1386) | c_provis :4287, c_provis_f :4360 | 10 + 10 = 20 |
| 1438 | `BUY=i_log,{5 38}` | VENDOR_B_PROVISIONER | same | 20 |
| 1439 | `BUY=i_board,{5 38}` | VENDOR_B_PROVISIONER | same | 20 |
| 1506 | `BUY=i_ingot_iron,{3 13}` | VENDOR_B_JEWELER (@1496) | c_jeweler :3471, c_jeweler_f :3545 | 1 + 7 = 8 |
| 1632 | `BUY=i_log,{24 72}` | VENDOR_B_BOWYER (@1621) | c_bowyer :1499, c_bowyer_f :1572 | 1 + 6 = 7 |
| 1858 | `BUY=i_log,{10 15}` | VENDOR_B_WEAPONS_BLADED (@1826) | c_blacksmith :1248, c_weaponsmith_blade :6035/_f :6117 | 23 / 14 |
| 1859 | `BUY=i_ingot_iron,{10 15}` | VENDOR_B_WEAPONS_BLADED | same | 23 / 14 |
| 1894 | `BUY=i_ingot_iron,{10 15}` | VENDOR_B_WEAPONS_BLUNT (@1878) | c_blacksmith :1249, c_weaponsmith_blunt :6201/_f :6283 | 23 / 6 |
| 1895 | `BUY=i_log,{10 15}` | VENDOR_B_WEAPONS_BLUNT | same | 23 / 6 |
| 2114 | `BUY=i_log,{4 18}` | VENDOR_B_BLACKSMITH (@2109) | c_blacksmith :1250, c_blacksmith_f :1350 | 18 + 5 = 23 |
| 2115 | `BUY=i_ingot_iron,{44 88}` | VENDOR_B_BLACKSMITH | same | 23 |

The elf (`c_vendor_elf.scp`) and gargoyle (`c_vendor_gargoyle.scp`) chardefs use
the same templates and spawn **zero** (`c_blacksmith_elf` 0,
`c_blacksmith_gargoyle` 0). They are not routes.

Wholesale includes found: `VENDOR_B_PROVISIONER` takes `BUY=VENDOR_B_BOWYER`,
`BUY=VENDOR_B_WEAPONS_BLADED` and `BUY=VENDOR_B_WEAPONS_BLUNT`. It already
carries its own log/board/ingot rows, so the includes add nothing new here.

## Rows added to `src/economy/Market.cpp` kNpcBuyers

Ordered most-spawned-first (shortest errand). Trade strings are the ones
`ServiceForTrade` (Runner.cpp:887) resolves.

- `i_ingot_iron`: blacksmith, provisioner, jeweler, tinker
- `i_log`: blacksmith, provisioner, carpenter, bowyer, tinker
- `i_board`: provisioner, carpenter, tinker
- `i_hide`: cobbler, tanner
- `i_hides_cut`: cobbler, tanner

Not added, with reasons:

- **weaponsmith** rows (`:1858/:1859/:1894/:1895`) — `ServiceForTrade` maps both
  `"weaponsmith"` and `"blacksmith"` to `wm::Service::Blacksmith`, so the row
  would be the same errand twice.
- **furtrader** (`:406`) — `"furtrader"` and `"tanner"` both resolve to
  `wm::Service::Tanner` (ClientTravel.cpp:1565/1566, AtlasGenMain.cpp:592/593).
- **i_hides_cut_2** (`:343/:481`) — see the DUPEITEM finding below.

`ServiceForTrade` gained `{"tanner", Service::Tanner}` and
`{"cobbler", Service::Tailor}`, matching the mappings ClientTravel.cpp:1555/1565
and AtlasGenMain.cpp:592/600 already carry. Without them a hide row would fall
through to `GeneralVendor` and the travel leg would aim nowhere.

## Coloured ingots: SETTLED FROM SOURCE — ID only, hue never consulted

Question: does a `BUY=i_ingot_iron` row buy `i_ingot_shadow`
(`[ITEMDEF i_ingot_shadow]` + `ID=i_ingot_iron`,
`runtime/scripts/items/i_provisions_ore.scp:356-364`)?

**Answer: NO.** Chain, all `server/Source-X/src/game/`:

1. `chars/CCharNPCStatus.cpp:611` — `CChar::NPC_FindVendableItem` looks the
   player's item up in the vendor's `LAYER_VENDOR_BUYS` container with
   `pContBuy->ContentFind(CResourceID(RES_ITEMDEF, pVendItem->GetID()))`.
   No hue term anywhere in the function.
2. `CContainer.cpp:228` — `ContentFind` delegates to `IsResourceMatch(rid, ...)`.
3. `items/CItem.cpp:6041` — `IsResourceMatch` returns true only on
   `rid == pItemDef->GetResourceID()`. The `RES_ITEMDEF` fallback below it
   (`:6046-6071`) special-cases exactly two pairs: `ITEMID_LOG_1` ← `ITEMID_BOARD1`
   and `ITEMID_HIDES` ← `ITEMID_LEATHER_1`. Everything else returns false.
4. `items/CItemBase.h:309` — `CItemBase::GetID()` returns
   `GetResourceID().GetResIndex()`, i.e. the itemdef's own resource index.
5. `items/CItemBase.cpp:1659-1694` — a script `ID=` line (`IBC_ID`) does
   `CopyBasic(pItemDef)` and sets `m_dwDispIndex` only, i.e. the DISPLAY id
   (`GetDispID()`, CItemBase.h:313). It never changes the itemdef's resource id;
   the guard at `:1661` in fact requires the itemdef's own id to be
   `>= ITEMID_MULTI`, which is what a named `[ITEMDEF i_ingot_shadow]` section
   gets.

So `i_ingot_shadow` is a distinct RES_ITEMDEF that merely borrows iron's artwork.
`ContentFind` for it finds nothing in a buy box holding `i_ingot_iron` (01bef).
The thirteen coloured hues still bank. Hue is not part of the match key in
either direction — the payout question is separate and unmeasured.

Status: **verified source behaviour**, not runtime-verified (shard down).

### Related: t_log vs t_board defeats the log←board leniency

`NPC_FindVendableItem` also runs `pVendItem->GetType() != pItemSell->GetType()`
(`CCharNPCStatus.cpp:618`). `i_log` is `TYPE=t_log`
(`items/i_provisions_logs.scp:62`) and `i_board` is `TYPE=t_board` (`:27`), so
the `ITEMID_LOG_1 ← ITEMID_BOARD1` case in `IsResourceMatch` can never complete a
sale. Each needs its own row, and each has one.

### Related: DUPEITEM collapses before the vendor sees it

`[ITEMDEF 01081] DEFNAME=i_hides_cut_2 DUPEITEM=01067`
(`items/i_profession_tailor_tanner.scp:396-399`).
`CItemBase::FindItemBase` (`items/CItemBase.cpp:2254-2256`) resolves a dupe id
through `CItemBaseDupe::GetItemDef()` to the master `CItemBase`, so such an
item's `GetID()` is 01067 = `i_hides_cut`. The bot-side rows for `i_hides_cut`
already cover it; `tm_vend.scp:343/:481` are belt-and-braces. The bot's
graphic→defname table (`src/progression/VendorPolicy.cpp:332`) only ever names
0x1067 anyway.

## Payouts — NOT stated

`i_ingot_iron` ([ITEMDEF 01bef], `items/i_provisions_ore.scp:220-231`) carries
`RESOURCES` and `SKILLMAKE` but **no `VALUE=` line**, so Sphere computes the
payout (`items/CItemBase.cpp` `GetMakeValue` → `CalculateMakeValue`). Nonzero,
but must be read off the 0x9E window, never predicted.

Where a `VALUE=` does exist, the payout is VALUE reduced by `VENDORMARKUP`,
which is a **percentage** (`chars/CCharNPCStatus.cpp:332-345`, "+100% = double
price"), not a flat subtraction. The relevant values: `i_log` VALUE=1
(`items/i_provisions_logs.scp:63`), `i_board` VALUE=2 (`:28`), `i_hide` and
`i_hides_cut` VALUE=5 (`items/i_profession_tailor_tanner.scp:352, :300`). The
rounded gp figure is UNKNOWN — the markup source and rounding were not traced to
the end.

**A log at VALUE=1 may round to ZERO gold after markup.** That is an economic
hazard, not a floor: a lumberjack could walk 200 logs to a carpenter and be paid
nothing while the stock is still consumed. Measure this first when the shard
returns; no life should plan around log income until then.

## Player-first is untouched

The restore changed the vendor TABLE, not the sale POLICY. `NpcBuyersFor` still
gates every material row on `faucet::NpcFloorOpenFor(item, playersDeclined)`
(`Market.cpp`), so `HasNpcBuyer("i_log")` at the default
`playersDeclined=false` remains FALSE — that is the WTS window, no longer an
empty table. `HasNpcBuyer("i_log", true)` and
`HasNpcBuyer("i_ingot_iron", true)` are now TRUE.

## Tests

- `tests/m7_market.cpp` `TestNpcPriceFloorBuyerResolution`: 18 restored rows
  added to the positive `kBuys` table; `i_log`, `i_board`, `i_ingot_iron`,
  `i_hide`, `i_hides_cut` removed from `kNoBuyer` (they were asserting the TNS
  defect). `kNoBuyer` keeps `i_ore_iron`, `i_ingot_valorite`, `i_hides_cut_2`,
  `i_cloth`, `i_cloth_bolt`, `i_wool`, `i_yarn_ball`, each with its source
  citation. The "permission without a buyer" case moved from `i_log` to
  `i_ore_iron`, which genuinely has no BUY row anywhere in the tree.
- `MaySellToNpc` for a lumberjack's logs and a smith's ingots flipped from
  refused to allowed at `playersDeclined=true`, with the window-open refusal
  asserted alongside so the player-first rule cannot be silently dropped.
- `tests/m7_market.cpp` `TestNpcsMayNotBuyPlayerMarketGoods` and
  `TestArbitrageGuardStillApplies`: the default-strict assertions kept (they are
  the player-first window, not the defect) and re-commented, with new positive
  assertions that the counter exists once the window closes.
- `tests/m37_economy.cpp` `TestNpcPriceFloor`: header comment corrected; the
  policy assertions were already right and are unchanged.

ctest: 41/41.

## Limitations

- Shard down. Everything here is verified script + Source-X source behaviour.
  No live run has confirmed a blacksmith's 0x9E window lists iron ingots, nor
  what one pays.
- Spawn counts are read from the world save, not a running server. They differ
  sharply from the counts in `artifacts/npc_floor_2026-09-02.md` (which reported
  e.g. c_provis 240 against 20 here); that artifact's counts appear to have
  included rotating backup generations.
