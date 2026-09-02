---
name: wool-lamb-carve
description: Owner's wool rule (shear 1, kill shorn sheep, carve 3) is fighters-only income selling CLOTH to tailors; tailors shear only; the three client bugs it exposed
metadata:
  type: project
---

Wool chain, verified live by the owner and by Halain 2026-09-03 (commit b20836e):
shear = 1 wool; the shorn sheep stays "sheep" (body 0xDF); kill it and carve the
corpse with the wielded blade = 3 more wool + meat, all INSIDE the corpse.

Owner rulings (2026-09-02):
- Only melee fighters run the full chain (HARVEST_WOOL goal, NeedWoolIncome,
  weight 140): shear → kill → carve → loot → wheel → loom → scissors, then sell
  the CLOTH (not wool) to tailors player-first. Extra income source for warriors.
- Tailors shear only ("tailor doesnt have attack skill"); Wren's flow unchanged.

**Why:** gives fighters a gold tap that is real play, and tailors a player supply
of cloth instead of NPC bolts.

**How to apply:** DoMakeCloth in src/life/Runner.cpp branches on
`planner_.Current().kind == HarvestWool`. Don't add the kill step to tailors.
Client facts learned here (fixed, keep in mind for any corpse/loot work):
- headless client never expired dead mobiles (deadRemoveMs only swept by the
  renderer) — Client::Tick now sweeps; a kill is "done" when MobilePosition fails.
- carve is confirmed in words only (carve_corpse_* → ClassifyCarveMessage).
- Sphere sends container adds (0x25) only to clients with the container open:
  always open a corpse after carving before looking for the output.
Open: warriors' normal hunt loop still never loots/carves corpses.
