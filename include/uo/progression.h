#pragma once

// ---------------------------------------------------------------------------
// Character progression and economy state (M3).
//
// The distinction this exists to hold is "the build I want" versus "the
// character I actually am". A bot closes that gap by playing: training a skill
// it is short on, earning the gold to buy what the training consumes, and
// spending it. Nothing in this header can change a skill, a stat or a coin --
// it only describes goals, gaps, needs and budgets, so every decision built on
// it is unit-testable without a server.
//
// Skill values are in TENTHS throughout, because that is how Sphere stores and
// sends them (0x3A) and rounding them for convenience is how a cap check ends
// up off by a point.
//
// Deliberately NOT a class system. A build is a list of skill targets; a
// character that wants Mining, Blacksmithy, Alchemy and Magery is expressed the
// same way as one that wants Swordsmanship and Tactics, and the capabilities it
// has follow from its actual skills, stats and inventory rather than from a
// profession enum.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::prog {

// The shard's limits, read from its own configuration rather than assumed.
// The Revolution runtime's [SKILLCLASS 0] gives every skill a 100.0 cap, a
// 1000.0 total, 100 per stat and 300 total -- see
// docs/REVOLUTION_GAMEPLAY_TRUTH.md, including where that disagrees with how
// players remember the shard.
struct CapRules {
    u16 perSkillTenths = 1000;    // 100.0
    u32 skillSumTenths = 10000;   // 1000.0
    u16 perStat = 100;
    u16 statSum = 300;
};

struct SkillGoal {
    u16 skill = 0;          // [SKILL n] index, matching skills.mul
    u16 targetTenths = 0;
};

// What a character is trying to become. Order is not priority; the planner
// decides what to work on from the gaps.
struct CharacterBuild {
    std::string name;
    std::vector<SkillGoal> skills;
    u16 targetStr = 0;
    u16 targetDex = 0;
    u16 targetInt = 0;

    u32 SkillTotalTenths() const;
    u32 StatTotal() const;
    // Whether this build is reachable at all under the shard's caps. `why`
    // receives a human-readable reason when it is not, so a bot that was given
    // an impossible build says so instead of training forever.
    bool FitsCaps(const CapRules& caps, std::string* why) const;
    const SkillGoal* Goal(u16 skill) const;
};

// One skill as the server currently reports it.
struct SkillSnapshot {
    u16 skill = 0;
    u16 baseTenths = 0;    // the TRAINED value -- what training moves
};

struct TrainingNeed {
    u16 skill = 0;
    u16 haveTenths = 0;
    u16 wantTenths = 0;
    u16 GapTenths() const {
        return wantTenths > haveTenths
                   ? static_cast<u16>(wantTenths - haveTenths)
                   : 0;
    }
};

struct ProgressionPlan {
    std::vector<TrainingNeed> needs;   // largest gap first
    u32 skillSumTenths = 0;            // current total, for cap headroom
    u32 headroomTenths = 0;            // how much total skill may still be added
    bool complete = false;             // every goal met
    // True when the build cannot be finished without losing skill elsewhere:
    // the gaps add up to more than the cap has room for.
    bool needsSkillLoss = false;
};

// Compare a build against what the character actually has. Skills the snapshot
// does not mention are treated as 0, which is the honest reading of "the
// server has not told us".
ProgressionPlan PlanProgression(const CharacterBuild& build,
                                const std::vector<SkillSnapshot>& snapshot,
                                const CapRules& caps);

// --- economy ---------------------------------------------------------------

// Something the character needs to have in its pack. `have` is counted from
// the real inventory; nothing here creates items.
struct ResourceNeed {
    u16 graphic = 0;
    std::string name;
    u32 have = 0;
    u32 want = 0;
    u32 Shortfall() const { return want > have ? want - have : 0; }
    bool Met() const { return have >= want; }
};

// Gold, with a floor the bot will not spend below. The reserve is what stops a
// character selling its last supplies to buy training material and then having
// no way home.
struct Budget {
    i32 gold = 0;
    i32 reserve = 0;

    i32 Spendable() const { return gold > reserve ? gold - reserve : 0; }
    bool CanAfford(i32 cost) const { return cost >= 0 && cost <= Spendable(); }
    // Returns what was actually spent; refuses to overspend rather than going
    // negative, because the server would refuse too.
    i32 Spend(i32 cost);
    void Earn(i32 amount);
};

