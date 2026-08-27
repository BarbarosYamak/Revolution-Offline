// Cursor hotspot probe. The client (GumpTexture_Initialize @0x4B9A90) computes
// each cursor's click hotspot from a marker pixel of value 992 (0x03E0, pure
// green) embedded in the cursor art: hotspot X = column of the marker in the
// top row, hotspot Y = row whose first drawn pixel is the marker. This dumps,
// for every in-game directional cursor, its size and the (x,y) of every 0x03E0
// pixel so we can confirm the marker layout before porting the offset.
//
//   build_cursorprobe.bat            (default art paths)

#include "uo/art.h"

#include <cstdio>

using namespace uo;

namespace {
void dumpCursor(art::ArtLoader& art, u16 itemId, const char* tag) {
    const art::Sprite* s = art.Static(itemId);
    if (!s) { std::printf("%-10s 0x%04X: (no art)\n", tag, itemId); return; }
    std::printf("%-10s 0x%04X: %dx%d  markers(0x03E0):", tag, itemId, s->width, s->height);
    int n = 0, firstRow0X = -1, firstRowStartY = -1;
    for (int y = 0; y < s->height; ++y) {
        for (int x = 0; x < s->width; ++x) {
            const u16 p = s->px[static_cast<usize>(y) * s->width + x];
            if ((p & 0x7FFF) != 0x03E0) continue;
            ++n;
            if (n <= 8) std::printf(" (%d,%d)", x, y);
            if (y == 0 && firstRow0X < 0) firstRow0X = x;
        }
        // first drawn (non-zero) pixel of this row
        if (firstRowStartY < 0) {
            for (int x = 0; x < s->width; ++x) {
                const u16 p = s->px[static_cast<usize>(y) * s->width + x];
                if (!p) continue;
                if ((p & 0x7FFF) == 0x03E0) firstRowStartY = y;
                break;
            }
        }
    }
    std::printf("  total=%d rowtopX=%d rowstartY=%d\n", n, firstRow0X, firstRowStartY);
}
}  // namespace

int main(int argc, char** argv) {
    const char* idx = argc > 1 ? argv[1] : "E:/uo/artidx.mul";
    const char* mul = argc > 2 ? argv[2] : "E:/uo/art.mul";

    art::ArtLoader art;
    if (!art.Open(idx, mul)) {
        std::printf("failed to open %s / %s\n", idx, mul);
        return 2;
    }

    std::printf("== peace walk (0x206A..0x2071) ==\n");
    for (int d = 0; d < 8; ++d) dumpCursor(art, static_cast<u16>(0x206A + d), "peace");
    dumpCursor(art, 0x2073, "peace-arr");
    std::printf("== war walk (0x2053..0x205A) ==\n");
    for (int d = 0; d < 8; ++d) dumpCursor(art, static_cast<u16>(0x2053 + d), "war");
    dumpCursor(art, 0x205C, "war-arr");
    return 0;
}
