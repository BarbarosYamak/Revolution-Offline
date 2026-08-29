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
        case NeedKind::NeedSupplies:  return "NeedSupplies";
        case NeedKind::NeedPractice:  return "NeedPractice";
        case NeedKind::NeedSpells:    return "NeedSpells";
        case NeedKind::NeedMakeBandages: return "NeedMakeBandages";
        case NeedKind::NeedGear:      return "NeedGear";
        case NeedKind::NeedCraft:     return "NeedCraft";
        case NeedKind::Count:         break;
    }
    return "?";
}

namespace {

// HOW FULL IS FULL ENOUGH, for the purpose of wanting more.
//
// Not 64 (the whole eight circles). A book is filled across sessions, and
// circles 7-8 are sold by nobody on this shard -- they come from dungeon
// chests and monster loot -- so a need that is only satisfied at 64 would
// never switch off and would nag forever. 24 is the first three circles, which
// IS buyable: circles 1-4 come random from any mage shop and a scribe sells
// 1-5 by name. It is the point where a caster has a working kit rather than a
// complete one. PLACEHOLDER-ish: no Revolution source states a number, so this
// is a judgement about bot behaviour, not a claim about the shard.
constexpr int kSpellbookComfortable = 24;
// How much spare gold, ABOVE this life's own reserve, counts as "the economy
// is good enough" to go spell shopping in earnest. At this much clear, the
// need is at full strength; below it, proportionally less.
constexpr i32 kSpellShoppingSpare = 1000;
// Enough gold that buying bandages is the sensible move. Healers sell them at
// {5 20} and vets at {6 66} (tm_vend.scp), so a couple of hundred coins buys a
// fighting stock outright -- and walking to a shop beats shearing a sheep,
// spinning it, weaving it and cutting it up.
constexpr i32 kBandagesBuyable = 200;


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

// HOW A SKILL IS ACTUALLY RAISED.
//
// Paying a guildmaster (NeedSkillTraining) buys tenths up to 30.0 and stops.
// Everything above that is PRACTICE -- the hours a character puts in doing the
// thing: a mage casts, a warrior goes to a graveyard and fights, a healer
// bandages, a scribe writes. Those are different activities with different
// preconditions, and the code used to gate all of them on combat, so a scribe
// short of Inscription logged "nothing is here to practise combat on" and a
// mage could never practise Magery at all.
enum class PracticeBy : u8 {
    Fighting,   // needs a foe, or somewhere to find one
    Casting,    // needs mana, and reagents for most spells
    SelfUse,    // just use the skill: Meditation, Hiding, Stealth
    Working,    // raised by the profession's own work goals, not separately
};

PracticeBy HowToPractise(int skillId) {
    switch (skillId) {
        case rules::kSwordsmanship:
        case rules::kMaceFighting:
        case rules::kFencing:
        case rules::kArchery:
        case rules::kWrestling:
        case rules::kTactics:
        case rules::kAnatomy:
        case rules::kParrying:
            return PracticeBy::Fighting;
        case rules::kMagery:
        case rules::kEvaluatingIntel:
            return PracticeBy::Casting;
        case rules::kMeditation:
            // Hiding and Stealth belong here too; this build's rules.h
            // does not name them yet, so they fall to Working rather
            // than be invented. UNKNOWN, not omitted.
            return PracticeBy::SelfUse;
        default:
            // Inscription, Blacksmithing, Tailoring, Lumberjacking, Mining,
            // Fishing, Cooking ... all rise from the work the life already
            // does. They need no separate practice errand, and inventing one
            // would have the character craft for its own sake rather than to
            // sell.
            return PracticeBy::Working;
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
    // TWO DIFFERENT THINGS, and only one of them used to be here: being
    // hungry NOW, and having nothing to eat later. The old need fired only on
    // an empty pack, so a character with bread in its bag was never told to
    // eat it -- which did not matter, because NeedFood was wired to no goal at
    // all and fell into a void every tick.
    if (cfg.hungerLive) {
        if (obs.starving) {
            add(NeedKind::NeedFood, 0.85, "food",
                "the server says STARVING -- hunger damage is imminent",
                Fmt("food=%d starving=1", obs.food));
        } else if (obs.hungry) {
            add(NeedKind::NeedFood, 0.45, "food",
                "the server says hungry; eat before it costs health",
                Fmt("food=%d hungry=1", obs.food));
        } else if (obs.food < cfg.foodLow) {
            add(NeedKind::NeedFood, 0.25, "food",
                "carrying no food and hunger is enabled on this shard",
                Fmt("food=%d", obs.food));
        }
    }

    // --- weight and banking ------------------------------------------------
    const double weightFrac = obs.WeightFraction();

    // IS THE LOAD ITSELF THE INCOME? A character at its carry limit holding
    // fifteen fish has two ways to put the weight down, and only one of them
    // pays. Ranking the bank above the buyer produced a perfect oscillation:
    // walk eighty tiles to the bank, deposit the catch, EARN_GOLD withdraws it
    // two seconds later, weight is back at the cap, bank wins again. Six round
    // trips in one session and not one fish sold.
    //
    // Only suppresses banking when there is somewhere to actually take it:
    // a load with no buyer is exactly what the bank is for.
    bool loadIsSellable = false;
    if (cfg.profession) {
        const std::vector<market::Offer> onHand =
            market::Surplus(*cfg.profession, obs.pack, market::TradePolicy{});
        for (const market::Offer& o : onHand) {
            if (market::HasNpcBuyer(o.item.c_str()) ||
                mem.BestSupplier((std::string("buyer:") + o.item).c_str())) {
                loadIsSellable = true;
                break;
            }
        }
    }

    if (obs.overloaded) {
        // Already spilling onto the ground. Nothing else matters about the
        // pack: every further log is dropped where anyone can take it.
        add(NeedKind::NeedBank, 0.95, "deposit carried load",
            "the pack has overflowed and logs are going on the floor",
            Fmt("server said 'it is too heavy'; weight=%d/%d logs=%d",
                obs.weight, obs.maxWeight, obs.logs));
    } else if (weightFrac >= cfg.bankWeightFrac && !loadIsSellable) {
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
                add(NeedKind::NeedTrade, obs.marketQuiet ? 0.0 : urgency,
                    "sell to a player",
                    obs.marketQuiet
                        ? "carrying goods only a player would buy, and the "
                          "market was just tried and found empty"
                        : "carrying goods no vendor will take, which is what "
                          "the player market is for",
                    Fmt("%d x %s spare", biggest, spare.front().item.c_str()),
                    obs.marketQuiet);
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

    // --- making things -----------------------------------------------------
    //
    // A crafter's day is two errands, not one: fetch what it cannot make, then
    // make what it can sell. Split because they fail differently and a bot
    // that says "I cannot craft" when it means "nobody has sold me nightshade
    // yet" is lying about its own state.
    if (cfg.profession) {
        // TWO QUESTIONS, NOT ONE.
        //
        //   "can I make one right now?"   -> batch of 1
        //   "am I stocked for a sitting?" -> the full batch
        //
        // Asking only the second made a scribe buy blank scrolls forever.
        // NeedSupplies outranked NeedCraft by 0.02, so every time she held
        // fewer than five she went shopping instead of writing -- and since
        // buying never reduced the shortfall of the NEXT batch, she bought
        // until the purse fell from 781 gold to 92 without inscribing a
        // single scroll. Working stock is for working with.
        const CraftIntent now = ChooseCraft(*cfg.profession, obs, 1);
        const CraftIntent craft =
            now.item && now.missing.empty()
                ? now
                : ChooseCraft(*cfg.profession, obs, cfg.craftBatch);
        if (craft.item && craft.skillsMet) {
            if (craft.missing.empty()) {
                add(NeedKind::NeedCraft, 0.50, "make goods to sell",
                    "holds every input for something this life can legitimately "
                    "sell to an NPC",
                    Fmt("%s: %s", craft.item, craft.why));
            } else {
                // Can the shortfall actually be bought? A missing input with
                // no seller is a blocked state, not an errand -- and saying
                // otherwise is how a goal wins the scoring and then discovers
                // on entry that there was never anywhere to go.
                const prod::Ingredient& first = craft.missing.front();
                const econ::VendorRuling ruling =
                    econ::CanUseNPCVendorFor(first.item);
                // BELOW NeedCraft (0.50) and below selling. Shopping is what
                // a crafter does when it cannot work, never instead of
                // working: this used to be 0.52, which put the shop ahead of
                // the workbench permanently.
                // NO WORKING CAPITAL, NO SHOPPING TRIP.
                //
                // BUY_SUPPLIES spends everything above a hard floor of 100
                // gold. Below that there is nothing to spend, and a scribe
                // with 92 gold, no reagents and four finished scrolls in its
                // pack sat in the mage shop choosing this errand over and
                // over -- outranking the sale that was the only way out of
                // it. Selling is what pays for the next batch.
                const bool noCapital = (obs.gold - 100) <= 0;
                add(NeedKind::NeedSupplies,
                    (ruling.allowed && !noCapital) ? 0.44 : 0.0,
                    "buy craft inputs",
                    !ruling.allowed
                        ? "short of an input no NPC may legitimately sell it"
                        : (noCapital
                               ? "short of inputs AND of the gold to buy them "
                                 "-- what it has made has to be sold first"
                               : "short of what it needs to make its own goods"),
                    Fmt("%s needs %d x %s%s", craft.item, first.qty, first.item,
                        !ruling.allowed ? " -- and the vendor policy refuses "
                                          "that purchase"
                                        : (noCapital ? " -- and the purse is empty"
                                                     : "")),
                    !ruling.allowed || noCapital);
            }
        } else if (craft.item == nullptr && !cfg.profession->produces.empty()) {
            // Nothing sellable this life can make. Legible, and not a loop.
            add(NeedKind::NeedCraft, 0.0, "make goods to sell", craft.why,
                "nothing to make", true);
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
        // NOTHING HERE IS NOT THE SAME AS NOWHERE TO GO.
        //
        // This gate exists because completing TRAIN_COMBAT instantly with no
        // enemy present made the planner spin. But it blocked the need
        // outright, so a character that could have WALKED to a fight never
        // did -- which is the whole reason M6 has never run live. A fighter
        // with somewhere to hunt has an actionable need; a lumberjack does
        // not, and still waits for trouble to find it.
        // WHICH KIND OF PRACTICE THIS SKILL EVEN IS.
        //
        // Everything below used to assume "practise" meant "fight", so a
        // scribe short of Inscription was told there was nothing here to
        // practise combat on, and a mage could never practise Magery at all.
        // A skill raised by the profession's own work needs no errand of its
        // own -- Inscription rises from writing scrolls to sell, which is
        // already the money-making goal.
        const PracticeBy how = HowToPractise(t.skillId);
        if (how == PracticeBy::Working) continue;

        if (how != PracticeBy::Fighting) {
            // Casting needs mana; self-use needs nothing but the character.
            // Neither needs a foe, and neither may be blocked for want of one.
            // A REGION THAT REFUSES SKILL GAIN MAKES PRACTICE POINTLESS.
            // Not dangerous, not blocked by the server -- simply wasted. The
            // character must move before it is worth a single cast.
            const bool canGain = !obs.inNoGainRegion;
            const bool ready = canGain &&
                               ((how == PracticeBy::SelfUse) || obs.mana >= 10);
            add(NeedKind::NeedPractice, 0.20 + 0.25 * gap, SkillName(t.skillId),
                ready ? "below target, and this skill is raised by using it"
                      : (!canGain
                             ? "below target, but no skill advances in this "
                               "region -- move somewhere ordinary first"
                             : "below target, but there is not enough mana to cast"),
                Fmt("%s %.1f -> %.1f mana=%d no_gain_region=%d",
                    SkillName(t.skillId), have / 10.0, t.tenths / 10.0,
                    obs.mana, obs.inNoGainRegion ? 1 : 0),
                !ready);
            continue;
        }

        const bool nothingHere = obs.attackersOnMe == 0;
        // WHEN RESTING CANNOT HELP, THE 80% BAR IS A TRAP.
        //
        // Kaelen spent a whole session inside this deadlock. Hungry, so no
        // HP regeneration; wounded, so under the bar; no bandages, so HEAL
        // was blocked; no gold, so REPLACE_EQUIPMENT and GET_FOOD both stood
        // down. He climbed from 10/32 to 25/32 -- 78%, two points short --
        // and idled for 73% of his picks while every other need reported
        // BLOCKED. The exits were all locked behind each other:
        //
        //   hungry -> no regen -> under 80% -> cannot hunt -> cannot earn
        //          -> cannot buy food -> hungry
        //
        // Hunting is the only door out of that, because loot is the only
        // thing a broke fighter can turn into gold. So a character who cannot
        // heal, cannot eat and cannot buy may hunt from half health: waiting
        // is not caution when nothing is coming.
        //
        // Deliberately narrow. It needs ALL of no bandages, no money and
        // hunger -- a fighter with any of the three still waits for 80%,
        // because for them resting genuinely does work.
        const bool outOfOptions =
            obs.bandages <= 0 && obs.gold < cfg.goldFloor && obs.hungry;
        const int huntHpPct = outOfOptions ? 50 : 80;
        const bool couldGoHunting =
            nothingHere && cfg.profession && WantsToHunt(*cfg.profession) &&
            obs.hp * 100 >= obs.hpMax * huntHpPct && obs.WeightFraction() < 0.7;
        const bool blocked = nothingHere && !couldGoHunting;
        add(NeedKind::NeedTraining, 0.15 + 0.25 * gap, SkillName(t.skillId),
            couldGoHunting
                ? (outOfOptions
                       ? "below target -- and with no bandages, no money and an "
                         "empty stomach, hunting is the only way out, so go at "
                         "whatever health there is"
                       : "below target, and there is a graveyard to go and "
                         "practise in")
                : (nothingHere
                       ? "below target, but nothing is here to practise combat on"
                       : "below the target build value for this skill"),
            Fmt("%s %.1f -> %.1f", SkillName(t.skillId), have / 10.0,
                t.tenths / 10.0),
            blocked);
    }

    // --- bandages this character must MAKE ---------------------------------
    //
    // "if warrior economy is good then he can buy bandage and potion,
    // otherwise go get yourself wool make bandage" (project owner,
    // 2026-08-29). So this is strictly the poor branch. A character who can
    // pay for bandages should walk to a healer and pay -- NeedEquipment
    // already asks for that -- and this need must not compete with it.
    //
    // It exists because of a real deadlock. Kaelen was hungry so did not
    // regenerate, wounded so could not hunt, out of bandages so could not
    // heal, and broke so could not buy any: every exit locked behind another.
    // Bandages ARE sold here (VENDOR_S_HEALER_SHOP, VENDOR_S_VET) and none of
    // that helps an empty purse. A sheep costs nothing.
    //
    // Only for lives that actually fight. A scribe with no bandages is not in
    // danger; a fencer is.
    if (cfg.profession && WantsToHunt(*cfg.profession) &&
        obs.bandages < cfg.bandageLow) {
        const bool canAffordToBuy = obs.gold >= kBandagesBuyable;
        const double shortfall =
            1.0 - static_cast<double>(obs.bandages) /
                      static_cast<double>(cfg.bandageLow > 0 ? cfg.bandageLow : 1);
        add(NeedKind::NeedMakeBandages, 0.25 + 0.45 * shortfall, "bandages",
            canAffordToBuy
                ? "short of bandages, but there is money to buy them -- a shop "
                  "is faster than a sheep"
                : "no bandages and no money for any: shear, spin, weave, cut",
            Fmt("bandages %d/%d gold %d (buyable at %d)", obs.bandages,
                cfg.bandageLow, obs.gold, kBandagesBuyable),
            canAffordToBuy);
    }

    // --- gear worth wearing -------------------------------------------------
    //
    // "bots also always check for gear" (project owner). Standing, not
    // one-off: loot arrives in the pack all life long, and a piece that beats
    // what is worn is free armour nobody was looking at.
    //
    // Modest urgency, because this is an improvement rather than a
    // predicament -- it must never outrank eating or bandages. The goal
    // itself decides what is legal for the class and what the character is
    // strong enough for; the need only says "look".
    if (cfg.profession) {
        add(NeedKind::NeedGear, 0.22, "gear",
            "loot and shops both hold better armour than this character is "
            "wearing, and nothing checks unless this asks",
            Fmt("str %d gold %d reserve %d", obs.str, obs.gold,
                cfg.profession->goldReserve),
            false);
    }

    // --- a spellbook worth FILLING -----------------------------------------
    //
    // "we need to add that mages tries to fill their book, make it full spell
    // book" (project owner, 2026-08-29). A mage's book is equipment, and this
    // shard proved why it matters the hard way: Voris carried Magery 50.0 and
    // asked for Create Food 26 times in one session, being told every time
    // that the spell was not in his spellbook. Skill without spells is not a
    // caster.
    //
    // Deliberately a LOW, PATIENT need. A book is filled across many sessions,
    // never in one errand -- circles 1-4 are bought random from a mage shop,
    // 5 and part of 6 by name from a scribe, and 7-8 come from dungeon chests
    // and monster loot and are sold by nobody. See
    // docs/REVOLUTION_GAMEPLAY_TRUTH.md 3.5. Urgency rises as the book fills
    // is exactly wrong; it FALLS, because the cheap spells go in first and
    // what is left gets progressively harder to obtain.
    if (cfg.profession && obs.SkillTenths(rules::kMagery) > 0) {
        // The first circle is eight spells, and a caster with none of them is
        // in a different situation from one with a working core.
        const int target = kSpellbookComfortable;
        if (obs.spellsKnown < target) {
            const double shortfall =
                static_cast<double>(target - obs.spellsKnown) /
                static_cast<double>(target);
            const bool noBook = (obs.spellbookSerial == 0);

            // A FULL PURSE IS A REASON TO GO SHOPPING FOR SPELLS.
            //
            // "mage should also give priority to buy new spells not on the
            // book if economy is good enough" (project owner, 2026-08-29).
            // Which is how a player behaves: scrolls are the first thing spare
            // gold goes on, because every one of them is a permanent increase
            // in what the character can do, unlike food or reagents which are
            // spent again.
            //
            // Measured against the profession's OWN reserve, not a flat
            // number -- a mage holds back 800 for reagents where a lumberjack
            // holds 300 for a trainer -- so "good enough" means good enough
            // for THIS life. Nothing is added until the reserve is intact, so
            // this can never pull gold out from under the running costs.
            const i32 reserve = cfg.profession->goldReserve;
            const i32 spare = obs.gold - reserve;
            const double wealth =
                spare <= 0 ? 0.0
                           : std::min(1.0, static_cast<double>(spare) /
                                               static_cast<double>(kSpellShoppingSpare));
            const double urgency = 0.10 + 0.25 * shortfall + 0.35 * wealth;

            add(NeedKind::NeedSpells, urgency, "spells",
                noBook ? "no spellbook at all -- a mage that cannot cast "
                         "anything needs one before it needs anything else"
                       : (wealth > 0.5
                              ? "the purse is well clear of the reserve, and "
                                "spells are what spare gold buys first"
                              : "the book is short of the spells this life "
                                "will cast"),
                Fmt("spells %d/%d book=%s magery %.1f gold %d reserve %d "
                    "spare %d wealth %.2f",
                    obs.spellsKnown, target, noBook ? "none" : "carried",
                    obs.SkillTenths(rules::kMagery) / 10.0, obs.gold, reserve,
                    spare, wealth),
                false);
        }
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
            // ONCE IT IS AFFORDABLE, GO AND BUY IT.
            //
            // At a flat 0.30 this scored 60 against NeedSupplies' 61.6 and
            // lost every single tick, so a crafter reinvested its whole
            // surplus in reagents the moment it had one and never once walked
            // to the trainer. Ysolde sold eleven poison scrolls for 275 gold,
            // touched 321 -- past the fee -- and was back under 260 before the
            // next decision was taken (run_m5/p0gate6). She would have done
            // that forever.
            //
            // A player who has saved up for a skill stops buying stock and
            // goes and gets it, then returns to work. The weight comment on
            // TrainAtNpc already states this reasoning ("the gold is already
            // saved by the time the need fires"); the urgency simply never
            // reflected it. Below the fee it stays low -- saving is not
            // urgent, it is just saving.
            const double urgency = have <= 0 ? 0.55 : (canAfford ? 0.45 : 0.30);
            add(NeedKind::NeedSkillTraining,
                urgency,
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
