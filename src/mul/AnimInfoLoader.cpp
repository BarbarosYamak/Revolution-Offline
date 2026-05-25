#include "uo/animinfo.h"

#include "uo/mul.h"

namespace uo::animinfo {

bool AnimInfoLoader::Load(const char* path) {
    for (Entry& e : entries_) e = Entry{};
    loaded_ = false;

    mul::File f;
    if (!path || !f.Open(path)) return false;

    for (Entry& e : entries_) {
        u8 b[2];
        if (!f.Read(b, sizeof(b))) return false;
        e.walk = b[0] ? b[0] : 1;
        e.run = b[1] ? b[1] : 1;
    }

    loaded_ = true;
    return true;
}

u8 AnimInfoLoader::MoveFrameCount(u16 body, bool running) const {
    if (body >= entries_.size()) body = 50;
    const Entry& e = entries_[body];
    const u8 n = running ? e.run : e.walk;
    return n ? n : 1;
}

} // namespace uo::animinfo
