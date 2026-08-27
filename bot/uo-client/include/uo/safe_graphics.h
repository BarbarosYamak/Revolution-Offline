#pragma once

// uo/safe_graphics.h -- the crash-proof gateway to the client data tables.
//
// WHY THIS EXISTS
// ---------------
// ClassicUO dies on this shard with IndexOutOfRangeException because it indexes
// the tiledata / art / anim tables with whatever graphic id the server happens
// to send. Revolution runs Renaissance-era (2.0.3) client data, but the server
// scripts still hand out post-Renaissance graphics: a unicorn mount item
// (0x3EB4) is, in this tiledata.mul, a ship "prow" -- so its animId field is
// not a mount body at all, it is whatever bytes live at that offset. Feed that
// number to an animation table sized for real bodies and the process dies.
//
// The rule here is absolute: NO table is ever indexed with an unvalidated
// number, no lookup may fail by crashing, and anything that cannot be resolved
// resolves to a VISIBLE placeholder instead of silently disappearing or taking
// the viewer down. Every accessor below is total: it accepts the full 32-bit
// input domain and always returns a usable value, with the loader pointers
// allowed to be null.
//
// Exception-free and RTTI-free to match the project /EHs-c- /GR- build.

#include "uo/anim.h"
#include "uo/animdata.h"
#include "uo/animinfo.h"
#include "uo/art.h"
#include "uo/hues.h"
#include "uo/texmap.h"
#include "uo/tiledata.h"
#include "uo/types.h"

