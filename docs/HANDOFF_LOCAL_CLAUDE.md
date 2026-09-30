# Handoff to local Claude Code (2026-09-30)

From the cloud session that built the "living shard" layer, to the Claude
Code running on the owner's Windows machine (the checkout the tools call
`C:/Projects/RevolutionOffline`, where Sphere, the MUL files, `bot_data/`,
the credentials and `runtime/scripts` live).

**Read `CLAUDE.md` first. Its rules win over anything here.**

## 1. Where things stand

* **Branch:** `claude/tender-sagan-r5hwj5`.
* **PR:** https://github.com/BarbarosYamak/Revolution-Offline/pull/3 (20
  commits, not merged yet).
* **Status: everything on this branch is built and unit-tested offline, and
  NOTHING on it has run against the live Sphere server.** The cloud session
  had no Sphere, no MUL files and no accounts. It cross-compiled the Windows
  client with MinGW-w64 and ran the tests under Wine.
* **Tests (Wine):** 63/64 pass; `life_world_harness` has 459 checks, 0
  failures. The one failure is `m4_economy_invariant`: it needs
  `docs/tns_exports/economy_arbitrage_loops.tsv`, which exists only on this
  machine. **Commit that file.**
* **Proven live before this branch** (earlier waves; logs are in the repo):
  login and character creation, travel and moongates, the goal planner,
  banking, NPC buy and sell, trainers, graveyard hunting, mining, smelting,
  lumberjacking, fishing, crafting (blacksmith, tinker, tailor, carpenter),
  persistence, and fleet waves of up to about 120 bots.

## 2. What the branch adds (one doc each in `docs/`)

| Doc | Feature | Key code |
|---|---|---|
| — | Sparring v2 (every fighter, one agreement, 40% stop, bystander healers) | `include/uo/sparring.h`, `runner/Social.cpp` |
| — | Linux cross-build | `tools/cross/` |
| M5.1 | Personas and play windows; population manager (`--live`, `--plan`) | `include/uo/persona.h`, `tools/fleet_ramp.py` |
| M5.2 | Observer dashboard | `tools/observer.py`, `Runner::PublishStatus` |
| M5.3 | Turkish small talk | `include/uo/chatter.h` |
| M5.4 | Era timeline 2008–2016 (`--era-date`) | `include/uo/era.h` |
| M5.5 | Recall travel by runebook page point | `include/uo/recall_plan.h`, `travel/ClientTravel.cpp` |
| M5.6 | Treasure hunting (0x90/0x56 map packets) | `include/uo/treasure.h`, `runner/Treasure.cpp` |
| M5.7 | PvP: PK ambush, anti-PK, "PK var!" alarm, `--no-pvp` | `include/uo/pvp.h`, `runner/Pvp.cpp` |
| M5.8 | Housing (0x99 placement, multis) and storage in a secured bag | `include/uo/housing.h`, `runner/Housing.cpp` |
| M5.9 | Player vendors: pricing, 0x9A prompt, owners, buyers | `include/uo/player_vendor.h`, `runner/Vendors.cpp` |
| M5.10 | Party hunting: focus fire, healing, groups of 2–5, dungeons, loot turns, roles | `include/uo/party_hunt.h`, `runner/PartyHunt.cpp` |
| M5.11 | Reds avoid and route around guards; recall escape; rune marking | `RoutePlanner::GuardedCells`, `runner/Runes.cpp` |
| M5.12 | **Families (last names)**: server script and bots | `runtime/scripts/revolution/revolution_family.scp`, `runner/Family.cpp` |
| M5.13 | Runebooks: scribes craft to order, mages order one | `Orders.cpp`, `Identity.cpp CraftMenuFor` |
| M5.14 | Guild awareness (tags from name labels) | `Client::MobileGuildTag`, `runner/Family.cpp` |

New goals: `HUNT_TREASURE`, `HUNT_PLAYERS`, `BUY_HOUSE`, `FAMILY`,
`RUN_VENDOR`, `HOUSE_STORE`. These were appended at the end of `GoalKind` and
`NeedKind`, so old saves still load. New saved state is `persona`, `family`,
and places of kind `house`, `house_chest`, `my_vendor`, `player_vendor` and
`rune`. The schema version is unchanged; all of it is additive.

## 3. New owner rulings (saved in `bot/uo-client/.claude/agent-memory/revolution-god/`)

* `blank-runes-from-mage-shops.md`: blank runes were bought at mage shops.
  `i_rune_marker` is now REVOLUTION_NPC_VERIFIED.
* `family-last-names.md`:
  * a family deed (~50k) makes the user head, who picks the last name;
  * invitation deeds (~10–20k) give the invited player the same last name;
  * a family is social, shares a home or house, and stands together in PvP.

**Still UNKNOWN, ask the owner:** which NPC sold family deeds. The bots try a
banker, then a provisioner.

## 4. Do these on this machine, in order

1. **Build and test natively.**
   ```bat
   cd bot\uo-client
   cmake -S . -B build-m1 -G Ninja
   cmake --build build-m1
   ctest --test-dir build-m1 --output-on-failure
   ```
   Use the MSVC x86 environment, as in `scripts\build.bat`. `fleet_ramp.py`
   launches `build-m1\uo_client.exe`. MSVC may warn or error where MinGW did
   not; fix those first. Watch for the Windows `near` macro, which bit once
   already.
