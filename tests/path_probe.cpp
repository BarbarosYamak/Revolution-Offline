// Probe the bot's walkability/A* against the real MULs + verdata.mul overlay
// (the live client uses verdata, so we must too) and visualize a local window
// so a "no path" over a few tiles is easy to diagnose.
//   scripts/build_pathprobe.bat [sx sy sz gx gy [margin]]
#include "bot/Pathfinding.h"
#include "uo/map.h"
#include "uo/tiledata.h"
#include "uo/world.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <unordered_map>

using namespace uo;

static const char* kDirName[8] = {"N","NE","E","SE","S","SW","W","NW"};

static u64 Key(i32 x, i32 y) {
    return (static_cast<u64>(static_cast<u32>(x)) << 32) | static_cast<u32>(y);
}

// Walkability of a cell across a sweep of approach-z values. A surface atop
// stairs is only reachable when queried from a fromZ within step range, so
// this shows the z you must arrive at to step onto it.
static void SweepCell(world::World& world, const char* tag, long x, long y) {
    std::printf("%s (%ld,%ld) walkability vs approach fromZ:\n", tag, x, y);
    for (int fz = -16; fz <= 60; fz += 4) {
        world::WalkQuery q{}; q.x = (u32)x; q.y = (u32)y; q.fromZ = (i8)fz;
        const auto r = world.QueryCell(q);
        std::printf("  fromZ=%4d -> %-5s standZ=%-4d statics=%u land=0x%04X\n",
                    fz, r.walkable ? "WALK" : "block",
                    (int)r.standZ, r.staticCount, r.landTileId);
    }
}

// Dump every static in a cell with its tiledata flags + our classification,
// so we can see exactly which item is (mis)blocking the tile.
static void DumpCell(world::World& world, map::Map& m,
                     tiledata::TileDataLoader& td, i32 x, i32 y) {
    const u32 bx = (u32)x / 8, by = (u32)y / 8;
    const u8  cx = (u8)((u32)x % 8), cy = (u8)((u32)y % 8);
    map::LandCell lc{}; m.ReadCell((u32)x, (u32)y, &lc);
    const auto& lt = td.Land(lc.tileId);
    std::printf("CELL (%d,%d): land id=0x%04X z=%d flags=0x%08X\n",
                x, y, lc.tileId, (int)lc.z, lt.flags);

    map::StaticItem buf[256]; u32 n = 0;
    m.ReadStatics(bx, by, buf, 256, &n);
    for (u32 i = 0; i < n; ++i) {
        if (buf[i].cellX != cx || buf[i].cellY != cy) continue;
        const auto& s = td.Static(buf[i].itemId);
        i8 top = 0;
        const bool surf  = world.StaticSurfaceTop(buf[i].itemId, buf[i].z, &top);
        const bool block = world.IsStaticBlocker(buf[i].itemId);
        std::printf("   static id=0x%04X z=%d h=%u flags=0x%08X %s%s top=%d\n",
                    buf[i].itemId, (int)buf[i].z, s.height, s.flags,
                    block ? "[BLOCK]" : "", surf ? "[SURF]" : "",
                    surf ? (int)top : 0);
    }
}

static void Neighbors(world::World& world, const char* tag, i32 x, i32 y, i8 z) {
    std::printf("%s neighbors of (%d,%d,%d):\n", tag, x, y, (int)z);
    for (u8 d = 0; d < 8; ++d) {
        i32 dx, dy; bot::DirToDelta(d, &dx, &dy);
        const i32 nx = x + dx, ny = y + dy;
        world::WalkQuery q{}; q.x = (u32)nx; q.y = (u32)ny; q.fromZ = z;
        const auto r = world.QueryCell(q);
        if (r.walkable)
            std::printf("  %-2s (%d,%d) WALK standZ=%-4d dz=%+d statics=%u\n",
                        kDirName[d], nx, ny, (int)r.standZ,
                        (int)r.standZ - (int)z, r.staticCount);
        else
            std::printf("  %-2s (%d,%d) block            statics=%u\n",
                        kDirName[d], nx, ny, r.staticCount);
    }
}

