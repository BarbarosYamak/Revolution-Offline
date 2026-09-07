---
name: return-home-goal
description: RETURN_HOME/NeedHome design - why the stranded test is distance-from-home, not "unguarded here", and the Papua evidence behind it
metadata:
  type: project
---

`GoalKind::ReturnHome` / `NeedKind::NeedHome` (added 2026-09-07, commit
a45eecb on branch worktree-agent-abd50150f1e83c10f) fire on exactly one fact:
the character is outside its home region AND further from it than
`world_atlas::kMaxServiceTripTiles` (1200), mirrored in the life layer as
`life::kStrandedFromHomeTiles`.

**Why:** Alder and Kharazar logged in at the Papua bank (5674,3134). Papua is
Lost Lands; every service lookup from there answers with a Papua provider, so
no goal they could score ever moved them. The brief suggested an alternative
test -- "no guarded place within the service trip budget" -- and that test is
WRONG on this world data: `runner_detail::NearestGuardedPlace` returns
`papua_bank` at 5 tiles, because Papua's shop INTERIORS carry
REGION_FLAG_GUARDED in the shard's own scripts
(`runtime/scripts/maps/map0/map0_rooms.scp:1806-1812`, ROOMDEF
`a_olde_loan_savings_1`) even though the town region `a_papua_4` carries no
flags at all. A "guarded here" gate flaps room by room in any Lost Lands town.

**How to apply:** when a rule wants "is this place safe", check whether the
atlas's guarded bit is set on a shop interior before treating the town as
unguarded, and prefer a distance-from-home test for stranding. Real distances
that bound the threshold: Britain->Trinsic 1,110; Vesper->Minoc 440;
Papua->britain_bank(1650,1608) 4,024.

Execution detail worth keeping: the handler uses `TravelToPoint` at the home
bank, NOT `TravelToService(Banker, homeCity)` -- see
[[travel-to-service-prefers-sightings]].

Related: [[one-resolver-for-need-and-handler]].
