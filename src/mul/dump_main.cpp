#include "uo/map.h"
#include "uo/tiledata.h"
#include "uo/world.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using uo::usize;

namespace {

void EscapeAsciiNul(const char* in, usize len, char* out, usize cap) {
    usize j = 0;
    for (usize i = 0; i < len && j + 2 < cap; ++i) {
        char c = in[i];
        if (c == '\0') break;
        if (c == '"' || c == '\\') {
            if (j + 3 >= cap) break;
            out[j++] = '\\';
            out[j++] = c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out[j++] = '?';
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

int DumpTileData(const char* path, const char* section,
                 long start, long count) {
    uo::tiledata::TileDataLoader loader;
    if (!loader.Load(path)) return 2;

    const bool do_land   = (!section || std::strcmp(section, "land")   == 0 || std::strcmp(section, "all") == 0);
    const bool do_static = (!section || std::strcmp(section, "static") == 0 || std::strcmp(section, "all") == 0);

    char nm[64];

    if (do_land) {
        const long end = (count < 0)
            ? static_cast<long>(uo::tiledata::kLandCount)
            : start + count;
        for (long i = start; i < end && i < static_cast<long>(uo::tiledata::kLandCount); ++i) {
            const auto& t = loader.Land(static_cast<uo::u32>(i));
            EscapeAsciiNul(t.name, sizeof(t.name), nm, sizeof(nm));
            std::printf("{\"kind\":\"land\",\"id\":%ld,\"flags\":\"0x%08X\","
                        "\"textureId\":%u,\"name\":\"%s\"}\n",
                        i, t.flags, t.textureId, nm);
        }
    }

    if (do_static) {
        const long end = (count < 0)
            ? static_cast<long>(uo::tiledata::kStaticCount)
            : start + count;
        for (long i = start; i < end && i < static_cast<long>(uo::tiledata::kStaticCount); ++i) {
            const auto& s = loader.Static(static_cast<uo::u32>(i));
            EscapeAsciiNul(s.name, sizeof(s.name), nm, sizeof(nm));
            std::printf("{\"kind\":\"static\",\"id\":%ld,\"flags\":\"0x%08X\","
                        "\"weight\":%u,\"quality\":%u,\"misc\":\"0x%08X\","
                        "\"hue\":%u,\"stackOffset\":%u,\"value\":%u,"
                        "\"height\":%u,\"name\":\"%s\"}\n",
                        i, s.flags, s.weight, s.quality, s.misc,
                        s.hue, s.stackOffset, s.value, s.height, nm);
        }
    }

    return 0;
}

int DumpMapCell(const char* mapPath, const char* staidxPath,
                const char* staticsPath,
                long x0, long y0, long w, long h) {
    uo::map::Map m;
    if (!m.Open(mapPath, staidxPath, staticsPath)) return 2;

    if (w <= 0 || h <= 0) { w = 8; h = 8; }
    uo::map::StaticItem stbuf[256];

    for (long dy = 0; dy < h; ++dy) {
        for (long dx = 0; dx < w; ++dx) {
            const long x = x0 + dx;
            const long y = y0 + dy;
            if (x < 0 || y < 0) continue;
            uo::map::LandCell cell{};
            if (!m.ReadCell(static_cast<uo::u32>(x), static_cast<uo::u32>(y), &cell)) continue;

            // Statics for this cell (we read the whole block then filter).
            const uo::u32 bx = static_cast<uo::u32>(x) / 8;
            const uo::u32 by = static_cast<uo::u32>(y) / 8;
            const uo::u8  cx = static_cast<uo::u8>(x % 8);
            const uo::u8  cy = static_cast<uo::u8>(y % 8);
            uo::u32 nstatic = 0;
            m.ReadStatics(bx, by, stbuf,
                          sizeof(stbuf) / sizeof(stbuf[0]), &nstatic);

            std::printf("{\"x\":%ld,\"y\":%ld,\"land\":{\"tileId\":%u,\"z\":%d},"
                        "\"statics\":[",
                        x, y, cell.tileId, static_cast<int>(cell.z));
            bool first = true;
            for (uo::u32 i = 0; i < nstatic; ++i) {
                const auto& s = stbuf[i];
                if (s.cellX != cx || s.cellY != cy) continue;
                std::printf("%s{\"itemId\":%u,\"z\":%d,\"hue\":%u}",
                            first ? "" : ",", s.itemId,
                            static_cast<int>(s.z), s.hue);
                first = false;
            }
            std::printf("]}\n");
        }
    }
    return 0;
}

int DumpWalkable(const char* tiledataPath,
                 const char* mapPath, const char* staidxPath,
                 const char* staticsPath,
                 long x0, long y0, long w, long h, long fromZ) {
    uo::tiledata::TileDataLoader td;
    if (!td.Load(tiledataPath)) return 2;
    uo::map::Map m;
    if (!m.Open(mapPath, staidxPath, staticsPath)) return 2;
    uo::world::World world(td, m);

    if (w <= 0 || h <= 0) { w = 8; h = 8; }

    for (long dy = 0; dy < h; ++dy) {
        for (long dx = 0; dx < w; ++dx) {
            const long x = x0 + dx;
            const long y = y0 + dy;
            uo::world::WalkQuery q{};
            q.x = static_cast<uo::u32>(x);
            q.y = static_cast<uo::u32>(y);
            q.fromZ = static_cast<uo::i8>(fromZ);
            const auto r = world.QueryCell(q);
            std::printf("{\"x\":%ld,\"y\":%ld,\"walkable\":%s,"
                        "\"standZ\":%d,\"statics\":%u}\n",
                        x, y, r.walkable ? "true" : "false",
                        static_cast<int>(r.standZ), r.staticCount);
        }
    }
    return 0;
}

void Usage() {
    std::fprintf(stderr,
        "usage: uo_mul_dump <subcommand> [args]\n"
        "  tiledata <path> [land|static|all] [start] [count]\n"
        "    default section=all, start=0, count=-1 (all entries)\n"
        "  map <map.mul> <staidx.mul> <statics.mul> <x> <y> [w] [h]\n"
        "    dump w*h cells starting at (x,y). default w=h=8.\n"
        "  walk <tiledata.mul> <map.mul> <staidx.mul> <statics.mul> "
        "<x> <y> [w] [h] [fromZ]\n"
        "    walkability query for w*h cells. default w=h=8, fromZ=0.\n");
}

}

int main(int argc, char** argv) {
    if (argc < 2) { Usage(); return 1; }

    if (std::strcmp(argv[1], "tiledata") == 0) {
        if (argc < 3) { Usage(); return 1; }
        const char* path    = argv[2];
        const char* section = (argc > 3) ? argv[3] : "all";
        const long  start   = (argc > 4) ? std::atol(argv[4]) : 0;
        const long  count   = (argc > 5) ? std::atol(argv[5]) : -1;
        return DumpTileData(path, section, start, count);
    }

    if (std::strcmp(argv[1], "map") == 0) {
        if (argc < 7) { Usage(); return 1; }
        const char* mapPath     = argv[2];
        const char* staidxPath  = argv[3];
        const char* staticsPath = argv[4];
        const long x = std::atol(argv[5]);
        const long y = std::atol(argv[6]);
        const long w = (argc > 7) ? std::atol(argv[7]) : 8;
        const long h = (argc > 8) ? std::atol(argv[8]) : 8;
        return DumpMapCell(mapPath, staidxPath, staticsPath, x, y, w, h);
    }

    if (std::strcmp(argv[1], "walk") == 0) {
        if (argc < 8) { Usage(); return 1; }
        const char* td_path      = argv[2];
        const char* map_path     = argv[3];
        const char* staidx_path  = argv[4];
        const char* statics_path = argv[5];
        const long x = std::atol(argv[6]);
        const long y = std::atol(argv[7]);
        const long w     = (argc > 8) ? std::atol(argv[8]) : 8;
        const long h     = (argc > 9) ? std::atol(argv[9]) : 8;
        const long fromZ = (argc > 10) ? std::atol(argv[10]) : 0;
        return DumpWalkable(td_path, map_path, staidx_path, statics_path,
                            x, y, w, h, fromZ);
    }

    Usage();
    return 1;
}
