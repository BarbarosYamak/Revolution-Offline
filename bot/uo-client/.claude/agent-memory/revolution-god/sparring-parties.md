---
name: sparring-parties
description: Owner PLAYER_MEMORY 2026-09-04 — on Revolution 2-3 players partied and attacked each other in iron sets with low-damage weapons while others farmed Healing by bandaging them; the intended social training loop for fighters/healers (and stat gain), not yet implemented
metadata:
  type: project
---

Revolution training culture (owner, PLAYER_MEMORY, forum check pending): 2-3 players form a party and fight each other — full iron set, low-damage weapon (dagger/club class) — so hits land often but barely hurt. Other players stand by and bandage them to farm Healing (and Anatomy). Fighters gain weapon skill/Tactics/Anatomy and STR/DEX from real combat; nobody dies.

**Why:** owner 2026-09-04 when asked how stats/skills were raised; it's the human answer to slow stat gain — social, gear-based, rule-obeying. Not a server tweak.

**Owner rulings 2026-09-04 (second pass):** sparring is for EVERY fighter-type — warriors, warlocks, mages (bare-handed for STR, see [[caster-str-via-wrestling]]). It may happen INSIDE town, around Brit bank, on purpose: liveliness. Sparrers bandage themselves during the spar; other bots wanting Healing may bandage them too. Casters lock DEX at 25 (or plan targetDex if the build says 35) — read from the plan, never a constant. Verified legal: Source-X `CCharNotoriety.cpp:169` same party → NOTO_GUILD_SAME, so `Fight_Attack` never runs the crime check; no witness/guard path.

**How to apply:** design as a multi-bot social goal (M13): party invite via protocol, consent flag, iron-set + weak-weapon prerequisite (fists for casters), healers join with bandages, stop rule at HP threshold (stop swinging at partner < 40%), guard-zone-safe by party. No server-side shortcuts. Stat-gain weights on the shard stay stock (BONUS_STATS 10-50) except the Meditation DEX-90 template bug. Related: [[start-stats-mage-50-25-5]], [[spellbook-is-a-side-goal]].
