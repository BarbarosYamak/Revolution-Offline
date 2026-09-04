---
name: shard-ops-restart-and-saves
description: How to restart the shard, why .save misses statics, Scripts-X mirror dropped (runtime/scripts is truth), console script needs a window handle
metadata:
  type: project
---

- **runtime/scripts is the only script tree Sphere loads.** `server/Scripts-X` is a pristine upstream checkout for `git show` diffs only — owner agreed 2026-09-03 to drop the "mirror" habit (it had drifted 40+ files). `runtime/scripts` is its own git repo (branch `revolution-runtime`); commit there.
- **Restart:** `local/dev/sphere_console.ps1 -Commands "X#"` (save world+statics, exit). Process can linger after "Server terminated" → `Stop-Process -Force`. Relaunch with `Start-Process runtime/SphereSvrX64_nightly.exe -WorkingDirectory runtime`. New `revolution/*.scp` files and `sphere.ini` edits need this; edits to existing files need only `.resync`. When launched via Start-Process the console script may fail with "no main window handle" — use in-game `.serv.*` commands via an admin scenario instead.
- **Console `R` is a toggle:** first `R` = "PAUSED for Resync" (server idles, bots frozen), second `R` = reload + "Resync complete!". One `R` alone hangs the shard until you notice (2026-09-04, 5 min lost). Use `-Commands "R","R"` or check CPU is idle and send again. Console script needs `powershell -ExecutionPolicy Bypass`. `#` = world save (~0.3 s).
- runtime/scripts `origin` is upstream Sphereserver/Scripts-X (403 on push) — commits there are local only; no push target exists yet.
- **`.save` does not write statics.** `attr_static` items (e.g. the Jhelom teleporter 040005cdc) live in `spherestatics.scp`, written only by `.serv.savestatics` / console `##` / `X#`. Scenario `scripts/scenarios/save_statics.txt` does it. This is why the Jhelom fix "didn't stick" twice.
- Chars are in `sphereworld.scp` (not spherechars.scp). A pet's `memory_ipet` is an `i_memory` item with `CONT=<pet>` and the memory flags in `COLOR` (ipet = 0x2).
- Always `cp runtime/save/sphere*.scp` to a `save_backup_<date>_<tag>/` before any world-mutating admin scenario.

**Why:** three separate stalls this day came from these (mirror drift, statics not saved, hung process).
**How to apply:** cite instead of re-deriving; brief agents "runtime/scripts only".
