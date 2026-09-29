---
name: a-menu-title-is-not-a-menu-option
description: kCraftMenus step1 for inscription held the SUBMENU TITLE ("Spell Circle 3") instead of the option text ("third circle"), so every scribe sitting failed REFUSE_MISSING_RECIPE
metadata:
  type: project
---

A Sphere skillmenu has a TITLE and a list of OPTIONS, and they are written in
different places. `sm_legacy_inscription.scp` reads:

    [SKILLMENU sm_inscription]
    Spell Circles              <- the title
    ON=i_spell_circle_3        <- the option
    SKILLMENU=sm_inscrip_3
    ...
    [SKILLMENU sm_inscrip_3]
    Spell Circle 3             <- the SUBMENU's title

An option is rendered as the referenced itemdef's `NAME`, which for these is the
ordinal word: `i_magic_magery.scp:1893-1907` gives `NAME=third circle` and
`NAME=fourth circle`. So `kCraftMenus` holding `"Spell Circle 3"` as step1
matched nothing, and the scribe's whole ladder — poison included — ended at
`goal_failed=CRAFT reason="REFUSE_MISSING_RECIPE"`.

**Why:** the string was taken from the submenu header, which reads like a label
for the entry that opens it. Live proof that it is not:
`[0x7C] dialog menu=690: "Spell Circles" (3 options) 1) first circle
2) second circle 3) third circle` (run_gates/g_Thalia.console.txt, 2026-09-04).

**How to apply:** when adding or fixing a `kCraftMenus` row, read the `ON=`
line's itemdef `NAME=`, not the `[SKILLMENU]` header. Anchor with `^` when the
sibling options share a trailing word ("first circle" .. "eighth circle"). The
failure branch already prints what the menu really offered — one run of
`craft:   offered:` lines settles it without guessing. See
[[a-citation-can-point-at-the-wrong-case]].
