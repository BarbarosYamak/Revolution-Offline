#include "render/RadarColors.h"

#include "uo/mul.h"

namespace uo::render {

namespace {
constexpr u32 kEntries = 0x10000;   // 65536 colours (file is 0x20000 bytes)
}

bool RadarColors::Load(const char* path) {
    loaded_ = false;
    col_.clear();
    mul::File f;
    if (!f.Open(path)) return false;
    col_.assign(kEntries, 0);
    const usize want = static_cast<usize>(kEntries) * sizeof(u16);
    if (!f.Read(col_.data(), want)) {   // x86 little-endian: raw read matches
        col_.clear();
        return false;
    }
    loaded_ = true;
    return true;
}

u16 RadarColors::Land(u16 tileId, bool* hasColor) const {
    if (tileId >= 0x4000 || tileId == 2) { if (hasColor) *hasColor = false; return 0; }
    if (hasColor) *hasColor = true;
    return col_[tileId];
}

u16 RadarColors::Static(u16 itemId) const {
    const u32 idx = 0x4000u + itemId;
    return idx < col_.size() ? col_[idx] : 0;
}

}
