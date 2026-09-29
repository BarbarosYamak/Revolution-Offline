#include "RunnerInternal.h"
#include "NoviceEngage.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;

namespace {

// THE POUCH A MAGE SHOULD BE CARRYING, AS A SHOPPING LIST.
//
// The old list was whatever the CURRENT spell needed and had none of, which is
// how Aurelius walked into a graveyard on 2026-09-07 holding 50 mandrake, 44
// spider silk, 36 ginseng -- and ZERO black pearl, so Fireball was not a rung
// she owned even though her book held it, and the ladder collapsed to Harm.
// A player does not shop one spell at a time; the owner's rule is a standing
// band of every one of the eight (spell::ReagentBandFor).
//
// Emptiest first, because BUY_SUPPLIES buys reagentWants_.front() and the
// reagent at zero is the one costing this character a rung right now. The
// quantity is one number for the whole trip (`N x each of ...`), so it is the
// LARGEST gap on the list -- buying a few spare of the cheaper kinds is what a
// player buying round lots does anyway, and reagents keep.
void ReagentBandShortfall(const Observation& obs,
                          std::vector<std::string>& missing, i32* qtyOut) {
    // The rule itself lives with the band it is read off (uo/spellcast.h), so
    // the need model and the shopping errand ask the same question this does.
    spell::ReagentBandShortfall(obs.pack, obs.gold, &missing, qtyOut);
}

// Shared weak-yard assignments replace the old Britain-only novice rule
// (owner, 2026-09-09). Personal death/retreat memory still excludes hot yards.
constexpr double kHuntGroundHeatLimit = 3.0;

// BestWeaponSkillTenths moved to RunnerInternal.h (runner_detail): the
// novice engagement policy in Survive.cpp needs the same number.

// WHICH SKILLS A FIGHT CAN RAISE. The weapon schools plus the support
// skills Sphere rolls during a swing or a cast; ids from uo/rules.h, which
// reads them off the runtime's own skill<N>_*.scp file names. Healing and
// Meditation are deliberately absent: they rise from bandaging and resting,
// which happen away from a fight as often as in one, and this line is about
// combat training specifically.
bool IsCombatOrMagerySkill(int skillId) {
    switch (skillId) {
        case rules::kSwordsmanship:
        case rules::kFencing:
        case rules::kMaceFighting:
        case rules::kArchery:
        case rules::kWrestling:
        case rules::kTactics:
        case rules::kAnatomy:
        case rules::kParrying:
        case rules::kMagery:
        case rules::kEvaluatingIntel:
            return true;
        default:
            return false;
    }
}

// WHICH GOAL EARNS A NON-COMBAT SKILL GAIN "BY CRAFTING", "BY GATHERING" OR
// "BY HEALING" -- the same shape of line combat and practice already get.
//
// qa-forensics (2026-09-07, artifacts/fleet122_20260907/regrade/summary.md)
// found TRAIN-2 only ever credited "gained in combat", "gained by practice"
// or "bought from a trainer": a character with real gains in Mining,
// Blacksmithing or Tinkering (Ghalor 100.1->150.4, Dravys 100.1->130.1,
// both measured off TRAIN-1's skill totals) logged nothing that named the
// gain, so 112/118 characters failed the gate even when they trained.
//
// The category is read off the ACTIVE GOAL, not the skill id, because one
// skill (Tailoring, say) can rise from more than one kind of goal and the
// goal is the reason a player would give. Meditation is not looked up here
// at all -- see the comment on IsCombatOrMagerySkill above; it rises from
// resting, not from doing, in every one of these lines.
const char* TrainCategoryForGoalKind(GoalKind kind) {
    switch (kind) {
        case GoalKind::Craft:
        case GoalKind::Smelt:
        case GoalKind::MakeCloth:
        case GoalKind::MakeBandages:
            return "crafting";
        case GoalKind::Mine:
        case GoalKind::GatherLogs:
        case GoalKind::Fish:
        case GoalKind::HarvestWool:
            return "gathering";
        case GoalKind::Heal:
            return "healing";
        default:
            return nullptr;
    }
}

}  // namespace

// A SKILL GAIN EARNED IN A FIGHT, AT THE BENCH, IN THE FIELD OR OVER A
// PATIENT, SAID OUT LOUD ONCE.
//
// The NPC path already prints `train: X a->b bought from a trainer`
// (DoTrainAtNpc below) and it was the only training this project could
// verify from a log besides a fight. Every other source was invisible: the
// values move in the server's 0x3A skill packet (Client::OnSkills), reach
// the life layer as Observation::skills, and nothing ever compared two
// ticks of them for anything but the combat/Magery list.
//
// The baseline is refreshed on EVERY call, for EVERY skill (not just the
// combat list), including calls that do not print, so a lesson bought from
// a trainer or a lock change cannot be re-reported later as a gain won some
// other way.
void Runner::NoteCombatSkillGains(const Observation& obs, bool inFight) {
    for (const SkillTarget& s : obs.skills) {
        // Meditation rises from resting, not from doing; excluded from
        // every line below, combat or otherwise.
        if (s.skillId == rules::kMeditation) continue;
        const bool combatSkill = IsCombatOrMagerySkill(s.skillId);
        auto it = combatSkillSeen_.find(s.skillId);
        if (it == combatSkillSeen_.end()) {
            // First sighting is a baseline, never a gain: a skill absent
            // from the table reads 0.0, and 0.0 -> 51.2 is the first full
            // 0x3A arriving, not something this character did.
            combatSkillSeen_[s.skillId] = s.tenths;
            continue;
        }
        const i32 was = it->second;
        it->second = s.tenths;
        if (s.tenths <= was) continue;
        if (combatSkill) {
            if (!inFight) continue;
            LogLine("train: %s %.1f->%.1f gained in combat",
                    rules::SkillName(s.skillId), was / 10.0, s.tenths / 10.0);
            continue;
        }
        // Not a weapon/Magery skill: the only other source this function
        // can name is the goal running right now. Anything else (idle,
        // travel, banking...) stays silent -- the baseline above still
        // moved, so a later gain under a countable goal reports only its
        // own delta, never one earned somewhere this function cannot see.
        const char* category = TrainCategoryForGoalKind(planner_.Current().kind);
        if (!category) continue;
        LogLine("train: %s %.1f->%.1f gained by %s",
                rules::SkillName(s.skillId), was / 10.0, s.tenths / 10.0,
                category);
    }
}

// A SKILL GAIN EARNED BY PRACTISING, MEASURED THE SAME WAY.
//
// PRACTICE_SKILL used to end `Finish(true)` on nothing but an attempt count:
// Selene sent ten ActionUseSkill(Meditation) calls and completed the goal
// twice with the skill reading 20.0 throughout
// (artifacts/selene_train_false_positive_2026-09-06.md). A self-use skill
// never answers "that failed", so the only outcome this client can observe is
// the server's own skill table (0x3A, Client::OnSkills -> Observation::skills).
//
// Baseline is per bout (skill + the goal selection that owns it) and moves up
// with every gain, so one tenth is reported once.
bool Runner::NotePracticeGain(int skillId, i32 have) {
    if (practiceBaselineSkill_ != skillId) return false;
    if (have <= practiceBaselineTenths_) return false;
    LogLine("train: %s %.1f->%.1f gained by practice",
            rules::SkillName(skillId), practiceBaselineTenths_ / 10.0,
            have / 10.0);
    practiceBaselineTenths_ = have;
    ++practiceGains_;
    return true;
}

// LEAVING THE HUNT HANDS THE TRIP ALLOWANCE BACK. Every hand-off below goes
// to a different goal kind -- heal, bank, buy, practise -- and that goal
// plans its own journeys; the walks it makes are not attempts to reach a
// hunting ground. Counting them was D9 (Aurelius, 00:20:08).
bool Runner::HandOffFromHunt(GoalKind to, i64 forMs, const char* why, i64 nowMs) {
    huntTrips_ = 0;
    huntEmptyArrivals_ = 0;
    huntUnderstockAuthorized_ = false;
    ClearHuntEngageState();
    return HandOff(GoalKind::TrainCombat, to, forMs, why, nowMs);
}

i32 Runner::HuntSupport(Client& client, const Observation& obs, u32 target, bool announce) {
    std::vector<Client::HostileHit> allies;
    client.CombatSupportNear(target, allies);
    i32 count = 0;
    for (const auto& ally : allies) {
        bool foe = SocialFoe(ally.name);
        bool met = false;
        for (const auto& event : state_.memory.Events()) {
            if (event.detail != ally.name) continue;
            if (event.kind == "attacked_by_player") foe = true;
            if (event.kind == "hunt_companion") met = true;
        }
        if (foe) continue;
        ++count;
        if (!announce || ally.name.empty()) continue;
        social::Remember(state_.memory.relationships, ally.name, social::Encounter::Help, obs.nowMs);
        if (!met)
            state_.memory.NoteEvent("hunt_companion", ally.name.c_str(), "shared hunt",
                                    obs.x, obs.y, obs.nowMs);
        if (obs.nowMs - huntSocialMs_ >= 120000) {
            huntSocialMs_ = obs.nowMs;
            LogLine("hunt: assisting %s against target 0x%08X", ally.name.c_str(), target);
            client.ActionSay(met ? "Good to hunt with you again. I'll help with this one."
                                 : "I'll help with this one. Let's hunt together.");
        }
    }
    return count;
}

