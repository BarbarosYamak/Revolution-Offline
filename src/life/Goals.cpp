#include "uo/life.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace uo::life {

const char* GoalKindName(GoalKind g) {
    switch (g) {
        case GoalKind::Survive:               return "SURVIVE";
        case GoalKind::Heal:                  return "HEAL";
        case GoalKind::RecoverCorpse:         return "RECOVER_CORPSE";
        case GoalKind::GetTool:               return "GET_TOOL";
        case GoalKind::ReplaceEquipment:      return "REPLACE_EQUIPMENT";
        case GoalKind::Bank:                  return "BANK";
        case GoalKind::GatherLogs:            return "GATHER_LOGS";
        case GoalKind::TrainCombat:           return "TRAIN_COMBAT";
        case GoalKind::EarnGold:              return "EARN_GOLD";
        case GoalKind::TravelToRequiredPlace: return "TRAVEL_TO_REQUIRED_PLACE";
        case GoalKind::IdleBriefly:           return "IDLE_BRIEFLY";
        case GoalKind::Count:                 break;
    }
    return "?";
}

namespace {

std::string Fmt(const char* fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

const Need* FindNeed(const std::vector<Need>& needs, NeedKind kind) {
    for (const Need& n : needs) {
        if (n.kind == kind) return &n;
    }
    return nullptr;
}

// Which need each goal answers. A goal whose need is absent is not scored at
// all -- that is the hard filter, and it is why the reason list stays short.
struct GoalSpec {
    GoalKind kind;
    NeedKind need;
    double   weight;      // score per unit of need urgency
};

// The magnitudes are a deliberate lexicographic ordering expressed as one
// number, the way uo-offline's target scorer is (audit section 3.5):
//
//   1000+  emergencies that must preempt anything already running
//    500+  a blocked profession -- no axe means no progress at all
//    200+  housekeeping that protects what has been earned
//    100+  the productive work itself
//     10   the bounded no-op
const GoalSpec kGoals[] = {
    {GoalKind::Survive,               NeedKind::StayAlive,     1000.0},
    {GoalKind::RecoverCorpse,         NeedKind::RecoverCorpse,  950.0},
    {GoalKind::Heal,                  NeedKind::Heal,           700.0},
    {GoalKind::GetTool,               NeedKind::NeedTool,       520.0},
    {GoalKind::ReplaceEquipment,      NeedKind::NeedEquipment,  260.0},
    {GoalKind::Bank,                  NeedKind::NeedBank,       240.0},
    {GoalKind::EarnGold,              NeedKind::NeedGold,       150.0},
    {GoalKind::GatherLogs,            NeedKind::NeedLogs,       130.0},
    {GoalKind::TrainCombat,           NeedKind::NeedTraining,   110.0},
    {GoalKind::TravelToRequiredPlace, NeedKind::NeedTravel,      90.0},
};

}  // namespace

std::vector<ScoredGoal> Planner::Score(const std::vector<Need>& needs,
                                       const Observation& obs,
                                       const Memory& mem) const {
    std::vector<ScoredGoal> out;

    for (const GoalSpec& spec : kGoals) {
        const Need* need = FindNeed(needs, spec.need);
        if (!need) continue;   // hard filter: no need, no goal

        ScoredGoal g;
        g.kind = spec.kind;

        if (need->blocked) {
            // A blocked need is reported, not silently dropped. "Why didn't
            // it buy an axe" must always have an answer.
            g.feasible = false;
            g.blockedWhy = need->reason + " (" + need->evidence + ")";
            g.reasons.push_back("BLOCKED_NEED " + std::string(NeedKindName(need->kind)) +
                                " " + need->what);
            out.push_back(std::move(g));
            continue;
        }

        g.feasible = true;
        g.score = spec.weight * need->urgency;
        g.reasons.push_back(Fmt("%s urgency %.2f x %.0f = %.1f",
                                NeedKindName(need->kind), need->urgency,
                                spec.weight, g.score));
        if (!need->evidence.empty()) g.reasons.push_back(need->evidence);

        // --- goal-specific modifiers, each printed ------------------------
        switch (spec.kind) {
            case GoalKind::GatherLogs: {
                if (obs.axeEquipped) {
                    g.score += 20.0;
                    g.reasons.push_back("axe already in hand +20");
                }
                const KnownResourceSource* src =
                    mem.BestResource("logs", obs.x, obs.y, obs.nowMs);
                if (src) {
                    g.score += 25.0;
                    g.reasons.push_back(Fmt("remembered stand at %d,%d +25", src->x, src->y));
                }
                if (obs.WeightFraction() > 0.7) {
                    g.score -= 40.0;
                    g.reasons.push_back(Fmt("carrying %.0f%% of capacity -40",
                                            obs.WeightFraction() * 100.0));
                }
                const double heat = mem.DangerHeatAt(obs.x, obs.y, obs.nowMs);
                if (heat > 0.1) {
                    g.score -= heat * 60.0;
                    g.reasons.push_back(Fmt("remembered danger here heat %.2f -%.0f",
                                            heat, heat * 60.0));
                }
                break;
            }
            case GoalKind::Bank: {
                if (obs.atBank) {
                    g.score += 60.0;
                    g.reasons.push_back("already standing at the bank +60");
                }
                if (!mem.BestPlace("bank")) {
                    g.score -= 30.0;
                    g.reasons.push_back("no bank learned yet -30");
                }
                break;
            }
            case GoalKind::TrainCombat: {
                // Training is the lowest-value thing a healthy character can
                // do, and it must never outrank protecting a full pack.
                if (obs.WeightFraction() > 0.7) {
                    g.score -= 50.0;
                    g.reasons.push_back("pack nearly full -50");
                }
                if (obs.hp < obs.hpMax) {
                    g.score -= 30.0;
                    g.reasons.push_back("not at full health -30");
                }
                break;
            }
            case GoalKind::Survive: {
                if (obs.attackersOnMe > 1) {
                    g.score += 100.0 * (obs.attackersOnMe - 1);
                    g.reasons.push_back(Fmt("%d attackers +%d", obs.attackersOnMe,
                                            100 * (obs.attackersOnMe - 1)));
                }
                break;
            }
            default:
                break;
        }

        out.push_back(std::move(g));
    }

    // The bounded no-op always exists, so there is never "no goal". A tick
    // with nothing to do should say so rather than silently spin.
    {
        ScoredGoal idle;
        idle.kind = GoalKind::IdleBriefly;
        idle.feasible = true;
        idle.score = 10.0;
        idle.reasons.push_back("fallback: nothing else scored");
        out.push_back(std::move(idle));
    }

    std::stable_sort(out.begin(), out.end(), [](const ScoredGoal& a, const ScoredGoal& b) {
        if (a.feasible != b.feasible) return a.feasible;
        return a.score > b.score;
    });
    return out;
}

bool Planner::Exhausted(i64 nowMs, std::string* whyOut) const {
    if (!goal_.active) return false;
    if (goal_.attempts >= cfg_.maxAttempts) {
        if (whyOut) *whyOut = Fmt("attempts %d >= %d", goal_.attempts, cfg_.maxAttempts);
        return true;
    }
    if (nowMs - goal_.startedAtMs >= cfg_.maxGoalMs) {
        if (whyOut) *whyOut = Fmt("ran %llds without finishing (limit %llds)",
                                  static_cast<long long>((nowMs - goal_.startedAtMs) / 1000),
                                  static_cast<long long>(cfg_.maxGoalMs / 1000));
        return true;
    }
    return false;
}

bool Planner::Select(const std::vector<Need>& needs, const Observation& obs,
                     const Memory& mem, i64 nowMs, std::string* whyOut) {
    const std::vector<ScoredGoal> scored = Score(needs, obs, mem);

    const ScoredGoal* best = nullptr;
    for (const ScoredGoal& g : scored) {
        if (g.feasible) { best = &g; break; }
    }
    if (!best) {
        if (whyOut) *whyOut = "no feasible goal";
        return false;
    }

    // --- bounded failure: give up before re-deciding ----------------------
    std::string exhaustedWhy;
    if (goal_.active && Exhausted(nowMs, &exhaustedWhy)) {
        goal_.failureReason = exhaustedWhy;
        goal_.active = false;
    }

    if (!goal_.active) {
        goal_.kind = best->kind;
        goal_.active = true;
        goal_.startedAtMs = nowMs;
        goal_.attempts = 0;
        goal_.progress = 0;
        goal_.scoreAtSelection = best->score;
        if (whyOut) {
            *whyOut = goal_.failureReason.empty()
                          ? std::string("no goal was running")
                          : ("previous goal abandoned: " + goal_.failureReason);
        }
        goal_.failureReason.clear();
        return true;
    }

    if (best->kind == goal_.kind) {
        // Phase refresh, not a transition: keep the clock and the progress.
        goal_.scoreAtSelection = best->score;
        if (whyOut) *whyOut = "goal refreshed (still the best choice)";
        return false;
    }

    // --- commitment -------------------------------------------------------
    //
    // An emergency preempts regardless. Anything else has to wait out the
    // commitment floor AND beat the incumbent by a margin, which is what
    // stops gather/bank/gather flapping on a 0.01 score difference.
    const bool emergency = best->score >= cfg_.preemptScore;
    const i64 heldMs = nowMs - goal_.startedAtMs;

    if (!emergency && heldMs < cfg_.minCommitMs) {
        if (whyOut) {
            *whyOut = Fmt("%s held for %llds; commitment floor is %llds",
                          GoalKindName(goal_.kind), static_cast<long long>(heldMs / 1000),
                          static_cast<long long>(cfg_.minCommitMs / 1000));
        }
        return false;
    }

    // The incumbent's score is recomputed from the same list, so hysteresis
    // compares like with like rather than against a stale selection score.
    double incumbentScore = 0.0;
    bool incumbentStillFeasible = false;
    for (const ScoredGoal& g : scored) {
        if (g.kind != goal_.kind) continue;
        incumbentScore = g.score;
        incumbentStillFeasible = g.feasible;
        break;
    }

    if (incumbentStillFeasible && !emergency &&
        best->score < incumbentScore * (1.0 + cfg_.incumbentBonus)) {
        if (whyOut) {
            *whyOut = Fmt("%s %.1f does not beat %s %.1f by %.0f%%",
                          GoalKindName(best->kind), best->score,
                          GoalKindName(goal_.kind), incumbentScore,
                          cfg_.incumbentBonus * 100.0);
        }
        return false;
    }

    if (whyOut) {
        *whyOut = Fmt("%s %.1f superseded %s %.1f%s", GoalKindName(best->kind),
                      best->score, GoalKindName(goal_.kind), incumbentScore,
                      emergency ? " (emergency preempt)" : "");
    }
    goal_.kind = best->kind;
    goal_.startedAtMs = nowMs;
    goal_.attempts = 0;
    goal_.progress = 0;
    goal_.scoreAtSelection = best->score;
    goal_.failureReason.clear();
    return true;
}

void Planner::NoteAttempt(i64 nowMs) {
    (void)nowMs;
    goal_.attempts++;
}

void Planner::NoteProgress() {
    goal_.progress++;
    // Real progress clears the failure ladder -- the same rule uo-offline's
    // traveller uses, and for the same reason: a goal that is working must
    // not be abandoned because it has taken several steps.
    goal_.attempts = 0;
}

void Planner::Finish(bool success, const char* why, i64 nowMs) {
    (void)nowMs;
    goal_.active = false;
    goal_.failureReason = success ? std::string() : (why ? why : "unspecified failure");
}

}  // namespace uo::life
