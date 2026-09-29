---
name: stale-spawns-keep-old-scripts
description: Creature look/hue/loot are rolled once at @Create and saved; resync/restart only affects future spawns — run f_revo_respawn_monsters after any creature-script pass or the owner sees "still not fixed"
metadata:
  type: project
---

After editing creature chardefs (DISPID remaps, COLOR, @CreateLoot), standing mobs stay stale until they die. Owner saw invisible savages an hour after the fix was live (2026-09-04) because the spawner-born ones predated it.

Fix: `.serv.resync` then `.f_revo_respawn_monsters` (runtime/scripts/revolution/f_revo_respawn_monsters.scp) — RESETs every t_spawn_char gem whose first child has a monster/berserk/dragon brain (Source-X CCSpawn ISPV_RESET = KillChildren + tick). Prints a summary.

**Why:** DISPID/COLOR/loot live on the saved char, not the chardef; "0/40 knights have DISPID" in the save is the symptom, not a broken @Create.
**How to apply:** any creature-script pass ends with resync + respawn; verify on a FRESH `.newnpc` first (set `.act.p=` before `.act.kill` or no corpse). While the owner is logged in as Admin, Observer probes fail "account already in use" — ask the owner to run the command instead.

Confirmed 2026-09-05: chardef-level `ID=` does NOT redirect display -- `CChar::SetID`
sets `m_dwDispIndex = id` (CChar.cpp:1617); only `ON=@Create DISPID=` does, rolled
once and saved. 39 skeletal knights sat invisible (body 0x93, no frames) until
`.f_revo_respawn_monsters` ran: 2060 gems reset, 7255 stale monsters removed; all
22 respawned knights carry `DISPID=c_bone_knight`. Run it via
`local/dev/run_admin.ps1 -Scenario respawn_monsters` (needs owner logged OUT of
Admin, else "account already in use"); `admin_save` scenario forces `.serv.save`.
`rev.py gates` treats a console touched <120 s ago as live -- after taskkill, loop
`until … grep -q "launched N/N"`.
