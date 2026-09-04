---
name: dismount-to-gather
description: Owner rule 2026-09-04 — bots dismount before mining/lumberjacking and remount when the gathering sitting ends; shard itself stays permissive (Source-X Skill_Mining has no mount check)
metadata:
  type: feedback
---

Bots dismount to mine or lumberjack, then mount again when done gathering. Owner 2026-09-04: "dismount do mine or lumberjack then mount again when you are done with gathering."

**Why:** owner's Revolution memory is that you could not mine on horseback; Source-X `CChar::Skill_Mining` (CCharSkill.cpp:1384) has no mount check and no script blocks it, so the shard allows it today. Forum check was blocked (guest sees one board). Owner chose bot behaviour over a server rule — looks right by eye without inventing a shard mechanic.

**How to apply:** bot-core/bot-brain gather loops (Gather.cpp mine, lumber): dismount at the work site, remount before travelling away. Do NOT add a server-side mount check unless the owner says enforce. Fishing not mentioned — leave as is. Status: IMPLEMENTED 2026-09-04 (Gather.cpp mine+lumber, `Observation::dismountedForWork` so NeedMount doesn't read a deliberate dismount as horselessness; Kharain smoke PASS, lumber side code-only — no horse-owning lumberjack yet). Mount/dismount double-clicks always time out as use_object (~4 s ActionBusy each) though mount_state proves they landed — verdicts come from obs.mounted.
