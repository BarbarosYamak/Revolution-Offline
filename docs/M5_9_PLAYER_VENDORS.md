# M5.9: Player vendors

Date: 2026-09-30. **STATUS: DECISIONS AND CLIENT SUPPORT BUILT AND TESTED.
Owners are BLOCKED on the server; buyers only notice and remember vendors.**

Revolution had player vendors in houses and a searchable cooperative over
them, with a trade/vendor tax of about 10% around 2010
(REVOLUTION_RULESET_PROFILE.md:100-101).

## Built

* **Client:**
  * the **0x9A text prompt**: recognised, and answered with type 1 and the
    text. That is how an owner sets a price;
  * `TitledMobilesNear`, which lists nearby mobiles whose paperdoll title has
    been read, with their positions.
* **Decisions** (`include/uo/player_vendor.h`):
  * `IsPlayerVendorTitle`: "… the vendor" or "…'s vendor". NPC shopkeepers
    titled by their trade never match.
  * `AskingPrice`, using only what this character has seen:
    * undercut an NPC shop by 10%;
    * otherwise ask what players pay;
    * otherwise ask twice what an NPC gives;
    * otherwise 1.5× its own input cost;
    * otherwise don't list at all.

    After the ~10% tax the owner always nets more than an NPC would have paid
    and more than the inputs cost.
  * `ShouldList`: surplus only, never what the owner itself needs.
  * `WorthBuying`: it must beat the NPC shop (if one sells the item), be
    worth that much to the buyer, and fit the budget.
* **Runner:** every 30 seconds a bot checks the titled mobiles around it and
  remembers each player vendor it walks past (a known place,
  "player_vendor"). That is market knowledge learned by seeing, never from a
  global list.

## Blocked on the server

The player-vendor system was **not ported** to this runtime, and no NPC sells
a vendor deed (vendor snapshot :42-48, :1256-1257, :1300). Until it exists:
* no bot can own a vendor;
* how an owned vendor sells on this tree (the "buy" shop flow, dropping items
  on it, the price prompt) is UNKNOWN and unverified.

The price prompt and the pricing are ready for when it is.

## Owners and buyers (built 2026-09-30, UNVERIFIED live)

`src/life/runner/Vendors.cpp`, goal `RUN_VENDOR` (weight 125, in the Work
family).

**Owner:** a house owner (M5.8) with goods only players buy:
1. takes coin from the bank and buys a **vendor deed**, matched by name. The
   innkeeper and tavernkeeper carried `i_deed_vendor` in the shard's vendor
   table; those lines are commented out today;
2. uses the deed in its house, and remembers the vendor that appears (by its
   paperdoll title) as `my_vendor`;
3. drops one surplus stack at a time on the vendor and answers the **price
   prompt** with `AskingPrice` × qty, up to 5 listings per visit;
4. finishes by saying "vendor collect".

If no prompt comes, it stops for 4 hours. If the deed places nothing, or no
innkeeper sells one, it blocks the need.

**Buyer:** a character short of something (`market::Shortfall`), within 120
tiles of a player vendor it remembers, and not there in the last 2 hours:
1. walks there;
2. finds the vendor by its title;
3. says "buy";
4. buys **one** shortfall item that passes `WorthBuying` (cheaper than the NPC
   shop, worth it to the buyer, within a quarter of its gold);
5. records the price as a player trade in its own price book.

## Next, once the server has it

Uncomment `SELL=i_deed_vendor` on the innkeeper/tavernkeeper templates.
Then a live run shows whether this tree's vendor behaves as stock Sphere
does: a drop raises a price prompt, "buy" opens its shop, and "vendor
collect" hands over the takings.

## Verified

* `player_vendor`: 14 checks. They cover:
  * titles;
  * each pricing rule, including the tax floors;
  * listing;
  * buying.
* `life_world_harness`: 429 checks, 0 failures. With real packets:
  * a real paperdoll "Kemal the vendor" is remembered once, and "Aldo the
    provisioner" is not;
  * a real 0x9A prompt is recognised and answered with the right id, type 1
    and the text NUL-terminated.
* Full ctest under Wine: 61/62; the known missing TSV is the one failure.
