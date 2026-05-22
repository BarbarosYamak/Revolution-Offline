#pragma once

#include "uo/mul.h"
#include "uo/types.h"

#include <unordered_map>
#include <vector>

namespace uo::art {

// A decoded art bitmap in the client's native ARGB1555 (bit15 = opacity).
// A pixel of 0 is transparent; any nonzero value is opaque. Row-major.
struct Sprite {
    u16 width  = 0;
    u16 height = 0;
    std::vector<u16> px;
};

// Loads art.mul / artidx.mul and decodes tiles on demand. Land tiles are
// indexed directly (0..0x3FFF); static items live at 0x4000 + itemId. Decoded
// sprites are cached by art index. An "empty" index caches a 0x0 sprite and
// returns nullptr.
class ArtLoader {
public:
    bool Open(const char* artIdxPath, const char* artPath);
    bool IsOpen() const { return idx_.IsOpen() && art_.IsOpen(); }

    const Sprite* Land(u16 tileId);
    const Sprite* Static(u16 itemId);

private:
    const Sprite* LoadIndex(u32 index, bool isLand);

    mul::File idx_;
    mul::File art_;
    std::unordered_map<u32, Sprite> cache_;
};

}
