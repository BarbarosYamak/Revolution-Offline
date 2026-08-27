#include "uo/rules.h"

namespace uo::rules {

const Profile& Revolution() {
    static const Profile p{};
    return p;
}

const std::vector<int>& InactiveSkills() {
    static const std::vector<int> kInactive = {
        kHerding, kRemoveTrap, kMagicResistance, kEnticement, kPeacemaking,
        kProvocation, kSpiritSpeak, kForensics, kTasteId,
    };
    return kInactive;
}

bool SkillActive(int skillId) {
    for (int s : InactiveSkills())
        if (s == skillId) return false;
    return true;
}

const char* ViolationName(Violation v) {
    switch (v) {
        case Violation::None:          return "none";
        case Violation::OverTotal:     return "over_total";
        case Violation::OverPerSkill:  return "over_per_skill";
        case Violation::InactiveSkill: return "inactive_skill";
        case Violation::Negative:      return "negative";
        default:                       return "?";
    }
}

BuildCheck ValidateBuild(const Profile& p, const std::vector<BuildSkill>& build) {
    BuildCheck c;
    c.skillCount = build.size();

    for (const BuildSkill& s : build) {
        if (s.tenths < 0) {
            c.violation = Violation::Negative;
            c.skillId = s.skillId;
            return c;
        }
        if (s.tenths > p.perSkillCapTenths) {
            c.violation = Violation::OverPerSkill;
            c.skillId = s.skillId;
            c.totalTenths = 0;
            return c;
        }
        if (s.tenths > 0 && !SkillActive(s.skillId)) {
            // Resisting Spells is the one that matters in practice: the server
            // offers it, Revolution did not run it, and a generator that trusts
            // the server will put it in every mage build.
            c.violation = Violation::InactiveSkill;
            c.skillId = s.skillId;
            return c;
        }
        c.totalTenths += s.tenths;
    }

    if (c.totalTenths > p.totalSkillCapTenths) {
        c.violation = Violation::OverTotal;
        return c;
    }

    c.ok = true;
    return c;
}

// --- poisoning --------------------------------------------------------------

bool CanTrainPoisoning(const Profile& p, i32 mageryTenths) {
    (void)p; (void)mageryTenths;
    // Poisoning is an active skill with no Magery gate of any kind. The
    // best-attested warlocks on the shard carry Magery 100 AND Poisoning 100.
    return true;
}

bool CanCastPoisonSpell(const Profile& p, i32 mageryTenths) {
    (void)p; (void)mageryTenths;
    // Not only allowed, but the guide says Poisoning *increases* the Poison
    // spell's power. High Magery and high Poisoning is a coherent build, not a
    // contradiction.
    return true;
}

bool CanApplyPoisonToWeapon(const Profile& p, i32 poisoningTenths) {
    (void)p;
    // "Warriorların silah sürmesi için bu skill yeterlidir" -- the Poisoning
    // skill is what applies poison. No Magery term appears in that sentence.
    return poisoningTenths > 0;
}

bool CanUsePoisonedWeapon(const Profile& p, i32 mageryTenths) {
    // The one real restriction: "Büyücü yeteneği 40.0 ın üstündeki savaşçılar
    // zehirli silahı kullanamazlar." Above 40.0, not at it.
    return mageryTenths <= p.poisonedWeaponMaxMageryTenths;
}

// --- teaching ---------------------------------------------------------------

i32 TeachingMaxTenths(const Profile& p, i32 trainerSkillTenths) {
    if (trainerSkillTenths <= 0) return 0;
    i32 max = (trainerSkillTenths * p.teachingPercentOfTrainer) / 100;
    if (max > p.teachingAbsoluteMaxTenths) max = p.teachingAbsoluteMaxTenths;
    return max;
}

i32 TeachingQuote(const Profile& p, i32 fromTenths, i32 trainerSkillTenths) {
    const i32 ceiling = TeachingMaxTenths(p, trainerSkillTenths);
    if (fromTenths >= ceiling) return 0;
    return (ceiling - fromTenths) * p.teachingGoldPerTenth;
}

bool PaymentIsExact(const Profile& p, i32 quote, i32 gold) {
    // Overpaying is not generosity, it is loss: the trainer keeps the change.
    // Underpaying buys proportionally fewer points, which is legal but is not
    // what "pay the quote" means.
    if (!p.teacherKeepsChange) return gold >= quote;
    return gold == quote;
}

const char* TrainerVerdictName(TrainerVerdict v) {
    switch (v) {
        case TrainerVerdict::Pay:                return "pay";
        case TrainerVerdict::NoTrainer:          return "no_trainer";
        case TrainerVerdict::NothingToTeach:     return "nothing_to_teach";
        case TrainerVerdict::CannotAfford:       return "cannot_afford";
        case TrainerVerdict::SkillInactive:      return "skill_inactive";
        case TrainerVerdict::ExceedsBuildBudget: return "exceeds_build_budget";
        default:                                 return "?";
    }
}

TrainerDecision DecideTraining(const Profile& p, const TrainerSituation& s) {
    TrainerDecision d;

    if (!s.skillIsActive) { d.verdict = TrainerVerdict::SkillInactive; return d; }
    if (s.trainerSkillTenths <= 0) { d.verdict = TrainerVerdict::NoTrainer; return d; }

    i32 ceiling = TeachingMaxTenths(p, s.trainerSkillTenths);
    // Never buy past what the build actually wants.
    if (ceiling > s.targetTenths) ceiling = s.targetTenths;
    if (ceiling <= s.currentTenths) {
        d.verdict = TrainerVerdict::NothingToTeach;
        return d;
    }

    i32 points = ceiling - s.currentTenths;
    // Nor past what the 700-point budget can still hold.
    if (points > s.buildHeadroomTenths) points = s.buildHeadroomTenths;
    if (points <= 0) {
        d.verdict = TrainerVerdict::ExceedsBuildBudget;
        return d;
    }

    const i32 spendable = s.gold - s.goldReserve;
    const i32 fullQuote = points * p.teachingGoldPerTenth;
    if (spendable < fullQuote) {
        // Sphere will teach proportionally for less, but the trainer keeps
        // whatever it is handed -- so buying a partial amount is legitimate
        // only when the reduced quote is itself paid exactly.
        const i32 affordablePoints = spendable / p.teachingGoldPerTenth;
        if (affordablePoints <= 0) {
            d.verdict = TrainerVerdict::CannotAfford;
            return d;
        }
        points = affordablePoints;
    }

    d.verdict = TrainerVerdict::Pay;
    d.pointsBought = points;
    d.buyToTenths = s.currentTenths + points;
    d.quote = points * p.teachingGoldPerTenth;
    return d;
}

} // namespace uo::rules
