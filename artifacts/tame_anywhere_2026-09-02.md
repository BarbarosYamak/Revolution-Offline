# Taming anywhere, not just sheep — 2026-09-02

Owner: "rhea can tame a lot of things not just sheep."

## 1. Data — data/revolution_tamables.tsv (new)

`tools/tamablegen.py` (stdlib, same style as `pasturegen.py`, save parsing per
repo-root `tools/world_query.py`: chars live in `sphereworld.scp` +
`spherechars.scp`, `P=` is a world position only without `CONT=`).

Every WORLDCHAR whose chardef carries a TAMING requirement in
`data/revolution_creatures.tsv`, clustered PER SPECIES (so `taming_req` stays
meaningful) at 15 tiles, min 2 animals.

    4708 tamable creatures of 50 kinds -> 393 clusters

Columns: `x y map count radius label defname taming_req`.

Clusters within 150 tiles of each atlas city centroid (`--summary`), abridged:

| city | clusters | kinds (req) |
|---|---|---|
| Britain | 2 | Dog(15.3), Rat(0.9) |
| Guard Towers (Brit outskirts) | 6 | Rat, Dog, dire wolf(80), Cougar(50), Grizzly(59.1), Timber Wolf(23.1) |
| Cove | 5 | Brown Bear(41.1), Dog, Cougar(50), Grizzly(59.1) |
| Moonglow | 12 | Dog, Rat, grey wolf(50), Goat, Grizzly, Panther(53.1), Pig, Great Hart |
| Destard region | 13 | Timber Wolf x18, Great Hart, Bull Frog, Grizzly, Goat, Alligator(47.1) |
| Yew | 6 | Sheep x45, Dog, Rat |
| Trinsic | 7 | Dog x11, Rat x6 |
| Minoc | 3 | Rat x6, Crow x3 |

Britain proper is thin (dogs and rats); the useful herds for a Taming-50 tamer
sit in the belt outside the walls and around Cove/Moonglow. The full summary is
reproducible with `python tools/tamablegen.py --summary`.

## 2. Sphere's gain/success rules (source-verified, not assumed)

- `runtime/scripts/skills/skill35_taming.scp`: SKILL 35, `DELAY=2.0`,
  `ADV_RATE=10.0,200.0,800.0`, `@Fail`/`@Abort` only. **No `GAINRADIUS` line.**
- `server/Source-X/src/game/chars/CCharSkill.cpp:2344` — taming difficulty is
  `iTameBase/10`, i.e. the animal's own TAMING base.
- `CCharSkill.cpp:514-540` `Skill_CheckSuccess` — `Calc_GetSCurve(skill -
  difficulty*10, SKILL_VARIANCE=100)`. Equal skill and requirement is a coin
  toss; above it, it climbs.
- `CCharSkill.cpp:402-412` `Skill_Experience` — no gain when
  `difficulty + GAINRADIUS < max(skill, 5.0)`; also no gain in a
  `REGION_FLAG_SAFE` area, and none once `Skill_GetSum() >= SumMax`.
- `runtime/scripts/skills/skill.scp:18` `[COMMENT SKILL x]` documents
  `GAINRADIUS=100.0` ("set to 100.0 for original behaviour"). That block is a
  template, not a live skilldef, so the effective radius for SKILL 35 is the
  engine default — UNKNOWN, not read. 10.0 skill points is therefore used as
  the conservative window: if the default is 0 the preference merely costs
  nothing.

## 3. Behaviour

`include/uo/activities/tame.h` (pure, ctest-covered):
`TameCanGain(req, skill)`, `TameTravelBudgetTiles(remainingMs, windDownMs,
workReserveMs)` (inverts `EstimateTripTimeMs`, `uo/life.h`),
`ChooseTameCluster(list, x, y, skill, budgetTiles)` — never above skill, gain
window first, then nearest, then bigger herd; nothing beyond the clock.

`Runner::DoTameAnimal` now loads `revolution_tamables.tsv` (`LoadTamables`),
drops the herd it is standing in and every herd already walked to
(`tameVisited_`), and keeps the 3-trip cap. Pastures are untouched and remain
the wool chain's data.

## 4. Live runs (2 x 5 min, `rev.py gates CHARS=Rhea MINUTES=5`)

Run 1 (13:26-13:31): Rhea logged in as a GHOST from the 12:09 death near
(692,1188). Whole session was RECOVER_CORPSE / REPLACE_EQUIPMENT / GET_FOOD —
TAME_ANIMAL never scored. `run_gates/g_Rhea.console.txt` (run 1) line 152, 508.

Run 2 (13:31-13:36): `session_goals ... TAME_ANIMAL=2(33%)`, goals=2/6.
Observed flow (`run_gates/g_Rhea.console.txt` lines 341-574):

    341  tame: reading the names of everything nearby before judging this spot (Taming 50.0)
    353  tame: 'Sheep' is 12 tiles away -- walking up      (8 approach steps)
    465  tame: trying 'Sheep' (needs Taming 11.1, have 50.0, too easy to gain, attempt 1)
    496  [chat ascii] System: Sheep is already tame.
    ...  attempts 2..8, the same system reply after each

Scan -> names -> judge -> approach -> `ActionUseSkill(Taming, serial)` -> server
reply, all inside 5 minutes. The cluster picker did not run: a qualifying animal
was already in sight at her post-death position, which is the intended order.

## 5. Defect the run exposed, and the fix

`Client::JournalHeardSince` (`src/travel/ClientTravel.cpp:2580-2581`) DROPS every
journal entry whose `sourceSerial` is 0 or 0xFFFFFFFF — "System messages carry no
speaker to walk to". `CChar::Skill_Taming` says everything through
`SysMessage`/`SysMessagef` (`CCharSkill.cpp:2280-2330`). So the taming handler
could never hear its own answer, success or refusal: eight attempts over 90
seconds against a sheep the shard kept calling already tame.

Fixed by reading the answer with `Client::JournalSaidSince`
(`ClientTravel.cpp:2381`, no source filter). A refusal now blacklists that
animal's serial (`tameRefused_`) and re-judges the spot; three refusals stand the
goal down with the shard's own phrase as the reason. Success logs
`tame: success <name> serial=0x... req=... ` and `NoteProgress()`.

STATUS: the system-message fix is verified by source reading + `ctest 43/43`,
NOT by a live run — the two-run budget was spent. The next Rhea gate should show
`tame: 'Sheep' refused -- the shard said "is already tame"` instead of eight
identical attempts.
