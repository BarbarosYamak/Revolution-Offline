# Tailor loom/sash smoke #2 — Aelia — 2026-09-02

Gate: `python tools/rev.py gates CHARS=Aelia MINUTES=5` -> `wait CHARS=Aelia`
Logs: `run_gates/g_Aelia.console.txt` (726 lines), `run_gates/g_Aelia.err.txt` (21 lines)
State before: `run_gates/g_Aelia.state_before.json` (family=tailor, str50/dex20/int10 build)

## Link-by-link

1. MAKE_CLOTH picked
   - 23:05:01.851 `goal=MAKE_CLOTH reason="previous goal abandoned: nothing on the equipment list could be replaced"`
   - re-picked 23:06:13.890 `goal=MAKE_CLOTH reason="previous goal abandoned: nothing to deposit"`

2. Loom target chosen + position vs pieces
   - 23:05:42.919 `[ACTION] use_item_on item=0x40018074 target=0x40000472`
   - 23:05:42.919 `[target] object 0x40000472 (1473,1685,0) model 0x1061` — this is the NEAR piece (task-given coords 1473,1685). The far piece 0x40000465 @1474,1685 was never targeted.
   - Aelia's resolved position at that goto leg: (1472,1685,0) (line 456) — one tile from 0x40000472, matching the fixed nearest-piece selection.

3. Server reply after loom target
   - 23:05:42.934 `[ACTION_RESULT] use_item_on success (14ms) item consumed by the destination`
   - Journal: `System: The bolt of cloth is finished.` / `System: You put the bolt of cloth in your pack.`
   - No timeout, no CanTouch refusal — fix confirmed working (previous run: 3x use_item_on timeout on far piece).

4. i_cloth_bolt in pack
   - Confirmed indirectly: 23:06:12.208 `banking 1 i_cloth_bolt` -> `[ACTION] move_item serial=0x40012E08 amount=1 dest=0x40013D88` -> success.

5. Scissors dclick -> bolt target -> cloth count
   - NOT OBSERVED. No scissors action logged this run.

6. wool -> spinning wheel (0x40000477 @1475,1689) -> yarn
   - NOT OBSERVED. Aelia started with 6 yarn already (per task) and wove those directly; her 5 wool was never touched, no spinning-wheel action logged.

7. CRAFT i_sash (sewing kit gump sequence)
   - NOT OBSERVED. No "double-clicking the sewing kit" / "giving the sewing kit cursor" / "chose 'Misc.'" / "chose 'sash'" lines anywhere in the console log.
   - After banking the 1 bolt, MAKE_CLOTH re-picked but she needed 18 x i_yarn_ball and had 0 gold (`BLOCKED_NEED BUY_SUPPLIES ... i_cloth_bolt needs 18 x i_yarn_ball -- and the purse is empty`), so she began a long cross-map goto (toward a supplier?) instead of ever reaching scissors/wheel/sewing-kit steps.

8. goal_failed / handoff / death
   - No `goal_failed`, no `HandOff`, no death lines anywhere in the console log.
   - She was attacked en route (Sthsasist, Cougar) at 23:08:19-30, HP 24->18->11 (`event threat` x2), travel halted per existing HP-watchdog TODO, but the gate's 5-minute window ended before further escalation.
   - Clean logout at HP 11 (not dead): `session_summary duration=410s goals=1/5 ... deaths=0`, `checkpoint (clean logout)`, `logout_complete: acked`.

## Verdict per link
1. PASS
2. PASS (nearest-piece fix directly confirmed)
3. PASS
4. PASS (inferred via bank deposit of the exact item)
5. BLOCKED — not exercised this run
6. BLOCKED — not exercised this run
7. BLOCKED — not exercised this run (upstream: gold=0 blocked buying yarn/wool inputs, so no bolt count ever reached the sash-craft need)
8. PASS (none occurred; run ended safely, not a ghost)
