#include "render/Renderer.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <unordered_set>

namespace uo::render {

namespace {

// Isometric tile geometry. A cell's diamond is 44 wide, 44 tall, stepping 22px
// per cell on each screen axis; z raises by 4px per unit.
constexpr int kHalfTile = 22;
constexpr int kTile     = 44;
constexpr int kZStep    = 4;

constexpr u16 kBackground = 0;   // black

// One draw command — land, static, item or mobile. All share one list, ordered
// exactly like the client (World_RenderEntities @0x401E90 + CDrawItem_AddToDrawList
// @0x403B50): cells back-to-front (depth, then column), and WITHIN a cell by
// drawCellZ ascending, with PRIORITY items inserted before non-priority on a
// z-tie. Verified in the IDB: the ONLY priority class is LAND (flat & stretched
// both return 1 from UsePrioritySortTieBreaker @vtable+0x1C); statics, items,
// mobiles, multis and corpses are all non-priority. So at equal z, land draws
// first (it is the ground) and everything else keeps insertion order via a
// stable sort (statics0.mul order: trunk under crown).
struct Draw {
    i32 depth;          // x + y  — far-to-near tile depth
    i32 col;            // x - y  — orders cells on the same diagonal
    i32 z;              // within-cell sort key = drawCellZ (the tile's BASE z).
                        // Land = north corner (flat) or (c0+c1+c2+c3)>>2 average
                        // (stretched) per LandObject_CreateForCell; static/item/mob
                        // = base z. NOT z+height: a tall wall must sort BELOW the
                        // higher floor it supports so the floor covers it (else the
                        // wall top pokes up through the floor).
    bool foliage;       // tiledata Foliage flag (0x20000) — tree/bush leaf canopy.
                        // The client links these into a SEPARATE list
                        // (pActiveDrawListHead, Entity_OnAddToWorld @0x4C3C30) drawn
                        // in a later pass so the canopy sits over its trunk. We sort
                        // it last WITHIN its cell (after depth/col) — same effect for
                        // the canopy/trunk pair, but the cell walk still lets a nearer
                        // trunk occlude a farther canopy.
    bool priority;      // client UsePrioritySortTieBreaker: true only for land
    bool surface;       // floor/bridge static that can be visually occluded by walls
    bool occluder;      // wall/blocking static with vertical volume
    int height;         // tiledata height. On an exact z-TIE the SHORTER tile draws
                        // first and the taller one on top: a table (z20/h6) or a
                        // wall-top (z30/h3) sits over the floor (h0) it shares the
                        // cell+z with, instead of the floor bleeding over them.
    bool quad;          // true: stretched/texmap quad; false: flat blit
    const u16* src;     // source pixels (art sprite or texmap)
    int sw, sh;         // source dimensions
    bool transparent;   // skip 0 texels (art) vs draw all (texmap)
    int dx, dy;                   // flat-blit top-left (when !quad)
    Renderer::TexVert N, E, S, W; // quad corners (when quad)
};

}  // namespace

Renderer::Renderer(int width, int height)
    : w_(width), h_(height), fb_(static_cast<usize>(width) * height, kBackground) {}

void Renderer::RenderWorld(map::Map& map, art::ArtLoader& art,
                           const tiledata::TileDataLoader& td, texmap::TexmapLoader& tex,
                           i32 camX, i32 camY, i32 camZ,
                           const DynItem* items, usize nItems,
                           anim::AnimLoader* anim,
                           const Mob* mobs, usize nMobs) {
    std::fill(fb_.begin(), fb_.end(), kBackground);

    // Cells whose screen position can land on-screen. The margin pads for tall
    // statics anchored below the window and for elevation offsets.
    const int R = (w_ + h_) / kTile + 48;

    const i32 wc = static_cast<i32>(map.WidthCells());
    const i32 hc = static_cast<i32>(map.HeightCells());
    const i32 minX = std::max<i32>(0, camX - R), maxX = std::min<i32>(wc - 1, camX + R);
    const i32 minY = std::max<i32>(0, camY - R), maxY = std::min<i32>(hc - 1, camY + R);
    if (minX > maxX || minY > maxY) return;

    const u32 bx0 = static_cast<u32>(minX) / 8, bx1 = static_cast<u32>(maxX) / 8;
    const u32 by0 = static_cast<u32>(minY) / 8, by1 = static_cast<u32>(maxY) / 8;

    const int originX = w_ / 2;
    // Centre on the player INCLUDING his z. (sx,sy) is a cell's NORTH vertex,
    // but the player stands at the cell CENTRE (kHalfTile below the vertex), so
    // we centre the cell centre of (camX,camY,camZ): +camZ*kZStep accounts for
    // his elevation, -kHalfTile for the vertex->centre offset. Without this the
    // z=0 vertex is centred and an elevated player sits high with extra tiles
    // drawn below him.
    const int originY = h_ / 2 + camZ * kZStep - kHalfTile;

    std::vector<Draw> draws;
    std::vector<map::StaticItem> statics(2048);

    // Roof/floor cutoff (Visibility_ComputeFogDistance @0x404A00 +
    // RoofVisibility_FloodFillConnectedRoofs @0x404900). When the player is
    // under a roof the client hides the WHOLE connected roof, not just the
    // tiles right above his head: it flood-fills the connected roof and takes
    // its MINIMUM z as the ceiling, then culls everything at/above that z
    // (WorldObject_IsVisible @0x404CA0). 127/none => draw everything (outdoors).
    //
    // Roof/canopy tiles (thatch, shingles, tent) carry the Roof flag
    // (0x10000000 in this client era; see tiledata.h). Upper-floor surfaces
    // carry the Surface flag (0x200).
    constexpr u32 kRoofOverhead = tiledata::kFlagRoof;
    int ceilingZ = INT_MAX;
    const Mob* player = nullptr;
    for (usize i = 0; i < nMobs; ++i)
        if (mobs[i].isPlayer) { player = &mobs[i]; break; }
    if (player) {
        const int pz = player->z;
        std::vector<map::StaticItem> rbuf(2048);

        // Lowest roof tile (bit 0x10000000) in a cell; if ref != INT_MIN, only
        // tiles within 6 z of it count (so the fill follows one sloped roof and
        // won't jump to a taller neighbouring structure). INT_MIN = none.
        auto roofZAt = [&](i32 cx, i32 cy, int ref) -> int {
            if (cx < 0 || cy < 0) return INT_MIN;
            u32 nn = 0;
            if (!map.ReadStatics(static_cast<u32>(cx) / 8, static_cast<u32>(cy) / 8,
                                 rbuf.data(), static_cast<u32>(rbuf.size()), &nn))
                return INT_MIN;
            const u8 lx = static_cast<u8>(cx & 7), ly = static_cast<u8>(cy & 7);
            int best = INT_MIN;
            for (u32 i = 0; i < nn; ++i) {
                const map::StaticItem& s = rbuf[i];
                if (s.cellX != lx || s.cellY != ly) continue;
                if (!(td.Static(s.itemId).flags & kRoofOverhead)) continue;
                if (ref != INT_MIN && std::abs(static_cast<int>(s.z) - ref) > 6) continue;
                if (best == INT_MIN || s.z < best) best = s.z;
            }
            return best;
        };

        // Flood-fill the connected roof (4-neighbour, each step within 6 z),
        // seeded from the player's own + SE cell, accumulating the minimum z.
        struct Cell { i32 x, y; int ref; };
        std::vector<Cell> stack;
        std::unordered_set<u64> visited;
        auto seed = [&](i32 cx, i32 cy) {
            const int rz = roofZAt(cx, cy, INT_MIN);
            if (rz != INT_MIN && rz > pz + 14) stack.push_back({cx, cy, INT_MIN});
        };
        seed(player->x, player->y);
        seed(player->x + 1, player->y + 1);

        int minRoof = INT_MAX;
        usize guard = 0;
        while (!stack.empty() && guard++ < 16384u) {
            const Cell c = stack.back();
            stack.pop_back();
            const u64 key = (static_cast<u64>(static_cast<u32>(c.x)) << 32) |
                            static_cast<u32>(c.y);
            if (!visited.insert(key).second) continue;
            const int rz = roofZAt(c.x, c.y, c.ref);
            if (rz == INT_MIN) continue;
            if (rz < minRoof) minRoof = rz;
            stack.push_back({c.x - 1, c.y, rz});
            stack.push_back({c.x + 1, c.y, rz});
            stack.push_back({c.x, c.y - 1, rz});
            stack.push_back({c.x, c.y + 1, rz});
        }
        if (minRoof != INT_MAX) ceilingZ = std::max(minRoof, pz + 16);

        // Plain upper floor directly overhead (multi-storey): lowest Surface
        // tile in the player's own cell above his head lowers the ceiling too.
        u32 n = 0;
        if (map.ReadStatics(static_cast<u32>(player->x) / 8, static_cast<u32>(player->y) / 8,
                            statics.data(), static_cast<u32>(statics.size()), &n)) {
            const u8 lx = static_cast<u8>(player->x & 7), ly = static_cast<u8>(player->y & 7);
            for (u32 i = 0; i < n; ++i) {
                const map::StaticItem& s = statics[i];
                if (s.cellX != lx || s.cellY != ly) continue;
                if (s.z <= pz + 14) continue;
                if ((td.Static(s.itemId).flags & tiledata::kFlagSurface) &&
                    std::max(static_cast<int>(s.z), pz + 16) < ceilingZ)
                    ceilingZ = std::max(static_cast<int>(s.z), pz + 16);
            }
        }

        // Some interiors sit below the terrain land tile: the floor is a
        // static at negative z while the raw land at the same x/y remains
        // above it. Treat that terrain as the overhead cut plane too, or the
        // land is drawn over the room.
        map::LandCell ownLand{};
        if (map.ReadCell(static_cast<u32>(player->x), static_cast<u32>(player->y), &ownLand) &&
            ownLand.z > pz + 14 && pz + 16 < ceilingZ) {
            ceilingZ = pz + 16;
        }
    }
    auto culled = [&](int z) { return z >= ceilingZ; };

    // Vertical reach of z offsets, for the per-tile cull (z is a signed byte).
    const int kZPad = 128 * kZStep;

    // Per-frame heightmap over the visible range + a 2-cell margin (the
    // land-stretch model needs the +1 corner neighbors, and the official's
    // flat-vs-texmap test scans a small neighborhood). One block read per
    // covering block; lookups are then O(1).
    const i32 gx0 = std::max<i32>(0, minX - 2), gx1 = std::min<i32>(wc - 1, maxX + 2);
    const i32 gy0 = std::max<i32>(0, minY - 2), gy1 = std::min<i32>(hc - 1, maxY + 2);
    const int gw = gx1 - gx0 + 1, gh = gy1 - gy0 + 1;
    std::vector<i8> zmap(static_cast<usize>(gw) * gh, 0);
    {
        map::MapBlock zb;
        for (u32 by = static_cast<u32>(gy0)/8; by <= static_cast<u32>(gy1)/8; ++by) {
            for (u32 bx = static_cast<u32>(gx0)/8; bx <= static_cast<u32>(gx1)/8; ++bx) {
                if (!map.ReadBlock(bx, by, &zb)) continue;
                for (int cy = 0; cy < 8; ++cy) {
                    for (int cx = 0; cx < 8; ++cx) {
                        const i32 wx = static_cast<i32>(bx)*8 + cx;
                        const i32 wy = static_cast<i32>(by)*8 + cy;
                        if (wx < gx0 || wx > gx1 || wy < gy0 || wy > gy1) continue;
                        zmap[static_cast<usize>(wy - gy0) * gw + (wx - gx0)] = zb.cells[cy*8 + cx].z;
                    }
                }
            }
        }
    }
    auto zAt = [&](i32 x, i32 y) -> int {
        if (x < gx0 || x > gx1 || y < gy0 || y > gy1) return 0;
        return zmap[static_cast<usize>(y - gy0) * gw + (x - gx0)];
    };

    for (u32 by = by0; by <= by1; ++by) {
        for (u32 bx = bx0; bx <= bx1; ++bx) {
            // --- Land (drawn immediately; always under statics) ------------
            map::MapBlock mb;
            if (map.ReadBlock(bx, by, &mb)) {
                for (int cy = 0; cy < 8; ++cy) {
                    for (int cx = 0; cx < 8; ++cx) {
                        const i32 wx = static_cast<i32>(bx) * 8 + cx;
                        const i32 wy = static_cast<i32>(by) * 8 + cy;
                        const map::LandCell& c = mb.cells[cy * 8 + cx];
                        const art::Sprite* sp = art.Land(c.tileId);
                        if (!sp) continue;

                        const i32 dxw = wx - camX, dyw = wy - camY;
                        const int sx = originX + (dxw - dyw) * kHalfTile;
                        const int sy = originY + (dxw + dyw) * kHalfTile;
                        if (sx + kHalfTile < 0 || sx - kHalfTile >= w_) continue;
                        if (sy + kTile + kZPad < 0 || sy - kZPad >= h_) continue;

                        // Four corner z's: own cell (north), +x (east),
                        // +x+y (south), +y (west) — the UO land-stretch model.
                        const int z0 = c.z;             // N
                        const int z1 = zAt(wx + 1, wy);     // E
                        const int z2 = zAt(wx + 1, wy + 1); // S
                        const int z3 = zAt(wx,     wy + 1); // W

                        // Faithful to the client's land factory (sub_4B74E0 @0x4B74E0):
                        //   textureId == 0                         -> 44x44 art
                        //   textured, corners equal AND neighborhood
                        //     uniform in z                          -> 44x44 art
                        //   otherwise                               -> stretched texmap
                        // The neighborhood test is why a flat water tile *beside*
                        // the sloped shore is texmapped (wavy) — smoothing the coast.
                        const texmap::Texture* t =
                            tex.Get(td.Land(c.tileId).textureId);
                        bool useArt = (z0 == z1 && z1 == z2 && z2 == z3);
                        if (useArt && t) {
                            for (i32 ny = wy - 1; ny <= wy + 2 && useArt; ++ny)
                                for (i32 nx = wx - 1; nx <= wx + 2 && useArt; ++nx)
                                    if (zAt(nx, ny) != z0) useArt = false;
                        }
                        // drawCellZ (LandObject_CreateForCell @0x4B74E0): flat tile
                        // sorts by its north-corner z; a stretched tile sorts by the
                        // average of its four corners ((c0+c1+c2+c3)>>2). Using the
                        // north corner for stretched tiles mis-sorts sloped land vs.
                        // its neighbours — the cause of the saw-tooth coastline.
                        const int avgZ = (z0 + z1 + z2 + z3) >> 2;
                        if (culled(avgZ)) continue;
                        const i32 depth = wx + wy, col = wx - wy;
                        Draw d{};
                        d.depth = depth; d.col = col; d.z = z0; d.priority = true;
                        if (!t || useArt) {
                            if (z0 == z1 && z1 == z2 && z2 == z3) {
                                d.quad = false;
                                d.src = sp->px.data(); d.sw = sp->width; d.sh = sp->height;
                                d.transparent = true;
                                d.dx = sx - kHalfTile; d.dy = sy - z0 * kZStep;
                            } else {
                                // Textureless sloped tile: stretch the art so it
                                // still meets its neighbors (avoids a crack).
                                d.z = avgZ;
                                d.quad = true;
                                d.src = sp->px.data(); d.sw = sp->width; d.sh = sp->height;
                                d.transparent = true;
                                d.N = {sx,             sy            - z0 * kZStep, 22,  0};
                                d.E = {sx + kHalfTile, sy + kHalfTile - z1 * kZStep, 43, 22};
                                d.S = {sx,             sy + kTile     - z2 * kZStep, 22, 43};
                                d.W = {sx - kHalfTile, sy + kHalfTile - z3 * kZStep,  0, 22};
                            }
                        } else {
                            const int hi = t->size - 1;
                            d.z = avgZ;
                            d.quad = true;
                            d.src = t->px.data(); d.sw = t->size; d.sh = t->size;
                            d.transparent = false;
                            d.N = {sx,             sy            - z0 * kZStep, 0,  0 };
                            d.E = {sx + kHalfTile, sy + kHalfTile - z1 * kZStep, hi, 0 };
                            d.S = {sx,             sy + kTile     - z2 * kZStep, hi, hi};
                            d.W = {sx - kHalfTile, sy + kHalfTile - z3 * kZStep, 0,  hi};
                        }
                        draws.push_back(d);
                    }
                }
            }

            // --- Statics (deferred to the painter's pass) ------------------
            u32 n = 0;
            if (map.ReadStatics(bx, by, statics.data(),
                                static_cast<u32>(statics.size()), &n)) {
                for (u32 i = 0; i < n; ++i) {
                    const map::StaticItem& s = statics[i];
                    if (culled(s.z)) continue;   // hidden by the roof cutoff
                    const i32 wx = static_cast<i32>(bx) * 8 + s.cellX;
                    const i32 wy = static_cast<i32>(by) * 8 + s.cellY;
                    const art::Sprite* sp = art.Static(s.itemId);
                    if (!sp) continue;
                    const tiledata::StaticTile& stt = td.Static(s.itemId);
                    const i32 dxw = wx - camX, dyw = wy - camY;
                    const int sx = originX + (dxw - dyw) * kHalfTile;
                    const int sy = originY + (dxw + dyw) * kHalfTile - s.z * kZStep;
                    Draw d{};
                    d.depth = wx + wy; d.col = wx - wy;
                    d.z = s.z; d.priority = false; d.height = stt.height;
                    d.foliage = (stt.flags & tiledata::kFlagFoliage) != 0;
                    d.surface = (stt.flags & (tiledata::kFlagSurface | tiledata::kFlagBridge)) != 0;
                    d.occluder = !d.surface && (stt.flags & (tiledata::kFlagWall |
                                   tiledata::kFlagImpassable | tiledata::kFlagWindow)) != 0;
                    d.quad = false;
                    d.src = sp->px.data(); d.sw = sp->width; d.sh = sp->height;
                    d.transparent = true;
                    d.dx = sx - sp->width / 2;
                    d.dy = sy + kTile - sp->height;   // bottom-anchored
                    draws.push_back(d);
                }
            }
        }
    }

    // Dynamic server items (lamp posts, doors, ...) — drawn as static art,
    // interleaved into the same painter's order as map statics. The drawn
    // graphic is itemId + gfxOffset: a door stores a base graphic plus a
    // graphic-increment that selects its open/closed/hinge frame (sub_405290).
    for (usize ii = 0; ii < nItems; ++ii) {
        const DynItem& it = items[ii];
        if (culled(it.z)) continue;
        const u16 gid = static_cast<u16>(it.itemId + it.gfxOffset);
        const art::Sprite* sp = art.Static(gid);
        if (!sp) continue;
        const tiledata::StaticTile& stt = td.Static(gid);
        const i32 dxw = it.x - camX, dyw = it.y - camY;
        const int sx = originX + (dxw - dyw) * kHalfTile;
        const int sy = originY + (dxw + dyw) * kHalfTile - it.z * kZStep;
        Draw d{};
        d.depth = it.x + it.y; d.col = it.x - it.y;
        d.z = it.z; d.priority = false; d.height = stt.height;
        d.foliage = (stt.flags & tiledata::kFlagFoliage) != 0;
        d.surface = (stt.flags & (tiledata::kFlagSurface | tiledata::kFlagBridge)) != 0;
        d.occluder = !d.surface && (stt.flags & (tiledata::kFlagWall |
                     tiledata::kFlagImpassable | tiledata::kFlagWindow)) != 0;
        d.quad = false;
        d.src = sp->px.data(); d.sw = sp->width; d.sh = sp->height;
        d.transparent = true;
        d.dx = sx - sp->width / 2;
        d.dy = sy + kTile - sp->height;
        draws.push_back(d);
    }

    // Mobiles (players/NPCs) — a single still body frame, no animation yet.
    // Interleaved by z with statics, like the client's draw buckets. The
    // 64x128 anim canvas anchors at (kAnchorX, kAnchorY); we place that anchor
    // at the tile floor (cell centre). Equipment/mounts/hue are out of scope.
    if (anim) {
        for (usize mi = 0; mi < nMobs; ++mi) {
            const Mob& m = mobs[mi];
            if (culled(m.z)) continue;
            const anim::Frame* fr = anim->Body(m.body, m.dir);
            if (!fr) continue;
            const i32 dxw = m.x - camX, dyw = m.y - camY;
            const int sx = originX + (dxw - dyw) * kHalfTile;
            const int sy = originY + (dxw + dyw) * kHalfTile - m.z * kZStep;
            Draw d{};
            d.depth = m.x + m.y; d.col = m.x - m.y; d.z = m.z; d.priority = false;
            d.quad = false;
            d.src = fr->px.data(); d.sw = anim::Frame::kW; d.sh = anim::Frame::kH;
            d.transparent = true;
            d.dx = sx - anim::Frame::kAnchorX;
            d.dy = sy + kHalfTile - anim::Frame::kAnchorY;  // feet at cell centre
            draws.push_back(d);
        }
    }

    // Order like the client. World_RenderEntities @0x401E90 walks the visible
    // cells back-to-front (depth = x+y, then column = x-y); within a cell
    // CDrawItem_AddToDrawList @0x403B50 keeps the bucket sorted by drawCellZ (the
    // tile's BASE z) ascending, with a PRIORITY item inserted ahead of a
    // non-priority one on a z-tie. Verified in the IDB: land (flat & stretched) is
    // the ONLY priority class — so land draws first on a z-tie (it is the ground).
    // Statics sort by base z so a tall wall stays BELOW the higher floor it
    // supports (a wooden battlement floor covers the sandstone wall beneath it);
    // sorting by the tile TOP instead made tall walls poke up through the floor.
    // On an exact z-tie the SHORTER tile draws first and the taller one on top
    // (by tiledata height): a table (z20/h6) or a wall-top (z30/h3) draws over the
    // floor (h0) it shares a cell+z with, instead of the floor bleeding over them.
    // Stretched land uses its average-corner z (LandObject_CreateForCell) for a
    // smooth coast. Stable so equal-key statics keep statics0.mul order.
    std::stable_sort(draws.begin(), draws.end(), [](const Draw& a, const Draw& b) {
        if (a.depth != b.depth) return a.depth < b.depth;
        if (a.col   != b.col)   return a.col   < b.col;
        // Foliage (tree/bush canopies) draws after everything else IN ITS CELL,
        // like the client's separate pActiveDrawListHead pass (Entity_OnAddToWorld
        // @0x4C3C30, gated on the Foliage flag 0x20000) — so the leaf crown sits
        // over its own trunk. Kept per-cell (not global) so the back-to-front cell
        // walk still lets a nearer trunk occlude a farther canopy.
        if (a.foliage != b.foliage) return a.foliage < b.foliage;
        auto surfaceInsideOccluder = [](const Draw& floor, const Draw& wall) {
            return floor.surface && wall.occluder && wall.height > 0 &&
                   floor.z > wall.z && floor.z < wall.z + wall.height;
        };
        if (surfaceInsideOccluder(a, b)) return true;
        if (surfaceInsideOccluder(b, a)) return false;
        if (a.z     != b.z)     return a.z     < b.z;       // base drawCellZ
        if (a.priority != b.priority) return a.priority > b.priority;  // land under objects
        if (a.height != b.height) return a.height < b.height;          // shorter under taller
        return false;                                       // else: insertion order
    });

    for (const Draw& d : draws) {
        if (d.quad) {
            TexTri(d.src, d.sw, d.sh, d.transparent, d.N, d.E, d.S);
            TexTri(d.src, d.sw, d.sh, d.transparent, d.N, d.S, d.W);
        } else {
            BlitRaw(d.src, d.sw, d.sh, d.dx, d.dy, d.transparent);
        }
    }
}

void Renderer::Overlay(const u16* src, int sw, int sh, int dx, int dy) {
    if (!src || dx >= w_ || dy >= h_ || dx + sw <= 0 || dy + sh <= 0) return;
    for (int row = 0; row < sh; ++row) {
        const int py = dy + row;
        if (py < 0 || py >= h_) continue;
        const u16* srow = &src[static_cast<usize>(row) * sw];
        u16* drow = &fb_[static_cast<usize>(py) * w_];
        for (int col = 0; col < sw; ++col) {
            const int px = dx + col;
            if (px < 0 || px >= w_) continue;
            drow[px] = srow[col];
        }
    }
}

void Renderer::BlitSpriteKeyed(const u16* src, int sw, int sh, int dx, int dy, u16 key) {
    if (!src || dx >= w_ || dy >= h_ || dx + sw <= 0 || dy + sh <= 0) return;
    for (int row = 0; row < sh; ++row) {
        const int py = dy + row;
        if (py < 0 || py >= h_) continue;
        const u16* srow = &src[static_cast<usize>(row) * sw];
        u16* drow = &fb_[static_cast<usize>(py) * w_];
        for (int col = 0; col < sw; ++col) {
            const u16 p = srow[col];
            if (!p || p == key) continue;
            const int px = dx + col;
            if (px < 0 || px >= w_) continue;
            drow[px] = p;
        }
    }
}

void Renderer::ScreenToWorld(int sx, int sy, i32 camX, i32 camY,
                             i32* outX, i32* outY) const {
    // RenderWorld places the player's cell centre (feet) at the screen centre
    // (w/2, h/2): the +camZ*kZStep in originY and the per-tile -z*kZStep offset
    // cancel for the player's own elevation. A cell at (camX+dx, camY+dy) on the
    // same floor lands at screen (w/2 + (dx-dy)*kHalfTile, h/2 + (dx+dy)*kHalfTile).
    // Invert that:
    const double dsx = sx - w_ / 2.0;
    const double dsy = sy - h_ / 2.0;
    const double dx = (dsx + dsy) / (2.0 * kHalfTile);   // (dx-dy)+(dx+dy) = 2dx
    const double dy = (dsy - dsx) / (2.0 * kHalfTile);   // (dx+dy)-(dx-dy) = 2dy
    if (outX) *outX = camX + static_cast<i32>(std::lround(dx));
    if (outY) *outY = camY + static_cast<i32>(std::lround(dy));
}

void Renderer::Blit(const art::Sprite& s, int dx, int dy) {
    BlitRaw(s.px.data(), s.width, s.height, dx, dy, true);
}

void Renderer::BlitRaw(const u16* src, int sw, int sh, int dx, int dy, bool skipTransparent) {
    if (dx >= w_ || dy >= h_ || dx + sw <= 0 || dy + sh <= 0) return;

    for (int row = 0; row < sh; ++row) {
        const int py = dy + row;
        if (py < 0 || py >= h_) continue;
        const u16* srow = &src[static_cast<usize>(row) * sw];
        u16* drow = &fb_[static_cast<usize>(py) * w_];
        for (int col = 0; col < sw; ++col) {
            const u16 p = srow[col];
            if (!p && skipTransparent) continue;
            const int px = dx + col;
            if (px < 0 || px >= w_) continue;
            drow[px] = p;
        }
    }
}

void Renderer::TexTri(const u16* src, int texW, int texH, bool skipTransparent,
                      TexVert a, TexVert b, TexVert c) {
    const int area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area == 0) return;

