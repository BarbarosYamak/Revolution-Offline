#include "uo/rules.h"

#include <cstdio>

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

const char* SkillName(int skillId) {
    switch (skillId) {
        case kAlchemy:          return "Alchemy";
        case kAnatomy:          return "Anatomy";
        case kAnimalLore:       return "Animal Lore";
        case kArmsLore:         return "Arms Lore";
        case kBlacksmithing:    return "Blacksmithing";
        case kBowcraft:         return "Bowcraft";
        case kCarpentry:        return "Carpentry";
        case kCartography:      return "Cartography";
        case kCooking:          return "Cooking";
        case kEvaluatingIntel:  return "Evaluating Intelligence";
        case kFishing:          return "Fishing";
        case kForensics:        return "Forensic Evaluation";
        case kHealing:          return "Healing";
        case kHerding:          return "Herding";
        case kInscription:      return "Inscription";
        case kLockpicking:      return "Lockpicking";
        case kLumberjacking:    return "Lumberjacking";
        case kMagery:           return "Magery";
        case kMagicResistance:  return "Resisting Spells";
        case kMeditation:       return "Meditation";
        case kMining:           return "Mining";
        case kPoisoning:        return "Poisoning";
        case kParrying:         return "Parrying";
        case kProvocation:      return "Provocation";
        case kRemoveTrap:       return "Remove Trap";
        case kSpiritSpeak:      return "Spirit Speak";
        case kSwordsmanship:    return "Swordsmanship";
        case kTactics:          return "Tactics";
        case kTailoring:        return "Tailoring";
        case kTaming:           return "Animal Taming";
        case kTasteId:          return "Taste Identification";
        case kTinkering:        return "Tinkering";
        case kVeterinary:       return "Veterinary";
        case kArchery:          return "Archery";
        case kMaceFighting:     return "Mace Fighting";
        case kFencing:          return "Fencing";
        case kWrestling:        return "Wrestling";
        default:                break;
    }
    // Deliberately not a name we made up: an id we have no evidence for prints
    // as the id, so an UNKNOWN skill reads as UNKNOWN in the logs.
    static char buf[24];
    std::snprintf(buf, sizeof(buf), "skill %d", skillId);
    return buf;
}

int SkillStatStr(int skillId) {
    // Index == skill id, exactly as the runtime names its files
    // (skill<N>_<name>.scp). Transcribed 2026-09-04 from
    // runtime/scripts/skills/, one grep of STAT_STR= per file; nothing here is
    // inferred. The two that matter most to the stat-farm errand:
    // Magery 20 and Meditation 10 (a caster's ceiling), Wrestling 100
    // (skill43_wrestling.scp:12, with BONUS_STR=50 BONUS_STATS=10).
    static const int kStatStr[] = {
        /*  0 alchemy        */   5, /*  1 anatomy         */  15,
        /*  2 animal lore    */   0, /*  3 appraise        */   0,
        /*  4 arms lore      */  10, /*  5 parrying        */  75,
        /*  6 begging        */   5, /*  7 blacksmithing   */  95,
        /*  8 bowcraft       */  40, /*  9 peacemaking     */   0,
        /* 10 camping        */  30, /* 11 carpentry       */  60,
        /* 12 cartography    */  15, /* 13 cooking         */  25,
        /* 14 detect hidden  */  15, /* 15 enticement      */  15,
        /* 16 eval intel     */   5, /* 17 healing         */  10,
        /* 18 fishing        */  40, /* 19 forensics       */  10,
        /* 20 herding        */  50, /* 21 hiding          */  20,
        /* 22 provocation    */  20, /* 23 inscription     */  15,
        /* 24 lockpicking    */  20, /* 25 magery          */  20,
        /* 26 magic resist   */  40, /* 27 tactics         */  60,
        /* 28 snooping       */  30, /* 29 musicianship    */  20,
        /* 30 poisoning      */  15, /* 31 archery         */  40,
        /* 32 spirit speak   */  20, /* 33 stealing        */  40,
        /* 34 tailoring      */  30, /* 35 taming          */  30,
        /* 36 taste id       */  25, /* 37 tinkering       */  30,
        /* 38 tracking       */  25, /* 39 veterinary      */  30,
        /* 40 swordsmanship  */  75, /* 41 mace fighting   */ 100,
        /* 42 fencing        */  55, /* 43 wrestling       */ 100,
        /* 44 lumberjacking  */  85, /* 45 mining          */  85,
        /* 46 meditation     */  10, /* 47 stealth         */  20,
        /* 48 remove trap    */  20, /* 49 necromancy      */  20,
        /* 50 focus          */  10, /* 51 chivalry        */ 100,
        /* 52 bushido        */  55, /* 53 ninjitsu        */  55,
        /* 54 spellweaving   */  20, /* 55 mysticism       */  20,
        /* 56 imbuing        */  15, /* 57 throwing        */  40,
    };
    constexpr int kN = static_cast<int>(sizeof(kStatStr) / sizeof(kStatStr[0]));
    if (skillId < 0 || skillId >= kN) return -1;
    return kStatStr[skillId];
}

} // namespace uo::rules
