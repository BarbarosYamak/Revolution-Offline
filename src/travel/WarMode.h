#pragma once

// ---------------------------------------------------------------------------
// WarModeWatchdog — decides when a bot should drop out of war mode.
//
// M2 left bots able to enter war mode and never leave it, which is both wrong
// behaviour (a player walking through Britain in war mode is a player about to
// be attacked) and a hazard for everything that follows: a bot that banks,
// shops or crafts with its weapon out reads as hostile to every guard and
// every other bot.
//
// The rule is boring on purpose. War mode is a state the SERVER owns -- we
// know we are in it because of 0x72, and we leave it by sending 0x72 -- so
// this object holds no fake state. It only answers "should we ask to leave
// now?" from what the session has observed: whether a target is still valid,
// whether anything hostile has happened lately, and what the bot is trying to
// do instead.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::travel {

// Why a bot is allowed to be in war mode at all. Travel, banking, shopping and
// crafting are all Peace intents; the watchdog exists to notice that the
// combat intent ended and the peace intent took over.
enum class CombatIntent : u8 {
    None = 0,      // nothing is being fought; war mode is stale
    Fighting,      // an attack was accepted and the target is alive
    Defending,     // something is hitting us
    Count,
};

const char* CombatIntentName(CombatIntent i);

struct WarModeLimits {
    // No combat event for this long with no live target -> drop to peace.
    // Long enough to cover a real lull (a fleeing target, a bandage) and short
    // enough that a bot does not walk into the next town armed.
    i64 idleTimeoutMs = 15000;
    // A target that has not been seen at all for this long is gone, whatever
    // the reason: it died, it ran out of range, or it logged out.
    i64 targetLostMs = 8000;
};

class WarModeWatchdog {
public:
    void SetLimits(const WarModeLimits& l) { limits_ = l; }
    const WarModeLimits& Limits() const { return limits_; }

    // The server told us what war mode actually is (0x72). Never set from a
    // local guess.
    void OnServerWarMode(bool on, i64 nowMs);
    bool ServerWarMode() const { return warMode_; }

    // The bot deliberately drew its weapon. That is not a combat intent -- no
    // target exists yet -- but it does clear any standing peaceful intent and
    // start the idle clock, so drawing is not instantly undone.
    void OnWarModeRequested(i64 nowMs);
    // A deliberate combat intent began (we asked to attack, and the server
    // accepted with 0xAA carrying the serial we asked for).
    void OnCombatIntent(u32 targetSerial, i64 nowMs);
    // Anything that counts as combat still happening: a swing, a hit, our own
    // health dropping, the target's health changing.
    void OnCombatEvent(i64 nowMs);
    // The target is provably gone (0x1D, death animation, out of range purge).
    void OnTargetGone(u32 serial, i64 nowMs);
    // The bot is doing something a player would sheathe for.
    void OnPeacefulIntent(i64 nowMs);

    u32  TargetSerial() const { return target_; }
    CombatIntent Intent() const { return intent_; }

    // True when the session should send a 0x72 asking for peace. Never true
    // when the server has not told us we are in war mode.
    bool ShouldExitWar(i64 nowMs) const;
    // Why, for the log line. Static string.
    const char* ExitReason(i64 nowMs) const;

    // The session sent the request; stop asking until the server answers or
    // something changes.
    void NoteExitRequested(i64 nowMs);

    void Reset();

private:
    WarModeLimits limits_;
    bool warMode_ = false;
    CombatIntent intent_ = CombatIntent::None;
    u32  target_ = 0;
    // Timestamps carry an explicit "has one" flag rather than treating 0 as
    // unset. A monotonic session clock legitimately starts near zero, and a
    // 0-means-never sentinel silently disables the timeout for the first
    // moments of a session -- which is exactly when a bot is most likely to be
    // handed a fight.
    bool hasCombat_ = false;
    i64  lastCombatMs_ = 0;
    bool hasSeenTarget_ = false;
    i64  lastSeenTargetMs_ = 0;
    i64  peacefulSinceMs_ = 0;
    bool peacefulIntent_ = false;
    bool exitRequested_ = false;
    i64  exitRequestedMs_ = 0;
};

} // namespace uo::travel