    int minx = std::max(0,      std::min(a.x, std::min(b.x, c.x)));
    int maxx = std::min(w_ - 1, std::max(a.x, std::max(b.x, c.x)));
    int miny = std::max(0,      std::min(a.y, std::min(b.y, c.y)));
    int maxy = std::min(h_ - 1, std::max(a.y, std::max(b.y, c.y)));
    if (minx > maxx || miny > maxy) return;

    const float inv = 1.0f / static_cast<float>(area);
    const int maxU = texW - 1, maxV = texH - 1;

    for (int py = miny; py <= maxy; ++py) {
        u16* drow = &fb_[static_cast<usize>(py) * w_];
        for (int px = minx; px <= maxx; ++px) {
            // Edge functions: w0 opposite a (edge b->c), etc.
            const int w0 = (c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x);
            const int w1 = (a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x);
            const int w2 = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
            if (area > 0) { if (w0 < 0 || w1 < 0 || w2 < 0) continue; }
            else          { if (w0 > 0 || w1 > 0 || w2 > 0) continue; }

            int u = static_cast<int>((w0 * a.u + w1 * b.u + w2 * c.u) * inv + 0.5f);
            int v = static_cast<int>((w0 * a.v + w1 * b.v + w2 * c.v) * inv + 0.5f);
            u = u < 0 ? 0 : (u > maxU ? maxU : u);
            v = v < 0 ? 0 : (v > maxV ? maxV : v);

            const u16 p = src[static_cast<usize>(v) * texW + u];
            if (p || !skipTransparent) drow[px] = p;
        }
    }
}

}
