---
name: a-blocked-need-shadows-a-ready-one
description: Needs are sorted by urgency and FindNeed takes the FIRST of a kind, so one blocked entry of a multi-entry need kind hides every practicable sibling -- give blocked entries urgency 0.0
metadata:
  type: feedback
---

When one `NeedKind` is emitted once per candidate (NeedPractice: one per
planned skill), a blocked entry with a high urgency hides every ready sibling.
`Planner::Score` calls `FindNeed`, which returns the FIRST need of the kind
(src/life/Goals.cpp:172), and `Needs.cpp` `stable_sort`s the vector by urgency
descending -- so the winner is "highest urgency", blocked or not, and the goal
reads BLOCKED_NEED with a reason that belongs to a skill it was not going to
practise anyway.

**Why:** Aurelius, a mage, had `NeedPractice(Poisoning BLOCKED 0.45)` (no
dagger, no potion) standing in front of `NeedPractice(Magery 0.32)`, and
PRACTICE_SKILL therefore read as blocked for a caster who could have been
casting (run_gates/g_Aurelius.console.txt:66, 2026-09-06). The same trap was
about to be re-created by a "skip the blocked entry" special case for
Meditation at full mana.

**How to apply:** give a blocked entry urgency 0.0 rather than dropping it --
that is what `NeedBlockActive` already does for an observed refusal, it keeps
the BLOCKED_NEED telemetry ("why didn't it practise" still has an answer), and
it sorts the entry below every practicable one. Check for the same shape
wherever `add(...)` is called inside a loop over plan skills, pack items or
suppliers. Related: [[a-self-use-skill-has-no-failure-message]],
[[a-short-shelf-is-not-the-trade]].
