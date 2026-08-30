#include "uo/activities/disposal.h"

// The arithmetic of what leaves the pack, in its own translation unit so ctest
// reaches it without Client.

namespace uo::life {

const char* ItemRoleName(ItemRole r) {
    switch (r) {
        case ItemRole::Money:      return "money";
        case ItemRole::Consumable: return "consumable";
        case ItemRole::CraftInput: return "crafting input";
        case ItemRole::Tool:       return "tool";
        case ItemRole::Wearable:   return "wearable";
        case ItemRole::Produce:    return "produce";
        case ItemRole::Unknown:    return "no use";
    }
    return "?";
}

const char* DisposalStepName(DisposalStep s) {
    switch (s) {
        case DisposalStep::Keep: return "keep";
        case DisposalStep::Sell: return "sell";
    }
    return "?";
}

i32 KeepCount(ItemRole role, const DisposalTuning& tune) {
    switch (role) {
        // Gold is not merchandise, and a life that sells its food and reagents
        // to raise money for food and reagents has learnt nothing.
        case ItemRole::Money:
        case ItemRole::Consumable:
        case ItemRole::CraftInput:
            return kKeepEverything;
        case ItemRole::Tool:     return tune.keepTool;
        case ItemRole::Wearable: return tune.keepWearable;
        case ItemRole::Produce:  return tune.keepProduce;
        case ItemRole::Unknown:  return tune.keepUnknown;
    }
    return kKeepEverything;
}

DisposalPlan DecideDisposal(const DisposalSight& see,
                            const DisposalTuning& tune) {
    DisposalPlan out;

    const i32 keep = KeepCount(see.role, tune);
    if (keep == kKeepEverything) {
        out.reason = "this is stock, not surplus -- it stays in the pack";
        return out;
    }

    // THE VENDOR IS THE AUTHORITY ON WHAT IS SALEABLE. A price of zero is the
    // server declining to name one, which is not the same as an offer of
    // nothing -- selling into it hands the goods over for free.
    if (see.pricePerUnit <= 0) {
        out.reason = "no price named for it -- not giving it away";
        return out;
    }
    if (see.vendorTakes <= 0) {
        out.reason = "this buyer did not list it";
        return out;
    }

    const i32 surplus = see.carried - keep;
    if (surplus <= 0) {
        out.reason = keep > 0
                         ? "carrying no more than the one worth keeping"
                         : "none of it in the pack";
        return out;
    }

    // Never offer more than three things at once agree on: what is spare, what
    // this vendor said it would take, and what its purse has proved it can
    // afford. Offering beyond any of them is how a sale goes silent.
    i32 qty = surplus < see.vendorTakes ? surplus : see.vendorTakes;
    if (see.lotCap > 0 && see.lotCap < qty) qty = see.lotCap;
    if (qty <= 0) {
        out.reason = "the lot has been halved away to nothing";
        return out;
    }

    out.step = DisposalStep::Sell;
    out.quantity = qty;
    out.reason = keep > 0 ? "spare beyond the one worth keeping"
                          : "carrying it for no reason";
    return out;
}

}  // namespace uo::life
