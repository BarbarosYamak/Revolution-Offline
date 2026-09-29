#include "uo/activities/heal.h"

// The arithmetic of patching up, in its own translation unit so ctest reaches
// it without Client.

namespace uo::life {

const char* HealStepName(HealStep s) {
    switch (s) {
        case HealStep::None:         return "healthy";
        case HealStep::Bandage:      return "bandage";
        case HealStep::DrinkPotion:  return "drink a potion";
        case HealStep::CastHeal:     return "cast a heal";
        case HealStep::BuySupplies:  return "buy supplies";
        case HealStep::MakeBandages: return "make bandages";
        case HealStep::Rest:         return "rest";
        case HealStep::Stuck:        return "stuck";
    }
    return "?";
}

HealPlan DecideHeal(const HealSight& see, const HealTuning& tune) {
    HealPlan out;

    if (see.HpFraction() >= tune.healHpFraction) {
        out.step = HealStep::None;
        out.reason = "healthy enough to get on with the day";
        return out;
    }

    // --- what is already to hand, cheapest first --------------------------
    //
    // A potion is instant and a bandage takes seconds, so ORDER BY DANGER
    // rather than by cost: mid-fight the potion is the only one that lands.
    if (see.inDanger && see.healPotions > 0) {
        out.step = HealStep::DrinkPotion;
        out.reason = "under attack, and a potion is the only thing fast enough";
        return out;
    }
    if (see.useBandages && see.bandages > 0) {
        out.step = HealStep::Bandage;
        out.reason = "bandages in the pack, which is what they are for";
        return out;
    }
    if (see.canCastHeal && see.mana > 0) {
        out.step = HealStep::CastHeal;
        out.reason = "no bandages, but the mana to do without them";
        return out;
    }
    if (see.healPotions > 0) {
        out.step = HealStep::DrinkPotion;
        out.reason = "nothing else to hand";
        return out;
    }

    // --- nothing to hand: can this be fixed with money? -------------------
    const i32 spendable = see.gold - tune.goldReserve;
    // A freshly raised character should regenerate at the healer before
    // walking an equipment-shopping circuit. Hunger needs food instead.
    const bool shopIsRightHere =
        see.supplyDistance >= 0 && see.supplyDistance <= tune.nearShopTiles;
    if (!see.inDanger && see.HpFraction() < tune.minHpToShop &&
        !(shopIsRightHere && see.canBuySupplies && spendable >= tune.bandagePrice)) {
        out.step = see.hungry ? HealStep::Stuck : HealStep::Rest;
        out.reason = see.hungry ? "need food before regeneration can help"
                               : "recover health here before a shopping trip";
        return out;
    }
    if (see.canBuySupplies && spendable >= tune.bandagePrice) {
        out.step = HealStep::BuySupplies;
        out.reason = "nothing to hand, but enough gold to fix that";
        return out;
    }

    // THE POOR BRANCH, and only the poor branch. "if warrior economy is good
    // then he can buy bandage and potion, otherwise go get yourself wool make
    // bandage" (project owner). Firing this for a character that could simply
    // pay wastes the afternoon.
    if (see.useBandages && see.hasBandageMaterial) {
        out.step = HealStep::MakeBandages;
        out.reason = "too poor to buy them, but there is cloth to cut up";
        return out;
    }

    // THE DEADLOCK, NAMED.
    //
    // Hunger stops regeneration on this shard, so a hungry character with no
    // supplies and no money is not resting -- it is waiting for something
    // that will never arrive. Kaelen climbed from 10/32 to 25/32 and idled
    // for 73% of his picks in exactly this state, with every other need
    // reporting BLOCKED and nothing able to say why.
    //
    // Saying STUCK is what justifies the exits that look reckless: hunting at
    // half health is not bravado when waiting cannot help.
    if (see.hungry) {
        out.step = HealStep::Stuck;
        out.reason = "hungry, so no regeneration -- with no supplies and no "
                     "money, resting cannot fix this";
        return out;
    }

    out.step = HealStep::Rest;
    out.reason = "nothing to heal with, but regeneration still works";
    return out;
}

}  // namespace uo::life