namespace uo::safegfx {

// ---------------------------------------------------------------------------
// Hard limits of the Renaissance-era data set. These are the ONLY numbers that
// may reach a table index.
// ---------------------------------------------------------------------------

// tiledata.mul: 16384 land + 16384 static entries (see uo/tiledata.h).
constexpr u32 kLandTileCount   = tiledata::kLandCount;    // 0x4000
constexpr u32 kStaticTileCount = tiledata::kStaticCount;  // 0x4000

// anim.mul high-detail index layout (AnimLoader::IndexFor): monsters < 200,
// animals < 400, people 400..999. Body 1000+ has no slot in this era anim.idx
// -- everything above is post-Renaissance and must never be indexed.
constexpr u16 kMaxAnimBody = 999;

// Worn-equipment animation ids live in [0x190, 0x3E7]. The tiledata animId
// field is only meaningful for wearables; on any other tile it is garbage.
constexpr u16 kMinWornAnim = 0x0190;
constexpr u16 kMaxWornAnim = 0x03E7;

// Action groups reserved per body kind in anim.idx: 22 / 13 / 35.
constexpr u8 kMaxActionMonster = 21;
constexpr u8 kMaxActionAnimal  = 12;
constexpr u8 kMaxActionPeople  = 34;

// hues.mul holds 3000 entries; index 0 means "no hue".
constexpr u16 kHueCount = 3000;

// ---------------------------------------------------------------------------
// Sanitizers. Each maps an arbitrary input onto a value that is guaranteed
// safe to index with, or onto 0 meaning "refuse".
// ---------------------------------------------------------------------------

// Largest action group id that exists for this body kind.
inline u8 MaxActionFor(u16 body) {
    if (body < 200) return kMaxActionMonster;
    if (body < 400) return kMaxActionAnimal;
    return kMaxActionPeople;
}

// Is this a body anim.mul can actually address in this era?
inline bool BodyInRange(u32 body) { return body >= 1u && body <= kMaxAnimBody; }

// THE 0x3EB4 DEFENCE. The tiledata animId is the mount body only when the tile
// really is a mount item. On a Renaissance tiledata a post-Renaissance mount
// graphic lands on some unrelated tile (0x3EB4 is a ship prow here) whose
// animId is arbitrary. Anything that is not a body this data set can draw is
// refused -- the rider is then drawn on foot rather than the process dying.
inline u16 SanitizeMountBody(u32 animIdFromTileData) {
    return BodyInRange(animIdFromTileData) ? static_cast<u16>(animIdFromTileData) : 0u;
}

// Worn-item animation id from tiledata; 0 = not drawable as equipment.
inline u16 SanitizeWornAnim(u32 animIdFromTileData) {
    return (animIdFromTileData >= kMinWornAnim && animIdFromTileData <= kMaxWornAnim)
               ? static_cast<u16>(animIdFromTileData)
               : 0u;
}

inline u8 SanitizeDir(u32 dir) { return static_cast<u8>(dir & 7u); }

inline u8 SanitizeAction(u32 body, u32 action) {
    const u8 cap = MaxActionFor(static_cast<u16>(body));
    return action > cap ? 0u : static_cast<u8>(action);
}

// hues.mul index. The wire carries flags in the high bits (0x8000 = partial
// hue); strip them, then refuse anything past the table.
inline u16 SanitizeHue(u32 hue) {
    const u32 h = hue & 0x3FFFu;
    return h < kHueCount ? static_cast<u16>(h) : 0u;
}

// ---------------------------------------------------------------------------
// The visible placeholder.
//
// A 20x20 magenta/black checker with a solid magenta border, in ARGB1555. It
// is deliberately loud: an operator watching the shard should SEE that the
// server sent a graphic this client data set cannot draw, at the tile where it
// happened, instead of the object silently not existing.
// ---------------------------------------------------------------------------
constexpr u16 kPlaceholderInk  = 0xFC1F;  // opaque magenta
constexpr u16 kPlaceholderVoid = 0x8000;  // opaque black
constexpr int kPlaceholderDim  = 20;

inline const art::Sprite& Placeholder() {
    static const art::Sprite s = [] {
        art::Sprite p;
        p.width  = static_cast<u16>(kPlaceholderDim);
        p.height = static_cast<u16>(kPlaceholderDim);
        p.px.assign(static_cast<usize>(kPlaceholderDim) * kPlaceholderDim,
                    kPlaceholderVoid);
        for (int y = 0; y < kPlaceholderDim; ++y) {
            for (int x = 0; x < kPlaceholderDim; ++x) {
                const bool border = (x == 0 || y == 0 ||
                                     x == kPlaceholderDim - 1 ||
                                     y == kPlaceholderDim - 1);
                const bool check = (((x >> 2) ^ (y >> 2)) & 1) != 0;
                if (border || check)
                    p.px[static_cast<usize>(y) * kPlaceholderDim + x] = kPlaceholderInk;
            }
        }
        return p;
    }();
    return s;
}

// ---------------------------------------------------------------------------
// Audit counters -- what the guards actually caught. The viewer prints these,
// and the ctest asserts on them.
// ---------------------------------------------------------------------------
struct Audit {
    u64 lookups       = 0;
    u64 tileClamped   = 0;   // tiledata index outside 0..0x3FFF
    u64 artMissing    = 0;   // art index has no bitmap -> placeholder
    u64 bodyRejected  = 0;   // body id anim.mul cannot address
    u64 actionClamped = 0;   // action group outside the body kind range
    u64 mountRejected = 0;   // tiledata animId was not a drawable mount body
    u64 wornRejected  = 0;   // tiledata animId was not a drawable worn anim
    u64 hueRejected   = 0;   // hue index past hues.mul
    u64 nullTable     = 0;   // a loader was absent entirely

    void Reset() { *this = Audit{}; }
    u64 Guarded() const {
        return tileClamped + artMissing + bodyRejected + actionClamped +
               mountRejected + wornRejected + hueRejected + nullTable;
    }
};

// ---------------------------------------------------------------------------
// SafeTables -- every table lookup the renderer needs, made total.
//
// Every pointer may be null. Every id may be anything. Nothing here can index
// out of bounds, dereference null, or fail loudly.
// ---------------------------------------------------------------------------
class SafeTables {
public:
    SafeTables() = default;

