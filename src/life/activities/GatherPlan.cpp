#include "uo/activities/gather.h"

// The arithmetic of gathering, in its own translation unit so ctest reaches
// it without Client -- the same split as BuyPlan, AcquirePlan, CraftPlan and
// TrainPlan.

namespace uo::life {

const char* GatherStepName(GatherStep s) {
    switch (s) {
        case GatherStep::Swing:     return "swing";
        case GatherStep::ArmTool:   return "arm the tool";
        case GatherStep::LeaveArea: return "leave the area";
        case GatherStep::TakeItIn:  return "take the load in";
        case GatherStep::NeedTool:  return "need a tool";
        case GatherStep::Done:      return "done";
    }
    return "?";
}

GatherPlan DecideGather(const GatherRequest& req, const GatherSight& sight) {
    GatherPlan out;

    // A FULL PACK FIRST, before anything else is considered. Checked BEFORE
    // the swing rather than after the trip: a character that tests weight
    // only on arrival spends its last ten swings gaining nothing and then
    // walks to town at 100% capacity.
    if (sight.weightFraction >= req.packFullFraction) {
        out.step = GatherStep::TakeItIn;
        out.reason = "the pack is as full as this life will carry";
        return out;
    }

    // NO TOOL AT ALL is a different problem from a tool in the wrong place,
    // and they have different answers: buy one, or take it in hand.
    if (!sight.toolInPack && !sight.toolWielded) {
        out.step = GatherStep::NeedTool;
        out.reason = "no tool for this work";
        return out;
    }
    if (req.toolMustBeWielded && !sight.toolWielded) {
        // Sphere's mining reads SRC.WEAPON.USESCUR, so a pickaxe in the
        // backpack digs nothing at all -- Corran carried one for a whole
        // session and mined none.
        out.step = GatherStep::ArmTool;
        out.reason = "the tool is carried but not in hand, and this trade "
                     "needs it wielded";
        return out;
    }

    if (sight.targetInReach) {
        out.step = GatherStep::Swing;
        out.reason = "there is something here to work";
        return out;
    }

    // "THIS AREA IS DONE" IS NEVER Done.
    //
    // Returning goal-complete here handed control to a planner with no new
    // information: it re-picked the same goal, for the same character, in the
    // same worked-out clearing. Forty completions with progress=0 in under
    // two minutes. The honest answer is to go somewhere else.
    if (sight.areaWorkedOut) {
        out.step = GatherStep::LeaveArea;
        out.reason = "everything in range is worked out -- somewhere else, "
                     "not somewhere later";
        return out;
    }

    // Nothing in reach and the area is not spent: the character is simply
    // not standing in the right place yet.
    if (sight.held >= req.loadWorthTaking) {
        out.step = GatherStep::TakeItIn;
        out.reason = "a load worth the trip, and nothing in reach to add to it";
        return out;
    }

    out.step = GatherStep::LeaveArea;
    out.reason = "nothing in reach here";
    return out;
}

}  // namespace uo::life
