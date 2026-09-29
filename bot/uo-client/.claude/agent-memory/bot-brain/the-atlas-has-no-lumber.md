---
name: the-atlas-has-no-lumber
description: data/revolution_atlas.txt has zero PLACE rows yielding lumber, so no seeding or nearest-forest fallback can give a lumberjack a destination
metadata:
  type: project
---

`data/revolution_atlas.txt` contains no `lumber` resource anywhere. The only
values in a PLACE row's resource column are `hunting` (178), `reagents` (60),
`mining` (25), `fishing` (17). Check with
`awk -F'\t' '$1=="PLACE"{print $10}' data/revolution_atlas.txt | sort | uniq -c`.

**Why:** it makes `Atlas::NearestPlaceWithResource(Lumber)` null for EVERY home
city — Yew included — so `Client::TravelToResource` always answers "no known
source of that resource" (src/travel/ClientTravel.cpp:851), and
`life::SeedNewbieKnowledge` cannot seed a logs lead for anyone. A lumberjack who
works is one who happens to be STANDING in forest: Halain chopped at 1320,1846
in Yew without any seeded lead. Vorar, homed Britain, looped
`StepOutOfGuardZone`'s ~7-tile hop into treeless streets 107 times and gathered
nothing (artifacts/fix_gettool_lumber_2026-09-06.md).

**How to apply:** do not accept "add a nearest-forest atlas fallback" as a
data-only bot-brain fix — the category does not exist to read. The two real
routes are new atlas lumber PLACE rows (atlasgen / navigation-world) or a longer
guard-zone escape (`Client::StepOutOfGuardZone`, world/GuardZoneAdvance.h).
Related: [[a-material-by-class-is-not-a-material-by-rule]].
