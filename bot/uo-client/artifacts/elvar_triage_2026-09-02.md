# Elvar triage — 2026-09-02

Source: bot/uo-client/run_gates/g_Elvar.console.txt (947 lines, 13:46:03-13:52:37)
         bot/uo-client/run_gates/g_Elvar.err.txt (13 lines, only WARN move-rejects + resync)

## Goal timeline (only 2 goal_changed events logged; a 3rd transition back to
MINE at 13:52:16 fired the DoMine handler directly with no goal_changed line)

- 13:46:03 MINE active. Travels to Minoc Mine 1 interior (2568,487), strikes
  rock, collects ore across ~13 strike cycles.
- 13:48:37 goal_changed=SMELT from=MINE, "SMELT 91.0 superseded MINE 76.1".
  needs considered: NeedSmelt 0.65, NeedOre 0.59, NeedTrade 0.55,
  NeedGold(sell surplus) BLOCKED 0.55, NeedCraft 0.50, NeedSkillTraining 0.45.
  BLOCKED_NEED EARN_GOLD: "35 x i_ingot_iron spare, and no buyer known".
- 13:48:38-13:48:49 smelt: first forge (2561,501) unreachable — "cannot get
  within 1 tile of the forge ... after 4 tries -- looking for another"
  (console.txt:359). This is the "going around" the owner saw: 3 travel_start/
  travel_done round-trips to the same (2560,500) staging tile, no progress.
- 13:48:49-13:49:59 falls back to "Minoc blacksmith" forge (2468,557), travels
  there (71s), successfully smelts twice: 16 ore then 10 ore -> total spare
  rises to 45 x i_ingot_iron (console.txt:447-475).
- 13:50:11 goal_changed=TRADE_WITH_PLAYER from=SMELT, "79.8 superseded 0.0".
- 13:50:13 goal_blocked=TRADE_WITH_PLAYER reason="not enough session left for
  the trip" left=646s need=800s (Runner.cpp:2879-2887).
- 13:50:14 needs considered: NeedOre 0.59, NeedGold BLOCKED 0.55, NeedCraft
  0.50, NeedSkillTraining 0.45, NeedBank(put unsold stock away) 0.40,
  NeedTrade BLOCKED 0.00. BLOCKED_NEED TRADE_WITH_PLAYER: "on cooldown for
  another 599s after achieving nothing".
- 13:50:14 -> back to MINE (travel_start Minoc Mine 1 interior), no
  goal_changed log line emitted for this transition.
- 13:52:16-13:52:37 mining resumes normally, ore collected into pack.

## Live world state (tools/world_query.py --char Elvar)

    PACK i_ingot_iron x27, i_ore_iron x5   (unbanked, in pack)
    BANK i_ingot_iron x38                  (banked from an earlier session)
    SKILL Mining 50.3, Blacksmithing 50.1, Tinkering 20.6
    GOLD pack=714 bank=9230

Confirms real carried surplus at end of window: 27 ingots + 5 ore riding in
the pack, not deposited, while MINE remains the active/re-picked goal.

## Root cause

`NeedBank` "put unsold stock away" is pinned to a flat 0.40
(bot/uo-client/src/life/Needs.cpp:557, comment block 529-556). The comment
states this is deliberate: 0.40 sits below NeedTrade's live urgency (~0.49)
so the character tries to sell before banking. But once BOTH EARN_GOLD
(BLOCKED_NEED, no buyer known) and TRADE_WITH_PLAYER (blocked: session-time
gate, then 599s cooldown "after achieving nothing") are blocked, the
comparison the 0.40 constant was tuned against no longer holds — NeedOre
(0.59, unblocked, dynamically scored) keeps outscoring NeedBank every cycle,
so the planner re-picks MINE instead of BANK. Elvar mines more, which only
grows the unsellable surplus, and never deposits what he is already
carrying. No goal_spinning fired because each MINE/SMELT cycle does make
real (if useless) progress — ore is genuinely struck and smelted, so the
anti-spin backstop (Runner.cpp:2653) does not trigger.

Secondary, lower-impact defect: the first forge pick (2561,501) is
unreachable (4 failed within-1-tile attempts before giving up, ~11s wasted
in place) before falling back to the Minoc blacksmith forge — this is the
visible "going around" near the mine/forge before smelting actually starts.

## Not the cause (ruled out)

- Missing pickaxe/tools: `held=[pickaxe,smith hammer,]` present throughout.
- goal_spinning backstop: never fired in this window.
- Cooldown blocking MINE itself: MINE was never on cooldown; it's chosen
  every time it's the top unblocked need.
- BLOCKED_NEED on NeedBank itself: NeedBank was never logged as BLOCKED —
  it simply loses the scoring comparison to NeedOre.
