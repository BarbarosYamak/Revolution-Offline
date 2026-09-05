#include "uo/life.h"

#include "uo/builders.h"
#include "uo/spellcast.h"
#include "uo/vendor_policy.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

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
        case NeedKind::NeedOre:       return "NeedOre";
        case NeedKind::NeedPet:       return "NeedPet";
        case NeedKind::NeedSmelt:     return "NeedSmelt";
        case NeedKind::NeedCraft:     return "NeedCraft";
        case NeedKind::NeedCloth:     return "NeedCloth";
        case NeedKind::NeedMount:     return "NeedMount";
        case NeedKind::NeedWoolIncome: return "NeedWoolIncome";
        case NeedKind::NeedStrength:  return "NeedStrength";
        case NeedKind::Count:         break;
    }
    return "?";
}

// ---------------------------------------------------------------------------
// STAT FARMING. See the block comment on AssessStatFarm's declaration in
// life.h for the four shard facts this arithmetic rests on.
// ---------------------------------------------------------------------------

// life.h spells the lock states as plain numbers so it stays free of the
// packet layer. This is the seam where the two are held together.
static_assert(kStatLockUp     == build::kLockUp,     "lock state drift");
static_assert(kStatLockDown   == build::kLockDown,   "lock state drift");
static_assert(kStatLockLocked == build::kLockLocked, "lock state drift");

u8 StatFarmDexLockOnExit(const BuildPlan& plan, const Observation& obs) {
    return obs.dex >= plan.targetDex ? kStatLockLocked : kStatLockUp;
}

i32 PlanStrCeiling(const BuildPlan& plan) {
    i32 best = 0;
    for (const SkillTarget& t : plan.skills) {
        const int s = rules::SkillStatStr(t.skillId);
        if (s > best) best = static_cast<i32>(s);
    }
    return best;
}

StatFarmPlan AssessStatFarm(const BuildPlan& plan, const Observation& obs) {
    StatFarmPlan p;
    p.have   = obs.str;
    p.target = plan.targetStr;
    p.wrestlingTenths = obs.SkillTenths(rules::kWrestling);
    p.useDummy = p.wrestlingTenths < kDummyPracticeMaxTenths;

    // A plan with no named skills is an M4-era plan or a blank; it says
    // nothing about a ceiling, and inventing one from silence would send a
    // character punching things for a target nobody set.
    if (plan.skills.empty()) {
        p.why = "this plan names no skills, so it says nothing about a ceiling";
        return p;
    }
    if (plan.targetStr <= 0) {
        p.why = "no STR target in this plan";
        return p;
    }
    p.ceiling = PlanStrCeiling(plan);
    if (obs.str >= plan.targetStr) {
        p.why = "STR is already at the build's target";
        return p;
    }

    // THE ORDER OF THESE TWO TESTS IS THE WHOLE DESIGN.
    //
    // Wanting STR is not a reason to stop working. A lumberjack's own axe
    // carries him to STR 85 (Lumberjacking STAT_STR=85) and a smith's hammer
    // to 95, so while his own work still moves the number, chopping IS the
    // stat training and a Wrestling detour would be a swordsman dropping his
    // sword to punch a rabbit. Only once the character has run out the
    // ceiling its own plan offers is the detour the only road left.
    if (obs.str < p.ceiling) {
        p.why = "this life's own skills still raise STR";
        return p;
    }

    p.wanted = true;
    // 0.20 .. 0.50. A detour, not the work: at the top end it sits beside
    // practising a skill (NeedPractice caps at 0.45) rather than beside a
    // fighter at a graveyard (0.65), so a life that has something productive
    // to do does it, and a life that is blocked goes and gets stronger.
    const double span = static_cast<double>(plan.targetStr - p.ceiling);
    const double shortfall = static_cast<double>(plan.targetStr - obs.str);
    const double frac = span > 0.0 ? (shortfall / span) : 1.0;
    p.urgency = 0.20 + 0.30 * (frac < 0.0 ? 0.0 : (frac > 1.0 ? 1.0 : frac));
    p.why = p.useDummy
                ? "own skills are spent at their STR ceiling; a dummy will "
                  "still take Wrestling swings"
                : "own skills are spent at their STR ceiling; Wrestling is "
                  "past the dummy's limit, so it takes a live opponent";
    return p;
}

// See the declaration in life.h for why i_thread is not on this list.
bool IsWoolChainMaterial(const char* item) {
    if (item == nullptr) return false;
    static const char* const kChain[] = {
        "i_wool", "i_yarn_ball", "i_cloth_bolt", "i_cloth",
    };
    for (const char* c : kChain) {
        if (std::strcmp(item, c) == 0) return true;
    }
    return false;
}