2. **Load the family script.** `runtime/scripts/revolution/revolution_family.scp`
   is picked up by `ScpFiles=scripts/`.
   * Resync Sphere and read the console for script errors. `PROMPTCONSOLE
     f_family_found,...` and `<STRREGEX ...>` are UNVERIFIED Source-X syntax.
     If either is wrong, fix it the Source-X way; don't redesign it.
   * As a GM: `.add i_deed_family`, use it and name a family. Then `.add
     i_deed_family_invite`, target a second character, accept the "Aile
     daveti" gump, and check that both names now end with the last name.
3. **Unblock the server-side features** (all in scripts that are **not** in
   the repo):
   * **House deeds:** uncomment the `SELL=i_deed_*house*` lines in
     `VENDOR_S_DEED_TEMPLATE` (architect).
   * **Player vendors:** uncomment `SELL=i_deed_vendor` on the innkeeper and
     tavernkeeper templates.
   * **Runebook crafting:** add `ON=i_spellbook_runebook Runebook` and
     `MAKEITEM=i_spellbook_runebook` to the top level of `sm_inscription`.
   * **Family deeds:** add `SELL=i_deed_family` and `SELL=i_deed_family_invite`
     to whichever vendor the owner names.
   * **Then commit `runtime/scripts`** (minus anything secret), so future
     sessions can see what the server really has. Guild stones and S.O.S
     bottles need that before anyone can build them.
4. **Run the first live evening.**
   ```bat
   python tools\fleet_ramp.py --plan
   python tools\fleet_ramp.py --live --directory run_gates\live --max-online 15
   python tools\observer.py --run run_gates\live      :: http://127.0.0.1:8765
   ```
   Add `--no-pvp` for a first calm run. Add `--era-start 2009-06-01` to pin
   an era.
5. **Read the logs** (`run_gates\live\*.console.txt`, `population_log.tsv`,
   each bot's `bot_data\<id>\status.json`) and fix what breaks. **Fix from the
   evidence, never by weakening a check or a rule.**

## 5. What to grep in the console logs, per feature

| Feature | Healthy signs | Where it will likely break first |
|---|---|---|
| Persona | `persona: rhythm=... schedule=...` at login | — |
| Chat | `chat: greet`, `chat: answer_sa to X` | Hearing: the journal speaker or text format |
| Sparring | `sparring: party formed`, round lines, `Ready to spar.` | Party packets; health freshness |
| Recall | `[runebook] page N = ...`, `travel: runebook read`, `[travel] mode runebook_recall <- chosen` | Page point text format in the gump |
| Mark | `travel: marking a blank rune`, `marked a rune ... the server calls it '...'` | Rune label wording; `LooksBlankRune` |
| Treasure | `treasure: decode/travel/dig/open/unlock/loot` | Map graphic, 0x90 bounds, the chest graphic list |
| PvP | `pvp: pk engages`, `pvp: attacked by X -- calling for help`, `pvp: heard 'PK var! yardim!'` | Noto of attackers; `IsAttackingMe` coverage |
| Red routing | `[travel] murderer routing: N guarded cells are walls`, `refused: ... we are red` | Guarded region flags in the atlas |
| Party hunt | `party: called the target`, `party: focusing`, `party: bandaging a member`, `party: this kill is a friend's turn` | Leader war-mode reads; 0xA1 health for party members |
| Dungeons | `party: leading the group to Despise`, `party: in Despise` | Route planning into dungeon regions |
| House | `house: buy_deed/use_deed/place`, `house: placed at` | Deeds not sold (expected until step 3); the 0x99 reply format |
| House storage | `house: setting a bag down`, `a secured bag now stands` | The "I wish to secure this" speech on this tree |
| Vendors | `vendor: using the vendor deed`, `vendor: priced at`, `vendor: buying ... from a player vendor` | Whether a drop raises the 0x9A prompt |
| Family | `family: naming the family X`, `family: we are now of the X family`, `family: X invites us` | The script syntax (step 2); name change packets |
| Guild tags | `guild: we wear the [ABC] tag` | Only if the server shows guild abbreviations |
| Dashboard | Rows in the page, `updated Ns ago` | The `status.json` path under `--bot-data` |

## 6. Known gaps and things to watch

* A goal that loops without progress should hit the planner's existing
  spin, cooldown and BlockNeed guards. If one doesn't, that is a bug worth
  fixing first.
* PK bots are red after their first murder. Watch that they bank at
  Buccaneer's Den and never walk into guards.
* Loot turns assume both members saw the same kill (focus fire). Watch for
  corpses nobody loots.
* House and family deeds share graphic 0x14F0; errands match by shop row
  **name** (`VendorErrandSpec::nameContains`). Check the real names in the
  shop lists.
* The persona schedule uses the host's local clock. The owner's clock is
  Turkey time, which is intended.
* **Not built, on purpose:**
  * the LLM personality layer (CLAUDE.md: not yet);
  * guild founding and joining (no stone scripts);
  * S.O.S bottles (none in the runtime);
  * leaving or disbanding a family (unknown).

## 7. How the cloud session tested (if you ever need it)

`tools/cross/run_tests_wine.sh` uses `tools/cross/mingw-w64.cmake`. It needs
`g++-mingw-w64-x86-64-posix`, `wine64`, `ninja` and `cmake`. On this Windows
machine, just use MSVC and `ctest` natively.
