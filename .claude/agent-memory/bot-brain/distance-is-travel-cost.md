---
name: distance-is-travel-cost
description: Straight-line tiles are not "far" on this shard -- measure with the public moongates, and test guardedness town-wide, never per tile
metadata:
  type: project
---

Two measurement mistakes broke the same need (NeedHome / RETURN_HOME) twice
in one day, 2026-09-07. Both are general.

**Raw Chebyshev is not distance.** Skara Brae bank -> Trinsic bank is 1,226
tiles straight and 202 with one public moongate. Papua bank -> Britain bank
is 4,024 straight and still 2,240 by gate. Any "is this far?" policy scored
on the straight line will call an ordinary two-town errand a stranding.
The shard's public network is a complete graph over ten pads (every
`mg_<city>__<city>` row in `data/revolution_atlas.txt`), so a one-hop
walk-to-pad + walk-from-pad estimate is enough and needs only the atlas --
no navgrid, no route planner.

**Guardedness per TILE flaps; per TOWN it does not.** Papua's streets are
unguarded (`a_papua_4` is filed as *wilderness*) while its bank ROOMDEF
`a_olde_loan_savings_1` is REGION_FLAG_GUARDED -- so "am I on guarded
ground" answers differently in the doorway and at the counter. The durable
question is whether a guarded **Town-kind AREADEF** contains the tile. Papua
and Delucia have none; Skara/Britain/Minoc/Trinsic/Vesper/... do.
Heartwood and Sea Market are guarded towns nothing can walk or gate to, so
a town also has to be near a moongate entry to count: measured rect-to-pad
distances run Jhelom 33 ... Cove 459, then jump to Sea Market 805 and
Heartwood 2,440.

**Why:** owner ruling 2026-09-07 (option a) -- stranded bots go home,
ordinary players travel to other towns on purpose. Alder was dragged out of
a Skara Brae gear errand by a need weighted 600
(`artifacts/alder_home_20260907/Alder.console.txt` 12:22).

**How to apply:** any new "too far / wrong part of the world / is this
safe ground" policy should ask the atlas for travel cost with gates and for
a guarded *town*, not for `TileDist` and `RegionAt(x,y)->flags.guarded`.
Related: [[goals-that-spin]], [[scenario-constants-from-evidence]].
