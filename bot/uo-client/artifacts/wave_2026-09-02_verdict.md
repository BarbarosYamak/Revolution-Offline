# Wave 2026-09-02 09:09-09:40 verdict evidence

NOTE: run_gates/ contains 141 stale files from prior sessions (minoc_fix_*,
minoc_gate_*, minoc_verified_*, mk3_*). All queries below are restricted to
the 30 g_*.console.txt/g_*.err.txt files of this wave only.

## Connection / crash (roster criterion)
30/30 characters connected at their start timestamp and ran to a last
timestamp ~30min later (see per-char first/last table, all g_Draver..g_Zarthal).
No Traceback/FATAL/Unhandled/crash string found fleet-wide (0 matches).
5 chars needed >30min to actually send logout: Castor(+1:18), Aurelius(+1:29),
Rhea(+2:33), Illyria(logged out cleanly at 09:39:57, within window),
Hector (see below, STILL RUNNING, has not logged out as of 09:45).

## a) Moongate reuse
Xerxes: 158x "using moongate 0x4000289E for 'Ocllo (Newplayers)' (gump
active=0 serial=0x00000000)", 09:10:49.414 -> 09:39:25.900 (whole run).
Gump 0x80A0041C (13 options/15 texts, destination picker) opened exactly
once at 09:10:47.002 and never answered (no button-press log line found).
Vorar: 18x same pattern across Britain<->New Magincia gate toggling,
09:10:49.788 -> 09:37:10.126.

