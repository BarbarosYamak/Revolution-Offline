# M1.5 Phase 1 — Session State Audit

Date: 2026-08-25. Scope: every mutable static/global in `bot/uo-client` that two independent client sessions in one process could collide on. Baseline: `revolution-sphere-m1` @ `349ebcb`.

Classification:

- **A** — immutable or shared safely (protocol tables, pure helpers)
- **B** — logging/infrastructure, shared safely (may still need per-client identification)
- **C** — per-client state that MUST become instance-owned (or explicitly single-owner)
- **D** — UNKNOWN

## Headline

The client is already mostly instance-owned: `Client` holds 84 private members covering socket, parser, player, world caches, movement, targeting and containers. The blockers are a small, specific set — the JS binding layer, two process-wide teardowns invoked from `~Client`, the shared logger, a static path buffer, and the fact that `Run()` blocks.

## Findings

| # | State | Location | Class | Risk if two clients share it |
|---|---|---|---|---|
| 1 | `ClientBindings::client` | `src/js/ClientBindings.cpp:892` | **C** | Points at whichever `Client` installed bindings last. Every JS getter/action of both clients then operates on that one client. |
| 2 | `ClientBindings::context` | `:893` | **C** | Same: the surviving context wins; `Emit`/`TickEvents` deliver to the wrong runtime. |
| 3 | `ClientBindings::pending` | `:894` | **C** | Promise list shared between runtimes — a `goto`/`once` settled by client A resolves client B's promise. |
| 4 | `ClientBindings::handlers` | `:895` | **C** | `Player.on(...)` subscriptions from both clients in one list; events fan out to foreign handlers. |
| 5 | `ClientBindings::eventQueue` | `:896` | **C** | Server events from A dispatched into B's runtime. |
| 6 | `Logger` singleton | `src/Logger.cpp:10-11`, `include/uo/log.h:26` | **B** | Safe to share, but one log file with **no client identity** on any line — packet dumps from two sessions interleave indistinguishably. Requirement: tag packets with the client. |
| 7 | `Logger::Instance().Close()` in `~Client` | `src/Client.cpp:144` | **C (bug)** | Destroying client A **closes the log file for every other client**. |
| 8 | `Socket::WSACleanupOnce()` in `~Client` | `src/Client.cpp:145`, `src/net/Socket.cpp:13,28-33` | **C (bug)** | Destroying client A calls `WSACleanup()` process-wide, breaking still-live sockets of client B. |
| 9 | `g_wsa_started` | `src/net/Socket.cpp:13` | **A/B** | The init half is idempotent and safe (`WSAStart` guards on it). Only the *cleanup* half (#8) is wrong. |
| 10 | `MulPath` static ring buffer | `src/main.cpp:48-49` | **C (bug)** | 16 slots, rotating; one client already consumes 15 MUL paths, so two configurations alias and the second client gets corrupted paths. |
| 11 | `mfb_state` (MiniFB window) | `include/win32/MiniFB.h:127`, used `src/client/ClientRender.cpp:232`, `Client.cpp:141` | **C** | One window per process. Two renderer-enabled clients would fight over it; `mfb_close()` from one closes the other's window. Headless (our model) never touches it. |
| 12 | stdin reader thread | `src/Client.h:713-715`, started `src/Client.cpp:758` | **C** | `stdin_lines_` is per-instance, but **stdin itself is a process-wide resource**: two readers race for the same keystrokes. Also starts unconditionally in headless mode. |
| 13 | `Client::Run()` blocking loop | `src/Client.cpp:148-…` | structural | Not shared state, but `Run()` owns the process until disconnect, so a second client cannot be driven from the same thread. |

## Verified NOT a problem (already instance-owned)

Checked explicitly against the categories called out in the milestone brief:

| Category | Owner | Reference |
|---|---|---|
| Network/socket state | `Client::sock_` | `src/Client.h:337` |
| Packet parser state | `Client::stream_` (`PacketStream` has no statics) | `src/Client.h:338`, `src/net/PacketStream.h` |
| Huffman decoder state | `Client::huff_`, `decompress_` | `src/Client.h:339,343` |
| Player/client serial | `Client::playerSerial_` | `src/Client.h:385` |
| Account/character info | `Client::cfg_`, `charSlots_`, `selectedChar_` | `src/Client.h` |
| Movement sequence | `nav_.movement.moveSeq` | `src/navigation/NavigationState.h:30` |
| Pending movement queue | `nav_.movement.pending` | `NavigationState.h:32` |
| Direct step queue (M1) | `Client::directSteps_` | `src/Client.h` |
| World/mobile/item state | `mobileCache_`, `items_`, `corpses_`, `containerItems_` | `src/Client.h:600,480,493,516` |
| Targeting | `targetCursorActive_/Type_/Id_/Subtype_` | `src/Client.h:488-491` |
| Containers | `openContainers_` | `src/Client.h:561` |
| Dialogs | `activeDialog_` | `src/Client.h:386` |
| Keepalive state | `lastActivityMs_`, `pingSeq_`, `pingOutstanding_` | `src/Client.h` |
| Logout state | `loggingOut_`, `logoutSentMs_`, `logoutAcked_` | `src/Client.h` |
| A* planner + worker thread | `Client::pathPlanner_` (`unique_ptr`, own `worker_`) | `src/Client.h`, `src/navigation/PathPlanner.h:96` |
| Blacklist / rejected edges | `nav_.bot.blacklist`, `nav_.bot.rejectedEdges` | `NavigationState.h` |
| MUL loaders | no mutable statics; each `Client` loads its own `World` | `src/mul/*.cpp` |
| JS engine instance | `Client::js_` (own `JSRuntime`/`JSContext`) | `src/Client.h:721` |

Class **A** (shared safely, immutable): `kPacketLength` (`include/uo/packet_lengths.h`), `kSpherePacketLength` + `PacketLengthFor` (`include/uo/packet_lengths_sphere.h`), the Huffman code table (`src/net/Huffman.cpp:11`), all `build::` packet builders (pure, caller-supplied buffers), `bot::Pathfinding` (pure A* over caller-supplied queries).

## Class D — UNKNOWN

1. **Renderer with two clients.** Only one MiniFB window can exist, so a second renderer-enabled client is expected to misbehave — never tested, because the model is headless. Treated as unsupported rather than fixed.
2. **Memory cost per client.** Each `Client` opens its own MUL files and `World` cache when pathfinding is used. Fine for two; the per-client footprint at population scale has not been measured.
3. **quickjs cross-runtime safety.** Each `Client` has its own `JSRuntime`, which quickjs supports, but two runtimes in one process have never been exercised here.

## Consequences for Phase 2

Must fix (correctness): #1-#5 (bindings), #7 (logger close), #8 (WSA cleanup), #10 (path buffer), #13 (blocking `Run`).
Must guard (single-owner, documented): #11 (renderer), #12 (stdin).
Must add: per-client identity on log lines (#6).
