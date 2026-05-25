#include "uo/light.h"

namespace uo::light {

bool LightLoader::Open(const char* lightIdxPath, const char* lightPath) {
    return idx_.Open(lightIdxPath) && light_.Open(lightPath);
}

const Shape* LightLoader::Get(u16 lightId) {
    auto it = cache_.find(lightId);
    if (it != cache_.end())
        return it->second.width ? &it->second : nullptr;
    return Load(lightId);
}

const Shape* LightLoader::Load(u16 lightId) {
    Shape& s = cache_[lightId];   // inserts an empty (0x0) shape

    // 12-byte index entry: { u32 lookup, u32 length, u32 extra }. For light.mul
    // the data is a RAW intensity bitmap (no RLE); `extra` packs the dimensions
    // (low word / high word). The client reads w/h straight out of the idx
    // entry — see g_LightIdx[lightId*12 + 8/10].
    if (!idx_.Seek(static_cast<i64>(lightId) * 12, 0)) return nullptr;
    u32 entry[3];
    if (!idx_.Read(entry, sizeof(entry))) return nullptr;
    const u32 lookup = entry[0];
    const u32 length = entry[1];
    const u32 extra  = entry[2];
    if (lookup == 0xFFFFFFFFu || length == 0 || length > (1u << 20)) return nullptr;

    u16 w = static_cast<u16>(extra & 0xFFFF);
    u16 h = static_cast<u16>((extra >> 16) & 0xFFFF);
    // Self-check: light.mul is uncompressed, so width*height must equal length.
    // If `extra`'s word order is the other way for this file, swap; if it still
    // disagrees, fall back to a single row so we never read out of bounds.
    if (static_cast<u32>(w) * h != length) {
        if (static_cast<u32>(h) * w == length) { const u16 t = w; w = h; h = t; }
        else if (w != 0 && (length % w) == 0)  { h = static_cast<u16>(length / w); }
        else { w = static_cast<u16>(length); h = 1; }
    }
    if (w == 0 || h == 0) return nullptr;

    s.px.assign(length, 0);
    if (!light_.Seek(static_cast<i64>(lookup), 0)) { s.px.clear(); return nullptr; }
    if (!light_.Read(s.px.data(), length))        { s.px.clear(); return nullptr; }
    s.width  = w;
    s.height = h;
    return &s;
}

}
