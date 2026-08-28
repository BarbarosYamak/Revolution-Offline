#include "uo/life.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace uo::life {

const char* NeedKindName(NeedKind k) {
    switch (k) {
        case NeedKind::StayAlive:     return "StayAlive";
        case NeedKind::Heal:          return "Heal";
        case NeedKind::RecoverCorpse: return "RecoverCorpse";
        case NeedKind::NeedTool:      return "NeedTool";
        case NeedKind::NeedEquipment: return "NeedEquipment";
        case NeedKind::NeedFood:      return "NeedFood";
        case NeedKind::NeedBank:      return "NeedBank";
        case NeedKind::NeedGold:      return "NeedGold";
        case NeedKind::NeedLogs:      return "NeedLogs";
        case NeedKind::NeedTraining:  return "NeedTraining";
        case NeedKind::NeedTravel:    return "NeedTravel";
        case NeedKind::Count:         break;
    }
    return "?";
}

namespace {

std::string Fmt(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

const char* SkillName(int id) {
    switch (id) {
        case rules::kLumberjacking: return "Lumberjacking";
        case rules::kSwordsmanship: return "Swordsmanship";
        case rules::kTactics:       return "Tactics";
        case rules::kAnatomy:       return "Anatomy";
        case rules::kHealing:       return "Healing";
        default:                    return "skill";
    }
}

}  // namespace

std::vector<Need> AssessNeeds(const BuildPlan& plan, const Memory& mem,
                              const Observation& obs, const NeedConfig& cfg) {
    std::vector<Need> needs;
    if (!obs.inWorld) return needs;

    auto add = [&needs](NeedKind kind, double urgency, std::string what,
                        std::string reason, std::string evidence,
                        bool blocked = false) {
        Need n;
        n.kind = kind;
        n.urgency = std::min(1.0, std::max(0.0, urgency));
        n.what = std::move(what);
        n.reason = std::move(reason);
        n.evidence = std::move(evidence);
        n.blocked = blocked;
        needs.push_back(std::move(n));
    };

    // --- death first: nothing else matters while dead ----------------------
    if (obs.dead) {
        add(NeedKind::StayAlive, 1.0, "resurrection",
            "character is a ghost",
            "life=dead");
        if (obs.corpseKnown) {
            // Bounded, on purpose. A corpse run is a decision with a cost,
            // not an obligation; three failed approaches and abandoning is
            // the rational move (M4 brief, Phase 17).
            const bool exhausted = obs.corpseRecoveryAttempts >= 3;
            add(NeedKind::RecoverCorpse, exhausted ? 0.2 : 0.75, "own corpse",
                exhausted ? "corpse recovery attempts exhausted"
                          : "gear and carried resources are on the corpse",
                Fmt("corpse=%d,%d attempts=%d", obs.corpseX, obs.corpseY,
                    obs.corpseRecoveryAttempts),
                exhausted);
        }
        return needs;   // a ghost has no other needs it can act on
    }

    const double hpFrac = obs.HpFraction();

    // --- survival ----------------------------------------------------------
    //
    // ONLY when something is actually on us. A hostile visible across the
    // clearing is scenery: the M4 plan is explicit that this character does
    // not hunt as its main activity, and treating every sighting as a survival
    // emergency turned the first live lumberjack into a wandering monster
    // hunter that never finished a tree.
    if (obs.underAttack || obs.attackersOnMe > 0) {
        // Gang pressure: incoming damage scales with attackers while outgoing
        // does not, so the bail threshold rises with every extra attacker
        // (uo-offline's measured shape, our M3.9.1 base value).
        double bailAt = cfg.fleeHpFraction;
        const i32 extra = obs.attackersOnMe - 1;
        if (extra > 0) bailAt = std::min(0.90, bailAt + 0.08 * std::min(3, extra));

        if (hpFrac < bailAt) {
            add(NeedKind::StayAlive, 1.0, "disengage",
                "health below the bail threshold for this many attackers",
                Fmt("hp=%.0f%% bail=%.0f%% attackers=%d",
                    hpFrac * 100.0, bailAt * 100.0, obs.attackersOnMe));
        } else {
            add(NeedKind::StayAlive, 0.55, "a fight already started",
                "something is attacking or standing in melee range",
                Fmt("attackers=%d hostiles_seen=%d hp=%.0f%%",
                    obs.attackersOnMe, obs.hostilesNear, hpFrac * 100.0));
        }
    }

    if (hpFrac < cfg.healHpFraction) {
        const bool haveBandages = obs.bandages > 0;
        add(NeedKind::Heal, 0.4 + (cfg.healHpFraction - hpFrac), "bandage",
            haveBandages ? "wounded and carrying bandages"
                         : "wounded with no bandages carried",
            Fmt("hp=%d/%d bandages=%d", obs.hp, obs.hpMax, obs.bandages),
            !haveBandages);
    }

    // --- the tool the whole profession depends on --------------------------
    if (!obs.axeInPack && !obs.axeEquipped) {
        const KnownSupplier* supplier = mem.BestSupplier("hatchet");
        add(NeedKind::NeedTool, 0.9, "hatchet",
            "no usable axe: a lumberjack cannot work without one",
            supplier ? Fmt("known supplier '%s' at %d,%d",
                           supplier->name.c_str(), supplier->x, supplier->y)
                     : std::string("no known supplier of a hatchet"),
            supplier == nullptr);
    }

    // --- a weapon, so incidental danger is survivable ----------------------
    if (!obs.weaponEquipped) {
        // An axe IS a weapon in this build -- the era Lumberjack fights with
        // it -- so this only fires when there is nothing in hand at all.
        const bool haveAnything = obs.axeInPack || obs.axeEquipped;
        add(NeedKind::NeedEquipment, haveAnything ? 0.45 : 0.7, "weapon",
            haveAnything ? "carrying an axe but fighting unarmed"
                         : "nothing to fight with",
            Fmt("axe_pack=%d axe_worn=%d", obs.axeInPack ? 1 : 0,
                obs.axeEquipped ? 1 : 0));
    }

    if (obs.bandages < cfg.bandageLow) {
        const KnownSupplier* supplier = mem.BestSupplier("bandage");
        add(NeedKind::NeedEquipment, 0.5, "bandages",
            "below the bandage floor; a fight without them is a death",
            Fmt("bandages=%d low=%d", obs.bandages, cfg.bandageLow),
            supplier == nullptr && obs.gold < 50);
    }

    // --- hunger is live on this shard (HitsHungerLoss=1) -------------------
    if (cfg.hungerLive && obs.food < cfg.foodLow) {
        add(NeedKind::NeedFood, 0.25, "food",
            "carrying no food and hunger is enabled on this shard",
            Fmt("food=%d", obs.food));
    }

    // --- weight and banking ------------------------------------------------
    const double weightFrac = obs.WeightFraction();
    if (weightFrac >= cfg.bankWeightFrac) {
        add(NeedKind::NeedBank, 0.6 + (weightFrac - cfg.bankWeightFrac),
            "deposit carried load",
            "close to the carry limit; further gathering is wasted",
            Fmt("weight=%d/%d (%.0f%%) logs=%d", obs.weight, obs.maxWeight,
                weightFrac * 100.0, obs.logs));
    } else if (obs.logs >= cfg.logsWorthBanking) {
        add(NeedKind::NeedBank, 0.35, "deposit logs",
            "enough logs carried to be worth securing",
            Fmt("logs=%d threshold=%d", obs.logs, cfg.logsWorthBanking));
    }

    if (obs.gold < cfg.goldFloor) {
        add(NeedKind::NeedGold, 0.3, "gold",
            "too little gold to replace a tool or restock bandages",
            Fmt("gold=%d floor=%d", obs.gold, cfg.goldFloor));
    }

    // --- the work itself ---------------------------------------------------
    {
        const KnownResourceSource* src = mem.BestResource("logs", obs.x, obs.y, obs.nowMs);
        const bool canWork = obs.axeInPack || obs.axeEquipped;
        add(NeedKind::NeedLogs, canWork ? 0.4 : 0.1, "logs",
            "logs are this character's income and its Lumberjacking training",
            src ? Fmt("known stand at %d,%d successes=%d failures=%d",
                      src->x, src->y, src->successes, src->failures)
                : std::string("no remembered stand yet"),
            !canWork);
    }

    // --- progression toward the target build -------------------------------
    //
    // A training NEED always exists while a skill is short of target, but it
    // is only ACTIONABLE when there is something to practise on. Without that
    // gate the planner completes TRAIN_COMBAT instantly (nothing to fight),
    // re-selects it on the next tick, and spins -- which the first live run
    // did, several times a second.
    for (const SkillTarget& t : plan.skills) {
        const i32 have = obs.SkillTenths(t.skillId);
        if (have >= t.tenths) continue;
        const double gap = static_cast<double>(t.tenths - have) /
                           static_cast<double>(t.tenths > 0 ? t.tenths : 1);
        const bool nothingToPractiseOn = obs.attackersOnMe == 0;
        add(NeedKind::NeedTraining, 0.15 + 0.25 * gap, SkillName(t.skillId),
            nothingToPractiseOn
                ? "below target, but nothing is here to practise combat on"
                : "below the target build value for this skill",
            Fmt("%s %.1f -> %.1f", SkillName(t.skillId), have / 10.0,
                t.tenths / 10.0),
            nothingToPractiseOn);
    }

    // --- being in the right place -----------------------------------------
    //
    // Arrival is a claim about the TILE, not about the journey. This is the
    // uo-offline site-discipline lesson: a bot that stops short and starts
    // chopping air hides its own failure.
    if (!obs.atWorkSite && !obs.atBank) {
        add(NeedKind::NeedTravel, 0.2, "a place where work is possible",
            "standing somewhere that is neither the work site nor the bank",
            Fmt("at=%d,%d work_site=0 bank=0", obs.x, obs.y));
    }

    // Highest urgency first, so a caller that only wants the top need gets
    // the right one and the [life] log reads in priority order.
    std::stable_sort(needs.begin(), needs.end(),
                     [](const Need& a, const Need& b) { return a.urgency > b.urgency; });
    return needs;
}

}  // namespace uo::life
