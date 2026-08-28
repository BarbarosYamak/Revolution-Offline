#include "uo/life.h"
#include "uo/professions.h"
#include "uo/faucets.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace uo::life {

const char* PlanViolationName(PlanViolation v) {
    switch (v) {
        case PlanViolation::None:                  return "none";
        case PlanViolation::SkillBudgetExceeded:   return "skill_budget_exceeded";
        case PlanViolation::PerSkillCap:           return "per_skill_cap";
        case PlanViolation::InactiveSkill:         return "inactive_skill";
        case PlanViolation::NegativeTarget:        return "negative_target";
        case PlanViolation::StatTotalExceeded:     return "stat_total_exceeded";
        case PlanViolation::StatPerCapExceeded:    return "stat_per_cap_exceeded";
        case PlanViolation::CreationTooRich:       return "creation_too_rich";
        case PlanViolation::RejectedCreationSplit: return "rejected_creation_split";
        case PlanViolation::Count:                 break;
    }
    return "?";
}

PlanCheck ValidatePlan(const rules::Profile& p, const BuildPlan& plan) {
    PlanCheck out;

    // --- the 700-point budget ---------------------------------------------
    for (const SkillTarget& s : plan.skills) {
        if (s.tenths < 0) {
            out.violation = PlanViolation::NegativeTarget;
            out.skillId = s.skillId;
            return out;
        }
        if (s.tenths > p.perSkillCapTenths) {
            out.violation = PlanViolation::PerSkillCap;
            out.skillId = s.skillId;
            return out;
        }
        if (!rules::SkillActive(s.skillId)) {
            // The reason Resisting Spells must not creep back in: uo-offline's
            // T2A dexxer template carries it, and a generic T2A template is
            // not evidence about Revolution. M3.6 found it inactive here.
            out.violation = PlanViolation::InactiveSkill;
            out.skillId = s.skillId;
            return out;
        }
        out.resolvedTenths += s.tenths;
    }

    if (plan.unresolvedTenths < 0) {
        out.violation = PlanViolation::NegativeTarget;
        return out;
    }

    out.plannedTotalTenths = out.resolvedTenths + plan.unresolvedTenths;
    if (out.plannedTotalTenths > p.totalSkillCapTenths) {
        out.violation = PlanViolation::SkillBudgetExceeded;
        return out;
    }

    // --- the 225 / 100 stat rule ------------------------------------------
    out.statTotal = plan.targetStr + plan.targetDex + plan.targetInt;
    if (plan.targetStr > p.perStatCap || plan.targetDex > p.perStatCap ||
        plan.targetInt > p.perStatCap) {
        out.violation = PlanViolation::StatPerCapExceeded;
        return out;
    }
    if (out.statTotal > p.totalStatCap) {
        out.violation = PlanViolation::StatTotalExceeded;
        return out;
    }

    // --- creation is initialisation, not a request for final stats --------
    //
    // Source-X clamps to 60 per stat / 80 total and 50 per skill / 100 total
    // (CChar::InitPlayer). Asking for more is not an exploit -- the server
    // simply clamps it -- but it means the plan does not know what it will
    // actually get, and every character in this project that started life
    // with an unrealistic stat request died.
    const i32 createTotal = plan.createStr + plan.createDex + plan.createInt;
    if (createTotal > 80 || plan.createStr > 60 || plan.createDex > 60 ||
        plan.createInt > 60) {
        out.violation = PlanViolation::CreationTooRich;
        return out;
    }
    i32 createSkillTotal = 0;
    for (const SkillTarget& s : plan.createSkills) {
        if (s.tenths < 0 || s.tenths > 500) {   // 50.0 per skill, in tenths
            out.violation = PlanViolation::CreationTooRich;
            out.skillId = s.skillId;
            return out;
        }
        createSkillTotal += s.tenths;
    }
    if (createSkillTotal > 1000) {              // 100.0 total, in tenths
        out.violation = PlanViolation::CreationTooRich;
        return out;
    }

    // --- the split that killed six characters -----------------------------
    //
    // STR 55 / DEX 15 / INT 10 is explicitly rejected by the M4 plan: six
    // characters died solo on it against a single awake animal, one from full
    // health in about 34 seconds. This is a hard refusal rather than a
    // warning, because the failure was silent -- the character created fine
    // and died later, so nothing at creation time flagged it.
    if (plan.createStr == 55 && plan.createDex == 15 && plan.createInt == 10) {
        out.violation = PlanViolation::RejectedCreationSplit;
        return out;
    }

    out.ok = true;
    return out;
}

