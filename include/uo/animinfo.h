#pragma once

#include "uo/types.h"

#include <array>

namespace uo::animinfo {

// animinfo.mul stores two movement timing bytes per body:
// [walk frame ticks, run frame ticks]. The 2.0.7 client defaults to 4/2 when
// the file is missing and consumes one tick every 76ms render update.
class AnimInfoLoader {
public:
    bool Load(const char* path);
    bool IsLoaded() const { return loaded_; }

    u8 MoveFrameCount(u16 body, bool running) const;

private:
    struct Entry {
        u8 walk = 4;
        u8 run = 2;
    };

    bool loaded_ = false;
    std::array<Entry, 1000> entries_{};
};

} // namespace uo::animinfo
