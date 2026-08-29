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
        case GoalKind::TrainAtNpc:            return "TRAIN_AT_NPC";
        case GoalKind::TradeWithPlayer:       return "TRADE_WITH_PLAYER";
        case GoalKind::Fish:                  return "FISH";
        case GoalKind::BuySupplies:           return "BUY_SUPPLIES";
        case GoalKind::Craft:                 return "CRAFT";
        case GoalKind::GetFood:               return "GET_FOOD";
        case GoalKind::PracticeSkill:         return "PRACTICE_SKILL";
        case GoalKind::FillSpellbook:         return "FILL_SPELLBOOK";
        case GoalKind::MakeBandages:         return "MAKE_BANDAGES";
        case GoalKind::Explore:              return "EXPLORE";
        case GoalKind::UpgradeGear:          return "UPGRADE_GEAR";
        case GoalKind::IdleBriefly:           return "IDLE_BRIEFLY";
        case GoalKind::Count:                 break;
    }
    return "?";
}

const char* GoalFamilyName(GoalFamily f) {
    switch (f) {
        case GoalFamily::Emergency: return "emergency";
        case GoalFamily::Upkeep:    return "upkeep";
        case GoalFamily::Work:      return "work";
        case GoalFamily::Training:  return "training";
        case GoalFamily::Social:    return "social";
        case GoalFamily::Wander:    return "wander";
        case GoalFamily::Count:     break;
    }
    return "?";
}

