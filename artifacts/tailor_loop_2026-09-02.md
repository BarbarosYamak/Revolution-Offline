# Tailor cloth loop — evidence (2026-09-02)

Shard DOWN. Everything below is **verified source behaviour (Source-X and the
runtime scripts) plus unit tests**. No live run confirms any of it.
`ctest 41/41`, `m4_life` 537 checks / 0 failures.

---

## 1. The blocker the owner asked us to check: THREAD

**CONFIRMED, and it is a real blocker.** Every stock Tailoring recipe on this
runtime reads `RESOURCES=<n> i_cloth,1 i_thread`:

    runtime/scripts/items/i_provisions_clothing.scp
      :47/:48   i_cape        14 i_cloth, 1 i_thread   Tailoring 45.4
      :71/:72   i_skirt_long  10 i_cloth, 1 i_thread   Tailoring 31.8
      :93/:94   i_shirt_plain  8 i_cloth, 1 i_thread   Tailoring 22.7
      :306/:307 i_bandana      2 i_cloth, 1 i_thread   Tailoring 0.1

Thread is spun from COTTON, six per pile, and wool spins to YARN:

    server/Source-X/src/game/clients/CClientTarg.cpp:2053-2086
      case IT_WOOL   -> ITEMID_YARN1,   SetAmountUpdate(3)
      case IT_COTTON -> ITEMID_THREAD1, SetAmountUpdate(6)

`IT_YARN` and `IT_THREAD` share the loom case (:2186), so a wool-only tailor
can **weave cloth** and **cannot sew clothing**.

Per the owner's instruction the loop therefore **produces cloth only**. Nothing
was invented on Sphere's side. Cotton exists as an itemdef
(`items/i_vegetation.scp:3191`, `i_cotton` TYPE=t_cotton) and as a crop plant
(`:169`, `i_crop_cotton` TDATA3=i_COTTON), but no cotton harvesting behaviour is
proven and none was added. `life::IsWoolChainMaterial` deliberately excludes
`i_thread`, and a unit test asserts that exclusion with the reason.

**Open, for a later slice:** prove a cotton source (crop plants vs. spawned
piles) before a tailor can sew anything at all.

## 2. A defect found and fixed on the way: SCISSORS DO NOT SHEAR

`DoMakeBandages` step 5 handed the **scissors** to a sheep, citing
`CClientTarg.cpp:1878, case CREID_SHEEP`. That line is real, but it sits inside

    case IT_CARPENTRY_CHOP / IT_WEAPON_MACE_SHARP / IT_WEAPON_FENCE /
    IT_WEAPON_AXE / IT_WEAPON_SWORD           (CClientTarg.cpp:1866-1900)

and NOT inside `case IT_SCISSORS` (:2135). A sheep is a **character**, so
`pItemTarg` is null and the scissors case falls straight through to
`DEFMSG_ITEMUSE_SCISSORS_USE` — *"Scissors cannot be used on that to produce
anything"*. This shard's `types/type_scissors.scp` hooks `@TargOn_Item` only, so
it does not change that either.

So `MAKE_BANDAGES` could never obtain wool from a live sheep. Fixed by
`FindBlade` (pack **and** hands, since a fighter walks to a pasture with its
katana wielded); both goals now use it. Graphics are this shard's own itemdefs,
base + `DUPELIST` flip, verified in `runtime/scripts/items/weapons/i_weapons.scp`:
dagger 0F51/0F52, butcher knife 13F6/13F7, skinning knife 0EC4/0EC5, hatchet
0F43/0F44, axe 0F49/0F4A, katana 13FE/13FF, kryss 1400/1401, cutlass 1440/1441,
scimitar 13B5/13B6.

## 3. Where the sheep are — derived, never reasoned

`tools/pasturegen.py` (new) reads every `[WORLDCHAR c_sheep*]` out of
`runtime/save/sphereworld.scp`, single-link clusters at 24 tiles, drops flocks
under four animals, and writes `data/revolution_pastures.tsv`. 246 woolly sheep
→ 4 pastures:

    572,1096 (15)   677,1177 (15)   681,945 (15)   5159,3915 (7)

The three Yew flocks agree with the figures already in the Runner.cpp comment.
The runtime table is loaded in `LoadPastures`, exactly like
`revolution_creatures.tsv`. **Regenerate after any spawn edit** — like the
atlas, it goes stale. The old hard-coded `kPastures[]` remains only in
`DoMakeBandages` and is now redundant; it was left alone as out of scope.

The stations are the atlas's own PLACE id `britain_tailor_2`
(`data/revolution_atlas.txt:2116`, 1467,1686), with `TravelToService(Tailor)` as
the fallback for a life nowhere near Britain.

