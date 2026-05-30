#pragma once

#include "uo/mul.h"
#include "uo/types.h"
#include "uo/verdata.h"

#include <vector>

namespace uo::map {

// Britannia (map0) is 6144 x 4096 cells = 768 x 512 blocks.
// Other maps (Ilshenar, Malas, Tokuno) have their own dimensions.
constexpr u32 kBritWidthBlocks  = 768;
constexpr u32 kBritHeightBlocks = 512;
constexpr u32 kBlockCells       = 64;     // 8 x 8

// On-disk layout for one 8x8 block in map*.mul:
//   DWORD  header            (4 bytes, typically 0 — unused / version)
//   Cell   cells[64]         (192 bytes; row-major y * 8 + x)
//
// Cell:
//   WORD   tileId            (land tile id, refer to TileDataLoader::Land)
//   SBYTE  z                 (height in z-units)
//
// Block stride on disk: 4 + 64 * 3 = 196 bytes.
#pragma pack(push, 1)
struct LandCell {
    u16 tileId;
    i8  z;
};
struct MapBlock {
    LandCell cells[kBlockCells]; // already skips the 4-byte header
};

// staidx*.mul index entry — 12 bytes per (bx, by) block:
//   DWORD  lookup            (file offset into statics*.mul, 0xFFFFFFFF = no statics)
//   DWORD  length            (byte count in statics*.mul)
//   DWORD  extra             (unused for map data)
struct StaticsIndexEntry {
    u32 lookup;
    u32 length;
    u32 extra;
};

// statics*.mul record — 7 bytes per static item, packed within the
// block's length-bound chunk.
struct StaticItem {
    u16 itemId;
    u8  cellX;        // 0..7 within block
    u8  cellY;        // 0..7 within block
    i8  z;
    u16 hue;
};
#pragma pack(pop)

static_assert(sizeof(LandCell)          == 3,   "LandCell == 3");
static_assert(sizeof(MapBlock)          == 192, "MapBlock == 192 (header skipped)");
static_assert(sizeof(StaticsIndexEntry) == 12,  "StaticsIndexEntry == 12");
static_assert(sizeof(StaticItem)        == 7,   "StaticItem == 7");

constexpr u32 kBlockBytesOnDisk = 196;
constexpr u32 kNoStatics        = 0xFFFFFFFFu;

// In-memory statics overlay: temporarily rewrite a chopped tree's graphic to
// a stump so the world view shows it as depleted while the server regrows it.
// Modeled on the runtime blacklist overlay (range/z-aware spots, no MUL edit);
// kept purely in memory for now, but self-contained so a blacklist.mul-style
// persistence layer can be bolted on later without touching ReadStatics.
//
// Entries carry an absolute expiry (ms, same clock as Client::NowMs). Update()
// prunes expired spots and refreshes the clock Rewrite() compares against, so
// ReadStatics itself stays clock-free. The server's resource regrowth is a
// global ~68min tick (no per-tree timer the client can observe), so the TTL is
// just a sensible "looks chopped for a while" duration chosen by the caller.
class StumpOverlay {
public:
    // Use kRemove as `stumpId` to suppress (delete) the item from the list
    // rather than rewriting it to another graphic.
    static constexpr u16 kRemove = 0xFFFFu;

    // Replace static `fromId` at world cell (x,y) (z matched within kZTol)
    // with `stumpId` until `expiryMs`. Re-adding the same cell refreshes it.
    // Use stumpId=kRemove to strip the item from ReadStatics output entirely.
    void Add(i32 x, i32 y, i8 z, u16 fromId, u16 stumpId, i64 expiryMs);
    void Clear() { spots_.clear(); }

    // Drop expired spots and remember `nowMs` for later Rewrite() calls.
    void Update(i64 nowMs);

    // If a live override matches this cell+graphic, set *itemId to the new
    // graphic (or kRemove to signal deletion) and return true; otherwise
    // leave *itemId untouched and return false.
    bool Rewrite(i32 x, i32 y, i8 z, u16* itemId) const;

    usize Count() const { return spots_.size(); }

private:
    struct Spot { i32 x, y; i8 z; u16 fromId, stumpId; i64 expiryMs; };
    static constexpr i32 kZTol = 8;
    std::vector<Spot> spots_;
    i64               now_ = 0;
};

class Map {
public:
    Map();
    ~Map();

    Map(const Map&) = delete;
    Map& operator=(const Map&) = delete;

    // Width / height in BLOCKS. For map0 use kBritWidthBlocks / kBritHeightBlocks.
    // `verdataPath` is optional: when given (and the file exists) map block
    // and statics reads honor verdata.mul patches the same way the official
    // client does — patched blocks override the base map0/statics0 data.
    bool Open(const char* mapPath,
              const char* staidxPath,
              const char* staticsPath,
              u32 widthBlocks  = kBritWidthBlocks,
              u32 heightBlocks = kBritHeightBlocks,
              const char* verdataPath = nullptr);

    bool IsOpen() const { return map_.IsOpen(); }

    u32 WidthBlocks()  const { return widthBlocks_; }
    u32 HeightBlocks() const { return heightBlocks_; }
    u32 WidthCells()   const { return widthBlocks_  * 8; }
    u32 HeightCells()  const { return heightBlocks_ * 8; }

    // Read the 8x8 land block at block coords (bx, by).
    // Returns false on out-of-range or I/O error.
    bool ReadBlock(u32 bx, u32 by, MapBlock* out);

    // Read up to `cap` statics for block (bx, by) into `out`. Sets
    // *count to the actual number written. Returns false on I/O error.
    // Returns true with *count == 0 when the block has no statics.
    bool ReadStatics(u32 bx, u32 by,
                     StaticItem* out, u32 cap, u32* count);

    // Read a single cell (in cell coords, 0..widthCells-1). Cheaper if
    // you only need one cell; for path queries prefer ReadBlock.
    bool ReadCell(u32 x, u32 y, LandCell* out);

    // Runtime stump overlay applied by ReadStatics (see StumpOverlay).
    StumpOverlay&       Stumps()       { return stumps_; }
    const StumpOverlay& Stumps() const { return stumps_; }

private:
    // Rewrite/remove records in `out[0..count)` that match live stump entries.
    // Items marked kRemove are compacted out. Returns the new (<=count) count.
    u32 ApplyStumps(u32 bx, u32 by, StaticItem* out, u32 count) const;

    mul::File    map_;
    mul::File    staidx_;
    mul::File    statics_;
    mul::Verdata verdata_;
    StumpOverlay stumps_;
    u32 widthBlocks_;
    u32 heightBlocks_;
};

}
