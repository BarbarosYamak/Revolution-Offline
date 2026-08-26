# uo-client — state & design notes

An Ultima Online **2.0.7-protocol** client + A\* navigation bot, written to
talk to the custom `ouo` server in the parent directory. It logs packets to
stdout and a JSONL file, and drives a character with `goto`-style pathfinding.
An optional MiniFB renderer (world view + minimap + HUD) is **on by default**;
pass `--headless` to disable it and run as a pure console client.

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

- **Gait** (`sphere::Gait` in `include/uo/sphere_rules.h`): `Walk` / `Run` /
  `Auto`, session default `Auto`, which means **run**. The `0x80` bit on the
  direction byte is the whole wire form (`DIR_MASK_RUNNING`,
  Source-X `src/game/uo_files/uofiles_enums.h:435`). `Auto` drops to a walk only
  when stamina is under the reserve or the character is at full carry weight —
  running adds `RunningPenalty` to the weight-load roll that costs stamina
  (`CChar::CanMoveWalkTo`, `src/game/chars/CCharAct.cpp:4818-4838`). Individual
  A* steps pin `Walk` for final approach, doorways and shoves (`BotStepGait`).
  A scenario overrides the session gait with `gait walk|run|auto`; `--walk` /
  `--run` pin it for a whole session.
- **Cadence:** canonical foot speeds — run **200ms** / walk **400ms** per step
  (`runStepMs` / `walkStepMs`) with no added jitter, resolved per step inside
  `SubmitStep` so the cadence and the wire bit can never disagree. The server
  ignores the fastwalk key (we send 0), but it *does* time running steps:
  `Event_CheckWalkBuffer` (`src/game/clients/CClientEvent.cpp:727-800`) runs only
  while `STATF_FLY` is set and expects ≥200ms between on-foot steps
  (`:766-767`), with `WalkBuffer=15` / `WalkRegen=25` enabled in
  `runtime/sphere.ini:218,221`. Sitting exactly on 200ms with one step in flight
  is what keeps running reject-free.
- **Pipelined ("fastwalk stack"), predict + reconcile:** `kMaxInFlight = 4`.
  Several `0x02` moves may be in flight at once. Each step is predicted on send
  (pos for a step, facing for a turn) and confirmed by `0x22`. The `0x22` ack
  carries **no position**, but we never need it — position is predicted locally
  and only corrected by a reject. `0x21` snaps us back to the server's
  authoritative pose; `0x20` is a full resync that aborts the path. Pipelining is
  safe because the reject carries position and the server has **no step-rate
  anti-speedhack** (it keeps a 5-slot `movementTimers` ring built for exactly
  this); the throttle still paces *sends*, so a deeper queue just removes
  round-trip stalls (smoother travel) without moving illegally faster.
