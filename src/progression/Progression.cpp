#include "uo/progression.h"

#include <algorithm>
#include <cstdio>

namespace uo::prog {

namespace {

const char* const kTrainingPhaseNames[] = {
    "idle", "training", "need_resources", "backing_off", "reached", "blocked",
};
static_assert(sizeof(kTrainingPhaseNames) / sizeof(kTrainingPhaseNames[0]) ==
                  static_cast<usize>(TrainingPhase::Count),
              "kTrainingPhaseNames is out of step with TrainingPhase");

} // namespace

const char* TrainingPhaseName(TrainingPhase p) {
    const usize i = static_cast<usize>(p);
    return i < static_cast<usize>(TrainingPhase::Count) ? kTrainingPhaseNames[i]
                                                        : "?";
}

// --- CharacterBuild --------------------------------------------------------

u32 CharacterBuild::SkillTotalTenths() const {
    u32 sum = 0;
    for (const SkillGoal& g : skills) sum += g.targetTenths;
    return sum;
}

u32 CharacterBuild::StatTotal() const {
    return static_cast<u32>(targetStr) + targetDex + targetInt;
}

const SkillGoal* CharacterBuild::Goal(u16 skill) const {
    for (const SkillGoal& g : skills)
        if (g.skill == skill) return &g;
    return nullptr;
}

bool CharacterBuild::FitsCaps(const CapRules& caps, std::string* why) const {
    char buf[192];
    for (const SkillGoal& g : skills) {
        if (g.targetTenths > caps.perSkillTenths) {
            if (why) {
                std::snprintf(buf, sizeof(buf),
                              "skill %u target %u.%u is above the %u.%u cap",
                              g.skill, g.targetTenths / 10, g.targetTenths % 10,
                              caps.perSkillTenths / 10, caps.perSkillTenths % 10);
                *why = buf;
            }
            return false;
        }
    }
    if (SkillTotalTenths() > caps.skillSumTenths) {
        if (why) {
            std::snprintf(buf, sizeof(buf),
                          "skill total %u.%u is above the %u.%u cap",
                          SkillTotalTenths() / 10, SkillTotalTenths() % 10,
                          caps.skillSumTenths / 10, caps.skillSumTenths % 10);
            *why = buf;
        }
        return false;
    }
    if (targetStr > caps.perStat || targetDex > caps.perStat ||
        targetInt > caps.perStat) {
        if (why) *why = "a stat target is above the per-stat cap";
        return false;
    }
    if (StatTotal() > caps.statSum) {
        if (why) {
            std::snprintf(buf, sizeof(buf), "stat total %u is above the %u cap",
                          StatTotal(), caps.statSum);
            *why = buf;
        }
        return false;
    }
    if (why) why->clear();
    return true;
}

ProgressionPlan PlanProgression(const CharacterBuild& build,
                                const std::vector<SkillSnapshot>& snapshot,
                                const CapRules& caps) {
    ProgressionPlan plan;

    for (const SkillSnapshot& s : snapshot) plan.skillSumTenths += s.baseTenths;
    plan.headroomTenths = caps.skillSumTenths > plan.skillSumTenths
                              ? caps.skillSumTenths - plan.skillSumTenths
                              : 0;

    auto have = [&](u16 skill) -> u16 {
        for (const SkillSnapshot& s : snapshot)
            if (s.skill == skill) return s.baseTenths;
        return 0;
    };

    u32 totalGap = 0;
    for (const SkillGoal& g : build.skills) {
        TrainingNeed n;
        n.skill = g.skill;
        n.haveTenths = have(g.skill);
        n.wantTenths = g.targetTenths;
        if (n.GapTenths() == 0) continue;
        totalGap += n.GapTenths();
        plan.needs.push_back(n);
    }

    // Biggest gap first. A tie keeps the build's own order, which is the
    // closest thing to an author's preference that exists here.
    std::stable_sort(plan.needs.begin(), plan.needs.end(),
                     [](const TrainingNeed& a, const TrainingNeed& b) {
                         return a.GapTenths() > b.GapTenths();
                     });

    plan.complete = plan.needs.empty();
    // The shard's total-skill cap is measured across EVERY skill, and Sphere
    // hands a new character a random fraction of all 58 of them, so a build can
    // be individually legal and still not fit without shedding junk skill.
    plan.needsSkillLoss = totalGap > plan.headroomTenths;
    return plan;
}

// --- economy ---------------------------------------------------------------

i32 Budget::Spend(i32 cost) {
    if (cost <= 0) return 0;
    const i32 available = Spendable();
    const i32 actual = cost < available ? cost : available;
    gold -= actual;
    return actual;
}

void Budget::Earn(i32 amount) {
    if (amount > 0) gold += amount;
}

u32 PurchaseNeed::AffordableQuantity(const Budget& b) const {
    if (unitPrice <= 0) return quantity;   // free, or price unknown
    const i32 spendable = b.Spendable();
    if (spendable <= 0) return 0;
    const u32 affordable = static_cast<u32>(spendable / unitPrice);
    return affordable < quantity ? affordable : quantity;
}

// --- TrainingSession -------------------------------------------------------

void TrainingSession::Reset() {
    phase_ = TrainingPhase::Idle;
    skill_ = 0;
    startTenths_ = 0;
    currentTenths_ = 0;
    targetTenths_ = 0;
    attempts_ = 0;
    gains_ = 0;
    refusals_ = 0;
    attemptsSinceGain_ = 0;
    resumeAtMs_ = 0;
    blockedReason_ = "";
}

void TrainingSession::Begin(u16 skill, u16 currentTenths, u16 targetTenths,
                            i64 nowMs) {
    Reset();
    skill_ = skill;
    startTenths_ = currentTenths;
    currentTenths_ = currentTenths;
    targetTenths_ = targetTenths;
    resumeAtMs_ = nowMs;
    phase_ = currentTenths >= targetTenths ? TrainingPhase::Reached
                                           : TrainingPhase::Training;
}

void TrainingSession::OnSkillSample(u16 tenths, i64 nowMs) {
    (void)nowMs;
    if (phase_ == TrainingPhase::Idle) return;

    if (tenths > currentTenths_) {
        ++gains_;
        attemptsSinceGain_ = 0;
    }
    // A drop is real too: Sphere decays a skill to make room when the total is
    // pressed against the cap, and pretending otherwise would leave a training
    // loop chasing a target it is being pushed away from.
    currentTenths_ = tenths;

    if (currentTenths_ >= targetTenths_) phase_ = TrainingPhase::Reached;
}

void TrainingSession::OnAttempt(AttemptOutcome outcome, i64 nowMs) {
    if (phase_ == TrainingPhase::Reached || phase_ == TrainingPhase::Blocked)
        return;

    switch (outcome) {
        case AttemptOutcome::Success:
        case AttemptOutcome::Failed:
            // Both count: CChar::Skill_Done and CChar::Skill_Fail each call
            // Skill_Experience, so a failed attempt is still a roll.
            ++attempts_;
            ++attemptsSinceGain_;
            refusals_ = 0;
            phase_ = TrainingPhase::Training;
            resumeAtMs_ = nowMs;
            break;

        case AttemptOutcome::Refused:
            ++refusals_;
            if (refusals_ >= limits_.maxConsecutiveRefusals) {
                phase_ = TrainingPhase::Blocked;
                blockedReason_ = "the server refused too many attempts in a row";
                return;
            }
            phase_ = TrainingPhase::BackingOff;
            resumeAtMs_ = nowMs + limits_.refusalBackoffMs;
            break;

        case AttemptOutcome::Throttled:
            // Never retry into a throttle: M2 measured that Sphere re-arms its
            // ~300 s flood TTL every time you do, so trying harder is strictly
            // worse than waiting.
            ++refusals_;
            phase_ = TrainingPhase::BackingOff;
            resumeAtMs_ = nowMs + limits_.throttleBackoffMs;
            break;

        default:
            break;
    }
}

void TrainingSession::OnResourceExhausted(i64 nowMs) {
    if (phase_ == TrainingPhase::Reached || phase_ == TrainingPhase::Blocked)
        return;
    phase_ = TrainingPhase::NeedResources;
    resumeAtMs_ = nowMs;
}

void TrainingSession::OnResourcesRestocked(i64 nowMs) {
    if (phase_ != TrainingPhase::NeedResources) return;
    phase_ = TrainingPhase::Training;
    refusals_ = 0;
    resumeAtMs_ = nowMs;
}

bool TrainingSession::ShouldAttempt(i64 nowMs) const {
    if (phase_ != TrainingPhase::Training && phase_ != TrainingPhase::BackingOff)
        return false;
    return nowMs >= resumeAtMs_;
}

} // namespace uo::prog
