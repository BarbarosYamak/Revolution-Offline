# State reconciliation / goal-lifetime review — 2026-09-05

Read-only audit. Base 621d3d9 + uncommitted working tree (spellbook/pack-cache
workarounds in spellcast.h, Train.cpp, Cloth.cpp, Gear.cpp).

## 1. State model

### Where state lives (`src/Client.h`)

| Data | Field | Type | Line |
|---|---|---|---|
| Container/box/spellbook/vendor-window contents | `containerItems_` | `unordered_map<u32, vector<ContainerItem>>` | Client.h:1717 |
| "Is this container currently open" bookkeeping | `openContainers_` | `vector<OpenContainer>` (serial, gumpId) | Client.h:1716 |
| Backpack-known flag (one-shot, whole session) | `backpackContentsKnown_` | `bool` | set Client.cpp:1531 |
| World items (ground, corpses) | `items_` | `unordered_map<u32, ItemObj>` | — |
| Gold, mana, hp | `player_.gold/manaCur/...` | struct | — |
| Equipment layers | (separate equip map, not audited this pass) | | |

`ContainerItemCount(c)` (Client.cpp:3139) and `ContainerKnown(c)` (Client.cpp:2843)
both reduce to one test: `containerItems_.find(c) != end()`. There is **no
tri-state**. "Never opened", "opened and genuinely empty", and "open just
requested, 0x3C not back yet" are ALL represented as `find() == end()` ->
count()==0. The only positive population path is `OnContainerContents` (0x3C,
Client.cpp:1498-1531); a container that legitimately holds nothing sends no
0x3C at all (Sphere behavior, confirmed by the workaround below), so it can
**never** transition out of the "unknown" bucket by protocol alone.

### Ad-hoc workarounds that exist because the tri-state is missing

1. **Bank.cpp:63-67** — explicit rejected fix, kept as a comment: "Requiring
   `ContainerKnown` ... was wrong twice over: an EMPTY bank box sends no 0x3C
   at all, so the flag never flipped, and the character re-opened the bank
   every 2.5 seconds forever." Deposits are gated on `box` (serial non-zero)
   alone, never on contents-known. This is the cleanest proof that the
   collapsed model is not just an API wart — it made a *correct* looking
   gate infinite-loop.

2. **Train.cpp:1979-1990** (`DoFillSpellbook`) — `spellbookOpened_` bool
   gate: "ContainerItemCount is only meaningful once the server has sent the
   contents ... Treating an unopened book as an empty one would send the
   character shopping for spells it already owns." One extra open-then-wait
   round trip invented locally because the map can't say "pending."

