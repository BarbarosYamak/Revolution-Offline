// Standalone anim-frame dumper. Decodes a body's animation and writes PNGs so
// the decode / anchor can be checked visually. Two modes:
//
//   build_animprobe.bat <bodyHex>                     still frame 0, all 8 dirs
//   build_animprobe.bat <bodyHex> <action> [animIdx] [animMul]
//                                                     every frame of <action>,
//                                                     dirs 1 (SE) and 3 (N),
//                                                     plus a frame-count report
//                                                     for all 8 dirs.
//
//   e.g. build_animprobe.bat 0x00C8          (horse, still)
//        build_animprobe.bat 0x00C8 0        (horse walk cycle)
//        build_animprobe.bat 0x0190 4        (human stand cycle)
//        build_animprobe.bat 0x003B 0        (dragon walk cycle)

#include "uo/anim.h"
#include "uo/png.h"

#include <cstdio>
#include <cstdlib>

using namespace uo;

int main(int argc, char** argv) {
    const int  body   = argc > 1 ? static_cast<int>(std::strtol(argv[1], nullptr, 0)) : 0x00C8;
    const bool haveAct = argc > 2;
    const int  action = haveAct ? static_cast<int>(std::strtol(argv[2], nullptr, 0)) : 0;
    const char* idx = argc > 3 ? argv[3] : "E:/uo/anim.idx";
    const char* mul = argc > 4 ? argv[4] : "E:/uo/anim.mul";

    anim::AnimLoader a;
    if (!a.Open(idx, mul)) {
        std::printf("failed to open %s / %s\n", idx, mul);
        return 2;
    }

    if (!haveAct) {
        // Still mode: frame 0 of action 0 for each facing (regression check).
        for (int d = 0; d < 8; ++d) {
            const anim::Frame* f = a.Body(static_cast<u16>(body), static_cast<u8>(d));
            if (!f) { std::printf("body 0x%X dir %d: no frame\n", body, d); continue; }
            char path[256];
            std::snprintf(path, sizeof(path), "build/anim_%04X_d%d.png", body, d);
            png::Save(path, f->px.data(), f->width, f->height);
            std::printf("body 0x%X dir %d: %dx%d anchor(%d,%d) -> %s\n",
                        body, d, f->width, f->height, f->anchorX, f->anchorY, path);
        }
        return 0;
    }

    // Animation mode: report frame counts for all dirs, dump full cycles for a
    // non-mirrored (dir 3 = N) and a mirrored (dir 1 = SE) facing.
    std::printf("body 0x%X action %d frame counts:", body, action);
    for (int d = 0; d < 8; ++d)
        std::printf(" d%d=%u", d, a.FrameCount(static_cast<u16>(body),
                                               static_cast<u8>(d), static_cast<u8>(action)));
    std::printf("\n");

    const int dirs[2] = { 3, 1 };
    for (int di = 0; di < 2; ++di) {
        const int d = dirs[di];
        const u32 n = a.FrameCount(static_cast<u16>(body), static_cast<u8>(d),
                                   static_cast<u8>(action));
        for (u32 fi = 0; fi < n; ++fi) {
            const anim::Frame* f = a.Body(static_cast<u16>(body), static_cast<u8>(d),
                                          static_cast<u8>(action), static_cast<u16>(fi));
            if (!f) { std::printf("  d%d f%u: blank\n", d, fi); continue; }
            char path[256];
            std::snprintf(path, sizeof(path), "build/anim_%04X_a%d_d%d_f%02u.png",
                          body, action, d, fi);
            png::Save(path, f->px.data(), f->width, f->height);
            std::printf("  d%d f%u: %dx%d anchor(%d,%d) -> %s\n",
                        d, fi, f->width, f->height, f->anchorX, f->anchorY, path);
        }
    }
    return 0;
}
