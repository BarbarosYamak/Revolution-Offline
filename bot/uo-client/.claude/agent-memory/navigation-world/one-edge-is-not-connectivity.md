---
name: one-edge-is-not-connectivity
description: Navgrid pockets — a passable cell with one edge can still be a sealed 2-5 cell dead end; connectivity must be measured as component size, and cave interiors are z-band pockets over the mountain anchor
metadata:
  type: project
---

`RoutePlanner::Plan` snaps a start/goal off a disconnected cell, but the test
for "connected" must be **component size**, not "has an edge".

**Why:** Minoc Mine 1. Kharain mined at (2569,479,**z 0**, cave floor). His
navgrid cell (160,29) has its single anchor at (2566,474,**z 40**) — the
mountain top — with `edges == 0`. Its passable neighbours (159,27/28/29) and
(161,29) form a five-cell pocket wired only to each other; (159,28) and
(159,29) each have exactly one edge, to the other. The Papua-era
`SnapToConnected` accepted (159,28) as "connected", A* still had nowhere to
go, and 58 `[travel] no place offers banker` lines followed. Fixed 2026-09-06
with `RoutePlanner::ComponentReach` (bounded flood, cap
`kConnectedComponentCells` = 24; a cell touching a transit counts as connected).
Regression: `TestRealAtlasMinerInMinocMine` in tests/m9_service_selection.cpp.

**How to apply:**
- "no route" / "no place offers X" from inside a **cave, mine or upper storey**
  -> BFS the navgrid `edges` bits from that cell offline before touching the
  atlas or the service filter. The atlas is usually innocent.
- Expect cave interiors to be pockets: the coarse grid keeps one anchor per
  cell with no z-bands, so the character's real floor (z 0) and the cell's
  anchor (z 40+) are different places. See NavGrid.h KNOWN DEBT item 2.
- A pocket of size 1 was the Papua case; sizes 2-5 are the common case. Any new
  connectivity threshold must be > the largest pocket you have measured.
- Related: [[navgrid-island-cells]] (revolution-god memory, the Papua original).

Offline probe recipe: read `data/revolution_navgrid.bin` — header
`<8s magic="UONGRID2", u32 cellsX, u32 cellsY, u32 cellTiles, u32 reserved>`
then `cellsX*cellsY` records of `<u8 anchorOffX, u8 anchorOffY, i8 anchorZ,
u8 flags, u8 edges, u8 pad>`. Dir order is N,NE,E,SE,S,SW,W,NW (bit 0..7);
edges are stored on the SOURCE cell.
