# M1 — First Real Headless Player Connection to Sphere

Date: 2026-08-25. Result: **M1 PASSED**. `bot/uo-client` connects to the local Sphere Source-X shard as an ordinary UO network client, creates and plays a character, walks, speaks, opens its backpack, stays connected past ten minutes and logs out cleanly. No server-side bot object exists; no world state is touched directly.

## Acceptance summary

| Requirement | Result | Evidence |
|---|---|---|
| TCP connect | **PASS** | `[net] connected.` 23:17:48.711; Sphere: `23:17:2:Client connected [Total:1]. IP='127.0.0.1'` |
| Login | **PASS** | `0x80` accepted, `0xA8` server list (`MyShard`); Sphere: `Login for account 'revolutionbot01'. ConnectionType: ServerList` |
| Character select | **PASS** | `[0xA9] 5 slot(s) (populated 1): [0] RevolutionBot01` → `[ui] playing slot 0 ('RevolutionBot01')` → `0x5D` |
| Character create | **PASS** | first run: `[0xA9] populated 0` → `[0x00] creating character 'RevolutionBot01' in slot 0`; Sphere: `Account 'revolutionbot01' created new char 'RevolutionBot01' [01]` |
| World entry | **PASS** | `[0x1B] serial=0x00000001 body=0x0190 pos=(633,858,0)` → `[0x55] login complete`; Sphere: `Character startup for account 'revolutionbot01', char 'RevolutionBot01'` |
| Player serial received | **PASS** | `0x1B` → serial `0x00000001` |
| Initial position received | **PASS** | `0x1B` (633,858,0), confirmed by `0x20` draw-player |
| Backpack / container state | **PASS** | `[0x24] open container 0x40001FFE gump=60`, `[0x3C] container contents: 8 item(s)`, event `backpack_contents: serial=0x40001FFE items=8` |
| 10-tile movement | **PASS** | `walk done: (633,858) -> (638,858,0), 5 tile(s)` and `walk done: (638,858) -> (633,858,0), 5 tile(s)` — 10 tiles |
| Movement acks handled | **PASS** | 12 × `0x02` out, 12 × `0x22` in, **0 × `0x21`** (12 = 10 steps + 2 turns) |
| Speech "selam" | **PASS** | `[action] say: selam`; Sphere: `'RevolutionBot01' Says 'selam' mode=0` (×2) |
| No speedhack / WalkBuffer violation | **PASS** | zero rejects; min step gap 427 ms (≥ 400 ms walk cadence); no `LOGM_CHEAT` line in the Sphere log |
| 10-minute stability | **PASS** | session 23:17:48.705 → 23:27:57.982 (log file first/last line) = **10 min 09 s**, 30 keepalives sent / 30 answered, no reconnect |
| Clean logout | **PASS** | `0xD1` out → `[0xD1] logout acknowledged (accepted=1)` → `[net] closing connection (logout acknowledged)`; process exit code 0 |
| Sphere reports clean disconnect | **PASS** | `23:27:2:Client disconnected [Total:0]. Account: 'revolutionbot01'. Char: 'RevolutionBot01'. IP='127.0.0.1'` — no warning, no quota kick, no timeout |

Every row above is runtime evidence from a real session, not code inspection.

## Versions

| Component | Value |
|---|---|
| uo-client original commit | `2fb08c537ef379f4a4183aef65c1efadd4d28ebd` (branch `master`, 2026-06-01) |
| uo-client branch | `revolution-sphere-m1` |
| uo-client commit after changes | `b584f3d11e54d34b25a9921dca6a63ae49601fc3` (+ `349ebcb` ignoring the build dir) |
| Source-X | `dd4183ddc97b494b4c6c9e5d453b73910dfa02a2` — **unmodified in M1** |
| Scripts-X | `27e78bc896da239d3738fe02a6d6bf8e9045c16d` (only the M0 `spheretables.scp` map edit) |
| Toolchain | MSVC 19.51.36256.0 / VS 18 (2026), Ninja, x64 Release |

## Files modified

New:

