#include "uo/activities/buy.h"

// THE ARITHMETIC OF BUYING, kept in its own translation unit ON PURPOSE.
//
// Buy.cpp next door talks to Client and therefore cannot be linked by ctest.
// This half -- how many do I still need, which money may I spend, is the
// price acceptable -- is where every purchase bug in this project's history
// actually lived, so it is exactly the half that must be testable:
//
//   * six heater shields bought one at a time because the request said
//     "buy one" instead of "hold one"
//   * a scribe with 781 gold and a 900 reserve refusing three 3-gold
//     scrolls, every fifteen seconds, for a whole session
//   * Voris outside the alchemist unable to afford a 12 gold bottle while
//     carrying five poison potions worth about a hundred
//
// tests/activity_buy.cpp links this file alone.

namespace uo::life {

BuyPlan Decide(const BuyRequest& req, i32 held, i32 gold, i32 unitPrice) {
    BuyPlan out;

    // ALREADY HAVE IT. The question is what this life should END UP holding,
    // never "buy one more" -- that phrasing is what put six heater shields in
    // Corwyn's pack, each bought because the slot was still empty.
    const i32 shortfall = req.desiredTotal - held;
    if (shortfall <= 0) {
        out.satisfied = true;
        out.reason = "this life already holds as many as it wanted";
        return out;
    }
    out.quantity = shortfall;

    // The price is not known until a shop is open. Asking for the shortfall
    // is right; the errand clamps to stock and this function runs again with
    // a real number once there is one.
    if (unitPrice <= 0) {
        out.reason = "the price is not known yet";
        return out;
    }

    // A CEILING, so one greedy vendor cannot drain a purse.
    if (req.maxPricePerUnit > 0 && unitPrice > req.maxPricePerUnit) {
        out.quantity = 0;
        out.blocked = true;
        out.reason = "the price is above what this request will pay";
        return out;
    }

    // WORKING CAPITAL IS NOT THE DEATH RESERVE, and confusing them deadlocks
    // characters outright: a scribe with 781 gold and a 900 reserve stood in
    // a mage shop refusing to buy three 3-gold scrolls, every fifteen
    // seconds, for an entire session. A reserve that forbids the only
    // activity which refills it is not caution, it is a trap. The caller
    // decides which money this is by what it puts in minimumGoldReserve.
    const i32 spendable = gold - req.minimumGoldReserve;
    if (spendable < unitPrice) {
        out.quantity = 0;
        out.blocked = true;
        out.reason = "not enough gold above the reserve for even one";
        return out;
    }

    const i32 affordable = spendable / unitPrice;
    if (affordable < out.quantity) {
        out.quantity = affordable;
        out.reason = "buying what the purse allows rather than the full want";
        return out;
    }

    out.reason = "affordable in full";
    return out;
}

}  // namespace uo::life
