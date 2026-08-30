#include "uo/activities/rest.h"

// The arithmetic of having nothing to do, in its own translation unit so
// ctest reaches it without Client.

namespace uo::life {

const char* RestStepName(RestStep s) {
    switch (s) {
        case RestStep::Explore:  return "explore";
        case RestStep::Rest:     return "rest";
        case RestStep::Settle:   return "settle somewhere safe";
        case RestStep::Stagnant: return "stagnant";
    }
    return "?";
}

RestPlan DecideRest(const RestSight& see, const RestTuning& tune) {
    RestPlan out;

    // ENDING SOMEWHERE SAFE OUTRANKS EVERYTHING ELSE.
    //
    // Logging out in a hostile place leaves a ghost, and a ghost silently
    // fails every later run for that character -- a fault that costs whole
    // sessions to notice because nothing about it looks wrong at the time.
    if (see.sessionEnding) {
        if (see.somewhereSafe) {
            out.step = RestStep::Rest;
            out.reason = "somewhere safe, and the session is ending";
            return out;
        }
        out.step = RestStep::Settle;
        out.reason = "the session is ending and this is no place to log out";
        return out;
    }

    // STANDING STILL TO RECOVER IS A REAL ERRAND. This is the one case where
    // doing nothing is doing something.
    if (see.regenerating && see.hpFraction < tune.restWhileBelowHp) {
        out.step = RestStep::Rest;
        out.reason = "hurt, and standing still is actually mending it";
        return out;
    }

    // ALMOST EVERY BLOCKED NEED IN THIS PROJECT IS BLOCKED FOR WANT OF
    // KNOWING SOMETHING -- no supplier for a tongs, no buyer for these
    // ingots, no trainer in reach. Walking to an unvisited shop and reading
    // the paperdolls there is how that gets fixed, and a full crafter who
    // finished a session having visited ONE place is why it has to be the
    // default rather than the fallback.
    if (see.worthExploring) {
        out.step = RestStep::Explore;
        out.reason = "blocked for want of knowing where things are";
        return out;
    }

    // NOTHING TO DO FOR TOO LONG IS A FAULT, NOT A STATE (section 47).
    // Kaelen idled 73% of a session; the log called it idling and nobody
    // could tell it from a character taking a breather.
    if (see.blockedForMs >= tune.idleIsAFaultAfterMs) {
        out.step = RestStep::Stagnant;
        out.reason = "every errand has been unavailable for far too long -- "
                     "this is a fault to report, not a rest to take";
        return out;
    }

    out.step = RestStep::Rest;
    out.reason = "nothing pressing, and nothing left to go and learn";
    return out;
}

}  // namespace uo::life
