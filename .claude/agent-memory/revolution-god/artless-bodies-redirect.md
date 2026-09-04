---
name: artless-bodies-redirect
description: Owner rule for creatures the Revolution client cannot draw — redirect to a body it has (savages = human body, COLOR 1425), never leave invisible
metadata:
  type: feedback
---

When a spawned creature has no anim frames in the Revolution client, redirect the CHARDEF (`ID=` to a drawable body, keep DEFNAME/stats/spawn identity) rather than leaving it invisible or pruning it blindly. Owner 2026-09-04: savages on Revolution were **human body with COLOR 1425**.

**Why:** local/revolution-client has only anim.mul (no anim2-5, no bodyconv.def, no body.def). LBR-era bodies like savages 0xB7-0xBC have zero frames. Owner watches the client in parallel and sees bodiless mobs immediately.

**Caution (2026-09-04):** savages already had `DISPID=c_man/c_woman` (M10.7, c_monster_lbr.scp) — the owner's "no proper body" was the random skin hue, not invisibility. docs/REVOLUTION_BODY_ID_CATALOGUE.tsv resolves `ID=` only, NOT `DISPID=`, so its 94 "artless spawned" rows overstate; grep DISPID before calling a body invisible. Also: COLOR= without leading zero is decimal in Sphere (CExpression.cpp ~1442).

**How to apply:** Check anim.idx frame count first (bodies <200: group body*110; 200-399: 22000+(b-200)*65; ≥400: 35000+(b-400)*175). If zero and the owner remembers the Revolution look, redirect + hue. If nobody remembers, ask before pruning. Precedent: c_wolf_grey → ID=c_wolf_timber. Full artless table: docs/M10_6.md (386 of 488 bodies).
