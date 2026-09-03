#include "RunnerInternal.h"

namespace uo::life {
// The families were one translation unit until the split; the
// using-directive keeps unqualified lookup in these bodies identical
// to what the old anonymous namespace gave them.
using namespace runner_detail;


// ---------------------------------------------------------------------------
// TAMING.
//
// "add taming goal to cassia" (project owner, 2026-08-29). A tamer had no goal
// either: Cassia spent a whole session doing nothing but EXPLORE, because
// every other need she had was blocked and taming was not something she could
// want.
//
// skills/skill35_taming.scp: SKILL 35, PROMPT_MSG="Tame which animal?", so it
// targets a MOBILE and ActionUseSkill answers the cursor itself.
//
// WHAT to tame is read from the shard rather than guessed:
// data/revolution_creatures.tsv carries each chardef's TAMING requirement, and
// 109 of the 450 creatures are tamable at all. The hardest one this character
// can actually manage is the right pet -- a Rat needs 0.9, a Sheep 11.1 -- and
// anything above its skill is refused all day for nothing.
//
// ScanMobiles, not ScanHostiles: the latter excludes blue and green BY DESIGN,
// and a sheep is innocent. Innocent is exactly what a tamer wants.
//
// THE NAMES ARE THE OBSERVATION, AND THEY COST A PACKET. This handler filters
// on m.name because the creature table is keyed by name -- but a name only
// exists in the client after the 0x98 AllNames query that ActionScanMobiles
// issues (Client.cpp PrintNearbyMobiles), and this path never issued it. Rhea
// (Taming 50.0, wave 2026-09-02) therefore walked to all three pastures and
// logged "nothing tamable here" 60 ms after each arrival with eight
// c_sheep_woolly inside ten tiles. Ask first, judge after: see
// include/uo/activities/tame.h.
bool Runner::DoTameAnimal(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;
    LoadSeededCreatureDanger(client.DataDir());

    const double mySkill = obs.SkillTenths(rules::kTaming) / 10.0;

    // ------------------------------------------------------------------
    // 1. WHAT DID THE SHARD SAY to the last attempt? Sphere answers taming
    // in words and nothing else: CCharSkill.cpp CChar::Skill_Taming sends
    // DEFMSG_TAMING_* (runtime/scripts/core/messages.scp:911-922) and
    // skill35_taming.scp adds @Fail/@Abort. Success is "It seems to accept
    // you as master" / "...accepts you once more as it's master"; the refusals
    // are permanent facts about this animal or this character, so they end the
    // attempt instead of being retried into the ground.
    if (tameAskedMs_ > 0) {
        // SPHERE ANSWERS TAMING AS A SYSTEM MESSAGE, AND THE SPEECH READER
        // THROWS THOSE AWAY. Client::JournalHeardSince drops every entry
        // whose sourceSerial is 0 or 0xFFFFFFFF (ClientTravel.cpp:2580-2581)
        // -- 'System messages carry no speaker to walk to' -- and
        // CChar::Skill_Taming says everything through SysMessage/SysMessagef
        // (Source-X CCharSkill.cpp:2280-2330). So this handler could not hear
        // its own answer: Rhea asked a sheep eight times over ninety seconds
        // while the shard replied 'Sheep is already tame.' after every one
        // (run_gates/g_Rhea.console.txt 13:35:06-13:36:22, run 2 of
        // 2026-09-02). JournalSaidSince reads the whole journal, system lines
        // included, which is what a player's screen shows.
        static const char* const kMastered[] = {
            "as master", "as it's master", "as its master",
        };
        // Permanent facts about this animal or this character: retrying them
        // is the futile loop above.
        static const char* const kRefused[] = {
            "too many followers", "cannot be tamed", "is already tame",
            "you can't tame", "your own master", "too far away",
        };
        for (const char* phrase : kMastered) {
            if (!client.JournalSaidSince(phrase, tameAskedMs_)) continue;
            // WHAT HAPPENS TO THE PET IS SPHERE'S BUSINESS, not ours.
            // Skill_Taming ends with NPC_PetSetOwner(this) and
            // Skill_Start(NPCACT_FOLLOW_TARG) (CCharSkill.cpp), so the animal
            // follows its new master by itself. This runner has no pet system
            // and does not pretend to one -- it records the tame, credits the
            // progress, and stands the goal down.
            LogLine("tame: success %s serial=0x%08X req=%.1f -- accepts me as "
                    "master (Taming %.1f, %d attempt(s)); it follows me now",
                    tameTargetName_.c_str(), tameTarget_, tameTargetReq_,
                    mySkill, tameAttempts_);
            state_.memory.NotePlace("pasture", "tamed here", obs.x, obs.y,
                                    obs.z, obs.nowMs);
            planner_.NoteProgress();
            planner_.Finish(true, "tamed an animal", obs.nowMs);
            tameAskedMs_ = 0; tameAttempts_ = 0; tameTrips_ = 0;
            tameTarget_ = 0; tameTargetName_.clear(); tameScanMs_ = 0;
            tameTargetReq_ = -1.0; tameVisited_.clear(); tameRefused_.clear();
            planner_.Cooldown(GoalKind::TameAnimal, obs.nowMs + kNoPetCooldownMs);
            return true;
        }
        for (const char* phrase : kRefused) {
            if (!client.JournalSaidSince(phrase, tameAskedMs_)) continue;
            // THE SHARD'S OWN WORDS ARE THE REASON. One refused animal is not
            // a refused goal, though: this creature is remembered as a dead
            // end and the next tick judges the spot again, which is what a
            // player does when a sheep turns out to be somebody's pet.
            LogLine("tame: '%s' refused -- the shard said \"%s\"; leaving it "
                    "alone", tameTargetName_.c_str(), phrase);
            tameRefused_.push_back(tameTarget_);
            tameAskedMs_ = 0; tameAttempts_ = 0;
            tameTarget_ = 0; tameTargetName_.clear(); tameTargetReq_ = -1.0;
            if (tameRefused_.size() >= static_cast<usize>(kMaxTameRefusals)) {
                LogLine("goal_failed=TAME_ANIMAL reason=\"%d animal(s) refused "
                        "me, last said \"%s\"\"",
                        static_cast<int>(tameRefused_.size()), phrase);
                planner_.Cooldown(GoalKind::TameAnimal,
                                  obs.nowMs + kNoPetCooldownMs);
                planner_.Finish(false, "the animals refused me", obs.nowMs);
                tameTrips_ = 0; tameScanMs_ = 0;
                tameRefused_.clear(); tameVisited_.clear();
            }
            return false;
        }
    }

    // ------------------------------------------------------------------
    // 2. ASK FOR THE NAMES before reading them. One scan per spot: it is
    // re-armed on arrival at a pasture (below) and by the first tick of the
    // goal, and nothing may be called empty until it has come back.
    if (tameScanMs_ == 0) {
        if (client.TravelBusy()) return false;
        LogLine("tame: reading the names of everything nearby before judging "
                "this spot (Taming %.1f)", mySkill);
        client.ActionScanMobiles();
        tameScanMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    std::vector<Client::HostileHit> nearby;
    client.ScanMobiles(12, nearby);

    u32 best = 0;
    double bestReq = -1.0;
    std::string bestName;
    for (const Client::HostileHit& m : nearby) {
        if (m.name.empty()) continue;
        const double req = SeededTamingFor(m.name);
        if (req < 0.0) continue;            // not a tamable creature at all
        bool refusedBefore = false;         // already said no, this session
        for (usize r = 0; !refusedBefore && r < tameRefused_.size(); ++r)
            refusedBefore = tameRefused_[r] == m.serial;
        if (refusedBefore) continue;
        if (req > mySkill) continue;        // beyond this character today
        if (best == 0 || req > bestReq) {   // the best it can actually manage
            best = m.serial; bestReq = req; bestName = m.name;
        }
    }

    if (!best) {
        // NOT YET. An empty result is only believable once the 0x98 replies
        // have landed and a settle window has passed -- everything before that
        // is a client that has not been told the names yet, which is exactly
        // how three pastures full of sheep read as deserted.
        TameScanSight sight;
        sight.scanIssued = tameScanMs_ > 0;
        sight.namesPending = client.MobileNamesPending();
        sight.msSinceScan = obs.nowMs - tameScanMs_;
        if (!MayJudgeEmpty(sight, kTameSettleMs)) {
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }

        int named = 0;
        for (const Client::HostileHit& m : nearby) if (!m.name.empty()) ++named;

        // GO WHERE THE ANIMALS ARE. Standing down was wrong: Cassia lives in
        // Britain, a city, and cities have almost nothing tamable in them. She
        // logged "nothing in sight" once and then spent the session BLOCKED on
        // a three-minute cooldown, twelve times over, while sheep grazed
        // outside the walls.
        //
        // A miner travels to ore and a fisher to water; a tamer travels to
        // ANIMALS -- not to sheep. The herds come from the world save
        // (data/revolution_tamables.tsv, every chardef with a TAMING
        // requirement), and the choice is Sphere's own arithmetic: nothing
        // above this character's skill, the gain window preferred, nearest
        // first, and never further than the clock allows
        // (include/uo/activities/tame.h).
        if (client.TravelBusy()) return false;
        LoadTamables(client.DataDir());
        std::vector<TameCluster> spots = Tamables();
        // Drop the one we are standing in -- it has just been scanned and read
        // empty -- and every herd this goal has already walked to today.
        for (usize i = 0; i < spots.size(); ) {
            const i32 r = spots[i].radius > 0 ? spots[i].radius : 8;
            bool drop = TileDist(obs.x, obs.y, spots[i].x, spots[i].y) <= r;
            for (usize v = 0; !drop && v < tameVisited_.size(); ++v)
                drop = tameVisited_[v].first == spots[i].x &&
                       tameVisited_[v].second == spots[i].y;
            if (drop) spots.erase(spots.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        }
        const i64 remainingMs =
            cfg_.sessionLimitMs - (obs.nowMs - sessionStartMs_);
        const i32 budgetTiles = TameTravelBudgetTiles(
            remainingMs, kWindDownBudgetMs, kTameWorkReserveMs);
        const int pick = ChooseTameCluster(spots, obs.x, obs.y, mySkill,
                                           budgetTiles);
        if (pick >= 0 && ++tameTrips_ <= kMaxTameTrips) {
            const TameCluster& p = spots[static_cast<usize>(pick)];
            LogLine("tame: nothing tamable here -- %d named mobile(s) in sight, "
                    "none tamable at Taming %.1f; walking out to %s at %d,%d "
                    "(needs Taming %.1f, %d tiles of %d affordable, %s, "
                    "trip %d)",
                    named, mySkill, p.label.c_str(), p.x, p.y, p.req,
                    TileDist(obs.x, obs.y, p.x, p.y), budgetTiles,
                    TameCanGain(p.req, mySkill) ? "can still gain"
                                                : "too easy to gain",
                    tameTrips_);
            tameVisited_.push_back(std::make_pair(p.x, p.y));
            travelInFlight_ = client.TravelToPoint(p.x, p.y,
                                                   p.radius > 0 ? p.radius : 8,
                                                   "herd");
            // Arriving somewhere new means the names must be asked for again.
            tameScanMs_ = 0;
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        LogLine("goal_failed=TAME_ANIMAL reason=\"nothing tamable after %d "
                "trips, %d named mobile(s) at the last one, %d herd(s) known "
                "and none within %d tiles at Taming %.1f\"",
                tameTrips_, named, static_cast<int>(spots.size()), budgetTiles,
                mySkill);
        tameVisited_.clear(); tameRefused_.clear();
        planner_.Cooldown(GoalKind::TameAnimal, obs.nowMs + kNoPetCooldownMs);
        planner_.Finish(false, "nothing tamable in reach", obs.nowMs);
        tameTrips_ = 0;
        tameScanMs_ = 0;
        return false;
    }
    tameTrips_ = 0;

    i32 tx = 0, ty = 0; i8 tz = 0;
    if (client.MobilePosition(best, &tx, &ty, &tz)) {
        const i32 d = TileDist(obs.x, obs.y, tx, ty);
        if (d > 2) {
            LogLine("tame: '%s' is %d tiles away -- walking up",
                    bestName.c_str(), d);
            travelInFlight_ = client.TravelToEntity(best, 2);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
    }

    // A TAME IS SEVERAL ATTEMPTS, NOT ONE. Skill_Taming rolls 2-5 strokes
    // (m_atTaming.m_dwStrokeCount, CCharSkill.cpp) and skill35_taming.scp's
    // @Fail is an ordinary outcome, so keep asking -- but bounded, so an
    // animal this character will never manage is given up on honestly.
    if (tameTarget_ != best) { tameTarget_ = best; tameAttempts_ = 0; }
    if (++tameAttempts_ > kMaxTameAttempts) {
        LogLine("goal_failed=TAME_ANIMAL reason=\"'%s' resisted %d attempts "
                "(needs Taming %.1f, have %.1f)\"", bestName.c_str(),
                tameAttempts_ - 1, bestReq, mySkill);
        planner_.Cooldown(GoalKind::TameAnimal, obs.nowMs + kNoPetCooldownMs);
        planner_.Finish(false, "the animal resisted", obs.nowMs);
        tameAttempts_ = 0; tameTarget_ = 0; tameAskedMs_ = 0; tameScanMs_ = 0;
        tameVisited_.clear(); tameRefused_.clear();
        return false;
    }
    tameTargetName_ = bestName;
    tameTargetReq_ = bestReq;
    LogLine("tame: trying '%s' (needs Taming %.1f, have %.1f, %s, attempt %d)",
            bestName.c_str(), bestReq, mySkill,
            TameCanGain(bestReq, mySkill) ? "gainful" : "too easy to gain",
            tameAttempts_);
    // Mark the journal BEFORE the attempt so the answer above reads only this
    // attempt's messages.
    tameAskedMs_ = client.JournalNowMs();
    client.ActionUseSkill(rules::kTaming, best);
    planner_.NoteAttempt(obs.nowMs);
    // DELAY=2.0 and taming usually takes several attempts.
    nextActionMs_ = obs.nowMs + 6000;
    return false;
}

}  // namespace uo::life
