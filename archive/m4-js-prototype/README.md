# M4 JS prototype (archived)

Built 2026-09-29 on the old `main` client snapshot, before the owner chose
`revolution-sphere-m1` (now `bot/uo-client/`) as the project's bot client.
Kept here so nothing is lost; **not built, not used**.

What it contains, and where the idea now belongs:

| Here | Idea | Live home |
|---|---|---|
| `scripts/js/lib/archetypes.js`, `data/revolution_archetypes.tsv` | 21 archetypes from the Build Compendium with evidence classes | the C++ profession table in `bot/uo-client` |
| `scripts/js/lib/{fighter,crafterbot,gathering,crafting}.js` | fighter styles, crafters, orders (piece/quantity/set) | `src/life/` runner + activities |
| `scripts/js/lib/{economy,memory,training,life}.js` | loot/sell/ledger, danger memory, skill locks toward target build, persistence | `src/life/` (memory, planner, market) |
| `src/lifecycle/*`, `include/uo/lifecycle.h`, `tests/m4_lifecycle.cpp` | `.life` record with two-clock handling | `src/life/` state persistence |
| `tests/js_bot_smoke.js` | 342-check Node harness | — |

Design notes for each are in `docs/M4_1_PERSISTENCE.md` … `docs/M4_6_LIFE_AND_TRAINING.md`.
Full history: the commits on `main` up to `2c0b3e7` and `8b883e2`.
