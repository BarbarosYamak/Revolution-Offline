---
name: a-seed-is-not-a-forum-quote
description: kForumPriceSeeds is verbatim player evidence and must stay that way; a good no post ever priced gets a SECOND, lower-ranked seed derived from the shard's own itemdef VALUE
metadata:
  type: project
---

`market::kForumPriceSeeds` (src/economy/Market.cpp) is a table of numbers real
Revolution players posted, each row carrying its thread and date. A number
nobody posted must not be added to it — that table's whole value is that every
row can be checked.

But a good with no forum row still needs SOME price, or the fleet offers it at
`TradePolicy::openingAsk` (2gp, meaningless) or bids `blindPriceCeiling`
(12gp). The honest third source is the shard's own itemdefs, which a Revolution
player could work out standing at the counter. `kShardValueSeeds` holds those,
`SeedSalePrice()` is the ladder (forum first, arithmetic second), and
`BelievedSalePrice` ranks both BELOW every observation of the character's own.

Worked example, i_bandage (2026-09-07): `i_bandage VALUE=1` and Sphere sells at
1.15x / buys at 0.85x VALUE, so the NPC price is ~1gp — but a PLAYER-cut
bandage costs one cloth, and `i_cloth VALUE=3`. Seed 3. `CeilingPerUnit`'s
half-again makes the buyer's top offer 4, so the band is 2-4gp with no literal
anywhere in the handler. The NPC's 1gp is not a competing offer because the
whole player route only opens once the counters are DRY.

**How to apply:** when a trade needs a price and there is no forum row and no
observation, derive it from the recipe's inputs at their itemdef VALUE and say
so in the row's `basis` string. Do not put it in the forum table, and do not
hard-code it in a handler — `BelievedSalePrice` is the one place a price is
answered, so the first real trade can overwrite it.

Related: [[thresholds-are-rates-not-numbers]], [[two-sale-questions]].
