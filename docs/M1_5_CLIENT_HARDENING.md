# M1.5 — Headless Client Hardening

Date: 2026-08-26. Result: **M1.5 PASSED**. `bot/uo-client` is now a reusable foundation for many independent simulated players: session state is instance-owned, two real sessions ran in one process without interfering, and every movement packet in the client leaves through a single controller — A* included.

Infrastructure only. No combat, magery, skills, economy, AI, Revolution mechanics or LLM work was added.

## Versions

| Component | Value |
|---|---|
| uo-client before M1.5 | `349ebcb` (branch `revolution-sphere-m1`) |
| uo-client after M1.5 | **`fb105ff`** on the same branch |
| Source-X | `dd4183ddc97b494b4c6c9e5d453b73910dfa02a2` — **unmodified** |
| Scripts-X | `27e78bc896da239d3738fe02a6d6bf8e9045c16d` — unchanged since M0 |
| Toolchain | MSVC 19.51.36256.0 / VS 18 (2026), Ninja, x64 Release |

## Phase 1 — static/global state found

Full table with classifications in **`docs/M1_5_STATE_AUDIT.md`**. Thirteen findings; the client turned out to be mostly instance-owned already (`Client` holds 84 private members covering socket, parser, player, world caches, movement, targeting and containers). Summary:

| Class | Count | Items |
|---|---|---|
| **C** — must become instance-owned / single-owner | 8 | JS binding statics (`client`, `context`, `pending`, `handlers`, `eventQueue`); `Logger::Instance().Close()` in `~Client`; `Socket::WSACleanupOnce()` in `~Client`; the `MulPath` rotating static buffer; the MiniFB window; the stdin reader |
| **B** — shared infra, needed identification | 1 | the `Logger` singleton |
| **A** — safely shared | several | `kPacketLength`, `kSpherePacketLength`, the Huffman table, all `build::` builders, `bot::Pathfinding` |
| **D** — UNKNOWN | 3 | renderer with two sessions; per-session memory at population scale; two quickjs runtimes in one process |
| structural | 1 | `Run()` blocked the thread, so a second session could not be driven |

Two of these were outright multi-session bugs rather than design smells:

- `~Client` called `Logger::Instance().Close()` — destroying one session **closed the log file for every other session**.
- `~Client` called `Socket::WSACleanupOnce()` — destroying one session called `WSACleanup()` **process-wide**, breaking still-live sockets.
- `MulPath` used a 16-slot rotating static buffer while one session consumes 15 MUL paths, so a second configuration **aliased the first**.

## Phase 2 — what moved per instance

| Was | Now |
|---|---|
| One process-wide `Logger`, untagged lines | `Logger` is instantiable and carries a session tag. Each `Client` owns one, opens its own log file, and every console line and packet dump is prefixed `[tag]`. |
| `LogInfo(...)` etc. resolved to global free functions | The same names are declared as `Client` members, so unqualified calls inside any member function bind to **this session's** logger. ~230 call sites moved with no edits — C++ member hiding does the work, and non-member code still uses the process logger. |
| `~Client` closed the shared logger and winsock | `~Client` closes only its own log. Winsock starts and stops once, in `main()`. |
| `Run()` blocked until disconnect | `Start()` / `Tick(waitMs)` / `Finished()` / `ExitCode()`. `Run()` is now just that loop, so single-session use is unchanged. |
| One config, hard-coded paths | `--session user:pass:char[:scenario[:tag]]`, repeatable. Each session owns its config strings and MUL paths (`SessionStrings`); passwords resolve from `UO_BOT_PASS_<TAG>` or the spec. |
| stdin thread always started; renderer implicit | Both are opt-in (`--stdin`, `--render`) and **refused** for multi-session runs — one keyboard and one window per process. |
| JS bindings silently rebound to the newest client | Ownership is explicit: a second `Client` requesting bindings while another owns them is **refused and logged**, instead of redirecting the first client's promises, handlers and events. |

Requirement check: no global player serial, no global movement sequence, no global socket, no global target-cursor state — all were already `Client` members and remain so (`docs/M1_5_STATE_AUDIT.md`, "Verified NOT a problem"). Packet logs identify their session.

## Architecture after the refactor

