#pragma once

#include "uo/mul.h"
#include "uo/types.h"

#include <unordered_map>
#include <vector>

namespace uo::light {

// A light shape from light.mul: a raw, row-major intensity bitmap (one byte per
// texel, ~0..31). It is the BAKED radial falloff the client subtracts from the
// per-pixel darkness map — there is no runtime distance/falloff math. Mirrors
// Light_ApplyToRect_LoRes @0x40F660 in client_2.0.7 (the non-colored mode).
struct Shape {
    u16 width  = 0;
    u16 height = 0;
    std::vector<u8> px;   // intensity, row-major, width*height bytes
};

// Loads lightidx.mul / light.mul and returns shapes on demand, cached by light
// id. The light id for a static/item is its tiledata renderDimIndex (the same
// field the client feeds into g_LightIdx). Shapes are tiny and few (~100).
class LightLoader {
public:
    bool Open(const char* lightIdxPath, const char* lightPath);
    bool IsOpen() const { return idx_.IsOpen() && light_.IsOpen(); }

    // nullptr if the id is empty / out of range.
    const Shape* Get(u16 lightId);

private:
    const Shape* Load(u16 lightId);

    mul::File idx_;
    mul::File light_;
    std::unordered_map<u16, Shape> cache_;
};

}
