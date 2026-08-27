# Architecture Audit — Sphere Source-X ↔ uo-client ↔ uo-offline

Date: 2026-08-25. Read-only audit. No source modified.

Roots (paths below relative to these):

| Alias | Path |
|---|---|
| `SX/` | `server/Source-X/src/` |
| `UC/` | `bot/uo-client/` |
| `UO/` | `reference/uo-offline/` |

Scope per `CLAUDE.md`: prove **Sphere → headless client → real player connection** first. No gameplay AI, no Revolution rules, no Sphere combat changes, no ModernUO porting.

---

## 1. How Source-X accepts and authenticates a normal UO client

### 1.1 Accept

- Listen socket: `CServer::SocketsInit(CSocket&)` `SX/game/CServer.cpp:2525-2565` — binds `ServIP`/`ServPort` (`SX/sphere.ini:21,24`, default `127.0.0.1:2593`), non-blocking, `Listen()`.
- Accept loop: `CNetworkManager::acceptNewConnection()` `SX/network/CNetworkManager.cpp:111-270`. Per-IP gates (`ClientMaxIP`, blocked IPs, connect-rate) `:137-175`. Connection dropped if data already pending immediately after accept `:261-266`.
- Threads: `NetworkThreads=0` (`SX/sphere.ini:1153`) ⇒ single `CNetworkThread` (`CNetworkManager.cpp:367,387`). Input path: `CNetworkInput::processInput/receiveData/processData` `SX/network/CNetworkInput.cpp:39,69,136`.

### 1.2 Per-packet dispatch

`CNetworkInput::processData(state, buffer)` `CNetworkInput.cpp:279-296`:

```
CONNECT_UNK            -> processUnknownClientData   (seed handshake)
crypt not initialised  -> processOtherClientData     (telnet/axis/http)
else                   -> processGameClientData      (decrypt + handler table)
```

`processGameClientData` `:298-424`: decrypt via `CCrypto::Decrypt`, look up `PacketManager::getHandler(id)` `:341`, fixed length from handler ctor (`Packet(uint size)` `SX/network/packet.cpp:86-88`; 0 = length-prefixed variable). **Unknown packet id ⇒ warning + entire remaining buffer discarded** (`:399-406`), not a disconnect. >10 handler exceptions ⇒ kick (`:414-424`).

Handler registration table: `SX/network/CPacketManager.cpp:35-105` (+ `0xBF` sub-handlers `:110-129`). Opcode enum: `SX/common/sphereproto.h:15-195`.

### 1.3 Seed handshake — `CNetworkInput::processUnknownClientData` `CNetworkInput.cpp:524-673`

| Input | Result |
|---|---|
| >127 bytes before seed | disconnect `:542-547` |
| `GET /` / `POST /` | HTTP if `UseHTTP=2` `:552-563` |
| lone `0xEF` | wait for 21-byte new seed `:566-571` |
| 21-byte `0xEF` seed (`NETWORK_SEEDLEN_NEW`) | seed + 4 dwords version → `m_reportedVersionNumber` `:590-618` |
| `0xF1` UOG 8 bytes | UOG status `:620-626` |
| **anything else** | **"old client login handshake": raw 4-byte seed** `:627-633` |
| seed == 0 | disconnect `:636-640` |
| seed == `0xFFFFFFFF` alone | KR handshake `:645-655` |

After seeding, the next ≥5 bytes go to `client->SetConnectType(CONNECT_CRYPT)` + `CClient::xProcessClientSetup(...)` `:666-670`.

### 1.4 Login-packet detection — `CClient::xProcessClientSetup` `SX/game/clients/CClientLog.cpp:839-1000`

- `m_Crypt.Init(seed, raw, len, isKR)` `:852` → `CCrypto::Init` `SX/common/crypto/CCrypto.cpp:246-279`: **len 62 ⇒ `LoginCryptStart` (0x80 path); len 65 ⇒ `GameCryptStart` (0x91 path); else `BadEncLength`** (`CClientLog.cpp:855-860`).
- `xCanEncLogin()` `:1002-1024` gates by `UseCrypt`/`UseNoCrypt`; version check applied only to encrypted clients ("if unencrypted we check that later" `:1020`).
- `0x80 XCMD_ServersReq` → `CClient::Login_ServerList(acct, pass)` `:895`; if version unknown, sends `0xBD PacketClientVersionReq` `:900-908`.
- `0x91 XCMD_CharListReq` → `CClient::Setup_ListReq(acct, pass, true)` `:922`; verifies AuthID against account tag `customerid` when `UseAuthID=1` `:945` (`BadAuthID` otherwise); requests `0xBD` only if version > 1.26.04 `:948-952`.
- Errors → `CClient::addLoginErr` `CClientLog.cpp:92` → `0x82 PacketLoginError` `SX/network/send.cpp:2581`.

### 1.5 Login server phase

- `CClient::Login_ServerList` `CClientLog.cpp:296-334` → `LogIn(acct,pass,sMsg)` → **`0xA8 PacketServerList`** `send.cpp:3289`, mode `CLIMODE_SETUP_SERVERS`.
- `0xA0 PacketServerSelect::onReceive` `SX/network/receive.cpp:1940-1946` (3 bytes `:1936`) → `CClient::Login_Relay(idx)` `CClientLog.cpp:261-294` → `CClient::addRelay` `:209-259`: AuthID = crc32(servername+account) when `UseAuthID` `:236-243`, stored as account tag `customerid`; sends **`0x8C PacketServerRelay`** (11 bytes: ip, port, authid) `send.cpp:2809-2821`.
- **Same-socket continuation is explicitly supported:** `PacketServerRelay::onSent` `send.cpp:2823-2830` calls `m_Crypt.InitFast(customerId, CONNECT_GAME)` (`CCrypto.cpp:283-300`) "in case the client decides not to establish a new connection". After this the socket is `CONNECT_GAME` and `processGameClientData` runs; the next bytes **must be a bare `0x91`** — a re-sent raw 4-byte seed would be read as an unknown opcode and the buffer discarded (`CNetworkInput.cpp:399-406`).
- `0x8C` reports `ServIP`/`ServPort` of the same server (single-server config) — same port as login.

### 1.6 Game server phase

- `0x91` first on a fresh socket → `xProcessClientSetup` → `GameCryptStart`; on the relayed socket → `PacketCharListReq::onReceive` `receive.cpp:1688-1698` (65 bytes `:1684`; skips 4-byte session key, reads acct/pass) → `CClient::Setup_ListReq` `SX/game/clients/CClientMsg.cpp:2989-3052`: requires `CONNECT_GAME` `:2995`; `LogIn()`; sends **`0xB9 PacketEnableFeatures`** `send.cpp:3860` (flags from `CServerConfig::GetPacketFlag` `SX/game/CServerConfig.cpp:2873`; `0x03` if cliver < 1.26 `CClientMsg.cpp:3036-3040`), then **`0xA9 PacketCharacterList`** `send.cpp:3371`, mode `CLIMODE_SETUP_CHARLIST`.
- `0xA9` pads to ≥5 slots for `MINCLIVER_PADCHARLIST` **or any unencrypted client** (`CClientMsg.cpp:1632`). Extra start-info block only for ≥7.0.13 (`send.cpp:3397`, `MINCLIVER_EXTRASTARTINFO` `sphereproto.h:744`).
- `0x5D PacketCharPlay::onReceive` `receive.cpp:950-969` (73 bytes `:946`; slot dword at offset 65, ip at 69) → `CClient::Setup_Play(slot)` `CClientMsg.cpp:2911-2942` → `CClient::Setup_Start(pChar)` `:2795-2909` (fires `@Login`, `CharDisconnect()` prior) → `CClient::addPlayerStart` `:1465-1506`:

