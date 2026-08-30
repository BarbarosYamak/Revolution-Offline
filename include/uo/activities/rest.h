#pragma once

// ---------------------------------------------------------------------------
// REST AND ROAM -- what a life does when nothing is pressing
// (docs/BOT_ARCHITECTURE.md sections 12, 32 and 47).
//
// "bots shouldnt be idle unless its state specifically" (project owner).
//
// Idling used to WIN. Not occasionally -- 73% of Kaelen's picks and up to 85%
// elsewhere, because every real errand was blocked and the no-op was the only
// thing left that scored anything at all. A bot standing in a field for
// twenty minutes is not resting; it is a bot with nothing to do and no way to
// say so.
//
// The distinction this file draws is between the three states that all looked
// like "idle" in the logs and are completely different:
//
//   EXPLORE   the character is blocked for want of KNOWING something -- no
//             supplier for a tongs, no buyer for these ingots, no trainer in
//             reach. Almost every blocked need in this project is of that
//             shape, and walking to an unvisited shop and reading the
//             paperdolls there is how it gets fixed. A full crafter finished
//             a whole session having visited ONE place, which is exactly why
//             he knew no supplier for any of the three tools he lacked.
//   REST      genuinely nothing to do and a real reason to stand still:
//             regenerating, or waiting out a cooldown that will expire.
//   SETTLE    the session is ending, and a character must log out somewhere
//             SAFE -- logging out in a hostile place leaves a ghost that
//             silently fails every later run.
//
// Only the middle one is idling, and it has to earn it.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

struct RestSight {
    // Is the session winding down?
    bool   sessionEnding = false;
    // Somewhere a logout is safe -- a town, a guarded region.
    bool   somewhereSafe = false;
    // Is there anything left this life could usefully learn by walking
    // around? Unvisited shops, unknown suppliers, unseen towns.
    bool   worthExploring = false;
    // Is the character actually recovering by standing here?
    bool   regenerating = false;
    double hpFraction = 1.0;
    // How long every real errand has been unavailable, in milliseconds.
    i64    blockedForMs = 0;
};

struct RestTuning {
    // Past this, standing still stops being rest and becomes a report.
    i64 idleIsAFaultAfterMs = 20 * 60 * 1000;   // twenty minutes
    // Below this, standing still to regenerate is a real errand.
    double restWhileBelowHp = 0.80;
};

enum class RestStep : u8 {
    Explore = 0,   // go and learn something
    Rest,          // stand still, and mean it
    Settle,        // get somewhere safe before logging out
    Stagnant,      // nothing to do for far too long: a fault, not a state
};

const char* RestStepName(RestStep s);

struct RestPlan {
    RestStep    step = RestStep::Explore;
    const char* reason = "";
};

RestPlan DecideRest(const RestSight& see, const RestTuning& tune);

}  // namespace uo::life
