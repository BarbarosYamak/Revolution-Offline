# M1 — uo-client Baseline

Date: 2026-08-25. Phase 1 of M1: record the untouched state of `bot/uo-client` before any Sphere-compatibility work.

## Repository state

| Item | Value |
|---|---|
| Path | `C:\Projects\RevolutionOffline\bot\uo-client` |
| Remote | `https://github.com/xrip/uo-client.git` (origin) |
| Baseline commit | `2fb08c537ef379f4a4183aef65c1efadd4d28ebd` — 2026-06-01 "feat: code cleanup. readability" |
| Baseline branch | `master` |
| Working tree at baseline | **clean** (`git status --porcelain` empty) |
| Work branch created | `revolution-sphere-m1` (from `2fb08c53`) |

`server/Source-X` was not modified in M1 (`git -C server/Source-X status` remains clean; the only server-side change in the project is the M0 `sphere.ini`/`spheretables.scp` configuration documented in `M0_SPHERE_CONFIG.md`).

## Build

The upstream `scripts/build.bat` targets a toolchain that is not installed here — it calls
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat`
(VS 2022 Build Tools, 32-bit). This machine has **Visual Studio 18 (2026) Community**, the same
toolchain that built Source-X for M0. The upstream script was left untouched; M1 builds with an
explicit out-of-tree configuration instead.

### Dependencies

- **CMake** ≥ 3.20 — `…\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- **Ninja** — `…\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
- **MSVC** 19.51.36256.0 (toolset 14.51.36231), x64
- **quickjs-ng v0.15.0** — fetched at configure time by `FetchContent` (`CMakeLists.txt:19-30`). **Requires network access on first configure**; it is cached afterwards in `build-m1/_deps`.
- Win32 only in practice: `src/net/Socket.h` includes `<winsock2.h>` unconditionally and the client links `ws2_32 user32 gdi32`.

### Commands used (x64, Release, out-of-tree)

```bat
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

cmake -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -S C:\Projects\RevolutionOffline\bot\uo-client ^
  -B C:\Projects\RevolutionOffline\bot\uo-client\build-m1

cmake --build C:\Projects\RevolutionOffline\bot\uo-client\build-m1 --target uo_client
```

Baseline result: **build succeeded** at the untouched commit — `build-m1/uo_client.exe`, 1,385,984 bytes, linked from 57 targets (the client plus quickjs's own tools). Warnings are confined to the vendored `quickjs.c` (C4146/C4334/C4702/C4701) and MSVC's C4530 from `<xstring>`/`<chrono>` under `/EHs-c-`; none originate in uo-client sources.

`build-m1/` is a scratch build directory and is not intended for commit.

## Baseline behaviour relevant to M1

Recorded from the code at `2fb08c53` (details and line citations in `ARCHITECTURE_AUDIT.md` §3):

- Login chain implemented end to end: seed → `0x80` → `0xA8` → `0xA0` → `0x8C` → `0x91` → `0xB9` → `0xA9` → `0x5D` → `0x1B` → `0x55`.
- Hard-coded defaults in `src/main.cpp`: host `172.28.160.1:2593`, credentials `xrip/xrip`, MUL paths under `E:/uo/`, seed `0xAC1CA001`.
- Character slot hard-coded to 0 (`Client.cpp:680`); no character creation.
- No logout of any kind; the process ends only when the socket dies.
- Keepalive disabled by default, and when enabled it sent `0x09` (self-click) rather than `0x73`.
- Movement pipelined 4 deep, running cadence by default.
- An opcode with no entry in the 2.0.7 length table ended the process.

These are the items M1 addresses; each fix and its evidence is recorded in `M1_BOT_ALIVE.md`.
