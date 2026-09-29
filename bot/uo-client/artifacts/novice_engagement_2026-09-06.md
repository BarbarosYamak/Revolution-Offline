# Novice engagement policy (2026-09-06)

How a low-skill, low-hp fighter opens and breaks off a fight in the WEAK band
of the Britain graveyard. Tier *routing* was fixed separately (commit 1485ef5,
`artifacts/hunt_tier_gate_2026-09-06.md`); this is about how a novice fights
once it is in the right yard.

Code: `src/life/runner/NoviceEngage.h` (pure policy + constants),
`src/life/runner/Train.cpp` (the open decision),
`src/life/runner/Survive.cpp` (the break-contact decision).
Test: `tests/novice_engage.cpp` (23 checks, `ctest -R novice_engage`).

## 1. The evidence

Hector: fencer, 51 hp, Fencing 50.7, starter leather, ~6-7k gold banked.
He died twice on 2026-09-06 in the weak band, both times to a group, both
times with the flee interrupt firing on time *by its own rule* and too late
*by the clock*.

**Trace A — 02:16, two `c_skeleton`.** Source:
`artifacts/wave2_observation_2026-09-06.md`, section "02:16 -- DEATH";
Sphere log `02:16:P'Hector' was killed by N'Skeleton'., N'Skeleton'.`

    hp 51 -> 34 -> 27 -> (potion) 41 -> 32 -> 30 -> (potion) -> 13%
    02:16:23  interrupt=FLEE "HP 25%; 2 attacker(s); bail at 30%"
    02:16:24  dead at 1379,1485, region a_britain_graveyard_1

The retreat began with 13 hp and lasted one second.

**Trace B — 23:36:39-23:38:10, Skeleton + Skeleton + Spectre at 1389,1505.**
Source: `run_gates/g_Hector.console.txt`; Sphere log
`runtime/logs/sphere2026-09-06.log` `23:38 ... P'Hector' was killed by`.

| clock | hp | delta | window |
|---|---|---|---|
| 23:37:21.5 | 51/51 | | |
| 23:37:26.6 | 46/51 | -5 | 5.0 s |
| 23:37:31.6 | 42/51 | -4 | 5.0 s |
| 23:37:46.7 | 41/51 | | |
| 23:37:56.8 | 51/51 | potion | |
| 23:38:01.8 | 38/51 | -13 | 5.0 s |
| 23:38:02.4 | 31/51 | -7 | 0.6 s |
| 23:38:07.5 | 21/51 (41%) | -10 | 5.1 s |
| 23:38:10.6 | DEAD | -21 | 3.1 s |

Printed bail line throughout: **"bail at 22%"** — 11 hp of 51, under two
landed blows. Three potion attempts were refused by the server
(`You can't drink another potion yet` at 23:38:00.7, 23:38:04.8, 23:38:08.8),
so the potion cadence is not a resource this policy can spend.

Derived, from those rows only — no Sphere damage formula is assumed, and this
character's AR is still not observable (`hunt_tier_gate_2026-09-06.md` s.3):

- **8 hp per landed blow.** Largest isolated single-blow drop is 7 hp
  (23:38:01.8 -> 02.4, 0.6 s); trace A's 17-hp drop across one poll with two
  attackers averages 8.5 each. Round up: never be optimistic about the line.
- **One reaction window is ~3-5 s.** The survival tick reschedules 2 s out and
  the hp watchdog reports every ~5 s, so a bail decision is acted on up to one
  poll late. Both deaths measure exactly one window between the last decision
  and the corpse: trace A fled at 13 hp and died 1 s later; trace B was last
  seen at 21 hp and died 3.1 s later.
- **Sustained damage scales with the board.** Single-attacker phase
  (23:37:21-23:37:51, `1 attacker(s)` per the interrupt line) cost 12 hp in
  30 s = 0.4 hp/s. Three hostiles cost 21 hp in 3.1 s = 6.8 hp/s, a 17x
  difference. The group, not the creature, is what kills a novice.

