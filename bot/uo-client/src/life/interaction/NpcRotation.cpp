#include "uo/interaction/npc_rotation.h"

namespace uo::life {

void NpcRotation::Reset() {
    skip_.clear();
    current_ = 0;
    tries_ = 0;
}

bool NpcRotation::Aim(u32 serial) {
    if (serial == current_) return true;
    // A DIFFERENT FACE GETS A FRESH ALLOWANCE. Carrying the previous npc's
    // tally over is how a rotation writes off the second banker for the first
    // one's silence.
    current_ = serial;
    tries_ = 0;
    return false;
}

bool NpcRotation::NoteSilence() {
    if (!current_) return false;
    if (++tries_ < triesPerNpc_) return false;
    for (u32 s : skip_)
        if (s == current_) return true;      // already written off
    skip_.push_back(current_);
    // Cap the list: a bot that has tried thirty bankers has a different
    // problem, and an unbounded skip list is a slow leak on a long session.
    if (skip_.size() > 16) skip_.erase(skip_.begin());
    current_ = 0;
    tries_ = 0;
    return true;
}

void NpcRotation::NoteAnswered() {
    // FORGIVE EVERYONE. The next visit starts clean -- a banker that ignored
    // us while walking past is not a banker that will ignore us at the
    // counter, and remembering that across a whole session slowly empties the
    // town of people we are willing to speak to.
    skip_.clear();
    tries_ = 0;
}

}  // namespace uo::life