    void Bind(tiledata::TileDataLoader* td,
              art::ArtLoader* artL,
              hues::HuesLoader* huesL,
              anim::AnimLoader* animL,
              animdata::AnimDataLoader* animDataL,
              animinfo::AnimInfoLoader* animInfoL,
              texmap::TexmapLoader* texL) {
        td_ = td; art_ = artL; hues_ = huesL; anim_ = animL;
        animData_ = animDataL; animInfo_ = animInfoL; tex_ = texL;
    }

    Audit& audit() { return audit_; }
    const Audit& audit() const { return audit_; }

    // --- tiledata ---------------------------------------------------------
    const tiledata::StaticTile& StaticTile(u32 id) {
        ++audit_.lookups;
        if (id >= kStaticTileCount) { ++audit_.tileClamped; return ZeroStatic(); }
        if (!td_ || !td_->IsLoaded()) { ++audit_.nullTable; return ZeroStatic(); }
        return td_->Static(id);
    }

    const tiledata::LandTile& LandTile(u32 id) {
        ++audit_.lookups;
        if (id >= kLandTileCount) { ++audit_.tileClamped; return ZeroLand(); }
        if (!td_ || !td_->IsLoaded()) { ++audit_.nullTable; return ZeroLand(); }
        return td_->Land(id);
    }

    // --- art --------------------------------------------------------------
    // Never returns null: an unresolvable graphic becomes the placeholder.
    const art::Sprite& StaticArt(u32 id) {
        ++audit_.lookups;
        if (id > 0xFFFFu) {
            ++audit_.tileClamped; ++audit_.artMissing; return Placeholder();
        }
        if (!art_ || !art_->IsOpen()) {
            ++audit_.nullTable; ++audit_.artMissing; return Placeholder();
        }
        const art::Sprite* s = art_->Static(static_cast<u16>(id));
        if (!s || !s->width || s->px.empty()) { ++audit_.artMissing; return Placeholder(); }
        return *s;
    }

    const art::Sprite& LandArt(u32 id) {
        ++audit_.lookups;
        if (id >= kLandTileCount) {
            ++audit_.tileClamped; ++audit_.artMissing; return Placeholder();
        }
        if (!art_ || !art_->IsOpen()) {
            ++audit_.nullTable; ++audit_.artMissing; return Placeholder();
        }
        const art::Sprite* s = art_->Land(static_cast<u16>(id));
        if (!s || !s->width || s->px.empty()) { ++audit_.artMissing; return Placeholder(); }
        return *s;
    }

    // --- anim -------------------------------------------------------------
    // nullptr means "do not draw a body"; the caller draws a marker instead.
    // No body id, action or facing ever reaches anim.idx unvalidated.
    const anim::Frame* BodyFrame(u32 body, u32 dir, u32 action, u32 frame) {
        ++audit_.lookups;
        if (!BodyInRange(body)) { ++audit_.bodyRejected; return nullptr; }
        if (!anim_ || !anim_->IsOpen()) { ++audit_.nullTable; return nullptr; }
        const u8 act = SanitizeAction(body, action);
        if (act != action) ++audit_.actionClamped;
        const u16 fr = frame > 0xFFFFu ? 0u : static_cast<u16>(frame);
        return anim_->Body(static_cast<u16>(body), SanitizeDir(dir), act, fr);
    }

    u32 BodyFrameCount(u32 body, u32 dir, u32 action) {
        ++audit_.lookups;
        if (!BodyInRange(body)) { ++audit_.bodyRejected; return 0u; }
        if (!anim_ || !anim_->IsOpen()) { ++audit_.nullTable; return 0u; }
        const u8 act = SanitizeAction(body, action);
        if (act != action) ++audit_.actionClamped;
        return anim_->FrameCount(static_cast<u16>(body), SanitizeDir(dir), act);
    }

