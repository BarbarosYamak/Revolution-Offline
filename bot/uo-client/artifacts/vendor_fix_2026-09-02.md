# Vendor/trade cluster (wave-2 triage finding 5) — root causes and fixes

Date 2026-09-02. Shard DOWN: everything below is unit-test + log/source
evidence. No live re-run was possible; see LIMITS at the end.

Source logs: `bot/uo-client/run_gates/g_*.console.txt` (launch 18:08:52,
killed 18:14:26). Read with `tools/log_slice.py`, never whole-file.

---

## Sub-case A — REFUSE_NO_KNOWN_BUYER (Dorvar, Zarthal, Titus; Ithion same shape)

### What actually fired

    g_Dorvar.console.txt:740   goal_failed=BUY_SUPPLIES reason="REFUSE_NO_KNOWN_BUYER" item=i_fish_big_1
    g_Zarthal.console.txt:488  goal_failed=BUY_SUPPLIES reason="REFUSE_NO_KNOWN_BUYER" item=i_ingot_iron
    g_Titus.console.txt:636    goal_failed=BUY_SUPPLIES reason="REFUSE_NO_KNOWN_BUYER" item=i_log

Note the goal: **BUY_SUPPLIES**, not a sell goal. The refusal enum was the
sell side's (`NoKnownBuyer`) being reused on the buy side, which is what made
the triage read it as a lost buyer lookup.

### Root cause — YES, and the refusal itself was CORRECT

`Runner.cpp` `SupplierTradeFor()` maps a craft input to the shopkeeper trade
that sells it. It knows reagents, `i_scroll_blank`, `i_bottle_empty`,
`i_feather`, `i_kindling`. It returns null for fish, ingots and logs — which
is right: those are materials, and materials do not come from NPCs.

The defect is that "no NPC sells it" was treated as the end of the errand.
Checked against the profession catalogue (`src/life/Professions.cpp`):

| char | life | missing input | catalogue says |
|---|---|---|---|
| Dorvar | fisher | `i_fish_big_1` | fisher **produces** it (Professions.cpp:621) |
| Zarthal | miner_smith | `i_ingot_iron` | miner_smith **produces** it (:361) |
| Titus | archer | `i_log` | archer consumes (:843), lumberjack_swordsman produces (:270) |

So two of the three were shopping for their own output, and the third was
shopping for the one live producer→consumer edge in the catalogue — a player
rendezvous. None was an NPC errand.

### Fix

New pure decision, `market::RouteForInput` (include/uo/market.h,
src/economy/Market.cpp):

    NpcVendor      a shopkeeper trade is known    (unchanged precedence)
    SelfProduce    this life's own `produces`     -> hand off to Mine/Smelt/Fish/GatherLogs/Craft
    PlayerMarket   another profession produces it -> hand off to TradeWithPlayer
    NoKnownSource  neither                        -> refuse, now as REFUSE_NO_KNOWN_SUPPLIER

`npcTradeKnown` keeps precedence deliberately, so every currently-working
errand (scribe blank scrolls, mage reagents, alchemist bottles, provisioner
kindling/feathers) is byte-for-byte unchanged. The new routes only occupy the
branch that previously failed.

`DoBuySupplies` now consults it before failing, and the buy-side refusal has
its own name (`faucet::Refusal::NoKnownSupplier` ->
`REFUSE_NO_KNOWN_SUPPLIER`) so it can never again be read as a broken buyer
lookup.

The material-never-to-NPC rule is untouched and is asserted in the new test:
`HasNpcBuyer("i_ingot_iron")` and `HasNpcBuyer("i_log")` must both stay false.

Handoff is advisory by design — `Runner::HandOff` (Runner.cpp:2536) cools the
FROM goal and lets the planner re-score. It removes the blocker; it does not
force the successor.

### Test
`tests/m7_market.cpp` — `TestSupplyRouteForAMissingInput`,
`TestBuySideRefusalIsItsOwnReason`.

---

## Sub-case B — REFUSE_VENDOR_UNREACHABLE after 4 trips (Elara)

