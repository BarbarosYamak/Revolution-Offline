# bot-brain memory index

- [Grader covers 17 families](grader-covers-17-families.md) — wool logs under `bandages:`, treasure_hunter has no loop; take FARM-2 strings from Runner.cpp LogLines
- [wave10 is truncated](project-wave10-truncated.md) — no session_summary/session_goals anywhere, so its low LIFE-GATE scores are an artefact
- [Two sale questions](two-sale-questions.md) — "will an NPC pay" is not "would anyone buy"; conflating them silently disabled tailor/tinker/lumberjack
- [progress counts issues, not results](progress-counts-issues-not-results.md) — progress=243 is a busy-wait credited per tick; helpers that return true for "come back later"
- [Craft focus rotates per sitting](craft-focus-rotates.md) — satiation one level below GoalKind: 4 sittings on one product and the bench moves on
- [A material by class is not a material by rule](a-material-by-class-is-not-a-material-by-rule.md) — fish is WorldGathered; ask the faucet registry before restricting "materials" or you delete a fisher's income
- [Thresholds are rates, not numbers](thresholds-are-rates-not-numbers.md) — "500-600 ingots" is 5.5 units per skill point; two characters must get two caps
- [Demand needs a voice](demand-needs-a-voice.md) — the buyer stood silent at the market for 3 minutes; WTB shout + one shared WTB parser
- [Gather only when the market declines](gather-when-the-market-declines.md) — players first, per-ITEM `no_player_seller` gate; "cannot buy now" (broke / no session left) also counts as declined
- [Wool makes cloth, not thread](wool-makes-cloth-not-thread.md) — the chain's real numbers, and why a bot tailor can weave but cannot sew
- [A citation can point at the wrong case](a-citation-can-point-at-the-wrong-case.md) — right file:line, wrong switch branch; scissors never sheared a sheep
- [Names arrive after a scan](names-arrive-after-a-scan.md) — an empty name-filtered scan means "not asked yet"; three pastures of sheep read as deserted 60 ms after arrival
- [A spell's cost is not the profession's consumes list](a-spells-cost-is-not-the-professions-consumes-list.md) — obs.pack could not see sulfurous ash at all; QtyOf==0 means "not counted" as often as "not held"
- [A trip budget cannot see travel time](a-trip-budget-cannot-see-travel-time.md) — 3 trips is a whole session at ~60s of walking each; clock the shopping half, escalate the rest
- [A winning goal can hand itself away](a-winning-goal-can-hand-itself-away.md) — TRAIN_COMBAT won 84.5 and got 0 kills; a handoff is advice, the receiver must also out-score the field
- [ms stand-downs die with the process](ms-stand-downs-die-with-the-process.md) — atMs is per-process steady_clock; count sessions via NeedConfig::sessionIndex for durable rests
- [A book row's graphic is not a scroll's](a-book-row-graphic-is-not-a-scroll-graphic.md) — BookHasGraphic said "lacks it" about a spell the book refused; 84 gold on one Cunning Scroll, four times