    // Mount body from a layer-25 item graphic. THE unicorn case.
    u16 MountBodyFor(u32 mountGraphic) {
        const tiledata::StaticTile& st = StaticTile(mountGraphic);
        const u16 body = SanitizeMountBody(st.animId);
        if (!body) ++audit_.mountRejected;
        return body;
    }

    // Worn-equipment anim from an item graphic.
    u16 WornAnimFor(u32 itemGraphic) {
        const tiledata::StaticTile& st = StaticTile(itemGraphic);
        const u16 a = SanitizeWornAnim(st.animId);
        if (!a) ++audit_.wornRejected;
        return a;
    }

    u8 MoveFrameCount(u32 body, bool running) {
        ++audit_.lookups;
        if (!BodyInRange(body)) { ++audit_.bodyRejected; return running ? 2u : 4u; }
        if (!animInfo_ || !animInfo_->IsLoaded()) {
            ++audit_.nullTable; return running ? 2u : 4u;
        }
        const u8 n = animInfo_->MoveFrameCount(static_cast<u16>(body), running);
        return n ? n : 1u;
    }

    // --- animdata ---------------------------------------------------------
    u8 AnimFrameOffset(u32 itemId, u32 tick) {
        ++audit_.lookups;
        if (itemId >= kStaticTileCount) { ++audit_.tileClamped; return 0u; }
        if (!animData_ || !animData_->IsLoaded()) { ++audit_.nullTable; return 0u; }
        return animData_->FrameOffset(static_cast<u16>(itemId), tick);
    }

    // --- hues -------------------------------------------------------------
    u16 Remap(u16 pixel, u32 hue) {
        ++audit_.lookups;
        const u16 h = SanitizeHue(hue);
        if ((hue & 0x3FFFu) != h) ++audit_.hueRejected;
        if (!h) return pixel;
        if (!hues_ || !hues_->IsLoaded()) { ++audit_.nullTable; return pixel; }
        return hues_->Remap(pixel, h);
    }

    // --- texmaps ----------------------------------------------------------
    const texmap::Texture* LandTexture(u32 textureId) {
        ++audit_.lookups;
        if (textureId > 0xFFFFu) { ++audit_.tileClamped; return nullptr; }
        if (!tex_ || !tex_->IsOpen()) { ++audit_.nullTable; return nullptr; }
        return tex_->Get(static_cast<u16>(textureId));
    }

private:
    static const tiledata::StaticTile& ZeroStatic() {
        static const tiledata::StaticTile z{};
        return z;
    }
    static const tiledata::LandTile& ZeroLand() {
        static const tiledata::LandTile z{};
        return z;
    }

    tiledata::TileDataLoader* td_       = nullptr;
    art::ArtLoader*           art_      = nullptr;
    hues::HuesLoader*         hues_     = nullptr;
    anim::AnimLoader*         anim_     = nullptr;
    animdata::AnimDataLoader* animData_ = nullptr;
    animinfo::AnimInfoLoader* animInfo_ = nullptr;
    texmap::TexmapLoader*     tex_      = nullptr;
    Audit                     audit_{};
};

// Graphics known to have killed a client on this shard. Kept as data so the
// regression is named, not folklore.
struct KnownHostileGraphic { u16 graphic; const char* note; };
constexpr KnownHostileGraphic kKnownHostileGraphics[] = {
    {0x3EB4, "unicorn mount item; a ship prow in Revolution 2.0.3 tiledata "
             "-- killed ClassicUO on every launch"},
    {0x3EA0, "post-Renaissance mount item block; not a mount in this tiledata"},
    {0x3EB5, "adjacent post-Renaissance mount item"},
    {0x3EC5, "post-Renaissance mount item"},
    {0x3FFF, "last static tile id -- boundary"},
    {0x4000, "first id PAST the static tiledata table -- must clamp"},
    {0xFFFF, "maximum u16 graphic -- must clamp"},
};

}  // namespace uo::safegfx
