# Archetype flow continuation — 2026-09-05 afternoon

## Changes
- Magery practice distinguishes insufficient mana from an unusable book. If a known, safe, skill-eligible spell has its reagents, the runner meditates and retries; recovery is bounded and yields near hostiles. Missing reagents still produce a shopping list, including when mana is also empty. No-gain regions remain blocked.
- Bandage and cloth buying now recognize vendor discovery, reaching the counter, and opening the offer as progress even when the transition sends no action. Previously the `acted` guard prevented those transitions from resetting the five-attempt limit.
- Atlas generation excludes buildings from name-derived mining resources. Removed four generated false ore destinations (Britain and Magincia mining cooperatives, Minoc miners guild, Old Miners Supplies). Retained real mines and all unrelated atlas rows, including measured forests.

## Evidence
Initial five-minute Hector/Odessa run (15:28–15:33): Hector spent the session in upkeep with repeated five-attempt abandonment; Odessa reached Britain Mining Cooperative, found no mineable rock and fell back to exploration. Both logged out cleanly, with no deaths.

Build and CTest passed 43/43 after the main changes. Additional regression coverage checks depleted mana recovery, resumed casting, rejected/unsafe spells and empty reagent pouches. Final build status and second live run results below.

Atlas verification: ran uo_atlasgen against runtime/scripts with --skip-grid into artifacts/archetype_atlas_generated.txt. Exactly four old mining PLACE rows were absent; real Britain and Minoc mine markers remained. Only those four rows were removed from the operational atlas, preserving forest data.

## Remaining scope
These changes do not establish complete archetype passes. Treasure-map availability and the other archetypes from the midday handoff remain outstanding. No shard scripts, treasure loot rates, or character stats were changed in this continuation.

## Second live run (15:36–15:41, five-minute budget)
- Odessa: PASS for corrected mine selection and gathering. Went to Brit Mine1, struck rock at 1449,1246, collected ore at 15:40:46 and 15:41:20. Mining->smelting->gears->sale remains unproven; early time went to a mount errand and travel.
- Hector: PARTIAL. Weaver discovery, approach and shop opening completed; the shelf lacked loose cloth. Bought 10 bandages from a later counter (47->57). Still below the 100 floor; no completed hunt. Empty-shop fallback and repeated upkeep remain next targets.
- Aurelius: PASS for one kill and loot continuation: Skeleton kill at 15:39:38, loot confirmations at 15:39:41 and 15:39:49, remained at the yard. Spellbook shopping timed out after 90 seconds; practice was initially blocked by a no-gain region. The new meditation branch did not execute in this smoke, so its evidence is the unit regression only.
- Final focused planner test: 731 checks, zero failures, including the both-mana-and-reagents-empty edge case.

No complete end-to-end archetype certification is claimed. Continue with Hector's supply fallback, then a longer Odessa production/sale run and an ordinary-ground mage practice run.

Final verification: incremental full build succeeded; CTest 43/43 passed (4.21s). All three clients logged out with server acknowledgement and zero deaths. Aurelius session: 1 kill, gold 6624->6672. Odessa session included two ore pickups. Graphify code update completed: 31240 nodes, 81826 edges. git diff --check passed. Changes remain uncommitted.
