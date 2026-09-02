# Materials go to crafters, not counters — evidence (2026-09-02)

Owner ruling (2026-09-02): materials exist to be CRAFTED. NPC sale of a
material is a last resort behind TWO gates; the goal is organic supply/demand
between bots.

Shard DOWN. Everything below is **verified source behaviour plus unit tests**.
No live run confirms any of it. `ctest 41/41`, `m7_market` 279 checks / 0
failures.

---

## 1. Sell side — the surplus cap

`market::MaterialSurplusCap` / `market::MaterialNpcSaleGate`
(`include/uo/market.h`, `src/economy/Market.cpp`).

    cap = ownPlanNeed + trainingStock + marketReserve

| term | source | why it differs per character |
|---|---|---|
| `ownPlanNeed` | `craftBatch` x the largest per-craft demand for the item across the recipes this profession's `produces` list (production graph) | a smith's cutlass eats 8 ingots, a carpenter's board eats 1 log |
| `trainingStock` | `(tenthsRemaining/10) x 5.5` units, for the skill THAT recipe is gated on, only while the build plan still has it to climb | the owner's "stock 500-600 then start training blacksmith" (2026-08-30) expressed as a RATE, so a smith at 70.0 banks 165, not 550 |
| `marketReserve` | `restockConsumablesTo` x `WhoConsumes(item).size()` (new, mirror of `WhoProduces`) | 4 catalogue professions buy logs, 2 buy iron ingots |
| purse | `marketReserve` halved when `gold < profession.goldReserve` | a broke character releases its *market* stock sooner — never its own plan or training stock |

Measured caps (printed by the test, `tests/m7_market.cpp`
`TestMaterialSurplusCapIsPerCharacter`):

    lumberjack_swordsman / i_log        cap =  85  (plan 5  + training 0   + market 80)
    miner_smith          / i_ingot_iron cap = 630  (plan 40 + training 550 + market 40)

The gate: `allowed` requires `playersDeclined` **AND** `held(pack+bank) > cap`.
Below the cap the sale is refused with the numbers in the reason; the goods stay
banked (`NeedBank` "put unsold stock away", Needs.cpp, already existed) and
`DoEarnGold`'s existing `Cooldown` + `Finish(false)` branches run — no NPC trip,
no spin.

Wired at both `MaySellToNpc` call sites in `Runner::DoEarnGold`
(`src/life/Runner.cpp`), via `Runner::MaterialSaleGateFor`, which supplies the
one thing the market layer cannot see: the build plan's remaining tenths
(`state_.plan.skills` vs `obs.SkillTenths`) and pack+bank holdings.

### The trap this nearly walked into

`econ::IsFloorMaterial` grades FISH as WorldGathered
(`src/progression/VendorPolicy.cpp:122-134`), so fish is a "material" by class.
Capping it would have deleted a fisher's entire income to enforce a ruling whose
own list is "logs, boards, ingots, ore, cloth, hides, yarn". The gate therefore
consults the **faucet registry** first: an item with an `faucet::Allowed` route
is a documented tap ("caught fish cook fish then sell") and passes untouched.
Asserted in the test.

Finished goods (`i_dagger`) are not materials and pass untouched.

## 2. Demand side — the WTB shout

Before: a crafter short of materials walked to `britain_bank_2` and stood there
**silently** for the whole 3-minute window. A trade could only start if a
gatherer happened to announce the exact thing somebody happened to need. Supply
had a voice; demand did not.

New:

- `market::FormatBuyWant` / `ParseBuyWant` — `"WTB 20 i_log 4gp"`.
- `market::ChooseBuyWant` — mirror of `ChooseSellOffer`. Reads the same
  `PlayerMarketWants` the buy errand is already scored from; prices at the
  ceiling `ConsiderOffer` would actually accept (forum seed +50%, else
  `blindPriceCeiling`, capped at believed+50% when a price has been observed);
  quantity clamped to what PACK COIN can honour above `goldReserve`. Announces
  nothing when the purse cannot pay — that is the no-spin half.
- `Runner::DoTradeWithPlayer` buyer block: announces on the same
  `kAnnounceIntervalMs` schedule as a seller, within the same `kListenMs` bound.
- `ParseBuyReply` now delegates to `ParseBuyWant`, so a seller does **not** stop
  recognising answers to its own offer once buyers learned to speak first (this
  would have been a silent regression: the old parser read `"20"` as the item).

Fallback when nobody answers: unchanged `no_player_seller` +
`marketQuietUntilMs_`, plus a new `HandOff` to `Mine`/`GatherLogs` when
`market::RouteForInput(...) == SelfProduce` — i.e. only when the life can
genuinely make the thing itself. No NPC purchase of materials was added; most
are WorldProcessed and the vendor policy refuses them, so bank-and-wait remains
the dominant path (that is the plain stand-down, already there).

## 3. Gatherer answers a cold WTB

`market::AnswerBuyWant` (mirror of `ConsiderOffer`) + a new branch in the
`DoTradeWithPlayer` heard loop, guarded on `tradePartner_ == 0`.

- Only from the **pack** — an offer is a promise honourable without a second
  errand, the same rule the announce path follows.
- Only what the profession `produces` (via `Surplus`) — nobody becomes a fence.
- `minimumSurplusToOffer` relaxed to 1 (somebody is standing here asking);
  `keepOfOwnOutput` **not** relaxed (a lumberjack with 20 logs needs them for
  boards; 22 logs answers with 2).
- Declines when the buyer's ceiling is below its own observed price, rather than
  undercutting and teaching the fleet a wrong number.
- Answers with a spoken `WTS`, then the EXISTING machinery runs: the seller
  walks over and opens the window, the buyer's `ConsiderOffer` accepts. Asserted
  end to end in the test ("the buyer accepts the answer it provoked").

## 4. NoteProgress

Unchanged and already correct: both sides only `NoteProgress` inside the
`trade::Phase::Completed` branch, and only after comparing
`market::QtyOf(obs.pack, ...)` against `tradePackBefore_` — goods actually left
(seller) or arrived (buyer). The third branch logs "window completed but nothing
moved" and credits nothing.

## Files changed

    include/uo/market.h
    src/economy/Market.cpp
    src/life/Runner.cpp
    src/life/Runner.h
    tests/m7_market.cpp

## Limitations

- Shard down. No live run. In particular the WTB->answer->window handshake has
  never executed against a server, and the timing (8s announce interval inside a
  180s window, two bots both needing to be at `britain_bank_2`) is untested.
- The `SelfProduce` gather fallback is largely dormant with today's catalogue: a
  smith short of logs routes to `PlayerMarket` (correct — it cannot chop), and a
  lumberjack is never short of logs. It fires only for a life short of something
  it itself produces.
- `MaterialSaleGateFor` uses `obs.gold` (total wealth, includes the bank) for
  the purse term and `obs.goldOnHand` for `PolicyForPurse`. Deliberate, not
  measured against live behaviour.
- The 5.5 units/skill-point rate is derived from one owner statement
  ("500-600" for a full Blacksmithing climb). It has no independent evidence and
  no material other than iron has been checked against it.
