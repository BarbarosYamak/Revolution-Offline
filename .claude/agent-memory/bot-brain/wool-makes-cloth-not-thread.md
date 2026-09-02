---
name: wool-makes-cloth-not-thread
description: The wool chain ends at cloth; every stock Tailoring recipe also needs 1 i_thread, which comes from cotton — so bots can weave but not sew
metadata:
  type: project
---

The sheep-to-cloth chain on this runtime produces **cloth and nothing else**.
Every stock Tailoring recipe reads `RESOURCES=<n> i_cloth,1 i_thread`
(`runtime/scripts/items/i_provisions_clothing.scp`), and thread is spun from
COTTON (6 per pile), while wool spins to YARN (3 per pile) — Source-X
`CClientTarg.cpp:2053-2086`. `IT_YARN` and `IT_THREAD` share the loom case, so
either weaves a bolt; only thread sews.

**Why:** owner ruling 2026-09-02 asked for the gather loop and said explicitly
that if the recipes need thread, record it as a blocker and make the loop
produce cloth only — do not invent a fix on Sphere's side.

**How to apply:** a bot tailor can supply itself with cloth and is still
blocked on thread for any garment. Before promising a tailor can SEW, prove a
cotton source: `i_cotton` (itemdef `items/i_vegetation.scp:3191`) and the crop
plant `i_crop_cotton` (`:169`, TDATA3=i_COTTON) exist, but no harvesting
behaviour is proven and the world save holds no runtime cotton piles worth
speaking of. Cloth/thread/yarn are all `WORLD_PROCESSED` with buy=0 in
`data/revolution_vendor_policy.tsv`, so buying the gap is not an option either.

Chain numbers, verified: 1 sheep = 1 wool; 1 wool = 3 yarn; 4 yarn = 1 bolt
(the loom takes up to four from the stack in ONE gesture); 1 bolt = 50 cloth;
wool regrows in 30 minutes and the sheep flips to body 0x00DF meanwhile.

Related: [[gather-when-the-market-declines]], [[smith-order-book]].
