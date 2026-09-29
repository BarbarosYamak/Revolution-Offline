# Tailor pasture picker smoke — Aelia + Wren, 2026-09-04

Gate: `python tools/rev.py gates CHARS=Aelia,Wren MINUTES=5` then `wait`.
Run tags: g_Aelia (RevGen3_12), g_Wren (RevGen3_31). Console logs in run_gates/.

## Aelia (started at Delucia 5195,3991, home Britain)

- 17:44:07 cloth: "no sheep in sight -- walking to the flock of 15 at 1321,1817, 3872 tiles off, nearest to home (trip 1)" [line 269]
- 17:44:07 travel pasture -> (1321,1817) r=4 from (5193,3983), mode=moongate, plan ok legs=45 nodes=8743 [270-277]
- Walking to the Britain-side moongate through Delucia's monster-populated overworld; attacked by Fire Elemental (17:47:31), hellcat (17:47:53), dread spider (17:47:45) — HP dropped to 6 at one point (17:48:56) but she recovered (drank potions, left war mode); no death.
- 17:48:55-56: got stuck ("sealed in") near (5947,1373) — same spot as a 92-min-old decayed corpse from a prior session (line 42) — recovery-walk exhausted 3 attempts, travel_failed "sealed in; recovery exhausted" [610-647]. Not a pasture-picker defect: this is a pre-existing stuck-spot near Delucia unrelated to flock selection.
- After recovery she resumed moving toward Britain via moongate (transit=3, legs=48) and ended the 5-min session at (1498,1640) — Britain mainland, en route home — logged out clean ("wind-down: trip past its deadline ... logging out where I stand").
- No route through any a_trinsic_passage* region or "Trinsic" gate chosen by the pasture goal (only mention of Trinsic is "skipping Trinsic Healer" place-selection logic, irrelevant to pasture).
- session_summary: deaths=0, kills=0, gold 9100->8980.
- state.json (bot_data/revgen3_12.aelia/state.json): death_count=0, recent_deaths=0.

## Wren (started at 1915,2809 near Trinsic, home Britain)

- 17:44:04 cloth: "no sheep in sight -- walking to the flock of 15 at 1321,1817, 987 tiles off, nearest to home (trip 1)" [line 214]
- 17:44:04 travel pasture -> (1321,1817) r=4 from (1912,2804), mode=moongate, plan ok legs=17 nodes=4367 [215-222]
- 17:46:30 pre-armed moongate destination 'Britain'; 17:46:32 used moongate 'Britain' from Trinsic gate (gump choice 'Trinsic' -> dest 'Britain') — standard moongate travel, not overland Trinsic Passage.
- En route, attacked by Klapdud (17:45:51), Ettin (17:46:11), Ghoul (17:46:24); all survived, hp 50/50 by 17:48:44.
- 17:47:31 arrived at flock: "a sheep 10 tiles away -- walking up to it"; 17:47:45 "shearing a sheep (0 wool carried, want 7)"; wool 0->1 by 17:48:00. Reached and used the correct Britain flock.
- Session ended clean at (1312,1804), 5 tiles from the flock: "wind-down: arrived somewhere safe at 1312,1804", logout_complete.
- session_summary: deaths=0, kills=0, gold 9100->8980.
- state.json (bot_data/revgen3_31.wren/state.json): death_count=0, recent_deaths=0.

## Verdict inputs
- Pasture pick for both: (1321,1817) — the Britain flock (matches expected 1318,1811 area), zero picks at Jhelom/Yew/Delucia.
- Zero deaths, zero pasture-goal deaths.
- No Trinsic Passage or Lost Lands routing chosen by the pasture goal itself; all travel used moongate mode.
- Aelia's walk toward a Delucia-area "sealed in" stuck spot is a separate, pre-existing issue (matches a 92-min-old corpse location from a prior session), not caused by this fix and not a flock/route defect.