- **Redundant rejects:** a blocked step makes the server set MovePrevented and
  deny **every** queued move behind it until we resend `seq 0`, so a depth-N
  pipeline produces N identical `0x21`s. `OnMoveReject` acts only on the first
  (the one whose seq is still in `pending`); the rest just resync the (identical)
  pose and return, so stuck-waits / blacklist / OpenDoor aren't double-counted.
  Steps speculatively consumed from `botPath_` are pushed back on a reject so the
  door-retry branch (the one reject path that doesn't replan) resumes correctly.
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
- `cast <spellId>` — cast a spell (1-based id) via `0x12`/`0x56`
- `skill <skillId>` — use a skill (0-based id) via `0x12`/`0x24`
- `use <0xserial|type|'name'> [pack]` — double-click (`0x06`) an item by serial,
  tiledata graphic id, or name substring; searches backpack → worn gear → nearest
  world item. `pack` (or `inv`/`self`) limits the search to backpack + worn gear.
- `arm|disarm [weapon|shield|both]` — move the weapon (layer 1) / shield (layer 2)
  to the backpack and back; `disarm` remembers the serial so `arm` re-equips it
  (mirrors `Macro_ActionArmDisarm_Validate`, `0x07`+`0x08` / `0x07`+`0x13`).
- `pickup <target>` — lift the nearest matching world item (`0x07`) into the backpack (`0x08`)
- `drop <target> <x> <y> [z]` | `drop <target> <0xcontainer>` — move a backpack item to a tile or container
- `equip <target> [pack]` — wear an item (layer from tiledata `quality`); searches
  backpack + world unless `pack` limits to the backpack
- `unequip <weapon|shield|target> [pack]` — take a worn item off; drops to the
  player's tile (world) by default, or into the backpack with `pack`
- `stop`, `pos`, `verbose [on|off]`, `day [on|off]`, `target ...`
- any other line is sent as 0x03 ascii speech

> Item-target tokens (`use`/`pickup`/`drop`/`equip`/`unequip`): a `0x…` value ≥
> `0x40000000` is treated as an object serial, a smaller number as a graphic/type
> id, and anything else as a tiledata name. Multi-word names must be quoted
> (`'…'`/`"…"`) for every command except `use`, which also accepts unquoted
> multi-word names.

---

## Pathfinding (`src/bot/Pathfinding.{h,cpp}`)

8-connected A\* over `World::QueryCell`:
- Costs 10 (straight) / 14 (diagonal); Chebyshev heuristic (admissible).
- **Diagonal corner rule:** a diagonal step needs both orthogonal neighbours
  walkable (no corner-cutting).
- Step limits `maxStepUp/Down = 12`, `charHeight = 16`.
- `maxNodesExpanded = 32768` (ample — a ~125-tile town route stays well under).
- **Grass penalty** (`grassPenalty = 14`): open grass land tiles (currently
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

## Out-of-range object cull (`Client::PurgeOutOfRange`)

The official client purges its object manager every tick: any entity past
`Entity_IsWithinWorldRange` (a **Chebyshev/square radius** of `g_ObjectUpdateRange`
tiles around the player, default **18**, set by `0xC8`) is destroyed
(`_Destroy_RefCounter`) and re-acquired from the server's re-send when it comes back
in range. Modeled on `CObjectManager_UpdateMovement` @0x4c8b00 +
`Entity_IsWithinWorldRange` @0x4c4720 + `Packet_HandleClientViewRange` @0x429170.

We mirror this for the two caches that otherwise grow stale:
- **Mobiles** (`mobileCache_`) — drop anyone past `viewRange_`; never the local player,
  and a mobile mid-death-animation is kept until its own `deadRemoveMs` timer fires.
- **Open world containers** (`openContainers_`/`containerItems_`) — close any whose
  backing world item (`items_`) has left range. Player-owned containers (backpack/bank)
  have no `items_` entry, so they are never matched and stay open.

`PurgeOutOfRange` runs once per pump iteration, self-gated on player tile movement
(`lastPurgeX_/Y_`), right after `BotTick()`. `viewRange_` comes from `0xC8` (clamped
5..24, default 18). Generic ground items in `items_` are **not** culled (they feed the
renderer/pathfinding and are pruned by `0x1D` + the FIFO cap), matching the named scope.

**JS soft-handling:** JS holds serials, never pointers, so a culled record simply reads
back as `exists === false`. Removal also queues a `mobile_leave` (serial) or
`container_close` (`{serial}`) event so a tracker can drop it promptly; the threat meter
additionally self-prunes its per-serial maps each sample, so it needs no leave handler.

---

## Combat interrupt (hook only)

`OnMobileHp` (`0xA1`) tracks our HP; a drop while travelling calls
`BotInterruptForThreat`, which **halts the path safely** (clears path + queue,
resets seq) and logs/event-logs the threat. The actual reactions —
**engage / flee / recall ("kal ort por")** — are explicit TODOs; they need
combat-target packet specifics, recall spell/reagent/rune handling, and a policy.

---

## Server data model (creatures / templates)

Findings from reading the `ouo` server (parent dir; real UO server source, C). Useful
whenever the bot needs to reason about *what* a mob is, not just where it is.

- **Creature template DB:** `bank/templatestable.dat` — one master file, null-padded
  (read with `tr -d '\000'`). Records begin at a `# <name>  Difficulty N` header and
  carry tag-style fields: `<type>`, `<name>`, `<corpsename>`, `<alignment>`,
  `<notoriety>`, `<script>`, `<strength>`/`<hp>`/`<sk ...>`, `<eq ...>`, `<resource ...>`.
  Sibling files in `bank/` are extra columns keyed by `template.NNN` (`freq`, `lim`,
  `near`/`dungn` = spawn regions, `morestats` = stats, `jobs`, `stuff` = equipment).
- **Body / anim id** (what the client sees in `0x78`/`0x77`, i.e. `mob.body`) comes from
  `<type NORMAL <CONST>>`; resolve `<CONST>` via `bank/defines` (e.g. `ETTINS 18`,
  `OGRES 1`, `ORCS 17` — these match real UO bodies). Some consts are **weighted random
  body sets** `{ body wt body wt }` (e.g. `TROLLS { 54 1 53 1 55 1 }` → bodies 53/54/55),
  so one creature can spawn as several bodies — include all. `<name NNN>` (500–605) is a
  template/name id, **not** the body.
- **Aggression (attacks on sight)** is template data, not a runtime flag:
  **`<alignment EVIL>`** (equivalently `<notoriety -125>`). `<alignment>` ∈ {NEUTRAL,
  EVIL, GOOD, CHAOTIC}. Animals like bear/cougar/panther/horse/deer are **NEUTRAL** — on
  this server they do **not** aggress on sight (do not auto-engage them).
- **`<script monster>` is NOT an aggression signal** — it is the generic creature AI,
  used by passive animals too (deer/horse/pig/bear all carry it). The combat-AI scripts
  are layered: `monster` (base creature), `spellai`/`dragonai`/`daemon` (casters/special),
  `pet`/`packanimal`/`reindeer` (tameable), `human` (townsfolk/NPC jobs).
- **Body 400 = human**, shared by harmless NPCs and evil human mages alike, so it is
  **ambiguous from the client** — the bot must not treat it as aggressive; rely on a
  confirmed hit instead.
- **Notoriety** the client sees is a per-viewer relationship (`CMobile_ComputeNotoriety`),
  so it can not by itself say "this mob is attacking me" — wild gray animals read the same
  whether idle or hostile. That is why the JS threat meter combines body-list aggression
  with confirmed-attack signals (HP drop, swing, "is attacking you!").

### Mobile HP (health bars)

The server does **not** push a foreign mobile's HP until the client asks for it.
To get a mob's HP, send a **`0x34` status query** (subtype `0x04`, serial) — the
real 2.0.7 client's "open health bar". The server (`HandlePacket_CLIENTQUERY` case
`0x04`) then:
1. replies once with a **`0x11`** status packet carrying the mob's `curHp@37` /
   `maxHp@39` (the extended stat block after byte 41 is only sent for self/editing), and
2. calls `CPlayer_SetLastTarget`, adding the serial to the player's **8-slot target
   history** (`CPlayer_HasTargetedSerial`).

Thereafter `CMobile_BroadcastStatUpdate` (@0x0047197C) **auto-pushes `0xA1`** HP
updates to every nearby player (≤18 tiles) whose target history holds that serial,
whenever the mob's HP changes — so **no polling is needed**. `0x34` is passive (it
does not aggro the target, unlike the `0x05` attack). The client side: `OnStats`
caches mob HP from `0x11`, `OnMobileHp`/`OnMobileAttributes` from `0xA1`/`0x2D`;
`createThreatMeter` sends the `0x34` for every dangerous mob so `mob.hpPct` is known
before engaging. Up to 8 mobs can be tracked at once (the history depth).