## 4. Chain numbers, re-read from the engine

    blade on woolly sheep -> 1 wool     CClientTarg.cpp:1880 (CREID_SHEEP);
                                        sheep flips to CREID_SHEEP_SHORN
                                        (body 0x00DF), wool regrows on
                                        WoolGrowthTime (30 min), and a second
                                        attempt answers WWAIT (:1894)
    wool on spinning wheel -> 3 yarn    :2053
    yarn on loom           -> 1 bolt    :2186; ConsumeAmount(iNeed) at :2235
                                        takes up to FOUR from the stack in ONE
                                        gesture, so 4 yarn is one click
    scissors on bolt       -> 50 cloth  :2147, ConvertBolttoCloth

Two sheep therefore fund one bolt (2 wool -> 6 yarn -> 1 bolt + 2 spare) and one
bolt funds 50 cloth. Asserted in `TestTheWoolChainBookkeeping`.

## 5. The decision path

    NeedCloth (Needs.cpp, in the crafting block)
      fires when the chosen recipe's `missing` list holds a wool-chain
      material (wool / yarn / bolt / cloth -- NOT thread)
      urgency 0.0 and BLOCKED   while obs.NoSellerFor(item) is false
      urgency 0.15 + 0.40*frac  once it is true

    Observation::noSellerFor  (life.h, filled in Runner::Observe)
      the `no_player_seller` memory events DoTradeWithPlayer already writes
      when a WTB window expires unanswered, bounded by kPlayerWindowMemoryMs
      (one hour). `marketQuiet` is the same fact with the item thrown away,
      which is why it could not be reused: a tailor that failed to buy LOGS
      has learnt nothing about cloth.

    MAKE_CLOTH (Goals.cpp, GoalFamily::Work, weight 135)
      between CRAFT (130) and BUY_SUPPLIES (140), for the reason BUY_SUPPLIES
      already sits above CRAFT.

    Runner::DoMakeCloth
      bolt->cloth, yarn->loom (only at >= 4), wool->wheel, sheep->shear,
      else walk to the next pasture in the save-derived table.

**NoteProgress only on a real inventory delta.** Four counts (wool / yarn /
bolts / cloth) are marked before every gesture and compared on the next tick;
the mark is discarded if it is more than 30 s old, so a goal that lost and
regained the turn cannot read an unrelated purchase as its own progress. Three
gestures in a row that move nothing log `goal_failed=MAKE_CLOTH` and take a
five-minute `Planner::Cooldown` + `Finish(false)` — the escalate-after-three
rule, and the goal-that-did-nothing-must-stand-down rule together.

Sphere's re-shear refusal is treated as **move to the next sheep**, not as
failure: the sheared serial is remembered and the body flip to 0x00DF drops it
out of `NearestMobileWithBody` anyway.

## 6. Also fixed: the wrong gather receiver

`DoTradeWithPlayer`'s `no_player_seller` fallback computed
`gathers == "ore" ? Mine : GatherLogs`. A tailor gathers **wool**, so a tailor
short of `i_cloth_bolt` (which it produces, hence `SupplyRoute::SelfProduce`)
was handed an axe and sent to the forest. Now routes to `MAKE_CLOTH`.

## 7. Limitations — read before trusting any of this

- **Shard down; nothing here has executed against a server.** In particular no
  bot has ever sheared a sheep, spun a wheel or woven at a loom on this shard.
- **No action confirmation exists for the wheel or the loom.** Both are
  double-click-plus-target and answer with a SysMessage only — no 0x7C menu, no
  0x9F, nothing `Client::ActionBusy()` can wait on. This slice compensates with
  inventory deltas and a 3 s pacing gap; a proper confirmation
  (journal watch on `DEFMSG_ITEMUSE_WOOL_CREATE` / the loom's `BOLT_1..5`
  ladder) is a **bot-core follow-up**. `Client.cpp` was not touched.
- Whether the client's mobile cache learns of the sheep's body flip promptly is
  a hypothesis: `Client.cpp:1812` does update `m.body` from an incoming update,
  and Sphere's `SetID` calls `Update()`, but the timing is unmeasured. The
  remembered-serial list is the backstop.
- `data/revolution_pastures.tsv` is a snapshot of the save at generation time.
- The 4-yarn loom gesture is read from the engine, not measured live; if the
  runtime overrides `t_loom` in script the count could differ (no override was
  found under `runtime/scripts/types/`).
- `DoMakeBandages` still carries its own hard-coded `kPastures[]`. Out of scope
  for this change; it should be pointed at the same table.

## Files changed

    include/uo/life.h
    src/life/Needs.cpp
    src/life/Goals.cpp
    src/life/Runner.h
    src/life/Runner.cpp
    tests/m4_life.cpp
    tools/pasturegen.py            (new)
    data/revolution_pastures.tsv   (new, generated)
