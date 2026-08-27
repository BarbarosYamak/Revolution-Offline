#pragma once

#include "uo/types.h"

namespace uo::mountpolicy {

// ---------------------------------------------------------------------------
// M3.8 Phase 8 -- deterministic primitives for deciding whether to ride.
//
// NOT autonomous lifestyle AI. Nothing here decides to acquire a mount, tame
// one, buy one or keep one. It answers one question -- "for THIS trip, should
// this character be mounted?" -- from facts the caller already has.
//
// WHAT M3.7.1 MEASURED, and why the answer is not simply "always yes":
//
//   mounted   28.83 s        on foot   52.47 s        ratio 1.82x
//
// over the same 126-tile round trip, same character, same ground. The step
// cadence halves exactly (100 ms vs 200 ms, Event_CheckWalkBuffer
// CClientEvent.cpp:750-762) but a journey is not only steps: planning, replans,
// turn-in-place moves and ack latency are fixed costs a mount does not touch.
//
// So riding SAVES roughly 0.1 s per tile, and COSTS a few seconds to arrange.
// Below some distance that trade is a loss, and a bot that mounts to cross a
// courtyard looks exactly like a bot.
// ---------------------------------------------------------------------------

// Why a decision came out the way it did. Every refusal is explainable; a
// refusal a human cannot read is a bug.
enum class Reason : u8 {
    Ride,              // mount it
    AlreadyMounted,    // nothing to do
    NoMountAvailable,
    TooShort,          // the trip does not repay the overhead
    DestinationIndoors,
    Unsafe,            // war mode, or a hostile nearby
    NotLegal,          // ownership or era rules forbid this animal
    Count,
};

const char* ReasonName(Reason r);

// The saving, per tile, from the measured 1.82x. Deliberately expressed as the
// MEASURED end-to-end figure rather than the theoretical 2x: the cadence halves
// but the journey does not.
inline constexpr double kSecondsSavedPerTile = 0.10;

// What it costs to get on: reach the animal, double-click, wait for layer 25.
// Measured from the M3.7.1 runs, rounded up rather than down -- an optimistic
// overhead makes the bot mount for trips that do not repay it.
inline constexpr double kMountOverheadSeconds = 4.0;

// And to get off again at the far end, when the destination will not admit a
// mount. Dismounting is a double-click on yourself; the cost is the wait, not
// the packet.
inline constexpr double kDismountOverheadSeconds = 3.0;

// Below this, do not bother. Derived, not invented:
//   4.0 s overhead / 0.10 s per tile = 40 tiles to break even.
// Rounded up to 50 so a marginal trip is walked rather than ridden -- being
// wrong in the direction of "walked a bit slower" is cheaper than being wrong
// in the direction of "spent four seconds to save two".
inline constexpr i32 kMinTilesToRide = 50;

struct TripContext {
    // Straight-line tiles to the destination. The planner's own leg count would
    // be better and the caller rarely has it before planning; this is the input
    // that is actually available at decision time.
    i32  distanceTiles = 0;
    bool alreadyMounted = false;
    // A mount this character owns and can reach right now.
    bool ownedMountAvailable = false;
    // True when the mount may not follow -- a bank, a shop, a dungeon stair.
    // Sphere itself does not forbid riding indoors on this shard (MountHeight=0,
    // so the low-ceiling block at CCharStatus.cpp:1958 never fires), but a bot
    // that rides into a bank still has to get out again, and M3.7's one
    // navigation failure was an interior.
    bool destinationIndoors = false;
    // War mode, or a known hostile in range. Mounting is a double-click and
    // takes a beat; doing it while being hit is not what a player would do.
    bool inCombat = false;
    // Ownership and era legality, resolved by the caller from mounts.h. A
    // creature we may not tame is a creature we may not ride.
    bool mountIsLegal = true;
};

struct Decision {
    bool   ride = false;
    Reason reason = Reason::NoMountAvailable;
    // Estimated seconds saved by riding, net of overhead. Negative when riding
    // would cost more than it saves; carried so a caller can log the trade
    // rather than just the verdict.
    double netSecondsSaved = 0.0;
};

// The whole of Phase 8's decision layer.
Decision ShouldUseMountForTravel(const TripContext& ctx);

// Net seconds saved by riding a trip of this length, including getting on and
// (if needed) off again. Exposed separately so the arithmetic is testable
// without constructing a context.
double NetSecondsSaved(i32 distanceTiles, bool mustDismountOnArrival);

}  // namespace uo::mountpolicy
