---
name: one-resolver-for-need-and-handler
description: Pattern for satisfying the need/handler contract - a single atlas resolver in runner_detail, its result copied onto Observation for the pure need model
metadata:
  type: project
---

The need model (`src/life/Needs.cpp`) links no world code, so it cannot read
the atlas. The working pattern for arm A of
`docs/NEED_HANDLER_CONTRACT.md`:

1. Write ONE resolver in `runner_detail` (declared in
   `src/life/runner/RunnerInternal.h`, defined in `RunnerShared.cpp`) that
   answers every question both halves need at once -- including a `resolved`
   flag meaning "the handler would have somewhere to act".
2. `Runner::Observe` (src/life/runner/Core.cpp) calls it and copies the answer
   onto `Observation` fields. Default those fields to the values that make the
   need SILENT, so a pure need model with no world behind it never invents the
   errand.
3. `AssessNeeds` gates on the `resolved` field; the handler calls the same
   resolver and acts on the same numbers.

**Why:** a need that measures one distance while the errand walks to another
is exactly the disagreement the contract exists to end. Worked example:
`runner_detail::ResolveHomeReturn` + `Observation::homeKnown /
inHomeRegion / tilesFromHome` ([[return-home-goal]]).

**How to apply:** reach for this before adding a bespoke predicate to
`life::CanAct`, whenever the precondition is an atlas lookup rather than a
fact about the pack or the character sheet.
