#pragma once

#include "uo/mul.h"
#include "uo/types.h"

#include <unordered_map>
#include <vector>

namespace uo::anim {

// A decoded body animation frame. We mirror the official client's scratch
// canvas exactly: a fixed 64x128 ARGB1555 buffer (Mobile_RenderToCacheFromAnimMul
// @0x4D11D0 memsets 0x4000 bytes = 64*128 u16) with the draw anchor at
// (kAnchorX, kAnchorY). 0 = transparent. Directions 5..7 are stored mirrored.
struct Frame {
    static constexpr int kW       = 64;
    static constexpr int kH       = 128;
    static constexpr int kAnchorX = 32;   // command x-origin in the canvas
    static constexpr int kAnchorY = 80;   // command y-origin in the canvas
    std::vector<u16> px;                  // kW*kH, row-major; empty == no frame
};

// Loads anim.idx / anim.mul (high-detail body animations) and decodes a single
// still frame of a body for a given facing. This is groundwork for entity
// rendering: NO equipment layering, mounts, hue shifting, or body.def /
// bodyconv.def remapping — just the base body still frame.
//
// Frame format (AnimFrame_DrawCommandStream @0x4D0040, payload layout from
// Mobile_RenderToCacheFromAnimMul): 256-entry u16 palette, u32 frameCount,
// u32 frameOffset[frameCount], then each frame = { i16 cx, i16 cy, u16 w,
// u16 h } + a command stream. Each command is a u32: x = cmd>>22 (signed 10b),
// y = (cmd<<10)>>22 (signed 10b), run = cmd & 0xFFF, followed by `run` u8
// palette indices. Sentinel 0x7FFF7FFF ends the stream.
class AnimLoader {
public:
    bool Open(const char* idxPath, const char* mulPath);
    bool IsOpen() const { return idx_.IsOpen() && mul_.IsOpen(); }

    // body = mobile graphic (e.g. 0x190 human male). dir = 0..7 facing.
    // action defaults to 0 (first group; frame 0 is a near-standing pose).
    // Returns a 64x128 frame (anchor 32,80) or nullptr if unavailable.
    const Frame* Body(u16 body, u8 dir, u8 action = 0);

private:
    static u32 IndexFor(u16 body, u8 action, u8 storedDir);
    const Frame* Load(u32 key, u16 body, u8 action, u8 dir);

    mul::File idx_;
    mul::File mul_;
    std::unordered_map<u32, Frame> cache_;   // keyed by packed (body,action,dir)
};

}
