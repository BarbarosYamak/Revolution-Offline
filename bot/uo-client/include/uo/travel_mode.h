#pragma once

// ---------------------------------------------------------------------------
// How a character legitimately gets somewhere (M3.6).
//
// M2.5 answered "what is the route?". This answers the question before it:
// "which of the ways I actually own is worth using?" -- and it sits ABOVE the
// world planner, so nothing here can invent a coordinate or bypass a rule:
//
//     semantic goal -> [travel mode choice] -> world route -> A* -> SubmitStep()
//
// Every mode is a thing the character does with real mechanics. There is no
// "teleport to X,Y".
//
// The rules encoded here are the shard's own:
//
//   [SPELL 32] Recall  SKILLREQ=MAGERY 40.0  MANAUSE=11
//   Runebook charges are stored Recall SCROLLS (OFFICIAL_REVOLUTION_UPDATE
//   13.05.2009: "if you use the runebook with charges (by adding recall
//   scrolls) you will not need the magery skill") -- and a scroll cast has
//   never needed the caster's own skill, which is exactly why that rule works.
//   Guild runebooks are 07.01.2012 and OUTSIDE the target profile.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::travelmode {

enum class Mode : u8 {
    Walk = 0,          // always available; the fallback that cannot fail
    Moongate,          // public gates, proven in M2.5
    LooseRuneRecall,   // Recall at a marked rune carried in the pack
    RunebookRecall,    // Recall from a runebook page
    Count,
};

const char* ModeName(Mode m);

// What this character can do right now, and what it owns.
struct Capability {
    i32  mageryTenths = 0;
    int  manaNow = 0;
    bool haveReagents = true;    // this runtime has ReagentsRequired=0
    bool dead = false;
    bool inCombat = false;

    // Destination ownership. "For this destination", not in general.
    bool haveMarkedRune = false;      // a loose rune marked at the goal
    bool haveRunebookPage = false;    // a runebook page holding the goal
    int  runebookCharges = 0;         // stored Recall scrolls
    bool moongateRouteKnown = false;  // a gate pair that helps

    // The shard's numbers, overridable for tests.
    i32 recallSkillTenths = 400;
    int recallMana = 11;
};

struct Option {
    Mode        mode = Mode::Walk;
    bool        usable = false;
    std::string why;              // why not, when unusable
    i32         estimatedSeconds = 0;
    int         manaCost = 0;
    int         chargeCost = 0;
    // A rune wears out and is eventually destroyed (measured live: Sphere
    // decrements a rune's charges per use and then deletes it). A runebook page
    // does not, because the book re-cuts the rune from the point it stored.
    bool        consumesRune = false;
};

// Rough walking time. Running is ~2 tiles/second on this shard, and real routes
// are not straight, so this is deliberately pessimistic rather than precise --
// it only has to be good enough to prefer a Recall over a continent crossing.
i32 EstimateWalkSeconds(i32 tiles);

// Can this character use this mode for a destination it owns as described?
Option Evaluate(Mode m, const Capability& c, i32 walkTiles);

// Every mode, evaluated, best first. Unusable options are included WITH their
// reason -- a planner that only sees what works cannot explain itself, and a
// bot that cannot explain itself cannot be debugged.
std::vector<Option> Rank(const Capability& c, i32 walkTiles);

// The mode to use. Never fails: Walk is always last and always usable.
Mode Choose(const Capability& c, i32 walkTiles);

} // namespace uo::travelmode
