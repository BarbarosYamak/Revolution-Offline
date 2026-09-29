# Scribe smoke — Lyra & Thalia — 2026-09-04

Two consecutive gate windows were observed live. Wave 1 (start ~12:37:41,
session_end ~12:42:41/44) finished and was overwritten before `rev.py
wait`/`grade` were called. Wave 2 (start ~12:45:18/21, session_end
~12:50:18/21) is the run `rev.py wait CHARS=Lyra,Thalia` and `rev.py grade
CHARS=Lyra,Thalia` actually captured and graded; it is the basis for the
verdict below. A third wave began ~12:52:36 after grading, outside this
task's scope (console files get truncated on relaunch, `g_<Char>.console.txt`
is always "latest run only").

Raw per-minute snapshots: `scribe_smoke_2026-09-04_raw.txt` (wave 1),
`scribe_smoke_2026-09-04_run2_raw.txt` (wave 2).

## Owner's rule of the day
Bulk-buy blank scrolls + reagents from the NPC scribe FIRST, then MANY
consecutive inscription sittings, then sell/bank. Not one small batch per
goal-pick; not opening with a horse/comfort buy.

## Wave 2 timeline (graded run, 300s each)

### Lyra (12:45:18 start)
- t+0:20 first real goal `CRAFT` supersedes the login `FILL_SPELLBOOK` pick
  (score 65.0 vs 54.1). No BUY_SUPPLIES ever ran before this — the craft
  used **pre-existing pack stock** ("pack funds 77 i_scroll_poison -- one
  sitting of 77, not 5"), left over from a prior session, not bought this
  session.
- One sitting: 8x i_scroll_poison made (pack 10->18) in ~19s, then
  `EARN_GOLD` (score 76.5) preempted the sitting mid-way (77 available,
  only 8 made).
- `EARN_GOLD` never sold anything: `goal_failed=EARN_GOLD reason="tried
  all 1 trades that buy i_scroll_poison" -- standing down for 180s`.
- `BUY_SUPPLIES` picked once (t+3:19, score 133.0), immediately
  `goal_blocked=BUY_SUPPLIES reason="not enough session left for the trip"
  tiles=4 left=98s need=121s` -> cooldown 119s. Zero blank scrolls or
  reagents bought this session.
- Remaining time: 3x single-item (`qty=1`) vendor buys of individual spell
  scrolls for `FILL_SPELLBOOK` (~20-24gp each) — comfort/spellbook
  purchases, not bulk craft-input buying.
- Session end: `goals=0/5`, `gold=8742->8673` (net loss, the 8 scrolls
  made were never sold), `skills=120.9->120.9` (flat — no skill gain from
  the one sitting), `families=2 top=80%` (single-family dominated).

### Thalia (12:45:21 start)
- t+0:22 `EARN_GOLD` supersedes login `FILL_SPELLBOOK` (67.5 vs 56.4).
  Travels to a 'mage' vendor, sells pre-existing stock in a fragmented
  "shrinking lot" pattern (offer 15 -> refused (vendor purse dry) -> retry
  7 -> refused -> retry 3 -> accepted; repeats per counter across 3 visits)
  — noisy but does complete real sales.
- `CRAFT` picked (t+1:15): one sitting, 1->17 i_scroll_poison made in ~44s
  from pre-existing pack stock ("one sitting of 70, not 5"), again
  preempted by `EARN_GOLD` (76.5 vs 65.0) before the material ran out.
- `EARN_GOLD` again, more shrinking-lot sells.
- `CRAFT` picked a 2nd time (t+2:59): 2x "the scroll was ruined and the
  blank is spent" with **no** new `made` lines — burns its last blanks on
  failures, then `EARN_GOLD` preempts again before finishing.
- `CRAFT` picked a 3rd time (t+3:59): instantly
  `goal_blocked=CRAFT reason="REFUSE_REQUIRED_FOR_PRODUCTION" i_scroll_poison
  short of 1 x i_scroll_blank` — completely out of blank scrolls.
- Only now (t+4:05-4:17, last ~55s of the session) does it bulk-buy: two
  `qty=87` vendor purchases from the same shop. This is the only bulk buy
  either character made all session, and it happened reactively after
  running dry mid-craft, not proactively up front.
- Immediately `goal_changed=GET_FOOD from=BUY_SUPPLIES` (BUY_SUPPLIES score
  dropped to 0.0, "satisfied") — the 2x87 units just bought are never used
  to craft this session. `GET_FOOD` then spams
  `food: Create Food is short of i_reag_garlic -- buying food instead this
  time` (dozens of lines within ~1s) through session end.
- Session end: `goals=0/8`, `gold=8725->9542` (net +817, real sales),
  `skills=130.1->130.1` (flat despite ~20 scrolls made+sold), `families=2
  top=88%`.

## Grade (`rev.py grade CHARS=Lyra,Thalia`, family=scribe)
- Lyra: 11/18 — FAIL FARM-2, TRAIN-1, TRAIN-2, STOCK-4, LIVE-1, LIVE-4, LIVE-5
- Thalia: 13/18 — FAIL TRAIN-1, TRAIN-2, LIVE-1, LIVE-4, LIVE-5

## goal_spinning / BLOCKED_NEED
- No `goal_spinning=` lines either console (LIVE-3 passes both).
- `BLOCKED_NEED BUY_MOUNT` on cooldown every tick both characters (stale,
  pre-existing, not scribe-specific).
- `BLOCKED_NEED BUY_SUPPLIES`/`EARN_GOLD` cooldowns as noted above.