```
0x1B PacketPlayerStart (37 B, send.cpp:624)
addMapDiff()             -> 0xBF sub 0x18 (CClientMsg.cpp:2130)
MoveToChar() + Update()  -> 0x20 draw player, 0x78 mobiles, 0x1A items (0xF3 only >=7.0.0, send.cpp:5346), 0x4E/0x4F light, 0x11 status
addPlayerWarMode()       -> 0x72
addLoginComplete()       -> 0x55 PacketLoginComplete (1 B, send.cpp:1571)
addTime()                -> 0x5B (4 B)
addSeason()              -> 0xBC (3 B)
```

- `0x00 PacketCreate::onReceive` `receive.cpp:58-152` → `doCreate` `:158+` → `Setup_Start` `:243`. Also `0x8D` `:1511`, `0xF8` `:4628`.
- `0x83 PacketCharDelete` `receive.cpp:1483-1495` → `Setup_Delete` `CClientMsg.cpp:2944-2987` (`MinCharDeleteTime`) → `0x86` `send.cpp:2613`.

### 1.7 Account/password core — `CClient::LogIn(name,pass,sMsg)` `CClientMsg.cpp:3162-3253`

- `GUEST*` names auto-create up to `GuestsMax` `:3200-3220`; empty password rejected `:3223`.
- `fAutoCreate = (AccApp == ACCAPP_Free || GuestAuto || GuestTrial)` `:3230` → `CAccounts::Account_FindCreate(name, fAutoCreate)` `SX/game/clients/CAccount.cpp:189-211` (rejects names starting with a digit). `ACCAPP_TYPE` enum `SX/game/CServerDef.h:28-38` (`0 Closed, 2 Free, 3 GuestAuto, 4 GuestTrial, 6 Unspecified`); ini default **`AccApp=0` (closed)** `SX/sphere.ini:162`.
- Then `ClientLoginMaxTries`/`CheckPassword` `:3239-3248` → `LogIn(CAccount*)` `:3054-3160`: blocked priv, already-in-use (same-IP reconnect allowed if old char lingering `:3072-3100`), `ClientMax`, script `f_onaccount_login`.
- Hooks: `f_onaccount_create` in `CAccounts::Account_Add` `CAccount.cpp:228-247`.

### 1.8 Session end / timeouts

- **No `0x01` handler** (no `XCMD_*=0x01` in `sphereproto.h`). `0xD1 PacketLogout` `receive.cpp:3518-3522` (1 B) only replies `0xD1 PacketLogoutAck` `send.cpp:4631`.
- Real logout = **close the socket** → `CClient::CharDisconnect` `SX/game/clients/CClient.cpp:166-233`. Unless `CanInstantLogOut()` `:139-164`, an `IT_EQ_CLIENT_LINGER` item on `LAYER_FLAG_ClientLinger` keeps the char in world for `ClientLinger` seconds (`:206-217`; ini `ClientLinger=300` `sphere.ini:211`), then `SetDisconnected()` `SX/game/chars/CCharAct.cpp:4094-4097`. `@LogOut` trigger may alter linger `:187-195`.
- Idle kick: `CNetworkInput::processData()` `CNetworkInput.cpp:159-172` — no input for `DeadSocketTime` (minutes; default 5, `sphere.ini:1118`) ⇒ `addLoginErr(Other)`. `m_timeLastEvent` refreshed on every received chunk `:290`. `0x73 PacketPingReq` `receive.cpp:1335-1344` (2 B) → `0x73 PacketPingAck` `send.cpp:2266` — any packet keeps the socket alive.

---

## 2. Which protocol/client versions Source-X supports

- Key table: `SX/sphereCrypt.ini` (`<ver> <key1> <key2> <ENC_TYPE>`), loaded by `CCryptoKeysHolder::LoadKeyTable` `CCrypto.cpp:32-56`; **index 0 is always a synthetic no-crypt key** (`addNoCryptKey` `:58-64`). Range: **1.25.23 … 7.0.116** classic (`sphereCrypt.ini:26` newest; `:189-216` = 2.0.9 → 1.25.23) plus KR/EC. T2A rows: `1.26.0` `:204`, `1.25.35` `:207`; Renaissance `2.0.0-2.0.9` `:189-199` (2.0.0-2.0.3 `ENC_BTFISH`, 2.0.4+ `ENC_TFISH`).
- Encryption types `ENC_NONE/BFISH/BTFISH/TFISH/LOGIN` `SX/common/crypto/crypto_common.h:26-33`.
- Login-phase detection `CCrypto::LoginCryptStart` `CCrypto.cpp:521-620`: tries every key starting at nocrypt, accepts when decrypted `raw[0]==0x80 && raw[30]==0 && raw[60]==0` `:571-590`; unknown ⇒ falls back to unencrypted `:546-553`. Game-phase `GameCryptStart` `:622-720`: `ENC_NONE, BFISH, BTFISH, TFISH` looking for `raw[0]==0x91 && raw[34]==0 && raw[64]==0` `:639-661`, then ENC_LOGIN keys for <1.26 `:664-712`, else forces `ENC_NONE` `:716-718`.
- **Unencrypted clients: supported, gated by `UseNoCrypt`** (`CClientLog.cpp:1007-1010`; `CServerConfig.cpp:1025`). Ini defaults `UseCrypt=1` `sphere.ini:193`, **`UseNoCrypt=0` `sphere.ini:196` — must be set to 1 for the bot.**
- Version source for nocrypt clients: only the `0xEF` seed (`m_reportedVersionNumber`) or the `0xBD` reply (`PacketClientVersion::onReceive` `receive.cpp:2477-2521` → `net->m_reportedVersionNumber` and account tag `ReportedCliVer` `:2509-2517`; mismatch with ini `ClientVersion` ⇒ `BadVersion` `:2510-2511`). `CNetState::isClientVersionNumber` = crypt-version **or** reported-version `SX/network/CNetState.cpp:397-400`.
- `ClientVersion` ini key (`SX/game/CServerDef.cpp:245,273,331-332`; validated `CServer.cpp:2816-2822`); default commented out `sphere.ini:190` ⇒ any supported version.
- Feature thresholds (`MINCLIVER_*` `sphereproto.h:697-745`) govern packet formats; relevant ones for a 2.0.7-reporting client (all evaluate **false** ⇒ legacy formats):

