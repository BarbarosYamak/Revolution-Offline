---
name: verdict-attribution-can-be-wrong
description: A wave verdict names character+item pairs; re-derive them from the g_*.console.txt before fixing, because one pair was fabricated and would have sent the fix into the wrong subsystem
metadata:
  type: feedback
---

Re-derive a defect's character/item attribution from the run consoles before
acting on a wave verdict.

**Why:** `artifacts/wave_2026-09-02_verdict.md` section (g) said Dorvar spun on
`REFUSE_MISSING_RECIPE` for `i_fish_cut_raw` 110 times. `g_Dorvar.console.txt`
contains zero `REFUSE_MISSING_RECIPE` lines; his five `i_fish_cut_raw` lines
are all *sales* — the wave's own positives. The real third item was
`i_ingot_iron` (Draver), a completely different failure mode. Following the
verdict would have produced a fish-carving change for a defect that did not
exist and left the smelt hand-off unfixed.

**How to apply:** one command settles it —

    for f in g_*.console.txt; do n=$(grep -c "<the refusal token>" "$f"); \
      [ "$n" -gt 0 ] && echo "$f $n $(grep -ho '<pattern>' "$f" | sort -u)"; done

Run it in `bot/uo-client/run_gates/` before writing any code. A verdict is a
summary written by another agent; the console is the artifact. Same rule as
[[absence-is-not-evidence]] pointed the other way: presence in a report is not
presence in the run.

Related: [[goals-that-spin]], [[craft-route-is-not-the-recipe]].
