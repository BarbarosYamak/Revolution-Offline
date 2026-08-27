#include "bot/Blacklist.h"

#include "uo/endian.h"

#include <algorithm>
#include <cstdio>

namespace uo::bot {

namespace {
constexpr i32 kZTolerance = 16;  // ~one character height

// verdata file id for statics patches, and the item id we stamp on our
// synthetic blocking statics. The static's `hue` field carries our range
// so a spot round-trips through the verdata layout exactly.
constexpr u32 kVerdataStatics  = 1;
constexpr u16 kBlacklistTileId = 0x0002;
}

u64 Blacklist::Key(i32 x, i32 y) {
    return (static_cast<u64>(static_cast<u32>(x)) << 32) |
            static_cast<u32>(y);
}

void Blacklist::AddPersistent(i32 x, i32 y, i32 z, i32 range) {
    spots_.push_back({x, y, z, range, true});
    if (!path_.empty()) SaveFile();
}

void Blacklist::AddTransient(i32 x, i32 y, i32 z, i32 range) {
    spots_.push_back({x, y, z, range, false});
}

void Blacklist::ClearTransient() {
    spots_.erase(std::remove_if(spots_.begin(), spots_.end(),
                     [](const Spot& s) { return !s.persistent; }),
                 spots_.end());
}

u32 Blacklist::RecordReject(i32 x, i32 y) {
    return ++rejectCounts_[Key(x, y)];
}

bool Blacklist::IsBlocked(i32 x, i32 y, i32 z) const {
    for (const Spot& s : spots_) {
        i32 ax = (x > s.x) ? x - s.x : s.x - x;
        i32 ay = (y > s.y) ? y - s.y : s.y - y;
        i32 az = (z > s.z) ? z - s.z : s.z - z;
        const i32 cheb = (ax > ay) ? ax : ay;
        if (cheb <= s.range && az <= kZTolerance) return true;
    }
    return false;
}

usize Blacklist::PersistentCount() const {
    usize n = 0;
    for (const Spot& s : spots_) if (s.persistent) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// blacklist.mul persistence — verdata layout: a big-endian entry table
// (count, then {file, block, position, length, extra}) followed by the patch
// payloads. We emit file=1 (statics) entries whose payload is a run of 7-byte
// statics records; the record's hue field carries our `range`. This sits on
// top of any real verdata and is itself a valid verdata patch.
// ---------------------------------------------------------------------------
bool Blacklist::Load(const char* path, u32 heightBlocks) {
    path_ = path ? path : "";
    heightBlocks_ = heightBlocks ? heightBlocks : 512;
    spots_.erase(std::remove_if(spots_.begin(), spots_.end(),
                     [](const Spot& s) { return s.persistent; }),
                 spots_.end());
    if (path_.empty()) return false;

    std::FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) return true;  // no file yet — start empty

    bool ok = true;
    u8 hdr[4];
    if (std::fread(hdr, 1, 4, f) == 4) {
        const u32 count = LoadBE32(hdr);
        std::vector<u8> entries(static_cast<usize>(count) * 20);
        if (count != 0 &&
            std::fread(entries.data(), 1, entries.size(), f) != entries.size()) {
            ok = false;
        }
        for (u32 i = 0; i < count && ok; ++i) {
            const u8* e     = entries.data() + static_cast<usize>(i) * 20;
            const u32 file  = LoadBE32(e);
            const u32 block = LoadBE32(e + 4);
            const u32 pos   = LoadBE32(e + 8);
            const u32 len   = LoadBE32(e + 12);
            if (file != kVerdataStatics) continue;
            if (len % 7 != 0) { ok = false; break; }
            if (std::fseek(f, static_cast<long>(pos), SEEK_SET) != 0) { ok = false; break; }
            const u32 bx = block / heightBlocks_;
            const u32 by = block % heightBlocks_;
            for (u32 k = 0; k < len / 7 && ok; ++k) {
                u8 rec[7];
                if (std::fread(rec, 1, 7, f) != 7) { ok = false; break; }
                const u8  cx    = rec[2];
                const u8  cy    = rec[3];
                const i8  z     = static_cast<i8>(rec[4]);
                const i32 range = static_cast<i32>(rec[5] |
                                  (static_cast<u32>(rec[6]) << 8));
                spots_.push_back({static_cast<i32>(bx * 8 + cx),
                                  static_cast<i32>(by * 8 + cy),
                                  z, range, true});
            }
        }
    }
    std::fclose(f);
    return ok;
}

void Blacklist::SaveFile() const {
    if (path_.empty()) return;

    struct Rec { u8 cx, cy; i8 z; u16 hue; };
    std::vector<u32>              blocks;  // unique block ids
    std::vector<std::vector<Rec>> recs;    // parallel: records per block

    for (const Spot& s : spots_) {
        if (!s.persistent || s.x < 0 || s.y < 0) continue;
        const u32 block = static_cast<u32>(s.x / 8) * heightBlocks_ +
                          static_cast<u32>(s.y / 8);
        usize gi = 0;
        for (; gi < blocks.size(); ++gi) if (blocks[gi] == block) break;
        if (gi == blocks.size()) { blocks.push_back(block); recs.emplace_back(); }
        Rec r;
        r.cx  = static_cast<u8>(s.x % 8);
        r.cy  = static_cast<u8>(s.y % 8);
        r.z   = static_cast<i8>(s.z);
        r.hue = static_cast<u16>(s.range < 0 ? 0 :
                (s.range > 0xFFFF ? 0xFFFF : s.range));
        recs[gi].push_back(r);
    }

    std::FILE* f = std::fopen(path_.c_str(), "wb");
    if (!f) return;

    const u32 count = static_cast<u32>(blocks.size());
    u8 hdr[4];
    StoreBE32(hdr, count);
    std::fwrite(hdr, 1, 4, f);

    u32 pos = 4 + count * 20;
    for (u32 i = 0; i < count; ++i) {
        const u32 len = static_cast<u32>(recs[i].size()) * 7;
        u8 e[20];
        StoreBE32(e,      kVerdataStatics);
        StoreBE32(e + 4,  blocks[i]);
        StoreBE32(e + 8,  pos);
        StoreBE32(e + 12, len);
        StoreBE32(e + 16, 0);
        std::fwrite(e, 1, 20, f);
        pos += len;
    }
    for (u32 i = 0; i < count; ++i) {
        for (const Rec& r : recs[i]) {
            u8 rec[7];
            rec[0] = static_cast<u8>(kBlacklistTileId & 0xFF);
            rec[1] = static_cast<u8>(kBlacklistTileId >> 8);
            rec[2] = r.cx;
            rec[3] = r.cy;
            rec[4] = static_cast<u8>(r.z);
            rec[5] = static_cast<u8>(r.hue & 0xFF);
            rec[6] = static_cast<u8>(r.hue >> 8);
            std::fwrite(rec, 1, 7, f);
        }
    }
    std::fclose(f);
}

}
