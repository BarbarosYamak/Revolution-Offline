#include "uo/animdata.h"

#include "uo/mul.h"

namespace uo::animdata {

bool AnimDataLoader::Load(const char* path) {
    loaded_ = false;
    for (Entry& e : entries_) e = Entry{};

    mul::File f;
    if (!path || !f.Open(path)) return false;

    for (u32 i = 0; i < entries_.size(); ++i) {
        if ((i & 7u) == 0u) {
            u8 header[4];
            if (!f.Read(header, sizeof(header))) return false;
        }

        Entry& e = entries_[i];
        u8 tail[4];
        if (!f.Read(e.frames, sizeof(e.frames))) return false;
        if (!f.Read(tail, sizeof(tail))) return false;

        e.frameIndex = tail[0];
        e.frameCount = tail[1];
        e.frameInterval = tail[2];
        e.frameStart = tail[3];
        e.lastTick = 0xFFFFFFFFu;
        if (e.frameCount != 0 && e.frameIndex >= e.frameCount) e.frameIndex = 0;
    }

    loaded_ = true;
    return true;
}

u8 AnimDataLoader::FrameOffset(u16 itemId, u32 tick) {
    if (!loaded_ || itemId >= entries_.size()) return 0;

    Entry& e = entries_[itemId];
    if (e.frameCount == 0 || e.frameInterval == 0) return 0;

    const u32 interval = e.frameInterval;
    if (((tick + e.frameStart) % interval) == 0u && e.lastTick != tick) {
        ++e.frameIndex;
        if (e.frameIndex >= e.frameCount) e.frameIndex = 0;
        e.lastTick = tick;
    }

    return e.frames[e.frameIndex];
}

} // namespace uo::animdata