| Threshold | Value | Effect when false |
|---|---|---|
| `MINCLIVER_STATUS_V2` | 3.0.8d | `0x11` v1 (`send.cpp:154-165`) |
| `MINCLIVER_PADCHARLIST` | 3.0.0.10 | (nocrypt pads anyway `CClientMsg.cpp:1632`) |
| `MINCLIVER_DAMAGE/TOOLTIP/NOTOINVUL` | 4.0.0 | no `0xBF.22`, no `0xDC/0xD6` (`PacketPropertyList::CanSendTo` `send.cpp:4793`) |
| `MINCLIVER_NEWDAMAGE` | 4.0.7a | no `0x0B` (`send.cpp:124-127`) |
| `MINCLIVER_BUFFS` | 5.0.2b | no `0xDF` (`send.cpp:5103`) |
| `MINCLIVER_ITEMGRID` | 6.0.1.7 | `0x25` 20 B, `0x3C` 19-B records, `0x08` 14 B in (`send.cpp:932,1212`; `receive.cpp:420`) |
| `MINCLIVER_EXTRAFEATURES` | 6.0.14.2 | `0xB9` 3 B (`send.cpp:3870`) |
| `MINCLIVER_SA` | 7.0.0 | `0x1A` not `0xF3` (`send.cpp:5346`); no `0x17` (`send.cpp:454`) |
| `MINCLIVER_HS` | 7.0.9 | `0x24` 7 B (`send.cpp:876`) |
| `MINCLIVER_EXTRASTARTINFO` | 7.0.13 | short `0xA9` |
| `MINCLIVER_NEWMOBINCOMING` | 7.0.33.1 | `0x78` legacy equip records `itemid|0x8000`+hue (`send.cpp:2441,2478`) |

- Client types `CLIENTTYPE_2D/3D/KR/EC` `sphereproto.h:647-653`. `0xBF` sub 1/2 fastwalk keys exist only as enum names `sphereproto.h:219-220`; the `0x02` reader ignores the key dword (`receive.cpp:266`).

**Conclusion:** Source-X can serve a client that identifies as **2.0.7 unencrypted** and will emit T2A/Renaissance-era packet layouts for it. No hard minimum version constant; <1.26 clients are handled explicitly. Supported range: 1.25.23 → 7.0.116 (+KR/EC).

### 2.1 Outbound compression

`CNetworkOutput::sendPacketData` `SX/network/CNetworkOutput.cpp:385-465`: Huffman via `CClient::xCompress` (`CClientLog.cpp:23`, table `SX/common/crypto/CHuffman.cpp:9`) **only when `GetConnectType()==CONNECT_GAME`** `:416`; encryption only for `ENC_TFISH` `:431`. Login phase (`0xA8`, `0x8C`) is plaintext; everything after `0x91` (`0xB9`, `0xA9`, `0x1B`, …) is compressed. The table (`CHuffman.cpp:15-…` `0x0002,0x01f5,0x0226,…`) is the standard UO table and **matches uo-client's `kEnc`** (`UC/src/net/Huffman.cpp:11-13` `{2,0x0000},{5,0x001F},{6,0x0022},…`).

### 2.2 Config, scripts, build

- Tree = C++ engine + template `SX/sphere.ini` + `SX/sphereCrypt.ini`. **No game scripts.** Only `.scp`: `packaging/debian/data/sphereacct.scp`. Scripts expected at `ScpFiles=scripts/` `sphere.ini:55`, sourced from `Sphereserver/Scripts-X` (`server/Source-X/docs/Getting-started.md:266-278`). Accounts dir `AcctFiles=accounts/` `:61`.
- Build: CMake ≥3.29 (`CMakeLists.txt:3`), C++20; Windows toolchains in `server/Source-X/cmake/toolchains/` (`Windows-MSVC.cmake`, `Windows-Clang-*`, `Windows-GNU-*`).
- Ini keys relevant to the bot: `ServIP/ServPort` `:21,24`, `AccApp` `:162`, `UseCrypt/UseNoCrypt` `:193,196`, `ClientMax/ClientMaxIP` `:199,202`, `ClientLinger` `:211`, `WalkBuffer/WalkRegen` `:218,221` (speedhack detection), `UseAuthID` `:245`, `FeatureT2A` `:893`, `DeadSocketTime` `:1118`, `NetworkThreads` `:1153`, `UseAsyncNetwork` `:1167`. (`CServerConfig.cpp` parse table `:793-1033`.) A `ClientLogin` key does not exist — UNKNOWN/none.

---

## 3. What xrip/uo-client implements today

Flow driver: `Client::Run` `UC/src/Client.cpp:136-158` → `ConnectAndSendSeed` `:160-197` → `PumpUntilDisconnected` `:212-328` → `Dispatch` `:335-398`. State enum `UC/src/Client.h:102-113`. Config struct `Client::Config` `Client.h:55-91`, populated in `UC/src/main.cpp:7-46` (argv: host, port, user, pass, gamePortOverride, gameHostOverride `:51-56`; `--headless` `:47-49`; **no config file**).

### 3.1 Login

| Step | Code | Layout |
|---|---|---|
| seed | `build::Seed` `UC/src/builders/Builders.cpp:27-30`; sent `Client.cpp:173-190` | raw 4 B BE = `cfg_.plaintextSeed` (default `0xAC1CA001` `main.cpp:14`). **No `0xEF`.** |
| `0x80` | `build::LoginRequest` `Builders.cpp:37-44` | 62 B: user[30], pass[30], nextLoginKey `0x5D` (`UC/include/uo/builders.h:21-22`); sent right after seed `Client.cpp:149-154` |
| `0xA8` | `Client::OnServerList` `Client.cpp:414-461` | stride-40 entries; `selectedServer_=0` hardcoded `:447` |
| `0xA0` | `build::SelectServer` `Builders.cpp:47-52` | 3 B |
| `0x8C` | `Client::OnConnectToGameServer` `Client.cpp:536-641` | **never reconnects** (block commented `:617-632`). Branch A (`gamePort != loginPort`) `:572-575`: re-sends raw 4-byte seed=authKey `:592-594` then `0x91` `:598-603` on same socket. Branch B (ports equal or overrides): `0x91` only `:633-637`. Both then `huff_.Reset(); decompress_=true` `:607-608,638-639` |
| `0x91` | `build::GameLogin` `Builders.cpp:59-66` | 65 B: authKey u32, user[30], pass[30] |
| `0xB9` | `OnFeatures` `Client.cpp:800-804` | expects 3 B |
| `0xA9` | `OnCharacterList` `Client.cpp:654-699` | count `[3]`, 60-B slots from `[4]`, cap 5; **`selectedChar_=0` hardcoded** `:680` |
| `0x81` | `OnLegacyCharList` `:478-524` | pre-T2A; blocking stdin prompt `:504` |
| `0x5D` | `build::PlayCharacter` `Builders.cpp:77-88` | 73 B: `0xEDEDEDED`, name[30], zeros[30], slot u32 @65, clientIP=0 @69 `Client.cpp:689-693` |
| `0x1B` | `OnLoginConfirm` `Client.cpp:713-743` | serial, body `[9]`, x `[11]`, y `[13]`, z `[16]`, dir `[17]` |
| `0x55` | `OnLoginComplete` `:748-765` | → `State::InWorld`, stdin thread, auto-open backpack via `0x06` |
| `0xBD` | `OnClientVersionQuery` `:790-795` + `build::ClientVersion` `Builders.cpp:94-104` | replies `cfg_.version` = **"2.0.7"** `main.cpp:12` |
| `0x82` | `OnLoginDenied` `:770-783` | → `Failed` |
| `0x73` | `OnPing` `:2376-2381` → `build::PingReply` `Builders.cpp:107-111` | echo |

