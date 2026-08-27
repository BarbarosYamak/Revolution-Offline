#pragma once

#include "uo/types.h"

#include <vector>

namespace uo::render {

// radarcol.mul — 0x10000 little-endian RGB555 colours, one per tile id, exactly
// as the real 2.0.7 client loads it (CRadarGump_Constructor @0x48D660 reads
// 0x10000 WORDs straight from the file). Layout:
//   [0x0000 .. 0x3FFF]  land tile colours
//   [0x4000 + itemId ]  static item colours
// The value 0x7FFF is a sentinel the client paints as light grey
// (Color_PackRGB444(0xF3,0xF3,0xF3)). Land tile id 2 (and any id >= 0x4000) is
// the "no terrain" marker.
class RadarColors {
public:
    bool Load(const char* path);
    bool IsLoaded() const { return loaded_; }

    // Raw RGB555 colour for a land tile. *hasColor is false for the no-terrain
    // markers (id 2 or id >= 0x4000); the colour is then meaningless.
    u16 Land(u16 tileId, bool* hasColor) const;

    // Raw RGB555 colour for a static item (radarcol[0x4000 + itemId]).
    u16 Static(u16 itemId) const;

private:
    std::vector<u16> col_;
    bool loaded_ = false;
};

}
