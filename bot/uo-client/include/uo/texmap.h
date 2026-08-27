#pragma once

#include "uo/mul.h"
#include "uo/types.h"

#include <unordered_map>
#include <vector>

namespace uo::texmap {

// A land texture: a square (64x64 or 128x128) ARGB1555 bitmap, fully opaque.
// The official client stretches these across sloped land tiles (the smooth
// coastlines etc.) instead of the flat 44x44 art.
struct Texture {
    u16 size = 0;            // 64 or 128 (width == height)
    std::vector<u16> px;     // size*size, row-major
};

// Loads texidx.mul / texmaps.mul and decodes textures on demand by the land
// tile's textureId (tiledata LandTile.textureId).
class TexmapLoader {
public:
    bool Open(const char* idxPath, const char* mapPath);
    bool IsOpen() const { return idx_.IsOpen() && map_.IsOpen(); }

    const Texture* Get(u16 textureId);   // nullptr if the id has no texture

private:
    mul::File idx_;
    mul::File map_;
    std::unordered_map<u32, Texture> cache_;
};

}
