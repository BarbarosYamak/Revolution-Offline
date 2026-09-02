---
name: sphere-reach-is-two-tiles
description: Every reach-gated action (vendor buy, use, grab) needs Chebyshev distance <= 2; the client must not allow itself 3
metadata:
  type: project
---

Sphere refuses any reach-gated action beyond **2 tiles, Chebyshev**
(`max(|dx|,|dy|)`). Mirrored in `uo::sphere::kTouchDist` /
`CanTouchAtDist()` in `include/uo/sphere_rules.h`.

**Why:** `CChar::CanTouch` refuses on `iDist > 2`
(Source-X `src/game/chars/CCharStatus.cpp:1423`, where `iDist` is
`CPointMap::GetDist` — Chebyshev). The vendor buy packet checks it before
anything else: `if (buyer->CanTouch(vendor) == false)` -> "You can't reach the
Vendor" (`src/network/receive.cpp:752-756`). A client that considered 3 close
enough was guaranteed to be refused at exactly that distance — Aurelius opened
a mage shop fine from 3 tiles (speech range is larger) and had both purchases
refused in 1ms.

**How to apply:** any new action that the server gates on CanTouch — item use,
grab, vendor buy/sell — takes its threshold from `sphere::kTouchDist`, never a
fresh literal. Measure in Chebyshev; Manhattan gets the boundary wrong in both
directions (dx=3,dy=2 is 3 Chebyshev but 5 Manhattan; a pure diagonal at 2 is
reachable but 4 Manhattan). If a reach-gated action gets a server refusal,
suspect the metric and the threshold before suspecting a wall.

Related: [[los-is-reach-not-identity]].
