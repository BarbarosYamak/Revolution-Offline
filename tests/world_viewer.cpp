// Phase-1 standalone world viewer: loads the MULs and draws the isometric
// world centered on a coordinate. Arrow keys pan the camera (Shift = faster).
//
//   build_viewer.bat <camX> <camY> [w] [h] [scale]

#define MINIFB_IMPLEMENTATION
#include "win32/MiniFB.h"

#include "render/Renderer.h"
#include "uo/art.h"
#include "uo/map.h"
#include "uo/png.h"
#include "uo/texmap.h"
#include "uo/tiledata.h"

#include <cstdio>
#include <cstdlib>

using namespace uo;

namespace {
constexpr int VK_LEFT_  = 0x25;
constexpr int VK_UP_    = 0x26;
constexpr int VK_RIGHT_ = 0x27;
constexpr int VK_DOWN_  = 0x28;
constexpr int VK_SHIFT_ = 0x10;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <camX> <camY> [w] [h] [scale]\n", argv[0]);
        return 1;
    }
    i32 camX = std::atoi(argv[1]);
    i32 camY = std::atoi(argv[2]);
    const int w = argc > 3 ? std::atoi(argv[3]) : 512;
    const int h = argc > 4 ? std::atoi(argv[4]) : 384;
    const int scale = argc > 5 ? std::atoi(argv[5]) : 2;   // window = scale x fb
    const int camZ  = argc > 6 ? std::atoi(argv[6]) : 0;
    const char* dump = argc > 7 ? argv[7] : nullptr;       // PNG path => render once

    map::Map map;
    if (!map.Open("E:/uo/map0.mul", "E:/uo/staidx0.mul", "E:/uo/statics0.mul",
                  map::kBritWidthBlocks, map::kBritHeightBlocks, "E:/uo/verdata.mul")) {
        std::printf("failed to open map MULs (E:/uo/map0.mul ...)\n");
        return 2;
    }
    art::ArtLoader art;
    if (!art.Open("E:/uo/artidx.mul", "E:/uo/art.mul")) {
        std::printf("failed to open art MULs (E:/uo/artidx.mul, E:/uo/art.mul)\n");
        return 3;
    }
    tiledata::TileDataLoader td;
    if (!td.Load("E:/uo/tiledata.mul")) {
        std::printf("failed to load E:/uo/tiledata.mul\n");
        return 5;
    }
    texmap::TexmapLoader tex;
    if (!tex.Open("E:/uo/texidx.mul", "E:/uo/texmaps.mul")) {
        std::printf("failed to open texmaps (E:/uo/texidx.mul, E:/uo/texmaps.mul)\n");
        return 6;
    }
    if (!mfb_open("uo world viewer", w, h, scale, 15)) {
        std::printf("mfb_open failed\n");
        return 4;
    }

    render::Renderer rend(w, h);
    const i32 maxX = static_cast<i32>(map.WidthCells()) - 1;
    const i32 maxY = static_cast<i32>(map.HeightCells()) - 1;

    if (dump) {
        rend.RenderWorld(map, art, td, tex, camX, camY, camZ);
        png::Save(dump, rend.Frame(), w, h);
        std::printf("dumped %s (%d,%d,z%d)\n", dump, camX, camY, camZ);
        mfb_close();
        return 0;
    }

    std::printf("viewer: arrows pan (shift=fast), close window to quit. start=%d,%d\n",
                camX, camY);

    for (;;) {
        const char* keys = mfb_keystatus();
        const int step = keys[VK_SHIFT_] ? 8 : 1;
        // Screen-aligned panning along the iso diagonals.
        if (keys[VK_UP_])    { camX -= step; camY -= step; }
        if (keys[VK_DOWN_])  { camX += step; camY += step; }
        if (keys[VK_LEFT_])  { camX -= step; camY += step; }
        if (keys[VK_RIGHT_]) { camX += step; camY -= step; }
        camX = camX < 0 ? 0 : (camX > maxX ? maxX : camX);
        camY = camY < 0 ? 0 : (camY > maxY ? maxY : camY);

        rend.RenderWorld(map, art, td, tex, camX, camY);
        if (!mfb_update(rend.Frame(), 60)) break;
    }

    mfb_close();
    return 0;
}