// See the declaration in life.h for the live failure this exists to stop.
bool WoolChainWorkInProgress(const prof::Profession& p,
                             const std::vector<market::Stock>& holdings,
                             i32 craftBatch, const char* item) {
    if (item == nullptr || !IsWoolChainMaterial(item)) return false;
    // Cut cloth is the finished material of the chain, not a step in it.
    if (std::strcmp(item, "i_cloth") == 0) return false;

    // A LIFE THAT SELLS CLOTH IS MID-CHAIN UNTIL IT IS CLOTH. The fighters
    // list i_cloth in `produces` (owner ruling 2026-09-02: they shear, kill,
    // carve, spin, weave and cut, and sell the cloth to tailors) and the
    // intermediates alongside it so the pack can count them; wool, yarn and
    // a bolt in their pack are a load half-processed, never surplus and never
    // a bank deposit.
    for (const std::string& made : p.produces)
        if (made == "i_cloth") return true;

    // HOW MUCH CLOTH A SITTING OF THIS LIFE'S OWN RECIPES WOULD EAT, read off
    // the recipe graph rather than from a profession flag -- a full_crafter
    // sewing a shirt gets the same answer as a tailor, and a smith gets zero
    // because nothing it makes asks for cloth. Zero means this life does not
    // sew at all, so a bolt in its pack really is just goods.
    i32 perSitting = 0;
    for (const std::string& made : p.produces) {
        const prod::Recipe* r = prod::FindRecipe(made.c_str());
        if (r == nullptr) continue;
        for (const prod::Ingredient& in : r->inputs) {
            if (in.item && std::strcmp(in.item, "i_cloth") == 0)
                perSitting = std::max(perSitting, in.qty);
        }
    }
    if (perSitting <= 0) return false;
    perSitting *= std::max(1, craftBatch);

    i32 cloth = 0;
    for (const market::Stock& s : holdings) {
        if (s.item == "i_cloth") { cloth = s.qty; break; }
    }
    return cloth < perSitting;
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
// Working change kept on top of the profession's own reserve. Enough for an
// unplanned purchase without making the character worth robbing.
// HOW MUCH A LIFE WALKS AROUND WITH, whatever its savings target.
//
// `goldReserve` is what a profession wants to HAVE -- a scribe 5000, a
// lumberjack-swordsman 10000 -- and it was being added straight to the amount
// carried, so those two never deposited anything and wandered a full-loot
// shard with five and ten thousand coins in their pockets. That is precisely
// what the owner's rule forbids: "nobody carry gold on them unless they need
// to buy something -- always put additional items to bank, so they can get it
// when they need it".
//
// Carrying is now capped for everyone alike, and the cap is safe because the
// other half of the rule finally works: FetchCoinForPurchase withdraws what an
// errand costs, when the errand happens. "money bank deposit like blacksmith
// as well we can generalize" (project owner, 2026-08-30).
constexpr i32 kMaxGoldCarried = 800;
constexpr i32 kGoldWorthCarrying = 500;



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

// Every stock whose name starts with `prefix`, summed. Ore is one graphic
// for sixteen metals and the pack has been hue-resolved since S1, so a
// coloured vein sits in obs.pack as i_ore_rusty / i_ore_bronze / ... --
// not under "i_ore_iron". Anything that means "ore, whatever metal" has to
// ask by prefix or it only sees the iron.
i32 QtyInByPrefix(const std::vector<market::Stock>& pack, const char* prefix) {
    const usize n = std::strlen(prefix);
    i32 total = 0;
    for (const market::Stock& s : pack) {
        if (s.item.compare(0, n, prefix) == 0) total += s.qty;
    }
    return total;
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

// Does this life carry the thing at all? The catalogue is the answer; a life
// saved before the catalogue existed (cfg.profession == nullptr) keeps the M4
// lumberjack answers, so nothing that already works changes.
//
// PUBLIC, not TU-local. The ERRAND has to be able to ask the same question
// the NEED asks. The bandage need is gated on WantsConsumable(cfg,
// "bandage") and DoReplaceEquipment was not, so a crafting life -- whose
// catalogue entry deliberately drops Bandages() for CrafterHealPotions()
// ("so crafter do not buy bandages", project owner 2026-08-30) -- still
// walked to the healer and bought thirty of them, and the heal-potion branch
// behind it never ran at all. ONE test, not a second copy. (audit finding 1)
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

// CAN THIS LIFE BREW ITS OWN HEAL POTION RIGHT NOW, SKILL-WISE?
//
// Owner ruling 2026-09-04: an alchemist "must NOT open by buying NPC heal
// potions". Asked as a question about the production graph rather than about a
// job title, so it answers itself for every profession: does this life's own
// `produces` name a heal potion, and does the skill it carries reach that
// recipe's gate (i_potion_heal is ALCHEMY 15.1, Production.cpp). Inputs are
// deliberately NOT part of the question -- an empty pack is BUY_SUPPLIES's
// errand, not a reason to go and buy the finished article.
// HOW MANY MORE OF `item` THE PACK STILL FUNDS. The bench's own count
// (Craft.cpp inputsAvailable): the WORST-stocked input bounds the run. Used
// to size "a load" for the sell errand -- the load is the whole sitting's
// output, held plus still-makeable -- so a sale trip does not interrupt a
// bench with material left on it. 0 when the item has no recipe or nothing
// funds it. Owner rule 2026-09-04 (crafters-stock-then-sit): Lyra bought 89
// blank scrolls, sat down for 77, and EARN_GOLD 76.5 took her off the bench
// at 18 because "a load is 20" (g_Lyra 12:46:00).
static i32 StillMakeable(const char* item, const std::vector<market::Stock>& pack) {
    const prod::Recipe* r = prod::FindRecipe(item);
    if (!r) return 0;
    i32 fits = 500;   // the shard's own .makelast cap (revolution_makelast.scp:59)
    bool anyInput = false;
    for (const prod::Ingredient& in : r->inputs) {
        if (!in.item || in.qty <= 0) continue;
        anyInput = true;
        fits = std::min(fits, market::QtyOf(pack, in.item) / in.qty);
    }
    return anyInput ? std::max(0, fits) : 0;
}

static bool BrewsOwnHealPotion(const prof::Profession& p,
                               const Observation& obs) {
    for (const std::string& made : p.produces) {
        if (made != "i_potion_heal" && made != "i_potion_healgreat") continue;
        const prod::Recipe* r = prod::FindRecipe(made.c_str());
        if (!r) continue;
        if (r->skillId < 0) return true;
        if (obs.SkillTenths(r->skillId) >= r->skillTenths) return true;
    }
    return false;
}

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
                false);
        }
        return needs;   // a ghost has no other needs it can act on
    }

    // --- IS THE SHOP OPEN YET? ---------------------------------------------
    //
    // Owner ruling, 2026-09-04: an alchemist opens her life by buying a LOT of
    // nightshade and empty bottles from the NPC alchemist and brewing poison --
    // the brew batch is BOTH the training and the stock to sell -- and "she
    // must NOT open by buying NPC heal potions".
    //
    // Written as a rule the whole catalogue can carry rather than as a goal
    // order. A life whose FIRST income is Craft, that cannot make a single one
    // of its own goods right now, and whose shortfall is something a shopkeeper
    // may legitimately sell it, is a shop with empty shelves: no stock to sell,
    // no bench work to gain a skill on, and nothing to spend the horse money on
    // afterwards. Everything else on its list -- the horse, a scroll for the
    // book, a potion off a healer's shelf -- is the upkeep of a WORKING life,
    // and this one has not started working. So it is not that BUY_SUPPLIES is
    // pinned to the front of the day; it is that the comforts stop outbidding
    // the trade until the trade exists.
    //
    // THE GATE IS THE SHORTFALL, NOT THE JOB TITLE. A lumberjack's missing
    // input is logs and a tailor's is cloth, and the vendor policy refuses both
    // (data/revolution_vendor_policy.tsv) -- so those lives never see this and
    // their gathering errands are untouched. Capital is required for the same
    // reason BUY_SUPPLIES asks for it below: with nothing to spend, shopping is
    // not the way out, selling is.
    bool unstockedCrafter = false;
    if (cfg.profession && !cfg.profession->income.empty() &&
        cfg.profession->income.front() == prof::Income::Craft &&
        (obs.gold - 100) > 0) {
        // Batch of ONE: the question is "can I make anything at all", which is
        // the difference between an empty shop and a low shelf.
        const CraftIntent one =
            ChooseCraft(*cfg.profession, obs, 1, cfg.craftFocus);
        unstockedCrafter = one.item && one.skillsMet && !one.missing.empty() &&
                           econ::CanBuyFromNPC(one.missing.front().item).allowed;
    }

    // Resurrection is only the first half of a full-loot death.  Keep the
    // corpse need alive after the ghost becomes corporeal so RecoverCorpse
    // can heal to its safety threshold, return to the recorded tile, loot,
    // and re-equip.  Previously this need existed only inside the `obs.dead`
    // branch above and vanished on the exact tick it became actionable.
    if (obs.corpseKnown) {
        const bool exhausted = obs.corpseRecoveryAttempts >= 3;
        add(NeedKind::RecoverCorpse, exhausted ? 0.2 : 0.75, "own corpse",
            exhausted ? "corpse recovery attempts exhausted"
                      : "resurrected; gear and carried resources remain on the corpse",
            Fmt("corpse=%d,%d attempts=%d", obs.corpseX, obs.corpseY,
                obs.corpseRecoveryAttempts),
            false);  // let recovery execute its terminal abandonment decision
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
            add(NeedKind::StayAlive, 0.9, "a fight already started",
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
            false);  // potions, spells, food and regeneration are also healing
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
    } else if (cfg.profession && WantsToHunt(*cfg.profession) &&
               !WantsSpellCombat(*cfg.profession) && !obs.weaponEquipped) {
        // A FIGHTER WITH EMPTY HANDS. The buy side has existed since the
        // school-weapon table (Identity.cpp kSchoolWeapons, Gear.cpp
        // DoReplaceEquipment), but nothing ever ASKED for it: only hatchet
        // users got a weapon need. Titus, an archer stripped of his bow by a
        // death, had TRAIN_COMBAT hand off to REPLACE_EQUIPMENT with "no gear
        // yet -- shopping before the graveyard" and the planner then picked
        // TRADE_WITH_PLAYER, because no need said "weapon" (g_Titus
        // 2026-09-05 01:41). Ten minutes of IDLE_BRIEFLY followed.
        add(NeedKind::NeedEquipment, 0.7, "weapon",
            "a fighter with nothing in hand cannot hunt or defend itself",
            "weapon_worn=0");
    }

    // DRESSED. Cutting up the resurrection robe leaves a character standing
    // in a city in its underwear, and full loot leaves it there anyway.
    //
    // Low urgency on purpose -- being unclothed is embarrassing, not fatal,
    // so it must never outrank food, healing or a weapon. It only needs to be
    // ABOVE nothing, so that a character with no more pressing errand goes
    // and dresses instead of mining half naked.
    if (obs.clothingMissing > 0) {
        add(NeedKind::NeedEquipment, 0.28, "clothes",
            "not dressed -- shirt, trousers or shoes missing from both the "
            "body and the pack",
            Fmt("missing=%d of 3", obs.clothingMissing));
    }

    // A CRAFTER'S SELF-HEAL, which nothing ever asked for.
    //
    // "you are crafter you dont have heal skill so buy healing potion 3-4"
    // and "so crafter do not buy bandages" (project owner, 2026-08-30). The
    // bandage clause below is gated on WantsConsumable(cfg, "bandage"), which
    // is now false for every crafting life -- so without this they would ask
    // for nothing at all and walk the world with no way to heal.
    //
    // Sits at the same 0.5 as bandages: it is the same need wearing different
    // clothes. The potion is graded a BasicCraftTool-class purchase by the
    // vendor matrix, and the healer that sells it is the one the bandage
    // errand already walks to.
    // NOTE the profession check: WantsConsumable answers TRUE when there is no
    // profession at all (a life with no catalogue entry wants everything), so
    // reading ->consumables off the back of it is a null dereference. m4_life
    // runs exactly that way and segfaulted on the first build.
    if (cfg.profession && WantsConsumable(cfg, "heal potion")) {
        i32 low = 2;
        for (const prof::ConsumableNeed& c : cfg.profession->consumables)
            if (c.name == "heal potion") { low = c.low; break; }
        if (obs.healPotions < low) {
            // A BREWER DOES NOT OPEN HER LIFE AT SOMEONE ELSE'S COUNTER.
            //
            // Owner ruling 2026-09-04. Elara's first three goals on
            // 2026-09-03 were BUY_MOUNT, BANK, then REPLACE_EQUIPMENT buying
            // four NPC heal potions (progress=0) -- while carrying a mortar,
            // Alchemy 50.0 and i_potion_heal in her own `produces` at a gate
            // of 15.1. 0.5 x 260 = 130 was beating the shopping trip that
            // would have started her trade.
            //
            // 0.20 x 260 = 52 keeps the need VISIBLE and buyable on a quiet
            // day, under BUY_SUPPLIES (133 while unstocked) and under CRAFT
            // (65) -- so the answer becomes "brew some", which is what she is
            // for. Only ever damped for a life that can actually brew: every
            // fighter, gatherer and below-the-gate crafter keeps the old 0.5
            // exactly, which is the non-crafter upkeep path the brief protects.
            const bool brews = BrewsOwnHealPotion(*cfg.profession, obs);
            add(NeedKind::NeedEquipment, brews ? 0.20 : 0.5, "heal potions",
                brews ? "no Healing skill -- but this life BREWS heal potions "
                        "and carries the skill for it, so buying them off a "
                        "shelf is the wrong errand"
                      : "no Healing skill to make a bandage work -- a potion is "
                        "the only self-heal this life has",
                Fmt("potions=%d low=%d gold=%d brews_own=%d", obs.healPotions,
                    low, obs.gold, brews ? 1 : 0));
        }
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
        const econ::VendorRuling ruling = econ::CanBuyFromNPC("i_bandage");
        const bool noRoute = supplier == nullptr &&
                             (!ruling.allowed || obs.gold < 50);
        // A WOUND MAKES THIS URGENT, AND NOTHING SAID SO.
        //
        // Faustus logged in dead, was resurrected at 6/48, and carried no
        // bandages and 8,460 gold. HEAL is BLOCKED by construction with an
        // empty pack ("wounded with no bandages carried", ten assessments in
        // g_Faustus.console.txt:154-947), so the ONLY door out of a 12%-health
        // life is this errand -- and at a flat 0.50 it scored 130, below
        // BUY_MOUNT's 204, and he ping-ponged between RECOVER_CORPSE and HEAL
        // for the rest of the session without ever buying a bandage.
        //
        // Scaled by the wound, so a full-health character short of its floor
        // feels exactly what it felt before (0.50) and a bleeding one feels
        // 0.95 -- REPLACE_EQUIPMENT 247, above the horse, the food run and the
        // armour browse, and below the emergencies. Derived from health, not a
        // per-character number.
        double bandageUrgency = 0.5;
        if (hpFrac < cfg.healHpFraction && obs.healPotions <= 0) {
            const double wound = (cfg.healHpFraction - hpFrac) /
                                 (cfg.healHpFraction > 0 ? cfg.healHpFraction : 1.0);
            bandageUrgency = 0.5 + 0.45 * (wound < 1.0 ? wound : 1.0);
        }
        add(NeedKind::NeedEquipment, bandageUrgency, "bandages",
            (hpFrac < cfg.healHpFraction)
                ? "wounded with nothing to heal with -- bandages before "
                  "anything else money can buy"
                : "below the bandage floor; a fight without them is a death",
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
    // ...AND WHAT IS CARRIED THAT NOBODY WILL TAKE AT ALL. Distinct from the
    // above: `loadIsSellable` asks whether ANY of the load has a buyer, this
    // counts the part that has none.
    i32 unsoldStock = 0;
    std::string unsoldName;
    if (cfg.profession) {
        const std::vector<market::Offer> onHand =
            market::Surplus(*cfg.profession, obs.pack,
                            market::PolicyForPurse(obs.goldOnHand));
        for (const market::Offer& o : onHand) {
            // A BOLT ON THE WAY TO CLOTH IS NOT STOCK. See
            // WoolChainWorkInProgress in life.h -- without this the tailor's
            // first bolt counted as output nobody wanted and went in the box.
            if (WoolChainWorkInProgress(*cfg.profession, obs.pack,
                                        cfg.craftBatch, o.item.c_str()))
                continue;
            if (market::HasNpcBuyer(o.item.c_str()) ||
                mem.BestSupplier((std::string("buyer:") + o.item).c_str())) {
                loadIsSellable = true;
                continue;
            }
            if (o.qty > unsoldStock) {
                unsoldStock = o.qty;
                unsoldName = o.item;
            }
        }
    }

    // IS THE LOAD THE WORK ITSELF? A bolt of cloth weighs 50 stones, which is
    // most of a crafter's carry limit on its own, so weaving one is enough to
    // raise "deposit carried load" over every other need -- and the answer to
    // "I am carrying a heavy bolt" is the pair of scissors already in the
    // pack, not a walk to the bank. Same shape as `loadIsSellable` above: the
    // weight clause stands down only while there is a nearer thing to do with
    // the load. Overload is exempt (items are hitting the floor), and the
    // moment the bolt becomes cloth this is false again, so the trip that
    // secures the finished material still happens.
    bool loadIsWorkInProgress = false;
    std::string wipName;
    if (cfg.profession) {
        for (const market::Stock& s : obs.pack) {
            if (s.qty <= 0) continue;
            if (!WoolChainWorkInProgress(*cfg.profession, obs.pack,
                                         cfg.craftBatch, s.item.c_str()))
                continue;
            loadIsWorkInProgress = true;
            wipName = s.item;
            break;
        }
    }

    if (obs.huntReturnPending) {
        add(NeedKind::NeedBank, 1.0, "secure hunt loot",
            "finish the hunt by putting surplus in the bank", "loot received from confirmed corpse");
    } else if (obs.overloaded) {
        // Already spilling onto the ground. Nothing else matters about the
        // pack: every further log is dropped where anyone can take it.
        add(NeedKind::NeedBank, 0.95, "deposit carried load",
            "the pack has overflowed and logs are going on the floor",
            Fmt("server said 'it is too heavy'; weight=%d/%d logs=%d",
                obs.weight, obs.maxWeight, obs.logs));
    } else if (weightFrac >= cfg.bankWeightFrac && !loadIsSellable) {
        add(NeedKind::NeedBank,
            loadIsWorkInProgress ? 0.0
                                 : 0.6 + (weightFrac - cfg.bankWeightFrac),
            "deposit carried load",
            loadIsWorkInProgress
                ? "close to the carry limit, but the weight IS the unfinished "
                  "work -- it gets worked, not stored"
                : "close to the carry limit; further gathering is wasted",
            Fmt("weight=%d/%d (%.0f%%) logs=%d%s%s", obs.weight, obs.maxWeight,
                weightFrac * 100.0, obs.logs,
                loadIsWorkInProgress ? " work_in_progress=" : "",
                loadIsWorkInProgress ? wipName.c_str() : ""),
            loadIsWorkInProgress);
    } else if (obs.logs >= cfg.logsWorthBanking && !SellableInstead(cfg)) {
        add(NeedKind::NeedBank, 0.35, "deposit logs",
            "enough logs carried to be worth securing",
            Fmt("logs=%d threshold=%d", obs.logs, cfg.logsWorthBanking));
    } else if (unsoldStock > 0) {
        // STOCK NOBODY WILL BUY *YET* WAITS IN THE BOX.
        //
        // "until they have orders they keep other ingots in the bank"
        // (project owner, 2026-08-30). A smith mines special ore, smelts it,
        // and makes whole sets to an order placed by another bot -- and until
        // an order exists that output has nowhere to go.
        //
        // This is deliberately NOT the case SellableInstead guards against.
        // That rule says a life which produces something has a market for it
        // "by definition: an NPC buys it, or a player does", and refuses to
        // let a lumberjack bank logs it means to sell. Both halves of that
        // definition are FALSE for what `unsoldStock` counts: no NPC buys it
        // (HasNpcBuyer) and no player buyer is known (BestSupplier).
        //
        // AND IT MUST NOT DEPEND ON HAVING TRIED THE MARKET THIS SESSION. The
        // first version also required obs.marketQuiet, which is set only by an
        // actual trade attempt and is not persisted -- so Corwyn, sitting on
        // 9,938 gold, never scored NeedTrade highly enough to try, never set
        // the flag, and carried the same seventeen ingots through a fourth
        // session. A condition reachable only from the state it is meant to
        // resolve is not a condition.
        //
        // 0.40 is deliberately below NeedTrade's live urgency (0.49 when
        // carrying spare goods), so a character that CAN still announce its
        // stock does that first and only banks what the market ignored. And
        // below the weight clauses: a full pack is a more urgent reason to
        // visit a box than a tidy one.
        //
        // BUT A LOAD IS A LOAD. Elvar (2026-09-02) mined and smelted for a
        // whole session with 27 ingots and 5 ore riding in his pack: EARN_GOLD
        // and TRADE_WITH_PLAYER were both blocked (no buyer known, then on
        // cooldown), so 0.40 lost to the ore need (0.59) every cycle and the
        // surplus was never deposited -- on a full-loot shard, that is the
        // whole session's output walking around waiting to be taken. Once the
        // spare stock reaches surplusWorthTrip -- the same "this is a load
        // now" judgement the selling side uses -- the trip wins over gathering
        // more, exactly as the weight clause above does.
        const bool loadNow = unsoldStock >= cfg.surplusWorthTrip;
        add(NeedKind::NeedBank, loadNow ? 0.60 : 0.40, "put unsold stock away",
            loadNow
                ? "carrying a full load of output nobody is known to want -- "
                  "secure it before gathering more"
                : "carrying output no NPC buys and no player is known to want "
                  "-- it waits in the box until there is an order for it",
            Fmt("%d x %s spare with no buyer (trip at %d)", unsoldStock,
                unsoldName.c_str(), cfg.surplusWorthTrip));
    }

    // A BANK TRIP IS ALSO FOR TAKING MONEY OUT. Depositing was implemented
    // and withdrawing was not, so a character banked everything and then stood
    // in front of a shop it could not pay -- a smith hammer it had 9,785 gold
    // for, and a 196 gold lesson from Olin.
    if (obs.coinWanted > obs.goldOnHand) {
        add(NeedKind::NeedBank, 0.80, "withdraw for a purchase",
            "something is waiting to be bought and the purse is empty",
            Fmt("wanted=%d on_hand=%d banked=%d", obs.coinWanted,
                obs.goldOnHand, obs.gold), false);
    }

    // GOLD IN THE PACK IS GOLD AT RISK. "nobody carry gold on them unless they
    // need to buy something -- always put additional items to bank, so they
    // can get it when they need it" (project owner, 2026-08-29).
    //
    // This shard has full loot on death, so every coin carried past what the
    // errand needs is a coin one bad fight away from another player's pack.
    // What a life legitimately carries is its own goldReserve -- the sum it
    // holds back for tools, reagents and lessons -- plus a little working
    // change; anything above that belongs in the box.
    if (cfg.profession && !obs.atBank) {
        // WHAT IS CARRIED, NOT WHAT IS OWNED. obs.gold is the status-bar
        // total and includes the bank box on this shard, so this used to
        // announce "spare=8785" and walk to the bank to deposit coins that
        // were already in it -- which is also why the character appeared to
        // spam the banker.
        const i32 carry =
            std::min(cfg.profession->goldReserve, kMaxGoldCarried) +
            kGoldWorthCarrying;
        if (obs.goldOnHand > carry) {
            // URGENCY SCALES WITH WHAT IS AT RISK. A flat 0.38 lost to
            // training every time -- 0.38 x 240 = 91 against a trainer need at
            // 110 -- so Corwyn walked around with 9,842 gold on him for a
            // whole session and never once opened a box, on a shard where
            // death is full loot. A few hundred spare is worth ignoring; nine
            // thousand is not, and the number should say which it is.
            const i32 spare = obs.goldOnHand - carry;
            const double risk =
                std::min(1.0, static_cast<double>(spare) /
                                  static_cast<double>(carry > 0 ? carry * 4 : 1));
            add(NeedKind::NeedBank, 0.30 + 0.45 * risk, "deposit surplus gold",
                "carrying more coin than this life needs, and death here is "
                "full loot",
                Fmt("on_hand=%d (total %d) reserve=%d carry=%d spare=%d "
                    "risk=%.2f", obs.goldOnHand, obs.gold,
                    cfg.profession->goldReserve, carry, spare, risk));
        }
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
        std::vector<market::Offer> spare =
            market::Surplus(*cfg.profession, holdings,
                            market::PolicyForPurse(obs.goldOnHand));
        // NOT THE HALF-MADE CLOTH. Same ruling as the bank side above: while
        // this life still owes itself cloth, the bolt it just wove is the next
        // gesture's input, not a thing to carry to a buyer.
        spare.erase(std::remove_if(spare.begin(), spare.end(),
                                   [&](const market::Offer& o) {
                                       return WoolChainWorkInProgress(
                                           *cfg.profession, holdings,
                                           cfg.craftBatch, o.item.c_str());
                                   }),
                    spare.end());
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
            const market::Offer* lead = nullptr;
            for (const market::Offer& o : spare) {
                if (o.qty > biggest) { biggest = o.qty; lead = &o; }
            }
            // A LOAD IS THE WHOLE SITTING, not a fixed count. While the pack
            // still funds more of the lead item, the material on the bench
            // is part of the load: held + still-makeable. The urgency then
            // reaches its full-load value only when the material is spent,
            // which is when a player stands up and walks to town. Below
            // that, the fixed surplusWorthTrip stays the floor so a life
            // that gathers rather than crafts is unchanged.
            const i32 stillFunds =
                lead ? StillMakeable(lead->item.c_str(), obs.pack) : 0;
            const i32 trip = std::max(std::max(1, cfg.surplusWorthTrip),
                                      biggest + stillFunds);
            const double frac = std::min(1.0, static_cast<double>(biggest) / trip);
            double urgency = 0.15 + 0.40 * frac;
            // THE BENCH FINISHES FIRST. While the pack still funds more of
            // the lead item, the sale waits behind the sitting: NeedCraft is
            // 0.50 x 130 = 65 (Goals.cpp), so the sale must stay under
            // 65/150 ≈ 0.43 until the material is spent. Without this cap
            // the ramp crossed 65 at ~87% of the load and took Lyra off a
            // sitting of 157 with 20 still to make (g_Lyra 12:56:55,
            // 2026-09-04). A gatherer's surplus has no recipe, stillFunds is
            // 0, and its ramp is untouched.
            if (stillFunds > 0) urgency = std::min(urgency, 0.40);
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
                    : Fmt("%d x %s spare, a load is %d (bench still funds %d), "
                          "buyer: %s", biggest, spare.front().item.c_str(),
                          trip, stillFunds, route.c_str()),
                route.empty());
        }

        // --- the other side of the market ----------------------------------
        //
        // A SHORTFALL IS ALSO A REASON TO GO. Everything above reads
        // Surplus(): a life with goods has an errand. A life SHORT of an input
        // only another character's profession makes had none -- the only
        // producer of NeedTrade in this whole file was the Surplus branch --
        // so a smith 20 logs short of the spear it wants to forge never scored
        // TRADE_WITH_PLAYER at all and the buyer half of every trade was
        // unreachable. Half a market is not a market.
        //
        // PlayerMarketWants() applies both filters: `rawResource` (the world
        // makes it -- go and dig, do not go and wait) and affordability at the
        // worst price this life would accept. Iron ore is filtered out by the
        // first; a log survives it, which is the one live producer-consumer
        // edge the catalogue has.
        //
        // Only when the seller half did not already raise one: Planner::Score
        // takes the FIRST need of a kind, and two NeedTrade rows would make
        // the second dead text.
        bool alreadyTrading = false;
        for (const Need& n : needs) {
            if (n.kind == NeedKind::NeedTrade) { alreadyTrading = true; break; }
        }
        // A miner-smith's first meaningful errand is to mine, not to stand at
        // the player market waiting for a secondary crafting input.  Without
        // this gate, a fresh miner with no ore could score a 0.55 player-buy
        // need against a 0.45 mine need while still in town and never take the
        // trip to the vein.  One twenty-metal batch proves the gather/smelt
        // loop before normal producer-to-producer buying resumes.
        const bool freshOreGatherer = [&] {
            if (!cfg.profession || cfg.profession->gathers != "ore") return false;
            constexpr int kFirstSmithBatch = 20;
            const int metal = QtyIn(obs.pack, "i_ore_iron") +
                              QtyIn(obs.pack, "i_ingot_iron") +
                              QtyIn(obs.bank, "i_ore_iron") +
                              QtyIn(obs.bank, "i_ingot_iron");
            return metal < kFirstSmithBatch;
        }();
        if (!alreadyTrading && !freshOreGatherer) {
            const market::TradePolicy buyPolicy =
                market::PolicyForPurse(obs.goldOnHand);
            // No refusal string is asked for here: AssessNeeds is pure and
            // cannot log. The Runner asks for it, on the tick where it can
            // print it (`market: ... not buying`).
            //
            // PACK COIN, NOT THE STATUS-BAR FIGURE. obs.gold counts the bank
            // box, but DriveOpenTrade only ever offers coin found in the
            // BACKPACK (FindBackpackItemByGraphic(kGoldCoin)) -- it does not
            // fetch from the bank first. `buyPolicy` above is already keyed
            // to obs.goldOnHand; the affordability check inside
            // PlayerMarketWants has to be too, or a life scores a want it
            // cannot actually pay for at the trade window.
            const std::vector<market::Want> buyable = market::PlayerMarketWants(
                *cfg.profession, holdings, obs.goldOnHand, buyPolicy, nullptr);
            if (!buyable.empty()) {
                // SAME SHAPE AS THE SELLER, deliberately, so weight 145 in
                // kGoals needs no re-tuning: a life 20 short of a 20-restock
                // input scores 0.55 x 145 = 79.8, the seller's own observed
                // number.
                i32 biggest = 0;
                for (const market::Want& w : buyable)
                    biggest = std::max(biggest, w.qty);
                const i32 restock = std::max(1, buyPolicy.restockConsumablesTo);
                const double frac =
                    std::min(1.0, static_cast<double>(biggest) / restock);
                const double urgency = 0.15 + 0.40 * frac;
                add(NeedKind::NeedTrade, obs.marketQuiet ? 0.0 : urgency,
                    "buy from a player",
                    obs.marketQuiet
                        ? "short of an input only another character's "
                          "profession makes, and the market was just tried "
                          "and found empty"
                        : "short of an input only another character's "
                          "profession makes",
                    Fmt("%d x %s short", biggest, buyable.front().item.c_str()),
                    obs.marketQuiet);
            }
        }
    }

    // --- reagents a mage needs to practise at all --------------------------
    //
    // BEFORE the craft-supplies clause below, because FindNeed (Goals.cpp)
    // returns the FIRST NeedSupplies row and this one is the more urgent kind
    // of empty: a mage out of reagents has no trade at all, where a crafter
    // short of an input still has a pack to sell.
    //
    // Written by the runner from what PRACTICE_SKILL found missing, never
    // guessed here -- which spell a character practises with depends on what
    // its spellbook holds, and the need model does not read spellbooks.
    // Wave 2026-09-02: four mages cast at an empty pouch for a whole session
    // because nothing in the needs list ever said "go and buy reagents".
    if (!obs.practiceReagentsShort.empty()) {
        // Same working-capital rule the craft clause states below: BUY_SUPPLIES
        // spends only what is above its hard floor of 100, so under that there
        // is nothing to spend and selling is the way out, not shopping.
        const bool noCapital = (obs.gold - 100) <= 0;
        std::string list;
        for (const std::string& r : obs.practiceReagentsShort)
            list += (list.empty() ? "" : ",") + r;
        add(NeedKind::NeedSupplies, noCapital ? 0.0 : 0.46,
            "buy spell reagents",
            noCapital ? "out of reagents AND of the gold to buy them -- "
                        "something has to be sold first"
                      : "out of the reagents its practice spell consumes, and "
                        "a mage shop sells every one of them",
            Fmt("%d x each of %s", obs.practiceReagentQty, list.c_str()),
            noCapital);
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
        const CraftIntent now = ChooseCraft(*cfg.profession, obs, 1,
                                            cfg.craftFocus);
        const CraftIntent craft =
            now.item && now.missing.empty()
                ? now
                : ChooseCraft(*cfg.profession, obs, cfg.craftBatch,
                              cfg.craftFocus);
        // ...AND A THIRD, WHICH IS THE SHOPPING QUESTION ON ITS OWN.
        //
        //   "is the pack BALANCED for the sitting it has already paid for?"
        //
        // The two questions above both size the sitting at a fixed number, and
        // that made bulk buying pointless: Elara came back from the alchemist
        // with 84 nightshade and kept the four bottles her newbie kit gave her,
        // because five potions want five bottles and she had four -- close
        // enough that "can I make one right now?" said yes and the errand
        // vanished. She brewed twice and stopped with 82 leaves in the pack.
        // CraftBatchFromStock reads the size off the best-stocked input, so the
        // shortfall it names is the OTHER half of a recipe already half paid
        // for -- never more of the thing just bought.
        const i32 stockBatch = CraftBatchFromStock(*cfg.profession, obs,
                                                   cfg.craftBatch,
                                                   cfg.craftFocus);
        const CraftIntent stocked =
            stockBatch > cfg.craftBatch
                ? ChooseCraft(*cfg.profession, obs, stockBatch, cfg.craftFocus)
                : craft;
        // WHAT THE BATCH IS SHORT OF THAT ONLY A LOOM CAN MAKE.
        //
        // Read off the chosen recipe's own missing list rather than from a
        // profession flag: "is this life a tailor" is the wrong question --
        // the right one is "does the thing it is trying to make need cloth
        // and is there none". A full_crafter sewing a shirt gets the same
        // answer as a tailor, which is correct, and a smith never sees this
        // need at all because no recipe it chose asks for cloth.
        std::string clothShort;
        i32 clothShortQty = 0;
        // The full quantity a sitting wants of `clothShort`, so the urgency
        // fraction below means the same thing whichever of the two shapes
        // filled the shortfall in.
        i32 clothWantQty = 0;
        for (const prod::Ingredient& ing : craft.missing) {
            if (!IsWoolChainMaterial(ing.item)) continue;
            clothShort = ing.item;
            clothShortQty = ing.qty;
            // Four yarn per bolt; the batch's worth of that is the denominator
            // the "a full batch short scores 0.55" note below is written for.
            clothWantQty = std::max(1, cfg.craftBatch * 4);
            break;
        }
        // ...OR THE THING BEING MADE *IS* THE CLOTH. "I have yarn, I need to
        // weave it."
        //
        // The loop above reads the recipe's MISSING list, which is the right
        // question only while the wool chain is an INPUT to what is being made.
        // When the chosen output is itself a wool-chain item -- the tailor's
        // `produces` opens with i_cloth_bolt (Professions.cpp) -- four yarn in
        // the pack make the missing list EMPTY, and an empty missing list said
        // "nothing to do about cloth". Aelia sat on 6 yarn and 0 bolts for a
        // whole session: CRAFT chose i_cloth_bolt, found every input present,
        // handed off to MAKE_CLOTH because a bolt is loom work, and MAKE_CLOTH
        // was never in the planner's list at all -- not even as a BLOCKED_NEED
        // (artifacts/cloth_walkup_bolt_route_capacity_2026-09-02.md section 4).
        // The session drifted CRAFT -> PRACTICE_SKILL -> EARN_GOLD.
        //
        // Deliberately narrow, and NOT a profession flag: the gate is that the
        // OUTPUT is on the wool chain, which in the whole catalogue only the
        // tailor's produces list is (every other trade names wool, yarn, cloth
        // and thread under `consumes`, never `produces`). So no other life's
        // player-first rule is touched, and nothing here buys cloth from an NPC.
        if (clothShort.empty() && craft.item && craft.skillsMet &&
            IsWoolChainMaterial(craft.item)) {
            const i32 want = std::max(1, cfg.craftBatch);
            const i32 have = QtyIn(obs.pack, craft.item);
            if (have < want) {
                clothShort = craft.item;
                clothShortQty = want - have;
                clothWantQty = want;
            }
        }

        if (craft.item && craft.skillsMet) {
            if (craft.missing.empty()) {
                add(NeedKind::NeedCraft, 0.50, "make goods to sell",
                    "holds every input for something this life can legitimately "
                    "sell -- to an NPC or, for a player-market good, to a player",
                    Fmt("%s: %s", craft.item, craft.why));
            }
            // THE SHOPPING ROW ANSWERS THE `stocked` QUESTION, not `craft`'s.
            //
            // When the bench can start, `craft` has nothing missing and there
            // used to be no supplies row at all -- which is precisely how 84
            // nightshade and 4 bottles counted as "stocked". `stocked` is the
            // same recipe sized to what the pack has already paid for, so this
            // row appears for the half of the recipe that is genuinely short
            // and stays silent when the pack is balanced. NeedCraft above is
            // untouched and still outranks it (65 against 61.6): a crafter that
            // can work, works, and shops afterwards.
            const CraftIntent& shop = craft.missing.empty() ? stocked : craft;
            if (shop.item && shop.skillsMet && !shop.missing.empty()) {
                // Can the shortfall actually be bought? A missing input with
                // no seller is a blocked state, not an errand -- and saying
                // otherwise is how a goal wins the scoring and then discovers
                // on entry that there was never anywhere to go.
                const prod::Ingredient& first = shop.missing.front();
                const econ::VendorRuling ruling =
                    econ::CanBuyFromNPC(first.item);
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
                // EMPTY SHELVES ARE NOT A LOW SHELF -- AND IT IS A RATE, NOT
                // A SECOND CONSTANT.
                //
                // How much of the sitting the pack has already paid for can
                // actually be made? That is the worst-stocked input, against
                // the batch the best-stocked one funds. One poison out of
                // thirty-eight is a shop with the shelves bare; thirty-five out
                // of thirty-eight is a shop that should be open. So the urgency
                // slides between them instead of switching:
                //
                //   frac 0.00 -> 0.95 x 140 = 133  over the heal-potion browse
                //                                  (130) and the spellbook (77)
                //   frac 1.00 -> 0.44 x 140 =  62  under CRAFT (65): working
                //                                  stock is for working with
                //
                // The floor of 0.44 is the number the scribe incident recorded
                // above was tuned to and is deliberately unmoved. Nothing here
                // outranks the bank run, food, healing or the emergencies. The
                // horse is handled at its own need -- no weight under the
                // emergency band beats BUY_MOUNT's 255.
                i32 madeable = -1;
                if (const prod::Recipe* rec = prod::FindRecipe(shop.item)) {
                    for (const prod::Ingredient& in : rec->inputs) {
                        if (!in.item || in.qty <= 0) continue;
                        const i32 can = QtyIn(obs.pack, in.item) / in.qty;
                        if (madeable < 0 || can < madeable) madeable = can;
                    }
                }
                if (madeable < 0) madeable = 0;
                // AGAINST A SITTING, NOT AGAINST THE BEST-STOCKED SHELF.
                // Measured against stockBatch alone, 72 bottles bought made
                // 66 nightshade look half-empty (33 of 67) and this need
                // pulled Elara off the mortar after five potions to go
                // shopping for reagents she would not touch for half an hour
                // (2026-09-04 01:26:48). A pack that holds a full sitting's
                // inputs is stocked; the shelf that runs low first is a
                // reason to shop after the sitting, not during it.
                const i32 sitting =
                    std::max(1, std::min(stockBatch, cfg.craftBatch));
                const double funded =
                    std::min(1.0, static_cast<double>(madeable) /
                                      static_cast<double>(sitting));
                const double supplyUrgency = std::max(0.44, 0.95 - 0.51 * funded);
                add(NeedKind::NeedSupplies,
                    (ruling.allowed && !noCapital) ? supplyUrgency : 0.0,
                    "buy craft inputs",
                    !ruling.allowed
                        ? "short of an input no NPC may legitimately sell it"
                        : (noCapital
                               ? "short of inputs AND of the gold to buy them "
                                 "-- what it has made has to be sold first"
                               : (unstockedCrafter
                                      ? "cannot make a single one of its own "
                                        "goods -- this is the trade itself, "
                                        "not a restock"
                                      : "short of what it needs to make its "
                                        "own goods")),
                    Fmt("%s x%d needs %d x %s (can make %d of %d)%s", shop.item,
                        stockBatch, first.qty, first.item, madeable, stockBatch,
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

        // --- CLOTH THIS LIFE HAS TO MAKE ITSELF ----------------------------
        //
        // Owner ruling, 2026-09-02: buy cloth from PLAYERS first; otherwise
        // gather it -- sheep, shears, wheel, loom, scissors -- and never buy
        // cloth, thread or yarn from an NPC. The vendor policy already
        // encodes the second half (data/revolution_vendor_policy.tsv:50,
        // i_cloth WORLD_PROCESSED, buy=0), which is exactly why a tailor
        // short of cloth used to score NeedSupplies at urgency 0.0 -- BLOCKED
        // with "short of an input no NPC may legitimately sell it" -- and
        // then had nothing at all left to do. This is the errand that was
        // missing from the other end of that refusal.
        //
        // THE GATE IS THE MARKET, NOT THE SHORTFALL. Walking to Yew is a
        // long trip and another character may be standing at the bank with a
        // bale to sell; the WTB window is cheaper and it is the behaviour the
        // owner asked for. So while `noSellerFor` does not carry cloth this
        // need is present but at zero and BLOCKED -- visible in telemetry,
        // never selected -- and TRADE_WITH_PLAYER's own NeedTrade row is what
        // wins. `no_player_seller` is written by DoTradeWithPlayer when its
        // listen period expires unanswered, so the first sheep is only ever
        // walked to after a real, failed attempt to buy.
        //
        // "CANNOT BUY NOW" IS ALSO AN ANSWER (owner ruling via lead,
        // 2026-09-02). Waiting for the WTB window to time out only works if
        // the window can ever open. Two live refusals happen BEFORE anything
        // is announced, and neither one writes a `no_player_seller`:
        //
        //   * the capital gate -- market::CanAffordToShop, the same
        //     `gold - blindPriceCeiling < goldReserve` PlayerMarketWants
        //     applies before it will name a single want. Aelia ran a whole
        //     5-minute gate at gold=0, so `buyable` was empty, NeedTrade never
        //     scored, TRADE_WITH_PLAYER never ran, and MAKE_CLOTH printed
        //     "the player market has not been asked for it yet" twenty times
        //     (run_gates/g_Aelia.console.txt:83-710).
        //   * the trip-time gate -- obs.marketTripFitsSession, the same test
        //     DoTradeWithPlayer makes on entry. Amara's need was 800 s and
        //     the market trip did not fit what was left of the session.
        //
        // Any of them means the player market cannot be asked for THIS
        // material on THIS tick, which under the ruling counts as declined:
        // the tailor goes and shears. Kept deliberately narrow -- it is
        // computed inside the cloth clause and read by nothing else, so no
        // other profession's player-first rule is touched, and it still never
        // buys wool, yarn or cloth from an NPC.
        if (!clothShort.empty()) {
            const bool asked = obs.NoSellerFor(clothShort);
            const market::TradePolicy buyPolicy =
                market::PolicyForPurse(obs.goldOnHand);
            const bool broke = !market::CanAffordToShop(
                *cfg.profession, obs.goldOnHand, buyPolicy);
            const bool noTime = !obs.marketTripFitsSession;
            //   * the goal's own stand-down -- TRADE_WITH_PLAYER cooling after
            //     it achieved nothing. Nobody can be asked while the goal that
            //     does the asking is out of the running, and waiting on an
            //     answer that cannot be sought is how EXPLORE ended up with
            //     half of Amara's session (see Observation::marketAskOnCooldown
            //     for the log lines).
            const bool cooling = obs.marketAskOnCooldown;
            const bool declined = asked || broke || noTime || cooling;
            // Same shape as the other work needs so no weight needs
            // re-tuning: a life a full batch short scores 0.55.
            const double frac =
                std::min(1.0, static_cast<double>(clothShortQty) /
                                  std::max(1, clothWantQty));
            add(NeedKind::NeedCloth, declined ? 0.15 + 0.40 * frac : 0.0,
                "cloth",
                asked ? "short of cloth, no NPC may sell it, and the player "
                        "market was asked and nobody answered -- so shear it"
                : broke ? "short of cloth, and too poor to buy any from a "
                          "player at all -- the wheel and the loom are free"
                : noTime ? "short of cloth, and there is not enough session "
                           "left to walk to the market and ask -- shear it "
                           "instead"
                : cooling ? "short of cloth, and the trade errand has stood "
                            "itself down -- nobody can be asked right now, so "
                            "shear it"
                          : "short of cloth, but the player market has not "
                            "been asked for it yet",
                broke ? Fmt("%d x %s short, %d gold on hand, reserve %d",
                            clothShortQty, clothShort.c_str(), obs.goldOnHand,
                            cfg.profession->goldReserve)
                      : Fmt("%d x %s short", clothShortQty,
                            clothShort.c_str()),
                !declined);
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
    // ORE, for the same reason and on the same terms as fish. This was missing
    // entirely: Corran held a pickaxe for a whole session and mined nothing,
    // because no need ever said he should.
    if (cfg.profession && cfg.profession->gathers == "ore") {
        // STANDING IN THE MINE IS THE ARGUMENT. A flat 0.45 left mining the
        // lowest-scoring thing a miner could do -- 0.45 x 130 = 58 against a
        // trainer errand at 61 -- so Corwyn walked past the ore in Minoc to go
        // and buy tenths of Tinkering, all session, every session.
        //
        // Being AT the rock is what a person weighs: the walk is already paid
        // for, the pickaxe is in hand, and there is nothing else this life
        // would rather be doing here.
        //
        // A GLUT IS A REASON TO STOP. `here` used to be a bare constant, so a
        // miner standing on ore mined for the whole session: the need never
        // fell, CRAFT (0.50 x 130 = 65) could never outrank it (0.65 x 130 =
        // 84.5), and Corwyn ended up holding 30 ingots he could neither smith
        // nor sell -- an ingot is a material, and materials do not go to NPCs.
        // "all these time it couldnt just mine smelt smith sell" (project
        // owner, 2026-08-29, said twice).
        //
        // Ore and ingots are one stock because smelting is 1:1 (Production
        // .cpp: i_ingot_iron <- {i_ore_iron, 1}). Twenty is five daggers at 4
        // ingots each, or three short spears at 6 -- a batch worth walking to
        // a forge with, rather than a token handful.
        //
        // It tapers to a floor, not to zero: if the forge turns out to be
        // unreachable, mining is still better than standing still.
        constexpr int kEnoughToSmith = 20;
        constexpr double kMiningFloor = 0.15;

        // AND THE STOCK IS IN THE BOX, NOT THE PACK. Unsold ingots are banked
        // the moment they have no buyer (see DoBank, "put unsold stock
        // away"), so counting the pack alone reports a smith with six hundred
        // ingots as having none, and he mines forever.
        const int stock = QtyIn(obs.pack, "i_ore_iron") +
                          QtyIn(obs.pack, "i_ingot_iron") +
                          QtyIn(obs.bank, "i_ore_iron") +
                          QtyIn(obs.bank, "i_ingot_iron");

        // TWENTY INGOTS IS A BATCH. IT IS NOT A TRAINING STOCK.
        //
        // "you cant train with only 15-20 iron first you need to stock some
        // maybe 500-600 then you start train blacksmith" (project owner,
        // 2026-08-30). Raising Blacksmithing means making and unmaking
        // hundreds of items; twenty ingots is five daggers, an afternoon's
        // pocket money and about 1% of what the skill costs.
        //
        // So a smith who has not finished training keeps digging against the
        // bigger number. Once the skill is where the build wants it, twenty is
        // the right threshold again -- at that point metal is stock to sell,
        // not fuel for a skill, and a full pack is a reason to go and smith.
        constexpr int kSmithTrainingStock = 550;   // the owner's 500-600
        int wantStock = kEnoughToSmith;
        const i32 smithNow = obs.SkillTenths(rules::kBlacksmithing);
        for (const SkillTarget& t : plan.skills) {
            if (t.skillId != rules::kBlacksmithing) continue;
            if (smithNow < t.tenths) wantStock = kSmithTrainingStock;
            break;
        }

        const double base = obs.atWorkSite ? 0.65 : 0.45;
        const double glut = stock >= wantStock
                                ? 1.0
                                : static_cast<double>(stock) / wantStock;
        const double here = base - (base - kMiningFloor) * glut;
        add(NeedKind::NeedOre, here, "ore",
            obs.atWorkSite
                ? "standing at the rock with a pickaxe -- this is the job"
                : "ore is this life's income and its Mining training",
            wantStock > kEnoughToSmith
                ? Fmt("%d ore+ingots of the %d wanted before smith training "
                      "(smithing %.1f) at_work_site=%d",
                      stock, wantStock, smithNow / 10.0, obs.atWorkSite ? 1 : 0)
                : Fmt("carrying %d ore+ingots at_work_site=%d", stock,
                      obs.atWorkSite ? 1 : 0),
            false);

        // AND THE OTHER HALF OF THE SAME DECISION. Ore is not income; metal
        // is. The mirror of NeedOre above: as the pack fills, digging matters
        // less and melting matters more, and the crossover is what turns a
        // miner into a smith for a while.
        //
        // Smelting is cheap to want and cheap to abandon -- the forge is in
        // the same city as the bank and the smithy this life already visits.
        // EVERY METAL, not just iron. A quarter of an ordinary rock is
        // rusty / copper / bronze (r_default_rock weights, see Runner.cpp
        // "AND EVERY METAL THE PACK ACTUALLY HOLDS"), and DoSmelt already
        // melts a coloured vein into its own ingot -- but this need used to
        // count "i_ore_iron" alone, so a pack holding only coloured ore never
        // raised NeedSmelt and the miner walked past the forge with it.
        // Owner saw exactly that on 2026-09-01: "miner not smelting ores
        // beside iron".
        const int ore = QtyInByPrefix(obs.pack, "i_ore_");
        if (ore > 0) {
            const double ready =
                ore >= kEnoughToSmith
                    ? 1.0
                    : static_cast<double>(ore) / kEnoughToSmith;
            add(NeedKind::NeedSmelt, 0.25 + 0.50 * ready, "ingots",
                "ore is dead weight until a forge turns it into metal",
                Fmt("carrying %d ore", ore), false);
        }
    }
    // A PET. A tamer without one is a tamer in name only, and Cassia spent a
    // session exploring because nothing else in her life was actionable.
    if (cfg.profession && cfg.profession->id == "tamer" && !obs.hasPet) {
        add(NeedKind::NeedPet, 0.42, "a pet",
            "a tamer with no animal has no trade to practise",
            Fmt("taming %.1f", obs.SkillTenths(rules::kTaming) / 10.0), false);
    }
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
            nothingHere && cfg.profession &&
            (WantsToHunt(*cfg.profession) || WantsSpellCombat(*cfg.profession)) &&
            obs.hp * 100 >= obs.hpMax * huntHpPct && obs.WeightFraction() < 0.7;
        const bool blocked = obs.huntReturnPending || (nothingHere && !couldGoHunting);
        // A FIGHTER'S URGENCY, ON THE SAME SCALE AS EVERY OTHER TRADE'S.
        //
        // 0.15 + 0.25 x gap tops out at 0.40, which is what a life feels about
        // a skill it would quite like to raise. It is not what a swordsman
        // feels about the only work he has. The mining need already says the
        // right sentence -- "ore is this life's income and its Mining
        // training", 0.45 away from the rock and 0.65 standing at it -- and a
        // graveyard is a fighter's rock. Same two numbers, same meaning.
        //
        // Only for a life that CAN go: a blocked one keeps the old figure,
        // because wanting something impossible harder helps nobody.
        double urgency = 0.15 + 0.25 * gap;
        if (couldGoHunting) {
            const double base = (obs.hostilesNear > 0) ? 0.65 : 0.45;
            if (base > urgency) urgency = base;
        }
        add(NeedKind::NeedTraining, urgency, SkillName(t.skillId),
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

    // Reaching the combat skill cap does not end a hunter's income loop.
    if (cfg.profession && (WantsToHunt(*cfg.profession) || WantsSpellCombat(*cfg.profession))) {
        bool trainingExists = false;
        for (const Need& need : needs)
            if (need.kind == NeedKind::NeedTraining) trainingExists = true;
        if (!trainingExists) {
            const bool ready = !obs.huntReturnPending &&
                obs.HpFraction() >= cfg.healHpFraction && obs.WeightFraction() < 0.70;
            add(NeedKind::NeedTraining, obs.hostilesNear > 0 ? 0.65 : 0.45,
                "hunt for income", "completed combat build still earns through hunting",
                "all combat skill targets reached", !ready);
        }
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
    //
    // ONLY FOR LIVES THAT FIGHT -- the need and the goal must agree.
    //
    // DoUpgradeGear already refuses to shop for a non-hunter ("for crafter
    // upgrade gear just wear normal clothing for now", project owner,
    // 2026-08-29): it logs one line and Finish(true)es with progress 0. So
    // asking a scribe to check her armour produced a goal that could only
    // ever complete having done nothing -- Ilyandra picked UPGRADE_GEAR over
    // and over on that branch (run_r4/w_Ilyandra.console.txt:742-758) until
    // the noop-spin backstop cooled it, then picked it again.
    //
    // A need whose goal cannot act on it is not a need. Same test the bandage
    // need above uses, for the same reason.
    if (cfg.profession && WantsToHunt(*cfg.profession)) {
        // A STARTER SET IS NOT A BROWSE.
        //
        // DoTrainCombat will not walk to a graveyard without three protected
        // layers, and hands the turn to UPGRADE_GEAR when it has none. At a
        // flat 0.22 that advice was unfollowable: UPGRADE_GEAR scored 22 and
        // lost to the wool chore (42) and the equipment run (72.8) every time,
        // so the fighter cooled its hunt for four minutes in favour of a goal
        // that was never picked -- g_Faustus.console.txt:391,398 and
        // g_Castor.console.txt:685,694 (2026-09-03, both waves).
        //
        // 0.75 x 100 = 75 puts the starter set above the wool chore (0.50 x
        // 140 = 70) and the armour advice therefore gets a turn, while
        // staying under a hunt this character could actually go on (0.65 x
        // 130 = 84.5) so an ARMOURED fighter is never sent shopping instead of
        // fighting. The browse keeps its old 0.22.
        const bool starterSet = !obs.hasBasicArmor;
        add(NeedKind::NeedGear, starterSet ? 0.75 : 0.22, "gear",
            starterSet
                ? "no armour worth the name, and a hunting ground is not a "
                  "place to arrive in a shirt"
                : "loot and shops both hold better armour than this character "
                  "is wearing, and nothing checks unless this asks",
            Fmt("str %d gold %d reserve %d basic_armour=%d", obs.str, obs.gold,
                cfg.profession->goldReserve, obs.hasBasicArmor ? 1 : 0),
            false);
    }

    // --- a horse, before anything else that costs a walk --------------------
    //
    // "create a new tailor, make it buy a horse first, mount, then do the
    // rest" (project owner, 2026-09-02). Every profession, not tailors only:
    // Revolution players bought a horse the day they could afford one
    // (forum, 24.03.2011: NPC horse 800 gp). The runtime animal trainer
    // asks 450-500 (i_char_icons.scp VALUE={450 500}); the gate below is the
    // price plus this life's reserve, so a broke character earns first and
    // the errand never spends the coin the work needs.
    //
    // NOT raised while mounted (the horse is under her) and not while dead.
    // A horse bought and left standing is DoBuyMount's business, not this
    // need's: Sphere releases the pet at the buyer's feet in the purchase
    // packet, and the goal mounts it on its next tick.
    {
        constexpr i32 kHorsePriceCeiling = 500;
        const i32 reserve = cfg.profession ? cfg.profession->goldReserve : 0;
        const bool canAfford = obs.gold >= reserve + kHorsePriceCeiling;
        // A STAND-DOWN THAT OUTLIVES THE PROCESS. The errand's own cooldown is
        // 600 s, which is a whole session on this shard's gates -- so a
        // character that failed to find a trainer failed again at the next
        // login, and again at the one after. Counted in SESSIONS PLAYED, the
        // only durable clock a need has (NeedConfig::sessionIndex); the record
        // is written once per session by the runner when BUY_MOUNT cools.
        constexpr i32 kMountRestSessions = 4;
        i32 restedSince = -1;
        for (const LifeEvent& e : mem.Events()) {
            if (e.kind != "mount_unavailable") continue;
            i32 s = 0;
            if (std::sscanf(e.detail.c_str(), "session=%d", &s) != 1) continue;
            if (s > restedSince) restedSince = s;
        }
        const bool restingFromPastSession =
            restedSince >= 0 && cfg.sessionIndex > 0 &&
            (cfg.sessionIndex - restedSince) < kMountRestSessions;
        const bool stoodDown = obs.mountAskOnCooldown || restingFromPastSession;
        // `dismountedForWork` is a precondition, not a preference: a miner
        // standing beside his own horse with a pickaxe in his hands is not a
        // character who needs to buy one, and treating him as one made
        // BUY_MOUNT supersede MINE every few seconds (see Observation::
        // dismountedForWork).
        if (!obs.mounted && !obs.dead && !obs.dismountedForWork) {
            // A HORSE IS A CONVENIENCE. THE GRAVEYARD IS THE JOB.
            //
            // 0.80 x 255 = 204 put BUY_MOUNT above everything a fighter does
            // for a living -- above a hunt it was standing next to (0.65 x 130
            // = 84.5) -- and it won the opening pick of every fighter session
            // on 2026-09-03 (g_Hector.console.txt:147, g_Faustus:162,365,
            // g_Titus:63). Each pick then failed on the trip and cooled, and
            // four fighters ended their sessions with kills=0.
            //
            // The owner's intent is kept for the lives it was written for --
            // "create a new tailor, make it buy a horse first" (2026-09-02) --
            // and for a fighter it becomes what it actually is: an errand for
            // a quiet hour. 0.30 x 255 = 76.5 sits UNDER a hunt this character
            // could go on (84.5) and OVER the wool chore (42) and the armour
            // browse (22), so the horse still gets bought on a day when the
            // fighting is blocked, cooled or unreachable.
            const bool fighter = cfg.profession && WantsToHunt(*cfg.profession);
            double urgency = canAfford ? 0.8 : 0.05;
            if (fighter && canAfford) urgency = 0.30;
            // THE SAME RULE, FOR A CRAFTER WITH NOTHING ON THE SHELVES.
            //
            // The fighter clause above says a horse must not outbid the work
            // that pays for it; `unstockedCrafter` is that argument for a life
            // whose work is a bench. Elara opened her first session with 10000
            // gold, BUY_MOUNT at 0.80 x 255 = 204, and not one leaf of
            // nightshade (run_gates/g_Elara.console.txt, 2026-09-03 15:13).
            // 0.25 x 255 = 63.75 sits under the stocking trip (133) and over
            // idling, so the horse is still bought -- on the way back from the
            // alchemist, with the mortar working.
            if (unstockedCrafter && canAfford) urgency = 0.25;
            add(NeedKind::NeedMount, stoodDown ? 0.0 : urgency, "riding horse",
                stoodDown
                    ? (obs.mountAskOnCooldown
                           ? "the horse errand has stood itself down after "
                             "failing -- not asking again this session"
                           : "no trainer could be reached in a recent session; "
                             "resting the errand for a few sessions")
                : !canAfford ? "on foot, but the purse does not clear the price "
                               "plus this life's reserve"
                : unstockedCrafter
                             ? "on foot with the price of a horse in hand, but "
                               "the shelves are empty and stock comes first for "
                               "a life that sells what it makes"
                : fighter    ? "on foot with the price of a horse in hand, but "
                               "the hunt comes first for a life that fights"
                             : "on foot with the price of a horse in hand",
                Fmt("gold %d reserve %d price<=%d stood_down=%d last_fail_session=%d "
                    "session=%d", obs.gold, reserve, kHorsePriceCeiling,
                    stoodDown ? 1 : 0, restedSince, cfg.sessionIndex),
                stoodDown || !canAfford);
        }
    }

    // --- wool as a fighter's income ----------------------------------------
    //
    // Owner ruling 2026-09-02: "add this part only to warrior so they can
    // sell cloth -- because tailor doesn't have attack skill -- and it will
    // provide them additional income source". So the shear-kill-carve loop
    // is a WantsToHunt life's chore, gated on i_wool being in its own
    // `produces` (the melee schools list it; the archer has no blade).
    //
    // 0.50 while the purse is under this life's reserve -- income is the
    // point -- and 0.30 otherwise, a chore that fills a quiet hour; either
    // way below a fighter standing at a graveyard (0.65) and a hunt it could
    // go on (0.45) when poor is not the case. Muted while carrying a load
    // (the trip is over; NeedGold sells it), while hurt, or while dead.
    if (cfg.profession && !obs.dead && WantsToHunt(*cfg.profession)) {
        bool sellsWool = false;
        for (const std::string& it : cfg.profession->produces)
            sellsWool = sellsWool || it == "i_wool";
        const i32 woolCarried = market::QtyOf(obs.pack, "i_wool");
        // Unsold cloth in the pack is a load already: no new production until
        // it is sold or banked (thresholds are the stock, not a constant).
        const bool loaded = obs.WeightFraction() >= 0.7 ||
                            market::QtyOf(obs.pack, "i_cloth") > 0;
        const bool hurt = obs.hp * 100 < obs.hpMax * 80;
        if (sellsWool) {
            const bool poor = obs.gold < cfg.profession->goldReserve;
            const bool blocked = loaded || hurt;
            add(NeedKind::NeedWoolIncome, blocked ? 0.0 : (poor ? 0.50 : 0.30),
                "wool",
                loaded ? "already carrying a load -- sell it first"
                : hurt ? "too hurt to be putting sheep down"
                : poor ? "purse under the reserve; sheep are the nearest coin"
                       : "wool sells to tailors and the flock is free",
                Fmt("wool=%d gold=%d reserve=%d weight=%.0f%%", woolCarried,
                    obs.gold, cfg.profession->goldReserve,
                    obs.WeightFraction() * 100.0),
                blocked);
        }
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

        // IS THE TRADE'S OWN LADDER STUCK ON A SPELL?
        //
        // Owner ruling 2026-09-04: completing the spellbook is a STANDING SIDE
        // GOAL for every book carrier, and a side goal "must not outbid the
        // trade while the trade is workable". So the weight below is decided
        // by one question the craft chooser can already answer: did it have to
        // SKIP a rung because the book lacks that scroll's spell
        // (CraftIntent::wantSpell -- see ChooseCraft)? If it did, the book is
        // no longer a comfort purchase, it is the thing standing between this
        // scribe and the next step of its trade.
        const CraftIntent rung =
            ChooseCraft(*cfg.profession, obs, 1, cfg.craftFocus);
        const spell::SpellDef* wantDef =
            rung.wantSpell ? spell::DefForSpell(rung.wantSpell) : nullptr;
        // WHAT A SHOP WILL ACTUALLY SELL. Circles 1-4 are mage-shop stock;
        // above that "there is NO purchasable source" (Production.cpp, the
        // i_scroll_gate_travel note) and the only honest route is monster loot
        // or a dungeon chest.
        const bool wantOnAShelf = wantDef && wantDef->circle <= 4;
        // Can the bench work at all right now on some other rung? A scribe
        // with nightshade and blanks can still write poison scrolls while it
        // waits for a Recall scroll, and that trade must keep its turn.
        const bool benchWorkable =
            rung.item && rung.skillsMet && rung.missing.empty();

        // The `spellsKnown >= target` guard on both blocked branches matters:
        // a book that is still SHORT has ordinary shopping to do, and saying
        // "blocked" would take that errand away from a low-INT mage with an
        // empty book. Blocked is only the whole truth once there is nothing
        // left to buy.
        if (rung.lowManaSpell && !rung.wantSpell &&
            obs.spellsKnown >= target) {
            // A STAT WALL, NOT A SHOPPING ERRAND. The scroll is already in the
            // book and the shard still hides the circle, because every leaf of
            // that submenu is `TESTIF=<cancast ... >` and the cast costs more
            // mana than this character can hold. Said as a blocked need so it
            // scores 0 and cannot spin, and named precisely so nobody sends
            // the purse after it: the answer is INT.
            const spell::SpellDef* lm = spell::DefForSpell(rung.lowManaSpell);
            add(NeedKind::NeedSpells, 0.0, "spells",
                Fmt("'%s' costs %d mana and this character can hold %d -- the "
                    "craft menu hides that circle until INT rises, and no "
                    "scroll purchase can change it",
                    lm ? lm->name : "the next rung", rung.lowManaCost,
                    std::max(obs.intel, obs.mana)),
                Fmt("spells %d/%d mana_wall_spell=%d cost=%d int=%d mana=%d",
                    obs.spellsKnown, target, rung.lowManaSpell,
                    rung.lowManaCost, obs.intel, obs.mana),
                true);
        } else if (rung.wantSpell && !wantOnAShelf &&
                   obs.spellsKnown >= target) {
            // NOWHERE TO BUY IT AND NOTHING TO SHOP FOR. Say so once per tick,
            // as a blocked need (score 0, so it cannot spin), and name the
            // route that would actually work. There is no hunt goal that can
            // carry a "loot scrolls" intent yet, so this is a BLOCKED_NEED and
            // deliberately not a goal.
            add(NeedKind::NeedSpells, 0.0, "spells",
                Fmt("%s needs '%s' (circle %d) and no shop sells above circle "
                    "4 -- that scroll only drops from monsters and dungeon "
                    "chests, and no hunt goal can ask for one yet",
                    rung.wantSpellItem ? rung.wantSpellItem : "the next rung",
                    wantDef->name, wantDef->circle),
                Fmt("spells %d/%d blocked_rung=%s spell=%d circle=%d",
                    obs.spellsKnown, target,
                    rung.wantSpellItem ? rung.wantSpellItem : "?",
                    rung.wantSpell, wantDef->circle),
                true);
        } else if (obs.spellsKnown < target ||
                   (rung.wantSpell && wantOnAShelf)) {
            // The second clause is the stuck-ladder case: a book can be past
            // the comfortable target and still be missing the ONE spell this
            // trade's next rung is written with, and that errand must exist.
            const double shortfall =
                std::max(0.0, static_cast<double>(target - obs.spellsKnown) /
                                  static_cast<double>(target));
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
            double urgency = 0.10 + 0.25 * shortfall + 0.35 * wealth;

            // WHERE THE SIDE GOAL SITS RELATIVE TO THE TRADE.
            //
            // These two numbers are expressed against the SCORES, not against
            // each other's urgencies, because the goal weights differ:
            // FILL_SPELLBOOK is 110 and CRAFT is 130 (Goals.cpp), so
            // NeedCraft's standing 0.50 is a score of 65 and the same urgency
            // on this need would be 55. The rule the owner set is about the
            // scores, so convert once and keep the arithmetic visible.
            constexpr double kFillSpellbookWeight = 110.0;
            constexpr double kCraftScore          = 0.50 * 130.0;   // 65.0
            // Below the bench, by a whole point of score, while the bench has
            // material. Without this the wealth term alone (0.35) put a
            // comfortable book-shopping trip at 77.0 and it beat a scribe who
            // was standing next to everything she needed to write.
            const double sideCap = (kCraftScore - 1.0) / kFillSpellbookWeight;
            // Above it when the ladder is stuck: not shopping for comfort but
            // for the one thing that unblocks the next rung. Kept just clear
            // of the bench rather than at the top of the list -- emergencies,
            // healing and food all still outrank it.
            const double stuckFloor = (kCraftScore + 15.0) / kFillSpellbookWeight;

            const bool stuck = rung.wantSpell && wantOnAShelf;
            if (stuck) {
                urgency = std::max(urgency, stuckFloor);
            } else if (benchWorkable) {
                urgency = std::min(urgency, sideCap);
            }

            add(NeedKind::NeedSpells, urgency, "spells",
                noBook ? "no spellbook at all -- a mage that cannot cast "
                         "anything needs one before it needs anything else"
                : stuck ? "the next thing this trade can make needs a spell "
                          "the book does not hold, and a shop sells it"
                : benchWorkable
                    ? "the book could be fuller, but the bench has material "
                      "and the trade comes first"
                : (wealth > 0.5
                       ? "the purse is well clear of the reserve, and "
                         "spells are what spare gold buys first"
                       : "the book is short of the spells this life "
                         "will cast"),
                Fmt("spells %d/%d book=%s magery %.1f gold %d reserve %d "
                    "spare %d wealth %.2f%s",
                    obs.spellsKnown, target, noBook ? "none" : "carried",
                    obs.SkillTenths(rules::kMagery) / 10.0, obs.gold, reserve,
                    spare, wealth,
                    stuck ? Fmt(" stuck_rung=%s spell=%s circle=%d",
                                rung.wantSpellItem ? rung.wantSpellItem : "?",
                                wantDef->name, wantDef->circle).c_str()
                          : (benchWorkable ? " capped below the bench" : "")),
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

    // --- STR this life's own work can never reach --------------------------
    //
    // Owner order 2026-09-04: "STR needed -> temporarily train Wrestling ->
    // spar bare-handed -> when the STR target is reached, set Wrestling DOWN
    // and let the real skills take its points back."
    //
    // Read from the PLAN rather than from the profession id, because the
    // question is arithmetic and not identity: a build whose best STAT_STR is
    // 20 cannot reach STR 85 whatever it is called. See AssessStatFarm.
    if (!obs.dead) {
        const StatFarmPlan sf = AssessStatFarm(plan, obs);
        if (sf.wanted) {
            // Same two brakes every fighting errand carries: a character does
            // not go looking for something to punch while hurt or while
            // carrying a load it is about to lose.
            const bool hurt   = obs.hp * 100 < obs.hpMax * 80;
            const bool loaded = obs.WeightFraction() >= 0.7;
            const bool blocked = hurt || loaded;
            add(NeedKind::NeedStrength, blocked ? 0.0 : sf.urgency, "Strength",
                hurt   ? "too hurt to be sparring for stats"
                : loaded ? "carrying too much to spar"
                         : sf.why,
                Fmt("str %d/%d ceiling %d wrestling %.1f dummy_ok=%d",
                    sf.have, sf.target, sf.ceiling, sf.wrestlingTenths / 10.0,
                    sf.useDummy ? 1 : 0),
                blocked);
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
