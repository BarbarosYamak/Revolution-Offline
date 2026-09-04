---
name: tailor-pasture-home-only
description: Owner 2026-09-04 — tailors shear the flock closest to Britain (SW farmland 1318,1811); never cross-map; cows there too for leather
metadata:
  type: feedback
---

Tailors shear the flock closest to their home city, nothing else. Owner 2026-09-04: "britain fields farmland 4 / closest britain / also lots of cows there as well for leather if needed."

**Why:** Aelia + Wren (Britain tailors) died 7x each walking the whole pasture table in order (Brit → Yew ×3 → Jhelom → Delucia via Trinsic Passage). Pasture index never reset between goals, and there was no distance cap. Fixed in Cloth.cpp with `kMaxPastureTilesFromHome=400` + index reset.

**How to apply:** Any "go find resource X" picker must be anchored on home and capped; when home's supply is bare, fall back to WTB / another activity, never a cross-map or dungeon route. The save has ONE Britain flock (15 sheep at 1318,1811) — owner remembers "lots"; if more sheep are wanted near Brit, that's a spawner change, not a picker change. Britain farmland also has cows → leather source for future tailor/leatherworker loop (unimplemented).

Related: [[tailor-cloth-source]], [[no-gathering-in-guarded-zones]].
