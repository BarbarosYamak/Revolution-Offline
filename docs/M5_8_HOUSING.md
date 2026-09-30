# M5.8: Housing

Date: 2026-09-30. **STATUS: BUILT AND TESTED OFFLINE. BLOCKED LIVE until the
server sells house deeds.**

## What a bot does (`include/uo/housing.h`, `src/life/runner/Housing.cpp`)

1. **Wants a house** when:
   * its total wealth (pack plus bank) reaches 30,000 gold (a tunable;
     Revolution's prices are UNKNOWN), or
   * it already carries a deed (0x14F0).

   It never wants a second house: `MaxHousesAccount=1` in `sphere.ini`.
   The goal is `BUY_HOUSE`, weight 120, in the Upkeep family.
2. **Buys a deed:** it takes coin out of the bank with the existing
   coin-for-purchase helper, then asks an **architect** (the atlas files
   architects under the Carpenter service). It always keeps 3,000 gold back.
3. **Picks sites:** rings 40–100 tiles out from its home town, 16 bearings
   each. The starting bearing comes from its identity, so a fleet doesn't
   queue for the same field, and the order is the same every session.
4. **Places:** it walks to the site and double-clicks the deed. The server
   answers with a **0x99 placement cursor** (now handled by the client),
   which it answers with the site and the multi model.
5. **Proof:** a **multi** (0x1A graphic with the 0x4000 flag, now tracked)
   appearing within 12 tiles is the only evidence the house stands. The house
   is then saved in the character's memory as a known place, "house".
   * If no multi appears within 8 seconds, the spot counts as refused and it
     tries the next one.
   * After 12 refusals it stops for the session.

`AutoHouseKeys=1` means the key arrives from the server; the bot never makes
one.

## Why it is blocked live

* In the repo's vendor snapshot
  (`artifacts/tm_vend_pre_restore_2026-09-02.scp:135-157`), every house-deed
  SELL line is commented out. The live tree may since have been restored.
* If no architect lists a deed, the bot logs "no deed bought" and blocks the
  need for 12 hours. It never invents a deed.
* **To enable it:** uncomment the deed SELL lines in the vendor template on
  the server.

## House storage (built 2026-09-30, UNVERIFIED live)

Goal `HOUSE_STORE` (weight 200, in the Upkeep family).
* A house owner without a chest buys a **bag** from a provisioner. Boxes and
  chests are carpenter-made on this shard; the vendor table only buys them.
* It sets the bag down in the house, says **"I wish to secure this"** (the
  stock Sphere house speech) and answers the cursor with the bag.
* The bag counts as the house chest only if it still lies in the house 3
  seconds later.
* Afterwards, a character carrying goods only players buy, with a pack at 50%
  weight or more, stores them in the chest when it is home.
* If the chest won't open or the bag is refused, it rests for 6 hours.

## UNKNOWN / not done

* Revolution's house rules: sizes, prices, where building is allowed, decay,
  lockdowns, friends and co-owners. The rules pages `/ev_cesitleri` and
  `/ev_kurallari` were never captured.
* **Using the house:** a secured bag for storage and a player vendor are now
  built (see below and M5.9). Both use stock Sphere house behaviour and are
  unverified on this tree.
* Which deed the architect sells first when it lists several; the errand's
  price cap bounds it.

## Verified

* `housing`: 16 checks. They cover:
  * when a house is wanted, including one per account;
  * the reserve;
  * deterministic, per-character, out-of-town sites;
  * every step, including refusals and giving up.
* `life_world_harness`: 423 checks, 0 failures. With real packets:
  * no deed and little gold means no need; a deed means the need appears;
  * the deed is double-clicked;
  * a real 0x99 cursor is recognised and answered with the site and multi
    model;
  * a real multi 0x1A proves the house, which is remembered;
  * no second house is wanted.
* Full ctest under Wine: 60/61; the known missing TSV is the one failure.
