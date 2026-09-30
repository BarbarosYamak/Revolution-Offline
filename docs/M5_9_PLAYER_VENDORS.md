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

## Next, once the server has it

1. **Owner:** it places the deed in its house (M5.8), drops surplus on the
   vendor, and answers each price prompt with `AskingPrice`.
2. **Buyer:** it walks to a remembered player vendor, says "buy", and uses the
   existing NPC shop flow with `WorthBuying` as the filter.

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