### 3.2 Character selection
Hardcoded slot 0 (`Client.cpp:680`); no config field. Server index hardcoded 0 (`:447`).

### 3.3 Movement
- `build::MoveRequest` `Builders.cpp:298-313`: `legacy=false` ⇒ **7 B** (cmd, dir|0x80 run, seq, fastwalk u32). Callers `Client::BotPumpMoves` `UC/src/navigation/Navigation.cpp:988-991` and `UC/src/client/ClientRender.cpp:1431-1434` pass key `0`, `cfg_.legacyMovePacket=false` (`main.cpp:23`).
- Sequence `Client::NextSeq` `Navigation.cpp:327-331`: starts 0, returns-then-increments, **wraps 0xFF→1**; reset to 0 by `BotResetMovement`.
- `kMaxInFlight=4` `:32`, walk 400 ms / run 200 ms `:35-36`, ack watchdog 5 s `:37`.
- `0x22` `OnMoveAck` `:304-320` (3 B); `0x21` `OnMoveReject` `:126-293` (8 B; snaps pose, resets seq, restores path steps, fatigue/mobile/door/blacklist logic); `0x20` `OnDrawGamePlayer` `Client.cpp:2577-2614` (19 B, aborts path).

### 3.4 Speech
- Out `0x03` `build::SpeechAscii` `Builders.cpp:322-335` (len, type, hue, font, text NUL); `Client::SayAscii` `Client.cpp:3089-3099` (type 0, hue 0x40, font 3). **`0xAD` unicode: not implemented.**
- In `0x1C` `OnAsciiMessage` `:2501-2523`; `0xAE` `OnUnicodeMessage` `:2531-2553` (UTF-16BE @48, degraded to ASCII).

### 3.5 Targeting
In `0x6C` `OnTargetCursor` `Client.cpp:2248-2258` (JS `target` event). Out builders `TargetCursorObject/Ground/Cancel` `Builders.cpp:261-289` (19 B); senders `TargetRespondObject/Ground/Static`, `CancelTargetCursor` `Client.cpp:2280-2352`.

### 3.6 Inventory
- Out: `0x07 PickUpItem` `Builders.cpp:230-236` (7 B); `0x08 DropItem` `:238-247` **14 B, no grid byte**; `0x13 EquipItem` `:249-256` (10 B). Callers `Client.cpp:2036-2038, 2071-2090, 2136-2137, 3113-3120`.
- In: `0x3C` `OnContainerContents` `:1126-1152` (19-B records); `0x25` `OnAddItemToContainer` `:1225-1246` (20 B); `0x24` `OnDrawContainer` `:1092-1119` (7 B); `0x2E` `OnEquipItem` `:1457-1465`; `0x1A` `OnObjectInfo` `:926-997` (old flag-bit layout); `0x77` `:1393-1400` (17 B); `0x78` `:1407-1433` (legacy equip records, hue iff `graphic&0x8000`).
- Caches `Client.h`: `items_` `:480-483`, `corpses_` `:493-502`, `openContainers_/containerItems_` `:513-516`, `playerEquip_` `:466-467`, `mobileCache_` `:535-554`.

### 3.7 Logout
**None.** No `0x01`/`0xD1` builder. Exit = socket close via loop exit (`Client.cpp:217,241-246,259-263,276-285`) or `Client::~Client` `:128-134`. Stdin thread not interruptible `:2655-2662`.

### 3.8 Framing / Huffman
- `PacketStream::TryNext` `UC/src/net/PacketStream.cpp:182-217` with `kPacketLength[]` `UC/include/uo/packet_lengths.h:24-73` (ported from client 2.0.7 `g_PacketLengthTable`). **Length 0 ⇒ "unknown opcode" ⇒ pump returns false ⇒ process exits** (`Client.cpp:276-285`). All slots `0xCD..0xFF` are 0 (`packet_lengths.h:63-72`).
- Huffman decode only (`UC/src/net/Huffman.h:394-418`, `Huffman.cpp:119-153`); enabled after `0x91`.

### 3.9 Runtime shape
- Windows/MSVC only in practice: `UC/src/net/Socket.h:5` `<winsock2.h>` unconditional, `Client.cpp:18` includes MiniFB, `CMakeLists.txt:32-40,72-74`, `scripts/build.bat:4-8` (`vcvars32` + Ninja, 32-bit). CMake says C++23 (`CMakeLists.txt:3`).
- `--headless`: `RenderTick` returns early (`ClientRender.cpp:174-175`), still links MiniFB.
- **One client per process:** `main.cpp:61-62` builds one `Client` and blocks in `Run()`; JS bindings use `static Client* client; static JSContext*` (`UC/src/js/ClientBindings.cpp:14-19`); `Logger::Instance()` singleton (`UC/src/Logger.cpp:10-11`).
- MULs (hardcoded `E:/uo/...` `main.cpp:18-22,28-37`): walkability needs `tiledata.mul, map0.mul, staidx0.mul, statics0.mul` (`Navigation.cpp:336-350`, lazy on first goto); renderer additionally `art*/texmaps*` (mandatory when renderer on, `ClientRender.cpp:183-198`). Map 0 only (`Navigation.cpp:348-350`).
- `UC/pol_packets.md` is unused reference text (no source references it).

---

## 4. uo-client assumptions specific to the UO Demo (draxinar/ouo) server

