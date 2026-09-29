# PRACTICE_SKILL is judged by the skill, not by the attempt count — 2026-09-06

Fixes the FAIL in `artifacts/selene_train_false_positive_2026-09-06.md`.
Build 45/45 ctest. Live smoke: `rev.py gates CHARS=Selene,Aurelius MINUTES=5`
(second wave, 19:25-19:32, after all code changes).

## 1. What was wrong

`Runner::DoPracticeSkill` ended a bout with `planner_.Finish(true, ...)` after
`kSelfPracticeBeforeRethink`=6 `ActionUseSkill` calls without ever reading the
skill. Selene logged 10 attempts and 2 completions with Meditation at 20.0 all
session. `tools/grade_life.py` clause 2 then counted the ATTEMPT line
(`practice: using X to raise it`) as verified training, so TRAIN-2 was
inflated for every self-practising character.

## 2. Server evidence — Meditation at full mana cannot gain (verified source)

`server/Source-X/src/game/chars/CCharSkill.cpp`

- `CChar::Skill_Meditation`, `SKTRIG_START` (2674-2679): while
  `Stat_GetVal(STAT_INT) >= Stat_GetMaxAdjusted(STAT_INT)` it prints
  `DEFMSG_MEDITATION_PEACE_1` ("You are at peace.") and returns `-SKTRIG_QTY`.
- `CChar::Skill_Start` (4447 `m_Act_Difficulty = Skill_Stage(SKTRIG_START)`,
  4505-4520): a negative `m_Act_Difficulty` makes it `Skill_Cleanup()` and
  return false, so `Skill_Done` — and with it `Skill_Experience` (3971) — is
  never reached. No tenth can be won.
