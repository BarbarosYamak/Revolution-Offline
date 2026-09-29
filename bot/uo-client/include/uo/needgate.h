// The need/handler contract. See docs/NEED_HANDLER_CONTRACT.md.
//
// A need may score only if its handler would act on it. There is one place
// that question is asked -- CanAct -- and two ways a handler may answer:
//
//   Arm A (pure)     the precondition is computable from the Observation, so
//                    it lives HERE and both the need site in AssessNeeds and
//                    the handler's first tick call it. One rule, two callers.
//   Arm B (observed) the precondition needs the Client, a runner-private table
//                    or a travel result. The handler's refusal writes a block
//                    with the same words (NoteNeedBlocked) and CanAct reads it
//                    back (NeedBlockActive). One mechanism, not two.
//
// Before this existed, seven pairs in one live wave scored a need the handler
// then refused, five of them until the goal_spinning backstop fired
// (artifacts/validation_wave_2026-09-06.md).
//
// NOTHING HERE MAY READ HIDDEN SERVER STATE. Every rule in this file was
// already being applied by a handler on the same observation; this is where
// they were moved to, not new knowledge.
#pragma once

#include <string>

#include "uo/life.h"

namespace uo::life {

// What the question is ABOUT. Both sides already hold this: the need site has
// the craft input it costed, the handler has supplyItem_.
struct GateSubject {
    // A craft input, consumable or other item defname. Optional.
    const char* item = nullptr;
    // True when the caller is asking about the hunting half of NeedTraining
    // (TRAIN_COMBAT) rather than an ordinary skill grind.
    bool hunting = false;
};

struct GateVerdict {
    bool        ok = true;
    // The handler's own sentence. Empty when ok.
    std::string why;
};

// How long a block stands.
enum class BlockScope : u8 {
    // The rest of this session. NeedConfig::sessionIndex is the only durable
    // clock a need has: an ms stand-down dies with the process (bot-brain
    // memory ms-stand-downs-die-with-the-process).
    Session,
    // A fact the world undoes on its own -- a drained shelf, a flock that
    // wandered off. kBlockWindowMs from the moment it was written.
    Window,
};

// Sphere's own shop restock cadence, the same number the drained-counter
// readers in Needs.cpp use for the same reason.
inline constexpr i64 kBlockWindowMs = 10 * 60 * 1000;

// Arm B, writing half. Called from a handler at the point it refuses.
void NoteNeedBlocked(Memory& mem, NeedKind kind, const char* reason,
                     BlockScope scope, const NeedConfig& cfg, i64 nowMs);

// Arm B, reading half. `why` receives the recorded reason when it returns
// true. Exposed on its own so a handler can ask "am I already stood down"
// without re-deriving the arm-A rules.
bool NeedBlockActive(const Memory& mem, NeedKind kind, const NeedConfig& cfg,
                     const Observation& obs, std::string* why);

// THE ONE QUESTION. Arm B first (an observed refusal outranks a guess), then
// the arm-A rules for this kind.
GateVerdict CanAct(NeedKind kind, const GateSubject& subject, const Memory& mem,
                   const Observation& obs, const NeedConfig& cfg);

}  // namespace uo::life