```
main()
  ├─ winsock init/teardown (process scope)
  └─ vector<Client>            one per --session, ticked round-robin
        │
        Client  ── the Sphere/UO adapter, all session state
        ├─ Logger      log_          own file + [tag] on every line
        ├─ net::Socket sock_         own connection, own peer address
        ├─ PacketStream stream_      own framing buffer
        ├─ Huffman     huff_         own decoder state
        ├─ CharacterState            serial, body, position, facing, equipment
        ├─ WorldState                mobiles, items, corpses, containers, dialogs
        ├─ TargetState               cursor id/type/subtype
        ├─ NavigationState nav_      movement + A* path + follow + blacklist
        ├─ PathPlanner  (own worker thread)
        ├─ JsEngine     js_          own runtime (bindings: one owner per process)
        └─ Scenario     scenario_    scripted player actions
```

The conceptual split the brief asked for is present; it lives inside `Client` and `NavigationState` rather than in separate classes, because that is where the existing architecture already put it. Splitting `Client` into five objects would have been churn without changing the isolation properties.

## Phase 3 — movement architecture

```
A* planner ─┐
scripted    ├─► step queue ─► Client::SubmitStep() ─► 0x02 ─► Sphere
manual keys ┘                        │                          │
                                     │                     0x22 / 0x21
                                     └──── pending queue ◄──────┘
                                            └─► position update / resync
```

`SubmitStep()` is the only place a `0x02` is built and sent. It owns, in order: the outstanding-step limit, the pacing gate, the turn-vs-step decision, the sequence allocation, the send, the pending-queue bookkeeping and the local prediction. Callers get back `Sent` / `Turned` / `Throttled` / `InFlight` / `Failed` and act accordingly.

Verified mechanically after the refactor:

```
build::MoveRequest call sites : 1   (src/Client.cpp, inside SubmitStep)
NextSeq() callers             : 1   (src/Client.cpp, inside SubmitStep)
movement.pending.push_back    : 1   (src/Client.cpp, inside SubmitStep)
```

A* keeps its planning, lookahead patching and door handling; the only packets it still sends itself are `0x12` OpenDoor and `0x98` AllNames, neither of which is movement. Every movement packet carries its origin in the log (`src=astar`, `src=action`, `src=manual`).

Pacing and gait are now a single configured choice for all sources. Previously A* ran unconditionally at 200 ms while scripted walks honoured the config — `nav_.movement.run` is now initialised from `cfg_.runWhenWalking` (default: walk). Walking never reaches Sphere's walk-buffer speedhack check, which only runs for running steps (`CClient::Event_Walk`, `src/game/clients/CClientEvent.cpp:905-935`).

Rejection/resynchronisation is explicit. `OnMoveReject` snaps to the server's authoritative pose and resets the sequence to 0 (the one value Sphere validates, `src/network/receive.cpp:270-273`). A reject that does not belong to an A* path now goes to `OnStepRejected()`, which retries the queued steps from sequence 0 and abandons the batch after three consecutive rejects instead of looping forever.

## Phase 4 — tests added

`tests/sphere_regression.cpp`, wired into CTest via `tests/CMakeLists.txt`; runs with no server, no MUL files and no network.

The rules under test live in **`include/uo/sphere_rules.h`** and are called by the client itself, so a test cannot drift from shipping behaviour. Each rule cites the Source-X source that justifies it.

| Bug from M1 | Rule | Cases |
|---|---|---|
| 0x8C same-socket relay | `StayOnLoginSocket` | same endpoint stays; advertised ip 0 stays; different ip/port reconnects; override forces reconnect; explicit guard against the inverted M1 answer |
| 0x73 ping storm | `DecidePing` | reply to our keepalive is consumed, never echoed; 50 simulated round-trips produce **zero** echoes |
| unsolicited ping handling | `DecidePing` | echoed once, then rate-limited |
| unknown opcode | `PacketLengthFor` + `PacketStream` | 0xCD has no length; error reported; buffer left intact for diagnostics |
| 0xD1 logout framing | overlay + `PacketStream` | absent from the 2.0.7 table, framed as 2 bytes by the overlay; overlay never shadows a known opcode |
| framing generally | `PacketStream` | split arrival waits; fixed and variable lengths both frame; stream drains |
| sequence wrap | `NextMoveSequence` | 255 → 1 matching Sphere, never → 0; 600 iterations never emit 0 |
| password logging | `CredentialPasswordOffset` | offsets 31/35 verified against real built packets; after redaction the password is absent, the account name survives, length unchanged |
| character slot | `SelectCharacterSlot` | by slot; empty/out-of-range refused; name beats slot; case-insensitive; unknown name refused (no silent fallback); two names → two distinct slots |