BuildPlan FrontierLumberjackSwordsman() {
    BuildPlan plan;
    plan.family = "frontier_lumberjack_swordsman";

    // The five skills docs/M4_LIFECYCLE_PLAN.md names, at 100.0 each.
    plan.skills = {
        {rules::kLumberjacking, 1000},
        {rules::kSwordsmanship, 1000},
        {rules::kTactics,       1000},
        {rules::kAnatomy,       1000},
        {rules::kHealing,       1000},
    };

    // 700.0 budget, 500.0 spent. The remaining 200.0 is UNRESOLVED ON PURPOSE.
    // The M4 plan names five skills and stops; filling the rest would be
    // inventing a canonical Revolution build, which the brief forbids. It is
    // carried as a number so the budget check is still exact.
    plan.unresolvedTenths = 2000;

    // 100 / 100 / 25 = 225. Straight off the M3.8 evidence list, and
    // independently corroborated by uo-offline's T2A dexxer stats.
    plan.targetStr = 100;
    plan.targetDex = 100;
    plan.targetInt = 25;

    // MEASURED, not reasoned. 40/35/5 survived the whole kill-carve-loot
    // chain where 55/15/10 died six times, which points at DEX -- swing speed
    // and evasion -- rather than raw strength.
    plan.createStr = 40;
    plan.createDex = 35;
    plan.createInt = 5;

    // Creation skills are chosen for the KIT as much as the skill, from the
    // shard's own newbie templates (templates_special/sp_tm_newbie.scp):
    //   [NEWBIE LUMBERJACKING] -> i_hatchet    (the axe the whole loop needs)
    //   [NEWBIE SWORDSMANSHIP] -> i_katana     (a real weapon, not Wrestling)
    //   [NEWBIE HEALING]       -> i_bandage,50 + i_scissors
    // 40 + 40 + 20 = 100.0, exactly the server's own creation ceiling.
    plan.createSkills = {
        {rules::kLumberjacking, 400},
        {rules::kSwordsmanship, 400},
        {rules::kHealing,       200},
    };

    return plan;
}

std::string MakeIdentityId(const std::string& account, const std::string& character) {
    std::string id;
    id.reserve(account.size() + character.size() + 1);
    auto append = [&id](const std::string& s) {
        for (char c : s) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u)) id.push_back(static_cast<char>(std::tolower(u)));
            else if (c == '-' || c == '_') id.push_back(c);
            else id.push_back('_');
        }
    };
    append(account);
    id.push_back('.');
    append(character);
    return id;
}

i32 Observation::SkillTenths(int skillId) const {
    for (const SkillTarget& s : skills) {
        if (s.skillId == skillId) return s.tenths;
    }
    return 0;
}

i32 Observation::SkillSumTenths() const {
    i32 sum = 0;
    for (const SkillTarget& s : skills) sum += s.tenths;
    return sum;
}


// ---------------------------------------------------------------------------
// M5: a plan is a profession, restated as the thing the character asks for at
// creation plus the things it must earn.
// ---------------------------------------------------------------------------
BuildPlan PlanFromProfession(const prof::Profession& p) {
    BuildPlan plan;
    plan.family = p.id;

    for (const prof::SkillTargetSpec& t : p.targets) {
        plan.skills.push_back({t.skillId, t.tenths});
        plan.viaTrainer.push_back(t.viaTrainer);
        plan.priority.push_back(t.priority);
    }
    plan.unresolvedTenths = p.unresolvedTenths;

    plan.targetStr = p.targetStr;
    plan.targetDex = p.targetDex;
    plan.targetInt = p.targetInt;

    // THE REVOLUTION CREATION RULE: exactly two skills at 50.0, and about 50
    // stat points. Nothing else is requested and nothing else is granted.
    plan.createStr = p.startStr;
    plan.createDex = p.startDex;
    plan.createInt = p.startInt;
    plan.createSkills = {
        {p.startSkillA, prof::kRevolutionStartSkillEach},
        {p.startSkillB, prof::kRevolutionStartSkillEach},
    };
    return plan;
}

