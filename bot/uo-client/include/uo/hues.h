#pragma once

#include "uo/mul.h"
#include "uo/types.h"

#include <array>
#include <vector>

namespace uo::hues {

class HuesLoader {
public:
    bool Load(const char* path);
    bool IsLoaded() const { return loaded_; }

    // Whole-sprite hue remap used by the practical renderer path. The original
    // client indexes a 32-color hue ramp by the source pixel's red component.
    u16 Remap(u16 pixel, u16 hue) const;

private:
    struct Entry {
        std::array<u16, 32> colors{};
    };

    bool loaded_ = false;
    std::vector<Entry> entries_;
};

}
