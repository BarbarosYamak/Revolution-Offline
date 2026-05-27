#pragma once

#include "uo/anim.h"
#include "uo/animdata.h"
#include "uo/art.h"
#include "uo/hues.h"
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
struct DynItem { u16 itemId; i32 x; i32 y; i8 z; u8 gfxOffset = 0; u16 hue = 0;
                 // Server object serial, carried through so PickObject can map a
                 // clicked sprite back to the entity (0 = not pickable).
                 u32 serial = 0; };

struct EquipAnim {
    u16 anim = 0;
    u16 hue = 0;
};

// A mobile (player/NPC) to draw. `dir` picks the facing; `action`/`frame` select
// the animation group and the frame within its cycle (chosen Client-side from
// movement state + the render clock). isPlayer flags the local player (used to
// compute the roof cutoff so we can see ourselves inside a building).
// equipAnims are worn-item animation ids, already in back-to-front draw order;
// each is drawn over the body at the SAME action/frame, using its own frame
// anchor.
// ddx/ddy interpolate the on-screen position between the previous and current
// cell during a step (in cells, added to x/y; 0 when not mid-step), so the
// sprite slides in sync with the walk cycle instead of teleporting. The local
// player's ddx/ddy scrolls the whole scene (camera follows it) and the player
// stays centred; other mobiles slide by their own offset on top.
struct Mob {
    u16 body; i32 x; i32 y; i8 z; u8 dir; bool isPlayer;
    u16 hue = 0;
    std::vector<EquipAnim> equipAnims;
    u8 action = 0; u16 frame = 0;
    float ddx = 0.0f; float ddy = 0.0f;
    // Item graphic of a held/worn light source (torch, lantern), -1 = none.
    // Set Client-side from the equipped item when its tiledata is flagged a
    // light source, so a carried torch casts a moving, classified light pool.
    int light = -1;
    // Mount/seat body drawn UNDER the rider (0 = on foot). Set from the layer-25
    // mount-equip item's tiledata animId. When mounted, the rider's action/frame
    // above are the seated ride group (23/24), so a mounted/seated mobile sits.
    // mountAction/mountFrame animate the mount body itself. A chair-only seat has
    // no body anim (mountBody 0) — the rider still sits, the chair is a world static.
    int mountBody = 0; u8 mountAction = 0; u16 mountFrame = 0;
    // Sitting on a chair (client-side detection, ported from the 2.0.7
    // g_SittingChairTable @0x55DB68). When set, `dir` has been overridden to the
    // chair's facing and the body is drawn shifted by sitOffsetY onto the seat.
    bool sitting = false; int sitOffsetY = 0;
    // Mobile serial, carried through for PickObject (its body, mount and worn
    // layers all report this serial when clicked).
    u32 serial = 0;
    // Mouse-over highlight: when set, EVERY layer (body, mount, worn gear) is
    // drawn with the highlight hue, not just the bare body — matching the client
    // (Mobile_OnRender @0x406FE0 forces the draw hue across all layers when the
    // serial matches g_ContextActionTargetSerial). Otherwise a clothed humanoid
    // only lit up on its exposed skin.
    bool highlight = false;
};

// Software isometric rasterizer. Produces an ARGB1555 framebuffer (one u16 per
// pixel) centered on a world cell, drawing land terrain and static art with a
// painter's-algorithm order. No windowing — hand Frame() to mfb_update.
class Renderer {
public:
    Renderer(int width, int height);

    void RenderWorld(map::Map& map, art::ArtLoader& art,
                     const tiledata::TileDataLoader& td, texmap::TexmapLoader& tex,
                     i32 camX, i32 camY, i32 camZ = 0,
                     const DynItem* items = nullptr, usize nItems = 0,
                     animdata::AnimDataLoader* animData = nullptr, u32 animTick = 0,
                     hues::HuesLoader* hues = nullptr,
                     anim::AnimLoader* anim = nullptr,
                     const Mob* mobs = nullptr, usize nMobs = 0,
                     int ambientDarkness = 0);

    const u16* Frame() const { return fb_.data(); }
    int Width()  const { return w_; }
    int Height() const { return h_; }

