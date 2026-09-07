---
name: a-scan-cap-can-hide-the-only-banker
description: Client::ActionScanMobiles double-clicks at most 8 unknown-titled mobiles per call; a crowded market can starve the one banker's paperdoll title for a whole errand if the caller only scans once
metadata:
  type: feedback
---

`Client::ActionScanMobiles` (Client.cpp) iterates `mobileCache_` and stops
after 8 `SendDoubleClick` calls, skipping anyone whose title is already
known. That is fine in a quiet street. In a crowded fleet run it is not: a
market spot with 14-28 mobiles nearby can go a whole scan without ever
clicking the one who is the banker, because the loop has no distance
ordering and the cap is smaller than the crowd.

**Why:** `BankErrand::Step::Find` (src/life/interaction/BankErrand.cpp) used
to scan exactly ONCE per errand, gated behind a 20s "is the last scan stale"
clock, then declare `"no banker in sight"` if the trade lookup still came up
empty. Proven live in fleet122c30_20260907 across three independent
characters (Breniel, Halar, Arvdris): each stood 3-7 tiles from the real
banker (Hyman, a fixed low serial, confirmed by `world_query.py --type
c_banker`), the scan requested 8 paperdolls, Hyman was never among them, and
the errand gave up within two seconds of that one scan. No rotation/skip-list
activity ever fired in any of the three logs — this is not the same defect
as a silent-NPC being skipped, it is the paperdoll title simply never being
asked for.

**How to apply:** any caller of `NearestMobileWithTrade`/`NearestShopkeeperWithTrade`
that gives up after one `ActionScanMobiles()` call is trusting a scan that
may not have reached the crowd's edge. Because `ActionScanMobiles` skips
titles it already knows, repeating the call (bounded, a handful of rounds)
reaches the NEXT batch each time rather than repeating the first — that is
the fix BankErrand now uses (`kMaxScanRounds`), not a longer freshness
timer. Watch for the same shape anywhere else that scans once and fails fast
in a crowd: VendorErrand, Gear.cpp's trade lookups, Train.cpp's trainer
search.

Related: [[los-is-reach-not-identity]] (a different, still-open gap in the
same family: `NearestMobileWithTrade` itself has no visible-then-blind
fallback the way `NearestShopkeeperWithTrade` does, so a banker behind a
counter with a KNOWN title can still be filtered out by
`MobileInLineOfSight` — not fixed here, out of BankErrand's scope since the
function is shared with Gear.cpp/Train.cpp/VendorErrand).