| # | Assumption | Cite | Sphere reality |
|---|---|---|---|
| 1 | nocrypt; seed is a relay token = server IP `0xAC1CA001` | `main.cpp:14`, `builders.h:15-17`, `README.md:180-181` | Sphere ignores seed value except 0/`0xFFFFFFFF` (`CNetworkInput.cpp:636-655`). OK. Needs `UseNoCrypt=1`. |
| 2 | Raw 4-byte seed, no `0xEF` | `Builders.cpp:27-30` | Accepted as "old client login handshake" (`CNetworkInput.cpp:627-633`). OK. Version then comes from `0xBD` reply. |
| 3 | `0x8C` never reconnects; Branch A re-sends seed on same socket | `Client.cpp:551-611,617-632` | Sphere supports same-socket via `InitFast` (`send.cpp:2823-2830`) but expects bare `0x91`. **Branch A would break** (seed bytes = unknown opcode → buffer discarded). Sphere single-server returns same port ⇒ **Branch B taken automatically** ⇒ OK, but fragile. |
| 4 | Huffman on immediately after `0x91`, ouo table | `Client.cpp:604-608`, `Huffman.cpp:7-10` | Identical to Sphere: compression starts when `CONNECT_GAME` (`CNetworkOutput.cpp:416`); same table. OK. |
| 5 | 2.0.7 length table; opcodes ≥0xCD fatal | `packet_lengths.h:5-18,63-72`; `Client.cpp:276-285` | Sphere gates modern opcodes on version (§2). Once Sphere knows version=2.0.7, `0xDC/0xDF/0xF3/0x0B/0x17` are suppressed. **Risk window:** packets sent before the `0xBD` reply lands, and any ungated opcode. Mitigation: never exit on unknown opcode. |
| 6 | Fastwalk key always 0, "server ignores it" | `Navigation.cpp:21-24,991` | Sphere also ignores it (`receive.cpp:266`). OK. |
| 7 | No anti-speedhack; pipeline depth 4 | `Navigation.cpp:21-32` | **Sphere has speedhack detection:** `Event_Walk` `SX/game/clients/CClientEvent.cpp:909-913` (`m_timeNextEventWalk`) + `WalkBuffer/WalkRegen` (`:930-935`, `sphere.ini:218-221`). Pipelining 4 moves at 200 ms may trigger `0x21` rejects. Reduce depth. |
| 8 | Seq wrap 0xFF→1 | `Navigation.cpp:327-331` | Sphere expects 0 after 255 (`receive.cpp:270-282`) ⇒ one spurious reject every 255 steps (self-heals via reset). Fix to wrap 255→0. |
| 9 | No client keepalive; `0x73` "junk on UO Demo", keepalive would be `0x09` self-click | `main.cpp:24`, `Client.cpp:42,309-323` | Sphere kicks after `DeadSocketTime` (5 min) silence (`CNetworkInput.cpp:159-172`); `0x73` is answered (`receive.cpp:1335-1344`). Use `0x73` keepalive. |
| 10 | `0x1A` old layout, `0x77/0x78` per 2.0.7 | `Client.cpp:917-925,1402-1406` | Matches Sphere legacy paths (`send.cpp:471-540`, `:2441-2480`). OK. |
| 11 | `0xB9` = 3 B | `Client.cpp:800-804` | Sphere sends 3 B iff version tags < 6.0.14.2 (`send.cpp:3870`) — requires `0xBD` reply before `0x91`. It is: Sphere requests `0xBD` in `Login_ServerList` (`CClientLog.cpp:900-908`) and uo-client answers immediately. Verify at runtime. |
| 12 | `0x3C/0x25` no grid, `0x08` 14 B, `0x24` 7 B | §3.6 | Match Sphere for <6.0.1.7 / <7.0.9 (§2 table). OK. |
| 13 | `0x81` legacy char list path | `Client.cpp:463-524` | Sphere sends `0xA9`. Dead code; harmless. |
| 14 | `0x5D` clientIP=0 | `Client.cpp:693` | Sphere skips ip (`receive.cpp:962`). OK. |
| 15 | Vendor buy is speech-triggered + ouo `0x3C/0x74` ordering quirk | `Client.cpp:1154-1195`, `bot-client.md:343-366` | Sphere uses `0x74` + `0x3B` buy packet. Out of scope now; needs rewrite later. |
| 16 | Door open via `0x12` sub `0x58` | `Builders.cpp:189-196` | Sphere `0x12` handler registered (`CPacketManager.cpp:38-64`); `0x58` support UNKNOWN. Out of scope now. |
| 17 | Map 0 only, Britannia size constants | `Navigation.cpp:348-350` | Revolution map size UNKNOWN; `0x1B` carries map size (`send.cpp:643-644`). Out of scope now. |
| 18 | Creature knowledge from ouo `templatestable.dat` | `bot-client.md:281-338` | N/A for Sphere. |
| 19 | Version string "2.0.7" | `main.cpp:12` | OK; makes Sphere select legacy formats. Must **not** be raised to a modern version or the length table breaks. |
| 20 | Hardcoded MUL paths `E:/uo`, host `172.28.160.1` | `main.cpp:8-9,18-22` | Must become config. |
| 21 | Blocking stdin prompts / stdin thread | `Client.cpp:504,2627-2662` | Incompatible with headless multi-bot. |

---

## 5. What must change for uo-client to connect to local Sphere Source-X

### 5.1 Server side (config only — no engine change)

| Setting | Value | Why |
|---|---|---|
| `UseNoCrypt=1` | `sphere.ini:196` | bot is plaintext (`CClientLog.cpp:1007-1010`) |
| `UseCrypt=1` | keep | human client still encrypted |
| `AccApp=2` (Free) *or* pre-created accounts in `accounts/sphereacct.scp` | `sphere.ini:162`; `CClientMsg.cpp:3230` | bot accounts must exist; Free auto-creates on first login |
| `UseAuthID=1` | keep | uo-client echoes the `0x8C` key in `0x91` (`Client.cpp:599`) ✓ |
| `ClientVersion` | leave commented | `0xBD` "2.0.7" must not mismatch (`receive.cpp:2510`) |
| `ServIP` | LAN/loopback IP | `0x8C` returns this; bot ignores it anyway |
| `FeatureT2A=01\|02`, `FeatureLBR/AOS/SE/ML=0` | defaults | keeps flags in 2-byte `0xB9` range |
| `WalkBuffer` | keep 15; tune if bots get rejected | `sphere.ini:218` |
| Scripts | install `Scripts-X` into `scripts/` | engine has none (§2.2) |
| `DebugFlags` | add `DEBUGF_PACKETS` during bring-up | `CServerConfig.cpp:832`; `CNetworkInput.cpp:344` |

### 5.2 Client side (uo-client) — minimum

1. **`0x8C` handling** `Client.cpp:536-641`: remove the raw-seed re-send (Branch A). Always send bare `0x91` on the same socket (Sphere `InitFast` path, `send.cpp:2823-2830`). Optionally implement the real reconnect (new TCP, seed, `0x91`) — Sphere supports both; same-socket is simpler.
2. **Unknown-opcode policy** `Client.cpp:276-285`: never exit the process. For a length-0 opcode: log, drop *this* connection, record the opcode. Already-correct entries for Sphere login extras: `0xBF` variable, `0xBC` 3, `0x5B` 4, `0xC8` 2, `0x4E` 6, `0x4F` 2.
3. **Sequence wrap** `Navigation.cpp:327-331`: 255→0, matching `receive.cpp:270-282`.
4. **Keepalive**: enable `0x73` ping every ≤60 s (`Client.cpp:309-323` currently sends `0x09`; switch to `0x73` with counter). Sphere `DeadSocketTime=5 min`.
5. **Movement pacing**: `kMaxInFlight` 1–2 and honour 400/200 ms until `WalkBuffer` behaviour is measured (`CClientEvent.cpp:909-935`).
6. **Config**: move host/port/user/pass/char slot/version/MUL dir out of `main.cpp:7-46` into a file or CLI; add `charSlot` to `Client::Config` (`Client.h:55-91`) and use it at `Client.cpp:680`.
7. **Logout**: explicit close: stop bot, flush, `sock_.Close()`. (Sphere treats close as logout; `ClientLinger` keeps the char 300 s — acceptable.)
8. **Headless hardening**: no stdin thread when headless (`Client.cpp:2640-2653`). Multi-client per process (statics `ClientBindings.cpp:14-19`, `Logger.cpp:10-11`) — **not required for the milestone**.
9. **MUL paths**: not needed for login/say/logout. For `walk` use raw `0x02` steps (no A*); A* needs Revolution client `map0/statics0/staidx0/tiledata` configured (`Navigation.cpp:336-350`).

