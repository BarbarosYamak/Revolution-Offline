---
name: a-death-record-outlives-the-corpse
description: Player corpses decay in 7 minutes (sphere.ini CorpsePlayerDecay) while the persisted death record survives logout — "we know where we died" is not "there is a corpse"
metadata:
  type: project
---

`runtime/sphere.ini:576  CorpsePlayerDecay=7` (minutes; NPC corpses likewise,
line 573). The bot's `travel::DeathRecord` persists across sessions, but
`DeathRecord::corpseSerial` is intentionally session-local and starts at 0, so
a character raised more than seven minutes after death walks back to bare
ground with no serial to open.

**Why:** this produced the Hector loop of 2026-09-02 — `RecoverySight::
corpseKnown` was true from the restored record, `DecideRecovery` returned
`Loot`, and the handler fired `open_container` on serial 0 every 1.5 s,
answered `invalid_state / null serial`, 200+ times, with nothing counting an
attempt.

**How to apply:** treat "corpse known" (a remembered tile) and "corpse
visible" (a bound world-item serial) as different facts. Reaching the tile is
not reaching the corpse. When the corpse is not there, the loot IS gone —
Revolution death is full loot, so the correct outcome is a stated failure plus
a planner cooldown, then `REPLACE_EQUIPMENT`; never a retry. Implemented as
`RecoveryStep::CorpseGone`. Same family as
[[goals-addressed-to-nobody]] — a request sent to serial 0 is addressed to
nobody.