3. **Train.cpp:2160-2186** (`DoPracticeSkill`) — second, independent
   workaround for the SAME cache, worse: `spellbookOpened_` is shared between
   two different goals (`FillSpellbook`, `PracticeSkill`), so "opened at some
   point this session" survives long after `containerItems_[book]` has been
   silently wiped (see #4). The comment documents the exact failure: Aurelius
   read a book cached at 19 items as 0 after an unrelated equip, and
   `PickPracticeSpell` said "nothing safe to cast at myself -- standing
   down" (run_gates/g_Aurelius.console.txt:610-624, 2026-09-05). Fix invented
   locally: `practiceRecheckedBook_`, re-open exactly once before trusting an
   empty read.

4. **Client.cpp:1316-1345** (`OnDeleteObject`) — the root cause #3 is
   patching around: a lift-then-drop of a container the bot itself owns
   (unequip/re-equip a spellbook) sends a 0x1D for the container's *old spot*
   even though the object never left the world. The existing code special-
   cases `ownLift` to skip `containerItems_.erase(serial)` in that one case,
   with an explicit note that this used to wipe a 19-spell cache down to
   nothing on every re-equip. There is no server signal distinguishing
   "destroyed" from "relocating"; the client infers it from whether the lift
   was self-initiated.

5. **spellcast.h `ChoosePracticeSpell`** (working-tree diff) — adds
   `PracticeChoice::manaNeeded` so a stocked-but-mana-blocked spell is not
   reported the same way as a truly empty/unsafe book. Not a container-cache
   issue, but the same family: a boolean/zero result was being asked to carry
   two different meanings ("nothing usable" vs "something usable, blocked by
   a recoverable resource").

6. **Craft.cpp / CraftConfirm.cpp / interaction/progress.h** — this family
   does NOT suffer the same problem: `Verify()` (progress.h:114) takes an
   explicit `itemBefore`/`itemNow` pair and a `NothingChecked` verdict, so
   "not measured" is representable. This is the pattern the container cache
   should be generalized to (see §4).

## 2. Purchase / loot / bank confirmation

`include/uo/interaction/progress.h` (`Expectation` / `Observed` / `Verify`) is
the canonical "world confirms it, not the packet" mechanism, documented as a
direct response to three named regressions (progress.h:12-23): a whole-order
refusal counted as 8 successful buys, an unverified training gold sink, and
6 duplicate heater-shield purchases.

Confirmed as ROUTED THROUGH `Verify`/before-after sampling (no `sent packet =
success` gap found):

- **VendorBuy**: `src/life/VendorErrand.cpp:366-437` (`Step::Verify`) —
  records `packBefore_`/`goldBefore_` before `ActionVendorBuy`
  (VendorErrand.cpp:335-345), then classifies `Confirmed/Contradicted/NotYet`
  against a `verifyDeadlineMs_` window.
- **VendorSell**: `src/life/runner/Economy.cpp:84-178` (verify step) against
  `sellGoldBefore_`/`sellItemBefore_` set at VendorErrand.cpp-style points
  Economy.cpp:756-762 and 818-826, immediately before `ActionVendorSellMany`.
- **Player trade**: `src/life/runner/Economy.cpp:1917-1922`
  (`tradePackBefore_`/`tradeGoldBefore_` sampled before the offer goes in).
- **Craft**: `src/life/activities/CraftConfirm.cpp:175-199` (`ConfirmCraft`,
  pack-count-rose beats the shard's own refusal text beats the deadline —
  explicit ordering comment at CraftConfirm.cpp:178-185).
- **Bank deposit/withdraw**: `src/life/runner/Bank.cpp` uses its own local
  before/after sampling (not routed through the shared `Verify()` struct —
  a duplication of the pattern rather than a gap; see §4 migration note).

No occurrence of "action sent -> immediately mark success" was found in the
buy/sell/trade/craft/bank paths. §18's gap, where it still holds, is narrower
than "no verification at all": it is the container-cache ambiguity in §1
(an unopened/unpopulated container reads identically to a confirmed-empty
one), which corrupts the INPUT to otherwise-correct `Verify()` calls in one
place — `BookHasSpell`/`BookHasGraphic` (Train.cpp:1464-1479) reading the
spellbook via `ContainerItemAt` before the book has ever been opened would
report "spell not known" for a book that simply hasn't answered yet.

## 3. Action vs goal lifetime

Goal-switch reset block: `src/life/runner/Core.cpp:1394-1434`, gated on
`!sameErrand` (i.e. genuinely a NEW `GoalKind`, not a re-pick of the same
kind — Corran regression comment at Core.cpp:1394-1403 explains why re-picks
must NOT reset).

**Explicitly reset on a real goal change:**
`chopTargetValid_`, `chopCursorPending_`, `travelInFlight_`,
`travelAttempts_`, every `lastXxxPlan_` (`lastCombatMove_`, `lastHealPlan_`,
`lastRestPlan_`, `lastRecoveryPlan_`, `lastTrainPlan_`, `lastCraftPlan_`,
`lastBandageAcquirePlan_`, `lastPotionAcquirePlan_`,
`lastGarmentAcquirePlan_`, `lastToolAcquirePlanByItem_`), `vendorChases_`,
`logsAtGoalStart_`. `TrainCombat` additionally calls
`client.TravelAbort(...)` if travel was busy (Core.cpp:1398-1400) — the ONLY
place a goal switch reaches into `Client`/`ClientTravel` at all.

**NOT reset, and confirmed to leak:**

| State | Owner | What happens on goal switch | Evidence |
|---|---|---|---|
| `foodErrand_` (a `life::VendorErrand`) | `Runner` member, `src/life/Runner.h:661` | Nothing. `DoGetFood` (Gear.cpp:1761) only calls `.Begin()` when `!foodErrand_.Running()`; a `GetFood` goal interrupted mid-`Step::Verify` by a higher-priority goal leaves `running_=true` with a stale `packBefore_`/`goldBefore_`/`verifyDeadlineMs_` baseline from the earlier visit. When `GetFood` is re-picked, `Running()==true` skips `Begin()` and resumes the OLD baseline/deadline, which can already be in the past. | Gear.cpp:1761-1770, Runner.h:661, Core.cpp reset block (no `foodErrand_` mention) |
| `Client::action_` (in-flight action) | `Client` | Only cleared if the NEXT action explicitly calls `BeginAction` (supersedes, `InvalidState`) or its own timeout fires via `ActionTick`. A goal switch that issues no new action at all leaves the old one live until its own deadline. | Client.cpp:2784-2789 (`BeginAction` supersede), Client.cpp:2817-2833 (`ActionTick`) |
| `Client::target_` (server-armed target cursor) | `Client` | `target_.OnCancelled()` fires ONLY inside `ActionTick`'s branch for the SAME action's own timeout (Client.cpp:2831-2832). Once a later, unrelated action calls `BeginAction` and supersedes the old `action_`, that branch can never fire again for the old target — `target_` is only overwritten by the NEXT server-sent 0x6C (`OnArmed`, Client.cpp:5289-5297, itself logs "superseded"). If the new goal never triggers a target cursor at all (e.g. cast interrupted, goal moves to Bank/Travel), the stale cursor is inert but never explicitly cancelled by the Runner. | Client.cpp:2817-2833, 5289-5297 |
| `openContainers_` / `containerItems_` for player-owned containers (backpack, bank, spellbook) | `Client` | Never closed by a goal switch. `PurgeOutOfRange` (Client.cpp:1440-1460) explicitly skips these ("Player-owned containers ... have no `items_` entry, so they never match here and stay open"). Only 0x24 gumpId 0xFFFF or an explicit `ForgetBankContainer()`/re-lift clears them. | Client.cpp:1440-1460 comment |
| Held drag (`Client::drag_`) | `Client` | Same shape as `action_`/`target_`: only reset on its own action's timeout (Client.cpp:2826-2830) or on the specific item-serial paths (`OnDeleteObject`, drop handlers). Not reset by `Runner`'s goal-switch block. | Client.cpp:2826-2830 |

**Concrete leak path** (chosen as the example): `GetFood` -> (interrupted by
a higher-priority `Survive`/`Heal` pick while `foodErrand_` is in
`Step::Verify`) -> `GetFood` re-picked later. `foodErrand_.Running()` is
still `true`, so `Gear.cpp:1761`'s `if (!foodErrand_.Running())` skips
`Begin()`, and the errand resumes with `packBefore_`/`goldBefore_` sampled
against the OLD attempt and a `verifyDeadlineMs_` that may already be in the
past — producing an immediate `NoProgress` verdict for a purchase that was
never actually retried this pick.

## 4. Smallest centralization proposal

Add one small type next to the existing `containerItems_` map, not a new
subsystem:

```cpp
enum class ContainerSync : u8 { Unknown, OpenPending, Known };
struct ContainerState {
    ContainerSync sync = ContainerSync::Unknown;
    i64 pendingSinceMs = 0;   // when OpenPending started, for a timeout
    i64 lastSyncMs = 0;       // when Known last got a real 0x3C
};
std::unordered_map<u32, ContainerState> containerSync_;  // sibling of containerItems_
```

- `ActionOpenContainer`/`ActionUseObject` targeting a container-graphic item
  sets `OpenPending` + `pendingSinceMs` immediately (Client.cpp, wherever
  the open request is sent, e.g. near 3803/3813 for the vendor family, and
  the plain double-click path).
- `OnContainerContents` (Client.cpp:1498) sets `Known` + `lastSyncMs` for
  every container it touches — including a container that ends up with an
  EMPTY list, IF Sphere ever does send a zero-count 0x3C for it. Given the
  bank-box counter-evidence in §1 item 1, this alone does not fully solve
  "opened and genuinely empty with no 0x3C at all" — that case needs a second
  positive signal, most naturally the 0x24 Draw Container ack itself
  (`OnDrawContainer`, Client.cpp:1477) promoting `OpenPending -> Known` after
  a short "no 0x3C arrived, and none is coming" grace window, since 0x24
  reliably precedes any contents burst for a container that has items and is
  the only ack at all for one that doesn't.
- `ContainerKnown(c)` becomes `containerSync_[c].sync == Known`;
  `ContainerItemCount`/`ContainerItemAt` stay as-is (still keyed by real
  contents) but callers gain `ContainerSyncState(c)` to branch on
  `OpenPending` explicitly instead of guessing from a bool.

**Handlers that could delete their local rule:**
- Bank.cpp:63-67 — no change needed (it already deliberately avoids gating on
  contents), but the comment could cite `ContainerSyncState` instead of
  re-explaining the 0x3C gap inline.
- Train.cpp `spellbookOpened_` (FillSpellbook, ~1979-1990) — replaced by
  `containerSync_[book].sync != Unknown` (or `== Known` once the 0x24-based
  promotion above exists).
- Train.cpp `practiceRecheckedBook_` (PracticeSkill, ~2160-2186) — the whole
  "shared flag across two goals" bug disappears because sync state is keyed
  by container serial, not by a per-goal bool; a re-open by ANY goal updates
  the one true state both goals read.

**Migration cost:** ~4 files (Client.h struct + map, Client.cpp set-points at
the open-request and 0x3C/0x24 handlers, Train.cpp two call sites, Bank.cpp
comment-only). No signature changes to `ContainerItemCount`/`ContainerItemAt`,
so nothing outside Train.cpp/Bank.cpp needs to change.

**Regression risk:** low-to-moderate. The genuinely uncertain part is the
"0x24 with no following 0x3C means empty, after grace period X" rule for
truly-empty containers — that is inferred from the bank-box comment, not
directly observed for spellbooks/backpacks, and should be confirmed with a
live empty-container open (bank box on a fresh character) before relying on
it for spellbook logic specifically.
