---
name: identity-cpp-in-progress-2026-09-02
description: Another agent is adding kCraftMenus tailoring rows to Identity.cpp on 2026-09-02 -- do not touch that file until it lands
metadata:
  type: project
---

On 2026-09-02 another agent (working the Sphere/protocol side) verified the
tailoring cloth chain directly in Source-X `CClientTarg.cpp:2053-2085,
2186-2266` and is adding the corresponding `kCraftMenus` route rows to
`src/life/Identity.cpp` itself. They asked bot-core not to touch
`Identity.cpp` while that lands.

Verified chain (their finding, Source-X-grounded):
- dclick i_wool + target i_spinwheel -> i_yarn_ball x3. Wool never makes
  thread.
- dclick i_cotton + target i_spinwheel -> i_thread x6 (cotton chain, not the
  sheep chain).
- dclick yarn OR thread + target i_loom_upright, 4 uses -> 1 i_cloth_bolt.
- scissors dclick on bolt -> i_cloth.
- Spinwheel/loom dclick alone does nothing; production is target-on, not
  proximity-use.
- Sewing kit: dclick opens a target cursor; the TYPE of the targeted item
  picks the root menu (cloth vs leather).

Checked against `Runner::DoMakeCloth` (`src/life/Runner.cpp:13087`) same day:
it already implements this correctly -- `ActionUseItemOn(raw, wheel)`,
`ActionUseItemOn(spun, loom)`, `kYarnPerBolt = 4`, scissors-on-bolt, thread
fallback for the spun item. No bot-core code change was needed for this
finding; it only confirmed existing behavior.

**Why:** avoid a merge collision on Identity.cpp and avoid re-deriving a
chain that was just independently confirmed against source.

**How to apply:** before editing `src/life/Identity.cpp` or `kCraftMenus`,
confirm with the user/other agent that the tailoring rows have landed. If
DoMakeCloth's mechanics are ever questioned again, this file is already
verified -- point back here instead of re-reading CClientTarg.cpp.

Related: [[craft-route-is-not-the-recipe]].