struct PurchaseNeed {
    u16 graphic = 0;
    std::string name;
    u32 quantity = 0;
    i32 unitPrice = 0;
    i64 TotalCost() const {
        return static_cast<i64>(unitPrice) * static_cast<i64>(quantity);
    }
    // The largest quantity this budget can pay for, which is what a bot
    // actually buys when it cannot afford the whole shortfall.
    u32 AffordableQuantity(const Budget& b) const;
};

// --- training --------------------------------------------------------------

enum class TrainingPhase : u8 {
    Idle = 0,
    Training,        // performing the skill action
    NeedResources,   // out of something the action consumes
    BackingOff,      // the server refused or throttled us; wait, do not retry
    Reached,         // the target was met
    Blocked,         // cannot progress: at cap, or too many refusals
    Count,
};

const char* TrainingPhaseName(TrainingPhase p);

enum class AttemptOutcome : u8 {
    Success = 0,     // the action was performed (gain is a separate question)
    Failed,          // performed and failed -- Sphere awards experience anyway
    Refused,         // the server said no (no mana, no reagent, wrong state)
    Throttled,       // the server is rate-limiting us
    Count,
};

struct TrainingLimits {
    // Consecutive refusals before the session declares itself blocked. A
    // training loop that keeps asking after the shard has said no is how M2
    // discovered Sphere re-arms its flood TTL on retry.
    int maxConsecutiveRefusals = 12;
    // Backoff after a refusal, and after a throttle. The throttle figure is
    // deliberately long: Sphere's NetTTL is 300 s and retrying re-arms it.
    i64 refusalBackoffMs = 2000;
    i64 throttleBackoffMs = 60000;
    // Attempts with no observed gain before the session reports a stall. Not
    // an error -- at 5% per use a hundred barren attempts is ordinary luck --
    // but a caller may want to know.
    int stallAttempts = 400;
};

// Tracks one skill being trained: attempts made, gains actually observed
// (always from a server-reported value, never inferred), and whether it is
// currently safe to keep going.
class TrainingSession {
public:
    void Begin(u16 skill, u16 currentTenths, u16 targetTenths, i64 nowMs);
    void SetLimits(const TrainingLimits& l) { limits_ = l; }
    const TrainingLimits& Limits() const { return limits_; }

    // The server told us where the skill now stands.
    void OnSkillSample(u16 tenths, i64 nowMs);
    // One training action resolved.
    void OnAttempt(AttemptOutcome outcome, i64 nowMs);
    // Something the action consumes ran out.
    void OnResourceExhausted(i64 nowMs);
    // ...and was replenished.
    void OnResourcesRestocked(i64 nowMs);

    TrainingPhase Phase() const { return phase_; }
    // True when the caller may perform another training action right now.
    bool ShouldAttempt(i64 nowMs) const;
    i64  ResumeAtMs() const { return resumeAtMs_; }

    u16 Skill() const { return skill_; }
    u16 StartTenths() const { return startTenths_; }
    u16 CurrentTenths() const { return currentTenths_; }
    u16 TargetTenths() const { return targetTenths_; }
    u16 GainedTenths() const {
        return currentTenths_ > startTenths_
                   ? static_cast<u16>(currentTenths_ - startTenths_)
                   : 0;
    }
    int Attempts() const { return attempts_; }
    int Gains() const { return gains_; }
    int ConsecutiveRefusals() const { return refusals_; }
    bool Stalled() const { return attemptsSinceGain_ >= limits_.stallAttempts; }
    const char* BlockedReason() const { return blockedReason_; }

    void Reset();

private:
    TrainingLimits limits_;
    TrainingPhase phase_ = TrainingPhase::Idle;
    u16 skill_ = 0;
    u16 startTenths_ = 0;
    u16 currentTenths_ = 0;
    u16 targetTenths_ = 0;
    int attempts_ = 0;
    int gains_ = 0;
    int refusals_ = 0;
    int attemptsSinceGain_ = 0;
    i64 resumeAtMs_ = 0;
    const char* blockedReason_ = "";
};

} // namespace uo::prog
