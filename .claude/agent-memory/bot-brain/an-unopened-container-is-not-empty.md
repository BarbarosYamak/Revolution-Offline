---
name: an-unopened-container-is-not-empty
description: a spellbook reads as 0 rows until it is opened, and NeedSpells scored that as an empty book — FILL_SPELLBOOK won the first goal of every session and achieved nothing
metadata:
  type: project
---

`ContainerItemCount` only answers after the server has sent the contents, which
happens on open. At login a carried spellbook therefore reads 0 rows.
NeedSpells treated that as an empty book: urgency 0.70, `FILL_SPELLBOOK 77.0`
won the first pick of every session, walked to the scribe shop, opened the
book, found the 23 spells that were already in it, dropped to 0.46 / `50.6` and
lost the turn 17 s later (run_gates/g_Lyra.console.txt:79-117, 2026-09-04).

**Why:** this looked like a planner supersession bug — "a goal that just started
loses to a lower earlier score". It was not. The score drop was *correct*; the
input was wrong. Chasing the planner would have found nothing.

**How to apply:** a container's contents are memory, not observation. Persist
the last real reading (`PersistentState::knownSpells`, written only from a
non-empty read in `LearnFromObservation`, since `Observe` is const) and let it
stand in until this session's own read replaces it — the same shape
`state_.bank` already uses. Empty must mean "not looked in yet" everywhere
downstream, and every consumer must fail OPEN on it: refusing a craft rung
because an unread book "lacks" a spell is a claim about state the character has
not observed. Before blaming a planner rule for a score that moved, check
whether an OBSERVATION moved first.
