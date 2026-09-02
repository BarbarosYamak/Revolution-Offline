# Wave 2 behaviour triage - 2026-09-01

Source: run_gates/g_*.console.txt + .err.txt (30 chars, launch ~18:08:52,
killed 18:14:26, roster run_gates/roster30.tsv). Grading already done in
artifacts/wave2_grade_2026-09-01.md (BLOCKED, truncated run) - this is a
behaviour-forensics pass, not a regrade. All timestamps are console/err
local time; "first problem" = earliest WARN/goal_failed/goal_spinning found
for that character, not necessarily the most frequent one.

## 30-character reconstruction

| char | family | chose to do | went to | achieved | first thing wrong |
|---|---|---|---|---|---|
| Draver | miner_smith | mine ore, train Tinkering, buy pickaxe | Minoc mine door (2556,499) | bought pickaxe from NPC (policy-allowed); never mined (no mine: event in whole run) | 18:08:59.956 move REJECTED at Minoc mine door; OpenDoor+retry loop |
| Kharain | miner_smith | mine ore | Minoc (2557,499) then travelled to Britain area (1416-1425,1690-1704) | nothing tracked; no mine: event | 18:09:00.680 move REJECTED at Minoc door; 18:09:09.653 13 lookahead repairs, abandoned trip |
| Elvar | miner_smith | mine ore | Minoc door (2555,499) then Britain (1416,1704-1705) | goal_completed=TRADE_WITH_PLAYER progress=1 x4 (18:13:49) - real player trade; no mining logged | 18:08:58.794 move REJECTED at Minoc door |
| Zarthal | miner_smith | craft (smith), sell ingots | Britain graveyard-ish coords (1361,1772) then smith forge | crafted 1x i_dagger successfully (pack 1->2, 18:10:00) | goal_failed=BUY_SUPPLIES REFUSE_NO_KNOWN_BUYER item=i_ingot_iron (18:11:23); 18:09:35.247 move REJECTED first |
| Vorar | lumberjack_swordsman | chop logs | Britain (1438,1691) then a wood tile that emptied | FARM-2 FAIL (0 logs per grade doc); ran dry (nothing here to chop x2, 18:14:21) | 18:10:08.723 13 lookahead repairs, abandoned trip |
| Halain | lumberjack_swordsman | chop logs, bank | Britain (1423,1599) area | goal_completed=BANK progress=2 (18:09:47); chop spot ran dry (x6 nothing here to chop) | 18:10:11.760 move REJECTED, then 18:10:30.678 goal (1408,1536) not walkable x34 - dominant |
| Delras | lumberjack_swordsman | chop logs, replace gear | Britain (1439,1693) | FARM-2 PASS (1 log per grade doc); REPLACE_EQUIPMENT progress=1 x6; goal_spinning=GATHER_LOGS at 18:10:18 | 18:08:58.794 move REJECTED door; 18:09:39.742 no path to (1472,1608) x8 |
| Cyras | lumberjack_swordsman | chop logs, train Carpentry | Britain (1439,1691)->(1448,1656) | nothing logged as completed/failed | 18:08:59.138 move REJECTED door; 18:09:00.450 no path x4; 18:13:41.900 lookahead repairs abandon |
| Rhaler | full_crafter | mine, smelt, sell | Minoc mine -> forge (2561,501 unreachable) -> forge (2469,557) | smelted 34 ingots successfully at the 2nd forge after giving up on the 1st (cannot get within 1 tile after 4 tries) | 18:09:16.879 equip rejected / drag cancelled by server (code 5) |
| Ghalor | full_crafter | mine, smelt, sell | same Minoc mine -> forge 2561,501 (fail) -> 2469,557 (ok) | goal_completed=MINE progress=3 (pack 96% full), smelted 22 ingots | 18:09:16.199 equip rejected / drag cancelled (code 5) |
| Falen | full_crafter | mine/craft/train Tinkering | Minoc | nothing tracked (no mine/smelt/craft events logged) | 18:11:16.966 13 lookahead repairs, abandoned trip (only problem logged) |
| Aelia | tailor | replace gear, earn gold, craft | Britain: healer, bank, butcher, innkeeper, inn, provisioner, armorer, tinkerguildmaster, animaltrainer | nothing transacted; EXPLORE progress=1 x5 only real completion | BLOCKED_NEED CRAFT: this life makes nothing sellable x44; goal_spinning=REPLACE_EQUIPMENT/EARN_GOLD (explicit bug in that goal, not pacing) from 18:09:01.707 |
| Amara | tailor | travel to required place, craft | tried to reach (1176,322) near Yew from (1124,362) | EXPLORE progress=1 x2; travel never succeeded | goal_failed=TRAVEL_TO_REQUIRED_PLACE three trips did not arrive (18:09:26.830); 45x no-path, start tile fully blocked (open=0 terrain=8), 2x route-exhausted escapes, goal_spinning |
| Elara | alchemist | buy reagents, craft potions | Magincia (656,2141) area | crafted i_potion_poison (pack 4->5, 18:13:15) despite churn | 18:09:18.976 move_item server_failure (wrong container); goal_failed=BUY_SUPPLIES REFUSE_VENDOR_UNREACHABLE no alchemist found after 4 trips (18:11:00) |
| Selene | alchemist | cast spells, train Meditation | Britain (1439,1693) | nothing completed/failed logged | 18:09:07.744 cast_spell timeout x6; 18:09:22.413 move_item server_failure |
| Lyra | scribe | cast spells, craft scrolls | Moonglow (1591,1664) | nothing completed | 18:09:10.438 cast_spell timeout x2; 18:09:16.646 goal not-walkable x3 |
| Thalia | scribe | craft scrolls, sell | Moonglow -> mage shop (no scroll stock) | crafted i_scroll_poison 13x successfully; EARN_GOLD progress=1 at 18:14:19 (last second) | 18:09:00.496 move_item server_failure (wrong container); use_object timeout x15 during craft (18:11:59) |
| Dorvar | fisher | fish, sell fish | Skara Brae coast (3662,2302 shore) | sold 33 fish for 66g to a fisher NPC (18:11:18) | goal (3662,2302) is not walkable x60 - dominant; goal_failed=BUY_SUPPLIES for i_fish_big_1 (18:12:38) |
| Ithion | fisher | fish, sell fish | Skara Brae -> Magincia fisher (3674,2289) -> Skara Brae fisher (656,2235) | sold fish twice (112g total), caught fish live | 18:10:07.644 use_item_on superseded by equip; then 18:11:36.870 no path to moongate (3564,2140) x10 |
| Odessa | merchant_tinker | buy/resell, trade with players | Britain (1438,1693) | goal_completed=TRADE_WITH_PLAYER x6 (real trade success) | 18:11:58.233 move REJECTED door (fairly clean until here); BLOCKED_NEED CRAFT nothing sellable x8; goal_spinning=TRADE_WITH_PLAYER at 18:13:48 |
| Serena | merchant_tinker | buy/resell | Britain (1416,1704) | nothing tracked | 18:11:28.737 no path to (1425,1690) (only problem logged) |
| Aurelius | mage | get food, fill spellbook | Britain -> scribe -> mage shop (1591,1657) | GET_FOOD progress=0 only | 18:08:58.625 move REJECTED door; 18:14:04.150 vendor_buy rejected cannot reach the Vendor at mage shop |
| Leander | mage | cast spells, fill spellbook | Britain (1440,1694) | nothing completed | 18:09:11.162 cast_spell timeout x2; 18:09:17.343 route exhausted escape |
| Illyria | mage | replace equipment, cast spells | Nujelm start area | goal_completed=REPLACE_EQUIPMENT progress=101 (nonsensical value) x8 | 18:09:11.302 cast_spell timeout x26 - dominant, every cast in the window failed to confirm |
| Hector | fencer | train combat, heal, survive | Britain -> Britain Graveyard | attacked a target (6x attack success), fled at HP 12 percent (SURVIVE override), recovered corpse | 18:09:01.008 move REJECTED door; 18:10:41.273 use_object (bandage) superseded by attack; goal_failed=REPLACE_EQUIPMENT healer does not stock clean bandages |
| Castor | fencer | train combat | Trinsic -> Britain Graveyard (616,2152) | fought Spectre + Skeletal Mage (2x attack success), HEAL progress=1, SURVIVE progress=0 | 18:09:20.386 move REJECTED door; 18:10:50.753 13 lookahead repairs abandon; 18:10:53.492 no path to (1336,1832) x5 |
| Faustus | macer | train combat | Britain -> toward moongate (3564,2140) | TRAIN_COMBAT blocked on cooldown for 239s after achieving nothing x4; 0 kills (per grade doc) | 18:09:41.256 move REJECTED door; 18:12:04.528 route exhausted at moongate (3564,2140,34) |
| Titus | archer | train combat, bank, buy logs | Skara Brae (1417,1593) | goal_completed=BANK progress=2; 0 combat achieved | 18:09:10.700 vendor_buy timeout; 18:09:41.981 no path; 18:10:12.845 goal (1424,1686) not walkable x12; goal_failed=BUY_SUPPLIES REFUSE_NO_KNOWN_BUYER item=i_log x2 |
| Rhea | tamer | tame animals | remote area near (563,1111) | nothing logged at all (no goal_completed/failed, no achieve lines) - near-silent run | 18:12:44.247 move REJECTED door (only problem logged, very late - run was otherwise idle) |
| Xerxes | treasure_hunter | train at NPC, survive, explore | Vesper start -> toward moongate (3564,2140) | goal_completed=TRAIN_AT_NPC (retried after 1st refusal of 3 attempts to hand over 211g); SURVIVE/REPLACE_EQUIPMENT(progress=243, nonsensical)/EXPLORE all completed | 18:09:12.099 use_item_on timeout; BLOCKED_NEED TRAIN_COMBAT nothing here to practise on x11; 18:14:17.599 route exhausted at moongate near kill time |

