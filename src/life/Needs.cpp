#include "uo/life.h"

#include "uo/vendor_policy.h"

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
        case NeedKind::NeedSkillTraining: return "NeedSkillTraining";
        case NeedKind::NeedTravel:    return "NeedTravel";
        case NeedKind::NeedTrade:     return "NeedTrade";
        case NeedKind::NeedCatch:     return "NeedCatch";
        case NeedKind::Count:         break;
    }
    return "?";
}

namespace {

// Does this life carry the thing at all? The catalogue is the answer; a life
// saved before the catalogue existed (cfg.profession == nullptr) keeps the M4
// lumberjack answers, so nothing that already works changes.
bool WantsTool(const NeedConfig& cfg, const char* name) {
    if (!cfg.profession) return true;
    for (const prof::ToolNeed& t : cfg.profession->tools) {
        if (t.name == name) return true;
    }
    return false;
}

bool WantsConsumable(const NeedConfig& cfg, const char* name) {
    if (!cfg.profession) return true;
    for (const prof::ConsumableNeed& c : cfg.profession->consumables) {
        if (c.name == name) return true;
    }
    return false;
}

// Is the load the character is carrying something it means to SELL, with a
// buyer that actually exists on this shard?
//
// Banking a good you intend to sell is not securing it, it is hoarding it. At
// twenty logs the bank need scores 0.35 x 240 = 84 and the surplus need scores
// 0.22 x 150 = 33, so without this a lumberjack would carry its own income to
// the bank forever and never once visit a buyer.
//
// The weight-driven bank clauses above are NOT gated on this: a pack that is
// overflowing has to be dealt with wherever the character is standing.
bool SellableInstead(const NeedConfig& cfg) {
    if (!cfg.profession) return false;
    // ANY market counts, not just an NPC one.
    //
    // The first version asked only HasNpcBuyer, and after logs became a
    // player-market good that made a lumberjack bank every log as "securing
    // it" -- 570 BANK goals in one fleet session, packs emptied, and
    // TRADE_WITH_PLAYER never once fired because there was no surplus left to
    // announce. Banking a good you mean to sell is hoarding it whichever
    // counter you were going to sell it over.
    //
    // A life that produces something HAS a market for it by definition: an NPC
    // buys it, or a player does. The weight-driven bank clauses still catch a
    // genuinely full pack, which is the case where banking really is the right
    // answer.
    return !cfg.profession->produces.empty();
}

i32 QtyIn(const std::vector<market::Stock>& pack, const char* item) {
    for (const market::Stock& s : pack) {
        if (s.item == item) return s.qty;
    }
    return 0;
}

bool GathersLogs(const NeedConfig& cfg) {
    return !cfg.profession || cfg.profession->gathers == "logs";
}

std::string Fmt(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

using rules::SkillName;


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
        // NERVE IS PER LIFE. Every profession carries a riskTolerance -- 0.55
        // for a swordsman down to 0.20 for a fisher -- and until now nothing
        // read it: every character fled at the same shared threshold, so the
        // fisher's "avoids everything" and the swordsman's willingness to
        // stand were both inert data.
        //
        // A cautious life bails EARLIER, so lower tolerance raises the
        // threshold. Bounded either side: nobody fights to 5% and nobody runs
        // at full health.
        double bailAt = cfg.fleeHpFraction;
        if (cfg.profession) {
            const double nerve = cfg.profession->riskTolerance;   // 0..1
            bailAt = std::min(0.75, std::max(0.20,
                        cfg.fleeHpFraction + (0.5 - nerve) * 0.4));
        }
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
    // EVERY tool this life needs, not just an axe.
    //
    // Gating on WantsTool("hatchet") stopped a mage asking for one, which was
    // right as far as it went -- but it also meant a FISHER never generated a
    // tool need at all, so it never got a GET_TOOL goal, and it stood beside a
    // lake failing to fish for want of a pole it never tried to buy. The
    // fifth place in this codebase written for one archetype.
    if (cfg.profession) {
        for (const prof::ToolNeed& t : cfg.profession->tools) {
            if (obs.HasTool(t.name)) continue;
            const KnownSupplier* supplier = mem.BestSupplier(t.name.c_str());
            add(NeedKind::NeedTool, 0.9, t.name,
                "this life cannot do its own work without one",
                supplier ? Fmt("known supplier '%s' at %d,%d",
                               supplier->name.c_str(), supplier->x, supplier->y)
                         : Fmt("no known supplier of a %s", t.name.c_str()),
                supplier == nullptr && obs.gold < 20);
        }
    } else if (!obs.axeInPack && !obs.axeEquipped) {
        const KnownSupplier* supplier = mem.BestSupplier("hatchet");
        add(NeedKind::NeedTool, 0.9, "hatchet",
            "no usable axe: a lumberjack cannot work without one",
            supplier ? Fmt("known supplier '%s' at %d,%d",
                           supplier->name.c_str(), supplier->x, supplier->y)
                     : std::string("no known supplier of a hatchet"),
            supplier == nullptr);
    }

    // --- a weapon, so incidental danger is survivable ----------------------
    // Only for a life whose tool IS its weapon. A mage's answer to "nothing in
    // hand" is a spellbook and a spell, not a sword, and M6 owns that; saying
    // NeedEquipment here would just be a need it can never clear.
    if (WantsTool(cfg, "hatchet") && !obs.weaponEquipped) {
        // An axe IS a weapon in this build -- the era Lumberjack fights with
        // it -- so this only fires when there is nothing in hand at all.
        const bool haveAnything = obs.axeInPack || obs.axeEquipped;
        add(NeedKind::NeedEquipment, haveAnything ? 0.45 : 0.7, "weapon",
            haveAnything ? "carrying an axe but fighting unarmed"
                         : "nothing to fight with",
            Fmt("axe_pack=%d axe_worn=%d", obs.axeInPack ? 1 : 0,
                obs.axeEquipped ? 1 : 0));
    }

    if (WantsConsumable(cfg, "bandage") && obs.bandages < cfg.bandageLow) {
        const KnownSupplier* supplier = mem.BestSupplier("bandage");
        // Gold is not the question. i_bandage is not in the M3.7 vendor
        // matrix at all, so it grades UNKNOWN and the policy refuses it --
        // permanently, until the research gap is closed. A need whose only
        // route is refused must be reported BLOCKED here, not selected as a
        // goal and then discovered to be impossible inside the goal body.
        //
        // The first live catalogue lumberjack proved why: it had 1000 gold,
        // so this need looked satisfiable, won the scoring at 130, and then
        // sat on a 30-second retry forever. It never chopped a log.
        const econ::VendorRuling ruling = econ::CanUseNPCVendorFor("i_bandage");
        const bool noRoute = supplier == nullptr &&
                             (!ruling.allowed || obs.gold < 50);
        add(NeedKind::NeedEquipment, 0.5, "bandages",
            "below the bandage floor; a fight without them is a death",
            supplier != nullptr
                ? Fmt("bandages=%d low=%d, supplier '%s'", obs.bandages,
                      cfg.bandageLow, supplier->name.c_str())
                : Fmt("bandages=%d low=%d, no supplier and the vendor policy "
                      "grades a bandage %s", obs.bandages, cfg.bandageLow,
                      econ::VendorClassName(ruling.klass)),
            noRoute);
    }

    // --- hunger is live on this shard (HitsHungerLoss=1) -------------------
    if (cfg.hungerLive && obs.food < cfg.foodLow) {
        add(NeedKind::NeedFood, 0.25, "food",
            "carrying no food and hunger is enabled on this shard",
            Fmt("food=%d", obs.food));
    }

    // --- weight and banking ------------------------------------------------
    const double weightFrac = obs.WeightFraction();
    if (obs.overloaded) {
        // Already spilling onto the ground. Nothing else matters about the
        // pack: every further log is dropped where anyone can take it.
        add(NeedKind::NeedBank, 0.95, "deposit carried load",
            "the pack has overflowed and logs are going on the floor",
            Fmt("server said 'it is too heavy'; weight=%d/%d logs=%d",
                obs.weight, obs.maxWeight, obs.logs));
    } else if (weightFrac >= cfg.bankWeightFrac) {
        add(NeedKind::NeedBank, 0.6 + (weightFrac - cfg.bankWeightFrac),
            "deposit carried load",
            "close to the carry limit; further gathering is wasted",
            Fmt("weight=%d/%d (%.0f%%) logs=%d", obs.weight, obs.maxWeight,
                weightFrac * 100.0, obs.logs));
    } else if (obs.logs >= cfg.logsWorthBanking && !SellableInstead(cfg)) {
        add(NeedKind::NeedBank, 0.35, "deposit logs",
            "enough logs carried to be worth securing",
            Fmt("logs=%d threshold=%d", obs.logs, cfg.logsWorthBanking));
    }

    if (obs.gold < cfg.goldFloor) {
        add(NeedKind::NeedGold, 0.3, "gold",
            "too little gold to replace a tool or restock bandages",
            Fmt("gold=%d floor=%d", obs.gold, cfg.goldFloor));
    }

    // Having goods worth selling is its own reason to visit a buyer, quite
    // separate from being short of gold. Keyed only to the purse, a character
    // that started with 1000 gp would carry a pack full of its own output
    // forever and never once sell anything -- which is not what a player does
    // with a surplus, and would have made the whole sell path unreachable on
    // a new character.
    //
    // Weaker than being broke: work first, errands second.
    if (cfg.profession) {
        // Pack AND bank. Goods sitting in the box are still this character's
        // stock -- it just has to go and fetch them, which is a step in the
        // errand rather than a reason not to have one.
        std::vector<market::Stock> holdings = obs.pack;
        for (const market::Stock& b : obs.bank) {
            bool merged = false;
            for (market::Stock& h : holdings) {
                if (h.item == b.item) { h.qty += b.qty; merged = true; break; }
            }
            if (!merged) holdings.push_back(b);
        }
        const std::vector<market::Offer> spare =
            market::Surplus(*cfg.profession, holdings, market::TradePolicy{});
        if (!spare.empty()) {
            // Urgency GROWS with the load, because "worth walking to town for"
            // is a question about quantity. Flat, it lost to gathering forever:
            // 0.22 x 150 = 33 against NeedLogs at 0.4 x 130 = 52, so a
            // lumberjack chopped until its pack overflowed and then banked --
            // never once selling.
            //
            // Ramps from a low floor at the minimum worth offering to 0.55 at
            // a full load, which crosses NeedLogs at roughly the point a
            // player would stop and head for town.
            i32 biggest = 0;
            for (const market::Offer& o : spare) biggest = std::max(biggest, o.qty);
            const i32 trip = std::max(1, cfg.surplusWorthTrip);
            const double frac = std::min(1.0, static_cast<double>(biggest) / trip);
            const double urgency = 0.15 + 0.40 * frac;
            // IS THERE ANYWHERE TO TAKE IT? A surplus with no buyer is not a
            // reason to go anywhere, and saying it is produces exactly the
            // churn the exhausted-area fix cured: the goal wins the scoring,
            // discovers on entry that nothing buys logs, completes with
            // progress 0, and is re-picked two seconds later.
            //
            // After the Revolution correction this is the NORMAL case for a
            // gatherer -- logs and ingots are player-market goods -- so it has
            // to read as a legible blocked state rather than a loop.
            std::string route;
            for (const market::Offer& o : spare) {
                if (market::HasNpcBuyer(o.item.c_str())) { route = "an NPC"; break; }
                const KnownSupplier* buyer =
                    mem.BestSupplier((std::string("buyer:") + o.item).c_str());
                if (buyer) { route = buyer->name; break; }
            }
            // No NPC route, but a PLAYER might want it. That is a real
            // errand rather than a blocked state, and it is the only market a
            // gatherer has left once materials stopped being NPC-sellable.
            if (route.empty()) {
                add(NeedKind::NeedTrade, urgency, "sell to a player",
                    "carrying goods no vendor will take, which is what the "
                    "player market is for",
                    Fmt("%d x %s spare", biggest, spare.front().item.c_str()));
            }
            add(NeedKind::NeedGold, urgency, "sell surplus",
                route.empty()
                    ? "carrying its own output with nobody known to buy it"
                    : "carrying more of its own output than its own work needs",
                route.empty()
                    ? Fmt("%d x %s spare, and no buyer known -- on this shard "
                          "it is a player-market good", biggest,
                          spare.front().item.c_str())
                    : Fmt("%d x %s spare, a load is %d, buyer: %s", biggest,
                          spare.front().item.c_str(), trip, route.c_str()),
                route.empty());
        }
    }

    // --- the work itself ---------------------------------------------------
    if (GathersLogs(cfg)) {
        // Proven first, then a lead. Never the old catch-all: a "stand" that
        // never yielded is not evidence of anything.
        const KnownResourceSource* src =
            mem.BestProvenResource("logs", obs.x, obs.y, obs.nowMs);
        const bool provenSrc = src != nullptr;
        if (!src) src = mem.BestHint("logs", obs.x, obs.y, obs.nowMs);
        const bool canWork = obs.axeInPack || obs.axeEquipped;
        add(NeedKind::NeedLogs, canWork ? 0.4 : 0.1, "logs",
            "logs are this character's income and its Lumberjacking training",
            src ? (provenSrc
                       ? Fmt("proven stand at %d,%d (%d successes, %d failures)",
                             src->x, src->y, src->successes, src->failures)
                       : Fmt("a lead on %s at %d,%d, untested",
                             src->label.empty() ? "woods" : src->label.c_str(),
                             src->x, src->y))
                : std::string("no stand and no lead"),
            !canWork);
    }

    // --- fishing, for a life that fishes -----------------------------------
    //
    // Generic by construction: it reads `gathers` from the catalogue rather
    // than assuming the character is a lumberjack, which is the mistake this
    // file has already made three times.
    if (cfg.profession && cfg.profession->gathers == "fish") {
        bool havePole = false;
        for (const prof::ToolNeed& t : cfg.profession->tools) {
            if (t.name == "fishing pole") { havePole = obs.axeEquipped || true; break; }
        }
        (void)havePole;
        add(NeedKind::NeedCatch, 0.45, "fish",
            "fish are this life's income and its Fishing training",
            Fmt("carrying %d", QtyIn(obs.pack, "i_fish_big_1")));
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

    // --- a skill worth BUYING ---------------------------------------------
    //
    // The economically grounded progression the M5/M7 briefs both describe:
    // the character notices it lacks a skill its life needs, works until it
    // can afford the fee, pays an NPC, and only then starts training normally.
    //
    // The need is BLOCKED rather than absent when the gold is short, so the
    // reason a character is still gathering is legible: it is saving.
    if (obs.wantTrainSkill >= 0) {
        const i32 have = obs.SkillTenths(obs.wantTrainSkill);
        if (have < obs.wantTrainTarget) {
            // The fee is not known until an NPC quotes it. `trainerFeeGuess`
            // is only used to decide whether it is worth WALKING there --
            // never to decide what to pay.
            const bool canAfford = obs.gold >= cfg.trainerFeeGuess;
            add(NeedKind::NeedSkillTraining,
                have <= 0 ? 0.55 : 0.30,
                SkillName(obs.wantTrainSkill),
                have <= 0
                    ? "this life needs a skill the character does not have at all"
                    : "an NPC can teach this faster than grinding it from here",
                Fmt("%s %.1f -> %.1f, gold %d (fee is quoted on arrival, "
                    "roughly %d)", SkillName(obs.wantTrainSkill), have / 10.0,
                    obs.wantTrainTarget / 10.0, obs.gold, cfg.trainerFeeGuess),
                !canAfford);
        }
    }

    // --- being in the right place -----------------------------------------
    //
    // Arrival is a claim about the TILE, not about the journey. This is the
    // uo-offline site-discipline lesson: a bot that stops short and starts
    // chopping air hides its own failure.
    // Only for a life that HAS a work site to be away from. Without this gate
    // a mage, which gathers nothing, was permanently "not at work" and kept
    // generating a travel need toward a forest.
    if (cfg.profession && cfg.profession->gathers.empty()) {
        // nothing to travel to
    } else if (!obs.atWorkSite && !obs.atBank) {
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
