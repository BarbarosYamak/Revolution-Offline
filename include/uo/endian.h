#pragma once

#include "uo/types.h"

namespace uo {

// UO protocol is big-endian on the wire.

inline u16 LoadBE16(const u8* p) {
    return static_cast<u16>((u16(p[0]) << 8) | u16(p[1]));
}

inline u32 LoadBE32(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}

// verdata.mul stores its directory and payload offsets as native (little-
// endian) DWORDs — the original client reads them straight into ints.
inline u16 LoadLE16(const u8* p) {
    return static_cast<u16>(u16(p[0]) | (u16(p[1]) << 8));
}

inline u32 LoadLE32(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}

inline void StoreBE16(u8* p, u16 v) {
    p[0] = static_cast<u8>(v >> 8);
    p[1] = static_cast<u8>(v);
}

inline void StoreBE32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v >> 24);
    p[1] = static_cast<u8>(v >> 16);
    p[2] = static_cast<u8>(v >> 8);
    p[3] = static_cast<u8>(v);
}

}
