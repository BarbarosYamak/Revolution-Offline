#pragma once

// ---------------------------------------------------------------------------
// CORPSE RECOVERY -- getting up, going back, and knowing when not to
// (docs/BOT_ARCHITECTURE.md sections 25 and 47).
//
// DEATH ON THIS SHARD IS FULL LOOT. Everything the character was carrying and
// wearing is on the ground where it fell, so the corpse run is not a tidying
// errand -- it is the whole of a life's equipment, and often its gold.
//
// WHICH IS EXACTLY WHY IT MUST BE ABLE TO SAY NO.
//
// The handler this replaces walked straight back with no evaluation of any
// kind: no health check, no danger memory, no attempt limit. That is the
// precise shape of a DEATH LOOP, which section 47 lists among the failures a
// fleet must detect --
//
//     die -> resurrect at a tenth of health -> walk back to the thing that
//     killed you -> die -> repeat
//
// -- and this project has never seen one only because, in 21 recorded runs,
// no bot has ever died. The recovery path has never fired. A capability that
// is BUILT and never FIRED is indistinguishable from one that is broken, so
// the risk rules are written here BEFORE the first death rather than after
// the first loop.
//
// "Personality/risk matters. A cautious bot may abandon a corpse in extreme
// danger" (section 25). Abandonment is a real outcome with a real cost: the
// gear is gone. It is still cheaper than the loop.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

struct RecoverySight {
    // A ghost can do nothing else until it is raised.
    bool   dead = false;
    bool   threatened = false;
    // Do we even know where we fell?
    bool   corpseKnown = false;
    i32    corpseDistance = 0;
    // How this character's own memory rates that place, 0..1. Personal, not
    // global: somewhere this bot died three times is dangerous TO IT.
    double dangerHeatAtCorpse = 0.0;
    double hpFraction = 1.0;
    // How many times this corpse has already been walked at.
    i32    attemptsSoFar = 0;
    // Is there anything left to carry away?
    bool   corpseEmpty = false;
    // Gear recovered but not yet worn.
    bool   gearInPack = false;
    // Has a live world-item serial for the corpse actually been bound? A
    // death record outlives the corpse: sphere.ini CorpsePlayerDecay=7 (min),
    // and the record survives a logout, so a character raised long after the
    // fact walks back to a tile with nothing on it.
    bool   corpseVisible = false;
    // Consecutive decisions taken while STANDING AT the death site with no
    // corpse bound. Without this the loot step opens container 0 forever.
    i32    probesAtSite = 0;
};

struct RecoveryTuning {
    // 0 = abandon at the first sign of trouble, 1 = go back regardless.
    double riskTolerance = 0.5;
    // Do not walk back below this. UO raises a ghost at roughly a tenth of
    // its health, and walking into the same fight at a tenth is the loop.
    double minHpToReturn = 0.55;
    // Give up after this many trips. A corpse decays; an infinite errand
    // does not.
    i32    maxAttempts = 3;
    // How many decisions to spend standing on the death tile waiting for the
    // corpse to appear in the world-item stream before calling it gone. The
    // stream needs a moment after arrival, so this is not 1.
    i32    maxProbesAtSite = 5;
};

enum class RecoveryStep : u8 {
    SeekResurrection = 0,  // a ghost: nothing else is possible
    Recover,               // alive but too hurt to go back yet
    TravelToCorpse,
    Loot,
    ReEquip,               // gear in the pack that belongs on the paperdoll
    Abandon,               // too dangerous, too far gone, or too often tried
    CorpseGone,            // stood on the spot; the corpse is not there
    Done,
};

const char* RecoveryStepName(RecoveryStep s);

struct RecoveryPlan {
    RecoveryStep step = RecoveryStep::Done;
    const char*  reason = "";
};

RecoveryPlan DecideRecovery(const RecoverySight& see,
                            const RecoveryTuning& tune);

}  // namespace uo::life
