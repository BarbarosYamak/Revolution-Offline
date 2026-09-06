---
name: a-self-use-skill-has-no-failure-message
description: Meditation always answers "You are at peace", so a practice bout's only honest outcome is the 0x3A skill delta -- and Sphere refuses to START Meditation at full mana, so a full pool is a precondition, not bad luck
metadata:
  type: feedback
---

A goal whose action cannot fail must be judged by a MEASURED result, never by
an attempt count. For skills that means the server's own skill table
(0x3A -> `Client::OnSkills` -> `Observation::skills`) read before and after the
bout; anything else is a completion that did nothing.

**Why:** `DoPracticeSkill` called `planner_.Finish(true, ...)` after six
`ActionUseSkill` calls without ever reading the skill. Selene logged ten
`practice: using Meditation to raise it (20.0)` lines and two
`goal_completed=PRACTICE_SKILL` with the skill on 20.0 the whole session, and
`grade_life.py` clause 2 counted that ATTEMPT line as verified training -- so
TRAIN-2 was inflated for every self-practising character
(artifacts/selene_train_false_positive_2026-09-06.md).

The reason nothing ever moved is a hard server precondition, not variance:
`CChar::Skill_Meditation` at `SKTRIG_START`
(server/Source-X/src/game/chars/CCharSkill.cpp:2674-2679) prints "You are at
peace." and returns `-SKTRIG_QTY` while `Stat_GetVal(STAT_INT) >=
Stat_GetMaxAdjusted(STAT_INT)`; `Skill_Start` (:4505-4520) then cleans up on
the negative difficulty, so `Skill_Done` and `Skill_Experience` are never
reached. The gain is credited at `SKTRIG_SUCCESS` when the pool FILLS, so the
precondition is "mana below max" -- mana must be SPENT (a cast, a fight)
before resting teaches anything. A character with Meditation planned and no
mana-spending skill can never raise it, and the need must say so.

**How to apply:** the shape is a per-bout baseline keyed by skill AND
`planner_.Current().startedAtMs` (a tenth won at a trainer between bouts must
not be re-reported as won by practice), one
`train: <skill> <old>-><new> gained by practice` line per real tenth, and
`EndPracticeBout` returning success only if the bout gained -- otherwise
`goal_failed=` with a reason, a cooldown and a memory event. When you tighten
a grader afterwards, take the OUTCOME line, and check the regex against the
text actually emitted: `casting spell \d+ at myself` never matched
`practice: casting Bless (spell 21, ...) at myself` and had been dead for
months. Related: [[progress-counts-issues-not-results]],
[[a-kill-belongs-to-the-trip-not-the-fight]].
