# uo-client — state & design notes

A headless Ultima Online **2.0.7-protocol** client + A\* navigation bot, written to
talk to the custom `ouo` server in the parent directory. No GUI; it logs packets
to stdout and a JSONL file, and drives a character with `goto`-style pathfinding.

---

## Build

```
scripts\build.bat          # vcvars32 -> cmake -G Ninja -> ninja  (MSVC, 32-bit)
```
Output: `build\uo_client.exe` (+ `uo_mul.lib`, `uo_mul_dump.exe`).
The build uses `/EHs-c- /GR-` (exceptions & RTTI off); the C4530 warnings from STL
headers are expected and harmless. If linking fails with **LNK1168**, a previous
`uo_client.exe` is still running and holding the file — close it and rebuild.

Standalone test/diagnostic helpers (not part of the normal build):
- `scripts\build_hufftest.bat`  — Huffman compress/decompress round-trip.
- `scripts\build_bltest.bat`    — blacklist.mul (verdata) round-trip.
- `scripts\build_pathprobe.bat <sx sy sz gx gy>` — run the bot's A\* against the
  real MULs and report path length / node-cap behaviour.
- `uo_mul_dump.exe`  — dump tiledata / map cells / walkability (see its `--help`).

---

## Configuration (hardcoded in `src/main.cpp`)

| field | default | notes |
|---|---|---|
| loginHost / loginPort | 172.28.160.1 / 2593 | overridable via argv[1..2] |
| username / password   | xrip / xrip | argv[3..4] |
| version               | "2.0.7" | reported in 0xBD |
| plaintextSeed         | 0xAC1CA001 | = server IP; nocrypt |
| sendSeed              | true | 4-byte seed prefix on connect |
| legacyMovePacket      | false | false = 7-byte 0x02 (T2A+) |
| enableKeepalive       | false | 0x73 keepalive disabled |
| acceptDoors           | true | A\* routes through door tiles (see Doors) |
| tiledata/map/staidx/statics | `E:/uo/*.mul` | required for the bot |

The MUL files are loaded lazily on the first `goto`.

---

## Connection / protocol

Login flow (single TCP socket, "stay-on-socket" model):
`seed → 0x80 → 0xA8 → 0xA0 → 0x8C → (seed) 0x91 → 0xB9 → 0xA9 → 0x5D → 0x1B → 0x55`.

- **No encryption** (server is in nocrypt mode; `sendCipher` is null). The plaintext
  seed is just a relay token.