Already matching, **no change needed**: seed format; `0x80` 62 B (`receive.cpp:1450`); `0xA8` parse; `0xA0` 3 B; `0x91` 65 B (`receive.cpp:1684`); `0xBD` reply; `0xB9` 3 B; `0xA9` parse; `0x5D` 73 B slot @65 (`receive.cpp:946-962`); `0x1B` 37 B (`send.cpp:624`); `0x55`; `0x02` 7 B (`receive.h:62`); `0x03` layout (`receive.cpp:305-322` skips font); `0x21` 8 / `0x22` 3 (`send.cpp:757,781`); `0x20` 19 B; `0x77` 17 B; `0x78` legacy; `0x1A` legacy; `0x3C/0x25/0x24` sizes; `0x08` 14 / `0x13` 10 / `0x07` 7 / `0x06` 5 / `0x09` 5 / `0x6C` 19 (`receive.cpp:358-522,1074`); Huffman table.

---

## 6. uo-offline: behavioral reference vs ModernUO-bound code

Architecture verdict: **every bot is a server-side `PlayerMobile` subclass** (`UO/playerbots/source/CustomBots/PlayerBot.cs:28`) ticked by server timers (`BehaviorTickManager.cs:20-79`). 120 C# files / ~38.7k LOC; only 5 files lack `using Server`. **No network client exists. ~0% of the C# is directly reusable.** Reusable value = data tables + decision heuristics.

### 6.1 Useful behavioral/economic references (re-express client-side, no ModernUO)

| Concept | Cite (`UO/playerbots/source/CustomBots/`) |
|---|---|
| Class roll weights (18 classes) | `BotClass.cs:67-89` |
| Skill-tier bell curve (initial population only) | `BotSkillTier.cs:38-47` |
| 7-skill T2A build templates per class + PK templates → **target builds** (verify vs Revolution) | `BotSkillTemplate.cs:74-190,195-230` |
| Tier → skill milestones, stat profiles scaled by tier | `BotSkillTemplate.cs:298-334,57-66,340-372` |
| Personality traits/tendencies/phase durations | `Behaviors/BotPersonality.cs:18-27,47-80` |
| Session length 1–4 h, hourly population curve | `BotSessionManager.cs:42-57` |
| Lifecycle transition cadence | `Behaviors/BotLifecycleManager.cs:42-44` |
| Retreat HP thresholds by tier, PK flee, duel stop | `Behaviors/AdventurerBehavior.cs:42,61,373-379`; `Behaviors/PKBehavior.cs:47`; `BotDuelSystem.cs:96` |
| Spell selection table + openers (verify vs Revolution magery) | `AdventurerBehavior.cs:966-975,1010-1026` |
| Mana rest thresholds | `AdventurerBehavior.cs:322-323` |
| PK victim scoring (from *observed* mobiles only) | `PKBehavior.cs:15-19,44-59,989-1000` |
| Supply thresholds per class | `BotSupplies.cs:43-48,59-100,156-168` |
| Magic-travel decision gates | `Behaviors/MagicTravel.cs:52-87` |
| Destination types + class weights + home-city bias | `DestinationType.cs:19-70`; `BotEconomy.cs:34-83`; `Behaviors/DestinationCatalog.cs:218` |
| Danger heat map (45-min half-life) | `BotDangerMap.cs` |
| Gossip weights / event journal (witnessed events only) | `BotEventJournal.cs:58-74`; `Behaviors/PlayerBotBehavior.cs:102` |
| Friend graph + greet cooldown | `BotSocialGraph.cs:26-27` |
| Guild catalog | `BotGuilds.cs:43-60` |
| Name pools/generators | `NamePool.cs:31-249,321-360` |
| Speech hues, chat cadence | `SpeechHues.cs`; `PlayerBotBehavior.cs:27-44` |
| Chat corpora + gossip templates (data) | `UO/playerbots/data/PlayerBotChat/**` |
| Bank crowd roles | `Behaviors/BankSitterBehavior.cs:33-41` |
| Price heuristics (haul, buyback, gold by tier) — *heuristics only* | `BotEconomy.cs:316`; `Behaviors/CrafterBehavior.cs:626`; `EquipmentTable.cs:54-64` |
| Kit sizing / reagent stash targets (shopping lists) | `EquipmentTable.cs:821-856` |
| Waypoint/destination/zone JSON schemas + offline nav tooling | `WaypointGraph.cs:16-34`; `UO/playerbots/data/{Destinations,Waypoints,Zones}`; `UO/tools/*.py` |
| Death state machine (haunt → healer ≤500 tiles → corpse run) minus cheats | `BotDeathManager.cs:42-53` |

### 6.2 ModernUO-specific — cannot be reused (by subsystem)

Global grep (`CustomBots/`): `Timer.` 67, `World.` 65, `Map` 268, `GetMobilesInRange|GetItemsInRange` 51, `MoveToWorld` 40+, `AddToBackpack|DropItem` 35, `Delete()` 65, `PathFollower|MovementPath` 50, `.Skills[` 40, `Region` 27.