| File | Purpose |
|---|---|
| `include/uo/packet_lengths_sphere.h` | Sphere length overlay + `PacketLengthFor()`; the 2.0.7 table stays a read-only RE artifact |
| `src/bot/Scenario.h` / `.cpp` | scripted player-action runner (the adapter boundary's consumer) |
| `scripts/scenarios/m1_smoke.txt` | the M1 acceptance scenario |

Changed:

| File | Change |
|---|---|
| `src/Client.h` | Sphere config fields; public player-action API; adapter state |
| `src/Client.cpp` | 0x8C relay rewrite, `SendGameLogin`, character select/create, `PumpDirectSteps`, `PumpKeepalive`, `OnLogoutAck`, `ReportUnframeableStream`, `LogPacketRedacted`, 0x73 fix, backpack-contents flag |
| `src/main.cpp` | full CLI + environment configuration; no hard-coded host/credentials/paths |
| `src/builders/Builders.cpp`, `include/uo/builders.h` | `PingRequest`, `LogoutRequest`, `CreateCharacter` (+ `CreateCharacterParams`) |
| `src/net/PacketStream.cpp` / `.h` | use the Sphere overlay; expose `PendingData()` for diagnostics |
| `src/net/Socket.cpp` / `.h` | record peer IP/port (`PeerIp()`, `PeerPort()`) |
| `CMakeLists.txt`, `.gitignore` | build `Scenario.cpp`; ignore `build-m1/` |

`git diff --stat` against the baseline: 14 files, +758 / −139.

## Protocol incompatibilities found, and the fixes

### 1. `0x8C` relay: the seed must not be re-sent on the same socket

Source-X advertises its own `ServIP`/`ServPort` in `0x8C`, which for a single-server shard is the endpoint the client is already connected to. `PacketServerRelay::onSent` (`src/network/send.cpp:2823-2830`) calls `m_Crypt.InitFast(customerId, CONNECT_GAME)` "in case the client decides not to establish a new connection" — the socket is switched to the game protocol server-side and a bare `0x91` is expected next. Re-sending the 4-byte seed there would make Source-X read `0xAC…` as an opcode, find no handler and discard the whole buffer (`src/network/CNetworkInput.cpp:399-406`), stalling the login until `DeadSocketTime`.

The old decision (`stay_on_socket = gamePort != loginPort`, `Client.cpp:572-575` at baseline) was inverted for this case and only worked by accident. It now compares the advertised endpoint with the socket's actual peer:

```
[0x8C] game server = 127.0.0.1:2593  seed=0x93724DA7
[0x8C] staying on the login socket (advertised 127.0.0.1:2593 == peer 0x7F000001:2593)
event relay_same_socket: no seed re-sent; 0x91 follows
```

A different endpoint takes the real path: close, reconnect, seed, `0x91`.

**Nocrypt on the relayed socket works** because the plaintext `0x91` goes through `CCrypto::RelayGameCryptStart` (`src/common/crypto/CCrypto.cpp:318-405`), and the no-crypt key is key index 0 with client version 0 (`CCryptoKeysHolder::addNoCryptKey`, `:58-64`), so the `GetClientVerNumber() < 2000400` branch runs and the packet passes through untouched.

### 2. `0x73` echo caused a ping storm and got the client kicked

**Found at runtime, not by inspection.** Source-X answers a client `0x73` with its own `0x73` (`PacketPingReq::onReceive` → `PacketPingAck`, `src/network/receive.cpp:1335-1344`) and **never pings first** — `PacketPingAck` is constructed nowhere else in the tree. The baseline handler echoed every inbound `0x73`, so each keepalive started an unbounded ping-pong:

```
23:14:53.623 PKT out 0x73 ; 0x73 Ping (keepalive)
23:14:53.638 PKT out 0x73 ; 0x73 PingReply
23:14:53.638 PKT out 0x73 ; 0x73 PingReply
…
```

≈24,000 exchanges in 100 seconds. Sphere's own protection ended it:

```
23:17:A'revolutionbot01' was DISCONNECTed by 'MyShard'.
23:17:WARNING:NetState id 1 (IP: 127.0.0.1, Account: revolutionbot01) exceeded its input quota (10362/ 10000).
```

That is `MaxSizeClientIn=10000` bytes per 10 s (`runtime/sphere.ini:1137`), enforced in `src/network/CNetworkThread.cpp:153-185`.

Fix: an inbound `0x73` is consumed as the answer when a keepalive is outstanding; an unsolicited ping (the UO Demo behaviour this client was written for) is still echoed, rate limited to once per second. After the fix the same 10-minute window produced **30 pings and 30 answers**, 396 bytes outbound in total.

### 3. Unknown opcodes killed the process

An opcode with no length entry cannot be framed, and neither can anything after it — the stream is genuinely unrecoverable, so the session must end. But the baseline just returned an error out of the pump. It now reports precisely and ends only that session:

```
[stream] unknown opcode (no length entry): opcode 0xNN, N byte(s) buffered
[stream] head: <hex>
[stream] framing is unrecoverable -- ending this session. Add a length for 0xNN to
         include/uo/packet_lengths_sphere.h if the server is expected to send it.
```

`include/uo/packet_lengths_sphere.h` supplies lengths Source-X may send that the 1997 table lacks, leaving `packet_lengths.h` untouched as the reverse-engineering artifact it is. The first entry, `0xD1 = 2` (`PacketLogoutAck`, `src/network/send.cpp:4631`), was needed immediately: without it the logout acknowledgement would itself have been an unframeable opcode.

**No unknown opcode occurred in any run.** All 21 distinct inbound opcodes framed correctly: `0x11 0x1B 0x1C 0x20 0x22 0x24 0x3C 0x4F 0x55 0x5B 0x72 0x73 0x78 0x8C 0xA8 0xA9 0xAE 0xB9 0xBC 0xBD 0xBF 0xD1`.

### 4. Movement sequence — the audit item that was *not* a bug

`ARCHITECTURE_AUDIT.md` §4 item 8 claimed the client's `255 → 1` sequence wrap was wrong. Re-reading the server says otherwise, so **nothing was changed**:

- `PacketMovementReq::onReceive` (`src/network/receive.cpp:259-282`) validates the sequence only in one case: `if (net->m_sequence == 0 && sequence != 0)` → reject. Otherwise it never compares.
- On accepting sequence 255 it does `if (sequence == UINT8_MAX) sequence = 0; net->m_sequence = ++sequence;` — i.e. Sphere's own next expected value after 255 is **1**, exactly what `Client::NextSeq` produces (`src/navigation/Navigation.cpp:327-331`).
- `Event_Walk` never inspects the sequence beyond echoing it in the ack/reject.

What does matter is that the first move after login or after any reject carries sequence 0; `BotResetMovement` already guarantees that (`Navigation.cpp:777-780`).

### 5. Movement pacing versus Sphere's speedhack checks

Source-X has two mutually exclusive checks in `CClient::Event_Walk` (`src/game/clients/CClientEvent.cpp:905-940`):

- `EF_FastWalkPrevention` — strict per-step timing, but it is **off** (`Experimental=0`, `runtime/sphere.ini:847`).
- otherwise the walk buffer — and that branch requires `STATF_FLY`, which the server sets only when the direction byte carries the run bit.

So **walking is never subject to either check**. M1 walks by default (`--run` opts in), one step in flight at a time, at the canonical 400 ms cadence. Measured gaps: 427, 428, 443, 439, 441, 619, 441, 442, 443, 441, 444 ms. Zero rejects across every run. The pipelined 4-deep A* path (`kMaxInFlight`, `Navigation.cpp:32`) was left untouched — it is not used by the M1 action queue.

### 6. Character slot, and creating a character at all

Slot 0 was hard-coded (`Client.cpp:680` at baseline). It is now `--char-slot`, or `--char-name` to pick the slot by name. Sphere has no console verb that creates a character, so when the account has none the client sends the ordinary `0x00` packet — the same thing a human client does. Details, offsets and the server-side clamping are in `M1_TEST_ACCOUNT.md`.

### 7. Client version handling

Unchanged and correct: Source-X requests `0xBD` during `Login_ServerList` (`src/game/clients/CClientLog.cpp:900-908`) and the client answers with `cfg_.version` (`"2.0.7"`) before `0x91` is sent. That keeps every `MINCLIVER_*` gate false, so Sphere emits the legacy layouts the 2.0.7 length table expects. Confirmed on the wire: `0xB9` arrived as **3 bytes** (`in 0xB9 len=3`, features `0x0005`) — the 4-byte form would mean the version had not registered. `0x24` came as 7 bytes and `0x3C` with 19-byte records, both the pre-grid layouts.

## Login sequence observed

```
TCP connect 127.0.0.1:2593
-> seed 7f000001                      (4 raw bytes)
-> 0x80 LoginRequest (62)             [password redacted in log]
<- 0xBD  version request
-> 0xBD  "2.0.7"
<- 0xA8  server list: [0] MyShard
-> 0xA0  select server 0
<- 0x8C  127.0.0.1:2593 seed=0x93724DA7      (same endpoint -> stay on socket)
-> 0x91  GameLogin (65, authkey echoed)      [Huffman starts here, server->client]
<- 0xB9  features 0x0005 (3 bytes)
<- 0xA9  char list: [0] RevolutionBot01
-> 0x5D  PlayCharacter slot 0
<- 0x1B  serial 0x00000001 body 0x0190 (633,858,0)
<- 0xBF, 0x4F, 0x78, 0x20, 0x11, 0x72, 0x55, 0x5B, 0xBC, 0x1C…
   in world
```

## Packet observability

- Opt-in with `--log-packets`; off by default so a normal run stays quiet.
- One line per packet: timestamp, direction, opcode, length, full hex, and a note — `[23:17:48.714] PKT out 0x80 len=62 8072…5d ; 0x80 LoginRequest [password redacted]`.
- **Passwords are never logged.** `Client::LogPacketRedacted` masks the 30-byte password field of `0x80` and `0x91` with `0xEE` before the dump. Verified in the captured log: both packets show the `ee…` run where the password would be.
- Unknown opcodes are logged with a hex dump of the buffer head instead of dying silently.
- Named events (`relay_same_socket`, `char_create_sent`, `backpack_contents`, `walk_batch_done`, `logout_ack`, `stream_unframeable`, …) mark the milestones a diagnosis needs.

## Adapter boundary

```
Scenario (scripts/scenarios/*.txt)      <- fixed list of actions; no bot brain yet
        |  ActionWalk / ActionSay / ActionOpenBackpack / ActionLogout
Client  (the Sphere/UO adapter)         <- packets, framing, sequences, Huffman, sockets
        |  UO protocol
Sphere Source-X                         <- authoritative
```

`bot::Scenario` holds a `Client&` and calls only the public action API; it cannot see a socket, a packet or a sequence number. The eventual bot brain replaces `Scenario` at the same seam. Only the actions the smoke test needs exist — `use_item`, `target`, `cast`, `use_skill`, `attack`, `equip`, `pickup`, `drop` are deliberately absent until a milestone needs them (the underlying packet builders already exist in `Builders.cpp`).

## Configuration used

```
uo_client.exe --host 127.0.0.1 --port 2593 --char-name RevolutionBot01 --create-char
              --scenario scripts\scenarios\m1_smoke.txt
              --log <path> --log-packets --headless
```

with `UO_BOT_USER` / `UO_BOT_PASS` from `local/dev/bot-credentials.env` (gitignored). Server settings are the M0 ones, unchanged: `UseNoCrypt=1`, `AccApp=2`, `ServPort=2593`.

## Sphere console evidence (final run)

```
23:17:2:Client connected [Total:1]. IP='127.0.0.1'. (Connecting/Connected: 1/1).
23:17:2:Login for account 'revolutionbot01'. IP='127.0.0.1'. ConnectionType: ServerList.
23:17:2:Character startup for account 'revolutionbot01', char 'RevolutionBot01'. IP='127.0.0.1'.
23:17:2:'RevolutionBot01' Says 'selam' mode=0
23:17:2:'RevolutionBot01' Says 'selam' mode=0
23:27:2:Client disconnected [Total:0]. Account: 'revolutionbot01'. Char: 'RevolutionBot01'. IP='127.0.0.1'.
```

No `LOGM_CHEAT`, no fastwalk warning, no quota warning, no timeout.

## Bot console evidence (final run, abridged)

```
[23:17:48.711] [net] connected.
[23:17:48.740] [0x8C] staying on the login socket (advertised 127.0.0.1:2593 == peer 0x7F000001:2593)
[23:17:48.741] [0xA9] 5 slot(s) (populated 1):  [0] RevolutionBot01
[23:17:48.741] [ui] playing slot 0 ('RevolutionBot01')
[23:17:48.756] [0x1B] serial=0x00000001 body=0x0190 pos=(633,858,0)
[23:17:48.757] [0x55] login complete — entering world
[23:17:48.758] [0x3C] container contents: 8 item(s)
[23:17:51.799] [action] say: selam
[23:17:52.848] [action] walk dir=2 x5 (walk)
[23:17:55.653] [action] walk dir=6 x5 (walk)
[23:17:57.937] [scenario] line 33: hold
[23:27:57.981] [action] logout requested
[23:27:57.981] [0xD1] logout acknowledged (accepted=1)
[23:27:57.982] [net] closing connection (logout acknowledged)
```

Traffic over the whole session: 52 packets out, 84 in, 396 bytes outbound.

## Unresolved UNKNOWNs

1. **Revolution client protocol version.** The bot reports `2.0.7`; what `Revolution.exe` itself reports (and which `sphereCrypt.ini` key it uses) is still unknown, and the human client has never been run. If it turns out to be a different era, Sphere will send *that* client different layouts — it does not affect the bot, which negotiates its own version.
2. **Opcodes never exercised.** Only the 21 opcodes above have been seen. Combat, magery, vendors, gumps, targeting and multi-mobile traffic will surface more, and any without a length entry will end a session with the diagnostic in §3 until added to the overlay.
3. **Grid-index layouts.** `0x3C`/`0x25`/`0x08` are exchanged in their pre-6.0.1.7 form because our reported version is old. Correct for us, but it means the client cannot talk to a shard configured for modern clients without work.
4. **Turn-then-step cost.** Sphere acks a direction change without moving, so a 5-step batch costs 6 packets. Harmless, but it means "tiles walked" and "0x02 sent" are not the same number.
5. **Character creation parameters.** Skill ids 31/33/17 were sent as Swordsmanship/Tactics/Healing on the assumption that Sphere's `SKILL_TYPE` ordering matches the classic table. The server accepted them and clamps everything anyway, but the resulting character's actual skills were never read back (`0x3A` was not requested).
6. **Start location.** `startLoc = 0` produced (633,858) on map 0 — which starting city that is has not been checked against Scripts-X's `map0_starts.scp`.

## Remaining technical debt

- **One bot per process.** `ClientBindings` keeps `static Client*` (`src/js/ClientBindings.cpp:14-19`) and `Logger` is a process singleton (`src/Logger.cpp:10-11`), so multiple bots need separate processes until that state is per-instance. Not needed for M1; unavoidable for a populated world.
- **Stdin thread still starts in headless mode.** `StartStdinThread()` runs on `0x55` regardless (`Client.cpp:758`) and cannot be interrupted (`:2655-2662`); harmless with a redirected stdin, but it should be conditional.
- **`ClientRender.cpp` still owns a second, manual movement path** that sends `0x02` directly. It is renderer-only and unused headless, but there are now two places that build move packets.
- **A\* path and the direct-step queue are separate.** `BotPumpMoves` (4-deep, run cadence, MUL-dependent) is untouched and not reconciled with `PumpDirectSteps`. When navigation returns, one of them should absorb the other, and the A* path needs the same walk-vs-run reasoning applied.
- **`scripts/build.bat` still targets absent VS 2022 Build Tools** — M1 builds out-of-tree instead (`M1_BASELINE.md`).
- **The 2.0.7 length table has no entries above 0xCC.** The overlay currently holds exactly one. Expect to extend it as more of the protocol is exercised.
- **Password length trap.** Sphere silently truncates stored passwords to 16 characters; anything longer can never log in again. Worth a check in whatever provisions accounts later (`M1_TEST_ACCOUNT.md`).

## Scope discipline

Not implemented, by instruction: combat, skill training, economy, crafting, LLM integration, personalities, Revolution mechanics, map-wide navigation, taming, PvP. No server-side bot object, no direct manipulation of skills, stats, inventory, position, gold or world state — every action in this milestone is a packet a normal client sends, and Source-X was not modified.
