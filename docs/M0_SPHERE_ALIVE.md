# M0 — Sphere Source-X Alive

Date: 2026-08-25. Result: **M0 PASSED**. Vanilla Source-X + Scripts-X boots on Revolution 10 client data and listens on port 2593.

## Executable

- `C:\Projects\RevolutionOffline\runtime\SphereSvrX64_nightly.exe` (4,427,264 B, 2026-08-25 19:34), a copy of `server/Source-X/build-x64/bin-x86_64/Nightly/SphereSvrX64_nightly.exe`.
- Banner: `SphereServer Version X1-Nightly [Windows-x86_64]`, `Compiled at Aug 25 2026 (19:34:15) [branch master / build 4232 / GIT hash dd4183ddc97b494b4c6c9e5d453b73910dfa02a2]`.
- Runtime DLL alongside: `runtime/libmariadb.dll` (MySQL disabled in ini; not used).

## Source commits

| Component | Path | Commit |
|---|---|---|
| Source-X | `server/Source-X` | `dd4183ddc97b494b4c6c9e5d453b73910dfa02a2` — 2026-07-23 "Added new client encryptions (116) - fixed Enhanced client 115 encryption after merge" (matches the exe's embedded GIT hash) |
| Scripts-X | `server/Scripts-X` and the working copy `runtime/scripts` (clone of `https://github.com/Sphereserver/Scripts-X.git`) | `27e78bc896da239d3738fe02a6d6bf8e9045c16d` — 2026-03-27 "Normalization - readme update …" |

## Toolchain (from `server/Source-X/build-x64/CMakeCache.txt`)

- Generator: Visual Studio 18 2026, platform x64, configuration `Nightly` (`CMAKE_CONFIGURATION_TYPES=Debug;Release;Nightly`).
- Compiler: MSVC 19.51.36256.0 (`C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231`), linker `link.exe` from the same toolset. PE32+ (x86-64) image importing `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`, `MSVCP140.dll` and the UCRT `api-ms-win-crt-*` set.
- Build was done before this session (exe timestamp 19:34 today); this session did **not** rebuild.

## Runtime paths

```
runtime/
  SphereSvrX64_nightly.exe
  sphere.ini            (edited, see M0_SPHERE_CONFIG.md)
  sphere.ini.baseline   (pristine copy)
  sphereCrypt.ini       (vanilla)
  mul/                  MulFiles=mul/        <- Revolution MULs (server-required subset)
  scripts/              ScpFiles=scripts/    <- Scripts-X working copy (1 local edit: spheretables.scp)
  accounts/             AcctFiles=accounts/  (sphereaccu.scp created on first boot)
  save/                 WorldSave=save/      (sphereworld/spherechars/spheremultis/spheredata.scp created on first boot)
  logs/                 sphere<date>.log; boot1_* and boot2_* archived
```

Working directory for the process must be `runtime/` (all ini paths are relative).

## Revolution MUL files loaded

Copied from `local/revolution-client/` (full extraction kept intact, see `REVOLUTION_CLIENT_INVENTORY.md`):

| File | Size | SHA1 | Role |
|---|---|---|---|
| map0.mul | 89,915,392 | ffcd163e… | Felucca terrain, 7168×4096 (ML size) |
| staidx0.mul | 4,718,592 | 8ac31651… | statics index |
| statics0.mul | 21,971,754 | ebcd7cdc… | statics |
| tiledata.mul | 1,036,288 | 6decd36b… | tile flags (pre-HS format) — log: `Caching tiledata.mul...` |
| multi.idx | 98,184 | 9feb70f3… | multi index |
| multi.mul | 653,976 | 250b0c0f… | multis |
| verdata.mul | 7,194,036 | 6a354f80… | patch overlay (optional; loaded by `g_VerData.Load`) |

Log evidence: `Indexing the client files...` → `Caching tiledata.mul...` → `Initializing the world...` → `Allocated map sectors: map0=7168 map1=7168`. No `File '...' not found` fatal (`CServerConfig.cpp:5024-5028`).

Note: map 1 (Trammel) is served from `map0.mul` because `map1.mul` is absent (vanilla fallback `CUOInstall.cpp:408-409`). Maps 2–5 are disabled (no MULs).

## sphere.ini changes

Four lines (details + evidence in `M0_SPHERE_CONFIG.md`):

| Key | Before | After |
|---|---|---|
| AGREE | `//AGREE=1` | `AGREE=1` |
| MulFiles | `//MulFiles=mul/` | `MulFiles=mul/` |
| AccApp | `0` | `2` |
| UseNoCrypt | `0` | `1` |

Plus `runtime/scripts/spheretables.scp`: 21 `maps/map2..5/*` entries commented out.

## Listening address / port

`netstat -ano` while running: `TCP 0.0.0.0:2593 0.0.0.0:0 LISTENING <pid>`.
`ServIP=127.0.0.1`, `ServPort=2593` in ini; Source-X binds `INADDR_ANY` for local addresses (`src/network/CSocket.cpp:327-330`), so the socket is reachable on all interfaces, including `127.0.0.1:2593`. Port 2593 is used for both login and game (single-server `0x8C` relay to itself).

## Boot history

| Attempt | Result | Cause / action |
|---|---|---|
| 1 | FATAL `Failed to load server settings (code -3)` | `AGREE` unset (nightly gate `CServer.cpp:2768-2774`). Set `AGREE=1`. |
| 2 | `Startup complete (items=0, chars=0, accounts=0)`, listening | 1,606 `ERROR: Unsupported map #N … Auto-fixing that to 0` from Scripts-X map2–5 files (would plant off-map teleporters/regions on Felucca). Disabled those script entries in `spheretables.scp`. |
| 3 | `Startup complete`, listening, **0 script errors**, 734 scripts indexed | Accepted. Log: `runtime/logs/sphere2026-08-25.log` (792 lines). |

Sequence in boot #3: ini → `Indexing the client files` → `Caching tiledata.mul` → `spheretables.scp` → `Initializing the world` → `Indexing 734 script files` → `Loading scripts/...` ×734 → load `save/*` → GC → `Startup complete` → `Use '?' to view available console commands`.

## Warnings encountered (boot #3, all non-fatal)

- Nightly-build banner (`WARNING:` block) — informational.
- `'save/spherestatics.scp' not found` / `Can't Load save/spherestatics.scp` — fresh world, no statics save yet. Expected. (`sphereworld/chars/multis/data.scp` were created by boot #2's initial save and loaded fine in #3.)
- `(spawner_defs.scp,251/310/313/317/338/401/507/515) Replacing existing VarStr '…'` ×8 — Scripts-X defines these spawn-group names twice; vanilla Scripts-X state, harmless.
- `The server has no accounts. To create admin account use: ACCOUNT ADD [login] [password] / ACCOUNT [login] PLEVEL 7` — expected; `AccApp=2` will auto-create player accounts on first login; an admin (PLEVEL 7) account still needs the console command.

## Acceptance criteria

- [x] Source-X built — `build-x64/bin-x86_64/Nightly/SphereSvrX64_nightly.exe`, GIT hash matches checkout (built prior to this session, not rebuilt here)
- [x] Scripts-X present — `runtime/scripts` @ `27e78bc8`, `Indexing 734 script files`
- [x] Revolution data extracted without execution — 7-Zip static `x` of the RAR SFX; SHA1s match the client's own `updt1062.txt` manifest
- [x] required MUL files found — map0/statics0/staidx0/multi.idx/multi.mul/tiledata/hues all present
- [x] Sphere loads MUL data — `Caching tiledata.mul...`, `Allocated map sectors: map0=7168`
- [x] Sphere loads Scripts-X — 734 files, 0 errors
- [x] Sphere boots — `Startup complete (items=0, chars=0, accounts=0)`
- [x] server listens on localhost:2593 — `0.0.0.0:2593 LISTENING` (includes 127.0.0.1)

## Unresolved UNKNOWNs

1. **Revolution client protocol version.** `Revolution.exe` (2016-01-06) is a patched classic client; the exact version it reports (`0xBD`/`0xEF`) and its encryption key are UNKNOWN until a login is captured. Affects which `sphereCrypt.ini` key is used and which packet layouts Sphere emits to it.
2. **Trammel.** Sphere currently exposes map 1 as a mirror of `map0.mul`. Whether Revolution had Trammel (and whether `Map1` should be disabled) is UNKNOWN — Revolution-fidelity milestone.
3. **Revolution's own scripts.** This is vanilla Scripts-X; none of Revolution's mechanics are present. By design for M0.
4. **`0.0.0.0` bind.** Vanilla Source-X behaviour; if strict loopback-only is required it needs a firewall rule or engine change.
5. **`Revolution.ini`** is an HTML page, not a config — the real launcher config (server IP/port the original client used) was not found in the archive; irrelevant for our local server since we control the client's connection.
6. **Bot login path** (`UseNoCrypt`, `AccApp`) is configured but **not yet exercised** — that is the next milestone (`ARCHITECTURE_AUDIT.md` §7/§8).

## Not done (by instruction)

No uo-client changes, no bots, no Revolution mechanics, no Sphere combat/skill/era changes.
