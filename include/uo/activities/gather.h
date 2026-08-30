#pragma once

// ---------------------------------------------------------------------------
// GATHER -- one framework for chopping, mining and fishing
// (docs/BOT_ARCHITECTURE.md section 22).
//
// Three handlers in this runner answer the same question in three different
// ways: DoGatherLogs, DoMine and DoFish. The question is
//
//     there is a tool, a place and a load -- swing, move, or stop?
//
// and every one of the answers below was learned the hard way:
//
//   "THIS AREA IS DONE" IS A REASON TO GO SOMEWHERE ELSE, NOT TO STOP.
//   GATHER_LOGS completed forty times with progress=0 in under two minutes,
//   because "every tree within 24 tiles is worked out" returned goal-complete
//   -- handing control to a planner with no new information, which re-picked
//   the same goal, for the same character, standing in the same clearing.
//   That is the M4 Session L churn (22 goals attempted, 1 completed) finally
//   made visible.
//
//   A FULL PACK IS NOT A REASON TO KEEP SWINGING. Weight is checked before
//   the swing, not after the trip, or the character walks to town at 100%
//   capacity having spent the last ten swings gaining nothing.
//
//   THE TOOL MUST BE IN HAND, not merely owned. Sphere's mining reads
//   SRC.WEAPON.USESCUR, so a pickaxe in the backpack mines nothing -- Corran
//   carried one for a whole session and dug no ore.
//
// The variation between the three trades is DATA -- which tool, which
// resource, how far the reach -- not three copies of this logic.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

struct GatherRequest {
    // What is being gathered, as the world model names it ("logs", "ore").
    const char* resource = "";
    // A load worth taking to town. Not a cap on the pack -- a threshold at
    // which the trip pays for itself.
    i32 loadWorthTaking = 20;
    // Above this fraction of carrying capacity, stop and deal with the load.
    double packFullFraction = 0.70;
    // Does this trade need the tool actually WIELDED (mining, lumberjacking)
    // or merely carried?
    bool toolMustBeWielded = true;
};

// What the world looks like right now.
struct GatherSight {
    i32    held = 0;              // how much of the resource is carried
    double weightFraction = 0.0;  // 0..1 of capacity
    bool   toolInPack = false;
    bool   toolWielded = false;
    bool   targetInReach = false; // a tree/vein/water within working range
    bool   areaWorkedOut = false; // everything in range is spent
};

enum class GatherStep : u8 {
    Swing = 0,      // there is something here to work
    ArmTool,        // the tool is carried but not in hand
    LeaveArea,      // worked out: go somewhere else, do NOT report done
    TakeItIn,       // the load is worth a trip, or the pack is full
    NeedTool,       // no tool at all
    Done,           // nothing to gather and nothing to carry
};

const char* GatherStepName(GatherStep s);

struct GatherPlan {
    GatherStep  step = GatherStep::Done;
    const char* reason = "";
};

GatherPlan DecideGather(const GatherRequest& req, const GatherSight& sight);

}  // namespace uo::life
