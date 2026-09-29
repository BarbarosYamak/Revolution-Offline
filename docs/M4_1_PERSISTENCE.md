# M4.1: Persistence layer

Date: 2026-09-29. **STATUS: BUILT AND UNIT-TESTED, NOT YET PROVEN LIVE.**
This is step 1 of `M4_LIFECYCLE_PLAN.md` §4. Nothing here has run against the
shard yet. The first live proof is in §6.

A character's life now survives a logout. It records what the character is for
(its targets), what it has learned (its knowledge) and where it had got to (its
objective). Next session it continues that life instead of starting a new one.

---

## 1. What was built

| Piece | Where | What it does |
|---|---|---|
| Character record | `include/uo/lifecycle.h`, `src/lifecycle/CharacterRecord.cpp` | Identity, target build, target STR/DEX/INT, equipment and gold goals, current objective, death history, route memory, last logout point, and a wall-clock snapshot of `PersonalKnowledge` + `supply::Registry` |
| Client seam | `src/lifecycle/ClientLife.cpp` | Loads on `0x55`; saves on logout, on death, when the session ends and every 60 s |
| Supplier feed | `Client::NoteVendorStock` | Every vendor offer now goes into a per-session `supply::Registry`. Before this, the registry was only exercised by tests, and nothing in the client ever filled it |
| Route memory | `Client::TravelFinish` | Every arrival or failure is counted per destination label |
| CLI | `--life-dir <dir>` | Turns persistence on: one `<dir>/<Name>.life` per character. Leave it off and every pre-M4 scenario behaves exactly as before |
| Tests | `tests/m4_lifecycle.cpp` | 108 checks. No server, no MULs |

## 2. What is deliberately NOT saved

Skills, stats, gold and items. **Those are the server's facts.** A bot that
saved its own skill values would be keeping a second set of books. The first
time the two disagreed, the bot's copy would be the wrong one, and the bot
would act on it. The record holds **targets** and **knowledge** only. The
present state is re-read from Sphere after every login. This follows the core
principle: the bot does not manipulate or mirror server state.

## 3. The two-clock problem (the bug this layer exists to avoid)

`Client::NowMs()` reads `steady_clock`. Its epoch is arbitrary and resets
whenever the process restarts. Every timestamp in `PersonalKnowledge` and
`supply::Registry` is on that clock. Copied across a logout unchanged, a
last-session timestamp is **wrong in a way that looks plausible**: a shop list
read eight hours ago comes back as `VerifiedCurrent`, and a danger note that
expired overnight comes back live.

So the record stores **wall-clock** milliseconds. Every crossing goes through
`life::Clock`, which pairs a steady reading with a wall reading taken at the same
instant. `TestClockAcrossLogout` simulates this case: two processes, steady
readings of ~1e9 and ~5e4, eight hours apart. It checks that the vendor is
`Stale` and that its age is preserved to the millisecond. It also checks that a
24-hour danger note expires 24 hours after it was noted, not 24 hours after
login.

## 4. Safety rules built into the format

* **A corrupt life is never overwritten.** `LoadFile` returns one of `Ok`,
  `NotFound`, `Corrupt` or `Invalid`. Only `NotFound` starts a new life. On
  `Corrupt` or `Invalid` the client turns persistence **off** for that session
  and leaves the file untouched. A fresh life would silently erase everything
  the old one had learned.
* **A truncated write is detected.** Every file ends with an `end` line, so a
  save killed mid-write cannot load as a shorter life. Saves go to `<path>.tmp`
  and are then renamed over the old file, so a crash leaves the previous
  version intact.
* **A file cannot grant a purchase.** The vendor policy is code and can change
  between builds. Every `NPC_VENDOR` supplier is re-ruled by the *current*
  policy when it is loaded, so a hand-edited file, or one written under an
  older policy, cannot make a refused vendor usable.
* **Targets are validated at load.** A target build over 700, the same skill
  listed twice, an inactive Revolution skill, a stat over 100 or stats over 225
  in total all load as `Invalid`. None of them are quietly clamped.
* **No adopting someone else's life.** File names fold non-alphanumerics to `_`
  (which also makes `../` harmless), so two names can map to one file. The
  client checks that the name inside the file matches before applying it.
* **The previous session's logout is checked.** It counts as "safe" only if the
  character was alive inside a region the shard flags `GUARDED` or `SAFE`. The
  next login logs a warning when the previous session broke the rule.
