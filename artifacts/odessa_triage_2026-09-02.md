# Odessa triage — 2026-09-02

Source: bot/uo-client/run_gates/g_Odessa.console.txt (~13:45:57-13:55:32),
g_Odessa.err.txt. Character: merchant_tinker, home Britain (start city idx 2).

## Timeline
- 13:45:57 spawn -> EXPLORE loop around Britain (goto_start/goto_done pairs,
  arrived, goal_completed=EXPLORE progress=1 each time it merely scans
  nearby mobiles) console.txt:56-190
- 13:46:53 moongate gump -> chooses 'Britain' (already home; re-entering
  town via gate as part of an explore leg) console.txt:142-177
- 13:46:02 goal=TRADE_WITH_PLAYER "no goal was running" console.txt:80
- 13:48:52 WARN no path to (1425,1690); goto stopped short by 23 tiles
  err.txt:4-5, console.txt:290
- 13:48:54-13:51:32 goal=BUY_SUPPLIES: shouts "WTB 8 i_ingot_iron 52gp"
  every ~8s, 22 times, nobody answers console.txt:298-418
- 13:51:55 both real needs now BLOCKED_NEED:
  - TRADE_WITH_PLAYER: "previous goal abandoned: another profession makes
    this, not a shopkeeper" -> cooldown 599s console.txt:433,438
  - BUY_SUPPLIES: "previous goal abandoned: nobody was selling" ->
    cooldown 119s console.txt:433,446
- 13:52:01 WARN move REJECTED, 0x20 resync, path aborted err.txt:6-7
- 13:52:30 EXPLORE goal=EXPLORE "no goal was running", score=15.0,
  reason "fallback: nothing else is actionable, so go and learn the
  world" console.txt:510-520
- 13:52:33 explore picks 'britain_start' (~7 tiles) console.txt:517-520
- 13:52:38 explore picks 'cove_provisioner', ~1360 tiles away, moongate
  plan legs=33 nodes=20004 console.txt:556-564
- 13:53:39 "You have left the protection of the city guards" en route
  console.txt:625
- 13:55:03-13:55:32 pre-armed moongate to 'Minoc', another EXPLORE pick,
  goto_start jumps to (2696,7xx) console.txt:719-788,766-788

## Root cause
EXPLORE is the designed no-idle fallback (Goals.cpp:424-469, RestPlan.cpp
comment "blocked for want of knowing where things are") and is *supposed*
to outrank idle. It is working as designed in isolation, but two things
combine to make Odessa "always going somewhere" instead of settling into
her merchant_tinker loop:

1. Her two real profession needs die fast and go on cooldown:
   BUY_SUPPLIES gives up after ~2m40s of unanswered street shouts
   (nobody was selling) -> 119s cooldown; TRADE_WITH_PLAYER self-aborts
   immediately ("another profession makes this, not a shopkeeper") ->
   599s cooldown. With both blocked, EXPLORE (score 15.0) is the only
   feasible goal essentially all the time.
2. EXPLORE's target picker, `Client::TravelToUnexploredPlace` (called at
   Runner.cpp:12756, matched via NotePlace-recorded ids from
   Runner.cpp:12702), has no locality/radius preference — it will nominate
   ANY unvisited atlas place regardless of distance from home, so a
   merchant_tinker who lives in Britain gets sent to Cove (~1360 tiles)
   and then Minoc mid-run. That is the visible "always going somewhere."

Not a goal-spinning case (each EXPLORE genuinely completes,
progress=1) and not a travel failure (arrivals mostly succeed, one
partial: goto finished 23 tiles short of (1425,1690), console.txt:290).