### Aggressive creatures

The resolved EVIL body list is baked into `scripts/js/bootstrap.js`
(`DEFAULT_AGGRESSIVE_BODIES`, the default `aggressiveBodies` for `createThreatMeter`).
Re-derive it with `awk` over `bank/defines` + the de-nulled `templatestable.dat` if the
shard's templates change.

---

## Vendors / buying (consumables restock)

Buying from an NPC vendor is **speech-triggered**, not a packet. The server's
shop handler (`Q4M7` in `scripts.wombat/human.m`) opens a vendor's buy window for
**any shop keyword** (`buy`/`trade`/`shop`/…, list in `Script_hasShopKeyword`)
spoken within **3 tiles** — there is **no name check**, so `Player.say('buy')`
near the vendor is enough, and every vendor in earshot responds (one window each).

The buy window arrives as a sequence (`CShopkeeper_OpenBuyWindow` @0x004D0C90):
a greeting, **0x3C** stock-container contents (serial/graphic/amount per item),
**0x74 SHOP_DATA** (price + name, *same order*), optionally a second 0x3C/0x74
pair for the offered/resale container, then a **0x24 gump 0x30** whose
"container" serial is the **vendor mobile**, and a 0x11 (gold). `Client::
OnVendorShopData` zips 0x3C+0x74 by index into `VendorItem` rows (carrying the
shop-container layer: `0x1A`=26 stock, `0x1B`=27 offered); the gump 0x30 in
`OnDrawContainer` finalizes the session and emits **`vendor_buy`**
`{vendor, items:[{serial,graphic,amount,price,layer,name}]}`.

