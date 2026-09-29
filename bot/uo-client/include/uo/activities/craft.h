#pragma once

// ---------------------------------------------------------------------------
// CRAFT -- make a batch of something, and know when one actually appeared
// (docs/BOT_ARCHITECTURE.md sections 19 and 18).
//
// SECTION 19'S RULE: "Never start another craft merely because 2 seconds
// passed." This project's craft loop is the FIFTH place to need that rule and
// the one where it is still visibly wrong at the time of writing:
//
//   craft: no result from the last i_potion_poison in 8s -- trying again
//   craft: making i_potion_poison -- using its own tool to open the menu
//   [0x7C] dialog menu=690: "What sort of potion do you want to make?"
//   event action_result: use_object timeout
//   [0x7D] answering dialog index=2 ... craft: chose '^Poison'
//   ...and the same three lines again, and again
//
// Note what that log actually shows: the menu DID open -- the 0x7C arrived
// and was answered -- while the action layer reported `use_object timeout`,
// because nothing had told it that "a craft menu is open" is what success
// looks like for that click. Eight seconds later the wait expired and the
// whole sequence started over, discarding the menu that was already up.
//
// So the two halves this file separates are exactly the two that were tangled:
//
//   THE PLAN      how many are still wanted, and are there materials for one?
//                 Pure arithmetic, and the half that decides whether to swing
//                 at all.
//   THE WAIT      did an item appear? Answered by the PACK COUNT rising, not
//                 by a timer -- section 18 -- with the deadline owned by a
//                 Handshake so a retry cannot land inside the last attempt.
//
// What stays in the runner is the menu walking itself: reading a Sphere craft
// dialog and choosing the row is genuinely craft-specific, already carefully
// written, and has its own hard-won comment about not counting steps.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

struct CraftRequest {
    // What to make, as the production graph names it.
    const char* item = "";
    // How many to end up holding, on the same "total, not delta" principle
    // that stopped BuyRequest re-buying what was already in the pack.
    i32 desiredTotal = 1;
    // How many of the INPUTS to keep back rather than consuming. A smith that
    // turns every ingot into daggers cannot smith tomorrow.
    i32 minimumMaterialsReserve = 0;
};

enum class CraftStep : u8 {
    Done = 0,        // enough have been made
    Make,            // materials are present; swing
    ShortOfInputs,   // cannot proceed until inputs arrive
    ReserveHit,      // there are inputs, but they are the working reserve
};

const char* CraftStepName(CraftStep s);

struct CraftPlan {
    CraftStep   step = CraftStep::Done;
    i32         remaining = 0;   // how many still wanted
    const char* reason = "";
};

// The pure half. `held` is how many of the OUTPUT are in the pack;
// `inputsAvailable` how many complete sets of ingredients are on hand.
CraftPlan DecideCraft(const CraftRequest& req, i32 held, i32 inputsAvailable);

}  // namespace uo::life
