#pragma once

#include "uo/art.h"
#include "uo/map.h"
#include "uo/types.h"

#include <vector>

namespace uo::render {

// Software isometric rasterizer. Produces an ARGB1555 framebuffer (one u16 per
// pixel) centered on a world cell, drawing land terrain and static art with a
// painter's-algorithm order. No windowing — hand Frame() to mfb_update.
class Renderer {
public:
    Renderer(int width, int height);

    void RenderWorld(map::Map& map, art::ArtLoader& art, i32 camX, i32 camY);

    const u16* Frame() const { return fb_.data(); }
    int Width()  const { return w_; }
    int Height() const { return h_; }

private:
    void Blit(const art::Sprite& s, int dx, int dy);

    int w_;
    int h_;
    std::vector<u16> fb_;
};

}