int NextSkillToBuy(const BuildPlan& plan, const Observation& obs,
                   i32 trainerCeilingTenths) {
    int best = -1;
    int bestPriority = -1;
    for (usize i = 0; i < plan.skills.size(); ++i) {
        if (i >= plan.viaTrainer.size() || !plan.viaTrainer[i]) continue;
        const SkillTarget& t = plan.skills[i];
        const i32 have = obs.SkillTenths(t.skillId);
        if (have >= t.tenths) continue;
        // Past what a trainer can give, paying one is simply waste -- the
        // character has to grind from here whether it pays or not.
        //
        // 300 is no longer a guess. It is this shard's own configured hard
        // ceiling: sphere.ini NPCTrainMax=300 (set by the owner 2026-08-28;
        // 420 had been Source-X's built-in default, CServerConfig.cpp:149, and
        // was never Revolution's) with NPCTrainPercent=30. Source-X computes
        // min(NPC's own skill x 30%, NPCTrainMax, the student's cap) in
        // CChar::NPC_GetTrainMax (CCharNPCStatus.cpp:514-541), so 30.0 is the
        // most any NPC here can teach, and an NPC below 100.0 in the skill
        // teaches less -- Alenne stopped at 21.9, which puts her Meditation at
        // about 73.0. The per-NPC part is still only knowable by asking, which
        // is why an actual refusal outranks this number below.
        if (have >= trainerCeilingTenths) continue;
        // Which is why the answers actually received outrank the guess. An
        // NPC of this trade has already said no to this skill; the ceiling is
        // below the character and only moves further below as it grows.
        bool refused = false;
        for (int r : obs.trainerRefusedSkills) {
            if (r == t.skillId) { refused = true; break; }
        }
        if (refused) continue;
        const int pri = (i < plan.priority.size()) ? plan.priority[i] : 0;
        if (pri > bestPriority) { bestPriority = pri; best = t.skillId; }
    }
    return best;
}
// ---------------------------------------------------------------------------
// What to make next.
//
// A crafter is not a gatherer with extra steps: it cannot begin at all until
// somebody sells it the inputs, and it must not begin at all unless what it
// makes has somewhere legitimate to go. Both of those are answered here, once,
// so the need model and the goal cannot disagree about them -- the way
// EARN_GOLD and its own need disagreed about the bank and deadlocked.
// ---------------------------------------------------------------------------
// Does this life fight on purpose? A lumberjack finishes fights that find it;
// a swordsman goes looking. Read off the build rather than off a list of
// archetype names: a profession that wants MORE than the 50.0 a weapon school
// is granted at creation intends to use it.
bool WantsToHunt(const prof::Profession& p) {
    for (const prof::SkillTargetSpec& t : p.targets) {
        const bool weapon = t.skillId == rules::kSwordsmanship ||
                            t.skillId == rules::kMaceFighting ||
                            t.skillId == rules::kFencing ||
                            t.skillId == rules::kArchery;
        if (weapon && t.tenths > 500) return true;
    }
    return false;
}

CraftIntent ChooseCraft(const prof::Profession& p, const Observation& obs,
                        i32 batch) {
    CraftIntent out;
    if (batch < 1) batch = 1;

    for (const std::string& made : p.produces) {
        // ONLY WHAT MAY BE SOLD. The registry is the authority on which of
        // this shard's goods a bot may take to an NPC at all, and a good with
        // no legitimate destination is not a reason to spend gold on
        // reagents. (A player-market good will belong here too once the
        // player market can actually complete a sale; it cannot yet, so
        // making for it would be manufacturing into a void.)
        if (!faucet::AllowedForItem(made.c_str())) continue;

        const prod::Recipe* r = prod::FindRecipe(made.c_str());
        if (!r) continue;
        // Gathered things are not crafted things. A fisher's "produces" holds
        // fish, and fish come out of the sea.
        if (!r->inputs[0].item) continue;

        // The recipe's own skill requirements, against what the SERVER last
        // reported. Never against the build plan: a plan is an intention.
        const bool skillsOk =
            (r->skillId < 0 || obs.SkillTenths(r->skillId) >= r->skillTenths) &&
            (r->skillId2 < 0 || obs.SkillTenths(r->skillId2) >= r->skillTenths2);
        if (!skillsOk) {
            if (!out.item) {
                out.item = nullptr;
                out.why = "the skills for everything this life makes are short";
            }
            continue;
        }

        out.item = r->output;
        out.skillsMet = true;
        out.why = "can be made and can be sold";
        for (const prod::Ingredient& in : r->inputs) {
            if (!in.item || in.qty <= 0) continue;
            const i32 want = in.qty * batch;
            const i32 have = market::QtyOf(obs.pack, in.item);
            if (have < want) {
                prod::Ingredient shortfall;
                shortfall.item = in.item;
                shortfall.qty = want - have;
                out.missing.push_back(shortfall);
            }
        }
        if (out.missing.empty()) out.why = "every input is in the pack";
        else out.why = "inputs are short";
        return out;
    }
    if (!out.why || !*out.why) out.why = "this life makes nothing sellable";
    return out;
}

}  // namespace uo::life