    // Composite a small ARGB1555 buffer (sw x sh, row-major) onto the frame at
    // (dx,dy), clipped to the framebuffer. Opaque copy — every source pixel is
    // written. Used to stamp the minimap panel over the world after RenderWorld.
    void Overlay(const u16* src, int sw, int sh, int dx, int dy);
    void FillRect(int x, int y, int w, int h, u16 color);
    void BlendRGBA(const u32* bgra, int sw, int sh, int dx, int dy);


    // Blit a sprite onto the frame at (dx,dy) skipping transparent (0) pixels
    // AND any pixel equal to `key` (a chroma-key background). UO cursor art
    // fills its background with 0x001F (blue), so the cursor passes its corner
    // colour as the key. Used for the software mouse cursor on top of all.
    // When skipHotspotMarker is set, pure-green pixels (0x03E0) are also skipped:
    // UO cursor art embeds two such marker pixels (on the top row and left
    // column) to encode the click hotspot — they must not be drawn.
    void BlitSpriteKeyed(const u16* src, int sw, int sh, int dx, int dy, u16 key,
                         bool skipHotspotMarker = false);

    // Inverse of the isometric world projection: map a framebuffer pixel back to
    // a world cell, assuming the clicked ground sits at the camera's elevation
    // (the per-tile z offset cancels for same-z ground; slopes are approximate —
    // good enough for a navigation goal that A* then resolves). camX/camY is the
    // cell the view is centred on (the player). Writes the resulting cell.
    // Topmost world entity (server item or mobile) whose drawn sprite covers
    // screen pixel (sx,sy). Faithful to the client's World_GetObjectAtScreenLoc
    // @0x409AB0: it walks the last RenderWorld draw list front-to-back and
    // returns the first object whose sprite alpha-mask is non-transparent under
    // the cursor — a real PICK, not a math inversion. Only items/mobiles are
    // pickable (map land/statics have no serial). Returns 0 when nothing is hit;
    // sets *mobile true when the hit was a mobile (vs a server item).
    u32 PickObject(int sx, int sy, bool* mobile = nullptr) const;

    void ScreenToWorld(int sx, int sy, i32 camX, i32 camY, i32* outX, i32* outY) const;
    void WorldToScreen(i32 worldX, i32 worldY, i8 z,
                       i32 camX, i32 camY, i32 camZ,
                       int* outSx, int* outSy) const;

    // Screen + art-texture coordinate, used to stretch land tiles.
    struct TexVert { int x, y, u, v; };

private:
    void Blit(const art::Sprite& s, int dx, int dy);
    void BlitRaw(const u16* src, int sw, int sh, int dx, int dy,
                 bool skipTransparent, hues::HuesLoader* hues = nullptr, u16 hue = 0);
    // Affine-textured triangle (a,b,c). Samples src (texW x texH, row-major) at
    // each vertex's (u,v). When skipTransparent, source texels of 0 are left
    // alone (for the diamond land art); texmaps draw fully opaque. Used to warp
    // a land tile onto a quad whose corners follow the four corner cells' z.
    void TexTri(const u16* src, int texW, int texH, bool skipTransparent,
                TexVert a, TexVert b, TexVert c);

    // A collected point light: a screen-space center + the emitter's item
    // graphic (used to classify color/radius). Gathered during the draw.
    struct LightSrc { int sx, sy; u16 graphic; };

    // Procedural night lighting. Builds a per-pixel 3-channel "darkness" map
    // (0 = full bright .. 31 = near black per R/G/B) seeded with
    // `ambientDarkness`, then SUBTRACTS a smooth radial corona for each light
    // (color + radius classified from its graphic — warm for fire/candles,
    // white for lamps), and composites with the linear curve `ch*(32-d)/32`.
    // No-op when fully lit. Runs over the world frame only; HUD/minimap are
    // stamped afterwards at full brightness.
    void ApplyLighting(int ambientDarkness, const std::vector<LightSrc>& srcs);

    // One pickable sprite from the last RenderWorld, stored in draw order
    // (back-to-front). PickObject scans these in reverse for the topmost hit.
    struct PickEntry { u32 serial; bool mobile; int dx, dy, sw, sh; const u16* src; };

    int w_;
    int h_;
    std::vector<u16> fb_;
    std::vector<u8>  dark_;   // per-pixel RGB darkness scratch (3*w*h, reused)
    std::vector<PickEntry> picks_;  // pickable sprites from the last RenderWorld
};

}
