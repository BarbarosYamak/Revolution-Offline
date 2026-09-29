---
name: dedup-inflates-counts
description: log_slice --dedup normalises numbers, so a TEMPLATE xN count merges every coordinate variant — confirm any loop claim with a raw grep -c of the literal line before briefing an agent
metadata:
  type: feedback
---

Never brief a defect as "×N loop" from a `log_slice.py --dedup` TEMPLATE count alone. Confirm with `grep -c` of the literal line (coords included).

**Why:** 2026-09-04 I briefed navigation-world with "forge goto stopped short ×251"; the template had merged all 542 `goto finished` lines across every target. The literal line occurred twice. Agent spent budget disproving my brief before finding the real defect (leg slack ignoring arriveRadius).

**How to apply:** dedup is for spotting families; counts for a brief come from `--grep "<exact coords>"` without `--dedup`, or `grep -c`. State counts as "template" vs "literal" when relaying.