## b) Concurrent trade windows
Odessa opened trade with Kharain 09:11:48.335 then with Elvar 09:11:50.703
(two live windows for one WTB). Kharain put nothing in after 25s ->
cancelled 09:12:14.784. Resulting cancel state then spammed
"life] trade:  cancelled (partner_cancelled)" at ~60ms cadence, 200x each
in Odessa/Kharain/Elvar logs, 09:12:14.785 -> 09:14:20.663 (self-resolved
when GET_FOOD superseded TRADE_WITH_PLAYER). Same family also hit Draver
and Titus as goal_spinning=TRADE_WITH_PLAYER ("completed 5 times in a row
with progress 0") at 09:29:58.339 and 09:26:43.359 respectively -- 5
characters total affected. 0 trades fleet-wide ever completed (only
"trade_open success" = window opened, no accept/complete event exists in
any g_* log).

## c) Fish equip loop
Dorvar: 200x "fish: arming the pole" 09:09:59.349->09:19:42.363, paired
1:1 with "put the fishing pole in your pack" and drag_cancel reason=5,
alternating with "cutting a whole fish" (200x each).
Ithion: 200x 09:11:15.768->09:20:36.413, same pairing.
FISH goal repeatedly re-picked ("no goal was running" -> FISH) after being
superseded by GET_FOOD/GET_TOOL, recurring through the whole run, not a
single contiguous stretch.

## d) Tame: nothing tamable
Rhea: first "tame: nothing tamable here (Taming 50.0)" fired at
09:10:15.349, 64ms after server login-reconciliation completed (position
630,975 -- not even at a pasture yet). 9 "nothing tamable" trip lines
total across 3 pasture coords (572,1098 / 669,943 / 669,1175), 3 full
goal_failed=TAME_ANIMAL cycles (09:14:21, 09:22:31, 09:37:04+), one
goal_spinning=TAME_ANIMAL flag at 09:35:41.466 ("completed 5 times in a
row with progress 0"). Zero occurrences of "c_sheep_woolly" anywhere in
Rhea's log -- confirms no name/entity scan is performed before judging.

## e) Reagent-less practice casts
"You lack Sulfurous Ash": Aurelius 100 events 09:16:10.761->09:30:08.494,
Illyria 100 events 09:10:02.606->09:20:32.396.
"You lack Mandrake Root": Elara 293 events 09:10:05.098->09:20:02.114,
Selene 299 events 09:09:32.726->09:19:30.190.
No restock/buy-reagent goal observed interleaved in any of the 4 logs.

## f) Eat loop at Britain provisioner
Item: bread loaves/bread loaf, serial changes across the run (observed
0x40010BE3, 0x4000D130, 0x40015F7B, 0x40019648 for Halain alone -- new
purchases do happen, but each purchased item then also gets stuck).
Fleet-wide only 2 "You eat the food"/"manage to eat the food" success
chat lines PER CHARACTER for all 9 present chars (Halain, Leander,
Odessa, Cyras, Zarthal, Rhaler, Ghalor, Elvar, Kharain) -- i.e. eating
only ever truly completed once (the character's very first 2-loaf
purchase), then never again. use_object then times out on ~4s cadence
forever after: Halain/Leander/Odessa/Cyras/Zarthal/Rhaler/Ghalor each
logged exactly 200 "food: eating (hungry=1 starving=0, carrying 2)" +
200 "use_object timeout" pairs. Elvar/Kharain only attempted twice total
(both succeeded) -- they were occupied by the trade-window spin (b)
instead. GET_FOOD abandoned at "ran 300s without finishing (limit 300s)"
and re-picked repeatedly (Halain: 5x, 09:18:59 onward). Petra is not in
this roster (no g_Petra.* files exist).

## g) CRAFT goal_spinning REFUSE_MISSING_RECIPE
Dorvar: i_fish_cut_raw, 110x 09:11:17.385->09:38:51.437, 22
goal_spinning=CRAFT flags.
Vorar: i_board, 45x 09:23:33.794->09:38:03.008, 9 goal_spinning flags.
Serena: i_gears, 30x 09:13:53.679->09:20:23.145, 6 goal_spinning flags.

## h) Deaths
Hector: ghost at session start (09:10:00.460 "ghostly hand"),
resurrected by Natasha at 09:11:21.952. Repeatedly HEAL<->RECOVER_CORPSE
handoff ("too hurt to walk back into whatever did this"), corpse at
(1457,1602) reached at 09:21:47.999 ("last corpse" travel_done) but
open_container on it returns "invalid_state ... null serial" every
~1.5s -- 200+ occurrences logged by end of console capture. RECOVER_CORPSE
repeatedly abandoned at the 300s goal limit and re-picked (09:26:26,
09:31:27, 09:36:27, 09:41:27...) forever. AS OF THIS REPORT (09:45, run
nominally ended 09:40) Hector's process is STILL LIVE, still spamming
open_container invalid_state, has NOT logged out -- the 30-min session
limit did not force a clean shutdown from this stuck state. NEW DEFECT:
corpse container becomes permanently unopenable (invalid serial) and the
bot has no fallback/abandon path, nor does session-limit enforcement
override a stuck action loop.
Illyria: resurrected by Natasha at 09:11:21.952 (same event, same
location as Hector), logged out cleanly at 09:39:57 -- no lingering
issue.
Lyra: alive="alive" confirmed by server at session start (09:09:35.697)
and no death/ghost/resurrect/corpse line anywhere in this run's log --
her earlier death (per prior-session memory) is historical, not
reproduced in this wave.

## New defect (not in the 8 listed families)
Hector's post-resurrection corpse-container permanently invalid
(open_container invalid_state / null serial) blocking RECOVER_CORPSE
forever and preventing clean session-limit logout -- see (h). This is
the highest-severity finding of the wave: it is the only defect that
left a bot connected and spinning past the intended 30-minute test
window at report time.

## Positives (g_* files only)
- NPC vendor sales with confirmed gold increase: 5 events -- Dorvar x3
  (10618->10666->10740->10816), Falen x1 (9826->9952), Ithion x1
  (10096->10128). "vendor_sell success" (incl. menu-open) = 28 lines/14
  events total.
- Smelting: 11x "smelt: opening the forge", 11x "giving the forge's
  cursor the ore" producing ingots (e.g. Draver "17 ore, 2 i_ingot_iron
  so far"; Ghalor rusty ore variant). Real ore->ingot conversion observed.
- WTB/WTS speech: 1460 lines fleet-wide (buy+sell offers combined).
- Player-to-player trades completed: 0. Only "trade_open success" (window
  opened) exists; no accept/complete event in any log; the one real
  attempt (Odessa/Kharain/Elvar) cancelled.
- CRAFT goal succeeded: 0 ("CRAFT.*success"/"craft_success" = 0 matches
  fleet-wide).
- Skill gains: 0 explicit skill-increase log lines found fleet-wide
  ("you have gained" only matches fame/karma for Castor, not skill
  points). No skill_N value change observed between session-start
  reconciliation and end of run in the sampled logs.