| Subsystem | Non-reusable API usage |
|---|---|
| Character | `PlayerBot : PlayerMobile` `PlayerBot.cs:28`; `Player=true` `:244`; `Backpack=new Backpack` `:310-314`; overrides `CheckShove` `:503`, `Move` `:519`, `IsHarmfulCriminal` `:536`, `OnDeath/OnAfterDelete/OnAfterSpawn/OnSpeech` `:580,747,764,794`; `Serialize/Deserialize` `:912,927` |
| Skills/stats | `Skills[...].Base =` `PlayerBot.cs:359,367,376,457`; `RawStr/RawDex/RawInt=`, `Hits=HitsMax` `:388-395`; `bot.Kills=` `PKBehavior.cs:140`; `SkillTier++; ReinitializeAsClass` `BankSitterBehavior.cs:122-123` |
| Equipment | `EquipmentTable.cs` throughout: `new Item()`, `EquipItem`, `AddToBackpack` (`:1036-1047,1077,1192,1531-1534,928-930`), `Quality/DamageLevel/AccuracyLevel` `:1458-1477,1491-1503`, `TrapType` `:877`; `StripGearAndPack` `PlayerBot.cs:469-483` |
| Economy | `new Gold()` `BotEconomy.cs:209,326,345`, `CrafterBehavior.cs:628`, `CrafterProfiles.cs:133,184,232`, `BotTreasureHunts.cs:386,637`, `EquipmentTable.cs:66`; `item.Delete()`/`MakeMaterial` `BotEconomy.cs:370-378`, `CrafterStock.cs:90-105`; `World.Mobiles.Values`/`Map.GetMobilesInRange` `BotEconomy.cs:169-182,296`; `Timer` `:105` |
| Supplies/vendors | reflection ctor + `pack.DropItem` `BotSupplies.cs:344-383`; sale proceeds regardless of gold `:324-333`; `ShopperBehavior.cs:59-64,108-121` is `Say`/`Direction` theater only; no vendor gump |
| Gathering | `new IronOre/Log` on timer, no skill check `GathererBehavior.cs:426-428`; `Timer.DelayCall` `:365`; `PathFollower` `:61`; `MoveToWorld` `BotPackAnimal.cs:105` |
| Crafting | no `CraftSystem`; item factories `CrafterProfiles.cs:112-126`; `Quality=Exceptional` `CrafterProduction.cs:155-165`; "illusion, not simulation" `CrafterProduction.cs:2-4` |
| Combat | `bot.Combatant=` `AdventurerBehavior.cs:510,563,596`; `foe.HitsMax` `:503`; fake regen `:625-634`; reflection `CreateSpell().Cast()` `:1101-1109,1583`; potion `Drink` `:1510-1530`; `BandageContext.BeginHeal` `:1544-1557`; `GetMobilesInRange` `:1198,1210`; `MoveDelays=Server.Movement.Movement` `:27`; `IsGuarded` `PKBehavior.cs:993`; `GrabGold` `:1158-1172`; `bot.Move(d)` `:971` |
| Magic travel | `BotRecallSpell : RecallSpell` `MagicTravel.cs:385,450-461`; `new RecallRune()` `:422`; gate = `MoveToWorld` `:663-684`; `MoongateTravel.cs:195` |
| Taming | `bc.Tamable/Controlled` `BotTaming.cs:134-135`; `SetControlMaster` `:285,449,333`; conjured pets `BotCombatPet.cs:116-129`; mounts `BotMountHelper.cs:70-89` |
| Scheduling | `Timer.DelayCall` `BotSessionManager.cs:74`, `BotLifecycleManager.cs:57`, `BehaviorTickManager.cs:30`; `World.Mobiles.Values` `BotSessionManager.cs:116,134`; logout=`bot.Delete()` `:253`; `MoveToWorld` `LifecycleTransitions.cs:82` |
| Memory | `BotEventJournal.Record(…,Point3D,Map)` `:128`; `Region` `:219`; `Core.BaseDirectory` `:79`; `Serial` keys `BotSocialGraph.cs:35-39` |
| Chat | `bot.Say/Emote`; `HandlesOnSpeech/OnSpeech` `PlayerBot.cs:744-751`; `Timer` `BotSceneRunner.cs:33` |
| Death | `bot.Resurrect()`, `Hits=55%` `BotDeathManager.cs:168-169`; `Corpse` `:187,269`; `MoveToWorld` `:392`; fresh kit `:307`; `GhostBehavior.cs:113`; `CorpseReclaimBehavior.cs:109` |
| Navigation | `PathFollower/MovementPath` `TravelerBehavior.cs:618,1738,1812`; `Map.CanFit/GetAverageZ/CanSpawnMobile` `Nav/Walkable.cs:38-50`, `BotStuckTelemetry.cs:521-523`; teleports `TravelerBehavior.cs:1151,1365,2803`, `DungeonCrawlerBehavior.cs:1094,1118`, `BotStuckTelemetry.cs:531`, `BotPartySystem.cs:500,560`, `BotFactionWar.cs:224`; `bot.Move(d)` `TravelerBehavior.cs:2872-2904`; `DoorHelper.cs`; `Nav/HpaGraph.cs:56` built from server `Map` |
| Spawner/persist | `PlayerBotSpawner : Spawner` `PlayerBotSpawner.cs:20-21`; `BotStartupManager.cs:44-65`; `BankFixtures.cs:107`; `IGenericWriter` `PlayerBot.cs:912` |
| Housing/guilds/treasure | `BaseHouse`, `HousePlacement.Check`, `Owner=null; RestrictDecay` `BotHousing.cs:326-329,381-389`; `ApplyNameSuffix` `PlayerBot.cs:552`; `new Ghoul/…`, `new MetalChest`, `new Gold` `BotTreasureHunts.cs:555-607,637` |
| Bank macros | `bot.Hidden=` `BankSitterBehavior.cs:140,214` |
| Admin/tooling | all `*Command.cs`, `AdminPanel/*` (`Gump` subclasses), `EditorReloadWatcher`, `LiveMapSnapshot`, `BotStatusPage`, `AuditNavCommand`, `BotPadAudit` |

### 6.3 Server-side cheats in uo-offline (forbidden by our Core Principle)

Skills/stats assigned (`PlayerBot.cs:359-395`); tier re-roll (`BankSitterBehavior.cs:119-131`); gold from nothing (§6.2 Economy); items from nothing (`EquipmentTable.cs`, `BotSupplies.cs:344-357`, `MagicTravel.cs:422`, `GathererBehavior.cs:426-428`); fake regen (`AdventurerBehavior.cs:625-634`); teleports (§6.2 Navigation); free resurrection (`BotDeathManager.cs:159-169`); murder counts set (`PKBehavior.cs:138-141`); conjured pets/mounts; `Hidden=true` without skill; criminal bypass (`PlayerBot.cs:536-549`); shove bypass (`:503`); omniscient scans (`BotEconomy.cs:169`, `BotSessionManager.cs:116`, `AdventurerBehavior.cs:503`); houses without deed (`BotHousing.cs:381-389`).

Honest counter-examples (behaviour reference only): real `RecallSpell` cast (`MagicTravel.cs:385-400`), real `Cast()` in combat (`AdventurerBehavior.cs:1109`), real bandage/potion use (`:1510-1557`), real corpse looting (`DungeonCrawlerBehavior.cs:355-437`), materials consumed per craft (`CrafterProduction.cs:75-79`).

### 6.4 `patches/`, `scripts/`, `tools/`, installers

- `UO/patches/0001-basehouse-keep-ownerless-bot-houses.patch`, `0002-publicmoongate-young-facet-fallback.patch` — diffs against ModernUO `BaseHouse.cs`/`PublicMoongate.cs` (`UO/INTEGRATION-NOTES.txt:1-70`). **Not applicable to Sphere.**
- `UO/scripts/start.sh` etc. — launch `ModernUO.dll` + ClassicUO. ModernUO-only.
- `UO/tools/*.py`, `map/map.html`, `.pgm` atlas — offline nav tooling; atlas produced by ModernUO `Walkable.TryFindSeedZ`, `serve_map.py:20-31` hardcodes ModernUO paths. **Algorithms portable; data must be regenerated from Revolution client MULs.**
- `UO/install*.ps1/.sh` — ModernUO/ClassicUO installers.
- `UO/docs/T2A-MAP.md` — map-mul swap procedure; only relevance: server and client must share one MUL set. Source comments cite `IDEAS`/`DESIGN-PLAN.md` which are **not in the repo (UNKNOWN)**.

