#include "uo/hues.h"

namespace uo::hues {

namespace {
constexpr u32 kHueCount = 3000;
constexpr u32 kGroupSize = 8;
}

bool HuesLoader::Load(const char* path) {
    loaded_ = false;
    entries_.clear();

    mul::File f;
    if (!path || !f.Open(path)) return false;

    entries_.resize(kHueCount);
    for (u32 i = 0; i < kHueCount; ++i) {
        if ((i % kGroupSize) == 0 && !f.Seek(4, 1)) {
            entries_.clear();
            return false;
        }
        for (u16& c : entries_[i].colors) {
            if (!f.Read(&c, sizeof(c))) {
                entries_.clear();
                return false;
            }
            c &= 0x7FFFu;
            if (c == 0) c = 1;
        }
        u16 start = 0;
        u16 end = 0;
        char name[20];
        if (!f.Read(&start, sizeof(start)) || !f.Read(&end, sizeof(end)) ||
            !f.Read(name, sizeof(name))) {
            entries_.clear();
            return false;
        }
    }

    loaded_ = true;
    return true;
}

u16 HuesLoader::Remap(u16 pixel, u16 hue) const {
    hue &= 0x3FFFu;
    if (!pixel || !loaded_ || hue == 0 || hue >= entries_.size()) return pixel;
    const u16 idx = static_cast<u16>((pixel >> 10) & 0x1Fu);
    return static_cast<u16>(0x8000u | entries_[hue].colors[idx]);
}

}
