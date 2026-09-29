#pragma once

// ---------------------------------------------------------------------------
// TRAIN -- raise a skill, by whichever method actually works for it
// (docs/BOT_ARCHITECTURE.md sections 18 and 20).
//
// THE OWNER'S DISTINCTION, which the code kept losing:
//
//   "npc training and training are different, normal training is doing
//    actions to level up your skill to 100"
//
// A guildmaster sells a SHORTCUT to 30.0 for a few hundred gold. Everything
// above that is earned by doing the thing -- casting, swinging, smithing.
// Conflating the two produced a miner with a pickaxe, 50.0 Mining and 50.0
// Blacksmithing who spent an entire session walking between tinkers and never
// once swung at rock, because "buy a tenth" outscored "do the job".
//
// THE CEILING IS PER-NPC, AND LOW. Source-X teaches 30% of the trainer's own
// value (NPC_GetTrainMax), so an ordinary vendor rolling MEDITATION={50 75}
// caps out around 22.5 -- which is why Ysolde, at 21.9, was told "you already
// know as much as I can teach" and the code wrongly wrote off the SKILL
// rather than that TRAINER. A guildmaster carries
// TAG.OVERRIDE.TRAINSKILLMAX=50.0 and can actually teach to 30.
//
// AND PAYING IS NOT LEARNING. `training_unverified` appears a dozen times in
// one fleet run: gold recorded as GOLD_DESTROYED_TRAINER with no confirmed
// skill gain. Section 18 says what success means here -- the server's own
// 0x3A value moved -- and the gold half matters just as much, because
// "the fee was taken and nothing was learned" and "the trainer refused and
// kept nothing" are completely different problems wearing the same log line.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

enum class TrainMethod : u8 {
    None = 0,
    // Pay an NPC. Fast, bounded, and only useful below the ceiling.
    BuyFromNpc,
    // Do the thing. The only route above the NPC ceiling, and the only route
    // to 100.
    Practise,
};

const char* TrainMethodName(TrainMethod m);

struct TrainRequest {
    int  skillId = -1;
    // Where this build wants the skill to end up, in tenths.
    i32  targetTenths = 0;
    // What an NPC of this kind can teach up to, in tenths. Per-NPC and
    // discovered, never assumed: an ordinary vendor is around 225, a
    // guildmaster 300.
    i32  npcCeilingTenths = 0;
    // What this life is willing to pay for a lesson, and what it has.
    i32  feeQuoted = 0;
    i32  gold = 0;
    // Whether the catalogue thinks this skill is worth buying tenths of at
    // all -- some are far cheaper to grind.
    bool worthBuying = true;
};

enum class TrainStep : u8 {
    Done = 0,        // the target is reached
    Buy,             // an NPC can still teach this, and it is affordable
    Practise,        // do the thing: above the ceiling, or not worth buying
    CannotAfford,    // an NPC could teach it but the purse says no
};

const char* TrainStepName(TrainStep s);

struct TrainPlan {
    TrainStep   step = TrainStep::Done;
    TrainMethod method = TrainMethod::None;
    const char* reason = "";
};

// The pure half. `haveTenths` is the server's own current value.
TrainPlan DecideTrain(const TrainRequest& req, i32 haveTenths);

}  // namespace uo::life