```
$ ctest --output-on-failure
    Start 1: sphere_regression
1/1 Test #1: sphere_regression ................   Passed    0.01 sec
100% tests passed, 0 tests failed out of 1

50 checks, 0 failure(s)
```

These are unit tests over framing and decision logic. They do not claim a working login — runtime acceptance below still requires a live Sphere.

## Phase 5 — two real clients, one process

Command (one process, two `--session` specs, passwords from the gitignored env file):

```
uo_client --host 127.0.0.1 --port 2593 --create-char --log ... --log-packets --headless
  --session revolutionbot01::RevolutionBot01:scripts\scenarios\m15_two_bots_a.txt
  --session revolutionbot02::RevolutionBot02:scripts\scenarios\m15_two_bots_b.txt
```

| Acceptance | Result | Evidence |
|---|---|---|
| both connect | **PASS** | `[revolutionbot01] [net] connected.` and `[revolutionbot02] [net] connected.` at 23:54:10 |
| both login independently | **PASS** | Sphere: two `Login for account 'revolutionbot01' / 'revolutionbot02'` lines; distinct 0x8C seeds `0x93724DA7` vs `0x0A7B1C1D` |
| both select their own character | **PASS** | bot01 `[ui] playing slot 0 ('RevolutionBot01')`; bot02 had none and created one: Sphere `Account 'revolutionbot02' created new char 'RevolutionBot02' [01fef]` |
| both enter world | **PASS** | two `Character startup for account ...` lines |
| different player serials | **PASS** | `0x00000001` vs `0x00001FEF` |
| independent positions | **PASS** | bot01 `(633,858)→(637,858)→(633,858)`; bot02 `(633,858)→(633,861)→(633,858)` |
| independent movement sequences | **PASS** | both start at seq 0 and advance separately (bot01 0-9, bot02 0-7) in their own logs |
| A walks without affecting B | **PASS** | bot01's 10 moves are absent from bot02's log; bot02's position unchanged during them |
| B walks without affecting A | **PASS** | mirror of the above |
| both say distinct messages | **PASS** | Sphere: `'RevolutionBot01' Says 'bot01 online'`, `'RevolutionBot02' Says 'bot02 online'`, plus both `walk complete` lines |
| Sphere receives both correctly | **PASS** | bot02's client even logged `System: RevolutionBot01 has arrived in Yew.` — each bot observes the other as a real player |
| ≥ 5 minutes connected | **PASS** | 23:54:10.098 → 23:59:47.303 = **5 min 37 s** (both) |
| keepalives independent | **PASS** | 16 sent / 16 answered per session, on each session's own 20 s timer |
| no packet cross-talk | **PASS** | per-session logs: 0 lines tagged with the other session's id, in either direction |
| no rejects from client architecture | **PASS** | `0x21` count: 0 and 0 |
| both cleanly logout | **PASS** | both `0xD1 → logout acknowledged (accepted=1) → closing connection`; Sphere counted down `[Total:1]` then `[Total:0]` |

## Phase 6 — A* navigation smoke test

Single session, `--mul-dir runtime/mul`, scenario `scripts/scenarios/m15_nav.txt`: `goto 658 858` (25 tiles east of the Yew spawn), then `goto 633 858` back.

| Acceptance | Result | Evidence |
|---|---|---|
| A* does not write to the socket | **PASS** | all 66 movement packets logged `src=astar`, i.e. issued by `SubmitStep`; the only direct sends left in the navigation code are `0x12` OpenDoor and `0x98` AllNames |
| no movement rejection | **PASS** | 66 × `0x02` out, 66 × `0x22` ack, **0** × `0x21` |
| final position matches destination | **PASS** | `goto finished at (658,858,0); target (658,858); ARRIVED (off by 0 tile(s))` and the same for the return leg to `(633,858)` |
| sequence remains correct | **PASS** | monotonic 0,1,2,… across both legs, first move of the trip at seq 0 |
| no speedhack warning | **PASS** | `runtime/logs/sphere2026-08-26.log` has no `LOGM_CHEAT`, fastwalk, speedhack or quota line for either run |

Planning cost: 25 steps in 5.1 ms (first leg), 1.5 ms (second). Pacing at the default walk gait: **minimum step gap 413 ms**, no gap below 400 ms, and no run bit on the wire.

