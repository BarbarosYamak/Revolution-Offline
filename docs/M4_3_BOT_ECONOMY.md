# M4.3: Bots run their own economy

Date: 2026-09-29. **STATUS: BUILT AND UNIT-TESTED, NOT YET PROVEN LIVE.**

Owner request: bots should sell loot they don't need and maintain their own
economy. Before this change a bot never looted anything, never sold anything
(the client parsed a vendor's sell list, but scripts could not use it), and
spent gold without tracking it. Its only income was whatever arrived in the
bank by other means.

---

## 1. What a bot does now

```text
kill -> loot own corpse -> keep fighting/chopping
pack full, or carrying > MAX_CARRY_GOLD
  -> sell to each vendor on the route
  -> bank gold down to WALLET
  -> stash items nobody buys
  -> restock within budget
  -> print ledger
```

| Piece | Where |
|---|---|
| `EconomySkill`: loot, sell, bank surplus, stash junk, budget, ledger | `scripts/js/lib/economy.js` |
| `EconomyPolicy.planSale / keepAmount / createLedger`: pure, tested | same file |
| Purchases capped by budget and recorded in the ledger | `lib/survival.js` `buyFrom` |
| Probable kill recorded (serial + spot) when a foe leaves view while we were alive and not fleeing | `lib/combat.js` |
| `Vendor.sell`, `vendor_sell` event, `World.items` (ground items, corpse attribution) | `src/js/ClientBindings.cpp`, `Client::SendVendorSell` |
| Wired into both bots | `graveyard.js`, `lumberjack.js` |

## 2. The rules it follows

* **No global market data.** What a vendor buys, and at what price, is learned
  only from that vendor's own sell list: the 0x9E window a player sees after
  saying "sell". No price is hardcoded. An item becomes "unsellable" only after
  **every vendor on the bot's route has been asked** and none listed it. A
  single sighting of a buyer overrides past refusals.
* **Loot only your own kill.** The client now tracks which mobile each corpse
  belongs to (`corpseOf`, from the 0xAF death message). An unattributed corpse
  is taken only if it is the *only* one at the spot. With two unattributed
  corpses the bot touches neither, because one may belong to another player.
  A kill older than 90 s is not walked back to.
* **Never sell what the bot needs.**
  * Kept outright: gold, the bot's tool (`AXE`), its weapons (`WEAPONS`),
    bandages, and anything in `KEEP`.
  * Consumables with a restock target: 2× the target is kept and only the
    surplus is sold.
  * Anything a vendor offers 0 gold for is skipped; giving loot away is not
    selling.
* **The server is the authority on sales.** `SendVendorSell` drops rows the
  vendor did not list and clamps quantities to what it listed. A script
  therefore cannot sell something the server never asked for.
* **Budget:**
  * `GOLD_RESERVE` is never spent on discretionary purchases. Essentials
    (bandages, food) may dip into it, because that is what the reserve is for.
  * No purchase can exceed the gold actually carried.
* **Death costs:** full loot loss means carried gold is at risk. Above
  `MAX_CARRY_GOLD` (800) the bot banks its gold, keeping only `WALLET` (150).
* **Ledger:** every gold change is recorded by kind: sale, loot, buy, training.
  Bank transfers are not income. The bank trip prints sales, loot, spending,
  net and net per hour.

## 3. Per bot

| | Sells to (atlas PLACE) | Notes |
|---|---|---|
| GraveyardHunter | Britain blacksmith 1418,1547 · armorer 1481,1584 · tanner 1431,1612 · provisioner 1469,1668 | Loots every kill, then sells |
| Lumberjack | Trinsic provisioner 1852,2831 | Logs are **offered first**; only unsold logs are banked. Whether the provisioner buys logs is the server's answer. The atlas lists no Trinsic carpenter |

## 4. UNKNOWN / not done

* **Revolution vendor buy prices and buy lists:** UNKNOWN. Nothing here assumes
  them; the shard's own sell lists decide.
* **Looting monster corpses:** assumed not to be a crime (Sphere treats
  monster corpses as free to loot). Not checked against Revolution evidence.
* **Items nobody buys go into the bank.** The client has no drop-to-ground
  action yet. Over a long run the bank box will fill up. The fix is a real
  drop action.
* **Market knowledge is per session.** The buy side (vendor stock) persists via
  the M4.1 record; the sell side does not yet.
* **Player vendors and trading with other bots:** not built. That is the next
  layer of "their own economy".

## 5. Verified

* `tests/js_bot_smoke.js`: 67 checks, 0 failures. Covers:
  * the sale plan: loot is sold; food only above 2× target; the tool never; no
    0-gold sales;
  * the ledger;
  * market learning;
  * corpse attribution: own kill only, ambiguity refused, stale kills skipped;
  * the budget;
  * that no mixin method shadows another.
* `ClientBindings.cpp` and `Client.cpp`: MinGW syntax check, no new warnings.
* **Nothing was run against Sphere.**

Live proof: 30 minutes of `graveyard.js`. Expect `[econ] looted`,
`[econ] selling`, and a positive net on the ledger line.

## 6. Ideas adopted from other UO bot projects

On the owner's request I surveyed existing projects. **Ideas only, no code:**
RazorEnhanced, ClassicAssist and UOTerm are GPL/AGPL, and Klein187/uo-offline
is inspiration-only under CLAUDE.md. uo-offline also cheats by our rules: it
sets `bot.Hits` while resting and deposits gold server-side. Upstream
xrip/uo-client has nothing newer than our fork; it only supports buying.

| Adopted | From | Where |
|---|---|---|
| Restock at a **low mark** (default: ¼ of target, or `low`), not at zero, with a 10-minute errand cooldown | uo-offline `BotSupplies` | `survival.js consumableIsLow` |
| **Minimum sell price** per item (`MIN_PRICE`) | ClassicAssist / RazorEnhanced sell agents | `economy.js planSale` |
| One sale capped at **255 units** | our `sphere.ini` `VendorMaxSell=255` | `planSale` |
| **Looted set** (a corpse is never opened twice), **no looting with a hostile within 2 tiles** (the kill is kept for later; corpses last 7 minutes per `sphere.ini`), **human-bodied corpses never looted** | uo-offline, UOTerm loot playbook, ClassicAssist autoloot | `economy.js lootKill` |
| **Gang pressure:** each extra hostile within 2 tiles raises the flee floor by 0.1, capped at 0.6 | uo-offline `CheckRetreat` | `combat.js fleeFloor` |

Deferred, worth doing next:
* experience-scaled retreat thresholds, so novices flee earlier;
* a "hunted" escalation after a second flee from the same foe;
* a per-bot danger map built only from what the bot itself witnessed, with
  heat that decays;
* a cooldown on resource spots the server reported as empty;
* persona `active_hours` and `risk_tolerance` feeding a play schedule;
* a bandage lock that waits for the "finish applying" journal line (Revolution
  timing UNKNOWN);
* a rate-limited action queue with retry on "you must wait".