To buy, send **0x3B** (`build::VendorBuy` / `Client::SendVendorBuy`):
`vendorSerial`, flag `2`, then `count×{layer(1), serial(4 BE), qty(2 BE)}`
(`HandlePacket_OFFERACCEPT` @0x00496C0F: `numItems=(len-8)/7`). The server replies
**0x3B OFFERACCEPT** (close), a speech total, and a coin sound; `Client::
OnVendorOfferAccept` emits **`vendor_done`** `{vendor, flag}`. Gold ≥ 2000 with an
empty backpack auto-draws from the bank (`CMobile_ProcessBuyList`).

**Identifying the right vendor:** names vary, and the plain name (single-click /
0x98) has no job. The job is only in the **paperdoll title** (`"<name> the
<job>"`, `CNPC_PaperdollTitle_VT`). `Player.doubleClick(serial)` (raw 0x06) on an
NPC opens its paperdoll → **0x88** → `Client::OnOpenPaperdoll` caches the title
(`mobile.title`) and emits **`paperdoll`** `{serial, title}`. So the bot matches a
vendor by `title.includes('healer')`, not by name.

JS surface: `Vendor.buy(vendorSerial, [{serial, qty}])`, events `vendor_buy` /
`vendor_done` / `paperdoll` (shared registry — `Vendor.once(...)` etc.),
`mobile.title`, `Player.doubleClick(serial)`. The reusable restocker is
`scripts/js/lib/restock.js` (`restockConsumables(consumables, token)`): for every
consumable the backpack is **out of** (count 0) it walks to `coords`, finds the
vendor by `title`, says buy, and buys up to `target`. The lumberjack wires it as
the tail of the bank cycle: `bank = sequence(goToBank, deposit, rest, restock)`,
config in `Lumberjack.CONSUMABLES`. Backpack counting is pure JS over
`Player.equipment.backpack.items` (each row has `name` + `amount`).

### Corrections from Source-X (M3, measured live)

The three paragraphs above describe the *reference* server. Against Source-X,
which is the server under test, three of their claims are wrong in ways that
cost live runs:

* **There IS a name check, and it matters.** `CClientEvent.cpp:1962` calls
  `NPC_OnHearName` on every character in earshot; a match sets `bNamed` and
  **breaks** the listener loop, so exactly one NPC answers. A bare keyword
  falls through to "pick closest NPC" — which is not necessarily the one you
  walked to. `Client::AddressMobile` now prefixes the cached paperdoll name
  (`"Taite buy"`, `"Cassiel sell"`).
* **A keyword is not understood until the NPC is in conversation.** Speaking
  before `e_Human_ConvInit` has opened routes the line to `e_Human_HearUnk` —
  *"Hmm?"* — and no window opens. Approach, let it greet, say hello, then
  trade.
* **Reach is two tiles, not three.** `CChar::CanTouch` ends in
  `if (iDist > 2) fCanTouch = false` (`CCharStatus.cpp:1423`), 3-D, after an
  LOS check. Vendors wander within `walkRange` 5, so re-approach immediately
  before each exchange rather than trusting the arrival.

Selling is the mirror of buying: `"sell"` answers with **0x9E** and *no* 0x24
gump, so the sell list is its own confirmation (`ActionVendorSellOpen`).

---

## World knowledge and travel (M2.5)

Above the tile A* sits a layer that takes destinations as *needs* rather than
coordinates. `Client::TravelToService(Service::Banker, "Yew")` resolves against
a world atlas, plans a route over a coarse navigation grid, and hands each leg
to `ActionGoto` — so `SubmitStep()` is still the only thing that ever builds a
`0x02`.