### Not navigation. Vendor IDENTIFICATION.

    :105  18:08:57  [0x88] paperdoll 0x000027D1: "Bret, the alchemist"
                    [world] Bret ... is a alchemist at (606,2184)
    :547  18:10:46  supplies: looking for a 'alchemist' ... (trip 1, 0 place(s) already tried)
    :565  18:10:48  travel mobile 0x000027D1 ARRIVED at (605,2181,0)
    :584/:604       trip 2, trip 3 -- travel legs=0 plans=0, i.e. she never moved
    :639  18:11:00  goal_failed=BUY_SUPPLIES REFUSE_VENDOR_UNREACHABLE no 'alchemist' found after 4 trips

Every trip ARRIVED. Travel was steering at Bret by serial the whole time (via
`knowledge_.RecentService` in `TravelToServiceSkipping`, ClientTravel.cpp:588,
which is also why "0 place(s) already tried" never grew). Only ONE paperdoll
was requested in the whole window (`:582`, an unrelated weaponsmith) — because
Bret's title had been cached since 18:08:57 and `ActionScanMobiles` skips
mobiles it already knows.

So the client knew the alchemist's name, trade and position, stood 3 tiles from
him, and its own shop lookup returned 0.

The gate: `Client::NearestShopkeeperWithTrade` (Client.cpp) required
`MobileInLineOfSight`. Elara (605,2181) to Bret (606,2184) traces through
(605,2182)/(605,2183) — the shop counter — so LOS said no and the candidate was
dropped entirely. Sight was being used as an IDENTITY test.

### Fix
Two-pass: prefer a shopkeeper in line of sight; if none is visible, still
return the nearest one whose trade is known, and let the caller's existing
approach step walk round the counter. Every caller already walks up before
speaking, and Sphere still enforces real reach on the wire
(`CChar::CanTouch`, CCharStatus.cpp:1414-1430), so nothing can buy through a
wall.

Also added an optional `skip` list to that lookup (used by sub-case D).

### Test
`tests/trade_verify.cpp` — `TestOccludedShopkeeperIsStillFound`, replaying
Elara's exact coordinates and serials through the real packet dispatcher.

LIMIT stated in the test: the harness Client loads no MULs, so
`MobileInLineOfSight` is false for everything. That makes the occluded case
exactly reproducible, but the "visible beats occluded" preference is NOT
proven by this test — only the code path and the distance ordering are.

---

## Sub-case C — "You can't reach the Vendor" (Aurelius)

### Root cause — YES, a provable off-by-one against the server's own rule

    g_Aurelius.console.txt:411  18:13:53  Kenton, the mage is a mage at (1588,1655)
                        :412  18:13:53  [0x20] player @(1591,1657,10)
                        :423  18:13:55  VENDOR open vendor=0x000010D1 -- succeeded, 17 items
                        :468  18:14:04  VENDOR buy item=0x40010855 qty=1
                        :469  18:14:04  System: You can't reach the Vendor   (1ms later)
                        :482  18:14:15  identical retry, identical refusal

Chebyshev distance (1591,1657)->(1588,1655) = max(3,2) = **3**.

Server source, verified:
* `receive.cpp:752-756` — the buy packet's first act is
  `if (buyer->CanTouch(vendor) == false)` -> DEFMSG_NPC_VENDOR_CANTREACH.
* `CCharStatus.cpp:1423` — `if (( iDist > 2 ) && fCanTouch) fCanTouch = false;`
  `iDist` is `CPointMap::GetDist`, the same Chebyshev metric as the client's
  `TileDist` (Runner.cpp:1076).

The client's `kVendorReach` was **3** — precisely the distance at which the
answer is always no. Opening the shop worked because speech range is larger;
the buy could never work. It was not a wall and not a stale position.