bool Runner::DoTrainCombat(Client& client, const Observation& obs) {
    // Something is already here: finish that fight. This is how every
    // character trains, hunter or not.
    // A FIGHT IN PROGRESS IS DEFENCE. A fight to pick is not.
    //
    // This used to hand EVERY sighting to DoSurvive, which answers "what is
    // most likely to kill me" and attacks the nearest reachable thing. That is
    // right when something is already swinging and wrong when choosing whom to
    // start on -- and it meant the whole M6 layer (Classify, ChooseTarget,
    // ChoosePrey and 54 unit tests) was called by nothing at all.
    if (obs.underAttack || obs.attackersOnMe > 0) return DoSurvive(client, obs);

    // THE FIGHT THIS GOAL WENT OUT FOR IS OVER AND IT WAS WON.
    //
    // Until now DoTrainCombat had no success return a fighter or a caster
    // could ever reach -- the single `return true` below is gated on
    // "neither hunts nor casts" -- so a character with four confirmed kills
    // reported nothing, and the anti-spin backstop cooled the goal off for
    // "completing with progress 0" (fleet-100, 60+/98 characters;
    // artifacts/fleet100_triage_2026-09-06.md Cause A).
    //
    // The kill was recorded by ProcessHuntAftermath, which runs before the
    // goal is chosen and cannot know whose trip it was; this is the goal
    // that walked to the graveyard, so this is where it counts. Reached
    // only once the corpse is dealt with: while looting is in progress
    // ProcessHuntAftermath owns the tick and RunGoal is never called.
    if (huntKillsPending_ > 0) {
        const i32 killed = huntKillsPending_;
        huntKillsPending_ = 0;
        huntEmptyArrivals_ = 0;
        huntUnderstockAuthorized_ = false;
        ClearHuntEngageState();
        for (i32 i = 0; i < killed; ++i) planner_.NoteProgress();
        LogLine("hunt: %d confirmed kill(s) this trip -- combat training done "
                "for now", killed);
        return true;
    }
    if (needCfg_.profession && needCfg_.profession->combatStrategy == CombatStrategyId::Ranged &&
        market::QtyOf(obs.pack, "i_arrow") < 20) {
        const bool stocked = market::QtyOf(obs.bank, "i_arrow") > 0;
        return HandOffFromHunt(stocked ? GoalKind::Bank : GoalKind::Craft,
                       30000, stocked ? "withdraw ammunition before hunting"
                                      : "fletch ammunition before hunting", obs.nowMs);
    }
    const bool caster = needCfg_.profession && WantsSpellCombat(*needCfg_.profession);
    // Local line-of-sight is an optimistic preflight: the server can still
    // reject a moving target after we have reached what looked like a firing
    // tile.  Client classifies Source-X reach/LOS text as Rejected; reagent,
    // mana, and spell failures are ServerFailure, so this is specifically a
    // target-position verdict.  Consume the result before candidate selection
    // so the same target cannot receive another cast on the next tick.
    if (caster) {
        // Prefer the refusal captured at packet time.  The completed-action
        // fallback keeps synthetic and older action paths working.
        u32 refused = client.TakeSpellReachRefusalTarget();
        if (refused == 0 && !client.ActionBusy() &&
            client.ActionKind() == act::Kind::CastSpell &&
            client.ActionResult() == act::Result::Rejected)
            refused = client.CurrentAction().destination;
        if (refused != 0 && refused != client.PlayerSerial()) {
            if (casterReachRefusedTarget_ != refused) {
                LogLine("hunt: server refused line of sight to 0x%08X -- "
                        "trying another target", refused);
                casterReachRefusedTarget_ = refused;
            }
            MarkUnreachable(refused, obs.nowMs);
            if (huntApproachTarget_ == refused) huntApproachTarget_ = 0;
            if (currentFoe_ == refused) currentFoe_ = 0;
        }
    }
    int attackSpell = -1;
    if (caster) {
        if (obs.spellbookSerial && !client.ContainerKnown(obs.spellbookSerial)) {
            if (!client.ActionBusy()) {
                client.ActionOpenContainer(obs.spellbookSerial);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 2000;
            }
            return false;
        }
        attackSpell = PickSurvivalSpell(client, obs, false);
        if (attackSpell < 0) {
            const int known = PickSurvivalSpell(client, obs, false, false);
            const spell::SpellDef* spell = spell::DefForSpell(known);
            // SAY WHAT THE BOOK GAVE. Aurelius (2026-09-05 10:34) walked to the
            // graveyard with an empty pack and no hand-off in the log, which
            // this line would have explained in one read.
            LogLine("hunt: caster attack spell -- supplied=%d known=%d (%s) book=0x%08X",
                    attackSpell, known, spell ? spell->name : "none",
                    obs.spellbookSerial);
            if (spell) {
                // NOT WHAT ONE RUNG COSTS, NOR EVEN WHAT THIS LADDER COSTS:
                // what a mage carries. Shopping the current ladder still left
                // holes -- a book gains rungs as Magery grows, and the reagent
                // for the rung it is about to learn is the one it never has.
                // The standing band covers all eight (owner ruling 2026-09-06,
                // ReagentBandShortfall above).
                std::vector<std::string> missing;
                i32 bandGap = 0;
                ReagentBandShortfall(obs, missing, &bandGap);
                if (obs.gold <= 100) {
                    // Restore one usable attack before trying to fund the
                    // standing eight-reagent band with a nearly empty purse.
                    missing.clear();
                    for (const char* reagent : spell->reagents) {
                        if (reagent && reagent[0] && market::QtyOf(obs.pack, reagent) == 0)
                            missing.emplace_back(reagent);
                    }
                    bandGap = 1;
                }
                if (!missing.empty()) {
                    // THE BANK BEFORE THE SHOP. Aurelius had 138-146 of every
                    // reagent in the box and none in the pack; buying more is
                    // what a player who forgot the bank does, not a plan.
                    bool banked = true;
                    for (const std::string& r : missing)
                        if (market::QtyOf(obs.bank, r) <= 0) banked = false;
                    if (banked) {
                        return HandOffFromHunt(GoalKind::Bank, 30000,
                                       "withdraw attack-spell reagents before hunting",
                                       obs.nowMs);
                    }
                    reagentWants_ = missing;
                    // The band, capped by what the purse can actually pay for
                    // across this many kinds at the price this character has
                    // seen -- PlanReagentBuy's own affordability arm, given the
                    // band as its target instead of a per-session cast count.
                    i32 unit = 0;
                    if (const market::PriceObservation* pr = state_.prices.Latest(
                            missing.front().c_str(), market::PriceSource::NpcVendorSells))
                        unit = pr->pricePerUnit;
                    const auto shopping = spell::PlanReagentBuy(
                        0, bandGap, unit, obs.gold, static_cast<int>(missing.size()));
                    reagentWantQty_ = std::max(1, shopping.buy);
                    std::string list;
                    for (const std::string& r : reagentWants_)
                        list += (list.empty() ? "" : ", ") + r;
                    LogLine("hunt: reagent band short -- wants %d of each of [%s] "
                            "(%s)", reagentWantQty_, list.c_str(), shopping.why);
                    return HandOffFromHunt(GoalKind::BuySupplies, 60000,
                                   "restock the reagent band through the existing supply errand",
                                   obs.nowMs);
                }
                if (obs.mana < spell->mana && obs.hostilesNear == 0 && !client.ActionBusy()) {
                    client.ActionUseSkill(rules::kMeditation);
                    nextActionMs_ = obs.nowMs + 6000;
                    return false;
                }
            }
            return HandOffFromHunt(GoalKind::FillSpellbook, 60000,
                           "no castable attack spell: fill the spellbook before hunting",
                           obs.nowMs);
        }
    }
    if (state_.huntReturnPending) {
        return HandOffFromHunt(GoalKind::Bank, 10000,
                       "secure the last hunt's loot before starting another", obs.nowMs);
    }
    // Readiness gates apply to a target already in view as well as to travel.
    if (obs.HpFraction() < needCfg_.healHpFraction) {
        return HandOffFromHunt(GoalKind::Heal, 10000,
                       "recover before opening another fight", obs.nowMs);
    }
    if (obs.WeightFraction() >= BankWeightLine(needCfg_)) {
        state_.huntReturnPending = true;
        return HandOffFromHunt(GoalKind::Bank, 10000,
                       "make room for loot before fighting", obs.nowMs);
    }
    if (obs.bandages == 0 && obs.healPotions == 0 &&
        PickSurvivalSpell(client, obs, true) < 0) {
        return HandOffFromHunt(GoalKind::ReplaceEquipment, 30000,
                       "restock healing supplies before the next fight", obs.nowMs);
    }
    // THE OWNER'S FLOOR: a fighting life carries at least bandageLow (100 for
    // a hunter, ResolveConsumableThresholds) before it goes looking for a
    // fight (2026-09-05). A market trip that cannot fit the session is not
    // evidence that three bandages are safe; it is the signal to start the
    // self-supply branch before fighting.
    //
    // ONE DEPARTURE LINE, WHEREVER THE BOT IS STANDING.  The need model
    // already classifies stock between the field line and desired full stock
    // as a small top-up and lets work outrank it. This handler used a second,
    // town-only rule of `bandageLow`, so Hector held 75 bandages (the field
    // line for a 100 desired stock), selected TRAIN_COMBAT, then immediately
    // abandoned it just because he was in Britain. The bank/market/gear
    // detour repeated until combat spun and yielded no kills. A real player
    // does not queue for another 25 bandages before a safe hunt; the ordinary
    // restock need can top up on the next town errand.
    const i32 bandageGateLow = needCfg_.bandageFieldLow;
    if (!huntUnderstockAuthorized_ &&
        needCfg_.profession && WantsToHunt(*needCfg_.profession) &&
        obs.bandages < bandageGateLow &&
        life::WantsConsumable(needCfg_, "bandage")) {
        const bool replacementResting =
            planner_.Cooling(GoalKind::ReplaceEquipment, obs.nowMs);
        if (!replacementResting) {
            return HandOffFromHunt(GoalKind::ReplaceEquipment, 30000,
                           "below the bandage floor -- restock before the next fight",
                           obs.nowMs);
        }
        return HandOffFromHunt(GoalKind::MakeBandages, 30000,
                       "the replacement errand is resting -- self-supply before "
                       "the next fight",
                       obs.nowMs);
    }

    if (FollowHuntingParty(client, obs)) return false;

    // Do not open a fight while an earlier errand is still walking us toward
    // its destination.  The food run proved why: the planner selected combat
    // during the provisioner's final approach, attacked a town animal, then
    // the still-live travel action pulled the character back toward the
    // counter.  Finish the movement before making a combat decision.
    if (client.TravelBusy()) return false;

    // Preparation comes before selecting a new target.  A character may defend
    // itself above, but it must not initiate a town fight or graveyard trip
    // until it has a weapon and basic armour.
    if (!caster && !obs.weaponEquipped) {
        return HandOffFromHunt(GoalKind::ReplaceEquipment,
                       kGearCooldownMs, "no gear yet -- shopping before the "
                       "graveyard", obs.nowMs);
    }
    // ARMOUR IS A PREFERENCE THE SHOP CAN REFUSE. THE HUNT IS NOT OPTIONAL.
    //
    // This handoff was unconditional, and it is the measured reason four
    // fighters fought nothing in two waves on 2026-09-03: TRAIN_COMBAT was
    // selected, handed itself straight to the gear errand, and cooled for four
    // minutes -- every single time it won the scoring.
    //   run_gates/g_Titus.console.txt:673,684 -- "TRAIN_COMBAT 84.5 superseded
    //   UPGRADE_GEAR 22.0" then, 1.9 s later, "goal=UPGRADE_GEAR reason=
    //   previous goal abandoned: no gear yet -- shopping before the graveyard".
    //   Same pair in g_Hector.console.txt:156,164 and g_Castor.console.txt:767.
    // The gear errand then failed on its own terms ("no 'armorer' reachable
    // after 3 trips", "every slot this class may fill is filled"), so the loop
    // had no exit and session_summary read kills=0 for all four.
    //
    // The owner's rule stands -- "they need gear too" (2026-08-31) -- so the
    // shop is still tried FIRST. What changes is that a shop which has already
    // stood its own errand down (UPGRADE_GEAR sets its own cooldown on every
    // failure path, Gear.cpp:1895,1961 and the vendor-unreachable path) no
    // longer gets asked again by proxy. A fighter with a weapon, bandages and
    // no armour available to buy goes hunting in what it is wearing, which is
    // what a player with 8k gold and an empty armoury does.
    if (!caster && !HasBasicArmor(client, obs)) {
        if (!planner_.Cooling(GoalKind::UpgradeGear, obs.nowMs)) {
            // kShortRestMs, NOT kGearCooldownMs. The handoff cools the goal it
            // leaves, and four minutes is an entire session on this shard's
            // gates: a fighter that stepped aside to buy a breastplate could
            // not come back to the hunt afterwards even when the shopping
            // finished in thirty seconds. The rest only has to be long enough
            // for the gear errand to be picked instead.
            return HandOffFromHunt(GoalKind::UpgradeGear,
                           kShortRestMs, "no gear yet -- shopping before the "
                           "graveyard", obs.nowMs);
        }
        LogLine("hunt: no basic armour, and the armour errand has already stood "
                "itself down -- going as I am rather than standing here");
    }

    const wm::Region* combatRegion = client.CurrentRegion();
    const bool inGuardedRegion = combatRegion && combatRegion->flags.guarded;

    // Notoriety alone is not a reason to begin training inside town.  Gray
    // wildlife and other lawful mobiles can appear there, but a normal new
    // warrior goes to an unguarded hunting ground for deliberate fights.
    // Self-defence remains handled above regardless of region.
    if (obs.hostilesNear > 0 && !inGuardedRegion && !client.ActionBusy()) {
        std::vector<Client::HostileHit> seen;
        client.ScanHostiles(12, seen);
        // The weak Britain graveyard band and the knight/lich rings share a
        // region and can sit inside one hostile scan.  A novice who merely
        // filters by creature danger can still chase a nominally weak target
        // north through the ring, where the strong creatures aggro on the
        // way.  Keep both the board count and the prey list inside the
        // atlas-provided weak band while this life is training there.
        const bool novicePolicy =
            novice::IsNovice(BestWeaponSkillTenths(obs), obs.hpMax);
        const auto* atlas = client.WorldAtlas();
        const wm::Place* weakBand = novicePolicy && atlas
            ? atlas->NearestHuntingGroundOfTier(world_atlas::HuntTier::Weak, obs.x, obs.y, 64) : nullptr;
        if (weakBand && TileDist(obs.x, obs.y, weakBand->position.x,
                                 weakBand->position.y) <= weakBand->radius + 2) {
            std::vector<Client::HostileHit> withinWeakBand;
            withinWeakBand.reserve(seen.size());
            for (const Client::HostileHit& h : seen) {
                if (TileDist(h.x, h.y, weakBand->position.x,
                             weakBand->position.y) <= weakBand->radius)
                    withinWeakBand.push_back(h);
            }
            const usize outside = seen.size() - withinWeakBand.size();
            if (outside) {
                LogLine("hunt: ignoring %zu hostile(s) outside the novice weak "
                        "band while at %s", outside, weakBand->name.c_str());
                seen.swap(withinWeakBand);
            }
        }
        if (!seen.empty()) {
            // Seeing several creatures is normal at a graveyard.  ChoosePrey
            // scores the weakest and least-grouped one; "one at a time" means
            // issuing one opening attack and never selecting another while an
            // attacker exists, not waiting for the whole screen to contain
            // exactly one mobile (which left Hector standing until a pack
            // attacked him first).
            std::vector<combat::Candidate> cands;
            cands.reserve(seen.size());
            for (const Client::HostileHit& h : seen) {
                if (IsUnreachable(h.serial, obs.nowMs)) continue;
                if (IsHuntExcluded(h.serial)) continue;
                if (h.name.empty()) {
                    client.RequestMobileStatus(h.serial);
                    continue; // Identify prey before applying creature danger.
                }
                combat::Candidate c;
                c.serial = h.serial;
                c.name   = h.name;
                // The notoriety BYTE and the Noto enum agree value for value --
                // 1 blue, 2 green, 3/4 gray, 5 orange, 6 red, 7 yellow
                // (Client.h:644, combat.h:28-37) -- so this is a mapping,
                // not a guess.
                c.noto   = static_cast<combat::Noto>(h.noto);
                c.dist   = TileDist(h.x, h.y, obs.x, obs.y);
                c.hpCur  = h.hpCur;
                c.hpMax  = h.hpMax;
                c.warMode = h.warMode;
                c.attackingMe = client.IsAttackingMe(h.serial);
                c.friendlySupport = HuntSupport(client, obs, h.serial);
                cands.push_back(std::move(c));
            }
            if (cands.empty()) {
                // ALL EXCLUDED, NOT MERELY UNREACHABLE. If every mobile
                // ScanHostiles saw is on huntExcludedThisTrip_ (each already
                // given up on after kMaxHuntEngageTries), waiting 4 s and
                // re-scanning only repeats the same exclusion forever --
                // that exclusion is trip-scoped, not the 30 s unreachable_
                // window below, so it will not clear on its own. The goal
                // must end instead of spinning.
                bool allGivenUp = !seen.empty();
                for (const Client::HostileHit& h : seen) {
                    if (!IsHuntExcluded(h.serial)) { allGivenUp = false; break; }
                }
                if (allGivenUp) {
                    LogLine("goal_failed=TRAIN_COMBAT reason=\"every visible "
                            "hostile was given up on after %d tries each\"",
                            kMaxHuntEngageTries);
                    client.EnsurePeaceMode();
                    planner_.Cooldown(GoalKind::TrainCombat,
                                      obs.nowMs + kHuntStandDownMs);
                    planner_.Finish(false, "no huntable target left this trip",
                                    obs.nowMs);
                    nextActionMs_ = obs.nowMs + 4000;
                    return false;
                }
                LogLine("hunt: every visible hostile is on the recent retreat "
                        "list -- not re-engaging");
                client.EnsurePeaceMode();
                nextActionMs_ = obs.nowMs + 4000;
                return false;
            }
            // ONE AT A TIME, AND NOT IN A CROWD (owner rule, 2026-09-04):
            // "don't fight where 3+ hostiles are within reach". This is a
            // decision about the BOARD, taken before any target is scored --
            // ChoosePrey only penalises a candidate that has company, so it
            // will still hand back the least-bad skeleton in a yard holding
            // four things that can reach us. A player looks at that yard and
            // leaves. Counted at kCrowdRadius (combat.h:194), the same radius
            // the scorer calls company.
            {
                int inReach = 0;
                int nearby  = 0;
                for (const Client::HostileHit& h : seen) {
                    const i32 d = TileDist(h.x, h.y, obs.x, obs.y);
                    if (d <= combat::kCrowdRadius) ++inReach;
                    if (d <= novice::kSoloRadius) ++nearby;
                }
                // A NOVICE ONLY FIGHTS DUELS (2026-09-06). Two plain skeletons
                // are enough to kill a 51-hp fencer at ~50 weapon skill, twice
                // in one day, so below the novice band the board must hold one
                // hostile and nothing within joining distance of it. See
                // NoviceEngage.h and artifacts/novice_engagement_2026-09-06.md
                // for the hp-per-exchange rows this comes from.
                // WHO IS SWINGING, NOT WHO IS STANDING NEAR. Counting reach
                // refused a duel next to a bystander: Hector picked a Cougar
                // at 9 tiles and a Spectre at 10, a second skeleton drifted
                // inside four on the walk in, and the break-off fired at 100%
                // health with attackers=1 -- five times, zero fights, in ten
                // minutes (g_Hector.console.txt:108,128,632,1106,1127,1129).
                // `nearby` is still counted and logged, because a pack at
                // joining distance is worth seeing in the trace, but the veto
                // is the owner's 3+ within reach and the attacker count.
                i32 support = 0;
                for (const auto& c : cands) support = std::max(support, c.friendlySupport);
                const bool crowded = !novice::GroupMayOpen(obs.attackersOnMe, inReach, support);
                // AND A BUILD THAT PLANS NO COMBAT SKILL NEVER OPENS AT ALL.
                if (!BuildFightsAtAll(needCfg_.profession)) {
                    LogLine("engage=no in_reach=%d near=%d hp=%.0f%% "
                            "reason=\"this build plans no combat skill\"",
                            inReach, nearby, obs.HpFraction() * 100.0);
                    client.EnsurePeaceMode();
                    planner_.Cooldown(GoalKind::TrainCombat,
                                      obs.nowMs + kHuntStandDownMs);
                    planner_.Finish(false, "this build does not fight", obs.nowMs);
                    nextActionMs_ = obs.nowMs + 4000;
                    return false;
                }
                if (crowded) {
                    ++huntGroundRotation_;
                    huntCrowdMoves_ = 0;
                    LogLine("engage=no in_reach=%d near=%d novice=%d hp=%.0f%% "
                            "reason=\"%d hostile(s) within %d tiles, %d within "
                            "%d -- cannot take them one at a time\"",
                            inReach, nearby, novicePolicy ? 1 : 0,
                            obs.HpFraction() * 100.0, inReach,
                            combat::kCrowdRadius, nearby, novice::kSoloRadius);
                    // Remember the spot, so the ground picker above and the
                    // corpse run both learn this yard is busier than this
                    // character can handle.
                    state_.memory.NoteDanger(obs.x, obs.y, 12,
                                             cands.front().name.c_str(), 0.5,
                                             obs.nowMs);
                    client.EnsurePeaceMode();
                    planner_.Cooldown(GoalKind::TrainCombat,
                                      obs.nowMs + kHuntStandDownMs);
                    planner_.Finish(false, "too crowded to open a fight",
                                    obs.nowMs);
                    nextActionMs_ = obs.nowMs + 4000;
                    return false;
                }
            }
            combat::Stance me;
            // REGION_FLAG_GUARDED, straight from the atlas -- there is no
            // Observation field for it and inventing one would just cache a
            // fact the world model already answers.
            me.inGuardedRegion = false;
            me.attackersOnMe   = obs.attackersOnMe;

            // What this life has LEARNED about these creatures, so a lich it
            // died to last session is not "weak and alone" today.
            const Memory& mem = state_.memory;
            const i64 now = obs.nowMs;
            // Learned first, seeded second. Experience beats a stat block.
            LoadSeededCreatureDanger(client.DataDir());
            const combat::CreatureDangerLookup danger =
                [&mem, now](const std::string& n) {
                    const double learned = mem.CreatureDanger(n.c_str(), now);
                    if (learned > 0.0) return learned;
                    const double seeded = SeededDangerFor(n);
                    return seeded >= 0.0 ? seeded : 0.0;
                };

            // WHAT KIND OF THING EACH ONE IS.  The shard-derived prior owns
            // the hard tier: it keeps a knight out of a novice hunt even when
            // the character has no history with one.  Learned creature heat
            // is deliberately NOT maxed into that number: it is a signed,
            // unbounded-confidence scale, and doing so made an old costly
            // Skeleton encounter turn a .038 weak-band creature into 2.08,
            // permanently refusing every legal target.  `danger` above still
            // uses that history to rank legal prey away from costly types.
            for (combat::Candidate& cc : cands) {
                const double seeded = SeededDangerFor(cc.name);
                cc.speciesDanger = combat::EligibilitySpeciesDanger(
                    seeded, mem.CreatureDanger(cc.name.c_str(), now));
            }

            // The profession's nerve is the tolerance the legality layer
            // gates on. Left at the struct default (0.50) every fencer and
            // macer refused a skeleton that had turned to face it: warMode
            // + adjacent + full bar = 0.60, over 0.50, "none worth starting
            // on" -- while Professions.cpp said 0.70-0.75 all along.
            combat::EngagePolicy policy;
            if (needCfg_.profession)
                policy.riskTolerance = Nerve(needCfg_);
            // AND WHAT KIND OF THING THIS CHARACTER MAY OPEN ON. Under 60.0
            // that is the weak band and nothing else -- the rule the four
            // 2026-09-07 knight deaths cost (combat.h, SpeciesCeiling).
            policy.maxSpeciesDanger =
                combat::SpeciesCeiling(BestFightSkillTenths(obs));
            const int prey = combat::ChoosePrey(cands, me, combat::RevolutionCrimeRules(),
                                                policy, obs.HpFraction(), danger);
            if (prey >= 0) {
                const combat::Candidate& c = cands[static_cast<usize>(prey)];
                // GIVE UP BEFORE RE-ATTACKING, NOT AFTER. ChoosePrey returns
                // the same best candidate every tick this section runs
                // (i.e. whenever underAttack/attackersOnMe are both zero --
                // see the DoSurvive hand-off at the top of this function), so
                // a target that is unreachable or simply never fights back
                // was attacked and re-attacked with nothing to show for it
                // (fleet122c30_20260907, "hunt: picked 'Spectre'" x28, zero
                // swings). Checked here, before ActionAttack, not after.
                if (HuntEngageTries(c.serial) >= kMaxHuntEngageTries) {
                    // AN ATTEMPT ONLY COUNTS AGAINST THE TARGET WHEN NOTHING
                    // WAS EXCHANGED (owner brief 2026-09-07, live wave
                    // fleet122d30_20260907): "giving up on 'Chickadee'" and
                    // "giving up on 'Rat'" excluded two animals that fled or
                    // died while taking real damage and simply never got in
                    // range to hit back -- correct for Spectre (zero swings
                    // either way), wrong for them. Source-X sends no
                    // per-swing packet (0x2F fires once per fight; see
                    // OnMobileHp/OnMobileAttributes), so the target's own
                    // health bar dropping since the FIRST attack on this
                    // serial is the only observable proof a hit landed. If
                    // it has, the budget resets instead of writing the
                    // target off.
                    const double startFrac = HuntEngageStartHp(c.serial);
                    const double curFrac = (c.hpCur >= 0 && c.hpMax > 0)
                        ? static_cast<double>(c.hpCur) / c.hpMax : -1.0;
                    const bool exchanged = startFrac >= 0.0 && curFrac >= 0.0 &&
                                           (startFrac - curFrac) >= 0.05;
                    if (exchanged) {
                        LogLine("hunt: '%s' 0x%08X is taking damage without "
                                "retaliating (%.0f%%->%.0f%% hp) -- still "
                                "worth pressing", c.name.c_str(), c.serial,
                                startFrac * 100.0, curFrac * 100.0);
                        ResetHuntEngageTries(c.serial);
                    } else {
                        LogLine("hunt: giving up on '%s' 0x%08X after %d "
                                "tries (no exchange) -- next target",
                                c.name.c_str(), c.serial,
                                HuntEngageTries(c.serial));
                        MarkHuntExcluded(c.serial);
                        if (currentFoe_ == c.serial) currentFoe_ = 0;
                        nextActionMs_ = obs.nowMs + 500;
                        return false;
                    }
                }
                const combat::Classification v =
                    combat::Classify(c, me, combat::RevolutionCrimeRules(), policy,
                                     obs.HpFraction());
                // PRINT THE VERDICT. R2's whole point is that a kill is
                // exercised THROUGH the legality layer, not around it, and a
                // verdict nobody logs is a verdict nobody can check.
                LogLine("hunt: picked '%s' at %d tiles -- verdict=%s threat=%.2f "
                        "species=%.2f ceiling=%.2f learned_danger=%.2f (%s)",
                        c.name.c_str(), c.dist, combat::VerdictName(v),
                        v.threat, c.speciesDanger, policy.maxSpeciesDanger,
                        mem.CreatureDanger(c.name.c_str(), obs.nowMs),
                        v.reason.c_str());
                LogLine("engage=yes target='%s' dist=%d hp=%.0f%% bandages=%d "
                        "reason=\"one target, board is not crowded\"",
                        c.name.c_str(), c.dist, obs.HpFraction() * 100.0,
                        obs.bandages);
                if (caster) {
                    // Source-X rejects a spell target behind a wall, but the
                    // old hunter kept retrying that same cast every 2.5 s.
                    // A player closes to a firing position first.  Give one
                    // route a chance, then suppress this target briefly if
                    // that route cannot make it visible; this leaves the
                    // picker free to choose another reachable hostile.
                    if (huntApproachTarget_ == c.serial) {
                        if (client.GotoBusy()) {
                            nextActionMs_ = obs.nowMs + 1000;
                            return false;
                        }
                        huntApproachTarget_ = 0;
                        if (!client.MobileInLineOfSight(c.serial)) {
                            LogLine("hunt: cannot get line of sight to '%s' "
                                    "after approach -- trying another target",
                                    c.name.c_str());
                            MarkUnreachable(c.serial, obs.nowMs);
                            nextActionMs_ = obs.nowMs + 500;
                            return false;
                        }
                    }
                    if (!client.MobileInLineOfSight(c.serial)) {
                        constexpr i32 kCasterFiringDistance = 3;
                        LogLine("hunt: no line of sight to '%s' -- moving "
                                "into a firing position", c.name.c_str());
                        if (!client.ActionGotoMobile(c.serial, kCasterFiringDistance)) {
                            LogLine("hunt: cannot route to '%s' for line of "
                                    "sight -- trying another target", c.name.c_str());
                            MarkUnreachable(c.serial, obs.nowMs);
                            nextActionMs_ = obs.nowMs + 500;
                            return false;
                        }
                        huntApproachTarget_ = c.serial;
                        nextActionMs_ = obs.nowMs + 1000;
                        return false;
                    }
                    const int poison = PickPoisonOpener(client, obs);
                    casterReachRefusedTarget_ = 0;
                    client.ActionCastSpell(poison >= 0 ? poison : attackSpell, c.serial);
                    if (poison >= 0) poisonOpenedTarget_ = c.serial;
                }
                else {
                    client.ActionAttack(c.serial);
                    const i32 range = needCfg_.profession &&
                        needCfg_.profession->combatStrategy == CombatStrategyId::Ranged
                            ? 6 : 1;
                    // Attack sends intent, not movement. A target that does
                    // not retaliate never reaches DoSurvive's chase branch.
                    if (c.dist > range && !client.GotoBusy())
                        client.ActionGotoMobile(c.serial, range);
                }
                // First write wins (SetHuntEngageStartHp): this anchors the
                // exchange check above to the hp reading at the START of the
                // current try budget, not whatever the bar reads on a later
                // retry.
                SetHuntEngageStartHp(c.serial, c.hpCur >= 0 && c.hpMax > 0
                    ? static_cast<double>(c.hpCur) / c.hpMax : -1.0);
                BumpHuntEngageTries(c.serial);
                HuntSupport(client, obs, c.serial, true);
                currentFoe_ = c.serial;
                currentFoeName_ = c.name;
                // TRAIN_COMBAT opens the fight, but SURVIVE owns it as soon
                // as the target retaliates.  Initialise the shared fight
                // window here; waiting for DoSurvive to see a *different*
                // serial left fightStartedMs_ at zero and made the very first
                // assessment look millions of seconds old.
                fightStartedMs_ = obs.nowMs;
                chaseBestDist_ = c.dist;
                chaseProgressMs_ = obs.nowMs;
                foeHpAtStart_ = c.hpCur >= 0 && c.hpMax > 0
                                    ? static_cast<double>(c.hpCur) / c.hpMax
                                    : -1.0;
                foeHpAskedMs_ = obs.nowMs;
                client.RequestMobileStatus(c.serial);
                huntEmptyArrivals_ = 0;
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            LogLine("hunt: %zu hostile(s) in sight and none worth starting on",
                    seen.size());
            // Say WHY for the nearest one, or the refusal is unverifiable.
            {
                usize closest = 0;
                for (usize i = 1; i < cands.size(); ++i)
                    if (cands[i].dist < cands[closest].dist) closest = i;
                const combat::Classification v =
                    combat::Classify(cands[closest], me, combat::RevolutionCrimeRules(),
                                     policy, obs.HpFraction());
                LogLine("hunt:   nearest '%s' at %d tiles -- verdict=%s threat=%.2f "
                        "species=%.2f ceiling=%.2f tolerance=%.2f (%s)",
                        cands[closest].name.c_str(), cands[closest].dist,
                        combat::VerdictName(v), v.threat,
                        cands[closest].speciesDanger, policy.maxSpeciesDanger,
                        policy.riskTolerance, v.reason.c_str());
            }
        }
        // No acceptable prey here: continue the search instead of waiting at the gate.

    }
    if (obs.hostilesNear > 0 && !inGuardedRegion && client.ActionBusy()) {
        // DoSurvive may report that there is presently nothing to defend
        // against.  That is not successful combat training and must not mark
        // TRAIN_COMBAT complete merely because an opened target has not yet
        // retaliated or has briefly left the mobile cache.
        return false;
    }
    if (obs.hostilesNear > 0 && inGuardedRegion) {
        LogLine("hunt: ignoring %d lawful hostile(s) in guarded town; heading "
                "to a hunting ground", obs.hostilesNear);
    }

    // NOTHING HERE. Until now that was the end of it -- "return true" -- and
    // it is why M6 has never once been exercised live: the layer that decides
    // what may legally be attacked was never given anything to decide about.
    // A fighter with no fight in reach should go and find one.
    if (!needCfg_.profession || (!WantsToHunt(*needCfg_.profession) && !caster)) return true;

    // GEAR UP BEFORE THE FIRST HUNT. "hunting ground can be graveyards for
    // early hunting, brit sewers maybe, but they need gear too" (project
    // owner, 2026-08-31). Affordability is not the issue -- a fresh fighter
    // Not while hurt, and not while loaded: the goal scorer already docks
    // both, but arriving at a graveyard at half health is a death rather than
    // a lesson, and that is a decision this goal should make for itself.
    //
    // FINISH(FALSE) WITH A COOLDOWN, NOT "return true". Returning true here
    // reports the goal COMPLETE -- Tick() calls planner_.Finish(true, ...)
    // right after -- so nothing happened and the planner is told it
    // succeeded. w_Kaelen logged this exact line into a five-times-in-five-
    // seconds spin (run_r4/w_Kaelen.console.txt:620-710, 25/32 health every
    // time) because Finish(true) carries no cooldown and TRAIN_COMBAT was the
    // very next need re-picked. A goal that decided not to act did nothing,
    // and "did nothing" stands down (goal-that-did-nothing-must-stand-down),
    // it does not report success.
    // 80 WAS A SECOND COPY OF needCfg_.healHpFraction (0.80, life.h) --
    // the same line DoTrainCombat's own readiness gate above and DecideHeal
    // both read. One number, one place.
    if (obs.HpFraction() < needCfg_.healHpFraction) {
        LogLine("hunt: %d/%d health -- not going looking for a fight",
                obs.hp, obs.hpMax);
        planner_.Cooldown(GoalKind::TrainCombat, obs.nowMs + kHuntStandDownMs);
        planner_.Finish(false, "too hurt to go looking for a fight", obs.nowMs);
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    // THE HUNT WEIGHT LINE, not a literal: needCfg_.huntWeightFrac (0.70,
    // life.h) is the same number NeedTraining reads when it asks whether this
    // life can hunt at all (Needs.cpp), so the need and the goal agree.
    if (obs.WeightFraction() >= needCfg_.huntWeightFrac) {
        LogLine("hunt: carrying too much to fight (%.0f%%)",
                obs.WeightFraction() * 100.0);
        planner_.Cooldown(GoalKind::TrainCombat, obs.nowMs + kHuntStandDownMs);
        planner_.Finish(false, "carrying too much to fight", obs.nowMs);
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    if (client.TravelBusy()) return false;
    if (!travelInFlight_) {
        // A TRIP IS A JOURNEY THAT WAS ACTUALLY STARTED. This counter used to
        // rise on every pass through here, including passes that never picked
        // a ground and passes that failed to start a walk, so Aurelius failed
        // "no hunting ground reachable after 4 trips" at 00:20:08 with no
        // graveyard travel planned since 00:19:04 -- the intervening plans
        // were a healer and a vendor (D9,
        // artifacts/validation_wave_2026-09-06.md). It is now incremented
        // below, only where travelInFlight_ actually became true, and reset
        // by HandOffFromHunt whenever this goal is left for another kind.
        if (huntTrips_ >= kMaxHuntTrips) {
            LogLine("goal_failed=TRAIN_COMBAT reason=\"no hunting ground "
                    "reachable after %d trips\"", huntTrips_);
            planner_.Cooldown(GoalKind::TrainCombat,
                              obs.nowMs + kNoHuntingGroundCooldownMs);
            planner_.Finish(false, "no hunting ground reachable", obs.nowMs);
            huntTrips_ = 0;
            nextActionMs_ = obs.nowMs + 60000;
            return false;
        }
        // WHICH GROUND, AND WHY. Nearest is not the same question as
        // survivable: only weak patrol areas outside strong-ring buffers.
        const i32 weaponTenths = BestFightSkillTenths(obs);
        const auto* atlas = client.WorldAtlas();
        const wm::Place* ground = nullptr;
        if (socialGroupUntilMs_ && socialPeer_ && client.PartyContains(socialPeer_) &&
            client.PartyLeader() != client.PlayerSerial()) {
            // The follower fights locally but never chooses a competing destination.
            nextActionMs_ = obs.nowMs + 1000;
            return false;
        }
        if (atlas) {
            // Try each yard at most once; personal danger still rules it out.
            const wm::Place* first = nullptr;
            for (usize tries = 0; tries < atlas->Places().size(); ++tries) {
                ground = atlas->GroupHuntingGround(client.PlayerSerial(), huntGroundRotation_);
                if (ground && ground == first) { ground = nullptr; break; }
                if (!first) first = ground;
                if (!ground || state_.memory.DangerHeatAt(ground->position.x,
                        ground->position.y, obs.nowMs) <= kHuntGroundHeatLimit) break;
                ++huntGroundRotation_;
                ground = nullptr;
            }
        }
        std::string huntPlace;
        if (ground) {
            const auto points = atlas->HuntingPatrol(*ground);
            if (!points.empty()) {
                const auto& point = points[(client.PlayerSerial() / 2 + huntPatrolStep_++) % points.size()];
                LogLine("hunt_ground=%s band=weak weapon=%.1f rotation=%zu reason=\"distributed group hunting\"",
                        ground->name.c_str(), weaponTenths / 10.0, huntGroundRotation_);
                travelInFlight_ = client.TravelToPoint(point.x, point.y, 2, "group graveyard patrol");
                if (travelInFlight_) huntPlace = ground->name;
                else ++huntGroundRotation_;
            }
        }
        if (!ground) {
            planner_.Cooldown(GoalKind::TrainCombat, obs.nowMs + kNoHuntingGroundCooldownMs);
            planner_.Finish(false, "no suitable weak graveyard", obs.nowMs);
            nextActionMs_ = obs.nowMs + 30000;
            return false;
        }
        if (travelInFlight_) {
            LogLine("hunt: heading to %s to train (trip %d)",
                    huntPlace.c_str(), ++huntTrips_);
        } else {
            LogLine("goal_blocked=TRAIN_COMBAT reason=\"%s\"",
                    client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
        }
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    travelInFlight_ = false;
    if (*client.TravelFailureText()) {
        ++huntGroundRotation_;
        huntCrowdMoves_ = 0;
        LogLine("hunt: rotating after failed journey: %s", client.TravelFailureText());
        nextActionMs_ = obs.nowMs + 1000;
        return false;
    }
    huntTrips_ = 0;
    ClearHuntEngageState();
    // ARRIVING IS NOT TRAINING, AND AN EMPTY YARD MUST END SOMEWHERE.
    //
    // huntTrips_ is cleared by arriving, so the "no hunting ground reachable"
    // bound above can never fire for a character that keeps arriving at a
    // ground with nothing in it: it patrols the next point, arrives again,
    // and spends the session doing that. This is the other half of the same
    // rule -- a goal that cannot find prey FAILS with a reason instead of
    // completing with nothing (Faustus, x11 "nothing is here", fleet-100).
    // The allowance is the same kMaxHuntTrips journeys the travel side gets,
    // and it is cleared the moment a fight is opened or a kill is credited.
    ++huntGroundRotation_;
    if (++huntEmptyArrivals_ >= kMaxHuntTrips) {
        huntCrowdMoves_ = 0;
        LogLine("goal_failed=TRAIN_COMBAT reason=\"nothing to fight after %d "
                "arrivals at a hunting ground\"", huntEmptyArrivals_);
        // Transient, not structural: the yard repopulates, so this is the
        // short stand-down (RunnerInternal.h kHuntStandDownMs), not the
        // three-minute one the atlas failure takes.
        planner_.Cooldown(GoalKind::TrainCombat, obs.nowMs + kHuntStandDownMs);
        planner_.Finish(false, "nothing to fight at the hunting ground",
                        obs.nowMs);
        huntEmptyArrivals_ = 0;
        huntUnderstockAuthorized_ = false;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    // Arrived. Ask what is here; the targeting layer judges legality, and a
    // graveyard's dead are the one thing on this shard that is always lawful
    // to swing at.
    LogLine("hunt: arrived at %d,%d -- looking for something to fight",
            client.PlayerX(), client.PlayerY());
    client.ActionScanMobiles();
    if (!state_.memory.HasEvent("first_hunt")) {
        state_.memory.NoteEvent("first_hunt", "went hunting", "graveyard",
                                obs.x, obs.y, obs.nowMs);
    }
    nextActionMs_ = obs.nowMs + 3000;
    return false;
}


// ---------------------------------------------------------------------------
// STAT_FARM -- the Wrestling detour, which is how a caster's STR moves at all.
//
// Owner order, 2026-09-04: "STR needed -> temporarily train Wrestling -> spar
// bare-handed (NPC/player/dummy) -> when the STR target is reached, set the
// Wrestling lock DOWN and let the real mage skills fill its points."
//
// The four shard facts it rests on are cited on AssessStatFarm in life.h. The
// two that shape THIS function:
//
//   * a training dummy refuses a rider, refuses a ranged weapon skill, and
//     refuses Wrestling above SkillPracticeMax=300 -- but below that every
//     double-click is a Skill_Experience roll, and every roll is a stat roll
//     (Source-X CCharUse.cpp:349-405, CCharSkill.cpp:3508);
//   * hitting an NPC is a crime, and a speaking witness in a guarded region
//     calls guards with GuardsInstantKill=1 (CCharFight.cpp:24-99,:1474). So
//     this errand NEVER swings at an NPC and never opens a fight inside a
//     guarded region. What it may punch is a hostile the ordinary targeting
//     layer has already judged legal.
//
// TARGET ORDER, exactly as briefed:
//   (a) a training dummy, while Wrestling is under 30.0;
//   (b) a low-danger hostile already in view, outside a guarded region;
//   (c) a consenting bot partner -- NOT IMPLEMENTED. There is no party or
//       consent mechanism in this client today (no ActionPartyInvite, no
//       spar handshake), and inventing one here would be a second market
//       protocol hidden inside a training errand. Design note:
//       .claude/agent-memory/revolution-god/sparring-parties.md.
//
// KNOWN LIMIT, stated rather than papered over: (a) only finds a dummy the
// server has already sent us, i.e. one within view. There are 55 in the world
// save (tools/world_query.py --count i_training_dummy) but no route to one --
// the nearest to the Britain bank is 81 tiles away and on an upper floor.
// Until dummies are a routable place, a character in the middle of a town
// will report "nothing here to spar with" and stand the errand down.
// ---------------------------------------------------------------------------

namespace {
// i_training_dummy and its DUPELIST (runtime/scripts/items/i_profession.scp:
// 1168-1200): 01070 plus 01071..01077, which are the facing and animation
// frames. The id on the wire is usually not the base one.
constexpr u16 kTrainingDummy[] = {0x1070, 0x1071, 0x1072, 0x1073,
                                  0x1074, 0x1075, 0x1076, 0x1077};
}  // namespace

bool Runner::BeginStatFarm(Client& client, const Observation& obs) {
    if (!statFarmLocksSent_) {
        statFarmLocksSent_ = true;
        statFarmActive_ = true;
        // NOT the "do not lock early" build policy (MaintainBuildLocks) --
        // that rule is about freezing a PLANNED skill before the cap binds.
        // This is an explicit, temporary farming configuration, and it is
        // logged as one so the two are never confused in a run log.
        //
        // DEX LOCKED is the point of the whole arrangement: Wrestling pulls
        // DEX toward 75 (skill43_wrestling.scp STAT_DEX=75) and a caster
        // wants nothing of the sort. Source-X skips any stat whose lock is
        // not UP (CCharSkill.cpp Skill_Experience), so the lock is a real
        // brake and not a preference.
        client.ActionSetSkillLock(static_cast<u16>(rules::kWrestling),
                                  kStatFarmLocks.wrestling);
        client.ActionSetStatLock(0, kStatFarmLocks.str);
        client.ActionSetStatLock(1, kStatFarmLocks.dex);
        client.ActionSetStatLock(2, kStatFarmLocks.intel);
        statLockSent_[0] = static_cast<u8>(kStatFarmLocks.str + 1);
        statLockSent_[1] = static_cast<u8>(kStatFarmLocks.dex + 1);
        statLockSent_[2] = static_cast<u8>(kStatFarmLocks.intel + 1);
        LogLine("stat_farm: locks STR=UP DEX=LOCK INT=UP wrestling=UP "
                "(farming step, not a build lock)");
        // Durable, because the EXIT is not run by this goal: once STR is
        // reached the need falls silent, so the Wrestling-DOWN is done by the
        // lock keeper, which needs to know a farm ever happened.
        if (!state_.memory.HasEvent("stat_farm_started")) {
            state_.memory.NoteEvent("stat_farm_started",
                                    "began farming STR by wrestling", "",
                                    obs.x, obs.y, obs.nowMs);
        }
        nextActionMs_ = obs.nowMs + 800;
        return true;
    }

    // FISTS. Fight_GetWeaponSkill returns Wrestling only with both hands
    // empty, and the dummy refuses a ranged skill outright -- so a fisher's
    // pole or a warlock's katana has to go in the pack first. The serial is
    // remembered so the errand can hand it back.
    if (client.ActionBusy()) return true;
    for (u8 layer : {kLayerHand1, kLayerHand2}) {
        const u32 worn = client.EquippedAtLayer(layer);
        if (!worn) continue;
        if (!statFarmStowedWeapon_) statFarmStowedWeapon_ = worn;
        LogLine("stat_farm: putting 0x%04X away -- wrestling is bare-handed",
                client.EquippedGraphicAt(layer));
        client.ActionUnequip(worn);
        nextActionMs_ = obs.nowMs + 1400;
        return true;
    }
    return false;
}

void Runner::EndStatFarm(Client& client, const Observation& obs) {
    if (!statFarmActive_) return;
    statFarmActive_ = false;
    statFarmLocksSent_ = false;
    statFarmSwings_ = 0;
    statFarmWrestlingAtSwing_ = -1;
    statFarmStrAtSwing_ = -1;

    // DEX back to what the BUILD wants. The lock was a brake on the sparring,
    // never a decision about the character.
    const u8 dex = StatFarmDexLockOnExit(state_.plan, obs);
    client.ActionSetStatLock(1, dex);
    statLockSent_[1] = static_cast<u8>(dex + 1);
    LogLine("stat_farm: standing down -- DEX lock back to %s",
            dex == build::kLockLocked ? "LOCK" : "UP");

    if (statFarmStowedWeapon_) {
        LogLine("stat_farm: taking the weapon back up");
        client.ActionEquip(statFarmStowedWeapon_, kLayerServerChooses);
        statFarmStowedWeapon_ = 0;
    }
}

bool Runner::DoStatFarm(Client& client, const Observation& obs) {
    const StatFarmPlan sf = AssessStatFarm(state_.plan, obs);

    // Nothing to farm any more -- STR reached, or the plan's own skills can
    // still carry it. Either way this errand is over; the Wrestling-DOWN is
    // the lock keeper's job (MaintainBuildLocks), not this goal's, because by
    // then the need has already fallen silent and the goal is never picked.
    if (!sf.wanted) {
        LogLine("stat_farm: nothing to farm -- %s (str %d/%d ceiling %d)",
                sf.why, sf.have, sf.target, sf.ceiling);
        EndStatFarm(client, obs);
        planner_.Cooldown(GoalKind::StatFarm, obs.nowMs + kStatFarmStandDownMs);
        planner_.Finish(false, "no STR left to farm", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // HP FLOOR. Half health and the sparring stops: a caster with 0.0
    // Wrestling and no armour dies to the thing it started on. Heal owns the
    // recovery -- bandages, potion or spell, whichever this life actually has.
    if (obs.hp * 100 < obs.hpMax * 50) {
        LogLine("stat_farm: %d/%d health -- breaking off", obs.hp, obs.hpMax);
        client.EnsurePeaceMode();
        EndStatFarm(client, obs);
        return HandOff(GoalKind::StatFarm, GoalKind::Heal, kShortRestMs,
                       "under half health while sparring", obs.nowMs);
    }
    // Something is already swinging at us: that is defence, and DoSurvive
    // owns it. The stat rolls happen either way.
    if (obs.attackersOnMe > 0) return DoSurvive(client, obs);
    if (client.TravelBusy()) return false;

    if (BeginStatFarm(client, obs)) return false;

    // Did the last swing actually move anything the server reports? Progress
    // is a skill or stat that CHANGED, never "we swung again".
    if (statFarmWrestlingAtSwing_ >= 0) {
        const bool moved = sf.wrestlingTenths > statFarmWrestlingAtSwing_ ||
                           obs.str > statFarmStrAtSwing_;
        if (moved) {
            LogLine("stat_farm: str=%d/%d wrestling=%.1f (was %.1f) after %d "
                    "swing(s)", obs.str, sf.target, sf.wrestlingTenths / 10.0,
                    statFarmWrestlingAtSwing_ / 10.0, statFarmSwings_);
            planner_.NoteProgress();
        }
    }

    const wm::Region* region = client.CurrentRegion();
    const bool guarded = region && region->flags.guarded;

    // --- (a) a training dummy ---------------------------------------------
    //
    // Legal anywhere -- a dummy is furniture, not a mobile, so no crime is
    // seen and no guard is called. Only while Wrestling is under
    // SkillPracticeMax, because above it the dummy refuses and the swing is
    // simply a wasted double-click.
    if (sf.useDummy && !obs.mounted) {
        u32 dummy = 0;
        for (u16 g : kTrainingDummy) {
            dummy = client.FindWorldItemByGraphic(g, 16);
            if (dummy) break;
        }
        if (dummy) {
            i32 dx = 0, dy = 0;
            client.WorldItemPosition(dummy, &dx, &dy);
            if (TileDist(dx, dy, obs.x, obs.y) > 1) {
                if (!travelInFlight_) {
                    LogLine("stat_farm: target=dummy at %d,%d -- stepping up to "
                            "it (str=%d/%d wrestling=%.1f)", dx, dy, obs.str,
                            sf.target, sf.wrestlingTenths / 10.0);
                    travelInFlight_ = client.TravelToEntity(dummy, 1);
                    if (!travelInFlight_) {
                        LogLine("stat_farm: cannot reach the dummy -- \"%s\"",
                                client.TravelFailureText());
                        planner_.NoteAttempt(obs.nowMs);
                    }
                }
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            travelInFlight_ = false;
            if (client.ActionBusy()) return false;
            LogLine("stat_farm: target=dummy str=%d/%d wrestling=%.1f",
                    obs.str, sf.target, sf.wrestlingTenths / 10.0);
            LogLine("stat_farm: swing");
            statFarmWrestlingAtSwing_ = sf.wrestlingTenths;
            statFarmStrAtSwing_ = obs.str;
            ++statFarmSwings_;
            client.ActionUseObject(dummy);
            planner_.NoteAttempt(obs.nowMs);
            // Source-X animates the dummy for 3 s and refuses an early
            // re-click, so anything shorter is a wasted packet.
            nextActionMs_ = obs.nowMs + 3200;
            return false;
        }
    } else if (sf.useDummy && obs.mounted) {
        LogLine("stat_farm: mounted -- a dummy refuses a rider "
                "(CCharUse.cpp:361), looking for something else");
    }

    // --- (b) a low-danger hostile already in view --------------------------
    //
    // Never inside a guarded region and never an NPC: both are crimes with a
    // witness, and this shard kills criminals on sight. What is left is what
    // ChoosePrey already judges legal -- the same targeting layer TRAIN_COMBAT
    // uses, with this life's own risk tolerance.
    if (!guarded && obs.hostilesNear > 0 && !client.ActionBusy()) {
        std::vector<Client::HostileHit> seen;
        client.ScanHostiles(10, seen);
        std::vector<combat::Candidate> cands;
        cands.reserve(seen.size());
        for (const Client::HostileHit& h : seen) {
            if (IsUnreachable(h.serial, obs.nowMs)) continue;
            combat::Candidate c;
            c.serial = h.serial;
            c.name   = h.name;
            c.noto   = static_cast<combat::Noto>(h.noto);
            c.dist   = TileDist(h.x, h.y, obs.x, obs.y);
            c.hpCur  = h.hpCur;
            c.hpMax  = h.hpMax;
            c.warMode = h.warMode;
            c.attackingMe = client.IsAttackingMe(h.serial);
            cands.push_back(std::move(c));
        }
        if (!cands.empty()) {
            combat::Stance me;
            me.inGuardedRegion = false;
            me.attackersOnMe   = obs.attackersOnMe;
            const Memory& mem = state_.memory;
            const i64 now = obs.nowMs;
            LoadSeededCreatureDanger(client.DataDir());
            const combat::CreatureDangerLookup danger =
                [&mem, now](const std::string& n) {
                    const double learned = mem.CreatureDanger(n.c_str(), now);
                    if (learned > 0.0) return learned;
                    const double seeded = SeededDangerFor(n);
                    return seeded >= 0.0 ? seeded : 0.0;
                };

            // Use the calibrated species tier for this hard gate; a learned
            // outcome remains a ranking signal through `danger` above.  The
            // two values have different scales, so maxing them would let an
            // old costly encounter make every familiar weak creature illegal.
            for (combat::Candidate& cc : cands) {
                const double seeded = SeededDangerFor(cc.name);
                cc.speciesDanger = combat::EligibilitySpeciesDanger(
                    seeded, mem.CreatureDanger(cc.name.c_str(), now));
            }

            combat::EngagePolicy policy;
            if (needCfg_.profession)
                policy.riskTolerance = Nerve(needCfg_);
            policy.maxSpeciesDanger =
                combat::SpeciesCeiling(BestFightSkillTenths(obs));
            // HALVED on purpose. Bare-handed at Wrestling 0.0 is not the same
            // character that picked this fight with a sword, and the errand is
            // worth exactly nothing if it gets the caster killed.
            policy.riskTolerance *= 0.5;
            const int prey = combat::ChoosePrey(cands, me,
                                                combat::RevolutionCrimeRules(),
                                                policy, obs.HpFraction(), danger);
            if (prey >= 0) {
                const combat::Candidate& c = cands[static_cast<usize>(prey)];
                LogLine("stat_farm: target=%s str=%d/%d wrestling=%.1f",
                        c.name.c_str(), obs.str, sf.target,
                        sf.wrestlingTenths / 10.0);
                LogLine("stat_farm: swing");
                statFarmWrestlingAtSwing_ = sf.wrestlingTenths;
                statFarmStrAtSwing_ = obs.str;
                ++statFarmSwings_;
                client.ActionAttack(c.serial);
                HuntSupport(client, obs, c.serial, true);
                currentFoe_ = c.serial;
                currentFoeName_ = c.name;
                fightStartedMs_ = obs.nowMs;
                chaseBestDist_ = c.dist;
                chaseProgressMs_ = obs.nowMs;
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
        }
    }

    // --- (c) a consenting partner -- see the header note. Not implemented.

    // NOTHING TO PUNCH. A goal that did nothing stands down; it does not
    // report success (goal-that-did-nothing-must-stand-down).
    LogLine("goal_blocked=STAT_FARM reason=\"nothing here to spar with\" "
            "guarded=%d hostiles=%d dummy_ok=%d", guarded ? 1 : 0,
            obs.hostilesNear, sf.useDummy ? 1 : 0);
    EndStatFarm(client, obs);
    planner_.Cooldown(GoalKind::StatFarm, obs.nowMs + kStatFarmStandDownMs);
    planner_.Finish(false, "nothing here to spar with", obs.nowMs);
    nextActionMs_ = obs.nowMs + 5000;
    return false;
}


// ---------------------------------------------------------------------------
// TRAIN_AT_NPC -- buy a skill the way a player does.
//
//   travel to a trade NPC -> ask who is here -> say "train <skill>"
//   -> READ THE PRICE THE NPC QUOTES -> hand over exactly that in gold
//   -> verify against the server's own skill number
//
// The price is never computed. Source-X answers with
// "For %d gold I will train you in all I know of %s" (defmessages.tbl
// NPC_TRAINER_PRICE) and the bot reads that line. If the shard retunes
// NPCTrainCost the bot follows without being told, and a refusal
// ("I know nothing about that", "You already know as much as I can teach")
// is read as a refusal rather than timed out.
//
// Nothing here sets a skill. The gold leaves the pack by an ordinary
// lift-and-drop onto the trainer, and the proof of training is the server's
// skill value afterwards.
// ---------------------------------------------------------------------------
bool Runner::DoTrainAtNpc(Client& client, const Observation& obs) {
    const int skillId = obs.wantTrainSkill;
    // NOTHING TO BUY IS NOT AN ERRAND DONE. Returning true here reported
    // `goal_completed=TRAIN_AT_NPC progress=0` and freed the planner to hand
    // the same goal straight back; standing the goal down is what stops that.
    if (skillId < 0) {
        planner_.Cooldown(GoalKind::TrainAtNpc, obs.nowMs + kNoTrainerCooldownMs);
        planner_.Finish(false, "no skill is waiting on a trainer", obs.nowMs);
        return false;
    }

    const i32 have = client.PlayerSkillBase(static_cast<u16>(skillId));

    // --- did the gold we handed over actually buy anything? ---------------
    //
    // The proof is the SERVER'S skill number, not our own bookkeeping. Asking
    // for a fresh skill list is what a player's client does anyway.
    if (trainPaid_) {
        // DID THE GOLD ACTUALLY BUY ANYTHING? Section 18's train rule --
        // "skill changed, or the trainer definitively refused" -- and the
        // judgement now lives in activities/TrainConfirm.cpp, routed through
        // interaction/progress.h. `training_unverified` is the name this
        // project gave to skipping it: GOLD_DESTROYED_TRAINER a dozen times
        // in one fleet run with no confirmed gain to show for any of it.
        TrainConfirmInput tin;
        tin.skillBefore = trainSkillBefore_;
        tin.skillNow    = have;
        tin.goldBefore  = trainGoldBefore_;
        tin.goldNow     = obs.gold;
        tin.quoted      = trainQuoted_;
        tin.msSincePaid = obs.nowMs - trainPaidMs_;
        tin.skillsAsked = trainSkillsAsked_;
        const TrainConfirmResult tconf = ConfirmTraining(tin);
        if (tconf.verdict == TrainVerdict::Learned) {
            // THE ONE PLACE THIS GOAL MAY CLAIM PROGRESS, and it was missing:
            // even a lesson that worked reported `goal_completed=TRAIN_AT_NPC
            // progress=0`, which is the exact signal the anti-spin backstop
            // reads as "succeeded at nothing".
            planner_.NoteProgress();
            LogLine("train: %s %.1f->%.1f bought from a trainer",
                    rules::SkillName(skillId), trainSkillBefore_ / 10.0, have / 10.0);
            state_.memory.NoteEvent("skill_trained", rules::SkillName(skillId),
                                    trainerTrade_.c_str(), obs.x, obs.y,
                                    obs.nowMs);
            // Remember WHERE, so the next skill this life buys does not start
            // the search from nothing. Recorded only because we dealt with it.
            KnownSupplier sup;
            sup.need = std::string("trainer:") + trainerTrade_;
            sup.name = trainerTrade_;
            sup.sourceType = "npc_trainer";
            sup.x = obs.x; sup.y = obs.y; sup.z = obs.z;
            sup.observedPricePerUnit = trainQuoted_;
            sup.lastVerifiedMs = obs.nowMs;
            sup.policyAllows = true;
            state_.memory.NoteSupplier(sup);
            // The other half of the verdict: this trade DOES teach this skill,
            // and here is what it charged. Worth as much as the refusal.
            TrainerVerdict v;
            v.skillId  = skillId;
            v.trade    = trainerTrade_;
            v.npcSerial = trainerSerial_;
            v.taught   = true;
            v.atTenths = have;
            v.quoted   = trainQuoted_;
            v.why      = "taught";
            v.whenMs   = obs.nowMs;
            state_.memory.NoteTrainerVerdict(v);
            trainPaid_ = false;
            trainAsked_ = false;
            trainSkillsAsked_ = false;
            trainPackRefreshed_ = false;
            trainPayAttempts_ = 0;
            trainTrips_ = 0;
            Checkpoint(client, obs.nowMs, "skill bought from a trainer");
            return true;
        }
        if (tconf.verdict == TrainVerdict::AskForSkills) {
            client.ActionRequestSkills();
            trainSkillsAsked_ = true;
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        if (tconf.verdict == TrainVerdict::FeeTakenNoLesson ||
            tconf.verdict == TrainVerdict::NoAnswer) {
            const bool feeTaken =
                tconf.verdict == TrainVerdict::FeeTakenNoLesson;
            LogLine("train: refused %s -- paid %d for %s "
                    "(purse %+d, skill %+.1f)", tconf.reason, trainQuoted_,
                    rules::SkillName(skillId), tconf.check.goldDelta,
                    tconf.check.skillDelta / 10.0);
            state_.memory.NoteEvent(
                feeTaken ? "training_took_fee_taught_nothing"
                         : "training_no_answer",
                rules::SkillName(skillId), trainerTrade_.c_str(),
                obs.x, obs.y, obs.nowMs);
            if (feeTaken) {
                // A TRAINER THAT TAKES THE FEE AND TEACHES NOTHING IS A FACT
                // ABOUT THAT TRAINER. Write HIM off, not the skill -- the
                // mistake that cost Ysolde Meditation entirely -- so the next
                // lesson is bought from somebody else.
                bool known = false;
                for (u32 k : trainerSilent_)
                    if (k == trainerSerial_) { known = true; break; }
                if (!known && trainerSerial_) trainerSilent_.push_back(trainerSerial_);
            }
            planner_.NoteAttempt(obs.nowMs);
            trainPaid_ = false;
            trainAsked_ = false;
            trainSkillsAsked_ = false;
            // Ask for the pack again: the most likely reason a give did
            // nothing is that the serial it named no longer exists.
            trainPackRefreshed_ = false;
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    // --- decide: done, buy from an NPC, or grind it (S2.4, DecideTrain) ----
    //
    // Whether this skill is worth an NPC's time at all: the catalogue's own
    // viaTrainer flag for this target, AND no trade-wide refusal has already
    // written it off (obs.trainerRefusedSkills; see Runner.cpp's need-assess
    // pass and Memory::TrainerSaysMaxed/TrainerRefused).
    bool worthBuying = true;
    for (usize i = 0; i < state_.plan.skills.size(); ++i) {
        if (state_.plan.skills[i].skillId != skillId) continue;
        worthBuying = (i < state_.plan.viaTrainer.size()) && state_.plan.viaTrainer[i];
        break;
    }
    for (int r : obs.trainerRefusedSkills) {
        if (r == skillId) { worthBuying = false; break; }
    }
    // What an NPC of this trade can teach: a durable TrainerVerdict for this
    // (skill, trade) outranks any guess -- it is what a trainer here already
    // SAID about its own ceiling. Absent one, train.h:19-23's own numbers:
    // 300 for a guildmaster, 225 otherwise, and a guildmaster only counts once
    // NearestGuildmasterForTrade actually finds one nearby.
    auto rememberedCeilingTenths = [&]() -> i32 {
        for (const TrainerVerdict& v : state_.memory.Trainers()) {
            if (v.skillId != skillId || v.trade != trainerTrade_) continue;
            if (v.taught) return v.atTenths;
            if (v.why == "the trainer has nothing left to give") return v.atTenths;
        }
        const bool guildmasterNearby =
            client.NearestGuildmasterForTrade(trainerTrade_.c_str(), trainerSilent_) != 0;
        return guildmasterNearby ? 300 : 225;
    };

    TrainRequest req1;
    req1.skillId = skillId;
    req1.targetTenths = obs.wantTrainTarget;
    req1.npcCeilingTenths = rememberedCeilingTenths();
    req1.feeQuoted = 0;   // no quote yet -- DecideTrain reads 0 as "go ask"
    req1.gold = obs.gold;
    req1.worthBuying = worthBuying;
    const TrainPlan plan1 = DecideTrain(req1, have);
    if (plan1.step != lastTrainPlan_) {
        LogPlan(TrainStepName(plan1.step), plan1.reason);
        lastTrainPlan_ = plan1.step;
    }
    if (plan1.step == TrainStep::Done) {
        // Same rule as before S2.4: the skill is where it should be, so
        // nothing was achieved and nothing needs to be. Rest the goal rather
        // than completing it -- `return true` here reported
        // `goal_completed=TRAIN_AT_NPC progress=0` and the planner handed the
        // same goal straight back (S3's fix; DecideTrain does not change it).
        planner_.Cooldown(GoalKind::TrainAtNpc, obs.nowMs + kNoTrainerCooldownMs);
        planner_.Finish(false, plan1.reason, obs.nowMs);
        return false;
    }
    if (plan1.step == TrainStep::Practise) {
        return HandOff(GoalKind::TrainAtNpc, GoalKind::PracticeSkill,
                       kNoTrainerCooldownMs, plan1.reason, obs.nowMs);
    }
    // TrainStep::Buy (CannotAfford is unreachable here -- feeQuoted is 0):
    // fall through to the unchanged travel/approach/ask machinery below.

    if (client.TravelBusy()) return false;

    // --- get to a trainer --------------------------------------------------
    // Skip anyone of this trade who has already been asked three times and
    // never said a word. Alenne the mage stood one tile away and answered
    // nothing about Meditation across two sessions and seven asks, while Alek
    // -- also "the mage", in a different Britain shop -- quoted 184 gold for
    // the same skill on the first ask. Why one answers and the other does not
    // is UNKNOWN; what a player does about it is not, and is what this does.
    // AND SKIP ANYONE WHO ALREADY REFUSED. Silence and refusal are different
    // answers with the same consequence for THIS NPC -- and, because the
    // ceiling is that NPC's own skill and not the trade's, the next one may
    // still teach it. Seeded from memory rather than kept in the transient
    // list, so a refusal survives the logout that earned it.
    for (u32 refused :
         state_.memory.TrainersWhoRefused(skillId, trainerTrade_.c_str())) {
        bool known = false;
        for (u32 k : trainerSilent_) { if (k == refused) { known = true; break; } }
        if (!known) trainerSilent_.push_back(refused);
    }
    // THE GUILDMASTER FIRST. Both the guildmaster and the shopkeeper of a
    // trade answer NearestMobileWithTrade -- it truncates "mage guildmaster"
    // to "mage" -- so it returns whoever is nearer, and in Britain's mage shop
    // that is always the shopkeeper. Alenne, two tiles away, refused Ysolde
    // Meditation at 21.9 on every visit while a mage guildmaster stood five
    // tiles off at 1490,1549 able to teach it (run_m5/p0gate7). Ask the one
    // whose job it is; fall back to the trade only when no guildmaster is in
    // reach, because an ordinary tradesman can still teach a low skill.
    u32 trainer = client.NearestGuildmasterForTrade(trainerTrade_.c_str(),
                                                    trainerSilent_);
    if (trainer && trainer != trainerSerial_) {
        LogLine("training: a '%s guildmaster' is here -- asking them rather "
                "than the shopkeeper", trainerTrade_.c_str());
    }
    if (!trainer) {
        trainer = client.NearestMobileWithTrade(trainerTrade_.c_str(),
                                                trainerSilent_);
    }
    if (!trainer) {
        if (trainTrips_ >= kMaxTrainTrips) {
            LogLine("goal_failed=TRAIN_AT_NPC reason=\"no '%s' reachable after "
                    "%d trips\"", trainerTrade_.c_str(), trainTrips_);
            state_.memory.NoteEvent("trainer_unreachable", trainerTrade_.c_str(),
                                    "", obs.x, obs.y, obs.nowMs);
            // STAND DOWN. Without this the goal was re-picked 60 ms later and
            // the whole three-trip count began again -- and every one of those
            // "trips" was a no-op, because the remembered trainer place is the
            // tile the character is already standing on: run_m5/p0gate9 logs
            // travel_start and ARRIVED at the same coordinate with legs=0,
            // three times, then goal_failed, then immediately goal=TRAIN_AT_NPC
            // again. Nothing about the world changes in two seconds.
            trainTrips_ = 0;
            // Three real trips and an explicit memory update changed what
            // this life knows. It is not a zero-work spin even though no
            // lesson was bought; the cooldown lets normal work take over.
            planner_.NoteProgress();
            return HandOff(GoalKind::TrainAtNpc, GoalKind::IdleBriefly,
                           kNoTrainerCooldownMs, "no trainer reachable",
                           obs.nowMs);
        }
        if (!travelInFlight_) {
            // A TRIP IS A JOURNEY THAT ACTUALLY ARRIVED SOMEWHERE (D9 rule,
            // the same one HandOffFromHunt states). This used to increment
            // HERE, before the walk was even planned, and then decrement
            // itself again on the stale-note branch -- so a trip that never
            // started, or a travel request the router refused, still spent
            // the allowance and the failure line blamed the world for three
            // journeys it had not made. The counter now rises once, at the
            // arrival branch below, where the character has genuinely stood
            // somewhere and found nobody of the trade.
            const KnownSupplier* known = state_.memory.BestSupplier(
                (std::string("trainer:") + trainerTrade_).c_str());
            if (known) {
                // STANDING ON THE SPOT AND SEEING NOBODY IS THE DISPROOF.
                //
                // A remembered supplier is a POSITION, not a mobile, and the
                // NPC that earned it can be gone -- despawned, re-rolled by a
                // spawner, or just wandered off. The old code walked back
                // regardless, which meant that when the trainer was missing it
                // issued a travel to the tile it was ALREADY on. Live:
                //
                //   arrived at (2629,2099,10)
                //   training: back to a trainer we have used before, 'carpenter' at 2629,2099
                //   training: back to a trainer we have used before, 'carpenter' at 2629,2099
                //   goal_failed=TRAIN_AT_NPC reason="no 'carpenter' reachable after 3 trips"
                //
                // Two of the three trips were spent travelling nowhere, two
                // seconds apart, and the goal then blamed the world. This is
                // the same shape ForgetPlace was written for -- belief that
                // survives being disproved is not memory, it is a loop.
                const i32 dToKnown = TileDist(obs.x, obs.y, known->x, known->y);
                if (dToKnown <= 3) {
                    LogLine("training: no '%s' where we remembered one at %d,%d "
                            "-- forgetting it and looking properly",
                            trainerTrade_.c_str(), known->x, known->y);
                    state_.memory.ForgetSupplier(
                        (std::string("trainer:") + trainerTrade_).c_str(),
                        known->x, known->y);
                    // Do not spend a trip on a lesson. Rescan from here first:
                    // the trade may be a few tiles off rather than absent.
                    client.ActionScanMobiles();
                    nextActionMs_ = obs.nowMs + 2000;
                    return false;
                }
                LogLine("training: back to a trainer we have used before, "
                        "'%s' at %d,%d", known->name.c_str(), known->x, known->y);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 2, "trainer");
            } else {
                LogLine("training: looking for a '%s' to teach %s (trip %d)",
                        trainerTrade_.c_str(), rules::SkillName(skillId),
                        trainTrips_ + 1);
                travelInFlight_ = client.TravelToServiceSkipping(
                    trainerService_, HomeOrNearest(state_.homeCity), trainerSilent_,
                    &trainerShopsTried_);
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=TRAIN_AT_NPC reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        // THE TRIP IS SPENT HERE, and only here. Arriving with no trainer in
        // sight is the one observation that says "this journey was wasted".
        ++trainTrips_;
        LogLine("training: arrived at %d,%d -- asking who is here (trip %d of %d)",
                client.PlayerX(), client.PlayerY(), trainTrips_, kMaxTrainTrips);
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // --- stand where a player would stand ---------------------------------
    //
    // Speech is heard by position, not by intent. The first live attempt at a
    // castle scribe stood 3 tiles away in 2D but SEVEN z below her -- another
    // floor -- and got a greeting but no training reply, three times, silently.
    // Close the distance in three dimensions before talking.
    if (trainerSerial_ != trainer) {
        // A DIFFERENT NPC, so the conversation starts over. Leaving
        // trainAsked_ set meant the bot skipped re-asking and then judged the
        // silence of an NPC it had never spoken to, reporting "the trainer
        // never answered" about a conversation that never happened.
        trainerSerial_ = trainer;
        trainerApproached_ = false;
        trainApproaches_ = 0;
        trainSilentAsks_ = 0;
        trainAsked_ = false;
        // AND THE SEARCH SUCCEEDED, so the travel allowance is handed back.
        // A trainer of this trade is standing in front of us; whatever the
        // conversation costs, it is not "no '%s' reachable". If this one is
        // later silenced or refuses, the next search starts from a full
        // allowance -- the same thing the refusal path below already does.
        trainTrips_ = 0;
    }
    if (!trainerApproached_) {
        i32 tx = 0, ty = 0; i8 tz = 0;
        if (client.MobilePosition(trainer, &tx, &ty, &tz)) {
            const i32 d = TileDist(obs.x, obs.y, tx, ty);
            const i32 dz = (obs.z > tz) ? (obs.z - tz) : (tz - obs.z);
            if (d > 2 || dz > 3) {
                LogLine("training: '%s' is %d tiles and %d z away -- walking to "
                        "them before speaking (approach %d of %d)",
                        trainerTrade_.c_str(), d, dz, trainApproaches_ + 1,
                        kMaxTrainApproaches);
                travelInFlight_ = client.TravelToEntity(trainer, 1);
                // ONE APPROACH WAS NOT ENOUGH, and the cost of that was a
                // whole session. Ysolde stood 7 tiles and 2 z from Alenne,
                // gave up closing after a single attempt, and then asked about
                // Meditation seven times from outside the shop. Every ask went
                // unheard, every timeout read as "the trainer never answered",
                // and the character concluded a perfectly good trainer was
                // useless. The very next session it walked all the way in,
                // asked once, and was quoted 219 gold.
                //
                // Speech is heard by POSITION. Keep closing until we are
                // actually in earshot, and only then talk regardless.
                if (++trainApproaches_ >= kMaxTrainApproaches) {
                    trainerApproached_ = true;
                }
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            trainApproaches_ = 0;
        }
        trainerApproached_ = true;
    }

    // --- ask, then read what the NPC actually says -------------------------
    if (!trainAsked_) {
        // TWO marks, on two clocks, because they answer different questions:
        // the journal mark says "read replies after this point", the tick mark
        // says "how long have I waited". Using the journal clock for both made
        // a 12-second window expire in 2.5 seconds.
        trainAskedMs_ = client.JournalNowMs();
        trainAskedTickMs_ = obs.nowMs;
        trainAsked_ = true;
        const char* const skillKey = SkillKey(skillId);
        if (!skillKey[0]) {
            // Never send the generic "train" command. It makes a trainer
            // list appear and looks like a real conversation in the log, but
            // it cannot quote a fee or teach a selected skill.
            LogLine("goal_failed=TRAIN_AT_NPC reason=\"no Sphere training key for %s\"",
                    rules::SkillName(skillId));
            planner_.Cooldown(GoalKind::TrainAtNpc,
                              obs.nowMs + kNoTrainerCooldownMs);
            planner_.Finish(false, "missing Sphere training key", obs.nowMs);
            trainAsked_ = false;
            return false;
        }
        LogLine("training: asking the trainer about %s (key %s, total gold %d, purse %d)",
                rules::SkillName(skillId), skillKey, obs.gold, obs.goldOnHand);
        client.ActionNpcTrain(trainer, skillKey);
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // Refusals first -- each is a real answer, not a timeout. The table lives
    // in activities/TrainConfirm.cpp so ctest can hold it to section 18's
    // "the trainer definitively refused".
    usize nRefusals = 0;
    const TrainerRefusal* refusals = TrainerRefusals(&nRefusals);
    for (usize ri = 0; ri < nRefusals; ++ri) {
        const TrainerRefusal& r = refusals[ri];
        if (!client.JournalSaidSince(r.text, trainAskedMs_)) continue;
        LogLine("train: refused %s -- '%s' will not teach %s at %.1f",
                r.why, trainerTrade_.c_str(), rules::SkillName(skillId),
                have / 10.0);
        // A DURABLE verdict, not a log line -- see uo/activities/
        // train_confirm.h for the session this cost when it was neither.
        TrainerVerdict v;
        v.skillId  = skillId;
        v.trade    = trainerTrade_;
        v.npcSerial = trainerSerial_;
        v.taught   = false;
        v.atTenths = have;
        v.why      = r.why;
        v.whenMs   = obs.nowMs;
        state_.memory.NoteTrainerVerdict(v);
        state_.memory.NoteEvent("trainer_refused", r.why, trainerTrade_.c_str(),
                                obs.x, obs.y, obs.nowMs);
        planner_.Finish(false, r.why, obs.nowMs);
        trainAsked_ = false;
        trainTrips_ = 0;
        Checkpoint(client, obs.nowMs, "trainer refusal");
        return false;
    }

    const i32 quoted = client.JournalNumberSince("i will train you", trainAskedMs_);

    // PAY WHOEVER ACTUALLY QUOTED -- the SPEAKER of the quote when one can be
    // identified, not the NPC that was addressed. Two tinkers stand together
    // in Minoc and the fee went to the one who had offered nothing; the log
    // that proves it is in uo/activities/train_confirm.h.
    u32 payee = trainer;
    if (quoted > 0) {
        std::vector<Client::Heard> said;
        client.JournalHeardSince(trainAskedMs_, said);
        for (const Client::Heard& h : said) {
            std::string low;
            for (char c : h.text)
                low.push_back(static_cast<char>(std::tolower(
                    static_cast<unsigned char>(c))));
            if (low.find("i will train you") == std::string::npos) continue;
            if (!h.speaker || h.speaker == trainer) break;
            LogLine("training: '%s' answered instead of the one asked -- "
                    "paying the trainer who actually quoted",
                    h.name.empty() ? "someone else" : h.name.c_str());
            payee = h.speaker;
            break;
        }
    }
    if (quoted <= 0) {
        if (obs.nowMs - trainAskedTickMs_ > 12000) {
            ++trainSilentAsks_;
            LogLine("training: no quote and no refusal after 12s (ask %d of %d)",
                    trainSilentAsks_, kMaxSilentAsks);
            planner_.NoteAttempt(obs.nowMs);
            trainAsked_ = false;
            // Silence is most often distance. Try to close it again before
            // repeating the same words from the same spot.
            trainerApproached_ = false;
            if (trainSilentAsks_ >= kMaxSilentAsks) {
                // Give up on this NPC for now. Deliberately NOT a trainer
                // verdict: a verdict is what an NPC SAID (train_confirm.h).
                LogLine("goal_failed=TRAIN_AT_NPC reason=\"'%s' never answered "
                        "about %s\"", trainerTrade_.c_str(),
                        rules::SkillName(skillId));
                state_.memory.NoteEvent("trainer_silent",
                                        rules::SkillName(skillId),
                                        trainerTrade_.c_str(), obs.x, obs.y,
                                        obs.nowMs);
                // Do not walk back to this same silent NPC next time. Held for
                // the session only, and not written to memory: silence is not
                // something the world told the character, so it is not a
                // belief -- it is just somewhere already tried today.
                bool listed = false;
                for (u32 sk : trainerSilent_) {
                    if (sk == trainerSerial_) { listed = true; break; }
                }
                if (!listed && trainerSerial_) {
                    trainerSilent_.push_back(trainerSerial_);
                }
                trainSilentAsks_ = 0;
                trainerSerial_ = 0;
                trainerApproached_ = false;
                // The silent NPC is now excluded for this session. That is a
                // useful result of the bounded investigation, not five
                // identical no-op training goals.
                planner_.NoteProgress();
                return HandOff(GoalKind::TrainAtNpc, GoalKind::IdleBriefly,
                               kNoTrainerCooldownMs, "the trainer never answered",
                               obs.nowMs);
            }
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // Call 2 (S2.4): the quote has landed, so DecideTrain now sees the real
    // fee instead of the "go ask" placeholder of 0. have/target/ceiling and
    // worthBuying have not changed since call 1 this tick -- only feeQuoted
    // has -- so only Buy and CannotAfford are actually reachable here.
    TrainRequest req2;
    req2.skillId = skillId;
    req2.targetTenths = obs.wantTrainTarget;
    req2.npcCeilingTenths = rememberedCeilingTenths();
    req2.feeQuoted = quoted;
    req2.gold = obs.gold;
    req2.worthBuying = worthBuying;
    const TrainPlan plan2 = DecideTrain(req2, have);
    if (plan2.step != lastTrainPlan_) {
        LogPlan(TrainStepName(plan2.step), plan2.reason);
        lastTrainPlan_ = plan2.step;
    }
    if (plan2.step == TrainStep::CannotAfford) {
        // NOT unbuyable -- too poor TODAY. No TrainerVerdict is written and
        // the skill is not marked refused: that is the whole point of this
        // step existing separately from Practise.
        state_.memory.NoteEvent("trainer_quote", rules::SkillName(skillId),
                                trainerTrade_.c_str(), obs.x, obs.y, obs.nowMs);
        trainAsked_ = false;
        return HandOff(GoalKind::TrainAtNpc, GoalKind::EarnGold, 60000,
                       plan2.reason, obs.nowMs);
    }
    if (plan2.step == TrainStep::Done || plan2.step == TrainStep::Practise) {
        // The quote itself changed nothing about have/target/ceiling, so this
        // is defensive rather than a path call 1 leaves live -- but the quote
        // is what finally proved the ceiling, so stand down rather than pay.
        return true;
    }
    // TrainStep::Buy: pay exactly what was quoted, below, unchanged.

    // --- pay exactly what was quoted ---------------------------------------
    //
    // Ask for the pack's contents FIRST: a give addressed to a serial Sphere
    // retired when it split the gold stack is a SILENT no-op
    // (uo/activities/train_confirm.h).
    if (!trainPackRefreshed_) {
        client.ActionOpenBackpack();
        trainPackRefreshed_ = true;
        nextActionMs_ = obs.nowMs + 1200;
        return false;
    }
    if (trainPayAttempts_ >= kMaxPayAttempts) {
        LogLine("goal_failed=TRAIN_AT_NPC reason=\"%d attempts to hand over %d "
                "gold all failed\"", trainPayAttempts_, quoted);
        planner_.Finish(false, "could not hand over the fee", obs.nowMs);
        trainAsked_ = false;
        trainPackRefreshed_ = false;
        trainPayAttempts_ = 0;
        return false;
    }
    // The fee has to be IN THE PACK. obs.gold counts the bank box on this
    // shard, so "can afford" and "can hand over" are different questions.
    if (FetchCoinForPurchase(client, obs, quoted)) return false;
    // Sphere trains only in proportion to the stack it receives.  A pack can
    // contain several coin stacks after banking or receiving change; choosing
    // the first one made Delys hand Caedmon five coins from a 495-gold purse
    // and receive only 0.5 skill from a quoted 29.5-point lesson.
    const u32 gold = client.FindBackpackItemByGraphicAtLeast(
        kGoldCoin, static_cast<u16>(quoted));
    if (!gold) {
        LogLine("training: quoted %d but no single gold stack can cover it", quoted);
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    LogLine("training: paying the quoted %d gold for %s (total gold %d, purse %d)",
            quoted, rules::SkillName(skillId), obs.gold, obs.goldOnHand);
    trainSkillBefore_ = have;
    // WHAT THE PURSE HELD BEFORE THE LESSON, so "the fee was taken and
    // nothing was taught" can be told apart from "the trainer refused and
    // kept nothing". Both used to log the same `training_unverified` line,
    // and they are completely different problems: the first is a trainer to
    // write off, the second a report that simply has not arrived yet.
    trainGoldBefore_ = obs.gold;
    trainQuoted_ = quoted;
    // The TICK clock, not the journal clock. These are different clocks and
    // mixing them made the ten-second verification window expire in 8.7s.
    trainPaidMs_ = obs.nowMs;
    trainSkillsAsked_ = false;
    ++trainPayAttempts_;
    client.ActionNpcGive(payee, gold, static_cast<u16>(quoted));
    trainPaid_ = true;
    nextActionMs_ = obs.nowMs + 4000;
    return false;
}

// --- WHAT IS IN A SPELLBOOK, READ THE WAY SPHERE ACTUALLY SENDS IT ----------
//
// A SPELLBOOK'S 0x3C IS NOT A LIST OF SCROLL ITEMS. Sphere synthesises one
// 19-byte record per spell in which the graphic is a CONSTANT and the SPELL
// NUMBER travels in the AMOUNT field
// (server/Source-X/src/network/send.cpp:1341-1358,
// PacketItemContents(const CClient*, const CItem* spellbook)):
//
//     for (int i = SPELL_Clumsy; i <= SPELL_MAGERY_QTY; ++i) {
//         if (!spellbook->IsSpellInBook((SPELL_TYPE)i)) continue;
//         writeInt32(UID_F_ITEM + UID_O_INDEX_FREE + i);  // synthetic serial
//         writeInt16(0x1F2E);                             // ALWAYS 0x1F2E
//         writeByte(0);
//         writeInt16((word)i);                            // <- the spell, 1..64
//         writeInt16(0); writeInt16(0);                   // x, y
//         writeInt32(spellbook->GetUID());
//         writeInt16(HUE_DEFAULT);
//     }
//
// (SPELL_Clumsy = 1, uofiles_enums.h:670. The 2.0.7 stream carries no grid
// byte, so the record is the 19-byte form Client::OnContainerContents already
// parses -- pol_packets.md's 0x3C, older-client loop. The 0x3C parse was and
// is correct; what was wrong was believing a book row's GRAPHIC meant
// anything.)
//
// Reading the graphic therefore reports "spell 1" for every row of every book:
// exactly the [1,1,1,1,1,1,1,1,1,1,1,1,1] Ilyandra's THIRTEEN-spell book
// produced (run_r4/w_Ilyandra.console.txt:501). The book was fine. The read
// was wrong -- and it made BookHasGraphic answer "already known" for a Clumsy
// scroll and "not known" for every other scroll on the shelf, which is the
// opposite of useful when the whole point is to buy a spell the book lacks.
bool Runner::BookHasSpell(Client& client, u32 book, int spell) const {
    if (!book || spell <= 0) return false;
    const usize n = client.ContainerItemCount(book);
    for (usize i = 0; i < n; ++i) {
        u32 serial = 0; u16 g = 0, amount = 0;
        if (!client.ContainerItemAt(book, i, &serial, &g, &amount)) continue;
        if (static_cast<int>(amount) == spell) return true;
    }
    return false;
}

// Is the spell this SCROLL teaches already in the book? Takes a scroll's own
// graphic -- what a vendor row or a pack item carries -- and asks the question
// in the book's own currency.
bool Runner::BookHasGraphic(Client& client, u32 book, u16 graphic) const {
    return BookHasSpell(client, book, SpellForScrollGraphic(graphic));
}

// Walk to a scroll seller, open it, and buy one thing.
//
// `graphic` of 0 means "any spell scroll". `skipKnown` refuses scrolls the
// book already holds, which is the difference between the two sellers on this
// shard and the whole point of preferring one:
//
//   a MAGE shop stocks random_first_circle .. random_fourth_circle, so what
//   arrives is not chosen and a duplicate is quite likely
//   a SCRIBE stocks 44 NAMED scrolls, so a specific missing spell can be asked
//   for and nothing is wasted on one already owned
//
// "mage should also give priority to buy new spells not on the book" (project
// owner) -- which is only possible at the scribe, so that is where this goes
// first. Returns false while still working.
// `owner` is the goal this purchase belongs to. It was hardcoded to
// FillSpellbook, so when the gear and scissors paths reused this helper their
// failures were logged as FILL_SPELLBOOK, cooled FILL_SPELLBOOK down, and
// shared its trip counter -- which is why a fencer produced 1,932 identical
// "buying armour" lines and a goal_failed=FILL_SPELLBOOK "no 'armorer'
// reachable". Three errands wearing one name.
// THE SCROLL ERRAND GIVING UP, in one place so every exit rests the same way.
//
// FILL_SPELLBOOK only. The gear and scissors errands borrow BuyScrollFrom and
// must keep their own four-minute rest: an armourer that was out of chainmail
// really may have some in four minutes, whereas the reason a mage found no
// scroll is that few sellers stock one at all.
i64 Runner::StandDownFromScrollShopping(const Observation& obs,
                                        const char* why) {
    ++scrollStandDowns_;
    const i64 rest = life::ScrollShoppingRestMs(scrollStandDowns_);
    LogLine("goal_failed=FILL_SPELLBOOK reason=\"%s\" -- %d empty scroll "
            "errand(s) running, resting %llds so practice, earning and "
            "training get the turn (the book is still wanted)",
            why, scrollStandDowns_, static_cast<long long>(rest / 1000));
    planner_.Cooldown(GoalKind::FillSpellbook, obs.nowMs + rest);
    scrollShopSinceMs_ = 0;
    scrollShopTickMs_  = 0;
    // The next errand starts its shop tour from scratch rather than resuming
    // half-way through an exhausted one.
    spellbookTrips_ = 0;
    spellbookSkipPlaces_.clear();
    spellbookSkipSellers_.clear();
    scribeExhausted_ = false;
    return rest;
}

bool Runner::BuyScrollFrom(Client& client, const Observation& obs,
                           const char* trade, wm::Service svc, u16 graphic,
                           bool skipKnown, u16 qty, const char* what,
                           GoalKind owner, u16 prefer) {
    if (client.TravelBusy()) return false;

    // TravelToService reaches a shop tile, not a named mobile.  This helper
    // used to arrive and immediately select the next atlas shop because its
    // title cache was still empty; a blacksmith could therefore be walked past
    // without ever being asked to open a trade window.  Finish the arrival
    // handshake once: scan paperdolls, let the normal cache update, then make
    // the seller decision on the next tick.
    if (travelInFlight_) {
        travelInFlight_ = false;
        LogLine("%s: arrived at a %s shop -- scanning nearby sellers",
                GoalKindName(owner), trade);
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // THE TRIP COUNTER BELONGS TO THE ERRAND, not to this helper. Three goals
    // share it, and without this a gear trip spent the spellbook's allowance
    // and vice versa -- the fencer's 1,932 identical "buying armour" lines
    // came out of exactly that confusion.
    if (buyTripsOwner_ != owner) {
        buyTripsOwner_ = owner;
        spellbookTrips_ = 0;
        spellbookSkipPlaces_.clear();
        spellbookSkipSellers_.clear();
    }
    // Armourers and blacksmiths share the atlas service but use different
    // paperdoll titles.  A newly arrived character may be standing in a
    // blacksmith shop before it has ever seen an "armorer", so use the smith
    // as an equivalent seller only for this armour errand.  The actual offer
    // is still checked against the requested graphic before any gold moves.
    //
    // EXCEPT FOR LEATHER. A blacksmith's shelf is ringmail through platemail
    // (VENDOR_S_ARMORER_RING/CHAIN/PLATE); only c_armorer carries
    // VENDOR_S_ARMORER_LEATHER. Castor (STR 32, legal for leather only) was
    // sent to blacksmith Guy, whose 65 rows held no leather, and burned the
    // whole session on "this 'armorer' has nothing the book lacks"
    // (run_gates/g_Castor.console.txt:614, 2026-09-03).
    const ArmorPiece* wantPiece = graphic ? ArmorFor(graphic) : nullptr;
    const bool wantLeather = wantPiece && wantPiece->cls == ArmorClass::Leather;
    // THE ATLAS TRIP GOES TO AN ARMOURY FOR EVERY ARMOUR CLASS. c_armorer
    // sells VENDOR_S_ARMORER_LEATHER, _RING, _CHAIN, _PLATE and _SHIELDS
    // (runtime/scripts/npcs/c_vendor_human.scp:646-650); the smithy carries
    // the metal rows too but is what the forge errand wants. With
    // armouryOnly=false the travel layer applies the forge rule -- "an
    // armoury is not a smithy" -- and strikes every armorer off the list, so
    // Castor (ringmail sleeves, 2026-09-05 02:26) walked past Britain's three
    // armourers and set out for the Jhelom blacksmith, 2,100 tiles and a
    // moongate away. "we have armor ... all at britain" (project owner).
    // The live-NPC lookup below still accepts a blacksmith standing nearby.
    const bool armourErrand = std::strcmp(trade, "armorer") == 0;
    // Service::None here means "the title must say armorer": the service
    // fallback inside NearestShopkeeperWithTrade otherwise accepts ANY
    // Blacksmith-service NPC -- which includes a weaponsmith, since
    // ServiceForPaperdollJob maps "blacksmith", "smith", "armourer",
    // "armorer" AND "weaponsmith" all onto wm::Service::Blacksmith
    // (ClientTravel.cpp kAliases). Passing `svc` (Blacksmith) here let a
    // weaponsmith stand in for the armorer we asked for -- Bae and Eulalia,
    // both weaponsmiths, were opened instead of Belora/Rudd the armourers
    // (run_gates/g_Castor.console.txt:72-92,508-601, 2026-09-05). Title
    // match must win for every armour class, leather included.
    const char* sellerTrade = trade;
    u32 keeper = client.NearestShopkeeperWithTrade(
        sellerTrade, armourErrand ? wm::Service::None : svc,
        &spellbookSkipSellers_);
    if (!keeper && !wantLeather && armourErrand) {
        // Second choice: a true blacksmith's shelf runs ring/chain/plate too.
        // Match the TITLE "blacksmith" exactly here as well -- `svc` would
        // let this fallback re-admit the same weaponsmith the pass above
        // just excluded.
        sellerTrade = "blacksmith";
        keeper = client.NearestShopkeeperWithTrade(sellerTrade,
                                                   wm::Service::None,
                                                   &spellbookSkipSellers_);
    }
    if (!keeper) {
        if (++spellbookTrips_ > kMaxSpellbookTrips) {
            spellbookTrips_ = 0;
            // A SELLER WE CANNOT REACH IS NOT THE END OF THE ERRAND.
            //
            // The scribe is preferred because only a scribe lets us choose the
            // spell, but plenty of towns have none. Falling back to the mage
            // shop -- a random scroll rather than a chosen one -- beats
            // failing the whole goal and standing down for four minutes.
            if (!scribeExhausted_ && std::strcmp(trade, "scribe") == 0) {
                LogLine("spellbook: no scribe reachable after 3 trips -- "
                        "falling back to a mage shop, where the scroll is "
                        "random rather than chosen");
                scribeExhausted_ = true;
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            if (owner == GoalKind::FillSpellbook) {
                StandDownFromScrollShopping(
                    obs, "no scroll seller reachable after 3 trips");
            } else {
                LogLine("goal_failed=%s reason=\"no '%s' reachable after 3 "
                        "trips\"", GoalKindName(owner), trade);
                planner_.Cooldown(owner, obs.nowMs + kNoSpellbookCooldownMs);
            }
            planner_.Finish(false, "no seller reachable", obs.nowMs);
            return false;
        }
        LogLine("%s: looking for a '%s' to sell %s (trip %d)",
                GoalKindName(owner), trade, what, spellbookTrips_);
        // A mage can leave the current city to fill a spellbook.  Remember each
        // attempted shop so retries progress through the atlas rather than
        // returning to the same seller that was just unreachable or empty.
        travelInFlight_ = client.TravelToServiceSkipping(
            svc, HomeOrNearest(state_.homeCity), spellbookSkipSellers_,
            &spellbookSkipPlaces_, true, wantLeather || armourErrand);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Walk up before speaking -- the lesson the food goal had to learn twice.
    i32 vx = 0, vy = 0; i8 vz = 0;
    if (!client.MobilePosition(keeper, &vx, &vy, &vz)) {
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    const i32 d = TileDist(obs.x, obs.y, vx, vy);
    if (d > kVendorReach) {
        LogLine("%s: the %s is %d tiles away -- walking up",
                GoalKindName(owner), sellerTrade, d);
        travelInFlight_ = client.TravelToEntity(keeper, 1);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    if (!OfferBelongsTo(client, keeper)) {
        LogLine("%s: asking the %s to show %s", GoalKindName(owner), sellerTrade, what);
        client.ActionVendorOpen(keeper);
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }

    int skipped = 0;
    // TWO PASSES, SO A PREFERENCE COSTS NOTHING WHEN IT CANNOT BE MET.
    //
    // Pass 0 looks only for `prefer` -- the scroll the craft ladder is stuck
    // on. Pass 1 is the original behaviour, unchanged, and runs whenever pass 0
    // bought nothing. Making `prefer` a filter instead would have sent a scribe
    // who could not find a Recall scroll straight to the "nothing this book
    // lacks" branch and on to a random mage shop, throwing away every other
    // named scroll on the shelf in front of her.
    const int firstPass = (prefer && !graphic) ? 0 : 1;
    for (int pass = firstPass; pass < 2; ++pass) {
    for (const Client::VendorItem& v : client.VendorOffer()) {
        const bool match =
            pass == 0 ? (v.graphic == prefer)
            : graphic ? (v.graphic == graphic)
                      : (v.graphic >= kFirstScrollGraphic &&
                         v.graphic <= kLastScrollGraphic);
        if (!match) continue;
        // DO NOT BUY A SPELL THIS CHARACTER ALREADY HAS. Gold spent on a
        // duplicate buys nothing at all -- the book refuses it and the scroll
        // is wasted.
        if (skipKnown && BookHasGraphic(client, obs.spellbookSerial, v.graphic)) {
            ++skipped;
            continue;
        }
        // AND DO NOT BUY ONE THE BOOK HAS ALREADY REFUSED.
        //
        // BookHasGraphic reads the container rows, and a spellbook row's
        // graphic is not the scroll's -- see BookHasSpell. So the shelf check
        // above can say "the book lacks this" about a spell the book will
        // silently refuse, and Selene bought the SAME Cunning Scroll four
        // times for 84 gold in ninety seconds, adding nothing each time
        // (run_gates/g_Selene.console.txt:1274-1753, 2026-09-02). The book's
        // own refusal is the authoritative answer and it is already recorded;
        // this just stops the purse paying for it again.
        if (skipKnown) {
            bool refusedBefore = false;
            for (u16 r : scrollBookRefused_)
                if (r == v.graphic) { refusedBefore = true; break; }
            if (refusedBefore) { ++skipped; continue; }
        }
        if (static_cast<i32>(v.price) > obs.gold) continue;
        LogLine("spellbook: buying %s ('%s', 0x%04X, spell %d) at %d gold "
                "-- %d of this shop's scrolls were already in the book%s",
                what, v.name.c_str(), v.graphic,
                SpellForScrollGraphic(v.graphic),
                static_cast<i32>(v.price), skipped,
                pass == 0 ? " (this is the scroll the craft ladder wants)" : "");
        client.ActionVendorBuy(keeper, v.serial, qty);
        // An ask, not progress -- same reason as BUY_SUPPLIES: counting a
        // purchase before the server takes the gold clears the failure ladder
        // on every retry.
        planner_.NoteAttempt(obs.nowMs);
        // A bought scroll must be re-read into the book, so force a re-open.
        spellbookOpened_ = false;
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }
    // Pass 0 finding nothing is SILENT on purpose: this loop runs on every tick
    // while a vendor window is open, so a "not on this shelf" line here would
    // be printed hundreds of times. The successful buy above says when the
    // preference was met; the once-per-shop lines below cover the rest.
    }

    // THE SHELF IN FRONT OF US BEATS THE PIECE WE PLANNED. An armour errand
    // asks for one exact graphic, but any legal piece that protects a slot
    // better than what is worn is the same errand done. Walking to another
    // shop for the planned piece while this one sells a wearable upgrade is
    // the trip Castor could not afford (see above).
    if (owner == GoalKind::UpgradeGear && wantPiece) {
        const Client::VendorItem* alt = nullptr;
        const ArmorPiece* altPiece = nullptr;
        for (const Client::VendorItem& v : client.VendorOffer()) {
            const ArmorPiece* a = ArmorFor(v.graphic);
            if (!a || !MayWear(*a, obs)) continue;
            if (static_cast<i32>(v.price) > obs.gold) continue;
            if (client.FindBackpackItemByGraphic(a->graphic)) continue;
            const u8 layer = client.ItemEquipLayer(a->graphic);
            if (!layer) continue;
            const u16 wornGfx = client.EquippedGraphicAt(layer);
            const ArmorPiece* worn = wornGfx ? ArmorFor(wornGfx) : nullptr;
            if ((worn ? worn->armor : 0) >= a->armor) continue;
            if (!altPiece || a->armor > altPiece->armor) { alt = &v; altPiece = a; }
        }
        if (alt) {
            LogLine("%s: this %s has no 0x%04X but sells %s (0x%04X, armor %d) "
                    "this character may wear -- buying that at %d gold",
                    GoalKindName(owner), sellerTrade, graphic,
                    alt->name.c_str(), alt->graphic, altPiece->armor,
                    static_cast<i32>(alt->price));
            client.ActionVendorBuy(keeper, alt->serial, 1);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 9000;
            return false;
        }
    }

    if (!scribeExhausted_ && std::strcmp(trade, "scribe") == 0) {
        LogLine("spellbook: this scribe has nothing the book lacks (%d of its "
                "scrolls are already known) -- trying a mage shop", skipped);
        scribeExhausted_ = true;
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // ONE SHOPKEEPER'S SHELF IS NOT THE WHOLE TRADE.
    //
    // This used to fail the goal outright, and for a mage shop that verdict is
    // simply wrong: the template sells FOUR RANDOM SCROLLS
    // (random_first_circle .. random_fourth_circle, tm_vend.scp:721-724), so
    // "all four are already in the book" says nothing about the next mage's
    // roll. Thalia stood down on exactly that -- goal_failed=FILL_SPELLBOOK
    // "this 'mage' does not stock a scroll (4 already known)"
    // (run_gates/g_Thalia.console.txt:523). Remember this seller, spend a trip,
    // and go and look at a different shelf. The trip budget above is still the
    // brake, so this cannot become a tour of every mage on the shard.
    if (spellbookTrips_ < kMaxSpellbookTrips) {
        ++spellbookTrips_;
        spellbookSkipSellers_.push_back(keeper);
        LogLine("%s: this '%s' has nothing the book lacks (%d already known) "
                "-- its stock is randomised, so trying another shop (trip %d)",
                GoalKindName(owner), sellerTrade, skipped, spellbookTrips_);
        travelInFlight_ = client.TravelToServiceSkipping(
            svc, HomeOrNearest(state_.homeCity), spellbookSkipSellers_,
            &spellbookSkipPlaces_, true, wantLeather || armourErrand);
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    if (owner == GoalKind::FillSpellbook) {
        StandDownFromScrollShopping(
            obs, "every seller within reach stocks nothing this book lacks");
    } else {
        LogLine("goal_failed=%s reason=\"this '%s' does not stock %s "
                "(%d already known)\"", GoalKindName(owner), sellerTrade, what,
                skipped);
        planner_.Cooldown(owner, obs.nowMs + kNoSpellbookCooldownMs);
    }
    planner_.Finish(false, "seller has none", obs.nowMs);
    return false;
}

// WHICH SPELL THIS CHARACTER CAN ACTUALLY PRACTISE WITH.
//
// Not a constant, because the starter book is not what the code assumed. The
// [NEWBIE MAGERY] template hands over a spellbook with MORE1=0382a8c38, and
// that bitmask decodes to exactly twelve spells --
//
//   Heal, Magic Arrow, Night Sight, Cure, Harm, Strength, Fireball, Poison,
//   Teleport, Fire Field, Greater Heal, Lightning
//
// -- which does NOT include Create Food. So the hardcoded practice spell was
// missing from the book of every freshly created character on this shard, not
// only from Voris's. "The spell is not in your spellbook", forever.
//
// The book is read rather than guessed: a spellbook's contents arrive as
// container items whose graphic is 0x1F2D + the spell number, so opening it
// says precisely what this character owns.
//
// The candidates are ordered by how safe they are to cast at oneself, which is
// the owner's rule for skill practice -- "we used to use all of the skills on
// ourselves with no damage to ourselves". Every one below is
// spellflag_good + spellflag_playeronly in spells_magery.scp: none can hurt the
// caster, none makes a criminal of them, and none needs a foe.
//
// AND WHAT IT COSTS. The list below used to be a bare set of spell numbers and
// the goal cast the first one the book held, which is how four mages spent a
// whole session being told "You lack Sulfurous Ash for this spell" every six
// seconds (wave 2026-09-02). The reagent table and the choice itself now live
// in include/uo/spellcast.h, read off spells_magery.scp's own RESOURCES lines,
// so the pack is asked BEFORE the server is.
spell::PracticeChoice Runner::PickPracticeSpell(Client& client,
                                                const Observation& obs) const {
    spell::PracticeChoice none;
    if (obs.spellbookSerial == 0) return none;

    // ASK THE WHOLE TABLE, NOT A HAND-PICKED LIST.
    //
    // This used to walk twelve spell numbers compiled into spellcast.h, so a
    // mage practised with whatever those twelve happened to be at whatever
    // circle. Owner ruling 2026-09-02: "for mage to cast there are lots of
    // skills, don't hard code Create Food." The candidate set is now every
    // spell in data/revolution_spells.tsv -- all eight circles -- and the
    // rules that narrow it (safe on oneself, SKILLREQ reached, mana, reagents,
    // gain window, rotation) live in spell::ChoosePracticeSpell.
    //
    // ASK THE BOOK IN ITS OWN CURRENCY. A spellbook row's graphic is the
    // constant 0x1F2E for every spell; the SPELL NUMBER is in the amount.
    // See Runner::BookHasSpell for the packet and the evidence.
    spell::LoadSpellTable(client.DataDir());
    spell::PracticeSight see;
    for (const spell::SpellDef& d : spell::SpellTable()) {
        if (BookHasSpell(client, obs.spellbookSerial, d.spell))
            see.inBook.push_back(d.spell);
    }
    see.magery = obs.SkillTenths(rules::kMagery);
    see.mana = obs.mana;
    see.casts = practiceCastCounts_;
    // The pack as the character can count it -- the same hue-resolved defname
    // stocks every other errand reads. A reagent in the BANK is not a reagent
    // in hand, and Sphere consumes from the pack.
    see.pack = obs.pack;
    see.uncastable = practiceRefusedSpells_;
    const spell::PracticeChoice choice = spell::ChoosePracticeSpell(see);
    if (choice.spell >= 0 || choice.manaNeeded > 0 || !choice.missing.empty()) return choice;

    const usize n = client.ContainerItemCount(obs.spellbookSerial);
    // SAY WHAT IS ACTUALLY IN THERE. "Nothing safe to cast" is a claim about
    // the book, and a claim about the book should be checkable from the log
    // rather than taken on trust -- especially since this is also the only
    // place that proves the book's contents can be read at all.
    std::string had;
    for (usize i = 0; i < n && i < 32; ++i) {
        u32 serial = 0; u16 gfx = 0, amount = 0;
        if (!client.ContainerItemAt(obs.spellbookSerial, i, &serial, &gfx,
                                    &amount))
            continue;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s%d", had.empty() ? "" : ",",
                      static_cast<int>(amount));
        had += buf;
    }
    LogLine("practice: the book holds %d item(s), spells [%s] -- %s",
            static_cast<int>(n), had.c_str(), choice.reason);
    return choice;
}

// ---------------------------------------------------------------------------
// FILLING THE BOOK.
//
// "we need to add that mages tries to fill their book, make it full spell
// book" (project owner, 2026-08-29). A mage's spellbook is equipment, and this
// shard demonstrated why it is not optional: Voris carried Magery 50.0 and
// asked for Create Food twenty-six times in one session, being told every time
// "The spell is not in your spellbook".
//
// WHERE SCROLLS COME FROM ON THIS SHARD -- read from its own vendor and loot
// tables, written up in docs/REVOLUTION_GAMEPLAY_TRUTH.md 3.5:
//
//   circles 1-4   any mage shop, but as random_first_circle .. fourth, so the
//                 spell that arrives is NOT chosen
//   circle 5, part of 6, and Resurrection   a scribe, by name
//   circles 7-8   nobody sells them: dungeon chests and monster loot only
//
// Two consequences this goal is built around. A mage cannot buy its way to a
// full book, so the goal aims at a working kit rather than completeness. And
// because purchases are random, the honest loop is buy-then-look, not
// pick-then-buy -- which is also how a player fills a book on this shard.
//
// The book is READ, never assumed: opening it and counting what the server
// sends back is the only truthful source for what this character can cast.
bool Runner::DoFillSpellbook(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    // NO BOOK IS A DIFFERENT PROBLEM FROM AN EMPTY ONE.
    if (obs.spellbookSerial == 0) {
        if (obs.gold < kSpellbookMoney) {
            LogLine("spellbook: no book and %d gold -- a book costs about %d, "
                    "so go and earn first", obs.gold, kSpellbookMoney);
            planner_.Cooldown(GoalKind::FillSpellbook,
                              obs.nowMs + kNoSpellbookCooldownMs);
            planner_.Finish(false, "no book and no money", obs.nowMs);
            return false;
        }
        // A spellbook itself: the mage shop sells them (SELL=i_spellbook),
        // and there is nothing to choose, so no reason to prefer a scribe.
        BuyScrollFrom(client, obs, "mage", wm::Service::Mage, kSpellbookGraphic,
                      false, 1, "a spellbook", GoalKind::FillSpellbook);
        return false;
    }

    // DID THE LAST SCROLL GO IN?
    //
    // Dropping a scroll on the book is not the same as the book taking it. A
    // spell the book ALREADY HOLDS is refused, and the server puts the scroll
    // straight back in the backpack -- which arrives as
    //   move_item server_failure ("item landed in a different container")
    // because the 0x25 names the pack, not the book. Nothing remembered that,
    // so the very next tick found the same scroll and offered it again:
    //   "spellbook: adding scroll 0x1F40 to the book (14 spells so far)"
    // repeated every five seconds for two minutes and forty seconds of a
    // fourteen-minute life, the count never once moving off 14. The engine's
    // own backstop eventually called it -- "goal_spinning=PRACTICE_SKILL ...
    // completed 5 times in a row with progress 0" -- which bounded the waste
    // without stopping it.
    //
    // The book's own count is the honest witness: if it did not rise, that
    // graphic is one this book will not take, so stop offering it.
    if (scrollOfferedGraphic_ != 0) {
        if (obs.spellsKnown > spellsBeforeOffer_) {
            planner_.NoteProgress();      // a REAL add, unlike the old
                                          // unconditional call below
            // A SPELL WENT IN, so this street is not empty after all. Clear the
            // escalating rest and give the errand a fresh time budget: Selene
            // bought three scrolls from one scribe in ninety seconds
            // (g_Selene.console.txt:2849-3056) and must not be damped for it.
            scrollStandDowns_ = 0;
            scrollShopSinceMs_ = obs.nowMs;
        } else {
            LogLine("spellbook: the book would not take scroll 0x%04X (still "
                    "%d spells) -- it already knows that one; not offering it "
                    "again", scrollOfferedGraphic_, obs.spellsKnown);
            bool known = false;
            for (u16 r : scrollBookRefused_)
                if (r == scrollOfferedGraphic_) { known = true; break; }
            if (!known) scrollBookRefused_.push_back(scrollOfferedGraphic_);
        }
        scrollOfferedGraphic_ = 0;
    }

    // LOOK IN IT BEFORE BUYING ANYTHING.
    //
    // ContainerItemCount is only meaningful once the server has sent the
    // contents, which happens when the book is opened. Treating an unopened
    // book as an empty one would send the character shopping for spells it
    // already owns.
    // ...and a DIFFERENT book is a different unknown. spellbookOpened_ is one
    // flag for whatever book Observe picked this tick, so a second book (one
    // just fetched out of the bank) would inherit the first one's "already
    // looked" and never be read. Comparing the SERIAL is what makes the flag
    // mean this book -- and it is the serial rather than ContainerKnown so a
    // book that refuses to answer is still asked only once.
    if (!spellbookOpened_ || spellbookOpenedSerial_ != obs.spellbookSerial) {
        LogLine("spellbook: opening the book to see what is already in it");
        client.ActionUseObject(obs.spellbookSerial);
        spellbookOpened_ = true;
        spellbookOpenedSerial_ = obs.spellbookSerial;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    if (obs.spellsKnown >= kSpellbookComfortableRuntime) {
        LogLine("spellbook: %d spells is a working book -- the rest are scribe "
                "and dungeon work, not shopping", obs.spellsKnown);
        planner_.Finish(true, "book is serviceable", obs.nowMs);
        return true;
    }

    // A SCROLL IN THE PACK BELONGS IN THE BOOK.
    //
    // Looted scrolls arrive here too, which is the only route to circles 7-8,
    // so this runs before any purchase.
    for (u16 g = kFirstScrollGraphic; g <= kLastScrollGraphic; ++g) {
        bool refused = false;
        for (u16 r : scrollBookRefused_) if (r == g) { refused = true; break; }
        if (refused) continue;            // this book already knows that spell
        const u32 scroll = client.FindBackpackItemByGraphic(g);
        if (!scroll) continue;
        LogLine("spellbook: adding scroll 0x%04X to the book (%d spells so far)",
                g, obs.spellsKnown);
        client.ActionMoveItem(scroll, 1, obs.spellbookSerial);
        // Progress is claimed ABOVE, next tick, and only if the count rose.
        // Claiming it here said the goal was working while it achieved
        // nothing, which is exactly what the anti-spin backstop exists to
        // catch.
        scrollOfferedGraphic_ = g;
        spellsBeforeOffer_ = obs.spellsKnown;
        // Re-read the book after the drop rather than assuming it took.
        spellbookOpened_ = false;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // THE ERRAND IS BOUNDED IN TIME, NOT ONLY IN TRIPS.
    //
    // Everything above this line is free: reading the book and putting a looted
    // scroll into it costs seconds and always achieves something. Everything
    // below is SHOPPING, and shopping on this shard means walking -- about a
    // minute per shop. Aurelius stayed under the three-trip budget for a whole
    // five-minute gate and still cast nothing, because three trips is four
    // minutes of travel (g_Aurelius.console.txt:136-847). A trip budget cannot
    // see that; a clock can.
    //
    // The clock only runs while the goal keeps getting the turn. A gap longer
    // than kScrollShopGapStaleMs means the planner took the turn away and gave
    // it back, which starts a fresh stretch rather than blowing the budget on
    // the first tick -- the same staleness rule the wool chain's mark uses.
    if (scrollShopSinceMs_ == 0 ||
        obs.nowMs - scrollShopTickMs_ > kScrollShopGapStaleMs) {
        scrollShopSinceMs_ = obs.nowMs;
    } else if (client.TravelBusy() || travelInFlight_) {
        // THE CLOCK STOPS ON THE ROAD. Walking is what shopping costs on this
        // shard, and the budget exists to catch standing at counters that
        // never sell -- not to punish the walk to the next one. Aurelius set
        // out for the mage shop at 00:15:43 and the goal failed at 00:16:15,
        // "90s of shopping and not one spell added", while he was still in
        // the street, then sat out a 899-second cooldown (D3,
        // artifacts/validation_wave_2026-09-06.md). Rolling the mark forward
        // by the elapsed tick leaves the budget measuring shop time only.
        scrollShopSinceMs_ += obs.nowMs - scrollShopTickMs_;
    } else if (obs.nowMs - scrollShopSinceMs_ > kScrollShopBudgetMs) {
        const i64 spent = obs.nowMs - scrollShopSinceMs_;
        char why[160];
        std::snprintf(why, sizeof(why),
                      "%llds of shopping and not one spell added to the book",
                      static_cast<long long>(spent / 1000));
        StandDownFromScrollShopping(obs, why);
        planner_.Finish(false, "nobody selling scrolls", obs.nowMs);
        return false;
    }
    scrollShopTickMs_ = obs.nowMs;

    // NOTHING TO ADD, SO BUY. Circles 1-4 only, and random at that.
    if (obs.gold < kScrollMoney) {
        LogLine("spellbook: %d spells and %d gold -- too poor to buy scrolls, "
                "standing down to earn", obs.spellsKnown, obs.gold);
        planner_.Cooldown(GoalKind::FillSpellbook,
                          obs.nowMs + kNoSpellbookCooldownMs);
        planner_.Finish(false, "no money for scrolls", obs.nowMs);
        return false;
    }
    // THE SCRIBE FIRST, BECAUSE ONLY THE SCRIBE LETS US CHOOSE.
    //
    // A mage shop stocks random_first_circle .. random_fourth_circle, so it
    // sells a lottery ticket: the spell that arrives is not chosen and may
    // well be one the book already holds. A scribe stocks 44 named scrolls, so
    // a spell the character actually lacks can be asked for by name.
    //
    // "buy new spells not on the book" is therefore a scribe errand, and this
    // shard has 19 of them standing in mage shops
    // (revolution/revolution_scribe_shops.scp) -- the same building, so
    // preferring one costs no extra walking.
    //
    // The mage shop stays as the fallback for a town with no scribe: a random
    // scroll is worth more than no scroll.
    // ASK FOR THE SPELL THE TRADE IS WAITING ON, FIRST.
    //
    // Owner ruling 2026-09-04: the side goal should prioritise the spell of the
    // next craft rung this life's skills already allow. ChooseCraft is the only
    // thing that knows which rung that is -- it is the code that had to SKIP it
    // -- so ask it, and turn its answer into the scroll's art id. A preference,
    // never a filter: if the shelf has not got it, the errand takes whatever
    // else the book lacks (see BuyScrollFrom's two passes).
    u16 prefer = 0;
    if (needCfg_.profession) {
        const CraftIntent rung =
            ChooseCraft(*needCfg_.profession, obs, 1, &craftFocus_);
        if (rung.wantSpell) {
            prefer = ScrollGraphicForSpell(rung.wantSpell);
            // ONCE PER WANTED SCROLL, not once per tick -- see
            // scrollPreferSaid_ for the 600 identical lines that bought.
            if (prefer != scrollPreferSaid_) {
                scrollPreferSaid_ = prefer;
                LogLine("spellbook: %s cannot be written until '%s' (spell %d) "
                        "is in the book -- asking for scroll 0x%04X by name",
                        rung.wantSpellItem ? rung.wantSpellItem : "the next rung",
                        spell::DefForSpell(rung.wantSpell)
                            ? spell::DefForSpell(rung.wantSpell)->name
                            : "?",
                        rung.wantSpell, prefer);
            }
        } else {
            scrollPreferSaid_ = 0;
        }
    }
    if (scribeExhausted_) {
        BuyScrollFrom(client, obs, "mage", wm::Service::Mage, 0, true, 1,
                      "a scroll", GoalKind::FillSpellbook, prefer);
    } else {
        BuyScrollFrom(client, obs, "scribe", wm::Service::Scribe, 0, true, 1,
                      "a spell this book lacks", GoalKind::FillSpellbook,
                      prefer);
    }
    return false;
}

// THE END OF A PRACTICE BOUT, JUDGED BY THE SKILL AND NOTHING ELSE.
//
// Success is a tenth actually gained on the practised skill while this bout
// ran (NotePracticeGain, above). An exhausted attempt budget with the skill
// where it started is a FAILURE with a reason -- the whole Selene defect was
// that it was reported as a completion.
bool Runner::EndPracticeBout(const Observation& obs, int skillId,
                             i64 cooldownMs) {
    const i32 gains = practiceGains_;
    const i32 was = practiceBaselineTenths_;
    selfPracticeRuns_ = 0;
    practiceBaselineSkill_ = -1;
    practiceBoutMs_ = -1;
    practiceGains_ = 0;
    planner_.Cooldown(GoalKind::PracticeSkill, obs.nowMs + cooldownMs);
    if (gains > 0) {
        LogLine("practice: %s is now %.1f -- %d gain(s) this bout",
                rules::SkillName(skillId), was / 10.0, gains);
        planner_.Finish(true, nullptr, obs.nowMs);
        return true;
    }
    LogLine("goal_failed=PRACTICE_SKILL reason=\"%d attempts at %s and the "
            "skill never moved off %.1f\"",
            kSelfPracticeBeforeRethink, rules::SkillName(skillId), was / 10.0);
    // So the next session knows this was tried and paid nothing. The cooldown
    // above stops the immediate re-pick; this is the durable half.
    state_.memory.NoteEvent("practice_no_gain", rules::SkillName(skillId),
                            "self-practice", obs.x, obs.y, obs.nowMs);
    planner_.Finish(false, "no skill gain observed", obs.nowMs);
    return false;
}

bool Runner::DoPracticeSkill(Client& client, const Observation& obs) {
    const int skillId = obs.wantPracticeSkill;
    if (skillId < 0) {
        // NOT A SUCCESS. This used to `return true`, which RunGoal reports as
        // goal_completed with progress 0 -- a goal succeeding at nothing.
        LogLine("goal_failed=PRACTICE_SKILL reason=\"nothing this life can "
                "practise right now\"");
        planner_.Cooldown(GoalKind::PracticeSkill, obs.nowMs + 60000);
        planner_.Finish(false, "no skill to practise", obs.nowMs);
        return false;
    }
    if (client.ActionBusy()) return false;

    const i32 have = obs.SkillTenths(skillId);

    // ONE BOUT, ONE BASELINE. A bout is this skill under this selection of the
    // goal; anything else (a new goal instance, a different skill) starts over.
    if (practiceBaselineSkill_ != skillId ||
        practiceBoutMs_ != planner_.Current().startedAtMs) {
        practiceBaselineSkill_ = skillId;
        practiceBoutMs_ = planner_.Current().startedAtMs;
        practiceBaselineTenths_ = have;
        practiceGains_ = 0;
        selfPracticeRuns_ = 0;
    }
    // Say every real gain out loud, the tick the server's skill table shows it.
    if (NotePracticeGain(skillId, have)) planner_.NoteProgress();

    // MEDITATION AT FULL MANA CANNOT GAIN, SO IT MUST NOT BE ATTEMPTED.
    //
    // Verified in the server source: CChar::Skill_Meditation
    // (server/Source-X/src/game/chars/CCharSkill.cpp:2674-2679) answers
    // SKTRIG_START with -SKTRIG_QTY and "You are at peace." while
    // Stat_GetVal(STAT_INT) >= Stat_GetMaxAdjusted(STAT_INT); CChar::Skill_Start
    // (:4505-4520) then cleans the skill up because m_Act_Difficulty < 0, so
    // Skill_Done -- and with it Skill_Experience -- is never reached. Mana has
    // to be SPENT before meditating is worth anything, which for this client
    // means casting (Magery practice) or fighting with spells.
    if (skillId == rules::kMeditation && obs.manaMax > 0 &&
        obs.mana >= obs.manaMax) {
        LogLine("goal_failed=PRACTICE_SKILL reason=\"Meditation cannot start "
                "at full mana (%d/%d) -- 'You are at peace' gives no skill "
                "credit; mana must be spent first\"",
                obs.mana, obs.manaMax);
        state_.memory.NoteEvent("practice_blocked",
                                rules::SkillName(skillId), "full mana",
                                obs.x, obs.y, obs.nowMs);
        planner_.Cooldown(GoalKind::PracticeSkill,
                          obs.nowMs + kNoPracticeGainCooldownMs);
        planner_.Finish(false, "meditation cannot gain at full mana",
                        obs.nowMs);
        return false;
    }

    if (skillId == rules::kPoisoning) {
        const u32 dagger = client.FindBackpackItemByGraphic(0x0F51)
            ? client.FindBackpackItemByGraphic(0x0F51) : client.FindBackpackItemByGraphic(0x0F52);
        const u32 poison = client.FindBackpackItemByGraphic(0x0F0A);
        if (!dagger || !poison) {
            if (needCfg_.profession && WantsSpellCombat(*needCfg_.profession))
                return HandOff(GoalKind::PracticeSkill, GoalKind::TrainCombat, 30000,
                               "no coating supplies; use Poison spells with legal targets", obs.nowMs);
            planner_.Cooldown(GoalKind::PracticeSkill, obs.nowMs + 30000);
            planner_.Finish(false, "poisoning supplies no longer available", obs.nowMs);
            return false;
        }
        if (++selfPracticeRuns_ >= kSelfPracticeBeforeRethink) {
            return EndPracticeBout(obs, skillId, 60000);
        }
        client.ActionApplyPoison(dagger, poison);
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 4000;
        return false;
    }

    // MAGERY AND EVALUATING INTELLIGENCE ARE RAISED BY CASTING, WITH OR
    // WITHOUT A FOE. The spell picker always uses current Magery for its
    // skill requirement; `skillId` remains the discipline whose observed gain
    // closes this practice bout.
    //
    // WHICH spell is not decided here and is not written down anywhere in this
    // client. Owner ruling 2026-09-02: "for mage to cast there are lots of
    // skills, don't hard code Create Food." The candidate set is the shard's
    // whole Magery table (data/revolution_spells.tsv, exported from
    // runtime/scripts/spells/spells_magery.scp by tools/spellgen.py, 64 spells
    // over 8 circles); the choice is spell::ChoosePracticeSpell, which keeps
    // only what this book holds, what SKILLREQ allows at this Magery, what the
    // mana and the pack can pay for, and what is safe to aim at oneself --
    // then prefers the highest circle in the gain window and rotates within it.
    //
    // Deliberately not a combat spell. Practising Magery must not be a way to
    // start fights the life did not choose. Create Food stays in DoGetFood,
    // where it is an errand rather than an exercise.
    if (skillId == rules::kMagery || skillId == rules::kEvaluatingIntel) {
        // Create Food belongs to the FOOD goal. It is absent from this shard's
        // starter book, but that must not veto Magery practice when the book
        // holds another safe spell. Read this character's book and choose from
        // it below instead of carrying the food-goal refusal into practice.
        // The book must be opened once before it can be read; an unopened book
        // is not an empty one.
        if (obs.spellbookSerial == 0) {
            LogLine("practice: no spellbook carried -- Magery cannot be "
                    "practised until FILL_SPELLBOOK has bought one");
            planner_.Cooldown(GoalKind::PracticeSkill,
                              obs.nowMs + kNoSelfSafeSpellCooldownMs);
            planner_.Finish(false, "no spellbook", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        if (!spellbookOpened_ || spellbookOpenedSerial_ != obs.spellbookSerial) {
            LogLine("practice: opening the spellbook to see what can be cast");
            client.ActionUseObject(obs.spellbookSerial);
            spellbookOpened_ = true;
            spellbookOpenedSerial_ = obs.spellbookSerial;
            practiceRecheckedBook_ = false;
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        // spellbookOpened_ ONLY MEANS "OPENED AT SOME POINT THIS SESSION".
        //
        // FILL_SPELLBOOK and PRACTICE_SKILL share that one flag, so an open
        // FILL_SPELLBOOK did minutes ago satisfies this gate even if
        // containerItems_ for that same serial has since gone empty --
        // Aurelius: FILL_SPELLBOOK opened the book and cached 19 items
        // (run_gates/g_Aurelius.console.txt:86-90), then an equip in between
        // (:610-624 "worn on the requested layer") left the cache reading 0,
        // and this gate took the shared flag's word for it: "the book holds 0
        // item(s) ... nothing safe to cast at myself -- standing down"
        // (:624, 2026-09-05). Re-open once before believing an empty read; a
        // book that is STILL empty after that is the genuine article and
        // falls through to PickPracticeSpell's own "nothing safe to cast"
        // reasoning below.
        if (client.ContainerItemCount(obs.spellbookSerial) == 0) {
            if (!practiceRecheckedBook_) {
                LogLine("practice: the book reads empty but was already "
                        "opened this session -- re-opening once before "
                        "believing that");
                client.ActionUseObject(obs.spellbookSerial);
                practiceRecheckedBook_ = true;
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
        } else {
            practiceRecheckedBook_ = false;
        }
        // READ THE SERVER'S ANSWER TO THE LAST CAST BEFORE SENDING ANOTHER.
        //
        // "You lack Sulfurous Ash for this spell" is a HARD signal: it names a
        // reagent, and Sphere will answer identically until one is bought. The
        // whole 2026-09-02 defect is that nothing read it -- Elara sent 322
        // casts and was refused 322 times. A refused spell is struck off for
        // the session (a later restock un-strikes it, because the pack check
        // below is what actually gates the cast) and the goal re-plans at once.
        if (practiceCastSpell_ >= 0 && practiceCastMark_ != 0 &&
            client.JournalSaidSince("you lack", practiceCastMark_)) {
            const spell::SpellDef* d = spell::DefForSpell(practiceCastSpell_);
            const char* named = nullptr;
            if (d) {
                for (const char* r : d->reagents) {
                    if (!r) break;
                    const char* needle = spell::LackNeedleFor(r);
                    if (needle && client.JournalSaidSince(needle, practiceCastMark_)) {
                        named = r;
                        break;
                    }
                }
            }
            if (named) {
                // Not mana, not skill -- a reagent, by name. Believe the pack
                // is empty of it whatever the container stream last said.
                LogLine("practice: the server refused spell %d for want of %s "
                        "-- striking that spell off for this session and "
                        "re-planning", practiceCastSpell_, named);
                bool known = false;
                for (int s : practiceRefusedSpells_)
                    if (s == practiceCastSpell_) known = true;
                if (!known) practiceRefusedSpells_.push_back(practiceCastSpell_);
            }
            // "You lack sufficient mana" reaches here too and must NOT strike
            // the spell off: mana comes back on its own.
            practiceCastSpell_ = -1;
            practiceCastMark_ = 0;
        }

        const spell::PracticeChoice pick = PickPracticeSpell(client, obs);
        if (pick.manaNeeded > 0) {
            // Rest is part of the casting loop. Bound retries so interrupted
            // or ineffective meditation cannot monopolize the planner.
            if (obs.underAttack || obs.hostilesNear > 0 ||
                ++selfPracticeRuns_ >= kSelfPracticeBeforeRethink) {
                selfPracticeRuns_ = 0;
                planner_.Cooldown(GoalKind::PracticeSkill, obs.nowMs + 15000);
                planner_.Finish(false, "mana recovery needs a break", obs.nowMs);
                return false;
            }
            LogLine("practice: mana %d below %d -- meditating before the next cast",
                    obs.mana, pick.manaNeeded);
            client.ActionUseSkill(rules::kMeditation);
            nextActionMs_ = obs.nowMs + 12000;
            return false;
        }
        // OUT OF REAGENTS IS A SHOPPING LIST, NOT A DEAD END.
        //
        // The pack is short of what every spell in this book costs, so there is
        // nothing to cast and nothing FILL_SPELLBOOK can do about it. Hand the
        // list to BUY_SUPPLIES -- the mage shop that sells scrolls sells these
        // too (tm_vend.scp:633-656) -- and stand down so it gets a turn.
        if (pick.spell < 0 && !pick.missing.empty()) {
            const i64 remainingMs =
                cfg_.sessionLimitMs > 0
                    ? cfg_.sessionLimitMs - (obs.nowMs - sessionStartMs_)
                    : 0;
            // A REAGENT KEEPS, SO THE HORIZON IS A SITTING, NOT THE REMAINDER.
            //
            // Buying only for the minutes left in this session is what a bot
            // does, not a player: Aurelius, holding 9,330 gold, walked to the
            // mage Alenne and bought THREE of each at 3 gold
            // (run_gates/g_Aurelius.console.txt:626,635,717). Reagents do not
            // spoil and the walk is the expensive part, so the target is a
            // whole session's practice -- still a rate times a time, still
            // per character, and still capped by the purse below.
            const i64 horizonMs = remainingMs > cfg_.sessionLimitMs
                                      ? remainingMs
                                      : cfg_.sessionLimitMs;
            const i32 casts = spell::ExpectedPracticeCasts(
                practiceCasts_, obs.nowMs - sessionStartMs_, horizonMs,
                kPracticeCastPeriodMs);
            // The price this character has SEEN, not one we assume. Unknown is
            // allowed: the vendor errand reads the shelf and clamps there.
            i32 unit = 0;
            if (const market::PriceObservation* p = state_.prices.Latest(
                    pick.missing.front().c_str(),
                    market::PriceSource::NpcVendorSells))
                unit = p->pricePerUnit;
            const i32 shortest = market::QtyOf(obs.pack, pick.missing.front().c_str());
            // ONE TRIP, THE WHOLE POUCH. What blocks the practice spell goes
            // first -- it is why this goal stood down -- but the walk to the
            // mage is the expensive part, so anything else under its band
            // rides along on the same trip (owner ruling 2026-09-06).
            reagentWants_ = pick.missing;
            std::vector<std::string> band;
            i32 bandGap = 0;
            ReagentBandShortfall(obs, band, &bandGap);
            for (const std::string& r : band)
                if (std::find(reagentWants_.begin(), reagentWants_.end(), r) ==
                    reagentWants_.end()) reagentWants_.push_back(r);
            const spell::ReagentPlan plan = spell::PlanReagentBuy(
                shortest, std::max(casts, bandGap), unit, obs.gold,
                static_cast<int>(reagentWants_.size()));
            reagentWantQty_ = plan.buy > 0 ? plan.buy : 1;
            std::string list;
            for (const std::string& r : reagentWants_)
                list += (list.empty() ? "" : ", ") + r;
            LogLine("practice: out of reagents for spell %d -- wants %d of each "
                    "of [%s] (a sitting is %d cast(s) at this character's own "
                    "rate, %s) and is standing down so BUY_SUPPLIES can go to "
                    "a mage",
                    pick.shortFor, reagentWantQty_, list.c_str(), casts,
                    plan.why);
            planner_.Cooldown(GoalKind::PracticeSkill,
                              obs.nowMs + kNoReagentCooldownMs);
            planner_.Finish(false, "no reagents for any castable spell",
                            obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        const int spell = pick.spell;
        if (spell < 0) {
            // AND STAND DOWN LONG ENOUGH FOR THE FIX TO HAPPEN.
            //
            // This is not a pacing failure, it is a PREREQUISITE failure: the
            // book has to gain a spell before practising can work, and the
            // goal that buys one is FILL_SPELLBOOK. Finishing without a
            // cooldown handed PRACTICE_SKILL straight back every five seconds
            // and FILL_SPELLBOOK only got a turn once the noop backstop had
            // counted to five -- Ilyandra ran that cycle for the whole session
            // with Magery frozen at 50.0
            // (run_r4/w_Ilyandra.console.txt:496-711).
            LogLine("practice: nothing safe to cast at myself is in this book "
                    "-- standing down so FILL_SPELLBOOK can buy one");
            planner_.Cooldown(GoalKind::PracticeSkill,
                              obs.nowMs + kNoSelfSafeSpellCooldownMs);
            planner_.Finish(false, "no self-safe spell in book", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        // At oneself. spell::SafeToPractiseOnSelf has already refused every
        // harmful, cursing, summoning, field and non-self-targeted spell, and
        // every spell carrying a flag this client cannot read, so this can
        // neither hurt the caster nor make a criminal of them.
        {
            const spell::SpellDef* d = spell::DefForSpell(spell);
            std::string cost;
            if (d) {
                for (const char* r : d->reagents) {
                    if (!r) break;
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s%s x%d",
                                  cost.empty() ? "" : ", ", r,
                                  static_cast<int>(market::QtyOf(obs.pack, r)));
                    cost += buf;
                }
            }
        LogLine("practice: casting %s (spell %d, circle %d, needs Magery "
                    "%.1f) at myself to raise %s (%.1f, mana %d, pack "
                    "holds %s)",
                    d ? d->name : "?", spell, d ? d->circle : 0,
                    d ? d->minSkillTenths / 10.0 : 0.0,
                    rules::SkillName(skillId), have / 10.0, obs.mana,
                    cost.empty() ? "no reagent cost" : cost.c_str());
        }
        practiceCastMark_ = client.JournalNowMs();
        selfPracticeRuns_ = 0;
        practiceCastSpell_ = spell;
        ++practiceCasts_;
        {
            bool seen = false;
            for (std::pair<int, i32>& c : practiceCastCounts_)
                if (c.first == spell) { ++c.second; seen = true; }
            if (!seen) practiceCastCounts_.push_back({spell, 1});
        }
        client.ActionCastSpell(spell, client.PlayerSerial());
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + kPracticeCastPeriodMs;
        return false;
    }

    // A SELF-USE SKILL NEVER FAILS, SO IT MUST BE BOUNDED.
    //
    // Meditation always answers "You are at peace", so this goal claimed
    // progress on every single tick, never completed, and was then restored
    // next session as a KEPT objective -- "restored objective KEPT:
    // PRACTICE_SKILL (progress 18)". The planner never got another look in.
    // Voris spent a whole life meditating while holding 2 poison potions, 170
    // nightshade and a mortar, and the reason no goal change ever appeared in
    // the log is that no goal change ever happened.
    //
    // Stand down after a stretch of it and let the planner re-decide. If
    // meditating really is the best thing available it wins again immediately;
    // if there is stock to sell or a batch to brew, that now gets its turn.
    // For an alchemist in particular the training IS the crafting -- Alchemy
    // is PracticeBy::Working -- so an unbounded meditation was crowding out
    // the very activity that raises the skill it lives by.
    if (++selfPracticeRuns_ >= kSelfPracticeBeforeRethink) {
        LogLine("practice: %d turns of %s -- standing down so the planner can "
                "look at the rest of this life",
                selfPracticeRuns_, rules::SkillName(skillId));
        return EndPracticeBout(obs, skillId, 60000);
    }

    LogLine("practice: using %s to raise it (%.1f)", rules::SkillName(skillId),
            have / 10.0);
    client.ActionUseSkill(skillId);
    planner_.NoteProgress();
    // Meditation runs for a while and the server decides when it ends. Long
    // enough that this is not a spin, short enough to notice an interruption.
    nextActionMs_ = obs.nowMs + 12000;
    return false;
}

}  // namespace uo::life