## Failure clusters (owner subsystem, representative file:line)

1. Door-adjacent move REJECTED at session start (OpenDoor + retry) -
   21/30 characters (Aelia, Aurelius, Castor, Cyras, Delras, Draver, Elvar,
   Faustus, Ghalor, Halain, Hector, Ithion, Kharain, Leander, Odessa,
   Rhaler, Rhea, Selene, Titus, Vorar, Zarthal). Nearly every runs first
   logged problem. Owner: navigation.
   bot/uo-client/src/navigation/Navigation.cpp:296 (OpenDoor+retry),
   :184 (move REJECTED log).

2. "goal (X,Y) is not walkable; skipping A* and stopping" - repeated
   dozens of times against the same target coordinate for one character
   at a time: Dorvar x60 (fishing spot 3662,2302), Halain x34
   (1408,1536), Titus x12 (1424,1686), Lyra x3. 4 chars but very high
   per-char repeat count = the life/travel planner keeps re-issuing an
   unreachable literal target instead of giving up or picking a new one.
   Owner: navigation / planner (target-selection feeds Navigation a
   dead coordinate repeatedly). Navigation.cpp:798.

3. "no path to (X,Y) ... stopping" (A* genuinely fails, distinct from
   #2) - 16/30 chars (Aelia, Amara x45, Aurelius, Castor, Cyras, Delras,
   Elara, Elvar, Faustus, Halain, Hector, Ithion, Kharain, Lyra, Serena,
   Titus). Amaras case is the worst: her start tile shows open=0
   terrain=8 - every exit blocked - and she never escaped for the rest
   of the run (goal_failed=TRAVEL_TO_REQUIRED_PLACE "three trips did not
   arrive", 2x route exhausted escape attempts). Owner: navigation.
   Navigation.cpp:806.

4. "13 lookahead repairs without movement; abandoning trip" - 8 chars
   (Castor, Cyras, Draver, Elvar, Falen, Kharain, Vorar, Dorvar). The
   bots local movement-repair loop gives up in place. Owner:
   navigation. Navigation.cpp:694.

5. Vendor/trade routed to a buyer that does not exist or cannot be
   reached - 7 chars (Dorvar, Ithion, Titus: REFUSE_NO_KNOWN_BUYER;
   Elara: REFUSE_VENDOR_UNREACHABLE after 4 trips; Zarthal:
   REFUSE_NO_KNOWN_BUYER for ingots; Aurelius: vendor_buy rejected
   "You cant reach the Vendor"; Thalia: mage "does not stock a
   scroll"). Owner: vendor-trade / economy planner.
   bot/uo-client/src/economy/Faucets.cpp:66,74 (refusal reasons),
   bot/uo-client/src/life/Runner.cpp:3481.

6. Action timeout: "X (no server confirmation)" on cast_spell,
   use_object, vendor_sell, move_item - 10 chars, dominant on mages and
   mid-craft crafters: Illyria cast_spell x26, Thalia use_object x15 +
   move_item x4, Elara use_object x6 + move_item x3, Selene cast_spell
   x6, Dorvar vendor_sell x5, Leander/Lyra cast_spell x2 each, Hector
   use_object x4 (superseded by attack), Ithion/Xerxes use_item_on x1-2.
   Owner: protocol / action-confirmation (client action state machine
   not resolving before its own deadline, action self-superseded).
   bot/uo-client/src/Client.cpp:2721.

7. Crafting professions with no sellable recipe defined - 7 chars
   (Aelia, Amara, Halain, Odessa, Serena, Vorar, Cyras) hit
   BLOCKED_NEED CRAFT: this life makes nothing sellable (nothing to
   make) repeatedly (Aelia x44). Tailor and merchant_tinker lives never
   escape this; two lumberjacks hit it too. Owner: planner / life
   template data (profession catalogue missing a produce-then-sell
   recipe for tailor/merchant_tinker).
   bot/uo-client/src/life/Identity.cpp:458.

8. Goal telemetry reporting nonsensical/zero progress plus explicit
   goal_spinning bug markers - Aelia (goal_completed=REPLACE_EQUIPMENT
   progress=0 x15, EARN_GOLD progress=0 x15, both flagged
   goal_spinning ... "this is a bug in that goal, not pacing"), Illyria
   (REPLACE_EQUIPMENT progress=101), Xerxes (REPLACE_EQUIPMENT
   progress=243), Amara/Odessa (goal_spinning). Owner: planner
   (goal completion/progress accounting).
   bot/uo-client/src/life/Runner.cpp:2483.

9. Moongate (3564,2140) approach friction - Ithion (10x no-path over
   ~90s before arriving), Faustus (1x no-path + escape reroute), Xerxes
   (route exhausted right at kill time). Same spot flagged in wave 1.
   Owner: navigation (navgrid around the moongate tile). Same sites
   as clusters #2/#3.

## Notes

- No character reached logout_complete; every "first thing wrong" here
  is mid-session, not an end-of-run failure.
- 9/30 characters produced a genuine economic/production result in the
  ~5-6 min window despite the friction above: Elvar and Odessa (player
  trade), Rhaler and Ghalor (smelt), Elara and Thalia (craft), Dorvar and
  Ithion (fish + sell), Zarthal (craft a dagger), Hector and Castor (real
  combat engagement). The other ~19 chars achieved nothing tangible
  in-window.
- Four miner_smiths (Draver, Kharain, Elvar, Zarthal) never produced a
  single logged mine: event despite being the profession whose entire
  purpose is mining - worth a follow-up with a longer run before
  concluding this is a defect vs. just truncation.
