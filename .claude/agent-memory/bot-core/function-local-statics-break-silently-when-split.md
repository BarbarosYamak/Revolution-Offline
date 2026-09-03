---
name: function-local-statics-break-silently-when-split
description: Moving a function-local-static table accessor into a header gives every TU its own empty copy — the loader fills one, the reader sees another, and no compiler or test says a word
metadata:
  type: feedback
---

When a translation unit is split, any accessor holding a **function-local
static** must be DECLARED in the shared header and DEFINED in exactly one .cpp.
Never let it become a header-inline body "for convenience".

**Why:** the Runner split (2026-09-03) moved four seeded tables —
`SeededCreatureDanger()`, `SeededTaming()`, `Pastures()`, `Tamables()`. They are
filled once during bootstrap and read from a different goal family. Put in a
header as ordinary (non-`inline`) bodies, each TU gets its own static: bootstrap
fills one, the goal reads another that is empty forever. There is no compiler
error, no link error, and no failing test — the bot simply reports "no pasture
table" or picks nothing, which reads like missing data rather than a build bug.

**How to apply:** after any such split, verify from the objects, not from a log
line: `dumpbin -symbols build-m1/CMakeFiles/<target>.dir/<path>/*.obj` and
confirm exactly one `DEF` for each accessor with every consumer an `UNDEF`
`REF`. A gate run only proves the one code path it walked; the symbol table
proves all of them. Same rule applies to any lazily-loaded cache behind an
accessor. Related: [[a-craft-route-is-not-a-recipe]],
[[state-flags-need-the-latest-statement]].
