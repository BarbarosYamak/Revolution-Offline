#pragma once

#include "uo/anim.h"
#include "uo/art.h"
#include "uo/map.h"
#include "uo/texmap.h"
#include "uo/tiledata.h"
#include "uo/types.h"

#include <vector>

namespace uo::render {

// A server-sent world item to draw as static art (lamp posts, doors, ...).
// gfxOffset is the 0x1A graphic-increment byte (present when graphic&0x8000):
// the drawn art is `itemId + gfxOffset`. The client stores this as
// directionByte and adds it to the graphic in sub_405290 — that's how a door
// picks its open/closed/hinge frame. (The separate 0x1A *direction*/facing byte
// is NOT added to the art.)
struct DynItem { u16 itemId; i32 x; i32 y; i8 z; u8 gfxOffset = 0; };

// A mobile (player/NPC) to draw as a still body frame. No animation yet; the
// facing picks the frame's direction. isPlayer flags the local player (used to
// compute the roof cutoff so we can see ourselves inside a building).
struct Mob { u16 body; i32 x; i32 y; i8 z; u8 dir; bool isPlayer; };

// Software isometric rasterizer. Produces an ARGB1555 framebuffer (one u16 per
// pixel) centered on a world cell, drawing land terrain and static art with a
// painter's-algorithm order. No windowing — hand Frame() to mfb_update.
class Renderer {
public:
    Renderer(int width, int height);

    void RenderWorld(map::Map& map, art::ArtLoader& art,
                     const tiledata::TileDataLoader& td, texmap::TexmapLoader& tex,
                     i32 camX, i32 camY,
                     const DynItem* items = nullptr, usize nItems = 0,
                     anim::AnimLoader* anim = nullptr,
                     const Mob* mobs = nullptr, usize nMobs = 0);

    const u16* Frame() const { return fb_.data(); }
    int Width()  const { return w_; }
    int Height() const { return h_; }

    // Screen + art-texture coordinate, used to stretch land tiles.
    struct TexVert { int x, y, u, v; };

private:
    void Blit(const art::Sprite& s, int dx, int dy);
    void BlitRaw(const u16* src, int sw, int sh, int dx, int dy, bool skipTransparent);
    // Affine-textured triangle (a,b,c). Samples src (texW x texH, row-major) at
    // each vertex's (u,v). When skipTransparent, source texels of 0 are left
    // alone (for the diamond land art); texmaps draw fully opaque. Used to warp
    // a land tile onto a quad whose corners follow the four corner cells' z.
    void TexTri(const u16* src, int texW, int texH, bool skipTransparent,
                TexVert a, TexVert b, TexVert c);

    int w_;
    int h_;
    std::vector<u16> fb_;
};

}
