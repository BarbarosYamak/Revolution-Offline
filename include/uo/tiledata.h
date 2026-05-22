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
// Flag bits (`TileFlags`) are documented in tons of public UO refs;
// included here as a non-exhaustive enum so callers don't need to
// reproduce magic constants.

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
    kFlagAnimation    = 0x01000000,
    kFlagHoverOver    = 0x02000000,
    kFlagArmor        = 0x04000000,
    kFlagRoof         = 0x08000000,
    kFlagDoor         = 0x10000000,
    kFlagStairBack    = 0x20000000,
    kFlagStairRight   = 0x40000000,
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

struct StaticTile {
    u32  flags;        // +0
    u8   weight;       // +4
    u8   quality;      // +5
    u8   _pad0[2];     // +6  (memory-only gap; file is dense)
    u32  misc;         // +8  (animation/quantity, version-dependent)
    u16  hue;          // +12
    u16  stackOffset;  // +14
    u16  value;        // +16
    u8   height;       // +18
    char name[20];     // +19 (ASCII, NUL-padded)
    u8   _pad1[1];     // +39 (memory-only)
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

    const LandTile&   Land(u32 id) const   { return lands_[id < kLandCount ? id : 0]; }
    const StaticTile& Static(u32 id) const { return statics_[id < kStaticCount ? id : 0]; }

    const LandTile*   LandArray()   const { return lands_; }
    const StaticTile* StaticArray() const { return statics_; }

private:
    LandTile*   lands_;
    StaticTile* statics_;
    bool        loaded_;
};

}
