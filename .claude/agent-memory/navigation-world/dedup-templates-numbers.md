---
name: dedup-templates-numbers
description: log_slice.py --dedup normalises numeric literals, so "TEMPLATE xN <line with coords>" counts every line of that shape, not N repeats of those coordinates -- always re-grep the literal before calling it a loop.
metadata:
  type: reference
---

`tools/log_slice.py --dedup` groups lines into templates with numbers
normalised, then prints the **first** matching line verbatim next to the group
count. So

    TEMPLATE x251 ... [action] goto finished at (2468,556,5); target (2467,556);
                      stopped short (off by 1 tile(s))

means "251 `goto finished` lines of any coordinates", not 251 trips to
(2467,556). The raw count of that exact target was **2** in the same 60-minute
log; the forge "loop" reported from the dedup output did not exist
(`artifacts/minoc_reach_2026-09-04.md`).

**How to apply:** dedup output is for *finding* candidate loops. Before
asserting a loop, confirm with `grep -c` on the literal (coordinates included)
and read ~15 lines of surrounding context. A genuine loop shows the same
literal coordinates repeating; a template count only shows a busy code path.

Related: [[two-arrival-tests-must-agree]].
