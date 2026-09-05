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

## Added 2026-09-05 (survival wave, commit f65225b)
- Archer ammunition loop: Titus' 11 arrows are in the bank; only gold
  withdrawal exists (no item-withdraw-from-bank primitive), no arrow
  buy/craft path; Ranged strategy correctly refuses combat with 0 arrows.
  Feature-sized, unowned.
- Titus: provisioner 'no food seller' failures.
- Castor: "carrying 0x1450 (unmapped item)".
- Faustus home is Britain but he was dumped in Papua after death; still
  needs to walk home (5-min smoke ended mid-UPGRADE_GEAR in Papua).

## Added 2026-09-05 03:20 (second fighter pass)
- Faustus: HARVEST_WOOL vs TRAIN_COMBAT score oscillation (54.5/28.5 vs
  42.0) -- pasture trip 232 tiles then graveyard; a macer at the pasture
  is wrong by owner rule (fighters kill+carve, tailors shear).
- Castor at Trinsic inn upstairs (z=10): A* "no path" from the room for a
  whole session before he got out; interiors/stairs case, navigation-world.
- Fighter banks ONE dead-weight item and leaves at 69% (Castor 03:14):
  halberd + maul still carried; bank goal should empty to a margin below
  the hunt line, not stop at the line.
- Castor "hunt: picked ''" -- target name blank in log (name not yet
  received); cosmetic.
- "[survival] hp 50/50 -> fight" logged every 5 s with no foe: noise.
- 0x1450 = i_bone_gloves, unmapped in econ item table.
- Stalemate legitimacy still unverified; `[0xA1] foe ... hp` log added.
