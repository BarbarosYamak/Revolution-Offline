---
name: vcvars-and-pathprobe
description: Correct vcvars path for building diagnostic tools locally (path_probe.exe); build_pathprobe.bat's hardcoded path is stale.
metadata:
  type: feedback
---

`scripts/build_pathprobe.bat` (bot/uo-client) hardcodes
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\...\vcvars32.bat`,
which does not exist on this machine. The real toolchain, per
`tools/rev.py`'s own `VCVARS` constant, is
`C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`
(64-bit, VS "18" Community).

**Why:** discovered while diagnosing wave-2 nav clusters
(2026-09-01/02) -- needed `path_probe.exe` to inspect real map/statics at
a stuck character's coordinates, and the checked-in batch script's cl
invocation silently produced no output when run through the Bash tool
(cmd.exe launched directly from this Bash tool also swallows output for
unclear reasons -- go through `python -c "subprocess.call('cmd /c ...')"`
like `tools/rev.py` does instead of raw `cmd.exe /c`).

**How to apply:** to build any standalone dev tool under `tests/` (e.g.
`path_probe.cpp`) outside the CMake/ninja build, invoke `cl` directly via
Python's `subprocess.call('cmd /c "<vcvars64.bat> >nul && <cl command>"')`,
using vcvars64 (64-bit) and forward slashes in paths (backslashes break
inside a Python heredoc). Set `UO_MUL_DIR` to
`C:/Projects/RevolutionOffline/runtime/mul` (the dir the run_gates fleet
actually uses) when running it. `path_probe.exe` is a diagnostic, not a
ctest target -- it is intentionally NOT in `add_test()`, per
`tests/CMakeLists.txt`'s own comment.
