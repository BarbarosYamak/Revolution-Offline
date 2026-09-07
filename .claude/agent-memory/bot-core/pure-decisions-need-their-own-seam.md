---
name: pure-decisions-need-their-own-seam
description: A Client-coupled class (BankErrand, VendorErrand) cannot be ctest-driven through its success path without real MUL/navgrid data; extract the decision as a free function and test that instead
metadata:
  type: feedback
---

`Client::NearestMobileWithTrade` (and `MobileInLineOfSight` underneath it)
requires a populated `Client::world_` (`uo::world::World`) to ever return a
match. No test harness in this suite sets it — grep for `world_ =` against
that exact member finds nothing outside `Navigation.cpp`/`PathPlanner.cpp`'s
own unrelated members of the same name. So any ctest that drives an errand
through `NearestMobileWithTrade`'s SUCCESS path (found a banker/vendor/
trainer) needs real MUL/navgrid fixtures (`m9_service_selection.cpp`'s
Section B pattern) or it cannot happen at all offline.

**Why:** this blocked writing a direct ctest for BankErrand's "the only
banker on the skip list gets retried after a cooldown" rule, because
reaching that branch requires `NearestMobileWithTrade` to succeed at least
once. Confirmed live in production logs that it DOES succeed there
(`fleet122c30_20260907`: "found a banker" and "the box is open" both occur)
— so `world_` is populated in a real run, just not in any offline harness in
this repo. Do not conclude from the offline-null observation that
`MobileInLineOfSight` is dead code in production; it is real, live evidence
that says otherwise.

**How to apply:** when a decision needs a fact only the un-mockable Client
can supply (a mobile exists, ignoring the skip filter), pull the decision
itself into a small free function taking primitives (bools, a clock, a
small state struct) and test THAT exhaustively — same pattern as
`uo/activities/buy.h`'s `Decide()`. `DecideOnlyBankerRetry`
(`include/uo/interaction/bank_errand.h`) is the worked example: BankErrand's
`Step::Find` still calls the real `Client::NearestMobileWithTrade`, but the
cooldown/retry/give-up shape around that one boolean is provable without a
Client at all.

Related: [[los-is-reach-not-identity]], [[offline-life-harness]],
[[a-scan-cap-can-hide-the-only-banker]].
