#pragma once

#include "uo/types.h"

namespace uo::tiledata {

// Layout per ObjectManager_LoadTileData @ 0x4C7AE0 in client_2.0.7.exe.
// tiledata.mul (pre-HSA, ~UO 2.0.7 era):
//   Land section:   512 blocks × (4-byte header + 32 × 26-byte entry)
//                   = 428032 bytes; 16384 entries total
//   Static section: 512 blocks × (4-byte header + 32 × 37-byte entry)
//                   = 608256 bytes; 16384 entries total
//   Total file:     1036288 bytes
//
// Flag bits (`TileFlags`). The low/mid bits (0x1..0x00800000) follow the
// standard MUL layout. The HIGH bits (>= 0x01000000), however, use this
// client's PRE-AOS ("client206") layout, which is shifted one bit UP from the
// modern AOS layout — verified by sampling tiledata.mul:
//   0x10000000 = Roof  (AOS puts Roof at 0x08000000)
//   0x20000000 = Door  (AOS puts Door at 0x10000000)
//   0x40000000 = StairBack,  0x80000000 = StairRight
// (NB: a tree is two statics in one cell — the trunk is just Impassable, but the
//  leaf CANOPY carries kFlagFoliage 0x20000; the renderer draws foliage last so
//  the crown sits over the trunk.)

constexpr u32 kLandCount   = 16384;
constexpr u32 kStaticCount = 16384;

enum TileFlags : u32 {
    kFlagBackground   = 0x00000001,
    kFlagWeapon       = 0x00000002,
    kFlagTransparent  = 0x00000004,
    kFlagTranslucent  = 0x00000008,
    kFlagWall         = 0x00000010,
    kFlagDamaging     = 0x00000020,
    kFlagImpassable   = 0x00000040,
    kFlagWet          = 0x00000080,
    kFlagSurface      = 0x00000200,
    kFlagBridge       = 0x00000400,
    kFlagGeneric      = 0x00000800,
    kFlagWindow       = 0x00001000,
    kFlagNoShoot      = 0x00002000,
    kFlagArticleA     = 0x00004000,
    kFlagArticleAn    = 0x00008000,
    kFlagArticleThe   = 0x00010000,
    kFlagFoliage      = 0x00020000,
    kFlagPartialHue   = 0x00040000,
    kFlagMap          = 0x00100000,
    kFlagContainer    = 0x00200000,
    kFlagWearable     = 0x00400000,
    kFlagLightSource  = 0x00800000,
    kFlagAnimation    = 0x01000000,   // animated (traps, ovens, piers)
    kFlagHoverOver    = 0x02000000,   // hover-over / no-diagonal
    // -- pre-AOS high flags (shifted +1 bit vs AOS), verified vs tiledata.mul --
    kFlagWhole        = 0x04000000,   // walls / solid structural pieces (AOS: Armor)
    kFlagWearable2    = 0x08000000,   // clothing, instruments (equipable/held)
    kFlagRoof         = 0x10000000,   // roof / canopy (thatch, shingles, tent)
    kFlagDoor         = 0x20000000,   // doors / secret doors
    kFlagStairBack    = 0x40000000,   // stairs (back face)
    kFlagStairRight   = 0x80000000,   // stairs (right face)
};

// On-file land tile is 26 bytes; in-memory keep it 28 to mirror the
// original client's stride and make byte-hash parity tests trivial.
#pragma pack(push, 1)
struct LandTile {
    u32  flags;        // +0
    u16  textureId;    // +4
    char name[20];     // +6  (ASCII, NUL-padded)
    u8   _pad[2];      // +26 (memory-only padding; matches stride 28)
};

// Field names/offsets verified against ObjectManager_LoadTileData @0x4C7AE0:
// file is dense (37B): flags(4) weight(1) quality(1) quantity(4) animId(2)
// hue(2) renderDimIndex(2) height(1) name(20). `animId` is the worn-item
// animation used to draw equipment over a mobile (valid range 0x190..0x3E7).
struct StaticTile {
    u32  flags;          // +0
    u8   weight;         // +4
    u8   quality;        // +5
    u8   _pad0[2];       // +6  (memory-only gap; file is dense)
    u32  quantity;       // +8   (file +6)
    u16  animId;         // +12  (file +10) worn-item animation id
    u16  hue;            // +14  (file +12)
    u16  renderDimIndex; // +16  (file +14)
    u8   height;         // +18  (file +16)
    char name[20];       // +19  (ASCII, NUL-padded)
    u8   _pad1[1];       // +39  (memory-only)
};
#pragma pack(pop)

static_assert(sizeof(LandTile)   == 28, "LandTile stride must be 28");
static_assert(sizeof(StaticTile) == 40, "StaticTile stride must be 40");

class TileDataLoader {
public:
    TileDataLoader();
    ~TileDataLoader();

    TileDataLoader(const TileDataLoader&) = delete;
    TileDataLoader& operator=(const TileDataLoader&) = delete;

    // Read tiledata.mul from `path`. Returns false on I/O or layout error.
    bool Load(const char* path);

    bool IsLoaded() const { return loaded_; }

    // Total accessors: any u32 is accepted. Out-of-range ids clamp to entry 0
    // (unchanged behaviour), and -- the part that matters for the observer
    // client -- an UNLOADED loader returns a zeroed tile instead of
    // dereferencing a null array. A failed/absent tiledata.mul must not be a
    // crash; see uo/safe_graphics.h.
    const LandTile&   Land(u32 id) const   {
        return lands_ ? lands_[id < kLandCount ? id : 0] : ZeroLand();
    }
    const StaticTile& Static(u32 id) const {
        return statics_ ? statics_[id < kStaticCount ? id : 0] : ZeroStatic();
    }

    static const LandTile&   ZeroLand();
    static const StaticTile& ZeroStatic();

    // Worn-item animation id for an item graphic (0 == none/not equippable).
    u16 ItemAnimId(u16 graphic) const { return Static(graphic).animId; }

    const LandTile*   LandArray()   const { return lands_; }
    const StaticTile* StaticArray() const { return statics_; }

private:
    LandTile*   lands_;
    StaticTile* statics_;
    bool        loaded_;
};

}
