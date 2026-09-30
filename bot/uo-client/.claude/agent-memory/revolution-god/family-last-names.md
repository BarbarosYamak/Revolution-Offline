---
name: family-last-names
description: Owner 2026-09-30 — Revolution family system: ~50k family deed makes the user head and lets them choose a last name; invitation deeds (~10-20k each) give an invited player the same last name; families were social, shared a home/house, and allied in PvP.
metadata:
  type: project
---

RevolutionUO had families with shared LAST NAMES. A family deed (~50,000
gold) made its user the family head, who chose the last name. Each invite
cost a deed (~10,000-20,000 gold) and the invited player carried the same
last name. Family meant: social ties, a shared home/house, allies in PvP.

**Why:** owner, 2026-09-30, first-hand ("you had a family deed for 50k or
something then you were family head choose a last name then you invite a
player per deed which is 10k or 20k and they have the same last name").

**How to apply:** server script runtime/scripts/revolution/revolution_family.scp
(deeds 50k / 15k, name appended to NAME, invite gump "Aile daveti");
bot side src/life/runner/Family.cpp + include/uo/family.h. Still UNKNOWN:
which NPC sold the deeds, whether names were appended or shown as titles,
leaving/disbanding. Ask the owner before inventing those.
