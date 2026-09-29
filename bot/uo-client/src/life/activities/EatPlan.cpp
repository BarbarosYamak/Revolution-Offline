#include "uo/activities/eat.h"

#include <cstring>

// The arithmetic of eating, in its own translation unit so ctest reaches it
// without Client.

namespace uo::life {

const char* HungerName(Hunger h) {
    switch (h) {
        case Hunger::Starving:      return "starving";
        case Hunger::VeryHungry:    return "very hungry";
        case Hunger::Hungry:        return "hungry";
        case Hunger::FairlyContent: return "fairly content";
        case Hunger::Content:       return "content";
        case Hunger::Fed:           return "fed";
        case Hunger::WellFed:       return "well fed";
        case Hunger::Stuffed:       return "stuffed";
        case Hunger::Unknown:       return "unknown";
    }
    return "?";
}

const char* EatStepName(EatStep s) {
    switch (s) {
        case EatStep::Nothing:      return "nothing";
        case EatStep::EatNow:       return "eat";
        case EatStep::BuyNow:       return "buy food now";
        case EatStep::BuyIfPassing: return "buy food if passing";
        case EatStep::Forage:       return "forage";
    }
    return "?";
}

bool ParseHunger(const char* said, Hunger* out) {
    if (!said || !out) return false;
    // ORDER MATTERS: "very hungry" contains "hungry", and "well fed" contains
    // "fed". Longest and most specific first, or a starving character reads
    // as merely hungry -- which is the difference between eating now and
    // taking damage.
    struct Row { const char* text; Hunger level; };
    static const Row kRows[] = {
        {"very hungry",    Hunger::VeryHungry},
        {"fairly content", Hunger::FairlyContent},
        {"well fed",       Hunger::WellFed},
        {"starving",       Hunger::Starving},
        {"stuffed",        Hunger::Stuffed},
        {"content",        Hunger::Content},
        {"hungry",         Hunger::Hungry},
        {"fed",            Hunger::Fed},
    };
    for (const Row& r : kRows) {
        if (std::strstr(said, r.text)) { *out = r.level; return true; }
    }
    return false;
}

EatPlan DecideEat(const EatSight& see, const EatTuning& tune) {
    EatPlan out;

    const bool actuallyHungry = see.hunger <= Hunger::Hungry &&
                                see.hunger != Hunger::Unknown;

    // --- hungry: this is worth interrupting the day for -------------------
    if (actuallyHungry) {
        if (see.foodCarried > 0) {
            out.step = EatStep::EatNow;
            out.reason = "the server says hungry, and there is food in the pack";
            return out;
        }
        const i32 spendable = see.gold - tune.goldReserve;
        if (spendable >= tune.mealPrice) {
            out.step = EatStep::BuyNow;
            out.reason = "hungry with an empty pack -- worth a journey";
            return out;
        }
        // Hungry, nothing to eat and nothing to buy with. Fishing, hunting,
        // or anything that produces a meal. Not a blockage: hunger is one of
        // the few needs a character can always answer with its own hands.
        out.step = EatStep::Forage;
        out.reason = "hungry with no food and no money -- go and get some";
        return out;
    }

    // --- not hungry: stock is a convenience, not an errand ----------------
    //
    // THE RULE THIS FILE EXISTS FOR. Corwyn was CONTENT and still made three
    // cross-town trips after a baker, because carrying no food scored higher
    // than everything that was blocked. Below `hungry`, food is worth buying
    // only when it is nearly free to do so.
    if (see.foodCarried < tune.foodToKeep) {
        if (!see.sellerNearby) {
            out.step = EatStep::Nothing;
            out.reason = "low on food but not hungry -- not worth crossing a "
                         "town for";
            return out;
        }
        const i32 spendable = see.gold - tune.goldReserve;
        if (spendable < tune.mealPrice) {
            out.step = EatStep::Nothing;
            out.reason = "would top up in passing, but cannot afford to";
            return out;
        }
        out.step = EatStep::BuyIfPassing;
        out.reason = "standing beside a seller with room for more -- top up";
        return out;
    }

    out.step = EatStep::Nothing;
    out.reason = "fed enough and carrying enough";
    return out;
}

}  // namespace uo::life
