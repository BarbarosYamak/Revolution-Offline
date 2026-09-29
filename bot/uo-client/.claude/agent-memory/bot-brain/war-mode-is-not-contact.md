---
name: war-mode-is-not-contact
description: A character who never swings back has war mode off and no target, so a contact test built on those two reads a fatal beating as peace
metadata:
  type: feedback
---

"Am I in a fight" must be answered from the **board** (hostiles within
scan range, and how long since one was last seen), never from war mode or
a chosen combat target alone.

**Why:** `combat::Decide` tested `v.inCombat || v.enemyAdjacent`. Odessa
(merchant_tinker, 50 hp, no combat skill in her plan) never entered war
mode and never chose a target, so both were false the whole way down her
health bar — the HP watchdog printed `hp 15/50 (30%) -> rest` and
`hp 5/50 (10%) -> rest` with a Harpy and three orcs on her, and she died
13 seconds later (run_gates/g_Odessa.console.txt 2026-09-07:1338,1360-1361;
sphere2026-09-07.log `00:55:P'Odessa' was killed by N'Harpy'.`). The same
trace also shows two **ten-second lulls** with no damage while all four
were still chasing her (00:54:49-00:55:04 and 00:55:15-00:55:25), which is
why "quiet" has to mean 15 s, not "no hit this poll".

**How to apply:** any policy that decides to rest, bandage, sit, or resume
an errand needs a positive all-clear — nothing in scan range for N seconds
— not the absence of a self-declared fight. Give the pure policy the
hostile count as an input with a safe default so existing callers are
unchanged. Related: [[a-flee-percent-is-not-a-retreat]],
[[an-emergency-overrides-the-catalogue]].
