#pragma once

// ---------------------------------------------------------------------------
// HEAL -- patch up, and know which of the four ways is open
// (docs/BOT_ARCHITECTURE.md sections 3 and 47).
//
// Every profession heals; they simply have different things to hand. A
// warrior has bandages because Healing and Anatomy are in the build, a mage
// has a spell and the mana for it, anyone may have bought a potion, and
// everyone can sit still and wait. That is four methods and one decision,
// not four handlers.
//
// THE DEADLOCK THIS EXISTS TO NAME.
//
// Kaelen spent an entire session inside it, and the log could not say so:
//
//     hungry -> no HP regeneration -> under the health bar -> cannot hunt
//            -> cannot earn -> cannot buy food -> hungry
//
// He climbed from 10/32 to 25/32 -- 78%, two points short of the bar that
// would have let him hunt -- and idled for 73% of his picks while every
// other need reported BLOCKED. Nothing was broken. Every individual refusal
// was correct. What was missing was any way to say "all four doors are shut
// AND resting cannot open them", which is a different state from "resting".
//
// Section 47 asks fleets to detect bots that are alive and doing nothing.
// This is where that starts: Stuck is a real answer, and it is the one that
// justifies hunting at half health because waiting is not caution when
// nothing is coming.
//
// THE POOR BRANCH IS THE OWNER'S RULE, verbatim: "if warrior economy is good
// then he can buy bandage and potion, otherwise go get yourself wool make
// bandage". Making bandages is what a character does when it CANNOT pay, and
// firing it for a character who can simply wastes the afternoon.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

struct HealSight {
    i32    hp = 1;
    i32    hpMax = 1;
    i32    mana = 0;
    i32    bandages = 0;
    i32    healPotions = 0;
    // Can this character cast a healing spell at all (spell known, not in
    // metal armour, and so on)? The caller answers; this does not guess.
    bool   canCastHeal = false;
    i32    gold = 0;
    // Wool or cloth in the pack, for the poor branch.
    bool   hasBandageMaterial = false;
    // Hunger stops regeneration on this shard, which is what turned Kaelen's
    // afternoon into a deadlock.
    bool   hungry = false;
    bool   inDanger = false;      // something is attacking right now

    double HpFraction() const {
        return hpMax > 0 ? static_cast<double>(hp) / hpMax : 0.0;
    }
};

struct HealTuning {
    // Below this, do something about it.
    double healHpFraction = 0.80;
    // What a bandage costs, roughly, so "can I just buy some" is answerable.
    i32    bandagePrice = 2;
    // Gold this life will not spend on consumables.
    i32    goldReserve = 0;
};

enum class HealStep : u8 {
    None = 0,      // healthy enough; nothing to do
    Bandage,
    DrinkPotion,
    CastHeal,
    BuySupplies,   // nothing to hand, but the purse can fix that
    MakeBandages,  // the POOR branch: no money, but wool
    Rest,          // nothing else available and regeneration will work
    Stuck,         // nothing available AND resting cannot help
};

const char* HealStepName(HealStep s);

struct HealPlan {
    HealStep    step = HealStep::None;
    const char* reason = "";
};

HealPlan DecideHeal(const HealSight& see, const HealTuning& tune);

}  // namespace uo::life
