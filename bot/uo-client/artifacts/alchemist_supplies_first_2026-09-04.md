# Alchemist opens on supplies, not on comforts — 2026-09-04

Owner ruling 2026-09-04: an alchemist opens her life by buying a LOT of
nightshade and empty bottles from the NPC alchemist and brewing poison — the
brew batch is both the training and the stock. She must NOT open by buying NPC
heal potions.

## Baseline (before)

`run_gates/g_Elara.console.txt`, 2026-09-03 15:13, fresh char, 10000 gp,
Alchemy 50 / Magery 50:

    NeedMount 0.80 x 255 = 204   -> BUY_MOUNT
    NeedBank 0.75 x 240 = 180    -> BANK
    NeedEquipment 0.50 x 260=130 -> REPLACE_EQUIPMENT (4 NPC heal potions, progress=0)
    NeedSpells 0.70 x 110 = 77   -> FILL_SPELLBOOK
    NeedSupplies 0.44 x 140 = 62 -> never won

## Change, in three rules (all in the need model, no goal ordering)

1. **"Is the shop open yet?"** (`src/life/Needs.cpp`, `unstockedCrafter`).
   A life whose FIRST `income` is `Craft`, that cannot make ONE of its own
   goods (`ChooseCraft` at batch 1), whose shortfall the vendor policy allows,
   and that has working capital, is a shop with empty shelves. The gate is the
   *shortfall*, not the job title — a lumberjack is short of logs and a tailor
   of cloth, both refused by `data/revolution_vendor_policy.tsv`, so neither
   ever sees this.
2. **NeedSupplies is a rate, not a second constant.** Urgency slides
   `0.95 -> 0.44` on `madeable / stockBatch` (worst-stocked input against the
   batch the best-stocked one funds). 0.95 x 140 = 133 clears the heal-potion
   browse (130) and the spellbook (77); the 0.44 floor is the scribe-incident
   number and is unmoved, so a balanced crafter still crafts (65) before it
   shops (62).
3. **`CraftBatchFromStock`** (`include/uo/life.h`, `src/life/Identity.cpp`).
   The batch is read off the best-stocked input, so a bulk buy sets the size of
   the sitting. Without it Elara came home with 84 nightshade, kept her four
   newbie bottles, and the fixed batch of 5 said "one bottle short" — small
   enough that "can I make one?" answered yes and the errand vanished. She
   brewed twice and stopped with 82 leaves in the pack (run 1, 00:55).
   `DoBuySupplies` asks the same question so goal and need agree.

Dampers, both narrow:

* `NeedMount` 0.80 -> 0.25 while `unstockedCrafter` (0.25 x 255 = 63.75, under
  the stocking trip, over idling — the horse is still bought later).
* `NeedEquipment("heal potions")` 0.50 -> 0.20 only when
  `BrewsOwnHealPotion()` — the profession's own `produces` names a heal potion
  AND the carried skill reaches that recipe's gate (`i_potion_heal`
  ALCHEMY 15.1, `src/progression/Production.cpp:227`). Every fighter, gatherer
  and below-the-gate crafter keeps 0.50 exactly.

`i_bottle_empty` needed no behaviour change — it has been on the alchemist's
`consumes`, graded `REVOLUTION_NPC_VERIFIED buy=1`
(`data/revolution_vendor_policy.tsv:106`), and mapped to the "alchemist" trade
by `SupplierTradeFor` all along. Only the comment at `Professions.cpp:522`
("bottles are NOT bought") was stale; it is now corrected with the shard
evidence (`SELL=i_BOTTLE_EMPTY,250`, VENDOR_S_ALCHEMIST).

## Dynamic batch size — `BulkSupplyQty` (`src/life/runner/Economy.cpp`)

Sized at the counter, where the quote is known. Terms:

    budget = gold - Profession::goldReserve      (the reserve is kept)
    share  = budget / (recipe inputs still to be bought)
    outputs= share / (perOutput * quotedUnitPrice)
    qty    = outputs * perOutput
    caps   = shelf amount (Sphere refuses an over-ask outright),
             0.8 * maxWeight - weight at a pessimistic 1 stone/unit,
             and the existing 100-gold hard floor
    never less than the sitting's own shortfall

## Runtime evidence (5-minute gates, `rev.py gates CHARS=Elara MINUTES=5`)

Run 1 (00:52), fresh:

    :72  goal_changed=BUY_SUPPLIES ... "BUY_SUPPLIES 133.0 superseded PRACTICE_SKILL 36.0"
    :221 supplies: buying 84 i_reag_nightshade at 3 each from 'Nightshade'
    :848 craft: making i_potion_poison -- using its own tool to open the menu
    :861 craft: made i_potion_poison pack 0->1

Run 3 (01:08), carrying 76 nightshade and 0 bottles:

    :83  goal=BUY_SUPPLIES reason="no goal was running"
    :86  reason: NeedSupplies urgency 0.95 x 140 = 133.0
    :242 NeedMount(riding horse 0.25)            <- damper visible
    :294 bank: withdrawing 758 gold -- 864 is wanted for a purchase
    :371 reason: i_potion_poison x38 needs 5 x i_bottle_empty (can make 0 of 38)
    :373 supplies: buying 72 i_bottle_empty at 12 each from 'empty bottles'

No `REPLACE_EQUIPMENT` / heal-potion goal in any of the three runs.
`ctest 43/43`. `grade CHARS=Elara`: 12/18 (FARM-2 TRAIN-1 TRAIN-2 LIVE-1
LIVE-3 LIVE-5).

## Open defect, outside this brief

The 72-bottle order never landed: every `vendor_buy` was answered
`System: You can't reach the Vendor` (`:377`, repeated ~20 times over three
minutes). Same NPC and same shop counter already documented at
`src/Client.cpp:2906-2923` — `NearestShopkeeperWithTrade` was fixed to return a
shopkeeper the line-of-sight test refuses, but the *approach* step in
`DoBuySupplies` measures `TileDist <= kVendorReach` and therefore never walks
round the counter, so the buy is issued from a spot Sphere's `CChar::CanTouch`
rejects. Retry behaviour also violates the owner's "unreachable = 1 try, max 2"
rule: 5 asks per goal entry, re-picked four times. Vendor reach and the retry
ladder are `bot-core`'s, not the need model's.
