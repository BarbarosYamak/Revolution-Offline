#pragma once

// ---------------------------------------------------------------------------
// NPC ROTATION -- "one silent NPC is not the whole trade"
// (docs/BOT_ARCHITECTURE.md sections 16 and 47).
//
// THE RULE, and both ways this project has broken it.
//
// TOO HARSH. Ysolde asked a mage trainer for Meditation, was told "you
// already know as much as I can teach", and the code wrote the SKILL off --
// so she never bought a tenth of it from anyone, ever. Sphere's teaching
// ceiling is per-NPC (NPC_GetTrainMax caps at 30% of the trainer's own
// value), so one refusal says something about that trainer and nothing about
// the trade. It cost her the only skill she could have bought.
//
// TOO FORGIVING. The bank ask went to the same banker every tick, forever,
// because nothing recorded that this one had already ignored us N times.
//
// The answer to both is the same small thing: keep a per-errand skip list,
// try the next one, and know when they are ALL exhausted -- at which point
// the errand fails honestly instead of either giving up early or looping.
//
// PER-ERRAND, deliberately. These lists used to be Runner members shared
// between goals (bankerSilent_, bankerAsked_, bankOpenTries_), which is the
// same mistake that let a gear trip spend the spellbook's trip allowance.
//
// Protocol-free: serials in, serials out. ctest reaches it directly.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <vector>

namespace uo::life {

class NpcRotation {
public:
    // How many times to ask ONE npc before deciding it is not answering.
    void Configure(i32 triesPerNpc) {
        triesPerNpc_ = triesPerNpc > 0 ? triesPerNpc : 1;
    }

    void Reset();

    // Point the rotation at whoever the world offers. Returns false when this
    // is a fresh face, which is how the caller knows to start counting again.
    bool Aim(u32 serial);

    // Record an ask that produced nothing. Returns true when THIS npc has
    // now been given its full allowance and should be skipped.
    bool NoteSilence();

    // Serials to exclude from the next lookup. Hand straight to the client's
    // trade search.
    const std::vector<u32>& Skip() const { return skip_; }

    // Everything answered: the current npc came good.
    void NoteAnswered();

    // How many distinct npcs have gone silent on this errand.
    i32 Exhausted() const { return static_cast<i32>(skip_.size()); }

    // Has this errand run out of people to ask? `maxNpcs` is the caller's
    // judgement about how many doors are worth trying.
    bool OutOfDoors(i32 maxNpcs) const {
        return static_cast<i32>(skip_.size()) >= maxNpcs;
    }

    u32 Current() const { return current_; }
    i32 Tries() const { return tries_; }

private:
    std::vector<u32> skip_;
    u32 current_ = 0;
    i32 tries_ = 0;
    i32 triesPerNpc_ = 3;
};

}  // namespace uo::life
