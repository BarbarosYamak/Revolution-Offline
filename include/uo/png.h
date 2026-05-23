#pragma once

// Debug helper: dump an ARGB1555 framebuffer (one u16 per pixel, as produced by
// the renderer / MiniFB bpp=15) to a PNG file. Self-contained — emits stored
// (uncompressed) DEFLATE so it needs no zlib. Handy for headless visual checks:
// render a frame, Save() it, and inspect the PNG. Not on any hot path.

#include "uo/types.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace uo::png {

namespace detail {

inline u32 Crc32(const u8* p, usize n, u32 c = 0) {
    c = ~c;
    for (usize i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (~(c & 1) + 1));
    }
    return ~c;
}

inline u32 Adler32(const u8* p, usize n) {
    u32 a = 1, b = 0;
    for (usize i = 0; i < n; ++i) { a = (a + p[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

inline void PutBE(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x >> 24));
    v.push_back(static_cast<u8>(x >> 16));
    v.push_back(static_cast<u8>(x >> 8));
    v.push_back(static_cast<u8>(x));
}

inline void Chunk(std::vector<u8>& out, const char* tag, const std::vector<u8>& data) {
    PutBE(out, static_cast<u32>(data.size()));
    const usize s = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), data.begin(), data.end());
    PutBE(out, Crc32(&out[s], 4 + data.size()));
}

}  // namespace detail

// Write the w*h ARGB1555 framebuffer at `fb` to `path` as a 24-bit RGB PNG.
inline bool Save(const char* path, const u16* fb, int w, int h) {
    std::vector<u8> raw;
    raw.reserve(static_cast<usize>(w * 3 + 1) * h);
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);  // filter byte: none
        for (int x = 0; x < w; ++x) {
            const u16 p = fb[static_cast<usize>(y) * w + x];
            const int r = (p >> 10) & 0x1F, g = (p >> 5) & 0x1F, b = p & 0x1F;
            raw.push_back(static_cast<u8>((r << 3) | (r >> 2)));
            raw.push_back(static_cast<u8>((g << 3) | (g >> 2)));
            raw.push_back(static_cast<u8>((b << 3) | (b >> 2)));
        }
    }

    // zlib stream: header + stored (uncompressed) DEFLATE blocks + adler32.
    std::vector<u8> z = {0x78, 0x01};
    usize off = 0;
    const usize n = raw.size();
    do {
        const usize blk = std::min<usize>(65535, n - off);
        z.push_back(off + blk >= n ? 1 : 0);  // BFINAL on the last block
        z.push_back(blk & 0xFF);        z.push_back((blk >> 8) & 0xFF);
        z.push_back(~blk & 0xFF);       z.push_back((~blk >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + blk);
        off += blk;
    } while (off < n);
    detail::PutBE(z, detail::Adler32(raw.data(), raw.size()));

    std::vector<u8> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<u8> ihdr;
    detail::PutBE(ihdr, static_cast<u32>(w));
    detail::PutBE(ihdr, static_cast<u32>(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8-bit, colour type 2 (RGB)
    detail::Chunk(out, "IHDR", ihdr);
    detail::Chunk(out, "IDAT", z);
    detail::Chunk(out, "IEND", {});

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

}  // namespace uo::png
