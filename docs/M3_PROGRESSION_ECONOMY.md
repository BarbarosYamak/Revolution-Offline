# M3 — Revolution Progression, Skill Training and Economy Foundations

Date: 2026-08-26. A character created under the shard's own constraints can now earn resources by playing, spend the proceeds, train a real skill, and hand goods to another player for coin — with the server deciding every one of those outcomes.

Companion document: **`docs/REVOLUTION_GAMEPLAY_TRUTH.md`**, the evidence-graded reference this milestone produced. Findings live there; this document records what was built and what happened.

Source-X modifications: **0**. Scripts-X modifications: **0**. Runtime compatibility configuration: unchanged from M2.5 (maps 2–5 stay disabled).

---

## 1. Baseline

| | |
|---|---|
| Client branch | `revolution-sphere-m1` |
| Entering M3 at | `544ca23` (M2.5 PARTIAL) |
| Runtime scripts | `revolution-runtime` @ `dc20378` |
| Phase 0 debt fixes | `512d5dc` |
| M3 progression/trade code | `defef4d` |
| M3 economy/trade/vendor fixes | `34257a2` |

---

## 2. Phase 0 — M2.5 technical-debt triage

Every one of the ten items in `docs/M2_5_WORLD_NAVIGATION.md` §17, with its exact text, a classification, and why.

### BLOCKS_M3 — fixed, tested live, and committed before any progression work started

> **5. The blank-rune purchase is unproven end to end** (§10), and the Britain mage shop's upper storey is a place the tile A* can climb into and not reliably back out of.

**BLOCKS_M3.** The second half is the blocker, and it is fatal rather than cosmetic. `path_probe` confirms that from the Mage Tower's upper floor the walker reaches 179 cells and **none of them is the ground**, so *every* destination returns "unreachable", permanently. A training loop that is supposed to run for hours and periodically go shopping cannot survive a state it can never leave. Fixed with a bounded escape rung: when the world route is exhausted, walk to the nearest navgrid anchor and restart the trip from there — three attempts at three different anchors, then a clean failure. `RoutePlanner::EscapeCandidates` is unit-tested; the rung fired live in the two-session obstacle run (escapes at (632,824), (616,840), (632,840), clean failure after 59 s, next destination accepted and reached).

*The first half — the unproven rune purchase — is M3's own subject matter and is answered in §7.*

> **8. Teleporters are represented but not proven live.** 450 nodes are in the graph and the walk-on model is implemented; no scenario has crossed one yet.

**BLOCKS_M3.** This read as a verification gap and was actually a live hazard. `RouteOptions::allowTeleporters` defaults to **true**, so every M3 journey could route through a pad, and `maps/map0/map0_teleports_ml.scp:9` puts one at **(535,992) — inside Yew** — that sends you to Heartwood at (6985,340). An ordinary walk through town could therefore fling a training bot across the continent mid-errand. Fixed: transit entry tiles the current trip did not choose are impassable to the tile A*, recomputed per leg. Proven live — the Mage Tower → Britain bank route logs `avoiding 2 teleporter pad(s)` and walks around the Britain Sewer pads at (1491,1640)/(1491,1642).

### FIX_DURING_M3

> **4. `Journey` is 2-D.** Floors are handled at the client edge (§8.6) rather than in the plan. A building whose storeys need different macro routes is not modelled.

**FIX_DURING_M3.** Partly addressed as a consequence of item 5 and of M3's own vendor work: the final leg pins the destination's floor, entity approach pins the target's, `TravelFinish` refuses to report success from the wrong storey, and the escape rung recovers from a pocket. A genuinely 3-D macro grid is still not built, and M3 shows why it will eventually matter — Britannia's shops are two and three storeys and speech range is three-dimensional (§7). Not a blocker: the client-edge handling covers every case M3 exercised.

### SAFE_TO_DEFER

> **1. Cell crossings are symmetric.** `BuildEdges` records one bit per direction pair. A genuine one-way drop is therefore modelled as two-way; the journey recovers, but the route is briefly wrong.

**SAFE_TO_DEFER.** Self-correcting by construction — a leg the walker refuses is retried and then replanned around. Cost is seconds, not correctness, and it cannot corrupt inventory, gold or progression state.

> **2. The navgrid regenerates wholesale.** No incremental update when client data changes.

**SAFE_TO_DEFER.** Offline tooling. 80 seconds, once, and only when the client data changes.

> **3. Route planning is synchronous.** See §16.

**SAFE_TO_DEFER.** ~1 ms for a cross-continent plan inside a 50 ms tick. M3's longest run planned dozens of routes without a visible hitch.

> **6. `expect_region` is containment-only.** Correct, but it means "am I in Yew?" is false while standing on the Yew moongate, which sits in its own AREADEF outside the town. Scenario 3b asserts the gate instead.

**SAFE_TO_DEFER.** Test vocabulary, not behaviour, and the current behaviour is the correct one.

> **7. Danger is unused by the planner.** It is recorded and queryable; no route cost consumes it yet.

**SAFE_TO_DEFER.** M3 travels between towns and docks. Danger costing belongs with the PvM/corpse work it exists for.

> **9. Trammel (map 1) remains enabled and unexamined** (§2).

**SAFE_TO_DEFER.** The atlas is map 0 only and the generator skips every non-map-0 row, so nothing M3 does can route across facets. Worth examining when facet travel becomes a subject.