- The mirror case at `SKTRIG_SUCCESS` (2688-2695) returns 0 ("only give skill
  credit now"), i.e. the gain lands when the pool FILLS, which is why the
  precondition is "mana below max", not "mana low".

Runtime confirmation: mana is 10/10 for Selene throughout
(`run_gates/g_Selene.console.txt:165,531`), and her Meditation never moved in
either the archived or the new run.

## 3. Changes

- `src/life/runner/Train.cpp`
  - `Runner::NotePracticeGain` — per-bout baseline from `obs.SkillTenths`
    (0x3A -> `Client::OnSkills` -> `Observation::skills`), logs
    `train: <skill> <old>-><new> gained by practice` once per tenth and calls
    `NoteProgress`.
  - `Runner::EndPracticeBout` — success only if the bout gained; otherwise
    `goal_failed=PRACTICE_SKILL reason="N attempts at X and the skill never
    moved off V"`, `Cooldown`, memory event `practice_no_gain`,
    `Finish(false, "no skill gain observed")`. Used by BOTH the generic
    self-use branch and the Poisoning branch (the two old `Finish(true)`
    sites).
  - Meditation-at-full-mana refusal in the handler, with the server citation,
    a `practice_blocked` memory event and `kNoPracticeGainCooldownMs`.
  - `wantPracticeSkill < 0` no longer `return true` (a completion with
    progress 0); it fails with a reason.
  - Bout identity is `skill + planner_.Current().startedAtMs`, so a tenth won
    at a trainer between bouts cannot be re-reported as won by practice.
- `include/uo/life.h` — `Observation::manaMax`; `src/life/runner/Core.cpp` —
  populated from `Client::PlayerManaMax()`, and the practice picker skips
  Meditation at full mana so a planned Magery gets the turn (casting is what
  spends mana).
- `src/life/Needs.cpp` — NeedPractice says why it is blocked
  (`Meditation cannot start at full mana ...`), and every BLOCKED practice
  entry now has urgency 0.0 so it sorts below a ready one: needs are
  `stable_sort`ed by urgency and `FindNeed` takes the first NeedPractice, so a
  blocked Poisoning at 0.45 was hiding a ready Magery at 0.32 for a mage
  (`run_gates/g_Aurelius.console.txt:66`, first wave).
- `src/life/runner/RunnerInternal.h` — `kNoPracticeGainCooldownMs` (3 min).
- `src/life/Runner.h` — bout members + two method declarations.
- `tests/life_harness.cpp` — scenario
  `ScenarioPracticeSucceedsOnlyOnASkillGain` (3 arms, 5 checks).
- `tools/grade_life.py` — TRAIN-2 clause 2 replaced by outcome lines only.

## 4. Grader

Before: `train: \S+ ...(bought from a trainer|gained in combat)` +
`practice: (using .* to raise it|casting spell \d+ at myself)` +
`goal_completed=<TRAIN goal> progress=[1-9]`.

After: `train: .+ [\d.]+->[\d.]+ (bought from a trainer|gained in combat|gained
by practice)` + the unchanged `goal_completed=` clause. The
`casting spell \d+ at myself` alternative was dropped for two reasons: it is an
attempt log (emitted immediately before `ActionCastSpell`), and it never
matched the emitted text anyway — Train.cpp logs
`practice: casting <name> (spell N, circle C, needs Magery X) at myself ...`.
`\S+` became `.+` because skill names contain spaces (Mace Fighting).

Re-grade of the archived Selene console with the new grader:

    TRAIN-1  FAIL  skills 120.8->120.8
    TRAIN-2  PASS  verified training events 1    lines: 31785

10 -> 1. All ten attempt-log hits are gone. The single remaining hit is
`goal_completed=PRACTICE_SKILL progress=3` at line 31785 — produced by the OLD
binary, and it is exactly the false completion this fix removes: under the new
code PRACTICE_SKILL can only complete after `EndPracticeBout` has seen a gain.
The honest reading of that archived session is TRAIN-1 FAIL / TRAIN-2 FAIL; the
residual PASS is an artefact of grading a console emitted by the pre-fix binary, not
a remaining hole in clause 2.

## 5. Runtime evidence (second smoke wave)

Selene (`run_gates/g_Selene.console.txt`, 300s):
- `:165,:531` `BLOCKED_NEED PRACTICE_SKILL: Meditation cannot start at full
  mana -- mana has to be spent before resting teaches anything (Meditation
  20.0 -> 50.0 mana=10/10 no_gain_region=0)`
- zero `practice:` attempts, zero `goal_completed=PRACTICE_SKILL`, zero
  `goal_spinning`. `:758` `session_summary ... skills=121.2->121.2` — flat and
  now reported as flat.
- She is an alchemist with Meditation planned and no mana-spending skill, so
  the block is permanent for her build. See DEFECTS.

Aurelius (`run_gates/g_Aurelius.console.txt`, 390s): blocked practice needs now
read `NeedPractice(Poisoning BLOCKED 0.00)` / `NeedPractice(Meditation BLOCKED
0.00)` and no longer shadow `NeedPractice(Magery 0.32)`. Combat unaffected:
`:117` TRAIN_COMBAT picked, `:497` SURVIVE emergency preempt, `:703` HEAL.
`session_summary ... kills=0 deaths=0`.

Death cross-check, `runtime/logs/sphere2026-09-06.log`: the only line for
either character is `494253:17:57:P'Aurelius' was killed by N'skeletal
knight'` — 80 minutes BEFORE the smoke window. No death in either window.

## 6. Deterministic proof of the gain path

`build-m1/tests/life_harness.exe`, scenario "Selene practice succeeds only on
an observed skill gain":

- 6 attempts, skill flat -> `goal_failed=PRACTICE_SKILL reason="6 attempts at
  Meditation and the skill never moved off 20.0"`, handler returns false.
- one tenth added mid-bout -> `train: Meditation 20.0->20.1 gained by
  practice`, handler returns true, one gain reported.
- mana at max -> refusal, never a completion.

ctest 45/45.
