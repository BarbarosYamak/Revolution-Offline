---
name: worktree-build-notes
description: Building and testing inside an agent worktree - rev.py quirks, ctest needs -C Debug, and which tests fail there for missing generated data
metadata:
  type: project
---

Working inside `bot/uo-client/.claude/worktrees/agent-*`:

- A fresh agent worktree may be checked out at the OLD upstream `master`
  (the xrip/uo-client tree, no `src/life` at all). Check `git log -1` and
  `git worktree list`; the project branch is `revolution-sphere-m1`.
- `python tools/rev.py reconfigure` in a worktree produces a **multi-config
  Visual Studio generator** (vswhere.exe is not on PATH, so the Ninja probe
  fails). `rev.py test` then reports every test "Not Run -- Missing
  -C <config>". Run `ctest --test-dir build-m1 -C Debug` instead, from a shell
  that has called vcvars64.bat.
- `rev.py build` prints "uo_client.exe unchanged (1970-01-01)" after a
  successful build, because it looks for `build-m1/uo_client.exe` while the
  VS generator writes `build-m1/Debug/uo_client.exe`. Judge the build by the
  MSBuild output, not by that line.
- Two ctest targets fail in a worktree for reasons that are NOT code:
  `m9_service_selection` (wants the generated navgrid, which lives only in the
  main tree's `data/`) and `m4_economy_invariant` (wants a data path argument).
  `m4_life` had 2 pre-existing failures at bad0bc3 ("away from the trees the
  need is the ordinary 0.40", "ore is ore until the forge says which ingot").
  Establish the baseline with `git stash` before blaming your own change.

**How to apply:** run the baseline before reporting any test failure from a
worktree, and never report the three above as regressions without checking.
