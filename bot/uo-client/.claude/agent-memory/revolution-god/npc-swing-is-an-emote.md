---
name: npc-swing-is-an-emote
description: Sphere announces NPC→player hits with the "*X is attacking you!*" emote, not a 0x2F swing packet; attacker tracking must read the emote
metadata:
  type: project
---

On this shard an NPC hitting a player produces the ascii emote `*Skeleton is attacking you!*` spoken BY the attacker (source serial = attacker); no 0x2F arrived for those swings (Aurelius 2026-09-05 11:29-11:30, two emotes, zero 0x2F).

**Why:** `Candidate.attackingMe` had no writer and `obs.underAttack` only knew fights we opened, so a bot being hit kept scoring the attacker as "a fight not worth starting". Client::NoteAttackEmote now records it; OnSwing kept for cases that do send 0x2F.

**How to apply:** any "am I being attacked" question goes through Client::IsAttackingMe / RecentAttackerCount, never the war watchdog target alone. Player-vs-player swings may still arrive as 0x2F — unverified.
