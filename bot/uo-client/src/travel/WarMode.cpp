#include "travel/WarMode.h"

namespace uo::travel {

namespace {

const char* const kIntentNames[] = { "none", "fighting", "defending" };
static_assert(sizeof(kIntentNames) / sizeof(kIntentNames[0]) ==
                  static_cast<usize>(CombatIntent::Count),
              "kIntentNames is out of step with CombatIntent");

// After asking for peace, give the server a moment to answer before asking
// again. Sphere's flood protection punishes repetition (M2), so the retry
// interval is generous.
constexpr i64 kExitRetryMs = 3000;

} // namespace

const char* CombatIntentName(CombatIntent i) {
    const usize n = static_cast<usize>(i);
    return n < static_cast<usize>(CombatIntent::Count) ? kIntentNames[n] : "?";
}

void WarModeWatchdog::Reset() {
    warMode_ = false;
    intent_ = CombatIntent::None;
    target_ = 0;
    hasCombat_ = false;
    lastCombatMs_ = 0;
    hasSeenTarget_ = false;
    lastSeenTargetMs_ = 0;
    peacefulSinceMs_ = 0;
    peacefulIntent_ = false;
    exitRequested_ = false;
    exitRequestedMs_ = 0;
}

void WarModeWatchdog::OnServerWarMode(bool on, i64 nowMs) {
    warMode_ = on;
    exitRequested_ = false;
    if (!on) {
        intent_ = CombatIntent::None;
        target_ = 0;
        return;
    }
    // Entering war mode is itself a combat event: it restarts the idle clock,
    // so a bot that draws its weapon is not immediately told to sheathe.
    if (!hasCombat_) {
        hasCombat_ = true;
        lastCombatMs_ = nowMs;
    }
}

void WarModeWatchdog::OnWarModeRequested(i64 nowMs) {
    hasCombat_ = true;
    lastCombatMs_ = nowMs;
    peacefulIntent_ = false;
    exitRequested_ = false;
    // Intent stays None on purpose: there is still nothing to fight, so the
    // moment the bot goes back to travelling or shopping the watchdog is right
    // to put the weapon away.
}

void WarModeWatchdog::OnCombatIntent(u32 targetSerial, i64 nowMs) {
    target_ = targetSerial;
    intent_ = CombatIntent::Fighting;
    hasCombat_ = true;
    lastCombatMs_ = nowMs;
    hasSeenTarget_ = true;
    lastSeenTargetMs_ = nowMs;
    peacefulIntent_ = false;
    exitRequested_ = false;
}

void WarModeWatchdog::OnCombatEvent(i64 nowMs) {
    hasCombat_ = true;
    lastCombatMs_ = nowMs;
    hasSeenTarget_ = true;
    lastSeenTargetMs_ = nowMs;
    if (intent_ == CombatIntent::None) intent_ = CombatIntent::Defending;
    peacefulIntent_ = false;
    exitRequested_ = false;
}

void WarModeWatchdog::OnTargetGone(u32 serial, i64 nowMs) {
    if (serial && serial != target_) return;
    target_ = 0;
    intent_ = CombatIntent::None;
    hasSeenTarget_ = true;
    lastSeenTargetMs_ = nowMs;
}

void WarModeWatchdog::OnTargetSeen(u32 serial, i64 nowMs) {
    if (!serial || serial != target_) return;
    hasSeenTarget_ = true;
    lastSeenTargetMs_ = nowMs;
}

void WarModeWatchdog::OnPeacefulIntent(i64 nowMs) {
    if (!peacefulIntent_) {
        peacefulIntent_ = true;
        peacefulSinceMs_ = nowMs;
    }
}

const char* WarModeWatchdog::ExitReason(i64 nowMs) const {
    if (!warMode_) return "";
    if (intent_ == CombatIntent::None && peacefulIntent_)
        return "no combat target and the bot is doing something peaceful";
    if (target_ && hasSeenTarget_ &&
        nowMs - lastSeenTargetMs_ > limits_.targetLostMs)
        return "combat target has not been seen for a while";
    if (intent_ == CombatIntent::None && hasCombat_ &&
        nowMs - lastCombatMs_ > limits_.idleTimeoutMs)
        return "war mode idle with no target";
    if (hasCombat_ && nowMs - lastCombatMs_ > limits_.idleTimeoutMs)
        return "no combat event within the timeout";
    return "";
}

bool WarModeWatchdog::ShouldExitWar(i64 nowMs) const {
    if (!warMode_) return false;
    // Do not hammer: one request, then wait for the server's 0x72.
    if (exitRequested_ && nowMs - exitRequestedMs_ < kExitRetryMs)
        return false;
    const char* why = ExitReason(nowMs);
    return why && *why;
}

void WarModeWatchdog::NoteExitRequested(i64 nowMs) {
    exitRequested_ = true;
    exitRequestedMs_ = nowMs;
}

} // namespace uo::travel
