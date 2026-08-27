#pragma once

#include "uo/types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace uo::bot {

// Runtime walkability overlay: extra impassable spots A* must avoid on top
// of the MUL/verdata static data. Each spot blocks every cell within
// Chebyshev `range` of (x,y) whose z is within kZTolerance of the spot z.
//
//   * Persistent spots are blocks we are confident about: a cell rejected
//     by the server `threshold` times. They survive between trips and are
//     meant to be serialised to blacklist.mul (verdata format) later.
//   * Transient spots are suspected dynamic obstacles (players/mobs). They
//     live for a single trip and are cleared on the next goto.
class Blacklist {
public:
    // Load persisted spots from a verdata-format file (file=1 statics
    // patches). `heightBlocks` is the map's block height, used for the
    // block<->cell index math. Remembers the path so AddPersistent() can
    // write back. Missing file is fine (starts empty). Returns false only
    // on a malformed file.
    bool Load(const char* path, u32 heightBlocks);

    void AddPersistent(i32 x, i32 y, i32 z, i32 range);
    void AddTransient (i32 x, i32 y, i32 z, i32 range);
    void ClearTransient();

    // Tally a server rejection at (x,y) and return how many times this cell
    // has been rejected so far (this session). Used to decide when a bump
    // is a real static block worth persisting vs. a transient obstacle.
    u32  RecordReject(i32 x, i32 y);

    bool  IsBlocked(i32 x, i32 y, i32 z) const;
    usize PersistentCount() const;
    usize Count() const { return spots_.size(); }

private:
    struct Spot { i32 x, y, z, range; bool persistent; };
    static u64 Key(i32 x, i32 y);
    void SaveFile() const;  // rewrite the whole blacklist.mul

    std::vector<Spot>             spots_;
    std::unordered_map<u64, u32>  rejectCounts_;
    std::string                   path_;        // backing blacklist.mul ("" = none)
    u32                           heightBlocks_ = 512;
};

}
