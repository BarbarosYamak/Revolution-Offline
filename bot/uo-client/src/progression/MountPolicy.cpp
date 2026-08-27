#include "uo/mount_policy.h"

namespace uo::mountpolicy {

const char* ReasonName(Reason r) {
    switch (r) {
        case Reason::Ride:               return "RIDE";
        case Reason::AlreadyMounted:     return "ALREADY_MOUNTED";
        case Reason::NoMountAvailable:   return "NO_MOUNT_AVAILABLE";
        case Reason::TooShort:           return "TOO_SHORT";
        case Reason::DestinationIndoors: return "DESTINATION_INDOORS";
        case Reason::Unsafe:             return "UNSAFE";
        case Reason::NotLegal:           return "NOT_LEGAL";
        case Reason::Count:              break;
    }
    return "?";
}

double NetSecondsSaved(i32 distanceTiles, bool mustDismountOnArrival) {
    if (distanceTiles <= 0) return 0.0;
    const double gross = distanceTiles * kSecondsSavedPerTile;
    double overhead = kMountOverheadSeconds;
    if (mustDismountOnArrival) overhead += kDismountOverheadSeconds;
    return gross - overhead;
}

Decision ShouldUseMountForTravel(const TripContext& ctx) {
    Decision d{};
    d.netSecondsSaved = NetSecondsSaved(ctx.distanceTiles, ctx.destinationIndoors);

    // ORDER MATTERS, and it is the order a player would think in.
    //
    // Already mounted comes first because it is the cheapest true answer: a
    // character on a horse asking whether to get one is a question with no
    // work attached. It is NOT "ride = true" -- there is nothing to do.
    if (ctx.alreadyMounted) {
        d.ride = false;
        d.reason = Reason::AlreadyMounted;
        return d;
    }

    // Legality before availability. A mount we may not ride is not a mount we
    // have, and answering "no mount available" would hide an authenticity
    // refusal behind a logistics one.
    if (!ctx.mountIsLegal) {
        d.reason = Reason::NotLegal;
        return d;
    }

    if (!ctx.ownedMountAvailable) {
        d.reason = Reason::NoMountAvailable;
        return d;
    }

    // Safety before economics. Mounting takes a beat, and doing it mid-fight is
    // not what a player does -- nor is it what an observer would believe.
    if (ctx.inCombat) {
        d.reason = Reason::Unsafe;
        return d;
    }

    // Indoors is a REFUSAL, not a cost adjustment.
    //
    // This shard sets MountHeight=0, so Source-X never applies the low-ceiling
    // block at CCharStatus.cpp:1958 and a mounted character can physically ride
    // into a bank. The refusal is ours, and it is deliberate: M3.7's single
    // navigation failure was an interior, mobiles block paths with no expiry,
    // and adding a horse to a crowded bank doorway is adding a blocker to the
    // one place this project has actually seen a bot get stuck.
    if (ctx.destinationIndoors) {
        d.reason = Reason::DestinationIndoors;
        return d;
    }

    // Finally the trade. Two gates that must agree: a distance floor, and the
    // arithmetic actually coming out positive. The floor exists because the
    // arithmetic alone would say yes at 41 tiles, where the saving is a fifth
    // of a second and the bot has visibly stopped to climb onto a horse.
    if (ctx.distanceTiles < kMinTilesToRide || d.netSecondsSaved <= 0.0) {
        d.reason = Reason::TooShort;
        return d;
    }

    d.ride = true;
    d.reason = Reason::Ride;
    return d;
}

}  // namespace uo::mountpolicy
