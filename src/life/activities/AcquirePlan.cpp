#include "uo/activities/acquire.h"

// THE ARITHMETIC OF ACQUIRING, in its own translation unit so ctest can reach
// it without linking Client -- the same split as BuyPlan.cpp next door, and
// for the same reason: this is where the bug was.
//
// Six heater shields were bought by a decision function that did not exist.
// The logic was spread across a wear loop and a buy loop in DoUpgradeGear,
// and between them nobody asked the one question that mattered: CAN this
// character wear the thing before spending gold on it.

namespace uo::life {

const char* AcquireStepName(AcquireStep s) {
    switch (s) {
        case AcquireStep::Done:   return "done";
        case AcquireStep::Wear:   return "wear";
        case AcquireStep::Buy:    return "buy";
        case AcquireStep::Refuse: return "refuse";
    }
    return "?";
}

AcquirePlan DecideAcquire(const AcquireRequest& req, i32 held, u16 worn) {
    AcquirePlan out;

    // ALREADY WEARING IT. The only definition of done for equipment: the
    // paperdoll, not the pack.
    if (req.mustWear && worn != 0 && worn == req.graphic) {
        out.step = AcquireStep::Done;
        out.reason = "already worn";
        return out;
    }

    // CAN THIS CHARACTER WEAR IT AT ALL? Asked FIRST, and asked whether or
    // not one is already carried -- because the answer decides both whether
    // to put it on and whether to buy another. This is the question whose
    // absence bought six shields: i_shield_heater needs STR 90, Corwyn has
    // 56, so every purchase was gold spent on something that could only ever
    // sit in the pack.
    if (req.mustWear && !req.wearable) {
        out.step = AcquireStep::Refuse;
        out.reason = req.unwearableReason;
        return out;
    }

    // IT IS IN THE PACK. Wear it rather than buying another -- the wear pass
    // and the buy pass used to be separate loops that could not see each
    // other, so a piece already carried did not stop the shopping.
    if (held > 0 && req.mustWear) {
        out.step = AcquireStep::Wear;
        out.reason = "it is in the pack and belongs on the paperdoll";
        return out;
    }

    // Not equipment, just stock.
    if (!req.mustWear && held >= req.desiredTotal) {
        out.step = AcquireStep::Done;
        out.reason = "this life already holds as many as it wanted";
        return out;
    }

    out.step = AcquireStep::Buy;
    out.reason = (held > 0) ? "some held, more wanted"
                            : "none held, and it may be bought";
    return out;
}

}  // namespace uo::life
