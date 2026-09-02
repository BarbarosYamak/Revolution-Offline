---
name: a-book-row-graphic-is-not-a-scroll-graphic
description: BookHasGraphic reads container rows whose graphic is not the scroll's, so "the book lacks this" can be wrong; the book's own refusal is the only authority
metadata:
  type: project
---

`Runner::BookHasGraphic` answers from the spellbook container's rows, and a
spellbook row's GRAPHIC is not the scroll's graphic (the amount field carries
the spell number — that is what `BookHasSpell` exists for). So the shelf-side
"does this book lack it" check can say yes about a spell the book will then
silently refuse.

**Why:** Selene bought the same Cunning Scroll four times for 84 gold in ninety
seconds (`run_gates/g_Selene.console.txt:1274-1753`, 2026-09-02). The pack-side
loop already knew better — the refusal was recorded in `scrollBookRefused_` —
but the purchase path never consulted it.

**How to apply:** the authoritative answer to "is this spell in the book" is
the book's behaviour (did `spellsKnown` rise after the drop), not any graphic
comparison. Anything that spends gold on a scroll must check
`scrollBookRefused_` as well as `BookHasGraphic`. If a future fix needs a
correct up-front answer, go through `BookHasSpell` /
`SpellForScrollGraphic`, not the row graphic.
