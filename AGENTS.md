# Repository Guidelines

## Project Structure & Module Organization

This is a C++17 CMake project for an Ultima Online client and tooling. Core code lives in `src/`: `net` handles sockets, packets, and Huffman; `mul` loads MUL/verdata assets; `render` draws through MiniFB; `bot` contains pathfinding/blacklist logic; `builders` creates packet payloads. Public headers are under `include/uo`, with MiniFB in `include/win32`. Probes live in `tests/`. Generated outputs belong in `build/` or `cmake-build-*`.

## Client Runtime Notes

`Client` owns protocol state, packet dispatch, movement, bot commands, renderer ticks, and cached objects. Bot movement is depth-1: send one `0x02` move, predict position, then reconcile on `0x22` ack or snap back on `0x21` reject. `bot::FindPath` runs A* over `World::QueryCell`; `blacklist_`, cached mobiles, and dynamic `0x1A` items add obstacles. Lookahead patches short prefixes of `botPath_`; full `BotReplanToGoal()` is the fallback.

## Build, Test, and Development Commands

- `scripts\build.bat`: configures Ninja with Visual Studio Build Tools and builds all CMake targets.
- `cmake -S . -B build -G Ninja` then `cmake --build build`: manual configure/build when the compiler environment is active.
- `scripts\build_hufftest.bat`: builds and runs the Huffman round-trip test.
- `scripts\build_bltest.bat`: builds and runs the blacklist round-trip test.
- `scripts\build_pathprobe.bat [args]`: builds and runs the pathfinding probe.
- `scripts\build_viewer.bat [args]`: builds and runs the world viewer probe.
- `scripts\render_regression.bat`: Windows-only renderer regression harness; dumps PNG scenes to `build\regression\` for visual comparison with the official 2.0.7 client. Do not use Bash/WSL here.

## Coding Style & Naming Conventions

Use C++17 unless a probe requires newer syntax. Keep code exception-free and RTTI-free to match CMake flags. Follow the existing style: 4-space indentation, same-line braces, `uo` namespace, PascalCase classes, and lowerCamelCase fields such as `loginHost`. Prefer aliases from `include/uo/types.h` for protocol/file data.

## Testing Guidelines

Place focused probes in `tests/` with behavior-oriented names, such as `huffman_roundtrip.cpp` or `path_probe.cpp`. For pathfinding changes, run `scripts\build_pathprobe.bat sx sy sz gx gy [margin]` on a real MUL route. New coverage should get a script under `scripts/` or a CMake/CTest target.

For renderer changes, run `scripts\render_regression.bat` and inspect all generated PNGs, especially `07_negz_interior.png` for negative-Z interiors. The harness uses `scripts\build_viewer.bat` and writes only under `build\regression\`.

## Commit & Pull Request Guidelines

Commits use concise, imperative, scope-prefixed messages, for example `bot: add path lookahead rerouting`. Keep commits focused and use scopes like `net:`, `mul:`, `render:`, `bot:`, or `tests:`. PRs should describe behavior, list commands, mention `*.mul` assets, and include screenshots for renderer-visible changes.

## Security & Configuration Tips

Do not commit credentials, shard-specific secrets, or private asset paths. `src/main.cpp` currently contains local defaults for host, login, and MUL asset locations; prefer command-line overrides or local-only edits when testing against private environments.