---

## 7. Minimum implementation: Sphere → bot login → char select → walk → say → logout

Principle: bot is a normal unencrypted 2.0.7-identifying client. No Sphere engine changes. No AI.

### 7.1 Wire sequence (expected)

```
bot                                 Sphere
 |-- TCP connect ------------------->|  acceptNewConnection (CNetworkManager.cpp:111)
 |-- seed (4 B, any non-zero) ------>|  processUnknownClientData :627-633
 |-- 0x80 (62 B) ------------------->|  LoginCryptStart -> nocrypt key 0 (CCrypto.cpp:546-553) [UseNoCrypt=1]
 |<-- 0xBD version req --------------|  Login_ServerList (CClientLog.cpp:900-908)
 |-- 0xBD "2.0.7" ------------------>|  receive.cpp:2477 -> ReportedCliVer tag
 |<-- 0xA8 server list (plain) ------|  send.cpp:3289
 |-- 0xA0 idx 0 -------------------->|  Login_Relay
 |<-- 0x8C ip/port/authid (plain) ---|  send.cpp:2809; onSent -> InitFast(CONNECT_GAME)
 |-- 0x91 (65 B, authid) ----------->|  PacketCharListReq -> Setup_ListReq (CClientMsg.cpp:2989)
 |<== 0xB9 (3 B) [huffman from here] |  send.cpp:3860
 |<== 0xA9 char list ================|  send.cpp:3371
 |-- 0x5D slot N ------------------->|  Setup_Play -> Setup_Start -> addPlayerStart
 |<== 0x1B, 0xBF.18, 0x20, 0x78, 0x1A*, 0x4E/0x4F, 0x11, 0x72, 0x55, 0x5B, 0xBC
 |-- 0x02 dir|0x80, seq 0, key 0 --->|  Event_Walk -> 0x22 ack / 0x21 reject
 |-- 0x03 say "hello" -------------->|  Event_Talk -> 0x1C to nearby (incl. self)
 |-- 0x73 ping every 60 s ---------->|  0x73 ack
 |-- close socket ------------------>|  CharDisconnect -> linger 300 s -> SetDisconnected
```

Account must exist or `AccApp=2`. Character must exist in slot N: create once with a human client, **or** send `0x00 PacketCreate` (`receive.cpp:58-152`) — optional for the milestone; recommend human-created characters first, `0x00` builder next.

### 7.2 Minimal code changes (uo-client)

1. `Client::OnConnectToGameServer` `UC/src/Client.cpp:536-641` — delete Branch A seed re-send; always bare `0x91`.
2. `Client::PumpUntilDisconnected` `:276-285` — unknown opcode ⇒ log + disconnect this client, not process exit.
3. `Client::NextSeq` `UC/src/navigation/Navigation.cpp:327-331` — wrap 255→0.
4. `kMaxInFlight` `Navigation.cpp:32` — 1 for the milestone.
5. Keepalive `Client.cpp:309-323` — send `0x73` (builder `Builders.cpp:107-111`) every 60 s; default on.
6. `Client::Config` `UC/src/Client.h:55-91` + `main.cpp` — add `charSlot`, read all fields from CLI/ini; remove `E:/uo` defaults; use `charSlot` at `Client.cpp:680`.
7. Add `Client::Logout()` — set state, `sock_.Close()`; skip stdin thread in headless.
8. Add `Client::Walk(dir, run)` public step API bypassing A* (raw `0x02` + seq) so `walk` works without MULs.
9. Scripted smoke driver: CLI arg sequence or tiny JS (`Player.say` exists; add `Player.step`): `wait InWorld; walk N x5; say hello; sleep 2; logout`.

### 7.3 Test harness

- Sphere: build Source-X (`README.md:111-135`), `scripts/` = Scripts-X, `sphere.ini` per §5.1, packet debug on.
- Run: `uo-client --headless --host 127.0.0.1 --port 2593 --user bot1 --pass x --char 0 --script smoke.js`.
- Pass: Sphere log shows account login + char `@Login`; `0x22` acks for 5 steps (human client nearby sees movement); speech visible to human client; disconnect logged with linger.

---

## 8. Implementation checklist

Server (config/data only):
- [ ] Build Source-X for Windows (`server/Source-X/cmake/toolchains/Windows-MSVC.cmake`).
- [ ] Install `Scripts-X` into `scripts/` (`docs/Getting-started.md:273-278`).
- [ ] `sphere.ini`: `UseNoCrypt=1`, `AccApp=2` (or pre-seed `accounts/sphereacct.scp`), `ClientVersion` stays commented, `DebugFlags` packet logging, `ServIP` set.
- [ ] Verify a human Revolution client logs in (baseline before bot work).
- [ ] Create bot test account + one character in slot 0 via human client.

Client (uo-client):
- [ ] `0x8C`: bare `0x91`, no seed re-send (`Client.cpp:536-641`).
- [ ] Unknown opcode ⇒ non-fatal disconnect + log (`Client.cpp:276-285`).
- [ ] Seq wrap 255→0 (`Navigation.cpp:327-331`).
- [ ] `kMaxInFlight=1` (`Navigation.cpp:32`).
- [ ] `0x73` keepalive ≤60 s (`Client.cpp:309-323`).
- [ ] Config: host/port/user/pass/charSlot/version/mulDir via CLI/ini (`main.cpp`, `Client.h:55-91`, `Client.cpp:680`).
- [ ] `Client::Walk(dir)` raw step API (no MUL dependency).
- [ ] `Client::Logout()`; no stdin thread in headless.
- [ ] Smoke script: login → select → 5 steps → say → logout.
- [ ] Capture JSONL packet log of the successful run; diff every inbound opcode against `packet_lengths.h`; record any Sphere packet outside the 2.0.7 table.

Verification:
- [ ] Sphere console shows login/logout for the bot account; human client sees bot move and speak.
- [ ] Repeat 3× to confirm `ClientLinger`/re-login works (`CClientMsg.cpp:3072-3100` same-IP reconnect path).

Explicitly deferred: `0x00` char creation builder; `0xAD` unicode speech; vendor `0x74/0x3B` rewrite; `0x12/0x58` door check vs Sphere; A* on Revolution MULs (map size/ID from `0x1B`); multi-client per process (statics `ClientBindings.cpp:14-19`, `Logger.cpp:10-11`); any Revolution mechanics; any uo-offline behaviour port.

## UNKNOWN (not verified)
- Whether Sphere emits any ungated opcode ≥0xCD to a 2.0.7-reporting client during login (only a packet capture settles this).
- `Event_CheckWalkBuffer` exact tolerance vs 200 ms run cadence (`CClientEvent.cpp:930-935`).
- `0x12` sub `0x58` OpenDoor support in Sphere.
- `processOtherClientData` behaviour (`CNetworkInput.cpp:448`) if a bot sends before the seed.
- Revolution map dimensions / map ID; Revolution client MUL compatibility with `UC/src/mul/*` loaders.
- uo-offline `IDEAS` / `DESIGN-PLAN.md` referenced in source but absent.
