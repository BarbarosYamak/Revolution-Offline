# uo-client

A from-scratch **Ultima Online (T2A, protocol 2.0.7)** client written in C++17,
built as the engine for an **automation / bot framework** with a **graphical
frontend that recreates the look of the original 1997-era client**.

The point is not to give you another way to *play* UO by hand. The point is to
let a bot play — pathfind, follow, open doors, react to obstacles — while you
**watch it happen** in a faithful isometric window and step in when you want to.
This is the classic UO *babysitting* loop: the script grinds, you keep an eye on
it. Here the script is a real protocol client and the window is a software
reimplementation of the original renderer.

And the "script" can be a **real script**: an embedded JavaScript engine lets you
write the bot's high-level behaviour in JS on top of the C++ navigation core. The
one that ships — a **lumberjack** — chops trees, banks the logs, restocks
consumables from vendors, eats, and fights, flees or resurrects on its own.

## Demo

[![Watch the bot navigate in the isometric window](https://img.youtube.com/vi/0YYXLrZHQfE/maxresdefault.jpg)](https://www.youtube.com/watch?v=0YYXLrZHQfE)

*The A\* bot pathfinding across Britannia while the software renderer recreates the 1997-era client — click to watch on YouTube.*

> **Scope.** This client talks to one server: the reverse-engineered **UO Demo**
> shard at **[draxinar/ouo](https://github.com/draxinar/ouo)**. Compatibility with
> other shards (OSI, modern emulators) is **out of scope** and not planned.

---

## Table of contents

- [Demo](#demo)
- [What it is / what it is not](#what-it-is--what-it-is-not)
- [How it was built — LLMs + reverse engineering](#how-it-was-built--llms--reverse-engineering)
- [Architecture](#architecture)
- [Networking & protocol](#networking--protocol)
- [Movement & the navigation bot](#movement--the-navigation-bot)
- [Bot scripting (JavaScript)](#bot-scripting-javascript)
- [Renderer — the observation frontend](#renderer--the-observation-frontend)
- [The target server](#the-target-server)
- [Requirements & game assets](#requirements--game-assets)
- [Build](#build)
- [Run & configuration](#run--configuration)
- [Commands & window controls](#commands--window-controls)
- [Testing & regression harnesses](#testing--regression-harnesses)
- [Project layout](#project-layout)
- [Status, limitations & roadmap](#status-limitations--roadmap)
- [Developer documentation](#developer-documentation)
- [License & disclaimer](#license--disclaimer)

---

## What it is / what it is not

**It is:**

- A complete **2.0.7 protocol client** — login handshake, Huffman-compressed game
  stream, packet framing, movement, mobiles, items, stats, speech/journal.
- An **A\* navigation bot** that drives a character with predict-and-reconcile
  movement, door handling, dynamic obstacle avoidance, and follow logic.
- An **embedded JavaScript scripting layer** for writing bots as priority
  behaviours (cancellable async steps) with batteries-included banking, survival
  and vendor-restock skills — shipped with a full autonomous **lumberjack**.
- A **software isometric renderer** that draws land, statics, dynamic items,
  animated mobiles (with equipment, mounts, hues and night lighting), a radar
  minimap and a HUD — modelled directly on the original client's output.

**It is not:**

- A hand-playable game client. Manual controls exist (arrow-key walk, click-to-go,
  war/peace toggle) but only as *supervision* tools layered over the bot.
- A general-purpose UO client. It implements exactly what the target server speaks
  and is verified against that one client/server pair.
- A cheat for live/official shards. It targets a private, reverse-engineered
  research server and ships no game assets.

**Design goals, in priority order:**

1. **Automate flexibly.** The client core (`Client`) owns protocol state and feeds
   a navigation layer that any higher-level behaviour can drive.
2. **Observe faithfully.** The frontend should look and behave like the real 2.0.7
   client so you can supervise the bot the way you'd babysit a macro in-game.
3. **Stay correct by construction.** Behaviour is continuously checked against the
   decompiled original; the decompilation itself is treated as a living artifact.

---

## How it was built — LLMs + reverse engineering

This project is an experiment in **LLM-driven systems development**. Both the
application code **and** the reverse-engineering of the original client are done
**exclusively through large language models** — primarily **Codex 5.5** and
**Claude Opus 4.7**. There is no hand-written C++ baseline underneath; the LLMs
read the decompiled binary, form hypotheses, write the client, and verify the
result against the running original.

The methodology that keeps this honest:

- **The decompiled `client_2.0.7.exe` is the source of truth.** Wherever behaviour
  is non-obvious, the implementation cites the original by address or symbol. The
  source is dense with references such as `Network_ProcessBuffer @ 0x42D8E0`
  (Huffman / packet buffering), `CRadarGump_Update` / `CRadarGump_RenderMinimap`
  (the radar minimap rule), and `g_SittingChairTable @ 0x55DB68` (chair seating) —
  **43 decompiled-client references across 10 source files** at last count.
- **The decompilation is a living artifact.** Reverse-engineering happens in IDA,
  and every newly confirmed behaviour — plus every correction to a bad decompiler
  guess — is written **back into the IDB** as comments, renamed functions, types
  and variables, then saved. The local C++ source is never allowed to become the
  *only* record of a 2.0.7 finding.
- **Continuous verification.** Renderer changes are diffed against the official
  client visually (see [regression harnesses](#testing--regression-harnesses));
  protocol and pathfinding changes are checked against deterministic probes. When
  the model's port of a client routine diverges from the binary's behaviour, the
  binary wins and the code (and the IDB) are corrected.

The result is a codebase whose comments double as a reverse-engineering logbook:
reading `src/render/Renderer.cpp` or `src/net/Huffman.cpp` tells you not just what
the code does, but *which part of the original it mirrors and why*.

---

## Architecture

The code is C++17 written in a deliberate **"C with Classes"** style: no
exceptions, no RTTI (`/EHs-c- /GR-`), plain structs, explicit ownership via
`std::unique_ptr`, fixed-width type aliases (`u8`/`i32`/`usize` from
`include/uo/types.h`), `PascalCase` types and `lowerCamelCase` fields. There is no
heavy template metaprogramming and no hidden allocation in the hot paths.

```
                         ┌──────────────────────────────────────────┐
                         │                 Client                     │
                         │  protocol state machine · packet dispatch  │
                         │  movement · bot commands · render ticks    │
                         └───────┬───────────────┬───────────────┬────┘
                                 │               │               │
                ┌────────────────┘               │               └─────────────┐
                ▼                                 ▼                             ▼
        ┌───────────────┐                ┌─────────────────┐           ┌────────────────┐
        │      net      │                │   navigation    │           │     render     │
        │ Socket        │                │ PathPlanner     │           │ Renderer (iso) │
        │ PacketStream  │                │  (worker thread)│           │ Minimap/Radar  │
        │ Huffman       │                │ NavigationState │           │ Text / HUD     │
        └───────────────┘                │ + bot/ (A*,     │           │ MiniFBWindow   │
        ┌───────────────┐                │   Blacklist)    │           └────────┬───────┘
        │   builders    │                └────────┬────────┘                    │
        │ outbound pkts │                         │                             │
        └───────────────┘                         ▼                             ▼
                                          ┌─────────────────────────────────────────┐
                                          │                  mul                      │
                                          │  Map · TileData · World (walkability)     │
                                          │  Art · Texmap · Anim · AnimData · Hues    │
                                          │  Verdata · RadarColors                    │
                                          └─────────────────────────────────────────┘
```

| Module | Responsibility |
|---|---|
| `Client` (`src/Client.{h,cpp}`, `src/client/ClientRender.cpp`) | Connection state machine, packet dispatch, player/mobile/item caches, bot command surface, per-tick render driver. ~1.8k LOC of dispatch + glue. |
| `net/` | `Socket` (winsock wrapper), `PacketStream` (length-table framing), `Huffman` (server→client game-stream decompression). |
| `builders/` | Outbound packet construction (seed, move, speech, double/single click, OpenDoor, login, etc.). |
| `navigation/` | `PathPlanner` runs A\* on a **background worker thread** (request/poll); `NavigationState` holds movement, bot route, follow and learned-blocker state. |
| `bot/` | `Pathfinding` (the A\* core + grass bias) and `Blacklist` (runtime walkability overlay + `blacklist.mul` verdata I/O). |
| `js/` (`src/js/`) | Embedded **QuickJS** engine (`JsEngine`) plus `Player` / `World` / `Mobiles` / `Vendor` script bindings (`ClientBindings`). Runs the bot scripts under `scripts/js/`. |
| `mul/` | MUL/verdata asset loaders and `World::QueryCell` walkability. Also builds `uo_mul.lib` and the `uo_mul_dump` CLI. |
| `render/` | Software isometric `Renderer` (ARGB1555), `Minimap`/`RadarColors`, `Text`/HUD, and the `MiniFBWindow` host. |

---

## Networking & protocol

A single TCP socket using the **"stay-on-socket"** model. The login → in-world
flow is:

```
seed → 0x80 → 0xA8 → 0xA0 → 0x8C → (seed) 0x91 → 0xB9 → 0xA9 → 0x5D → 0x1B → 0x55
```

- **No encryption.** The server runs in *nocrypt* mode; the 4-byte plaintext seed
  is just a relay token (the default `0xAC1CA001` is literally the server IP).
- **Huffman decompression** (`src/net/Huffman.*`). The server begins compressing
  the game stream the moment it processes our `0x91` game-login; everything from
  `0xB9` onward is compressed. The decoder builds its trie from the **same table
  the server compresses with** (so the two can't drift), walks it MSB-first, and on
  the flush marker `256` discards the rest of the current byte (per-packet byte
  alignment) — matching the original client's `Network_ProcessBuffer @ 0x42D8E0`.
- **Framing** uses the `g_PacketLengthTable` parity rules in
  `include/uo/packet_lengths.h` (fixed-length vs. self-describing packets).

**Handled inbound packets** include: `0x11` stats, `0x1A` object, `0x1B`
login-confirm, `0x1C`/`0xAE` ASCII/Unicode messages, `0x1D` delete, `0x20`
draw-player, `0x21`/`0x22` move reject/ack, `0x2D` mob attributes, `0x2E` equip,
`0x3A` skills, `0x4E`/`0x4F` light levels, `0x55` login-complete, `0x6E`
animation, `0x72` war-mode, `0x73` ping, `0x77`/`0x78` mobile move/incoming,
`0x81`/`0xA9` char lists, `0x82` login-denied, `0x8C` connect-to-gameserver,
`0x98` mob name, `0xA1`/`0xA2`/`0xA3` HP/mana/stamina, `0xA8` server-list, `0xAF`
death, `0xB9` features, `0xBD` version-query, `0xC8` view-range.

Item, container and vendor flows add `0x24` container gump, `0x3C` container
contents, `0x74` vendor shop data, `0x3B` vendor offer, `0x88` paperdoll (the only
client-visible carrier of an NPC's job title), `0x2C` resurrect menu and `0x7C`
server menu/dialog.

**Outbound builders** (`src/builders/Builders.cpp`): seed, `0x02` move, `0x03`
speech, `0x06` double-click, `0x09` single-click, `0x12` OpenDoor (subcommand
`0x58`), `0x5D` play-character, `0x73` ping, `0x80` login, `0x91` game-login,
`0xA0` select-server, `0xBD` version.

The client also sends `0x07`/`0x08` (lift / drop), `0x34` (status query, for mob
HP), `0x3B` (vendor buy) and `0x98` (all-names query) for the item, vendor and
combat interactions used by the commands and scripts.

Every packet is logged to a JSONL file and to the console (gated by a `verbose`
toggle so the window isn't drowned in per-tick chatter).

---

## Movement & the navigation bot

### Predict-and-reconcile movement

Movement is **pipelined** (`kMaxInFlight = 4`, the "fastwalk stack"): several
`0x02` moves may be in flight at once, each predicting its new position/facing
locally, then reconciled on the server's replies.

- The `0x22` **ack carries no position**, but we never need it — position is
  predicted locally and only ever corrected by a reject. Pipelining is safe
  because the `0x21` reject carries the authoritative pose (see below) and the
  server has no step-rate anti-speedhack (the throttle still paces *sends*).
- `0x21` **reject** snaps the client back to the server's authoritative pose and
  drops the in-flight queue. A blocked step makes the server deny every queued
  move behind it (it holds MovePrevented until we resend `seq 0`), so a depth-N
  pipeline yields N identical rejects; only the first (whose seq is still
  in-flight) is acted on, the rest just resync the pose. Steps that were
  speculatively consumed from the path are restored so a reroute/door-retry
  resumes from the right spot.
- `0x20` is a full resync that aborts the current path.
- **Turn-then-step:** stepping a new direction first turns (an acked server
  `DoTurn`) and then steps; both are predicted locally.
- **Cadence:** canonical foot speeds — **run 200 ms / walk 400 ms** per step. The
  server has no step-timing anti-speedhack, so pacing is purely for realism.
- A **5 s watchdog** aborts the path if the oldest in-flight move is never acked.

### Threaded A\* planner

`navigation::PathPlanner` runs the A\* search on a **dedicated worker thread**. The
client posts a `PathRequest` (start, goal, blacklist, live mobiles, dynamic items)
and later `Poll()`s for a `PathResult` — so a long cross-continent search never
stalls the render loop or the network pump.

The search itself (`bot/Pathfinding`) is 8-connected A\* over
`World::QueryCell`:

- Costs **10** (straight) / **14** (diagonal); admissible Chebyshev heuristic.
- **No corner-cutting:** a diagonal step requires both orthogonal neighbours walkable.
- Step limits `maxStepUp/Down = 12`, `charHeight = 16`; node cap `32768`.
- **Grass penalty** biases routes onto roads/dirt/cobble (where mobs are sparser)
  while keeping the heuristic admissible.
- A **blacklist overlay** is consulted after the MUL walkability checks.

### Lookahead patching

Rather than rebuild the whole route every tick, the bot previews the next few
steps of the existing path, flags any cell newly blocked by the transient
blacklist / a fresh mobile (`0x77`/`0x78`) / a blocking dynamic item (`0x1A`) /
plain unwalkable terrain, and tries a small, cheap A\* **patch** around the
blockage — splicing it into the path prefix and keeping the tail. Full replanning
is the fallback.

### Obstacle, door, mobile & fatigue handling

On a `0x21` reject the bot decides, **in order**:

0. **Fatigue (stamina).** A reject shortly after a *"too fatigued to move"*
   message is treated as spent stamina — wait for regen and retry, **never**
   blacklist.
1. **Mobile on the tile.** A cached mobile on the blocked cell is a moving/shove
   obstacle — wait briefly and retry, **never** blacklist.
2. **Door.** Send the legitimate **OpenDoor action** (`0x12`/`0x58`); the server
   spatially searches the faced tile and opens any door there (graphic- and
   timing-independent). Confirm via the resulting `0x1A` update, retry, and a cell
   with a known door is **never** blacklisted.
3. **Wall / lamp post / unknown static.** Only now add a **transient** (this-trip)
   avoid and reroute.

`blacklist.mul` I/O exists (verdata format) but **auto-persist is disabled** —
the bot uses transient avoidance only, so it can't poison real passages.

---

## Bot scripting (JavaScript)

Above the C++ navigation core sits an embedded
**[QuickJS](https://bellard.org/quickjs/)** engine (`src/js/`), so the bot's
*high-level* behaviour is written in JavaScript — no recompile. Edit a script, type
`run` again, and it reloads in a fresh runtime; script errors are caught and
printed (`[js]`) and never crash the client.

```
run scripts\js\lumberjack.js      :: load + run a bot script (re-run to reload)
js stop                           :: tear the running script down
```

**Scripting surface** (`src/js/ClientBindings.cpp`): `Player` (live state +
actions — goto, use, equip, attack, follow, say, drop, `requestStatus`, …),
`World` (`statics`, the stump overlay), `Mobiles` (live serial-backed handles
carrying HP / notoriety / body / paperdoll title), and `Vendor` (speech-triggered
buying). Events (`on`/`once`) surface journal lines, target cursors, arrivals,
container opens, incoming/leaving mobiles, attacks, dialogs, resurrect menus,
paperdolls and vendor windows. `scripts/js/globals.d.ts` is the typed source of
truth for the whole surface.

**The behaviour runner** (`scripts/js/lib/bt.js`): a bot is a flat list of
behaviours in **priority order**. Each tick the highest-priority behaviour whose
guard is true owns the body, and a strictly higher-priority one **preempts** it.
Preemption is cooperative via a **cancellation token**: long awaits are wrapped so
a preempted step unwinds at once (a threat interrupts chopping within a tick, not
after the 15-second chop wait). A `BehaviorScript` base class (`lib/bot.js`) adds
the lifecycle (incl. an awaited one-time `onStartup`), movement (`walkTo`) and
inventory; opt-in mixins add banking (`lib/bank.js`) and survival/restock
(`lib/survival.js`). A shared **threat meter** (`lib/threat.js`) scores nearby
danger from a body-list of aggressive creatures plus confirmed-attack signals.

**The shipped bot** — `scripts/js/lumberjack.js` — is a complete worked example:
it rotates between forest stands chopping trees, banks the logs when full, withdraws
gold, restocks bandages/food from vendors (matched by paperdoll *job* title, not
name), eats on a timer, and — driven by the threat meter — fights, bandages
mid-fight, flees an unwinnable foe (rotating to a fresh stand and avoiding the
mob's area), and walks to a healer to resurrect when killed.

The full guide is **[`BT.md`](BT.md)**.

---

## Renderer — the observation frontend

The renderer (`src/render/Renderer.*`) is a **software isometric rasterizer** that
produces an **ARGB1555** framebuffer (one `u16` per pixel) and hands it to a
[MiniFB](include/win32/MiniFB.h) window, which does integer upscaling for free.
It is a deliberate reimplementation of the original client's draw pipeline, not a
generic engine — the projection, draw order, hue handling, lighting and minimap
rule are all modelled on `client_2.0.7.exe`.

What it draws, in painter's-algorithm order:

- **Land terrain** stretched across each tile's four corner z-values, sampling
  `art.mul` diamonds and `texmaps.mul` sloped textures.
- **Static art** from the map blocks, z-sorted against mobiles so same-z mobiles
  draw above world items correctly.
- **Dynamic server items** (`0x1A`) — lamp posts, doors (with their open/closed
  graphic offset), decor — keyed by serial.
- **Mobiles** (players/NPCs) as `anim.mul` body animations, with:
  - **Equipment** layered over the body at the same action/frame,
  - **Hues** from `hues.mul` colour ramps,
  - **Walk/run cadence** from `animinfo.mul`, with sub-cell sliding so sprites glide
    between cells in sync with the walk cycle (the local player stays centred and
    the world scrolls under it),
  - **Mounts & chair seating** — a mount body drawn under the rider, or a rider
    shifted onto a chair seat (ported from `g_SittingChairTable @ 0x55DB68`),
  - **Combat / death** animation states and war-mode poses.
- **Animated statics** via `animdata.mul`.
- **Procedural night lighting:** a per-pixel RGB darkness map seeded by the world
  light level (`0x4E`/`0x4F`), with smooth radial coronas subtracted for each
  classified light source (warm for fire/candles, white for lamps; a carried
  torch/lantern casts a moving pool). A `day [on<code>|</code>off]` toggle forces full daylight.

Overlaid on top of the world frame:

- **Radar minimap** (toggle `M`) — an isometric orientation panel using the same
  projection as the 3D view, coloured from `radarcol.mul` via the real client's
  radar rule (topmost surface wins), cached per 8×8 map block, auto-scaled to fit
  the player and the whole planned route, with route/player/goal markers. Modelled
  on `CRadarGump_Update` / `CRadarGump_RenderMinimap`.
- **HUD:** status bars (HP/mana/stamina), a scrolling system log / journal,
  overhead text, a chat input line, and the UO directional **walk cursor** under
  the mouse.

The window also accepts supervision input — see
[Commands & window controls](#commands--window-controls).

---

## The target server

The only supported backend is the reverse-engineered **UO Demo** server:
**[github.com/draxinar/ouo](https://github.com/draxinar/ouo)**. This client speaks
exactly that server's flavour of the 2.0.7 protocol (nocrypt, 1997-era move
packet, no client-side keepalive needed, no step-timing anti-speedhack).

Testing against other servers — official, ServUO, RunUO, etc. — is **not a goal**
and is not planned. Pointing the client at a different shard is unsupported and
will likely break at the handshake or framing layer.

---

## Requirements & game assets

- **Windows** (the renderer host and build scripts target MSVC; networking uses
  winsock). The non-Windows compile path exists for the MUL/bot code but the
  renderer is Win32.
- **Visual Studio Build Tools** (MSVC, 32-bit) + **Ninja** + **CMake ≥ 3.20**.
- **Original UO T2A-era MUL data files.** None are distributed with this repo —
  you must supply them from a legitimate Ultima Online installation. The paths are
  configured in `src/main.cpp` (default `E:/uo/*.mul`):

  | File(s) | Used for |
  |---|---|
  | `tiledata.mul` | Tile flags (walkability, surfaces, doors, light sources) |
  | `map0.mul`, `staidx0.mul`, `statics0.mul` | Map cells + static art (Britannia, map 0) |
  | `verdata.mul` *(optional)* | Version word / patch overlay (read-only here) |
  | `art.mul`, `artidx.mul` | Land + static tile bitmaps |
  | `texmaps.mul`, `texidx.mul` | Sloped land textures |
  | `anim.mul`, `anim.idx` | Mobile body animations |
  | `animdata.mul` | Animated static/dynamic art |
  | `animinfo.mul` | Mobile walk/run timing |
  | `hues.mul` | Colour ramps for tinted objects/mobiles |
  | `radarcol.mul` *(optional)* | Per-tile minimap colours |

  The MUL files are loaded lazily on the first navigation/render that needs them.

---

## Build

```bat
scripts\build.bat
```

This runs `vcvars32` → `cmake -G Ninja` → `ninja` and builds every target. Output
lands in `build\`:

- `build\uo_client.exe` — the client.
- `uo_mul.lib` — the MUL loader static library.
- `uo_mul_dump.exe` — a CLI for dumping tiledata / map cells / walkability.

Manual configure/build (with the MSVC environment already active):

```bat
cmake -S . -B build -G Ninja
cmake --build build
```

Build notes:

- Flags are `/W4 /EHs-c- /GR- /utf-8 /permissive-` — **exceptions and RTTI are
  off**, so the C4530 warnings from STL headers are expected and harmless.
- If linking fails with **LNK1168**, a previous `uo_client.exe` is still running
  and holding the file — close it and rebuild.

---

## Run & configuration

```bat
build\uo_client.exe                       :: use built-in defaults + renderer
build\uo_client.exe --headless            :: pure console client, no window
build\uo_client.exe <host> <port> <user> <pass> [gamePort] [gameHost]
```

Configuration lives in `src/main.cpp` as a `Client::Config`. The defaults are
**local placeholders** for the maintainer's LAN (host `172.28.160.1`, login
`xrip`/`xrip`) and are overridable on the command line — edit them locally or pass
args rather than committing your own environment. Key fields:

| Field | Default | Notes |
|---|---|---|
| `loginHost` / `loginPort` | `172.28.160.1` / `2593` | override via `argv[1..2]` |
| `username` / `password` | `xrip` / `xrip` | override via `argv[3..4]` |
| `version` | `2.0.7` | reported in `0xBD` |
| `plaintextSeed` | `0xAC1CA001` | = server IP; nocrypt relay token |
| `sendSeed` | `true` | 4-byte seed prefix on connect |
| `legacyMovePacket` | `false` | move-packet variant for the demo protocol |
| `enableKeepalive` | `false` | no client `0x73` keepalive |
| `acceptDoors` | `true` | A\* routes through doors, opened at runtime |
| `enableRenderer` | `true` | open the world window (`--headless` disables) |
| `*Path` (MUL files) | `E:/uo/*.mul` | see [assets](#requirements--game-assets) |
| `renderWidth/Height/Scale` | `960×540 ×2` | framebuffer + integer upscale |

---

## Commands & window controls

**stdin commands** (typed in the console while in-world):

| Command                                    | Effect |
|--------------------------------------------|---|
| `goto <x> <y> [z]`                         | One-shot A* path to fixed coordinates |
| `follow <name\|0xserial> [distance]`       | Follow a mobile; chase only when farther than `distance` (default 1) |
| `follow off`                               | Stop following |
| `mobiles`                                  | Query nearby names (`0x98`), then list `name serialId` |
| `cast <spellId>`                           | Cast a spell (1-based id) via `0x12`/`0x56` |
| `skill <skillId>`                          | Use a skill (0-based id) via `0x12`/`0x24` |
| `use <0xserial\|type\|'name'> [pack]`      | Double-click an item by serial, graphic id, or name; searches backpack → worn → nearest world item |
| `arm\|disarm [weapon\|shield\|both]`       | Move weapon/shield to backpack and back |
| `pickup <target>`                          | Lift nearest matching world item (`0x07`) into backpack |
| `drop <target> <x> <y> [z]\|<0xcontainer>` | Move backpack item to tile or container |
| `equip <target> [pack]`                    | Wear an item (layer from tiledata `quality`) |
| `unequip <weapon\|shield\|target> [pack]`  | Take a worn item off; drops to world or backpack |
| `stop`                                     | Abort the current path |
| `pos`                                      | Print the player's position |
| `day [on\|off]`                            | Force full daylight / restore server light levels |
| `verbose [on\|off]`                        | Toggle per-packet console chatter |
| `target ...`                               | Set target cursor |
| `run <script.js>`                          | Load + run a JS bot script in a fresh runtime (re-run to reload) |
| `js stop`                                  | Stop the running JS script |
| *anything else*                            | Sent as `0x03` ASCII speech |

**Item-target tokens:** `0x…` ≥ `0x40000000` → serial; smaller → graphic id; else → tiledata name. Multi-word names need quotes.

**Render-window controls** (supervision while the bot drives):

| Input | Effect |
|---|---|
| **Right-click** a tile | `goto` that cell |
| **Arrow keys** | Manual single-step walk (throttled) |
| **M** | Toggle the radar minimap panel |
| **SPACE** | Send OpenDoor for the faced tile |
| **TAB** | Toggle war / peace mode |
| **Type** | Enter the on-screen chat input |

---

## Testing & regression harnesses

Focused probes live in `tests/`, each with a build/run script in `scripts/`:

| Script / test | What it checks |
|---|---|
| `scripts\build_hufftest.bat` | Huffman compress/decompress round-trip |
| `scripts\build_bltest.bat` | `blacklist.mul` (verdata) round-trip |
| `scripts\build_pathprobe.bat <sx sy sz gx gy [margin]>` | A\* against the real MULs (path length / node-cap behaviour) |
| `scripts\build_viewer.bat [args]` | World-viewer probe (still renders) |
| `scripts\build_animprobe.bat` | Animation decode probe |

Two regression harnesses guard the behaviour-critical paths:

- **`scripts\path_regression.bat`** — runs two long cross-continent routes (Trinsic
  bridge ↔ Britain basement, both directions) and regenerates
  `tests\path_regression.txt`. Treat `result` / `steps` / `expanded` / `pathCost`
  as **deterministic** signals (any diff is a real behaviour change to explain);
  `searchUs` is wall-clock timing, read as a performance trend only. **Run this
  after any change under `src\bot\` or to `World::QueryCell`/walkability**, and only
  commit the baseline when the change is intended.
- **`scripts\render_regression.bat`** — Windows-only; dumps PNG scenes to
  `build\regression\` for visual comparison against the official 2.0.7 client
  (watch `07_negz_interior.png` for negative-Z interiors). **Run this after renderer
  changes.**

---

## Project layout

```
src/
  Client.{h,cpp}          connection state machine, dispatch, movement, bot logic
  client/ClientRender.cpp per-tick world drawing + HUD glue
  main.cpp                hardcoded Config + entry point
  Logger.cpp              JSONL + console packet log
  net/                    Socket · PacketStream (framing) · Huffman (decompression)
  builders/Builders.cpp   outbound packet builders
  navigation/             PathPlanner (threaded A*) · NavigationState
  bot/                    Pathfinding (A* + grass bias) · Blacklist (overlay + I/O)
  js/                     QuickJS engine (JsEngine) + Player/World/Mobiles/Vendor bindings
  mul/                    File · TileData · Map · World · Art · Texmap · Anim ·
                          AnimData · AnimInfo · Hues · Verdata · RadarColors · dump CLI
  render/                 Renderer (iso) · Minimap · RadarColors · Text · MiniFBWindow
include/
  uo/                     shared headers (types, packet ids/lengths, mul, ...)
  win32/MiniFB.h          windowing
tests/                    huffman / blacklist / path-probe / viewer / anim probes
scripts/                  build + regression batch files
  js/                     JS bot scripts (lumberjack) + lib/ (behaviour runner + skills)
AGENTS.md · BT.md · ...   developer + design documentation (see below)
```

---

## Status, limitations & roadmap

- **Combat in C++ is a hook; the policy lives in JS.** The C++ core only halts the
  path safely on a HP drop and logs the threat. The actual *engage / flee /
  resurrect* behaviour is implemented in the JS layer (the threat meter + the
  lumberjack's DPS-race assessment). **Recall** (spell + reagent/rune handling) is
  still TODO.
- **Road bias** uses a minimal grass tile set; expand it with this shard's exact
  grass/road IDs to sharpen routing.
- **`blacklist.mul` auto-persist is disabled** (read-only) to avoid poisoning
  passages — the bot uses transient avoidance only.
- **`verdata.mul` patching is not applied** (the target server only reads its
  version word, so base MULs already match).
- **Map 0 only** (Britannia, 768×512 blocks) is assumed.
- **Single backend.** Only the [UO Demo server](#the-target-server) is supported.

---

## Developer documentation

Several in-repo docs go deeper than this README:

- **[`AGENTS.md`](AGENTS.md)** — repository guidelines: structure, runtime notes,
  build/test commands, coding style, and the reverse-engineering workflow
  (including the rule that IDA findings are written back into the IDB).
  `CLAUDE.md` is a symlink to this file.
- **[`bot-client.md`](bot-client.md)** — the design & state notes: protocol flow,
  movement model, pathfinding internals, obstacle/door/mobile/fatigue handling, the
  full tunables table, and the known-limitations list.
- **[`BT.md`](BT.md)** — the bot-scripting guide: the priority behaviour runner,
  cancellation tokens, the `BehaviorScript` base class and skill mixins, with a full
  worked example. `scripts/js/globals.d.ts` carries the matching TypeScript types
  (the source of truth for the scripting surface).
- **[`JS-BIBLE.md`](JS-BIBLE.md)** — the deeper JS scripting reference.

---

## License & disclaimer

Licensed under a **custom non-commercial MIT-style license** — © 2026 Ilia
Maslennikov (xrip). You may use, modify and distribute it **for non-commercial
purposes only**, must retain the copyright/permission notice (including a link to
this repository), and may not sell or monetize it (or derivatives/services) without
prior written permission. See [`LICENSE`](LICENSE) for the exact terms.

*Ultima Online* is a trademark of its respective owners; this is an independent,
educational reverse-engineering project and is **not affiliated with or endorsed by**
them. No game data files (MULs) are included or distributed — you must provide your
own from a legitimate installation. The client is intended for use against your own
copy of the reverse-engineered [UO Demo server](https://github.com/draxinar/ouo).
