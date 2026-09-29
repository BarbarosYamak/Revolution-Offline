#include "uo/activities/train.h"

// The arithmetic of training, in its own translation unit so ctest reaches it
// without Client -- the same split as BuyPlan, AcquirePlan and CraftPlan.

namespace uo::life {

const char* TrainMethodName(TrainMethod m) {
    switch (m) {
        case TrainMethod::None:       return "none";
        case TrainMethod::BuyFromNpc: return "npc trainer";
        case TrainMethod::Practise:   return "practise";
    }
    return "?";
}

const char* TrainStepName(TrainStep s) {
    switch (s) {
        case TrainStep::Done:         return "done";
        case TrainStep::Buy:          return "buy";
        case TrainStep::Practise:     return "practise";
        case TrainStep::CannotAfford: return "cannot afford";
    }
    return "?";
}

TrainPlan DecideTrain(const TrainRequest& req, i32 haveTenths) {
    TrainPlan out;

    if (req.skillId < 0) {
        out.step = TrainStep::Done;
        out.reason = "no skill named";
        return out;
    }

    if (haveTenths >= req.targetTenths) {
        out.step = TrainStep::Done;
        out.reason = "the build's target for this skill is met";
        return out;
    }

    // ABOVE THE NPC CEILING THERE IS ONLY DOING THE THING.
    //
    // Sphere teaches 30% of the trainer's own value, so an ordinary vendor
    // tops out around 22.5 and a guildmaster at 30.0. Past that, no amount of
    // gold moves the number -- which is the fact Ysolde's session turned into
    // "the skill is unbuyable" when it only meant "not from HIM".
    if (haveTenths >= req.npcCeilingTenths) {
        out.step = TrainStep::Practise;
        out.method = TrainMethod::Practise;
        out.reason = "already at what an NPC of this kind can teach";
        return out;
    }

    // "npc training and training are different, normal training is doing
    // actions to level up your skill to 100" (project owner). Some skills are
    // simply cheaper to grind, and the catalogue says which.
    if (!req.worthBuying) {
        out.step = TrainStep::Practise;
        out.method = TrainMethod::Practise;
        out.reason = "this build would rather grind this one than buy it";
        return out;
    }

    if (req.feeQuoted > 0 && req.gold < req.feeQuoted) {
        // NOT a failure of the skill: earn, come back. Distinct from
        // Practise, because the answer changes when the purse does.
        out.step = TrainStep::CannotAfford;
        out.method = TrainMethod::BuyFromNpc;
        out.reason = "a lesson is affordable in principle but not today";
        return out;
    }

    out.step = TrainStep::Buy;
    out.method = TrainMethod::BuyFromNpc;
    out.reason = "below the ceiling, worth buying, and affordable";
    return out;
}

}  // namespace uo::life
