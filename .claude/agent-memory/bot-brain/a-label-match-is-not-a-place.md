---
name: a-label-match-is-not-a-place
description: DoMine chose its mine by finding the home city's name inside a memory hint's label; a place rule needs atlas PLACE ids, not string matching
metadata:
  type: feedback
---

An owner ruling about WHERE a bot works belongs in an allow-list of atlas
PLACE ids, resolved through `Atlas::PlaceById` and checked with `Yields(...)`.
Not a coordinate (it rots when the atlas is regenerated) and not a label
match.

**Why:** DoMine picked "the mine in the home city" by scanning hinted ore
resources for `source.label.find(state_.homeCity)`. That silently means "any
remembered ore whose printed name happens to contain the word Britain", and
when no hint matched it fell through to `TravelToResource(Mining)`, which is
nearest-first. Odessa (merchant_tinker, Britain) mined at (1449,1245) inside
Brit Mine1 for a whole session — run_gates/g_Odessa.console.txt 01:14-01:16.
Owner ruling 2026-09-07: every MINE trip goes to the Minoc mine, whatever the
home city; Britain-area mines are off the list.

**How to apply:** `kMinocMinePlaceIds` in `src/life/runner/RunnerInternal.h`
is the pattern — the ids, a comment citing the ruling and its date, and a
picker that returns null when nothing resolves so the caller can log
"unavailable" instead of quietly taking the forbidden nearest one. Also kill
the nearest-first fallback on the same branch: leaving it live means the rule
holds only while the atlas is loaded. The distant-home case is an ordinary
journey (moongate included), not a special case — Kharain already travels that
way (g_Kharain.console.txt 00:44-00:55).

Related: [[the-atlas-has-no-lumber]], [[a-category-is-not-a-difficulty]].