int main(int argc, char** argv) {
    long sx = (argc > 1) ? std::atol(argv[1]) : 1927;
    long sy = (argc > 2) ? std::atol(argv[2]) : 2777;
    long sz = (argc > 3) ? std::atol(argv[3]) : 0;
    long gx = (argc > 4) ? std::atol(argv[4]) : 1923;
    long gy = (argc > 5) ? std::atol(argv[5]) : 2773;
    int  M  = (argc > 6) ? std::atoi(argv[6]) : 8;
    const bool hasGoalZ = (argc > 7);
    long gz = hasGoalZ ? std::atol(argv[7]) : 0;
    if (M < 2)  M = 2;
    if (M > 20) M = 20;

    tiledata::TileDataLoader td;
    if (!td.Load("E:/uo/tiledata.mul")) { std::printf("td load fail\n"); return 2; }

    map::Map m;
    if (!m.Open("E:/uo/map0.mul", "E:/uo/staidx0.mul", "E:/uo/statics0.mul",
                map::kBritWidthBlocks, map::kBritHeightBlocks,
                "E:/uo/verdata.mul")) {
        std::printf("map load fail\n"); return 2;
    }
    world::World world(td, m);
    world.SetAcceptDoors(true);

    world::WalkQuery sq{}; sq.x = (u32)sx; sq.y = (u32)sy; sq.fromZ = (i8)sz;
    const auto sr = world.QueryCell(sq);
    std::printf("start (%ld,%ld,%ld) walkable=%d standZ=%d\n",
                sx, sy, sz, sr.walkable, (int)sr.standZ);

    // ---- Windowed reachability flood + local grids. Only for small windows;
    // a cross-continent route (Trinsic<->Britain) spans hundreds of tiles and
    // would explode the flood and the grid printing, so skip it there.
    const i32 minx = (i32)((sx < gx ? sx : gx) - M);
    const i32 maxx = (i32)((sx > gx ? sx : gx) + M);
    const i32 miny = (i32)((sy < gy ? sy : gy) - M);
    const i32 maxy = (i32)((sy > gy ? sy : gy) + M);
    const long winW = (long)(maxx - minx + 1), winH = (long)(maxy - miny + 1);
    if (winW * winH <= 8000) {
        auto inWin = [&](i32 x, i32 y) {
            return x >= minx && x <= maxx && y >= miny && y <= maxy;
        };
        std::unordered_map<u64, i8> reached;   // cell -> standZ
        std::deque<std::pair<i32,i32>> bfs;
        if (sr.walkable) {
            reached[Key((i32)sx,(i32)sy)] = sr.standZ;
            bfs.push_back({(i32)sx,(i32)sy});
        }
        while (!bfs.empty()) {
            const auto cur = bfs.front(); bfs.pop_front();
            const i8 cz = reached[Key(cur.first, cur.second)];
            for (u8 d = 0; d < 8; ++d) {
                i32 dx, dy; bot::DirToDelta(d, &dx, &dy);
                const i32 nx = cur.first + dx, ny = cur.second + dy;
                if (!inWin(nx, ny) || reached.count(Key(nx, ny))) continue;
                world::WalkQuery q{}; q.x = (u32)nx; q.y = (u32)ny; q.fromZ = cz;
                const auto r = world.QueryCell(q);
                if (!r.walkable) continue;
                if (dx != 0 && dy != 0) {
                    world::WalkQuery a{}, b{};
                    a.x=(u32)(cur.first+dx); a.y=(u32)cur.second; a.fromZ=cz;
                    b.x=(u32)cur.first; b.y=(u32)(cur.second+dy); b.fromZ=cz;
                    if (!world.QueryCell(a).walkable) continue;
                    if (!world.QueryCell(b).walkable) continue;
                }
                reached[Key(nx, ny)] = r.standZ;
                bfs.push_back({nx, ny});
            }
        }
        const bool goalReached = reached.count(Key((i32)gx,(i32)gy)) != 0;
        std::printf("goal (%ld,%ld) reached-by-flood=%d  (window %ldx%ld, %zu cells reachable)\n\n",
                    gx, gy, goalReached, winW, winH, reached.size());

        std::printf("reachability  (rows y=%d..%d, cols x=%d..%d):\n", miny, maxy, minx, maxx);
        for (i32 y = miny; y <= maxy; ++y) {
            std::printf("y%5d ", y);
            for (i32 x = minx; x <= maxx; ++x) {
                char c;
                if (x == (i32)sx && y == (i32)sy)      c = 'S';
                else if (x == (i32)gx && y == (i32)gy) c = goalReached ? 'g' : 'G';
                else c = reached.count(Key(x, y)) ? '.' : '#';
                std::printf("%c", c);
            }
            std::printf("\n");
        }

        std::printf("\nmax static surface top z (raw statics; '   .'=none):\n");
        for (i32 y = miny; y <= maxy; ++y) {
            std::printf("y%5d ", y);
            for (i32 x = minx; x <= maxx; ++x) {
                const u32 bx = (u32)x / 8, by = (u32)y / 8;
                const u8 cx = (u8)((u32)x % 8), cy = (u8)((u32)y % 8);
                map::StaticItem buf[256]; u32 n = 0;
                m.ReadStatics(bx, by, buf, 256, &n);
                int best = -999;
                for (u32 i = 0; i < n; ++i) {
                    if (buf[i].cellX != cx || buf[i].cellY != cy) continue;
                    i8 top = 0;
                    if (world.StaticSurfaceTop(buf[i].itemId, buf[i].z, &top) && top > best)
                        best = top;
                }
                if (best == -999) std::printf("   .");
                else              std::printf("%4d", best);
            }
            std::printf("\n");
        }

        std::printf("\nland z (raw map0):\n");
        for (i32 y = miny; y <= maxy; ++y) {
            std::printf("y%5d ", y);
            for (i32 x = minx; x <= maxx; ++x) {
                map::LandCell lc{};
                if (m.ReadCell((u32)x, (u32)y, &lc))
                    std::printf("%4d", (int)lc.z);
                else
                    std::printf("   ?");
            }
            std::printf("\n");
        }
        std::printf("\n");
    } else {
        std::printf("(window %ldx%ld too large; skipping reachability flood + grids)\n\n",
                    winW, winH);
    }

    // ---- A* cost: total path cost = sum of step costs (10 straight / 14
    // diagonal) plus the grass penalty per open-grass step. Run penalty=0
    // (true geometric route, cheap to search) and penalty=6 (what the live
    // client actually pays / explores).
    auto isGrass = [](u16 id) { return id >= 0x0003 && id <= 0x0006; };
    bot::PathStats st;
    auto measure = [&](u32 penalty, u32 cap) {
        bot::PathStats s{};
        bot::PathOptions opts;
        opts.maxNodesExpanded = cap;
        opts.stats        = &s;
        opts.hasGoalZ     = hasGoalZ;
        opts.goalZ        = (i32)gz;
        opts.grassPenalty = penalty;
        const auto t0 = std::chrono::steady_clock::now();
        const auto pp = bot::FindPath(world, (i32)sx,(i32)sy,(i8)sz,
                                      (i32)gx,(i32)gy, opts);
        const auto t1 = std::chrono::steady_clock::now();
        const double searchUs =
            std::chrono::duration<double, std::micro>(t1 - t0).count();
        i32 x=(i32)sx, y=(i32)sy; i8 z=(i8)sz;
        u64 geo=0, total=0; u32 grassSteps=0, diag=0;
        for (u8 d : pp) {
            i32 dx, dy; bot::DirToDelta(d, &dx, &dy);
            const bool isDiag = (dx != 0 && dy != 0);
            const u32 base = isDiag ? 14u : 10u;
            x += dx; y += dy;
            world::WalkQuery q{}; q.x=(u32)x; q.y=(u32)y; q.fromZ=z;
            const auto r = world.QueryCell(q);
            if (r.walkable) z = r.standZ;
            geo += base; total += base;
            if (penalty && isGrass(r.landTileId)) { total += penalty; ++grassSteps; }
            if (isDiag) ++diag;
        }
        std::printf("penalty=%-2u cap=%-8u %-9s steps=%zu (diag=%u) grassSteps=%u "
                    "geoCost=%llu pathCost=%llu expanded=%u searchUs=%.1f "
                    "closest=(%d,%d,%d) h=%u\n",
                    penalty, cap, pp.empty() ? "(NO PATH)" : "OK", pp.size(), diag,
                    grassSteps, (unsigned long long)geo, (unsigned long long)total,
                    s.expanded, searchUs, s.closestX, s.closestY, (int)s.closestZ,
                    s.closestH);
        if (!pp.empty() && pp.size() <= 64) {
            x=(i32)sx; y=(i32)sy; z=(i8)sz;
            std::printf("  route:");
            for (u8 d : pp) {
                i32 dx, dy; bot::DirToDelta(d, &dx, &dy);
                x += dx; y += dy;
                world::WalkQuery q{}; q.x=(u32)x; q.y=(u32)y; q.fromZ=z;
                q.hasPreferredZ = hasGoalZ;
                q.preferredZ = (i8)gz;
                const auto r = world.QueryCell(q);
                if (r.walkable) z = r.standZ;
                std::printf(" %s(%d,%d,%d)", kDirName[d], x, y, (int)z);
            }
            std::printf("\n");
        }
        st = s;
    };
    measure(0, 4000000);
    measure(6, 8000000);
    std::printf("\n");

    Neighbors(world, "FRONTIER", st.closestX, st.closestY, st.closestZ);
    std::printf("\n");
    SweepCell(world, "GOAL", gx, gy);
    std::printf("\n--- static detail around goal ---\n");
    DumpCell(world, m, td, (i32)gx,   (i32)gy);
    DumpCell(world, m, td, (i32)gx-1, (i32)gy);   // W
    DumpCell(world, m, td, (i32)gx+1, (i32)gy);   // E
    DumpCell(world, m, td, (i32)gx,   (i32)gy-1); // N
    DumpCell(world, m, td, (i32)gx,   (i32)gy+1); // S
    return 0;
}