* Enums are written **by name**, never by ordinal, so reordering an enum cannot
  re-map saved characters. Unknown record kinds are skipped and counted, so an
  older build can still read a newer file. A newer format *version* is refused
  rather than guessed at.

## 5. The file

Tab-separated text. It is meant to be read by a person diagnosing a character.
Below is a real one written by the code (tabs shown as `|`):

```text
revolution-offline-character|1
identity|Ahmet|revolutionbot01|lumberjack_swordsman|1790000000000|3|0|1790021600000
stats|100|100|25
skill|40|1000
skill|27|1000
skill|44|1000
skill|17|1000
skill|1|1000
equip|i_hatchet|0|1
equip|i_bandage|1|1
economy|100|500|80
objective|earn|i_log|1790018000000|0
logout|1434|1690|0|a_britain|1|1790018000000
last_death|2100|905|5|a_yew_forest|0|1790014400000|0
death|2100|905|5|a_yew_forest|0|0|1790014400000
route|service:banker|1|0|0|1790018000000|
supplier|NPC_VENDOR|43981|1450|1600|20|Tomas the carpenter|i_hatchet|12|23|1790018000000|1790018000000|0|0|BASIC_CRAFT_TOOL|1
visit|a_britain|1790021600000|1
danger|2100|900|10|1790108000000|grizzly den
end
```

`.life` files contain no passwords, but they are runtime output. Keep the life
directory out of version control, in the same way as `save/`.

## 6. The first character's targets, and what is UNKNOWN

| Target | Value | Evidence |
|---|---|---|
| Skills | Swordsmanship, Tactics, Lumberjacking, Healing, Anatomy at 100.0 | `M4_LIFECYCLE_PLAN.md` §1 |
| Remaining 200.0 | **UNALLOCATED** | The plan says "toward a 700-point build" but names only these five skills. It is not filled with a guess |
| STR / DEX / INT | 100 / 100 / 25 | The only warrior split that appears **twice** among the ten attested builds in `REVOLUTION_RULESET_PROFILE.md` §4 (a warlock thread and a thief thread). **No swordsman-specific source has been found.** |
| Tools | `i_hatchet` (required), `i_bandage` (required) | Both are itemdefs in `data/revolution_vendor_policy.tsv` |
| Gold reserve 100, bank above 500, unload at 80% weight | Bot tuning | Not a Revolution mechanic; tune from live runs |

The skill IDs Anatomy 1, Healing 17, Tactics 27 and Swordsmanship 40 were added
to `rules.h`. They follow the same Sphere `SKILL_TYPE` order as the IDs already
there (Swordsmanship 40 matches `builders.h`). They were **not** re-read off
`runtime/scripts/skills/*.scp`, because the Scripts-X tree is not in this
repository. Check them when it is available.

## 7. Repository fix found on the way

The root `.gitignore` pattern `build*/` also matched the source directory
`bot/uo-client/src/builders/`, and `mul/` matched `src/mul/`. **Neither
directory was ever committed**, so a fresh clone cannot configure `uo_client`,
`uo_viewer`, `uo_mul` or the `sphere_regression` test. The patterns now
re-include both directories. **The source files still have to be committed from
the machine that has them.** They cannot be recovered from this repository.

## 8. How it was verified

On Linux, because the full client cannot build from a clone (§7). Each pure
test was compiled directly:

| Test | Result |
|---|---|
| `m4_lifecycle` | 108 checks, 0 failures. Also clean under ASan + UBSan and clang |
| `m2_actions`, `m3_progression`, `m35_authenticity` | OK |
| `m37_economy` | 99 checks, 0 failures |
| `m38_closure` | 284 checks, 0 failures |

`Client.cpp`, `ClientTravel.cpp`, `ClientLife.cpp` and `main.cpp` were
syntax-checked with a MinGW cross-compiler. The only warnings were ones that
already existed in `Client.cpp`. **Nothing was linked or run against Sphere.**

## 9. Next

1. **Live proof of this step.** Run one character with `--life-dir` for two
   sessions and check:
   * the session count goes up;
   * a vendor opened in session 1 shows up as a supplier in session 2;
   * the logout point is recorded as safe after logging out in Britain.
2. **Step 2 of the plan: state assessment.** The `assess state` ladder, reading
   `LifeRecord()` together with live health, hunger, pack weight, gold and
   skills, and choosing a `life::ObjectiveKind`. The objective enum already
   follows that ladder's order.
