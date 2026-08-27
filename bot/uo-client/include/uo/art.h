#pragma once

#include "uo/mul.h"
#include "uo/types.h"

#include <unordered_map>
#include <vector>

namespace uo::art {

// A decoded art bitmap in the client's native ARGB1555 (bit15 = opacity).
// A pixel of 0 is transparent; any nonzero value is opaque. Row-major.
struct Sprite {
    u16 width  = 0;
    u16 height = 0;
    std::vector<u16> px;
};

// Loads art.mul / artidx.mul and decodes tiles on demand. Land tiles are
// indexed directly (0..0x3FFF); static items live at 0x4000 + itemId. Decoded
// sprites are cached by art index. An "empty" index caches a 0x0 sprite and
// returns nullptr.
class ArtLoader {
public:
    bool Open(const char* artIdxPath, const char* artPath);
    bool IsOpen() const { return idx_.IsOpen() && art_.IsOpen(); }

    const Sprite* Land(u16 tileId);
    const Sprite* Static(u16 itemId);

    // Opt-in crash/blank guard for out-of-era graphics.
    //
    // OFF (default): an index with no bitmap returns nullptr and the caller
    // draws nothing -- the existing bot/renderer behaviour, unchanged.
    //
    // ON: the same index returns a loud magenta placeholder sprite instead.
    // The observer client turns this on so a graphic this Renaissance-era
    // art.mul cannot draw (a post-Renaissance mount, a shard-custom item)
    // shows up as a visible marker at the right tile rather than silently
    // vanishing. Either way the lookup is bounds-checked and never throws.
    void SetPlaceholders(bool on) { placeholders_ = on; }
    bool Placeholders() const { return placeholders_; }

private:
    const Sprite* LoadIndex(u32 index, bool isLand);
    // Fill `s` with the placeholder bitmap and return it, or return nullptr
    // when placeholders are off.
    const Sprite* Miss(Sprite& s);

    mul::File idx_;
    mul::File art_;
    std::unordered_map<u32, Sprite> cache_;
    bool placeholders_ = false;
};

}