- **Huffman decompression** (`src/net/Huffman.{h,cpp}`): the server compresses the
  game stream the moment it processes our `0x91` (`HandlePacket_POSTLOGIN`). Login
  packets are plaintext; everything from `0xB9` onward is Huffman-compressed. The
  decoder builds a decode trie from the **same table the server compresses with**
  (so they can't drift), walks it MSB-first, and on flush marker **256** discards
  the rest of the current byte (per-packet byte alignment), matching the official
  client's `Network_ProcessBuffer` @ 0x42D8E0. Enabled right after we send `0x91`.
- Framing uses `g_PacketLengthTable` parity (`include/uo/packet_lengths.h`).

### Packets handled
- **Inbound:** 0x11 stats, 0x1A object, 0x1B login-confirm, 0x1C ascii msg,
  0x1D delete, 0x20 draw-player, 0x21 move-reject, 0x22 move-ack, 0x55 login-complete,
  0x73 ping, 0x77 mobile-move, 0x78 mobile-incoming, 0x81 legacy char-list,
  0x82 login-denied, 0x8C connect-to-gameserver, 0xA1 mobile-hp, 0xA8 server-list,
  0xA9 char-list, 0xAE unicode msg, 0xB9 features, 0xBD version-query, 0xC8 view-range.
- **Outbound builders** (`src/builders/Builders.cpp`): seed, 0x02 move, 0x03 speech,
  0x06 double-click, 0x09 single-click, 0x12 OpenDoor (subcommand 0x58),
  0x5D play-character, 0x73 ping, 0x80 login, 0x91 game-login, 0xA0 select-server,
  0xBD version.

---

## Movement model (`Client::BotPumpMoves` / `OnMoveAck` / `OnMoveReject`)

- **Running by default** (`botRun_`), `0x80` run bit on the direction byte.
- **Cadence:** canonical foot speeds — run **200ms** / walk **400ms** per step
  (`runThrottleMs_` / `walkThrottleMs_`) with no added jitter. The server has
  **no step-timing anti-speedhack** and ignores the
  fastwalk key (we send 0), so pacing is purely for realism / stamina.
- **Depth-1, predict + reconcile:** `kMaxInFlight = 1`. The server's `0x22` ack
  carries **no position**, so deeper pipelining would desync on a reject. Each
  step is predicted on send (pos for a step, facing for a turn) and confirmed by
  `0x22`; `0x21` snaps us back to the server's authoritative pose; `0x20` is a
  full resync that aborts the path.
- **Sequence:** starts at 0 (resync), 1..255 wrapping to 1; reset to 0 after a
  reject / resync.
- **Turn-then-step:** stepping a new direction first turns (server `DoTurn`,
  acked) then steps — handled by predicting facing on a turn.
- **Watchdog:** if the oldest in-flight move isn't acked within `ackWatchdogMs_`
  (5s) the path is aborted.

stdin commands (in-world):
- `goto <x> <y> [z]` — one-shot path to fixed coordinates
- `follow <name|0xserial> [distance]` / `follow off` — follow by name or serial; chase only when farther than `distance` (default 1)
- `mobiles` — first sends `0x98` AllNames queries for nearby mobiles, then lists `name serialId`
- `stop`, `pos`, `verbose [on|off]`
- any other line is sent as 0x03 ascii speech

---

## Pathfinding (`src/bot/Pathfinding.{h,cpp}`)

8-connected A\* over `World::QueryCell`:
- Costs 10 (straight) / 14 (diagonal); Chebyshev heuristic (admissible).
- **Diagonal corner rule:** a diagonal step needs both orthogonal neighbours
  walkable (no corner-cutting).
- Step limits `maxStepUp/Down = 12`, `charHeight = 16`.
- `maxNodesExpanded = 32768` (ample — a ~125-tile town route stays well under).
- **Grass penalty** (`grassPenalty = 6`): open grass land tiles (currently
  `0x0003–0x0006`, tunable in `IsGrassLikeTile`) cost extra, biasing routes onto
  roads/dirt/cobble where mobs are sparser. Heuristic stays admissible.
- **Blacklist overlay** consulted *after* the MUL checks (see below).
- With an explicit destination Z, `goalZ` is a finish-floor constraint and only
  biases surface selection near the goal. Intermediate cells still pick surfaces
  from the current `fromZ`, so a far route is not globally pulled toward the
  destination floor.

### Path lookahead (`Client::BotLookaheadPatchPath`)

The bot does not rebuild the whole path on every tick. When there are no pending
moves, it previews the already-built `botPath_`:

1. Simulate the next 5 path steps, plus 5 more candidate anchor steps, using
   `World::QueryCell` to track the predicted `standZ`.
2. A preview cell is runtime-blocked if it is in the transient blacklist, contains
   a fresh mobile from `0x77`/`0x78`, contains a blocking dynamic item from `0x1A`,
   or is not walkable by the normal MUL checks.
3. A door object is not treated as a blocker for reroute, but only when the door
   is in the exact step cell. Nearby doors are ignored so walking past a doorway
   does not spam OpenDoor.
4. If a block appears in the first 5 steps, the bot tries a small A\* patch from
   the current position to the first later preview anchor that is walkable and
   unblocked.
5. The patch search is capped at 4096 expanded nodes and does not use grass/forest
   penalties; it is meant to be a fast local detour, not a route-quality decision.
6. If a patch is found, it replaces only the path prefix and keeps the old path
   tail after the anchor. The patch may be shorter or longer than the skipped
   segment.
7. If no patch is found, `botPath_` is left untouched. A later real `0x21` reject
   is handled by the normal anti-stuck/reject fallback.

Door lookahead is separate: before stepping into a cached door cell, while already
facing the step direction, the bot sends the official macro packet
`0x12 OpenDoor (0x58)`, waits briefly, then retries the same step. It does not
double-click door serials.

### Walkability (`src/mul/World.cpp` `QueryCell` / `IsStaticBlocker`)
- Land surface walkable unless Impassable or Wet (water).
- Static surfaces = Surface|Bridge flags; their top z is a stand candidate.
  **Stairs** carry the Surface flag, so they're walked as ordinary stepped
  surfaces via the step-up limit — no special handling.
- A candidate surface is blocked if a non-surface static (Impassable|Wall|
  Window|NoShoot, **and Door unless `acceptDoors_`**) intrudes the vertical
  clearance column `[z, z+charHeight)`.
- `WalkResult` exposes `landTileId` (for the grass bias).

---

## Obstacle / door / mobile / fatigue handling

On a `0x21` reject the bot snaps to the server pose, clears the in-flight queue,
computes the blocked cell `(bx,by,bz)`, then decides **in this order**:

0. **Fatigue (stamina):** the `0x1C` "too fatigued to move" message sets
   `lastFatigueMs_`. A reject within `kFatigueWindowMs` (1.5s) of it is treated as
   stamina, **not** an obstacle → wait `kStaminaWaitMs` (2s) for regen, retry,
   **never blacklist**. (Walking into a character is a *shove* that succeeds when
   rested and is only denied when stamina is spent.)
1. **Mobile on the tile:** `mobileCache_` (from `0x77`/`0x78`, own serial excluded,
   pruned on `0x1D`) → if a mobile is on the blocked cell/floor it's a moving/shove
   obstacle → wait `kMobileWaitMs` (0.9s), retry, **never blacklist**.
2. **Door:** send the legit **OpenDoor action** `0x12`/`0x58` (server spatially
   searches the faced tile and opens any door there — graphic- and
   timing-independent). Door serials are not double-clicked. Doors don't swing
   instantly, so wait `kDoorWaitMs` (700ms) and retry, up to `kMaxDoorTries` (4)
   blind attempts.
   - **Open confirmation:** after sending an open we set `awaitingDoorOpen_`; any
     `0x1A` object update at the cell (≤2 tiles, ≤8 z) means the door swung →
     log "OPENED" and retry immediately; a re-bump with no update logs "did NOT open".
   - **Nearest-Z:** `FindDoorAt` returns the door closest in z (within ±8) so a
     door stacked on another storey is never the target.
   - **Guard:** a cell with a known door within 1 tile is **never blacklisted**;
     we only keep trying to open it, stopping the trip (no mark) after
     `kMaxDoorGiveUp` (10) if it truly won't budge.
3. **Wall / lamp post / unknown static:** none of the above and the door budget
   spent → add a **transient** avoid (this-trip only) and reroute; stop only if
   there's genuinely no other route.

Steps 0/1 keep retrying the same cell up to `kMaxStuckWaits` (25) then stop the
trip **without** marking anything.

### Important: blacklist.mul is currently read-only
`bot::Blacklist` (`src/bot/Blacklist.{h,cpp}`) is the runtime overlay (persistent +
transient spots, range/z-aware `IsBlocked`, checked by A\* after MUL). It can load
**and save** `blacklist.mul` in **verdata format** (big-endian entry table, file=1
statics patches, 7-byte records with the range carried in the hue field).

`Load("blacklist.mul")` runs at world-load, **but `AddPersistent` is no longer
called by the reject path** — auto-persisting was removed because it poisoned real
passages (a door/mob/lamp post is transient, not a wall). So today the bot only
ever uses **transient** avoidance during travel and never writes the file.
> If an old run left bad entries in `blacklist.mul`, delete the file once; it
> won't be recreated.

---

## Combat interrupt (hook only)

`OnMobileHp` (`0xA1`) tracks our HP; a drop while travelling calls
`BotInterruptForThreat`, which **halts the path safely** (clears path + queue,
resets seq) and logs/event-logs the threat. The actual reactions —
**engage / flee / recall ("kal ort por")** — are explicit TODOs; they need
combat-target packet specifics, recall spell/reagent/rune handling, and a policy.

---

## File layout

```
src/Client.{h,cpp}        connection state machine, dispatch, movement, bot logic
src/main.cpp              hardcoded Config + entry point
src/Logger.cpp            JSONL + console packet log
src/net/Socket.*          winsock wrapper
src/net/PacketStream.*    length-table framing
src/net/Huffman.*         server->client game-stream decompression
src/builders/Builders.cpp outbound packet builders
src/bot/Pathfinding.*     A* + DirToDelta + grass bias
src/bot/Blacklist.*       walkability overlay + blacklist.mul (verdata) I/O
src/mul/{File,TileDataLoader,Map,World}.cpp   MUL loaders + walkability
include/uo/*.h            shared headers (types, packet ids/lengths, mul, etc.)
tests/                    huffman / blacklist / path-probe standalone checks
```

---

## Key tunables (anon namespace in `Client.cpp`, unless noted)

| const | value | meaning |
|---|---|---|
| kMaxInFlight | 1 | moves in flight (depth-1; do not raise without position-carrying acks) |
| kMaxReplans | 40 | A\* replans per trip before giving up |
| kGrassPenalty | 6 | extra A\* cost on grass tiles |
| kDoorCacheMax | 20 | recent doors tracked |
| kMaxDoorTries | 4 | blind OpenDoor attempts before avoiding |
| kMaxDoorGiveUp | 10 | open attempts with a door present before stopping trip |
| kDoorWaitMs | 700 | wait for a door to swing |
| kMobileCacheMax | 64 | recent mobiles tracked |
| kFatigueWindowMs | 1500 | reject-after-fatigue window |
| kStaminaWaitMs | 2000 | wait for stamina regen |
| kMobileWaitMs | 900 | wait for a mobile to clear |
| kMaxStuckWaits | 25 | wait-retries at one cell before stopping (no mark) |
| walk/run throttle | 400 / 200 | step cadence (ctor) |
| ackWatchdogMs_ | 5000 | unacked-move abort (ctor) |

---

## Known limitations / TODO

- **Combat actions** (engage/flee/recall) — only the threat hook exists.
- **Road bias** — grass tile set is a minimal starting set (`0x0003–0x0006`);
  expand `IsGrassLikeTile` with this shard's exact grass/road IDs to sharpen it.
- **Door cache graphics** — `IsDoorGraphic` covers `0x0675–0x06F6`; the OpenDoor
  *action* is graphic-agnostic, so this only affects cached-door lookahead and
  confirmation logic.
- **verdata.mul read** — not implemented (this server only reads verdata's version
  word and never applies static/map patches, so base MULs already match it).
- **blacklist.mul auto-persist** — disabled (read-only) to avoid poisoning passages.
- **Map** — map0 (Britannia, 768×512 blocks) assumed.