> **10. One action in flight per session** (M2 debt, unchanged).

**SAFE_TO_DEFER — with a measured cost.** Everything M3 does is sequential, so it is not a correctness problem. It did cost throughput: a meditation attempt blocked the action slot for the full 8-second deadline until the outcome message was classified (§5.2), which was a 4× slowdown on the cheapest training action there is. Classifying the message fixed the symptom; a second action slot would fix the cause, and a bot that wants to walk while casting will need it.

### Re-verification

All eight M2.5 scenarios were re-run green on the Phase 0 build before progression work began:

| Scenario | Errors | Rejects | Note |
|---|---|---|---|
| semantic service | 0 | 0 | |
| cross-region (Britain → Yew) | 0 | 2 | both a door bump at the Britain bank, both recovered by the M1.5 OpenDoor/reroute path |
| moongate | 0 | 0 | one gate use |
| run policy | 0 | 0 | |
| war / peace | 0 | 0 | |
| dynamic obstacle (2 sessions) | 0 | 0 | 3 escape attempts, bounded |
| stuck recovery | 0 | 0 | |
| entity approach | 0 | 0 | |

Four of them were run again on the **final** M3 build, after the trade and vendor-speech changes:

| Scenario | Errors | Rejects | Finished |
|---|---|---|---|
| `m25_service_bank` | 0 | 0 | yes |
| `m25_war_peace` | 0 | 0 | yes |
| `m25_entity` | 0 | 0 | yes |
| `m25_escape` | 0 | 0 | yes |

