#pragma once

#include "uo/mul.h"
#include "uo/types.h"

#include <unordered_map>
#include <vector>

namespace uo::anim {

// A decoded body animation frame.
//
// The official client (Mobile_RenderToCacheFromAnimMul @0x4D11D0,
// AnimFrame_DrawCommandStream @0x4D0040) decodes EVERY body into a fixed
// 64x128 ARGB1555 scratch buffer (memset 0x4000 = 64*128 u16) with the command
// origin at (32, 80) and SKIPS the per-frame {cx,cy,w,h} header (a4 = frame+8),
// so in 2.0.7 a large body (dragon) or a tall one (horse head) is silently
// clipped to that box. We keep the same command coordinate space (col = 32+x,
// row = 80+y) but size the canvas to the actual decoded-pixel bounding box, so
// the whole sprite is kept. `anchorX/anchorY` is the canvas pixel that maps to
// the command origin (32,80); the renderer places it at the cell-floor centre,
// reproducing the original on-screen position for content that already fit.
// 0 = transparent. Directions 5..7 are stored mirrored.
struct Frame {
    static constexpr int kOriginX = 32;   // command x-origin (matches client)
    static constexpr int kOriginY = 80;   // command y-origin (matches client)
    int width  = 0;
    int height = 0;
    int anchorX = 0;   // canvas col mapping to the command origin
    int anchorY = 0;   // canvas row mapping to the command origin
    std::vector<u16> px;   // width*height, row-major; empty == no frame
};

// One action group of a body for one facing: every frame of the cycle, decoded.
// UO authors body and worn-equipment groups with matching frame counts per
// action, so a body frame index addresses the same pose on every layer.
struct Group {
    std::vector<Frame> frames;   // empty == group unavailable (negative-cached)
};

// Loads anim.idx / anim.mul (high-detail body animations) and decodes the frames
// of a body's action group for a given facing. NO equipment layering, mounts,
// body.def / bodyconv.def remapping — just the base body. Hue is applied later
// by the renderer from the decoded pixels.
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
    // action = animation group (walk/stand/run/...; default 0). frame indexes
    // into the group and is clamped to its size. Returns nullptr if the group is
    // empty/unavailable. Each frame fits its own content (anchor 32,80).
    const Frame* Body(u16 body, u8 dir, u8 action = 0, u16 frame = 0);

    // Number of frames in a body's action group for a facing (0 if unavailable).
    u32 FrameCount(u16 body, u8 dir, u8 action = 0);

private:
    static u32 IndexFor(u16 body, u8 action, u8 storedDir);
    // Decode one frame's command stream (starting at frameBase, an 8-byte header
    // then commands) into a content-fitted bitmap. Returns false on a bad/empty
    // frame. `mirror` flips dirs 5..7 horizontally.
    static bool DecodeFrame(const std::vector<u8>& raw, const u8* pal,
                            usize frameBase, bool mirror, Frame& out);
    const Group* LoadGroup(u32 key, u16 body, u8 action, u8 dir);
    const Group* GetGroup(u16 body, u8 dir, u8 action);

    mul::File idx_;
    mul::File mul_;
    std::unordered_map<u32, Group> cache_;   // keyed by packed (body,action,dir)
};

}
