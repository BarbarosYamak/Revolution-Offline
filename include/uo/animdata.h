#pragma once

#include "uo/types.h"

#include <array>

namespace uo::animdata {

// animdata.mul drives static-art animations. The original client keeps one
// mutable entry per static graphic id; all objects with that graphic share the
// same frame phase, and the selected frame is added to the base item id.
class AnimDataLoader {
public:
    bool Load(const char* path);
    bool IsLoaded() const { return loaded_; }

    u8 FrameOffset(u16 itemId, u32 tick);

private:
    struct Entry {
        u8 frames[64]{};
        u8 frameIndex = 0;
        u8 frameCount = 0;
        u8 frameInterval = 0;
        u8 frameStart = 0;
        u32 lastTick = 0xFFFFFFFFu;
    };

    bool loaded_ = false;
    std::array<Entry, 0x4000> entries_{};
};

} // namespace uo::animdata
