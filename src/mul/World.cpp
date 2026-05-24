#include "uo/world.h"

#include <algorithm>

namespace uo::world {

namespace td = uo::tiledata;

constexpr i32 kMaxLandStepUp = 2;

// A static is a "surface" if it has Surface or Bridge flag.
bool World::StaticSurfaceTop(u16 itemId, i8 baseZ, i8* topOut) const {
    const auto& s = td_.Static(itemId);
    const bool isSurface = (s.flags & (td::kFlagSurface | td::kFlagBridge)) != 0;
    if (!isSurface) return false;
    if (topOut) *topOut = static_cast<i8>(baseZ + s.height);
    return true;
}

// Anything impassable/wall/door (closed)/window/no-shoot is a blocker
// when standing into its volume. Foliage usually isn't a blocker for
// walking on land; classic clients treat it as Surface=0 / Impassable=0
// so we never treat it as wall.
bool World::IsStaticBlocker(u16 itemId) const {
    const auto& s = td_.Static(itemId);
    if (s.flags & (td::kFlagSurface | td::kFlagBridge)) return false;
    u32 blockMask = td::kFlagImpassable | td::kFlagWall |
                    td::kFlagWindow     | td::kFlagNoShoot;
    // Optionally let A* route through pure-door tiles (opened at runtime).
    if (!acceptDoors_) blockMask |= td::kFlagDoor;
    return (s.flags & blockMask) != 0;
}

// Read statics for a block and filter to cell. Caller bounds the buffer
// by kMaxStaticsPerCell; cells with more than that are extremely rare.
static constexpr u32 kMaxStaticsPerBlock = 1024;

WalkResult World::QueryCell(const WalkQuery& q) const {
    WalkResult r{false, 0, 0, 0, false};

    map::LandCell land{};
    if (!map_.ReadCell(q.x, q.y, &land)) return r;
    r.landTileId = land.tileId;

    map::StaticItem stbuf[kMaxStaticsPerBlock];
    u32 nblockStatics = 0;
    const u32 bx = q.x / 8;
    const u32 by = q.y / 8;
    const u8  cx = static_cast<u8>(q.x % 8);
    const u8  cy = static_cast<u8>(q.y % 8);

    if (!map_.ReadStatics(bx, by, stbuf, kMaxStaticsPerBlock, &nblockStatics)) {
        return r;
    }

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