```
src/world/Atlas.*        regions / places / services, parsed from the atlas file
src/world/NavGrid.*      16x16-tile cells: standable anchor + MEASURED crossings
src/world/RoutePlanner.* cell A* + teleporter/moongate edges -> <=40-tile legs
src/world/SharedWorld.*  one immutable copy per process
src/world/AtlasGenMain.cpp   uo_atlasgen: derives both from the shard's data
src/travel/Journey.*     leg sequencing, stuck/oscillation, bounded recovery
src/travel/PersonalKnowledge.*  what THIS character has seen/marked/died at
src/travel/WarMode.*     when to sheathe
src/travel/ClientTravel.cpp  Client glue + 0xB0/0xB1 gump (public moongates)
```

Two things that are easy to get wrong and are worth stating:

* **A navgrid edge is measured, not inferred.** "Both cells hold standable
  ground" does not mean you can walk between them — rivers and walls satisfy
  the first and fail the second. `NavGrid::BuildEdges` runs this same A*
  between neighbouring anchors and records what actually works.
* **`Map::Open` derives the map width from the file size.** `map*.mul` has no
  header, and Revolution ships the ML-size Britannia (896 x 512 blocks). The
  old 768-block constant silently amputated everything east of x = 6143.

Generate the data with:

```
uo_atlasgen --scripts runtime/scripts --mul runtime/mul \
            --out-atlas data/revolution_atlas.txt \
            --out-grid  data/revolution_navgrid.bin
```

