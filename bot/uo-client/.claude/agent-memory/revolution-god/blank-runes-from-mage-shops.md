---
name: blank-runes-from-mage-shops
description: Owner 2026-09-30 — blank recall runes were bought from mage shops on RevolutionUO; players marked them. Overrides the old "runes are marked, not bought" reading.
metadata:
  type: project
---

Blank runes (i_rune_marker, 0x1F14) were sold by MAGE SHOPS on RevolutionUO;
players bought them and cast Mark on them. The shard's vendor table already
sells them (tm_vend SELL=i_rune_marker), and REVOLUTION_GAMEPLAY_TRUTH.md:280
says 2-10 gold at the mage shop.

**Why:** owner, 2026-09-30: "blank runes from mage shops". First-hand
testimony, same standard as the reagent ruling (VendorPolicy.cpp).

**How to apply:** i_rune_marker is REVOLUTION_NPC_VERIFIED (allowed). Mages
buy a blank rune at a mage shop when they can Mark (Magery 60) and own no
home rune; marking still happens in game (runner/Runes.cpp).
