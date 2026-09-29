#include "uo/world.h"

#include <algorithm>

namespace uo::world {

namespace td = tiledata;

constexpr i32 kMaxLandStepUp = 2;

// A static is a standing surface if it has Surface or Bridge and is not
// Impassable. The classic client treats Impassable+Surface as blocker-only:
// it sets the blocker bit from 0x40 and does not add surface bits.
bool World::StaticSurfaceTop(u16 itemId, i8 baseZ, i8* topOut) const {
    const auto& s = td_.Static(itemId);
    if (s.flags & td::kFlagImpassable) return false;
    const bool isSurface = (s.flags & (td::kFlagSurface | td::kFlagBridge)) != 0;
    if (!isSurface) return false;
    if (topOut) *topOut = static_cast<i8>(baseZ + s.height);
    return true;
}

// The classic client's path stack only creates blocker volumes from
// Impassable (and from surfaces before they become stand candidates). Wall,
// Window and NoShoot are visual/LOS flags here; using them for walking blocks
// false-positive walls in open passages.
bool World::IsStaticBlocker(u16 itemId) const {
    const auto& s = td_.Static(itemId);
    if ((s.flags & td::kFlagImpassable) == 0 &&
        (s.flags & (td::kFlagSurface | td::kFlagBridge)) != 0)
        return false;
    u32 blockMask = td::kFlagImpassable;
    // Optionally let A* route through pure-door tiles (opened at runtime).
    if (!acceptDoors_) blockMask |= td::kFlagDoor;
    return (s.flags & blockMask) != 0;
}

bool World::StaticBlocksSightAt(i32 x, i32 y, i8 eyeZ) const {
    if (x < 0 || y < 0) return true;
    const CachedBlock* blk = CachedBlockAt(static_cast<u32>(x) / 8,
                                           static_cast<u32>(y) / 8);
    if (!blk) return true;
    const u8 cx = static_cast<u8>(x % 8);
    const u8 cy = static_cast<u8>(y % 8);
    for (const auto& s : blk->statics) {
        if (s.cellX != cx || s.cellY != cy) continue;
        const auto& tile = td_.Static(s.itemId);
        // Roofs and ordinary floor decoration are not walls.  Windows retain
        // their normal transparent sight line; the server's default speech
        // LOS likewise does not treat a window as a solid wall.
        if ((tile.flags & (td::kFlagWall | td::kFlagWhole |
                           td::kFlagImpassable)) == 0 ||
            (tile.flags & (td::kFlagWindow | td::kFlagTransparent)) != 0)
            continue;
        const i32 lo = static_cast<i32>(s.z);
        const i32 hi = lo + (tile.height ? tile.height : 1);
        if (static_cast<i32>(eyeZ) >= lo && static_cast<i32>(eyeZ) < hi)
            return true;
    }
    return false;
}

bool World::HasDoorAt(u32 x, u32 y, i8 fromZ, i8 standZ,
                      u8 charHeight) const {
    const u32 bx = x / 8;
    const u32 by = y / 8;
    const u8  cx = static_cast<u8>(x % 8);
    const u8  cy = static_cast<u8>(y % 8);

    const CachedBlock* blk = CachedBlockAt(bx, by);
    if (!blk) return false;

    const i32 colLo = (fromZ > standZ) ? fromZ : standZ;
    const i32 colHi = colLo + charHeight;
    for (const auto& s : blk->statics) {
        if (s.cellX != cx || s.cellY != cy) continue;
        const auto& st = td_.Static(s.itemId);
        if ((st.flags & td::kFlagDoor) == 0) continue;

        const i32 obsLo = static_cast<i32>(s.z);
        const i32 obsHi = obsLo + (st.height ? st.height : 1);
        if (obsHi > colLo && obsLo < colHi) return true;
    }
    return false;
}

// Upper bound on statics decoded per block. Blocks with more than this are
// extremely rare; the tail is dropped, matching the old fixed-buffer behavior.
static constexpr u32 kMaxStaticsPerBlock = 1024;

// Return a decoded block (land + statics) from the cache, loading it on miss.
// Round-robin eviction; nullptr on I/O error or out-of-range.
const World::CachedBlock* World::CachedBlockAt(u32 bx, u32 by) const {
    for (u32 i = 0; i < kBlockCacheSlots; ++i) {
        const CachedBlock& c = blockCache_[i];
        if (c.valid && c.bx == bx && c.by == by) return &c;
    }

    CachedBlock& slot = blockCache_[blockCacheNext_];
    blockCacheNext_ = (blockCacheNext_ + 1) % kBlockCacheSlots;
    slot.valid = false;   // invalidate until fully loaded (early-return safe)

    if (!map_.ReadBlock(bx, by, &slot.land)) return nullptr;

    u32 probe = 0;
    if (!map_.ReadStatics(bx, by, nullptr, 0, &probe)) return nullptr;
    if (probe > kMaxStaticsPerBlock) probe = kMaxStaticsPerBlock;
    slot.statics.resize(probe);
    u32 got = 0;
    if (probe > 0) {
        if (!map_.ReadStatics(bx, by, slot.statics.data(), probe, &got))
            return nullptr;
        slot.statics.resize(got);
    }

    slot.bx = bx;
    slot.by = by;
    slot.valid = true;
    return &slot;
}

void World::CollectStatics(i32 cx, i32 cy, i32 radius,
                           std::vector<StaticHit>& out) const {
    if (radius < 0) return;
    const i32 minX = cx - radius, maxX = cx + radius;
    const i32 minY = cy - radius, maxY = cy + radius;
    const i32 bx0 = (minX < 0 ? 0 : minX) / 8;
    const i32 by0 = (minY < 0 ? 0 : minY) / 8;
    const i32 bx1 = (maxX < 0 ? 0 : maxX) / 8;
    const i32 by1 = (maxY < 0 ? 0 : maxY) / 8;
    for (i32 by = by0; by <= by1; ++by) {
        for (i32 bx = bx0; bx <= bx1; ++bx) {
            const CachedBlock* blk = CachedBlockAt(static_cast<u32>(bx),
                                                   static_cast<u32>(by));
            if (!blk) continue;
            for (const auto& s : blk->statics) {
                const i32 sx = bx * 8 + s.cellX;
                const i32 sy = by * 8 + s.cellY;
                if (sx < minX || sx > maxX || sy < minY || sy > maxY) continue;
                out.push_back(StaticHit{sx, sy, s.z, s.itemId});
            }
        }
    }
}

WalkResult World::QueryCell(const WalkQuery& q) const {
    WalkResult r{false, 0, 0, 0, false};

    const u32 bx = q.x / 8;
    const u32 by = q.y / 8;
    const u8  cx = static_cast<u8>(q.x % 8);
    const u8  cy = static_cast<u8>(q.y % 8);

    const CachedBlock* blk = CachedBlockAt(bx, by);
    if (!blk) return r;

    const map::LandCell& land = blk->land.cells[cy * 8 + cx];
    r.landTileId = land.tileId;

    const map::StaticItem* stbuf = blk->statics.data();
    const u32 nblockStatics = static_cast<u32>(blk->statics.size());

    // Collect candidate standing surfaces (their top faces) at this cell.
    struct Candidate { i32 top; bool land; };
    Candidate cands[64];
    u32 ncands = 0;

    // Land surface. Impassable + Wet (water/void) land is a blocker, not a
    // floor for a walking humanoid (UO Demo / Sphere gate water behind Swim).
    const auto& landTile = td_.Land(land.tileId);
    const bool landBlocked =
        (landTile.flags & (td::kFlagImpassable | td::kFlagWet)) != 0;
    if (!landBlocked && ncands < 64) cands[ncands++] = {static_cast<i32>(land.z), true};

    u32 cellStaticCount = 0;
    bool nearFoliage = false;
    for (u32 i = 0; i < nblockStatics; ++i) {
        const auto& s = stbuf[i];
        // Forest bias: trees and undergrowth carry the Foliage flag. A foliage
        // static in this cell or an adjacent one (within the same block) marks
        // a "woods" cell so A* can be biased to skirt forests, not thread them.
        if (!nearFoliage && (td_.Static(s.itemId).flags & td::kFlagFoliage)) {
            const int ddx = static_cast<int>(s.cellX) - static_cast<int>(cx);
            const int ddy = static_cast<int>(s.cellY) - static_cast<int>(cy);
            if (ddx >= -1 && ddx <= 1 && ddy >= -1 && ddy <= 1) nearFoliage = true;
        }
        if (s.cellX != cx || s.cellY != cy) continue;
        ++cellStaticCount;
        i8 top;
        if (StaticSurfaceTop(s.itemId, s.z, &top)) {
            if (ncands < 64) cands[ncands++] = {static_cast<i32>(top), false};
        }
    }
    r.staticCount  = cellStaticCount;
    r.nearFoliage  = nearFoliage;

    const i32 fromZ = static_cast<i32>(q.fromZ);
    const i32 targetZ = q.hasPreferredZ ? static_cast<i32>(q.preferredZ) : fromZ;

    // Pick the best candidate: walkable, within step range, with clearance.
    bool best_found = false;
    i32  best_z = 0;
    i32  best_dist = 0x7FFFFFFF;

    for (u32 ci = 0; ci < ncands; ++ci) {
        const i32 sz = cands[ci].top;
        const i32 dz = sz - fromZ;
        if (q.hasPreferredZ && cands[ci].land && dz > kMaxLandStepUp) continue;
        if (dz > q.maxStepUp) continue;
        if (-dz > q.maxStepDown) continue;

        // (1) Climbing onto a stacked staircase from the wrong side. The
        // server only lets you step up onto a surface that you can actually
        // reach; if another standable surface sits between the level you step
        // from and this one, that lower shelf is where you'd land (or be
        // walled off), so this higher target is not a valid single step.
        // A lone ramp/floor (nothing between) is unaffected, so legitimate
        // stair climbs still resolve. Example: standing at z20 beside a stair
        // block whose steps top out at 20, 25 and 30 — the z25 shelf blocks a
        // direct hop to z30, and z25/z20 themselves lack headroom, so the
        // whole side is (correctly) impassable, exactly as the server rejects.
        if (dz > 0) {
            bool shelfBetween = false;
            for (u32 cj = 0; cj < ncands && !shelfBetween; ++cj) {
                if (cj == ci) continue;
                const i32 oz = cands[cj].top;
                if (oz > fromZ && oz < sz) shelfBetween = true;
            }
            if (shelfBetween) continue;
        }
        if (dz < 0) {
            bool shelfBetween = false;
            for (u32 cj = 0; cj < ncands && !shelfBetween; ++cj) {
                if (cj == ci) continue;
                const i32 oz = cands[cj].top;
                if (oz < fromZ && oz > sz) shelfBetween = true;
            }
            if (shelfBetween) continue;
        }

        // (2) Vertical clearance, measured from the *approach* level. The
        // original client's Pathfinding_GetTileMinZ/GetTileTopZ check headroom
        // against the floor you step in from, not the destination surface's
        // own z. So the body column runs from max(surfaceZ, fromZ) up by
        // charHeight: stepping DOWN under a low ceiling is blocked when the
        // ceiling leaves <charHeight above the level you enter at, even though
        // the surface itself sits lower (e.g. a side-drop off a stair onto a
        // floor that is capped by an upper storey). Counting *surfaces*
        // (stairs/floors) as ceilings, not just walls, is also how a cell
        // under a rising staircase is rejected. Decorative items are ignored.
        const i32 approachZ = (fromZ > sz) ? fromZ : sz;
        bool blocked = false;
        for (u32 i = 0; i < nblockStatics && !blocked; ++i) {
            const auto& s = stbuf[i];
            if (s.cellX != cx || s.cellY != cy) continue;

            const auto& st = td_.Static(s.itemId);
            const bool isSurf = (st.flags & (td::kFlagSurface | td::kFlagBridge)) != 0;
            const bool isBlock = IsStaticBlocker(s.itemId);
            if (!isSurf && !isBlock) continue;

            const i32 obs_lo = s.z;
            // Surfaces occupy up to their top face (z+height); a floor we
            // stand on tops out exactly at sz, so it never blocks itself.
            // Blockers (walls) get at least 1 unit of thickness.
            const i32 obs_hi = isSurf ? (s.z + st.height)
                                      : (s.z + (st.height ? st.height : 1));
            const i32 col_lo = approachZ;
            const i32 col_hi = approachZ + q.charHeight;
            if (obs_hi > col_lo && obs_lo < col_hi) blocked = true;
        }
        if (blocked) continue;

        const i32 targetDz = sz - targetZ;
        const i32 abs_target_dz = targetDz < 0 ? -targetDz : targetDz;
        if (!best_found || abs_target_dz < best_dist) {
            best_found = true;
            best_z     = sz;
            best_dist  = abs_target_dz;
        }
    }

    r.walkable = best_found;
    r.standZ   = static_cast<i8>(best_z);
    return r;
}

}