The atlas puts both death spots inside `a_britain_graveyard_1`, an unguarded
region — no guard line to shout at, and the healer Hector walked to after the
23:38 death took **~55 s of ghost travel** (23:38:11 "walking to a healer
(trip 1)" -> 23:39:06 "arrived at healer destination"). There is no cheap
rescue at this ground; the only defence is leaving early.

## 2. Rule 1 — do not open on a group

A novice may open a fight only when the board is a duel: at most one hostile
within `combat::kCrowdRadius` (4 tiles) **and** at most one within
`novice::kSoloRadius` (8 tiles). 8 is twice the crowd radius and covers the
Britain graveyard's 6-7 tile ring spacing (`hunt_tier_gate_2026-09-06.md`
s.1), which is how a pull in one ring becomes a fight with two.

`Train.cpp` previously refused only 3+ in reach; that gate passed trace A's
pair unchanged. It is now `!NoviceMayOpen(inReach, nearby)` for a novice and
unchanged (`inReach >= 3`) for everyone else. The refusal still logs
`engage=no`, notes the spot as busy and stands the goal down.

**Break contact when a second one joins**, at whatever health — not at 25%.
`Survive.cpp` drops a novice's crowd tolerance from the nerve-derived 2 (a
0.75-nerve fencer) to 1, and its in-reach ceiling from 3 to 2.

## 3. Rule 2 — a retreat floor in hits, not in percent

    RetreatFloorHp(attackers) = attackers * 8 hp/blow * 2 blows of margin

Two blows is the smallest margin the evidence supports: one window is exactly
what killed him in both traces. On Hector's 51-hp bar that reads

| board | floor hp | fraction of 51 |
|---|---|---|
| 1 | 16 | 31% |
| 2 | 32 | 63% |
| 3 | 48 | 94% (clamped to 90%) |

— fight one, leave two, never stand in three. The board is
`max(attackersOnMe, inReach)`: everything inside the crowd radius is about to
be an attacker.

This is applied as `bailAt = max(nerve line, retreat floor)` — **it can only
move the line up.** A 100-hp veteran with one attacker floors at 16%, below
the 0.32 default, so nothing that survives today is made bolder. Trace A's
`bail at 30%` becomes 63%; trace B's `bail at 22%` becomes 90%.

The interrupt now prints the floor and whether it is the binding term:

    interrupt=FLEE reason="HP 41%; 3 attacker(s); bail at 90% (retreat floor 90% for 3 on the board, binding)"
    disengage=yes attackers=3 in_reach=3 tolerate=1 novice=1 hp=41% ... reason="a second hostile joined and this one fights duels"

## 4. Who is a novice

`IsNovice(bestWeaponTenths, hpMax)` = `tenths < 600 || hpMax < 70`. Hector was
507 / 51 at both deaths. These bounds are the measured profile rounded up one
step — a **policy choice about where the band ends, not a measured cliff** —
and deliberately generous so the character that actually died sits well
inside. Marked as such in the header rather than dressed up as evidence.

## 5. Rule 3 — heal between fights: already covered, no change made

Checked, not re-implemented. `Train.cpp` already gates every re-engagement:

- `obs.HpFraction() < needCfg_.healHpFraction` (0.80, `life.h:1203`) ->
  hand off to `HEAL`, "recover before opening another fight" (`Train.cpp:297`);
- no bandages, no heal potion and no heal spell -> hand off to
  `REPLACE_EQUIPMENT`, "restock healing supplies before the next fight"
  (`Train.cpp:307`);
- below the bandage floor (`bandageLow` in town, `bandageFieldLow` in the
  field) -> restock first.

Trace B confirms these ran: at 23:36:49 and 23:37:51 `HEAL` superseded the
active goal on the bandage need, and the character bandaged and bought
potions after each death. The gap in trace B was **not** a missing heal gate —
it was that the character healed *where it stood*, inside the graveyard, with
three hostiles on it. Rules 1 and 2 above are what remove that situation;
"heal somewhere else" is a `DoHeal` relocation question and is **not** changed
here.

## 6. Known limits

- The 8 hp/blow figure is measured against *Hector's* starter leather in the
  *weak* band. It is not a shard damage formula and must not be reused as one.
- `kSoloRadius = 8` is reasoned from ring spacing, not measured as a join
  time. If novices are observed refusing every board at a graveyard, this is
  the first constant to re-derive.
- The novice-band bounds (600 tenths / 70 hp) are a policy choice, flagged as
  such in `NoviceEngage.h`.
