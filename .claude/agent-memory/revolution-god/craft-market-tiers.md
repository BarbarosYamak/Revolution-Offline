---
name: craft-market-tiers
description: Owner rulings 2026-09-02 on what crafters sold — tiered sets/bows/robes sell, cheap output is self/train, magic weapons are loot from everyone, fish always cooked
metadata:
  type: project
---

Owner rulings (2026-09-02), recorded in docs/REVOLUTION_CRAFT_PRODUCTS.md + data/revolution_craft_demand.tsv:

- Metal armor sets sell by ore tier (Copper..Blackrock) — Blacksmithing product. Confirmed.
- Magic weapons +6..+15 came from everyone (loot/any source), NOT Blacksmithing. TSV skill column = `Loot`.
- "Staff" not "staves" — owner wording. Magic staff items follow the loot rule; Carpentry attribution still unconfirmed.
- Tailoring: plain leather set ("deri set") AND studded set both sold — buyers mostly newbies. Tailor sale floor = leather set. (Smith floor is higher: iron output = self/train.)
- Everyone cooked fish; nobody kept or sold raw. Closes forum-evidence disagreement D7 in favour of cooked.
- Invulnerability set (armor w/ magic prop) — craft vs loot still UNKNOWN; owner ruling covered weapons only.

- Cloth sourcing rule B (lead decision, owner delegated 2026-09-02): WTB-first when affordable; "cannot buy now" (gold below reserve, no session time, nobody answered) counts as market declined → shear→spinwheel→loom→scissors self-chain. Never NPC for yarn/cloth.

**Why:** Revolution player market = quality tiers, not daggers. Cheap smith output (dagger, iron plate) is train/self stock; leather/studded sets have a newbie market.
**How to apply:** bot-brain sale-stock rules: smith sells only special-ore sets; tailor may sell leather/studded cheap to newbies plus magic robes; newbie bots buy leather sets as first armor. Fisher cooks before bank/sell. Magic weapons: loot/buy/sell for any profession, never craft.