(`m25_escape` needed one rerun: the harness's 420-second cap cut it off mid-route on the first pass, with its two move rejects already recovered. Given 900 seconds it finished clean.)

The first attempt at this sweep failed for a reason that became a finding rather than a defect: four back-to-back logins pushed the session past `MaxConnectRequestsPerIP` and the shard banned the address, then **re-banned it on every retry**. See §10.

---

## 3. What was built

All protocol-free, all unit-tested, all per-session:

| Unit | Holds |
|---|---|
| `include/uo/progression.h` | `CharacterBuild` / `SkillGoal` / `TrainingNeed` / `ProgressionPlan`, `CapRules`, `ResourceNeed` / `Budget` / `PurchaseNeed`, and `TrainingSession` |
| `include/uo/trade.h` | `TradeState` — the secure-trade state machine |
| `src/progression/Progression.cpp`, `Trade.cpp` | the logic behind both |
| `src/progression/ClientProgression.cpp` | skill/stat accessors reading only what the server sent |
| `src/progression/ClientTrade.cpp` | the 0x6F wire layer |
| `Client::AddressMobile` | "<name> <keyword>", so one named NPC answers instead of the nearest one (§8) |
| `tests/m3_progression.cpp` | 110 checks |

**Not a class system.** A build is a list of skill targets, so a hybrid — Mining + Blacksmithy + Alchemy + Magery on one character — is expressed exactly like a fighter, and capability follows from actual skills, stats and inventory rather than from a profession enum. The unit tests use that hybrid as their worked example on purpose.

`TrainingSession` carries the boundedness that long runs need: a refusal streak ends in `Blocked` rather than in more requests, and a throttle backs off for a minute rather than retrying — M2 measured that retrying into Sphere's flood protection re-arms its ~300 s TTL, so trying harder is strictly worse than waiting.

---

## 4. The Revolution truth audit

Full findings, with sources and evidence grades, are in **`docs/REVOLUTION_GAMEPLAY_TRUTH.md`**. The headlines:

* **The client ships 49 skills (0–48); the server defines and reports 58.** Skills 49–57 are post-AoS and invisible to a 2.0.3 client. Do not build on them.
* **Resisting Spells is enabled**, fully defined, capped like everything else, and appears in the live skill list. The memory that Revolution lacked it is **not corroborated by anything in this data set, and not refuted either** — the runtime is stock Scripts-X. Recorded as an open discrepancy; the practical rule is not to put it in a build merely because stock UO templates do.
* **The total skill cap is 1000.0, not 700.** `[SKILLCLASS 0] SKILLSUM=10000`, per-skill 100.0, `STATSUM=300`, per-stat 100. The project's own `CLAUDE.md` says 700; the runtime disagrees. Open discrepancy.
* **A new character starts with two thirds of the cap already spent.** Creation clamps the three chosen skills to 50 each / 100 total, then `MaxBaseSkill=200` hands out a random value up to 20.0 in *every other skill*. Measured: trained sum **665.0** (Fisher) and **743.5** (Mage) at birth, of a 1000.0 cap. Reaching a five-GM-skill build therefore requires *losing* junk skill, which `PlanProgression` reports as `needsSkillLoss`.
* **Stats start at 30/30/20 = 80** of 300.
* **Not every skill can be started from the skill list.** `Event_Skill_Use` handles only the gump-clickable ones; Fishing, Mining, Lumberjacking, Blacksmithy and Healing answer *"There is no such skill."* The tool is the verb.
* **Gain chance comes from `ADV_RATE` alone, not from task difficulty**, and **failure trains as well as success**.
* **No anti-macro system exists** in Source-X or in `runtime/scripts` — no gain windows, no repeated-target suppression, no AFK checks. Recorded as `PLAYER_MEMORY` vs `CURRENT_RUNTIME`; nothing was invented to replace it.

---

## 5. Skill training, measured

### 5.1 The arithmetic

Magery's `ADV_RATE=10.0,200.0,800.0` means uses-per-0.1-gain at 0/50/100 skill, which `CValueCurveDef::GetChancePercent` turns into `100000 / rate`:

| Magery | gain chance per use |
|---|---|
| 50.0 | 5.0 % |
| 60.0 | 3.1 % |

50.0 → 60.0 is 100 gains ≈ **2,500 casts**.

### 5.2 Mana is the limit, and it is flat

`Regen1=20` and `CChar::Stats_GetRegenVal` has **no INT term**: 1 mana per 20 seconds for everyone, 180 an hour, whatever your build. Measured live to the second — the first training block settled into one 4-mana Night Sight every **80 s**, waits landing at 15:30:24, 15:31:44, 15:33:04, 15:34:25, 15:35:44.

**45 casts/hour × 5 % = 0.225 Magery/hour, so Magery 50 → 60 is roughly 44 hours** at the passive rate. That is the "skill farming was hard" memory in numbers, and it is a mana limit rather than a dice limit.

Meditation is the way out — `Skill_Meditation` grants a mana point per stroke on a 2.0→1.0 s timer, roughly tenfold — but it **fails at low skill**: measured at Meditation 3.6, *"You lose your concentration"* two seconds in, every attempt, with mana still crawling at 19.3 s/point. And the OSI **passive** meditation bonus is present in `skill46_meditation.scp` as `f_meditation_setup` with **every call site commented out**, so there is no passive component on this shard at all.

The legitimate way out of that hole costs nothing: a failed skill use still earns experience, so failed meditation attempts train Meditation. At 3.6 the gain chance is ~42 % per attempt and an attempt is two seconds.

### 5.3 What actually happened

`m3_magery_training` alternates a meditation attempt with a Night Sight cast, so mana waits are spent training Meditation instead of standing still. Server-reported values, from the shard's own 0x3A:

| | Before (15:27) | ~1 h in (16:43) | ~1 h 45 m in (17:28) |
|---|---|---|---|
| **Magery (25)** | **50.0** | **50.5** | **50.9** |
| **Meditation (46)** | **3.6** | **9.5** | **12.7** |
| Trained sum | 743.5 | 749.9 | 753.5 |

Over the 45 minutes between the last two samples: Magery **+0.4 (≈ 0.53/hour)**, Meditation **+3.2 (≈ 4.3/hour)**. The two rates differ by a factor of eight for the reason §3.2 gives — the gain chance falls as the skill rises, and Meditation is starting from nearly nothing while Magery is halfway up.

Meditation's success rate rose with it: in the second block, **229 trance attempts, 163 losses of concentration — 66 successes (29 %)**, against ~3 % at Meditation 3.6. Cast throughput rose with that in turn: **126 casts in 40 minutes (189/hour)** against the 45/hour passive ceiling.

> **M3.6 correction (2026-08-26).** Two claims in this section are superseded.
> **(1) The training action was historically wrong.** RevolutionUO's own guide
> puts Magery 40–60 on **Greater Heal**; Night Sight is the 0–30 band. This
> block remains a valid proof that legitimate skill gain works, and is
> reclassified as
> `LEGITIMATE_SKILL_GAIN_PROOF_BUT_WRONG_HISTORICAL_TRAINING_ACTION`. The
> ~0.53/hour rate is **not** an authentic figure for this band and the ~17-hour
> projection derived from it is withdrawn.
> **(2) No reagents were consumed.** `runtime/sphere.ini:1060` sets
> `ReagentsRequired=0`; the character owns no reagents at all, which is how it
> cast 214 times. The figure below was inferred from the spell's `RESOURCES`
> line rather than measured. See `M3_6_PROGRESSION_RUNebook.md` §3 and §10.

Totals over the block: **214 successful casts**, **245 meditation attempts**, and **246** *"You lack sufficient mana"* refusals — the last figure being the honest measure of how much of a mage's day is spent waiting (§5.2). Night Sight is `RESOURCES=i_reag_spider_silk,i_reag_sulfur_ash`, so the block consumed roughly **214 spider silk and 214 sulfurous ash** from the character's own newbie stock; nothing was replenished.

**No skill was set, no gain rate was altered, no reagent or coin was created.** Every number above is the server's.

### 5.4 Magery ≥ 60: not reached, and why

Magery finished the session at **50.9**, up 0.9 from 50.0. At the measured **0.53 Magery/hour** — improving as Meditation climbs, but from a very low base — the remaining 9.1 points is **about seventeen hours of continuous casting**, and the passive-only figure is ~44 hours. That is a property of the shard's configuration, not of the client: nothing in the training loop is slower than a human doing the same thing, and the loop is polite to the server throughout.

This is reported as **not reached**, with the measured rate and the extrapolation, rather than shortened by any means. Raising Magery directly, editing `ADV_RATE`, granting mana, or using a GM command would each have satisfied the letter of the gate and destroyed the point of it.

**Consequence: Mark and Recall remain blocked** (§7).

---

## 6. Economy: the income loop

### 6.1 The character

`RevolutionFisher`, created through the ordinary `0x00` packet asking for Fishing 50 / Mining 30 / Blacksmithy 20 — a hybrid gatherer, not a profession. The server granted exactly that (`18 base=50.0`, `45 base=30.0`, `7 base=20.0`) and supplied the kit from its own newbie templates: a fishing pole, a pickaxe, tongs, 50 iron ingots, an apron, and **1000 gold**. That gold is the only money the income proof starts with, and none of it was added by anyone.

### 6.2 What fishing actually required

Three live failures, each a real finding, shaped the working loop:

1. **`skill 18` does not fish.** *"There is no such skill."* Fishing is started by **using the pole**.
2. **The pole is worn, not carried.** Sphere equips it — `i_fishing_pole` has `SKILL=Fencing`, so it is a weapon — and a backpack-only search finds nothing. Hence the `carried_graphic` resolver.
3. **Most water has no fish.** `[REGIONTYPE r_default_water]` is `RESOURCES=60.0 mr_nothing` against 10.0 for each of four fish, per tile, regenerating on a **ten-hour** timer. *"There are no fish here"* is a 60 % roll on that tile and it will keep saying it all day. **A fisherman has to move**, which is what the scenario does — three passes down twelve tiles, chosen by searching the client's own data for a standable tile with the most water inside Fishing's `RANGE=4`.

A fourth: Sphere rolls **1–2 strokes** per gather, so a cast can take 16 seconds and a shorter wait cancels it outright (*"You pull your line back in and stop fishing"*).

### 6.3 Gathering, live

`m3_income6`: 36 casts across 12 tiles → **11 barren tiles, 8 fished-and-failed, 8 successful catches**, and the pack went from **0 to 14 fish** (a catch yields `REAPAMOUNT=1,3`). The travel was semantic throughout: `travel_service fisherman` from Yew planned a **44-leg, 1744-tile** route to the Britain docks by itself.

`m3_income7` repeated it and found the fifth thing fishing requires: **a fisherman gets heavy.** `i_fish_big_1..4` is `WEIGHT=5.0`, so on a STR 30 character eleven of the run's catches landed as *"You put the fish at your feet. It is too heavy."* and stayed on the dock. Encumbrance is a real constraint on a gathering bot, and it is listed as debt rather than papered over.

### 6.4 Selling, live — and the processing step that is worth eight times as much

The catch was sold twice over, two different ways, because the shard prices them very differently.

**Whole fish to the fisherman.** `m3_sell2`: four fish to Shika the fisherwoman on the Britain docks — *"Here you are, 4 gold coins. I thank thee for thy business."* **1 gp per fish**, gold 1000 → 1004. That closes the loop end to end: travel by need → fish → sell → gold up, with nothing granted.

**Steaks to the cook.** Cutting a fish with any blade multiplies the stack by **four** — Source-X `CClientTarg.cpp:1950`, `pItemTarg->SetAmount(4 * pItemTarg->GetAmount())`. `m3_cut1` carved the remaining twelve fish into **48 raw fish steaks** with a dagger the character already owned, walked to Cassiel the cook, and sold 24 of them for **48 gold** at **2 gp each** — gold 1004 → 1052, `gold gain confirmed`.

| route | per fish | weight per fish | buyer |
|---|---|---|---|
| sell whole | **1 gp** | 5.0 stones | fisherman, cook |
| cut, sell steaks | **8 gp** (4 × 2 gp) | 0.4 stones | **cook only** |

So processing is worth **eight times** the raw good and one twelfth the weight, which also dissolves the encumbrance problem in §6.3.

**Every coin reconciles.** `RevolutionFisher` started with the 1000 gold its newbie template gave it and finished on **1102**:

| | Δ | running |
|---|---|---|
| newbie template (`MALE_DEFAULT`) | +1000 | 1000 |
| 4 whole fish → Shika the fisherwoman (`m3_sell2`) | **+4** | 1004 |
| 24 raw steaks → Cassiel the cook (`m3_cut1`) | **+48** | 1052 |
| steaks → `RevolutionMedic` (`m3_trade4`, completed server-side) | **+25** | 1077 |
| steaks → `RevolutionMedic` (`m3_trade5`) | **+25** | 1102 |

**+102 gold, all of it earned**, and no unexplained delta anywhere in the ledger. Nothing was added, and the one purchase attempted spent nothing (§14, item 7).

**And cooking them would be a mistake**, which is worth stating because general UO instinct says otherwise. On this shard's own tables:

| item | `VALUE` | appears in any `BUY=` line? |
|---|---|---|
| `i_fish_big_1..4` (whole) | 2 | yes — `VENDOR_B_FISHER`, `VENDOR_B_COOK` |
| `i_fish_cut_raw` (steak, raw) | **3** | yes — `VENDOR_B_COOK` only (`tm_vend.scp:730`) |
| `i_fish_cut_cooked` (steak, cooked) | 3 | **no — nowhere in the ruleset** |
| `i_fish_cooked_small` | **1** | no |

Cooking does not raise the price here (3 → 3) and removes every buyer; cooked small fish is worth *less* than raw. On RevolutionUO's Scripts-X, Cooking is a food and skill-gain activity, not an income multiplier. `SCRIPT_VERIFIED`.

The fisherman will not buy steaks either — `VENDOR_B_FISHER` lists only whole fish — so the processing route changes *who you sell to*, not just what.

### 6.5 Money-making audit matrix

Every route a character could take to gold, what the shard actually requires for it, and how far M3 got. Graded with the same categories as `REVOLUTION_GAMEPLAY_TRUTH.md`. Prices are the itemdef `VALUE`; what a vendor pays is a fraction of it, measured where a live figure exists.

| Route | Requires | Sells to | Unit value | Status |
|---|---|---|---|---|
| **Fishing → whole fish** | Fishing (any), a pole, water | fisher, cook | `VALUE=2`, **1 gp measured** | **LIVE_VERIFIED** — `m3_sell2` |
| **Fishing → carve → steaks** | the above + any blade | **cook only** | `VALUE=3`, **2 gp measured**, ×4 per fish | **LIVE_VERIFIED** — `m3_cut1`, the best route proven |
| Cooking fish steaks | Cooking | — | — | **SCRIPT_VERIFIED — do not.** `i_fish_cut_cooked` is in no `BUY=` line at all (§6.4) |
| Mining → ore | Mining, a pickaxe, a mountain | see below | `i_ore_iron VALUE=4`, `WEIGHT=2` | **SCRIPT_VERIFIED**, not run. The character has the pickaxe and Mining 30 |
| Mining → smelt → ingots | + Blacksmithy 20 to smelt (`SKILLMAKE=20.0 mining`) | blacksmith (`BUY=i_ingot_iron`) | **`i_ingot_iron` carries no `VALUE` at all** | **UNKNOWN.** A vendor is scripted to buy an item whose price the itemdef never states. Whether Sphere derives it from `RESOURCES` or pays nothing was not measured, and is not going to be guessed at here |
| Lumberjacking → logs / boards | Lumberjacking, an axe | carpenter, blacksmith | `i_log VALUE=1`, `i_board VALUE=2` | **SCRIPT_VERIFIED**, not run |
| Hunting → hides / furs | combat + a skinning knife | tanner | `i_hide VALUE=5`, `i_hides_cut VALUE=5` | **SCRIPT_VERIFIED**, not run — needs the combat that M3 deliberately did not build |
| Reagent gathering | walking to a field | alchemist, mage | e.g. `i_reag_ginseng VALUE=3` | **SCRIPT_VERIFIED**, not run. The atlas already has the reagent fields |
| Alchemy → potions | Alchemy + reagents + bottles | alchemist | potions bought back by `VENDOR_B_ALCHEMIST` | **SCRIPT_VERIFIED**, not run |
| Inscription → scrolls | Inscription + blank scrolls + reagents | mage shop | `i_scroll_blank VALUE=9` | **SCRIPT_VERIFIED**, not run — and it is the same wall as §7: 6th-circle scrolls need Inscription 60 **and** Magery 50 |
| Tailoring / Carpentry → goods | skill + cloth or boards | tailor, carpenter | per item | **SCRIPT_VERIFIED**, not run |
| Monster loot → gold | combat, survival | — (gold direct) | — | **HISTORICAL_UNVERIFIED** for rates. Deferred with combat |
| **Player-to-player sale** | a counterparty | another character | negotiated | **LIVE_VERIFIED** — §9.1, 25 gp for a stack of steaks |

Two things this table is meant to make obvious.

**Processing is where the money is, not gathering.** The one route measured both ways pays eight times more for one extra action with a tool the character already owns. That is the shape a progression planner should assume for the others until each is measured, and it is the reason `ResourceNeed` distinguishes a raw input from a finished good.

**Half of this is unmeasured, and says so.** Nothing in the untested rows was promoted to a Revolution fact because a template file implies it; `i_ingot_iron` is left as `UNKNOWN` rather than given a plausible price.

---

## 7. Mark, Recall and the rune

**Still blocked, for the reason M2.5 identified and M3 quantified.**

`i_rune_marker` (0x1F14) is sold by `VENDOR_S_MAGE_SHOP` for 2–10 gold, so a blank rune is buyable. Mark is not: it is `[SPELL 45]`, **Magery 60**, creation clamps Magery to 50, the newbie spellbook holds three spells in each of circles 1–4 and **not Mark**, and **no vendor on this shard sells a 6th-circle scroll** — `random_sixth_circle` appears only in monster loot, dungeon chests, and Inscription crafting at `Inscription 60.0, Magery 50.0`. Recall is gated behind Mark, because a Recall without a marked rune is a Recall to nowhere.

So the honest routes to Mark are: train Magery to 60 (§5.4 — hours), kill something that drops the scroll (PvM, deferred), or craft one (Inscription 60 + Magery 50 — the same problem twice). M3 took the first and did not get there.

**This is not a missing server feature.** Everything works; it is simply expensive, which is exactly the shard's design and matches the player memory that progression was slow. Marked as **BLOCKED — character capability**, not as a compatibility gap.

The rune purchase attempt itself surfaced the vendor findings in §8 and was not completed: the run ended stranded on the Mage Tower's upper storey, which is what made M2.5 debt #5 a blocker and got it fixed.

---

## 8. Vendors: six findings that cost live runs

* **Speech carries three tiles and every vendor in earshot answers.** Asking the mage for its wares got the alchemist's potion list instead. Only the vendor actually addressed now completes the shop action.
* **A trade is only in the paperdoll title, and a substring is not enough.** "Caedmon, the mage guildmaster" contains "the mage" and **keeps no shop at all**, so the bot stood in front of him saying "buy" to silence. Trades are now matched on the exact word after the last `" the "`.
* **Titles are gendered.** The Britain docks have both "the fisherman" and "the fisherwoman"; a scenario naming one worked or failed depending on which NPC had spawned. `mobile_trade` takes a list, and the service alias table knows both.

* **An NPC must be in conversation before a keyword registers.** `m3_income7` arrived at the fisherwoman and said "sell" 25 ms later, and the shard answered *"Shika: Hmm?"* — straight out of `e_Human_HearUnk`, the *unrecognised speech* handler, with the shop never opening. The M2 sale that worked had greeted first: stand there, let the vendor open the conversation itself (`e_Human_ConvInit`, *"Thou wishest to speak with me?"*), say hello, then trade. Scenarios now do what a person does.
* **Reach is exactly two tiles, and vendors wander.** `CChar::CanTouch` ends in `if (iDist > 2) fCanTouch = false` (`CCharStatus.cpp:1423`). Vendors have `walkRange 5` in their spawner rows, so a position read eight seconds ago is not where they are: `m3_sell1` opened the sell list correctly and then got *"You can't reach the Vendor"*. Scenarios now re-approach immediately before each exchange rather than trusting the arrival.
* **Say the vendor's name or the wrong shop answers.** Source-X walks every character in earshot and calls `NPC_OnHearName` on each (`CClientEvent.cpp:1962`); a name match sets `bNamed` and **`break`s** the loop, while a bare keyword falls through to *"pick closest NPC"*. In `m3_sell2` the bot stood one tile from Taite the provisioner, said "buy", and received the **shoemaker's** stock list from five tiles further off. `Client::AddressMobile` now prefixes the paperdoll name — `"Taite buy"`, `"Cassiel sell"` — and the right shop answered every time afterwards.

And one from travel: **shops are multi-storey and speech range is three-dimensional.** The bot reached the Mage Tower's gallery, one tile from the mage in x/y and out of earshot in the way that matters. Arrival is now floor-aware.

---

## 9. Secure player-to-player trade

Protocol, read from Source-X and confirmed on the wire — full notes in `include/uo/trade.h`:

* A trade is **opened by dropping an item on another player**; the context-menu route needs a newer client.
* The server creates two linked containers and sends both sides `0x6F SECURE_TRADE_OPEN` with the partner's serial, both container serials and the partner's name.
* Goods go in by ordinary lift-and-drop. **Gold is coins in the window** — there is no virtual gold ledger for this client version.
* Each side accepts with `0x6F SECURE_TRADE_CHANGE`; when both are ticked the server moves the goods. Un-ticking clears the partner's tick too.

**One rule this client enforces that the server does not:** Sphere will complete a trade after a partner adds or removes an item from an already-accepted window. `TradeState` retracts acceptance locally whenever either window's contents change, and counts it. That is the client being careful, and it is documented as such rather than presented as a shard rule.

### 9.1 The two-session test

`m3_trade_seller` / `m3_trade_buyer`, run through `run_m25_pair.ps1`, which starts the second session only after the first's **own log** reports it is in position — observable state, not a sleep. Inside the trade, every wait is on a server-reported fact: the seller waits for the buyer's coin before signing, the buyer waits for the goods before paying, so neither can be left holding an accepted-but-empty window.

**Result, `m3_trade5` — a real item-for-gold trade between two ordinary bot characters.** `RevolutionFisher` dropped a stack of raw fish steaks on `RevolutionMedic`, which opened the window; `RevolutionMedic` put 25 gold coins in; each side waited for the other's goods before ticking its box; the server closed the window with both ticked.

| | seller (`RevolutionFisher`) | buyer (`RevolutionMedic`) |
|---|---|---|
| trade outcome | `both_accepted` | `both_accepted` |
| gold | 1077 → **1102** (`gold gain confirmed`) | 975, **−25** |
| steaks | `expect_item_drop` passed | `expect_item_gain` passed |

Both sides verified against the **server's** post-trade backpack and gold, re-read after the window closed — not against anything the client believed it had done. No inventory was manipulated directly, and the two sessions' `TradeState` objects are per-`Client`, so neither could observe or disturb the other's window.

**The first attempt found a bug in this client, and it is worth recording because it was the client being wrong about a server that was right.** In `m3_trade4` both boxes were ticked, the server moved the goods — and the item movements *of the commit itself* tripped the client-side rule that retracts acceptance when the table changes (§9). That cleared both checks a millisecond before the CLOSE arrived, and since Sphere's CLOSE carries no reason code, a completed sale was reported as `partner_cancelled`. The trade had in fact succeeded: the seller's gold went 1052 → 1077 and the buyer was holding a steak at the start of the next run, which is how the diagnosis was confirmed. `TradeState` now latches "both accepted" the instant the server reports it, the safety rule stops firing from that point, and the latch is what classifies the close. Covered by a regression test named after the run.

---

## 10. Multi-session safety

Everything mutable stays a `Client` member: build goals, training session, budget, resource needs, the active trade, its partner, the offered items and both acceptance states, and the M2.5 travel and knowledge state. `TestTradeIsolation` asserts two `TradeState`s cannot see each other's window, acceptance or partner, and that closing one leaves the other alone. Live, the two halves of §9.1 ran as separate processes against separate accounts and neither observed the other's window.

**A fleet cannot log in all at once.** Re-verifying the M2.5 scenarios back to back tripped the shard's own connection guard:

```
ERROR:Outcome (default): requested kick + IP block allowed by script 'f_onserver_connectreq_ex'.
ERROR:Blocked connection from '127.0.0.1' [IP history: blocked=1, ttl=300, pings=3, connecting=0, connected=1].
ERROR:Reject reason: Blocked IP.
```

Every session after it died on `WSA=10054` at the socket before a packet was exchanged. This is the same lesson M2 learned about speech flood protection, one layer lower and with the whole address as the unit: **connection is rate-limited per IP, not per account.**

The exact rule, from the runtime's own `sphere.ini` and `core/serv_triggers.scp`:

* `MaxConnectRequestsPerIP=50` — and the comment beside it is the important part: *"does not decay, it resets only after `<NetTTL>` seconds elapsed since **last connection attempt**"*.
* `NetTTL=60*5` — five minutes.
* On overflow, `f_onserver_connectreq_ex` returns "reject **and ban the IP**" for `LOCAL.BAN_TIMEOUT`, 300 s by default.

So this is not a burst limiter. It is a **cumulative counter over the whole session** that only clears after five minutes of *complete silence from that address* — which means **a rejected attempt resets the clock as surely as a successful one**. A client that reconnects on failure can never recover: the four re-verification runs each renewed their own ban, and the log shows the TTL resetting to 300 at 17:43, 17:44, 17:44 and 17:45. Waiting it out in silence is the only way through.

For a bot population this is a hard design constraint, not a nuisance: logins must be staggered, and a failed connect must back off **without retrying**. Listed as debt — nothing in the client schedules or paces its own login; the harness does it by hand.

---

## 11. uo-offline audit — progression and economy

| Idea | Verdict | Why |
|---|---|---|
| Bot archetypes as classes (Miner, Tank Mage, Treasure Hunter…) | **REJECT_FOR_REVOLUTION** | Their bots *are* their template. Ours must be a hybrid whose capability follows from actual skills, stats and inventory — the M3 model is a list of skill targets precisely so "Mining + Smithy + Alchemy + Magery" is not a special case. |
| Supply thresholds driving a shopping trip ("bot runs low, drops what it is doing, goes shopping, visibly, for gold") | **PORTABLE_LOGIC** | Exactly the `ResourceNeed` / `Budget` / `PurchaseNeed` shape. Adopted. |
| "Recall scrolls scale with wealth, so a fresh Novice carries none and walks everywhere" | **INSPIRATION_ONLY — and independently true here** | On this shard a new character cannot Mark at all (§7). Their design choice is our hard constraint. |
| Crafters burning real stock and buying miners' ore for real coin | **ADAPT_VIA_REAL_CLIENT** | The economics are right; the mechanism must be secure trade over 0x6F, not a server-side transfer. |
| Gold/banking/wealth tracking | **ADAPT_VIA_REAL_CLIENT** | We read gold from `0x11` and spend it at a vendor or in a trade window. Never written locally. |
| Skill progression by direct award | **REJECT_FOR_REVOLUTION** | They can set a skill. We cannot and must not — §5 is the whole point. |
| Fixed 800→1600 bot population, live `[SetBotPopulation` | **REJECT_FOR_REVOLUTION** | Server-side spawning of bodies. Ours are real clients; population is a process-count question for a later milestone. |
| Stuck-bot rescue by teleport | **REJECT_FOR_REVOLUTION** | Already rejected in M2.5; the escape rung is the legitimate half. |
| Death/corpse recovery decision policy | **PORTABLE_LOGIC, deferred** | The shape is right and M2.5 already records what it needs. M4+. |
| "Supplies run out and nothing refills invisibly" | **REVOLUTION_OVERRIDE_REQUIRED** | Agreed in principle, but every number — what a fish sells for, what a rune costs, how fast mana returns — must come from this shard's data, not theirs. |

---

## 12. Runtime compatibility gaps found

Neither was invented around; both are recorded for a future restoration task.

1. **Runebooks do not work.** `i_spellbook_runebook` exists as a craftable item with `TYPE=t_normal //fixme: or t_runebook`, and **Source-X has no `IT_RUNEBOOK` type at all**. Player memory is explicit that Revolution players made and used them. The most likely explanation is a Revolution-specific *scripted* runebook (as `d_moongates` is a scripted moongate) that this stock Scripts-X does not contain. Full analysis and reconstruction requirements in `REVOLUTION_GAMEPLAY_TRUTH.md` §6. **Not implemented.**
2. **No anti-unattended-macro system.** Nothing in Source-X or `runtime/scripts`. Player memory says restrictions existed. **Not implemented.**

A third, smaller: the OSI **passive meditation** bonus is written in `skill46_meditation.scp` and commented out at every call site. Whether that was Revolution's choice or an upstream default is `UNKNOWN`; it materially shapes how hard mage training is.

---

## 13. Live scenario results

All against Source-X on `127.0.0.1:2593`, ordinary accounts, no GM commands, no admin assistance to any progression proof.

### A GM-provisioned test character, kept outside this record

`RevolutionMageGM` (UID `0x00000F6A`) was created at the user's request **for testing spell behaviour**, and is **not evidence for anything in this milestone**. It was made the ordinary way (Magery 50 / Meditation 30 / EvalInt 20, the most a `0x00` packet may ask) and then raised by **server console** commands — an operator action, not gameplay:

* `STR=100`, `DEX=25`, `INT=100`, `MAGERY=600`
* its newbie spellbook's `MORE1`/`MORE2` set to `0ffffffff` — the full-book fill that the shard's own `sp_tm_newbie.scp:453` documents in a comment
* 200 of each of the eight Magery reagents

Verified live: `STR 100 DEX 25 INT 100 (sum 225)`, `Magery base=60.0`, and it cast **Invisibility** (`[SPELL 44]`, sixth circle, *"An Lor Xen"*, mana 28→8) — which a Magery 50 character cannot do. That is the point of it: it makes sixth-circle behaviour testable now instead of after the ~17 hours §5.4 measured.

**The separation is deliberate and total.** `RevolutionMage2`, the character the Magery training result comes from, was not touched; nothing this character owns or can do was used in §5, §6, §7 or §9; and no gold, item or skill point in the ledger above came from it. If a later milestone cites it, it must say so explicitly.

*(An interactive Admin observer was connected to the shard during part of the session. Its logged commands are `add <creature>`/`add <item>` and `goname`, all acting on itself or its own location; nothing it did touched a bot character's skills, stats, gold or inventory, and every gold and skill delta below is accounted for by a logged in-game transaction.)*

| Scenario | Result | Evidence |
|---|---|---|
| `m3_skill_audit` | **PASS** | 58 skills reported; the first run reported **zero** and produced the 0x34-subtype-5 finding |
| `m3_create_fisher` | **PASS** | Fishing 50.0 / Mining 30.0 / Blacksmithy 20.0 exactly as requested; kit and 1000 gold from the shard's own templates |
| `m3_meditation_probe` | **PASS** | meditation fails at 3.6; mana measured at 19.3 s/point with no meditation help |
| `m3_magery_training` | **PARTIAL** | Magery **50.0 → 50.9**, Meditation **3.6 → 12.7** over 1 h 45 m, all server-reported; target of 60 not reached (§5.4) |
| `m3_income6` | **PASS (gather)** | 8 catches from 36 casts, pack 0 → **14 fish**; semantic 44-leg route from Yew |
| `m3_income7` | **PASS (gather)**, one finding | 11 more fish; eleven of them landed on the dock — *"It is too heavy"* (§6.3). Its sale step failed on the conversation rule (§8) |
| `m3_sell2` | **PASS (sell)** | 4 whole fish → *"Here you are, 4 gold coins"*; gold **1000 → 1004** |
| `m3_sell_cut` (`m3_cut1`) | **PASS** | 12 fish carved to **48 raw steaks**; 24 sold to the cook for **48 gold**; gold **1004 → 1052**, `gold gain confirmed`; 87 steps, no failures |
| `m3_trade_seller` / `m3_trade_buyer` (`m3_trade5`) | **PASS** | `both_accepted` on both sides; seller gold **1077 → 1102**, buyer **−25**; goods moved, verified from server state (§9.1) |

---

## 14. Technical debt

1. **Magery 60 is unreached**, and with it Mark, Recall and the M2.5 rune debt. Not a defect — a cost. §5.4 has the measured rate.
2. **Only one training strategy exists** (cast-and-meditate). The `TrainingSession` machinery is general; the per-skill strategies are not written.
3. **`TrainingSession` is not yet wired into `Client`.** Scenarios drive training directly. The state machine is tested but not load-bearing.
4. **Stat progression is unmeasured.** The mechanism is understood; no live before/after was taken.
5. **The income slice sells to an NPC.** Player-to-player sale of gathered goods is proven separately in the trade test but not joined into one loop.
6. **Fishing spot selection is a hand-listed set of twelve tiles.** It was derived from client data rather than guessed, but a bot should find its own water — the navgrid already marks `kCellWater`.
7. **No purchase was made with *earned* gold.** The character has 1000 starting gold, so "buy supplies" is proven only from the starting pile. The one purchase attempted (a dagger) turned out to be unnecessary — `MALE_DEFAULT` already supplies one — and `vendor_buy` silently did nothing when the named graphic was absent from the vendor's stock, which is its own small defect: a missing item should be an explicit failure, not a no-op.
8. **Trade offers one item at a time**, and the contents-changed retraction is local only — it does not send the retracting 0x6F.
9. **A gathering bot has no sense of its own weight.** `m3_income7` fished happily while the shard dropped eleven catches on the ground. Carrying capacity is not modelled anywhere in `prog::`, and the fix is not "carry less" — it is §6.4's processing step, which the bot should choose for itself.
10. **Vendor interaction is a fixed script of sleeps.** Greeting, waiting for `e_Human_ConvInit`, and re-approaching before each exchange are all correct behaviours (§8) but they are spelled out in the scenario rather than owned by the client. A `VendorSession` belongs beside `TradingSession`.
11. **Logins are not rate-limited by the client.** The shard blocks an *IP* for 300 s after a burst of connections (§10); the harness spaces sessions out by hand and nothing in `Client` knows to.
12. **One action in flight** (M2.5 debt #10), now with a measured cost — §2.
13. **`Journey` is still 2-D** — M2.5 debt #4, partly mitigated.

---

## 15. Deferred to M4+

Autonomous professions, bot populations, a free-running economy, dynamic pricing, autonomous PvP procurement, potion-keg production, guilds, factions, party AI, PK AI, the full corpse-recovery decision policy, gossip and reputation, runebook reconstruction, and the full fishing/S.O.S. progression.
