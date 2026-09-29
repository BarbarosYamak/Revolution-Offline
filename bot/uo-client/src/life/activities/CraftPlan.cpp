#include "uo/activities/craft.h"

// The arithmetic of crafting, in its own translation unit so ctest reaches it
// without Client -- the same split as BuyPlan.cpp and AcquirePlan.cpp.

namespace uo::life {

const char* CraftStepName(CraftStep s) {
    switch (s) {
        case CraftStep::Done:          return "done";
        case CraftStep::Make:          return "make";
        case CraftStep::ShortOfInputs: return "short of inputs";
        case CraftStep::ReserveHit:    return "reserve";
    }
    return "?";
}

CraftPlan DecideCraft(const CraftRequest& req, i32 held, i32 inputsAvailable) {
    CraftPlan out;
    out.remaining = req.desiredTotal - held;

    if (out.remaining <= 0) {
        out.step = CraftStep::Done;
        out.remaining = 0;
        out.reason = "as many are held as this batch wanted";
        return out;
    }

    // THE WORKING RESERVE IS NOT SPARE MATERIAL. A smith that turns every
    // ingot into daggers cannot smith tomorrow; the reserve is what protects
    // the next sitting, and it is checked BEFORE the count of what is free.
    if (inputsAvailable <= req.minimumMaterialsReserve) {
        out.step = (inputsAvailable > 0) ? CraftStep::ReserveHit
                                         : CraftStep::ShortOfInputs;
        out.reason = (inputsAvailable > 0)
                         ? "the only inputs left are the working reserve"
                         : "no inputs at all";
        return out;
    }

    const i32 usable = inputsAvailable - req.minimumMaterialsReserve;
    if (usable < out.remaining) {
        // Not a failure: make what the materials allow and come back. The
        // alternative -- refusing until every input for the whole batch is
        // present -- is how a crafter stands still holding half a batch.
        out.remaining = usable;
        out.step = CraftStep::Make;
        out.reason = "making what the materials allow, not the whole batch";
        return out;
    }

    out.step = CraftStep::Make;
    out.reason = "materials in hand for the whole batch";
    return out;
}

}  // namespace uo::life
