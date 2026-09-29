---
name: return-home-goal
description: RETURN_HOME/NeedHome design - the stranded test is travel cost with moongates PLUS a guarded-town suppressor, and the Papua/Alder evidence behind both
metadata:
  type: project
---

`GoalKind::ReturnHome` / `NeedKind::NeedHome` (added 2026-09-07, commit
a45eecb; **corrected the same day**, commit 7d7aa90 on branch
worktree-agent-a97bcd0c271ccf5b6).

Current arm condition, all inside `runner_detail::ResolveHomeReturn`:
outside the home region, AND travel cost home > `kStrandedFromHomeTiles`
(1200, mirrored from `world_atlas::kMaxServiceTripTiles`), AND NOT standing
on errand ground. Under attack it stays silent; HEAL still outranks it.

**Why it needed correcting:** the first version armed on raw Chebyshev alone.
Alder (home Trinsic) took an ordinary moongate errand to Skara Brae for
UPGRADE_GEAR and RETURN_HOME -- weight 600 -- fired on "1207 tiles from Bank
of Britannia - Trinsic Branch banker" and dragged him home mid-errand
(`artifacts/alder_home_20260907/Alder.console.txt` 12:22). Owner ruling
2026-09-07, option a: stranded bots go home; ordinary players travel to other
towns on purpose.

**The two measurements that make it work** are the general lesson and live in
[[distance-is-travel-cost]]: travel cost with the public moongates allowed
(Skara->Trinsic 202, not 1,226; Papua->Britain still 2,240), and a
guarded-**TOWN** test rather than a guarded-tile one (the Papua bank ROOMDEF
`a_olde_loan_savings_1` is REGION_FLAG_GUARDED while `a_papua_4` is filed as
wilderness, so a per-tile gate flaps room by room).

**How to apply:** if this need ever misfires again, look first at which of the
two arms let it through, and fix it in the resolver -- the need and the
handler read the same `HomeReturn`, so changing one place changes both
([[one-resolver-for-need-and-handler]]).

Execution detail worth keeping: the handler uses `TravelToPoint` at the home
bank, NOT `TravelToService(Banker, homeCity)` -- see
[[travel-to-service-prefers-sightings]].