An earlier run of the same scenario before the gait fix went at the running cadence (min gap 200 ms) and also completed with 66/66 acks and zero rejects — recorded here because it is evidence that Sphere's walk buffer tolerated the canonical run rate, not because running is now the default.

## Exact files changed

New:

| File | Purpose |
|---|---|
| `include/uo/sphere_rules.h` | pure, cited Sphere decision rules shared by the client and the tests |
| `tests/sphere_regression.cpp` | 50 regression checks for the M1 bugs |
| `tests/CMakeLists.txt` | CTest wiring |
| `scripts/scenarios/m15_two_bots_a.txt`, `m15_two_bots_b.txt` | two-session acceptance scenarios |
| `scripts/scenarios/m15_nav.txt` | A* navigation scenario |

Changed:

| File | Change |
|---|---|
| `include/uo/log.h`, `src/Logger.cpp` | `Logger` instantiable; session tag on every line and packet record |
| `src/Client.h`, `src/Client.cpp` | per-session log members; `Start`/`Tick`/`Finished`/`ExitCode`; destructor fix; `SubmitStep`; `ActionGoto`/`GotoBusy`; stdin opt-in; rules wired in |
| `src/main.cpp` | multi-session host, per-session strings, process-scope winsock, `--session`/`--tag`/`--stdin` |
| `src/navigation/Navigation.cpp` | A* offers steps to `SubmitStep`; `OnStepRejected`; ack pumps both movement sources |
| `src/navigation/NavigationState.h` | `MovementState` gains pacing, `maxInFlight`, `rejectStreak` |
| `src/client/ClientRender.cpp` | manual arrow keys go through `SubmitStep` |
| `src/js/ClientBindings.{h,cpp}` | binding ownership enforced, `InstallClientBindings` returns success |
| `src/bot/Scenario.{h,cpp}` | `goto` / `wait_goto` commands |

## Technical debt

1. **JS bindings are still process-singleton state.** Ownership is now enforced rather than silently shared, so there is no corruption — but only one session per process can run scripts. The fix is to thread the owner through the `Emit*` API (14 call sites in `Client.cpp` plus ~25 signatures) and hang the binding state off the `JSContext`. Deferred because the scenario runner needs no JS and the subsystem has no test coverage.
2. **One thread for all sessions.** The round-robin host splits a 50 ms budget across sessions, so per-session poll latency grows with the count. Fine for two; a population will want either a shared `select()` over all sockets or a thread pool.
3. **Each session loads its own MUL/World cache.** Two sessions mean two copies of the map data. Sharing the read-only loaders across sessions is the obvious next step before scaling.
4. **Renderer and stdin remain single-owner.** Correct as guards, but a supervised multi-bot UI will need a different design.
5. **`lastDirectStepMs_` is now redundant** with `nav_.movement.lastMoveSentMs`; harmless but duplicated.
6. **`scripts/build.bat` still targets absent VS 2022 Build Tools**; M1.5 builds out-of-tree as documented in `M1_BASELINE.md`.
7. **The A\* pipeline depth is still 1.** `maxInFlight` is configurable and the controller supports more, but nothing has measured whether Sphere tolerates a deeper pipeline; M1 and M1.5 both ran strict request/ack.
8. **The old `tests/*_probe.cpp` files** remain manual tools outside the CTest build.

## Unresolved UNKNOWNs

1. **Two renderer sessions** — untested and refused by the CLI; assumed broken because MiniFB keeps one global window.
2. **Per-session memory at scale** — two sessions are fine; the footprint of dozens (each with a `World` cache and a planner thread) has not been measured.
3. **Two quickjs runtimes in one process** — supported by quickjs in principle, never exercised here (item 1 in the debt list blocks it anyway).
4. **Sphere's walk-buffer tolerance for running bots** — one 66-step run at 200 ms passed cleanly; that is a single sample, not a characterisation.
5. **Character skills after creation** — still never read back (`0x3A` not requested), carried over from M1.
6. **Deeper movement pipelines** — see debt item 7.

## Scope discipline

No combat, magery, skill training, economy, crafting, AI, personalities, LLM integration, Revolution mechanics or advanced navigation was implemented. Source-X remains unmodified; the M0 `sphere.ini` configuration is unchanged. Credentials for both bot accounts live only in `local/dev/bot-credentials.env`, which is gitignored, and passwords are still masked in packet logs.
