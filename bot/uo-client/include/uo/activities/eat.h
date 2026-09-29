#pragma once

// ---------------------------------------------------------------------------
// EAT -- one decision about food, for every character
// (docs/BOT_ARCHITECTURE.md sections 3, 11 and 54).
//
// WHAT THE SERVER ACTUALLY TELLS US. Sphere answers `.hungry` -- and volunteers
// the same line as hunger changes -- with one of EIGHT levels
// (core/messages.scp:470-477, CCharAct.cpp:5798 DEFMSG_MSG_HUNGER):
//
//     starving / very hungry / hungry / fairly content
//     content / fed / well fed / stuffed
//
// The life layer collapsed those eight into two booleans, `hungry` and
// `starving`, and that is the whole of this file's reason to exist: "content
// at 12/15" and "two minutes from taking damage" looked the same to the
// planner, while "stuffed" could not be expressed at all.
//
// THE SESSION THAT SHOWED IT. v1_Corwyn, 2026-08-30, 14:41-14:49. He was
// CONTENT -- the save has FOOD=12/15, and no `hungry=1` ever appeared -- and
// still spent four goal picks and three cross-town trips hunting a baker,
// because carrying zero food items scored 0.25 x 250 = 62.5 and everything
// better was blocked. The bot was never going to eat. It was shopping.
//
// So the rule this encodes is not "check before eating" -- the eating was
// already correct. It is:
//
//     BELOW `hungry`, FOOD IS AN ERRAND OF CONVENIENCE.
//     Buy it when you are already standing near someone who sells it.
//     Do not cross a town for it until the server says you are hungry.
//
// Every profession eats, so this belongs to no profession. A miner, a mage
// and a tamer differ in what they can afford, not in when they are hungry.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

// The server's own words, in the server's own order. Worse is lower.
enum class Hunger : u8 {
    Starving = 0,
    VeryHungry,
    Hungry,
    FairlyContent,
    Content,
    Fed,
    WellFed,
    Stuffed,
    // Nothing has been heard yet. NOT the same as being full: a character
    // that has never been told must not conclude it is stuffed.
    Unknown,
};

const char* HungerName(Hunger h);
// Parse the server's line, as a player reads it. Returns false when the text
// is not a hunger report at all.
bool ParseHunger(const char* said, Hunger* out);

struct EatSight {
    Hunger hunger = Hunger::Unknown;
    i32    foodCarried = 0;
    // Is a seller of food within easy reach right now -- same building, same
    // corner of town? The caller decides what "near" means; this decides
    // whether nearness is enough.
    bool   sellerNearby = false;
    i32    gold = 0;
};

struct EatTuning {
    // How many meals a life likes to carry.
    i32 foodToKeep = 4;
    // What food costs, roughly, so affordability is answerable.
    i32 mealPrice = 8;
    i32 goldReserve = 0;
};

enum class EatStep : u8 {
    Nothing = 0,      // fed enough and stocked enough
    EatNow,           // hungry with food in the pack
    BuyNow,           // hungry with none: this is worth a journey
    BuyIfPassing,     // low on stock but not hungry: only if it is free to do
    Forage,           // hungry, no food, no money -- fish, hunt, something
};

const char* EatStepName(EatStep s);

struct EatPlan {
    EatStep     step = EatStep::Nothing;
    const char* reason = "";
};

EatPlan DecideEat(const EatSight& see, const EatTuning& tune);

}  // namespace uo::life
