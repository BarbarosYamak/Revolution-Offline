#pragma once

// ---------------------------------------------------------------------------
// DISPOSAL -- what leaves the pack, and how much of it stays
// (docs/BOT_ARCHITECTURE.md sections 7 and 54).
//
// THE SESSION THIS EXISTS FOR. v4_Corwyn, 2026-08-30 15:36:38. He walked to
// Curtis the blacksmith, said "Curtis sell", and the server answered with a
// list of ELEVEN of his own items that Curtis would buy:
//
//     dagger        x1   18 gp   (x2)
//     heater shield x1   61 gp   (x6)
//     ...
//
// He sold the daggers -- 72 gold -- and walked away from 366 gold of shields
// he had been hauling for three sessions. Two separate faults, and both of
// them are questions of QUANTITY that the old code asked as yes/no:
//
//   1. The errand sold ONE ITEM TYPE and then returned success. The vendor
//      had already named everything else it would take, in the same packet.
//
//   2. `LifeNeedsGraphic` was a boolean. A smith PRODUCES heater shields, so
//      `named(made)` was true, so every shield he owned counted as needed --
//      the sixth as much as the first. "having 6 heater shield that he cant
//      sell to a player now is not good for him so maybe max 1 he can bank
//      rest he should sell" (project owner, 2026-08-30).
//
// So the rule is a KEEP-COUNT per role, never a flag:
//
//     money, food, reagents, crafting inputs   keep what you carry
//     tools                                    keep one of each
//     armour and weapons you can use           keep one -- a spare, not six
//     what you make to sell                    keep none, that IS the stock
//     loot you have no use for                 keep none
//
// The caller says what ROLE a graphic plays for this particular character --
// a heater shield is stock to a smith and armour to a fencer -- and this
// decides how many of them stay. No item table lives here: the vendor's own
// 0x9E list is the authority on what is saleable and for how much.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

// What a graphic MEANS to this character. The same shield is Produce to the
// smith who makes them and Wearable to the fencer who fights with one.
enum class ItemRole : u8 {
    Money = 0,     // never sold
    Consumable,    // food, reagents, bandages -- stock, keep it
    CraftInput,    // ore, ingots, leather -- the next thing you make
    Tool,          // pickaxe, smith hammer -- one of each is enough
    Wearable,      // armour and weapons this life can actually use
    Produce,       // what this life makes in order to sell it
    Unknown,       // loot, and anything with no role -- no reason to keep it
};

const char* ItemRoleName(ItemRole r);

struct DisposalTuning {
    // A spare, not a collection. The owner's rule, 2026-08-30.
    i32 keepWearable = 1;
    i32 keepTool     = 1;
    // Stock exists to be sold. Keeping some back only delays the gold.
    i32 keepProduce  = 0;
    i32 keepUnknown  = 0;
};

// How many of this role a life keeps. Money, consumables and crafting inputs
// return -1, meaning "all of them" -- they are not surplus at any count.
constexpr i32 kKeepEverything = -1;
i32 KeepCount(ItemRole role, const DisposalTuning& tune);

struct DisposalSight {
    ItemRole role = ItemRole::Unknown;
    // How many are in the pack.
    i32 carried = 0;
    // How many of them THIS vendor listed as willing to buy, and at what
    // price each. Both come from the server's own 0x9E answer.
    i32 vendorTakes = 0;
    i32 pricePerUnit = 0;
    // A vendor's purse is finite; the sell path halves the lot after a silent
    // refusal. 0 means no cap in force.
    i32 lotCap = 0;
};

enum class DisposalStep : u8 {
    Keep = 0,   // it stays in the pack, and the reason says why
    Sell,       // offer `quantity` of it to this vendor
};

const char* DisposalStepName(DisposalStep s);

struct DisposalPlan {
    DisposalStep step = DisposalStep::Keep;
    i32          quantity = 0;
    const char*  reason = "";
};

DisposalPlan DecideDisposal(const DisposalSight& see,
                            const DisposalTuning& tune);

}  // namespace uo::life
