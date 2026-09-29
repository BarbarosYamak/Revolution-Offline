---
name: a-bystander-is-not-an-attacker
description: A duel test that counts hostiles in reach refuses every real board; count who is swinging, let bystanders raise the retreat floor instead
metadata:
  type: feedback
---

An engagement gate must count hostiles **attacking the character**, not
hostiles **within reach**. Company is priced through the retreat floor
(`board = max(attackersOnMe, inReach)`), never through a veto.

**Why:** the first novice policy (2026-09-06) used `inReach <= 1`. Hector
picked a Cougar at 9 tiles and a Spectre at 10, walked in, and a second
skeleton drifted inside four tiles on the way — so `in_reach=2` fired the
break-off at **100% health with attackers=1, and twice with attackers=0**.
Five refusals, 0 fights, 0 kills in ten minutes, then
`goal_failed=TRAIN_COMBAT "no hunting ground reachable after 3 trips"`
(run_gates/g_Hector.console.txt 2026-09-07:108,128,632,1106,1127,1129).
A graveyard always has a second skeleton somewhere in the yard; a player
who waited for an empty one would never train. Recounting on attackers
took him from 0 engagements to 3 in the same ten minutes.

**How to apply:** whenever a rule reads "N hostiles nearby → don't", ask
which of them is actually committed to the fight. Keep exactly one
radius-based veto — the owner's 3+ within reach — and spend the caution
budget on the hp floor instead. See
[[a-flee-percent-is-not-a-retreat]] for the floor itself and
[[goals-that-spin]] for what a permanently-refusing gate looks like from
the planner's side.
