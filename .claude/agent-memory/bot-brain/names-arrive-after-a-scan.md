---
name: names-arrive-after-a-scan
description: A mobile's name is empty until ActionScanMobiles' 0x98 query returns; any handler that filters on name must scan and settle before calling a place empty
metadata:
  type: project
---

`ScanMobiles` only reads the cache. Names get there via the 0x98 AllNames query
that `Client::ActionScanMobiles` -> `PrintNearbyMobiles` sends, and the replies
arrive asynchronously (500 ms client-side timeout). Any handler that filters
hits with `if (m.name.empty()) continue;` — the creature tables are keyed by
name — is reading "we have not asked yet", not "there is nothing here".

**Why:** DoTameAnimal did exactly this. Rhea (Taming 50.0, wave 2026-09-02)
walked to all three pastures and logged `tame: nothing tamable here` 60 ms
after each `travel_done`, with 8+ `c_sheep_woolly` inside ten tiles. Only the
vendor goals had ever issued the scan, which is why only they saw names.

**How to apply:** before believing an empty name-filtered scan, issue
`ActionScanMobiles`, wait for `Client::MobileNamesPending()` to clear, and let
a settle window pass (`uo::life::MayJudgeEmpty`, include/uo/activities/tame.h).
Re-arm the scan after every travel leg — a new place needs new names. The same
smell to look for elsewhere: a verdict logged within a tick of an arrival.
Related: [[goals-addressed-to-nobody]], [[a-grep-miss-is-not-absence]].
