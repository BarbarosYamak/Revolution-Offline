---
name: a-material-by-class-is-not-a-material-by-rule
description: FISH is graded WorldGathered so IsFloorMaterial calls it a material — gating it on the surplus cap would delete a fisher's income; ask the faucet registry, not the item class
metadata:
  type: project
---

`econ::IsFloorMaterial` returns TRUE for fish. Any new rule that restricts
"materials" must consult the faucet registry (`faucet::ForItem` +
`faucet::Allowed`) before applying, or it silently deletes a whole profession's
income.

**Why:** the owner's materials ruling names "logs, boards, ingots, ore, cloth,
hides, yarn". The vendor matrix grades fish `WorldGathered` and cooked fish
`WorldProcessed` (VendorPolicy.cpp), so both answer TRUE to `IsFloorMaterial` —
while fish is simultaneously one of the three documented gold TAPS ("caught fish
cook fish then sell"). Item CLASS and sale POLICY are different questions and
this is where they disagree. Caught before it shipped on 2026-09-02, but only
because the fisher's income was checked deliberately; nothing in the test suite
would have failed.

**How to apply:** when writing any gate keyed on `IsFloorMaterial` or on
`VendorClass::WorldGathered/WorldProcessed`, add the "does the registry already
allow a documented route" escape first, and put a fisher assertion in the test.
Related: [[two-sale-questions]].
