#include "uo/texmap.h"

namespace uo::texmap {

bool TexmapLoader::Open(const char* idxPath, const char* mapPath) {
    return idx_.Open(idxPath) && map_.Open(mapPath);
}

const Texture* TexmapLoader::Get(u16 textureId) {
    auto it = cache_.find(textureId);
    if (it != cache_.end())
        return it->second.size ? &it->second : nullptr;

    Texture& t = cache_[textureId];   // inserts an empty (size 0) texture

    // 12-byte index entry: { u32 lookup, u32 length, u32 extra }.
    if (!idx_.Seek(static_cast<i64>(textureId) * 12, 0)) return nullptr;
    u32 entry[3];
    if (!idx_.Read(entry, sizeof(entry))) return nullptr;
    const u32 lookup = entry[0];
    const u32 length = entry[1];
    if (lookup == 0xFFFFFFFFu || length == 0) return nullptr;

    // 64x64 = 8192 bytes, 128x128 = 32768 bytes.
    const u16 size = (length >= 128u * 128u * 2u) ? 128 : 64;
    const u32 need = static_cast<u32>(size) * size * 2u;
    if (length < need) return nullptr;

    t.px.resize(static_cast<usize>(size) * size);
    if (!map_.Seek(static_cast<i64>(lookup), 0)) { t.px.clear(); return nullptr; }
    if (!map_.Read(t.px.data(), need)) { t.px.clear(); return nullptr; }
    t.size = size;
    return &t;
}

}
