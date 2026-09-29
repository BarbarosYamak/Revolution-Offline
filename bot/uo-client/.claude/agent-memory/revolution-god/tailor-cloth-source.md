---
name: tailor-cloth-source
description: Ruling on where a tailor bot gets cloth — player-first WTB (forum evidence: bolts sold 17gp bulk), self-gather shear→spin→weave as fallback; never NPC; thread-vs-yarn quirk is UNKNOWN
metadata:
  type: project
---

Tailor cloth source (ruled 2026-09-02 after forum check): buy from players
first (WTB kumaş, ~17 gp/bolt basis), gather it herself when nobody sells
(sheep → wool → wheel → yarn → loom → bolt → scissors → cloth). Never from
an NPC — cloth/thread/yarn stay `WorldProcessed`.

**Why:** Forum topic 94084 (29 Şub 2016, "Kumaş Satılır (rulo halinde)"):
one player selling a large cloth stock by the bolt at 17 gp with bulk
discount, buyer replies at once — cloth was a bulk player commodity like
ingots/logs (topic 93370). Runtime chain is measured and works
(M3_7_RESOURCE_ECONOMY.md §7, dynamic wheels/looms at britain_tailor_2).

**How to apply:** Gather loop (MAKE_CLOTH: shear→wheel→loom→scissors)
implemented 2026-09-02, unit-tested, not yet run live. CONFIRMED blocker:
every stock Tailoring recipe is `<n> i_cloth, 1 i_thread`
(items/i_provisions_clothing.scp); thread comes only from cotton (6/pile),
wool gives yarn. A wool tailor makes cloth but cannot sew. Sewing stays
blocked until a cotton source (crop plant vs spawned pile) is proven.
Still UNKNOWN whether Revolution had this quirk — forum-sweep item.
Wheel/loom give no packet confirmation (SysMessage only) — bot-core
journal-watch follow-up.
