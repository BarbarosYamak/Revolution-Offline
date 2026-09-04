---
name: open-items-2026-09-04
description: Owner-assigned open defects and research questions for the next session (set 2026-09-03 evening); who owns each
metadata:
  type: project
---

Open items handed over by owner on 2026-09-03 (Sphere closed, runtime repo HEAD b2246a4 "Fleet reborn"):

Defects (delegate):
- Faustus never equips his mace → kills=0 root cause. Owner: `bot-core`.
- Aurelius `vendor_buy` timeout at scribe. Owner: `bot-core` / `uo-protocol`.
- Rhea never reaches taming. Owner: `bot-brain`.
- Dungeon chest "invalid ResourceType" error. Pre-existing, shard-side → `sphere-expert`, low priority.

Found 2026-09-04 during alchemist loop (Elara PASS 13/18, sold 66 poison for 1264gp; fixes uncommitted on revolution-sphere-m1):
- NeedBank 0.40 fires a bank trip for 4 spare heal potions (trip-at-20 ignores value) → dynamic-threshold work, `bot-brain`.
- Ocllo is UNGUARDED on this shard (a_Ocllo_7 no REGION_FLAG_GUARDED); Magincia has no scribe in atlas → Magincia chars ping-pong via moongate. Design question for owner, not a bug.
- Plain Poison trains ~nothing at Alchemy 50; Greater Poison at 55.1 is the next rung (authentic).

Research questions (lead holds, answer from scripts/forums, mark UNKNOWN if unproven):
- Bow ↔ log mapping (bowcraft: which logs make which bows).
- Meditation % (regen rate / what Meditation skill gives on Revolution).
- Weapon-merging era (when Revolution merged weapon schools; which era we emulate).

**Why:** Owner listed these as the next session's agenda; previous session ended with Sphere shut down.
**How to apply:** Open next session by confirming these still reproduce (rev.py gates CHARS=Faustus,Aurelius,Rhea MINUTES=5), then fan out defects to owners in parallel; research items go through forums-first-then-confirm-with-owner rule. Remove this memory once items close.