`uo_atlasgen --probe <x> <y>` reports what the generated world believes is at a
point. Full findings — moongates, Mark/Recall, the region model — are in
`docs/M2_5_WORLD_NAVIGATION.md`.

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
scripts/js/               JS bot scripts (lumberjack, …) + lib/ (auto-loaded)
src/js/                   QuickJS engine + Player/World/Mobiles bindings
```

> **Writing bots in JS:** see `BT.md` — the behaviour-runner framework
> (priority behaviours + cancellation tokens) with a full worked example.
> `scripts/js/lumberjack.js` is the reference bot.

---

## Key tunables (anon namespace in `Client.cpp`, unless noted)

| const | value | meaning |
|---|---|---|
| kMaxInFlight | 4 | moves in flight (pipelined fastwalk stack; reject de-dup in `OnMoveReject` makes depth>1 safe) |
| kMaxReplans | 40 | A\* replans per trip before giving up |
| kGrassPenalty | 14 | extra A\* cost on grass tiles |
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

## Fleet, route style and economy (M3.5)

Three self-contained units, all pure logic with injected clocks and seeds, all
covered by `tests/m35_authenticity.cpp` (856 checks).

**`include/uo/fleet.h` — connection admission.** Models THIS server's guard:
`MaxConnectRequestsPerIP=50`, a counter that **does not decay** and clears only
after `NetTTL` (300 s) of total silence *since the last attempt*, with a ~300 s
IP ban on overflow. Two consequences drive the design: a rejected attempt is
still an attempt (so retrying resets the clock you are waiting on — M3 banned
itself this way four times in three minutes), and the unit is the IP, so the
budget is fleet-wide. `AdmissionController` runs a conservative budget of 30,
staggers, backs off exponentially, breaks the circuit after repeated failures,
and **never emits an attempt while a ban is believed to be in force**.
`FleetLedger` shares the history between processes through a small text file.

**`include/uo/route_style.h` — bounded route variation.** Sits between the
world route and the tile A*:

```
semantic goal -> world route -> [route variation] -> local A* -> SubmitStep()
```

A `Style` derives from the character's name, so habits are stable across
sessions with no stored state. It answers three questions deterministically:
which of several equivalent approach tiles is mine, how much do I dislike this
tile (a bounded, stable spatial bias), and have I just walked here (a decaying
recent-path penalty). Five preferences change behaviour rather than decorate
it. Nothing here can make an illegal tile look legal.

**`include/uo/economy.h` — economic knowledge.** `PriceBook` separates prices
observed on this shard from RevolutionUO forum-era figures: `Best()` returns
only the former, and historical baselines are reachable only through
`HistoricalBaseline()`. `KnownTransformations()` records M3's carving discovery
as data with its evidence, and `EstimateTransformation` reports margin per unit
**and per stone** — because M3's fisher left eleven catches on the dock.

None of the three is wired into a live session yet; see the M3.5 debt list.

---

## uo_viewer — the observer client

`uo_viewer` is the graphical "watch the shard" client. It **replaces
ClassicUO** (`docs/OBSERVER_CLIENT.md`). It is not a second client: it is the
same `uo::Client` session engine with a different front end, so it logs in with
the same packets as a bot and cannot do anything a player cannot.

```
src/viewer/ViewerMain.cpp      entry point: credentials, paths, loop, Esc, PNG dump
include/uo/safe_graphics.h     the crash-proof gateway to every client-data table
tests/viewer_safety.cpp        ctest `viewer_safety` — the regression for it
```

```
cd build-m1
uo_viewer.exe                                  # first account in the creds file
uo_viewer.exe --user revolutionbot02
uo_viewer.exe --audit-only                     # graphics self-test only, no socket
uo_viewer.exe --dump-png frame.png --quit-after 30
uo_viewer.exe --creds ..\..\..\local\dev\observer-credentials.env
```

The two launchers at the project root run it for you and are the normal entry
points — they pass `--root` and the right credentials file, and forward any
extra arguments:

```
tools\launch_observer.bat                 # account Observer, normal player
tools\launch_admin.bat                    # account Admin, PLEVEL 7 (server-side)
tools\launch_observer.bat --create-char   # first run on an account with no character
tools\launch_*.bat --classicuo            # legacy ClassicUO path, still available
```

Defaults: `127.0.0.1:2593`, client version **2.0.3**, no encryption, client data
from `$UO_MUL_DIR` else `<root>/local/revolution-client`, credentials from
`<root>/local/dev/bot-credentials.env`. The project root is found by walking up
for that credentials file, or given with `--root`.

**Credential files.** `--creds` accepts every shape in `local/dev/`: any key
ending in `_ACCOUNT` or `_USER` names an account, and its siblings
`<PREFIX>_PASSWORD` / `<PREFIX>_PASS_<ACCOUNT>` / `<PREFIX>_HOST` /
`<PREFIX>_PORT` supply the rest, falling back to `UO_BOT_PASS_<ACCOUNT>` then
`UO_BOT_PASS`. So `bot-credentials.env`, `observer-credentials.env` and
`admin-credentials.env` all work unmodified, and a single-account file needs no
`--user`. The password goes exactly one place — the `0x80`/`0x91` login packets —
and is never printed, logged or put in the window title.

**Esc** sends `0xD1` and closes the socket, so the character logs out rather than
going link-dead. `M` toggles the minimap, `Tab` toggles war/peace.

### Why it exists: out-of-era graphics

Revolution ships Renaissance-era (2.0.3) client data while its server scripts
still hand out later graphics. ClassicUO died with `IndexOutOfRangeException`
every launch because it indexed tiledata/anim with whatever graphic arrived: the
unicorn mount item **`0x3EB4`** is a ship *prow* in this `tiledata.mul`, so the
`animId` it read there is not a body id.

`uo/safe_graphics.h` makes every such lookup **total** — it accepts the whole
32-bit input domain, every loader pointer may be null, and nothing can index out
of range:

* `SanitizeMountBody` / `SanitizeWornAnim` refuse a tiledata `animId` that is
  not a body / worn anim this era's `anim.mul` can address. Wired into
  `ClientRender.cpp` (`applyMountUnderlay`, `resolveEquip`).
* `SanitizeAction` / `SanitizeDir` / `SanitizeHue` clamp or refuse.
* `ArtLoader::SetPlaceholders(true)` (opt-in; bots leave it off) turns every
  unresolvable art index into a loud magenta placeholder instead of nullptr, and
  `Config::renderPlaceholders` additionally stands a placeholder on the cell of
  any mobile whose body cannot be drawn. An observer must *see* the object it
  cannot draw, not silently miss it.
* `TileDataLoader::Land/Static` return a zeroed tile when the loader holds no
  data, instead of dereferencing a null array.

`ctest -R viewer_safety` sweeps the whole domain through all of it with no client
data at all, and — when `UO_MUL_DIR` is set — again against the real MULs.

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