### Fix
`sphere::kTouchDist = 2` / `sphere::CanTouchAtDist()` added to
`include/uo/sphere_rules.h` (the header that exists to mirror server rules),
with the citation. `kVendorReach` now takes its value from there instead of
restating a number. Every reach check that used it — `BuyScrollFrom`'s walk-up
and `DoBuySupplies`'s re-measure-before-each-purchase — tightens automatically,
so a bot now walks the last tile instead of firing a doomed packet.

### Test
`tests/sphere_regression.cpp` — `TestTouchReach`, including Aurelius's actual
coordinates and an explicit check that the metric is Chebyshev (his dx=3,dy=2
is 3 Chebyshev / 5 Manhattan; a client measuring Manhattan gets the boundary
wrong in both directions).

---

## Sub-case D — "this 'mage' does not stock a scroll" (Thalia)

    g_Thalia.console.txt:523  goal_failed=FILL_SPELLBOOK reason="this 'mage' does not stock a scroll (4 already known)"

### The vendor table (grep -i, case-insensitive per the standing rule)

`runtime/scripts/templates/tm_vend.scp:717-742`, `[TEMPLATE VENDOR_S_MAGE_SHOP]`:

    SELL=random_first_circle,{4 24}
    SELL=random_second_circle,{4 24}
    SELL=random_third_circle,{4 24}
    SELL=random_fourth_circle,{4 24}
    ... reagents, blank scrolls, pen and ink, rune, spellbook

Four **random** scroll slots, not a fixed catalogue. `{4 24}` is the stock
band, not a price (standing memory rule).

Confirmed live on the wire in Aurelius's own vendor window
(`g_Aurelius.console.txt:430-433`): the mage offered exactly four scroll SKUs
— Feeblemind (1st circle), Strength (2nd), Telekinisis (2nd), Fire Field (4th).

So Thalia's observation was CORRECT: that shopkeeper's four current scrolls
were all already in her book. The defect is the conclusion. "This mage's
current roll has nothing new" is a fact about one NPC, not about mages, and
the code failed the whole goal and cooled it down.

### Fix
`BuyScrollFrom` now, on "nothing the book lacks": records the exhausted
shopkeeper serial in a new `spellbookSkipSellers_`, spends one of its existing
trip allowance, and travels to a different shop of the same service — passing
the skip list to both `NearestShopkeeperWithTrade` and
`TravelToServiceSkipping` so the sighting cache cannot walk it straight back
to the same shelf. Only when the trip budget is spent does it fail as before.
The trip budget is the brake, so this cannot become a tour of every mage on
the shard.

Related: the same skip list was NOT previously passed to
`TravelToServiceSkipping`'s `skipSerials` in the "no keeper here" branch,
which is the same shape of bug; that call now passes it too.

### Test
Skip-list behaviour of the lookup is covered by the last check in
`TestOccludedShopkeeperIsStillFound` (tests/trade_verify.cpp). The
travel-on-exhausted-shelf sequence itself is Runner-level and needs a live
session — NOT unit-proven.

---

## Gates

    build   cmake --build build-m1   OK, uo_client.exe relinked 2026-09-02 00:15:34
    ctest   41/41 passed             (was 41/41; no test weakened, removed or rewritten)

New checks added inside existing suites:
* m7_market            160 -> 177 checks, 0 failures
* sphere_regression     88 ->  97 checks, 0 failures
* trade_verify          25 ->  33 checks, 0 failures

## LIMITS — what is NOT proven

1. The shard is down. Nothing here is verified runtime behaviour on the wire
   for the FIXED code. The refusals themselves are verified runtime evidence
   from the 18:08-18:14 wave; the fixes are verified against source (Sphere
   and ours) and unit tests only.
2. `kVendorReach = 2` is derived from Source-X source, not from a live
   round-trip. A live buy from exactly 2 tiles has not been observed since
   the change.
3. The SelfProduce/PlayerMarket handoffs remove the blocker; whether the
   planner then actually scores Fish/Smelt/TradeWithPlayer highest for those
   characters is a planner question and is unobserved.
4. "Visible shopkeeper beats occluded shopkeeper" is implemented but not
   unit-provable without MULs.
5. Sub-case D's re-travel to a second mage shop is unobserved.
