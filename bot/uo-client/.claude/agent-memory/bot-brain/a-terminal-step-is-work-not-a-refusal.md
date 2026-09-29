---
name: a-terminal-step-is-work-not-a-refusal
description: A need gate may block only on handler steps that REFUSE; abandon/give-up/mark-gone are work, and gating them strands the need forever
metadata:
  type: project
---

When a need's score is gated on "would the handler act", the gate must copy the
handler's decision **order**, not just its rules — and it must block only on
the steps that are refusals.

`DecideRecovery` (src/life/activities/RecoveryPlan.cpp) tests `threatened`,
then spent-attempts `Abandon`, and only THEN "too hurt to walk back". A gate
that applied the health line unconditionally blocked RecoverCorpse for a
character raised at a fifth of its health with its trips already spent — but
the handler's next step there was `Abandon`, and **`Abandon` is what clears the
death record**. Blocked, the record is never cleared and the corpse is carried
for the rest of the character's life. All 17 archetypes failed
`TestRecoveryForEveryArchetype` on exactly this.

**Why:** a terminal step is the handler doing work — closing the errand — not
declining it. Starve it of the turn and the need can never end. Same shape as
[[an-errands-exit-cannot-live-in-the-errand]]: the thing that ENDS a goal needs
a turn to run in.

**How to apply:** before writing a gate for any pair, read the handler's
decision function top to bottom and classify each outcome as refusal or work.
Gate the refusals only. Then assert it: run the handler's own decision function
over a matrix of observations and require `Need::blocked == (step == <the
refusal step>)` — never re-type its rules in the test, which only proves the
test agrees with itself
(`TestContractCorpseGateMatchesTheHandler`, tests/m4_life.cpp).

Second rule from the same pass: a block's **staleness test belongs in the
reader**, not in the wrapper. `AssessNeeds` reads blocks through
`NeedBlockActive` directly, so a "bandage in the pack ends HEAL's block" check
written one level up in `CanAct` never ran at the need site — the character sat
out a stand-down with the medicine in hand.
