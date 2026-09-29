# Wave 2 clusters 7 & 8 + crafter rotation — evidence, 2026-09-01

Shard DOWN. Everything below is verified SOURCE behaviour or verified LOG
evidence from the wave-2 run_gates captures. No live re-run was possible.

## A. Cluster 7 — "this life makes nothing sellable"

### Root cause (verified, source)

`src/life/Identity.cpp` ChooseCraft gated every entry of a profession's
`produces` on `faucet::AllowedForItem(made)`. That function
(`src/economy/Faucets.cpp:386`) returns only faucets whose `Policy` passes
`faucet::Allowed()` — i.e. it answers **"will an NPC pay for this"**.

`Policy::RefusePlayerMarket` and `Policy::RefuseAuthenticity` both refuse the
NPC and explicitly KEEP the player market open; their own reason strings say so
(`Faucets.cpp:254` "Player-market usage stays valid"; `:247` "A smith without an
NPC faucet has to reach players"). Items with no row at all were also skipped.

Result, per life:

| life | produces | route each entry had | outcome |
|---|---|---|---|
| tailor | i_cloth_bolt, i_sash, i_robe, i_leather_tunic | no row / no row / RefusePlayerMarket / no row | 0 candidates |
| merchant_tinker | i_gears + 9 tools | RefusePlayerMarket / no row | 0 candidates |
| lumberjack_swordsman | i_log, i_board, i_parchment, i_scroll_blank, i_club | gathered / RefusePlayerMarket / no row / RefusePlayerMarket / RefuseAuthenticity | 0 candidates |

So `ChooseCraft` fell through to `Identity.cpp:458`, `NeedCraft` was added with
urgency 0.0 and `blocked=true` (`Needs.cpp:833-837`), forever.

Log confirmation (`tools/log_slice.py`):
`run_gates/g_Aelia.console.txt:1433,1450,1501,1548,1585` —
`BLOCKED_NEED CRAFT: this life makes nothing sellable (nothing to make)`
repeated while every other need sat on an anti-spin cooldown.

The comment that justified the gate (`Identity.cpp`, old text) said a
player-market good would qualify "once the player market can actually complete
a sale; it cannot yet". That premise is now false: Tarath→Durnholde, 48 logs
for 40gp, 2026-08-30; and this very wave logged
`goal_completed=TRADE_WITH_PLAYER` for Elvar (x4) and Odessa (x6).

Second, smaller defect found while fixing it: the tinker faucet row carried
`profession = "tinker"`, which matches no catalogue id — the entry in
`Professions.cpp` is `merchant_tinker`. Invisible to `AllowedForItem`, not to
anything that joins on the profession.

### Fix

* `include/uo/faucets.h` / `src/economy/Faucets.cpp`: new
  `faucet::SaleRoute RouteForItem(item)` — `Npc` / `PlayerMarket` /
  `Unrecorded` / `None` — and `faucet::OutputClassIsPlayerMarket(professionId)`.
  Deliberately two functions: the class rule alone would say yes to anything,
  so the caller must also have established the item is that trade's work.
* `src/life/Identity.cpp`: the recipe lookup now runs first, and a product is
  craftable when its route is `Npc` or `PlayerMarket`, or when it is
  `Unrecorded` **and** it is a recipe on this life's own `produces` **and** the
  life's output class is a player-market good.
* `src/economy/Faucets.cpp`: tinker row `profession` → `merchant_tinker`.
* No NPC counter was opened. `AllowedForItem` is unchanged, and selling still
  goes through it (`Market.cpp:615`, `MaySellToNpc`).

### Verified result (unit, `m5_professions`)

```
  tailor               -> i_cloth_bolt     (inputs are short) [short of an input]
  merchant_tinker      -> i_gears          (every input is in the pack)
  lumberjack_swordsman -> i_board          (every input is in the pack)
```

* lumberjack: complete loop — chop logs → i_board (Carpentry 0.0) → sell to a
  player. Also unblocks i_club and i_scroll_blank as it gains Carpentry.
* merchant_tinker: complete loop — i_gears from 1 ingot at Tinkering 14.7.
* tailor: **partial**. It now reaches for i_cloth_bolt and reports a legible
  input shortfall instead of "nothing to make", but its whole chain
  (wool → spinning wheel → yarn → loom → bolt → scissors → cloth, plus thread
  from cotton) is `VendorClass::WorldProcessed`
  (`VendorPolicy.cpp:130-131`, refusal "a station produces this; go and process
  it"), so no NPC can sell it the shortfall and no goal implements wool
  shearing or the spinning wheel / loom. Cluster 7 is cleared for the tailor;
  the tailor's *earn loop* is not yet closed. Reported, not papered over.

## B. Cluster 8 — goal progress telemetry

### What `progress` measures (verified, source)

`Goal::progress` (`include/uo/life.h:945`) is an unbounded counter incremented
by `Planner::NoteProgress()` (`Goals.cpp:654`) and reset to 0 only when the
planner ACTIVATES a goal (`Goals.cpp:546`, `:643`). It is not a percentage and
not an item count, so 101 and 243 are "NoteProgress was called that many times
in one goal instance" — the telemetry is not nonsense, it is an internal
counter printed as if it were an achievement (`Runner.cpp:2676`).

### Root cause of the inflated values

`Runner::ArmAxe` (`Runner.cpp:1638-1640`) returns `true` for two different
things: "I issued an unequip/equip" and "an action is already in flight, come
back later". `DoReplaceEquipment` credited BOTH with `NoteProgress()`, so the
tick rate was counted as work.

Measured, Xerxes: `run_gates/g_Xerxes.console.txt:72` cut the resurrection robe
at 18:08:57.081; `:86` shows `use_item_on timeout` at 18:09:12.099 — 15.0s in
flight; `:105` `goal_completed=REPLACE_EQUIPMENT progress=243` — one per ~60ms
tick of that wait, having armed nothing. Illyria shows the same shape behind
her 26 `cast_spell timeout`s: `g_Illyria.console.txt:152` (101), `:331` (110),
`:461` (70).

Worse than the wrong number: `NoteProgress()` also clears `goal_.attempts`
(`Goals.cpp:659`), so the failure ladder was reset on every tick and the goal
could never run out of tries.

### Root cause of the zero-progress completions

`DoReplaceEquipment`'s only genuine success exit is the "all three plans Done"
early-out. The tail of the function returned `true` unconditionally, so a pass
that bought nothing still logged `goal_completed`. Aelia, 15 times, progress=0,
with 0 gold against a healer quoting 30 — the identical line is visible for
Illyria at `g_Illyria.console.txt:150-152`:
`potions: 0 gold with a floor of 50 cannot buy one heal potion at 30` →
`potions: none bought` → `goal_completed=REPLACE_EQUIPMENT progress=0`.

Aelia's `EARN_GOLD progress=0 x15` is **not** a separate goal bug: DoEarnGold
already stands down correctly (`Runner.cpp:5998-6004`) and the cooldowns are in
her log (`g_Aelia.console.txt:1447,1498,1545`). She had nothing to sell because
of cluster 7. Fixing A removes it.

### Fix

* `Runner.cpp` DoReplaceEquipment: `NoteProgress()` only when `ArmAxe` actually
  issued something (`ActionBusy()` sampled before the call).
* `Runner.cpp` DoReplaceEquipment tail: log why, `planner_.Cooldown(...)`,
  `planner_.Finish(false, ...)`, `return false` — same rule DoEarnGold follows.

## C. Craft rotation (owner rule, 2026-09-01)

`Planner::Satiation` / `FamilySatiation` damp a GOAL and a FAMILY. `Craft` is a
single `GoalKind`, so neither can see the choice made inside it: `ChooseCraft`
returns the first workable entry of `produces`, in list order, forever.

Smallest mechanism consistent with that model — the same shape one level down:

* `life::CraftFocus` (`include/uo/life.h`, `src/life/Identity.cpp`): records
  the last product and its run of consecutive **sittings**; `kFocusRun = 4`,
  fading over `kFocusFadeMs = 3min` (the same window `Planner::kSatiationMs`
  uses). A different product breaks the streak, exactly as `Planner::NoteRan`
  does.
* `ChooseCraft` gains an optional `const CraftFocus*`. A fully-stocked recipe
  whose product is satiated is remembered but skipped, and returned only if
  nothing else is workable — bounded, like every other satiation here.
* `NeedConfig::craftFocus` so the need model asks the same question the errand
  answers.
* `Runner` owns `craftFocus_` (session-scoped, not persisted — the rule is
  about the shape of a day) and calls `NoteMade` when a sitting ENDS
  (`CraftStep::Done`), not per piece, so the focus cannot flip mid-batch. New
  log line `craft_focus=<item> sittings_in_a_row=<n>`.

~120 lines including comments and tests, so it was implemented rather than
written up as a design.

## Gates

`python tools/rev.py build test` → build OK (uo_client.exe relinked
2026-09-01 23:57), ctest **40/40 passed**; `m5_professions` 601 checks,
0 failures.

New tests, all in `tests/m5_professions.cpp`:
`TestEveryProducingLifeHasSomewhereToSell` (catalogue-wide regression guard),
`TestThePlayerMarketIsADestination`, `TestTheThreeStuckLivesCanNowWork`,
`TestCraftFocusRotates`.

## Not proven

* No live run: shard is down. B's fixes are proven by source reading against
  the captured logs, not by a fresh capture.
* The tailor's production chain (wool/spinning wheel/loom) has no goal.
* Illyria's exact 101/110/70 could not be attributed line-by-line from the
  truncated capture; the ArmAxe mechanism is proven from Xerxes' 15.0s window
  and matches Illyria's shape.