// The mapping is deliberately coarse. It answers "what KIND of thing is the
// character doing", which is the question a rounded day is measured by -- not
// "which subsystem owns this goal".
GoalFamily FamilyOf(GoalKind k) {
    switch (k) {
        case GoalKind::Survive:
        case GoalKind::Heal:
        case GoalKind::RecoverCorpse:
        case GoalKind::GetTool:
            return GoalFamily::Emergency;
        case GoalKind::Bank:
        case GoalKind::ReplaceEquipment:
        case GoalKind::GetFood:
        // A spellbook is equipment. Filling it belongs with buying a tool
        // and replacing armour, not with training -- the character is not
        // practising anything, it is shopping for the means to cast at all.
        case GoalKind::FillSpellbook:
        case GoalKind::MakeBandages:
        case GoalKind::UpgradeGear:
            return GoalFamily::Upkeep;
        case GoalKind::GatherLogs:
        case GoalKind::Fish:
        case GoalKind::Craft:
        case GoalKind::BuySupplies:
        case GoalKind::EarnGold:
            return GoalFamily::Work;
        case GoalKind::TrainCombat:
        case GoalKind::TrainAtNpc:
        case GoalKind::PracticeSkill:
            return GoalFamily::Training;
        case GoalKind::TradeWithPlayer:
            return GoalFamily::Social;
        case GoalKind::TravelToRequiredPlace:
        case GoalKind::Explore:
        case GoalKind::IdleBriefly:
            return GoalFamily::Wander;
        case GoalKind::Count:
            break;
    }
    return GoalFamily::Wander;
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
    // Above banking: being hungry outranks tidying the pack, and starving
    // outranks nearly everything short of an actual emergency. The urgency
    // does that scaling; the weight just puts it in the right neighbourhood.
    {GoalKind::GetFood,               NeedKind::NeedFood,       250.0},
    {GoalKind::EarnGold,              NeedKind::NeedGold,       150.0},
    {GoalKind::GatherLogs,            NeedKind::NeedLogs,       130.0},
    {GoalKind::TrainCombat,           NeedKind::NeedTraining,   110.0},
    {GoalKind::TravelToRequiredPlace, NeedKind::NeedTravel,      90.0},
    // Above ordinary gathering: buying a skill is a step change in what the
    // character can do, and the gold is already saved by the time the need
    // fires. Below housekeeping, because a full pack still comes first.
    {GoalKind::TrainAtNpc,            NeedKind::NeedSkillTraining, 200.0},
    // Just under EARN_GOLD: when a vendor will take the goods that is the
    // shorter errand, and the player market is for what it refuses.
    {GoalKind::TradeWithPlayer,       NeedKind::NeedTrade,         145.0},
    // Beside GatherLogs: it is the same kind of thing, the productive work
    // this life does, and it must not outrank housekeeping.
    {GoalKind::Fish,                  NeedKind::NeedCatch,         130.0},
    // Making is the same productive work as gathering, so it sits with FISH
    // and GATHER_LOGS. Buying the inputs ranks just above it: a crafter that
    // cannot start is worth a trip to the shop before anything else it does,
    // and without the inputs the making cannot happen at all.
    {GoalKind::Craft,                 NeedKind::NeedCraft,         130.0},
    {GoalKind::BuySupplies,           NeedKind::NeedSupplies,      140.0},
    // Practising sits with the productive work, because that is what it is:
    // the hours a character puts in to get a skill to 100. Slightly under
    // gathering so a life that can earn does not stand in a field casting
    // all day, and well under buying a skill outright, which is a step
    // change rather than an hour of practice.
    {GoalKind::PracticeSkill,         NeedKind::NeedPractice,      120.0},
    // Below practice and well below buying supplies. A book fills over many
    // sessions and must never crowd out the work that pays for it, but it
    // outranks idling, because a mage without spells is not really a mage.
    {GoalKind::FillSpellbook,         NeedKind::NeedSpells,        110.0},
    // Above buying supplies. A fighter with no bandages and no money is in a
    // worse place than one short of craft inputs: it cannot heal, so it cannot
    // hunt, so it cannot earn the money to fix any of it.
    {GoalKind::MakeBandages,          NeedKind::NeedMakeBandages,  145.0},
    // Below bandages and food: armour is what you want once you are fed and
    // able to heal, not instead of them.
    {GoalKind::UpgradeGear,           NeedKind::NeedGear,          100.0},
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

        // A goal serving its own cooldown is reported, not dropped -- exactly
        // like a blocked need. Placed BEFORE the blocked test so the cooldown
        // reason wins: it is the more recent and more specific fact.
        if (Cooling(spec.kind, obs.nowMs)) {
            g.feasible = false;
            g.blockedWhy = Fmt("on cooldown for another %llds after achieving "
                               "nothing",
                               static_cast<long long>(
                                   (cooldownUntilMs_[static_cast<int>(spec.kind)] -
                                    obs.nowMs) / 1000));
            g.reasons.push_back("COOLING " + std::string(GoalKindName(spec.kind)) +
                                " " + g.blockedWhy);
            out.push_back(std::move(g));
            continue;
        }

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
                // Only a stand that has ACTUALLY PAID OUT earns the bonus.
                // Crediting any remembered spot is what kept a character in
                // the scrub: it held 64 spots it had merely stood in, each
                // worth +25, and none of them worth visiting.
                const KnownResourceSource* proven =
                    mem.BestProvenResource("logs", obs.x, obs.y, obs.nowMs);
                if (proven) {
                    g.score += 25.0;
                    g.reasons.push_back(Fmt("proven stand at %d,%d (%d successes) +25",
                                            proven->x, proven->y, proven->successes));
                } else if (mem.BestHint("logs", obs.x, obs.y, obs.nowMs)) {
                    g.score += 10.0;
                    g.reasons.push_back("a known forest to try +10");
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

        // --- satiation: let something else have a turn ------------------
        // The stronger of the two claims wins -- doing the same errand over
        // and over, or doing the same KIND of thing over and over.
        const double goalSat = Satiation(spec.kind, obs.nowMs);
        const double famSat = FamilySatiation(spec.kind, obs.nowMs);
        const double sat = goalSat > famSat ? goalSat : famSat;
        if (sat > 0.0) {
            const double before = g.score;
            g.score *= (1.0 - sat);
            g.reasons.push_back(Fmt("%s just now, easing off %.0f%% "
                                    "(%.1f -> %.1f)",
                                    famSat > goalSat
                                        ? GoalFamilyName(FamilyOf(spec.kind))
                                        : "same errand",
                                    sat * 100.0, before, g.score));
        }

        out.push_back(std::move(g));
    }

    // GOING SOMEWHERE NEW BEATS STANDING STILL.
    //
    // "bots shouldnt be idle unless its state specifically" (project owner,
    // 2026-08-29). Idling was winning 73-85% of picks on some characters,
    // because every other goal was blocked and the no-op was the only thing
    // left that scored. That is a bot doing nothing with its life.
    //
    // Exploring is the honest answer, not filler: almost every blocked need in
    // this project is blocked for want of knowing WHERE something is -- no
    // known supplier of a tongs, no buyer for these ingots, no trainer in
    // reach. A full crafter finished a whole session having visited ONE place
    // (session_summary places=1), which is exactly why he knew no supplier for
    // any of the three tools he was short of. Walking to an unvisited shop and
    // reading the paperdolls there is how that gets fixed.
    //
    // Scored above idle and below every real errand, so it fills the gap and
    // never competes with work.
    {
        ScoredGoal explore;
        explore.kind = GoalKind::Explore;
        explore.feasible = true;
        explore.score = 15.0;
        explore.reasons.push_back(
            "fallback: nothing else is actionable, so go and learn the world");
        out.push_back(std::move(explore));
    }

    // The bounded no-op always exists, so there is never "no goal". Below
    // exploring: a character stands still only when it cannot even do that --
    // when every place with a service in it is already known.
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

// A GOAL THAT SUCCEEDS WITHOUT DOING ANYTHING, REPEATEDLY, IS SPINNING.
//
// The backstop for a bug this project keeps rediscovering in new goals. Each
// instance looked different and was the same thing: the goal could not act,
// said it was done, freed the planner, and was handed straight back the errand
// it had just failed to perform.
//
//   GET_TOOL     2,058 goals in ten minutes (Bruin)
//   GET_FOOD     whole 25-minute sessions, 100% of picks (Voris, Ysolde)
//   EARN_GOLD    13,111 completions at 60ms intervals (Kaelen)
//
// Fixing each site as it is found does not stop the next goal from doing it.
// So: five consecutive completions with no progress cools the goal off for a
// minute, whatever its internal reason. This is a safety net, not a substitute
// for the real fix -- it exists so that one broken goal cannot silently
// consume an entire session's worth of decisions again.
constexpr int kNoopSpinLimit = 5;
constexpr i64 kSpinCooldownMs = 60000;

void Planner::Finish(bool success, const char* why, i64 nowMs) {
    const int i = static_cast<int>(goal_.kind);
    // IDLING IS SUPPOSED TO ACHIEVE NOTHING. It is the one goal whose whole
    // purpose is to pass a little time, so it completes with progress 0 every
    // single time and tripped the backstop three times in one session. Exempt.
    if (i >= 0 && i < static_cast<int>(GoalKind::Count) &&
        goal_.kind != GoalKind::IdleBriefly) {
        // FAILING AT SIXTY MILLISECONDS IS THE SAME SPIN AS SUCCEEDING AT IT.
        //
        // The first version of this guard counted successes only, and the very
        // next run produced a failure-side instance: Ysolde logged
        // goal_failed=BUY_SUPPLIES "this 'mage' does not stock i_scroll_blank"
        // 746 times at 60ms intervals. The goal has a real bug either way, and
        // from the planner's seat the two look identical -- a goal that
        // terminates having changed nothing, over and over.
        if (goal_.progress == 0) {
            if (++noopCompletions_[i] >= kNoopSpinLimit) {
                Cooldown(goal_.kind, nowMs + kSpinCooldownMs);
                spinDetected_ = goal_.kind;
                noopCompletions_[i] = 0;
            }
        } else {
            noopCompletions_[i] = 0;
        }
    }
    goal_.active = false;
    goal_.failureReason = success ? std::string() : (why ? why : "unspecified failure");
}

GoalKind Planner::TakeSpinDetected() {
    const GoalKind k = spinDetected_;
    spinDetected_ = GoalKind::Count;
    return k;
}

// An emergency is never damped. A character does not get bored of not dying.
static bool IsEmergencyGoal(GoalKind k) {
    return k == GoalKind::Survive || k == GoalKind::Heal ||
           k == GoalKind::RecoverCorpse || k == GoalKind::GetTool ||
           k == GoalKind::IdleBriefly;
}

void Planner::NoteRan(GoalKind kind, i64 nowMs) {
    const int i = static_cast<int>(kind);
    if (i < 0 || i >= static_cast<int>(GoalKind::Count)) return;
    if (kind == lastRanKind_) {
        ++repeatRuns_[i];
    } else {
        // Something else got a turn, so the streak is over. Clearing the OLD
        // goal's counter rather than only setting the new one is what lets a
        // character come back to banking later without being punished for
        // having banked a lot an hour ago.
        const int prev = static_cast<int>(lastRanKind_);
        if (prev >= 0 && prev < static_cast<int>(GoalKind::Count))
            repeatRuns_[prev] = 0;
        repeatRuns_[i] = 1;
        lastRanKind_ = kind;
    }
    lastRanMs_[i] = nowMs;

    // And the same, one level up. A family's streak only breaks when a
    // DIFFERENT family runs -- a crafter cycling buy/craft/sell is one long
    // Work streak however much its individual goals alternate.
    const GoalFamily fam = FamilyOf(kind);
    const int fi = static_cast<int>(fam);
    if (fi < 0 || fi >= static_cast<int>(GoalFamily::Count)) return;
    if (fam == lastRanFamily_) {
        ++famRepeatRuns_[fi];
    } else {
        const int pf = static_cast<int>(lastRanFamily_);
        if (pf >= 0 && pf < static_cast<int>(GoalFamily::Count))
            famRepeatRuns_[pf] = 0;
        famRepeatRuns_[fi] = 1;
        lastRanFamily_ = fam;
    }
    famLastRanMs_[fi] = nowMs;
}

double Planner::Satiation(GoalKind kind, i64 nowMs) const {
    const int i = static_cast<int>(kind);
    if (i < 0 || i >= static_cast<int>(GoalKind::Count)) return 0.0;
    if (IsEmergencyGoal(kind)) return 0.0;
    if (repeatRuns_[i] <= 1 || lastRanMs_[i] <= 0) return 0.0;
    const i64 age = nowMs - lastRanMs_[i];
    if (age >= kSatiationMs) return 0.0;
    // Linear fade over the freshness window, so the damping lets go smoothly
    // rather than snapping back the instant the timer expires.
    const double fresh = 1.0 - (static_cast<double>(age) /
                                static_cast<double>(kSatiationMs));
    const double raw = kSatiationPerRepeat * (repeatRuns_[i] - 1);
    const double capped = raw < kSatiationMax ? raw : kSatiationMax;
    return capped * fresh;
}

double Planner::FamilySatiation(GoalKind kind, i64 nowMs) const {
    if (IsEmergencyGoal(kind)) return 0.0;
    const GoalFamily fam = FamilyOf(kind);
    if (fam == GoalFamily::Emergency) return 0.0;
    const int fi = static_cast<int>(fam);
    if (fi < 0 || fi >= static_cast<int>(GoalFamily::Count)) return 0.0;
    if (famRepeatRuns_[fi] <= 2 || famLastRanMs_[fi] <= 0) return 0.0;
    const i64 age = nowMs - famLastRanMs_[fi];
    if (age >= kSatiationMs) return 0.0;
    const double fresh = 1.0 - (static_cast<double>(age) /
                                static_cast<double>(kSatiationMs));
    const double raw = kFamilySatiationPerRepeat * (famRepeatRuns_[fi] - 2);
    const double capped = raw < kFamilySatiationMax ? raw : kFamilySatiationMax;
    return capped * fresh;
}

void Planner::Cooldown(GoalKind kind, i64 untilMs) {
    const int i = static_cast<int>(kind);
    if (i < 0 || i >= static_cast<int>(GoalKind::Count)) return;
    // Never shorten one that is already running: two callers cooling the same
    // goal should give the longer rest, not the last one written.
    if (untilMs > cooldownUntilMs_[i]) cooldownUntilMs_[i] = untilMs;
}

bool Planner::Cooling(GoalKind kind, i64 nowMs) const {
    const int i = static_cast<int>(kind);
    if (i < 0 || i >= static_cast<int>(GoalKind::Count)) return false;
    return nowMs < cooldownUntilMs_[i];
}

}  // namespace uo::life
