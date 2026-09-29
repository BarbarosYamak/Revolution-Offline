---
name: feathers-are-carved
description: Feather source ruling 2026-09-04 — no NPC sells feathers; carve bird/chicken 25, eagle 36, harpy 50; bowyer NPC only BUYS; archer needs a kill+carve chain
metadata:
  type: project
---

Feathers come only from carving: c_bird 25, c_chicken 25, c_eagle 36,
c_harpy 50 (+ stone harpy). `VENDOR_B_BOWYER` BUYs feathers {24 72}; its
SELL line is commented out. Policy row `i_feather WORLD_GATHERED buy=0` is
correct. Verified in `runtime/scripts/npcs/c_monster_classic.scp` and
`templates/tm_vend.scp`; owner's list matched exactly.

**Why:** crafter audit 2026-09-04 called i_feather "no source" — that was a
client gap (no carve chain), not a shard gap. Spawned 2026-09-04: birds 87,
chickens 56, eagles 163, harpies 141.

Owner 2026-09-04: birds inside guarded towns are fair game — killing and
carving an animal is not a crime (no NOTO_GOOD path). World save: 17 c_bird
+ 9 c_chicken within r=250 of Brit bank. The "no gathering in guarded
zones" rule is for lumberjacks/miners; bird carving is exempt.

**How to apply:** archer gathers feathers by killing birds/chickens near
home (inside town is fine) and carving (same shape as [[wool-lamb-carve]]); fighters carve
harpies/eagles as loot and sell feathers player-first to archers. Never
route i_feather to a BUY_SUPPLIES goal.
