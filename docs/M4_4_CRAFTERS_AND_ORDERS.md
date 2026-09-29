# M4.4: Crafters that train and take orders; bot memory and persona

Date: 2026-09-29. **STATUS: BUILT AND UNIT-TESTED, NOT YET PROVEN LIVE.**

## 1. Crafters

The owner reported that crafters were "not training or getting orders", and
that an order can be one piece or a whole set. That was accurate: **no
autonomous crafter existed.** Crafting had only ever been driven by one-shot
M3.7/M3.8 scenarios.

New: **`scripts/js/crafter.js`**, a Britain tinker, built on
**`scripts/js/lib/crafting.js`** (`CraftSkill`, `CustomerSkill`, and the pure
`OrderPolicy`).

**Why a tinker first:** Tinkering is the only craft whose full menu flow is
proven live on this shard (`m38_tinker_craft.txt`, `m391_craft_oracle.txt`).
The tool opens the 0x7C menu on a double-click, no crafting station is needed,
and iron ingots may be bought from an NPC under our vendor policy
(`PLAYER_MARKET_GOOD`, allowed). A smith uses the same code. It needs only
`CRAFT_TOOL`, `CRAFT_SKILL`, `MATERIAL` and a `craftTarget()` hook once its
hammer → target flow is proven.

| Behaviour | What it does |
|---|---|
| **Train** | Reads the **live craft menu** by category, which is the crafter's knowledge of what it can make right now. It picks something the menu offers, preferring items a vendor is known to buy (economy market memory), and never its own tools or material. It crafts, then reads the skill back from the server. The menu is re-read every 20 minutes or after 5.0 skill points of gain. |
| **Materials** | Buys ingots from the Britain blacksmith, within budget, once below the low mark. |
| **Tools** | Makes a spare set of tinker tools while one still works (Britain's tinker is a guildmaster with no shop). |
| **Sell** | Sells training output through the economy layer. Tools, ingots and **anything promised to a paid order** are never sold. |
| **Orders** | Spoken by any player near the crafter, human or bot. See below. |

### Orders: one piece, a quantity, or a whole set

```text
"<crafter> order pickaxe"              one piece
"<crafter> order 10 pickaxes"          a quantity (capped at 100)
"<crafter> order ringmail set"         a set, expanded to its pieces
"<crafter> status"  /  "<crafter> cancel"
```

1. **Check against the live menu.** Every piece must be offered by the live
   menu right now (the M3.9.1 menu oracle rule). Otherwise the crafter replies
   "I cannot make X yet" and records X as a training goal. It never attempts
   an item the menu doesn't list.
2. **Quote.** The price is material cost the crafter has observed plus 50%,
   with a 10-gold floor per piece. This is the crafter's own policy;
   **Revolution crafter prices are UNKNOWN.** A crafter holds at most 3 open
   orders, and an unpaid quote expires after 3 minutes.
3. **Pay up front by secure trade.** The customer opens a trade and offers the
   price in gold. The crafter accepts only if the gold in the customer's side
   of the window covers the price. Too little gold cancels the trade and the
   order stays unpaid. The payment is recorded in the ledger.
4. **Craft** one piece at a time, the oldest paid order first. Paid orders come
   before training.
5. **Deliver by secure trade** when the customer is within 3 tiles. The crafter
   says the order is ready, opens the trade and offers every piece.

`CustomerSkill.placeOrder / collectOrder` is the other side, available to any
bot. It opens the trade with one coin (`Trade.start` drags a single unit) and
then adds exactly the rest of the price, so no change is ever needed. It
refuses a quote over its budget.

**Sets** (`OrderPolicy.SETS`): ringmail, chainmail, platemail, and a "tinker
tools" starter kit. The piece names are the generic Sphere/UO labels;
**Revolution's own menu labels are UNVERIFIED.** That is safe because every
piece is checked against the live menu before an order is accepted, so a
wrong label shows up as "cannot make", not as a failed craft.

## 2. Bot memory and persona (`lib/memory.js`)

These are the deferred items from the M4.3 research, as ideas only:

* **Danger map.**
  * Heat is added where this character died (+5) or fled (+1.5), and halves
    every 45 minutes.
  * A place above 3.0 heat is picked a quarter as often, never banned.
  * It is built only from what the bot itself witnessed.
  * Used for graveyard waypoints.
* **Hunted.** A second flee from the same foe within a minute means the bot
  stays away three times longer and heals to full before returning.
* **Empty spots.** A tree the server reported as exhausted is skipped for 20
  minutes, not only for the current pass.
* **Persona.**
  * `riskTolerance` (0 = cautious, 1 = reckless) and `activeHours` in local
    time; windows may wrap midnight.
  * Outside its hours a bot walks to the bank and logs out through the new
    `Player.logout()`, which saves the M4.1 life and records whether the spot
    was safe.
* **Experience.** The flee floor rises when combat skill is low, because a
  novice misses most swings and its fights run long. uo-offline scales it the
  other way (veterans retreat earlier); ours is DERIVED from fight length, and
  the persona can override either way. The floor stays between 15% and 60%.
* **Bandage lock.** No second bandage is started mid-heal. The lock is a fixed
  5 s because Revolution's heal time and "finished" message are UNKNOWN.

## 3. Client bindings added

* `Player.skill(index)`, `Player.skillSum`, `Player.requestSkills()`
* `Player.logout()`
* `Trade.start / offer / accept / cancel / state`, over the M3 secure-trade
  state machine

## 4. Verified

* `tests/js_bot_smoke.js`: **151 checks, 0 failures.** Covers:
  * order parsing: piece, quantity, set, unknown set, not addressed to us;
  * the menu-oracle refusal;
  * quotes;
  * the order lifecycle: cancel, expiry, maximum open orders;
  * payment accepted when the gold covers the price, refused when it doesn't;
  * reservation of paid items;
  * training choice;
  * `craftOne` choosing the category, then the item, by name from a stubbed
    menu, and refusing an item the menu lacks;
  * danger decay and weighting, hunted, empty spots, schedules (including a
    window that wraps midnight), personal flee floors, the bandage lock;
  * that no mixin method shadows another.
* The test now fails if a promise never settles. A hang used to exit 0 and
  look like a pass.
* `ClientBindings.cpp`: MinGW syntax check, clean.
* **Nothing was run against Sphere.**

## 5. Live proof, next

1. Run `crafter.js` on a character with tinker tools and ingots. Expect
   `[craft] live menu offers …`, then crafts, then `[craft] skill a -> b`.
2. From a second account, say `"<name> order 3 scissors"`, trade the quoted
   gold, and wait for "ready". The trade window should hand over 3 scissors.
3. Record the real Revolution tinker menu labels, and correct `SETS` from them.
