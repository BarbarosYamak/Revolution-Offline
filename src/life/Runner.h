#pragma once

// ---------------------------------------------------------------------------
// Runner -- the autonomous player, driving a live Client.
//
// This is the M4 counterpart of bot::Scenario, and the difference is the whole
// point of the milestone. A Scenario is a LIST: travel, chop, bank, logout, in
// that order, decided by a human before the run. A Runner is a LOOP: observe,
// assess needs, score goals, commit to one, act, and re-decide. Nothing here
// says what to do next; the planner does, from what the character can see.
//
// Like Scenario, it talks to Client's PUBLIC API only. It has no access to
// sockets, packets or protocol state, and every action it starts is a request
// the server may refuse -- which is what makes "the bot must play the game" a
// structural property rather than a promise.
// ---------------------------------------------------------------------------

#include "uo/life.h"
#include "uo/world_model.h"
#include "uo/types.h"

#include <string>
#include <vector>

namespace uo {

class Client;

namespace life {

struct RunnerConfig {
    std::string dataRoot = "bot_data";
    std::string accountName;
    std::string characterName;
    // Which life this character is starting. Must name an entry in
    // uo::prof::All(); there is no default, because guessing one would
    // silently create the wrong character.
    std::string professionId;

    // Deterministic session limits (M4 brief, Phase 19). Whichever comes
    // first ends the session; the character then finds somewhere safe,
    // persists, and logs out properly.
    i64 sessionLimitMs = 30 * 60 * 1000;
    i32 goalLimit = 0;              // 0 = no goal-count limit

    // Bounded checkpoint frequency. Too often and a 300-bot host is writing
    // constantly; too rarely and a crash loses a session's learning.
    i64 checkpointIntervalMs = 60 * 1000;

    // How far around the character to look for trees.
    int searchRadius = 24;

    bool verbose = true;
};

class Runner {
public:
    Runner();
    ~Runner();

    bool Configure(const RunnerConfig& cfg, std::string* err);

    // Called once per in-world client tick. Cheap when there is nothing to do.
    void Tick(Client& client, i64 nowMs);

    bool Finished() const { return finished_; }
    const PersistentState& State() const { return state_; }
    const Planner& GetPlanner() const { return planner_; }

    // Persist immediately (clean logout, host shutdown, a meaningful change).
    bool Checkpoint(Client& client, i64 nowMs, const char* why);

    // Ends the session deliberately: finish the current safe action, head
    // somewhere safe, persist, and log out.
    void EndSession(const char* why);

private:
    enum class Phase : u8 {
        AwaitWorld = 0,
        Reconcile,
        Live,
        WindDown,      // heading somewhere safe
        LoggingOut,
        Done,
    };

    Observation Observe(Client& client, i64 nowMs) const;
    // Unequip whatever is in the weapon hand, then wear the axe. Two actions,
    // because Sphere will not wear a second weapon over a full hand.
    bool        ArmAxe(Client& client, const Observation& obs);
    void        LearnFromObservation(Client& client, const Observation& obs);
    // Seed the handful of named forests a new character would plausibly have
    // heard of. Runs once per life; everything else is earned.
    static constexpr int kSeedHints = 3;
    void        SeedCommonKnowledge(Client& client, i64 nowMs);
    // Hold the build to its caps: LOCK a planned skill or stat that has
    // reached its target, keep the rest training up. Nothing here raises a
    // value -- it moves the arrow a player clicks.
    void        MaintainBuildLocks(Client& client, const Observation& obs);
    void        RunGoal(Client& client, const Observation& obs);
    void        LogGoalChange(const Observation& obs, const std::string& why);
    void        LogLine(const char* fmt, ...) const;

    // --- goal bodies. Each returns true when the goal is finished. --------
    bool DoSurvive(Client& client, const Observation& obs);
    bool DoHeal(Client& client, const Observation& obs);
    bool DoRecoverCorpse(Client& client, const Observation& obs);
    bool DoGetTool(Client& client, const Observation& obs);
    bool DoReplaceEquipment(Client& client, const Observation& obs);
    bool DoBank(Client& client, const Observation& obs);
    bool DoGatherLogs(Client& client, const Observation& obs);
    bool DoTrainCombat(Client& client, const Observation& obs);
    bool DoEarnGold(Client& client, const Observation& obs);
    bool DoTravel(Client& client, const Observation& obs);
    bool DoTrainAtNpc(Client& client, const Observation& obs);
    bool DoTradeWithPlayer(Client& client, const Observation& obs);
    bool DoFish(Client& client, const Observation& obs);
    bool DriveOpenTrade(Client& client, const Observation& obs);
    void ResetTradeState();
    bool DoIdle(Client& client, const Observation& obs);

    RunnerConfig    cfg_;
    Store           store_{"bot_data"};
    PersistentState state_;
    Planner         planner_;
    NeedConfig      needCfg_;

    Phase phase_ = Phase::AwaitWorld;
    bool  finished_ = false;
    bool  configured_ = false;

    i64 sessionStartMs_ = 0;
    i64 lastCheckpointMs_ = 0;
    i64 lastTickMs_ = 0;
    i64 nextActionMs_ = 0;
    // Build-lock bookkeeping. `statLockSent_` holds `wantedState + 1` so 0
    // means "never sent"; the client is never told a stat's lock state, so
    // there is nothing to reconcile against.
    i64 nextLockCheckMs_ = 0;
    u8  statLockSent_[3] = {0, 0, 0};
    bool lockGateLogged_ = false;
    i64 windDownStartedMs_ = 0;
    i32 windDownTrips_ = 0;
    bool windDownArrived_ = false;

    SessionSummary session_;

    // Transient per-goal working state. NONE of this is persisted -- it is
    // the ephemeral half of the truth split, and mixing it into state.json is
    // exactly the mistake Phase 2 warns about.
    i32  chopX_ = 0, chopY_ = 0;
    i8   chopZ_ = 0;
    u16  chopGraphic_ = 0;
    bool chopTargetValid_ = false;
    bool chopCursorPending_ = false;
    // Swings counted, not timed. A timer that the next swing resets is not a
    // bound at all -- the first live run proved that by chopping one tree for
    // two minutes without ever tripping its "nothing came out" branch.
    // Two. Source-X answers a barren tree on the FIRST swing, so a third is
    // already wasted work -- and with 60% of trees barren by the shard's own
    // resource table, wasted swings dominate the loop.
    static constexpr i32 kMaxSwingsPerTree = 2;
    // One chop is DELAY=1.6 x rand(5)+2 strokes = 3.2 to 9.6 seconds. Swinging
    // again inside that window fires the skill's @Abort trigger and throws the
    // whole attempt away. Wait out the worst case; a yield cuts it short.
    static constexpr i64 kChopResolveMs = 10000;
    // How often to look for one of those answers while waiting. Short enough
    // that a barren tree costs a fraction of a second rather than ten.
    static constexpr i64 kChopPollMs = 400;
    i32  swingsOnTree_ = 0;
    // Journal mark taken when the axe swing is targeted, so the definitive
    // answers below can be read without re-reading the whole journal.
    i64  chopSwungJournalMs_ = 0;
    i32  approachCell_ = 0;   // which of the tree's eight neighbours we have tried
    i32  logsSeen_ = 0;
    std::vector<std::pair<i32, i32>> visitedTrees_;
    // The lead that sent us to the current area, so a dry area is charged
    // against the lead rather than against wherever we happen to stand.
    i32  lastHintX_ = 0, lastHintY_ = 0;
    // Destinations proven treeless THIS SESSION. Transient by design: the
    // world regenerates, so this must not persist.
    std::vector<std::pair<i32, i32>> deadTargets_;
    bool IsDeadTarget(i32 x, i32 y) const;
    // Set when every tree in range has been worked. Forces the next tick to
    // TRAVEL rather than re-survey the same ground; cleared on arrival.
    bool areaExhausted_ = false;
    i32  logsAtGoalStart_ = 0;
    i32  logsAtSessionStart_ = -1;
    u32  currentFoe_ = 0;
    // Chase bound: how long without getting closer before a foe is written off.
    static constexpr i64 kChaseGiveUpMs = 8000;
    i32  chaseBestDist_ = 0;
    i64  chaseProgressMs_ = 0;
    // Stalemate bound. A chase bound is not enough: an ADJACENT foe never
    // stops "closing", so a fight neither side can win runs forever. This
    // window is measured against the one progress signal a client has -- the
    // foe's health bar.
    static constexpr i64 kFightAssessMs = 20000;
    i64  fightStartedMs_ = 0;
    double foeHpAtStart_ = -1.0;
    i64  lastBandageMs_ = 0;
    i64  lastDangerNoteMs_ = 0;   // one danger note per fight, not per tick
    // Journal watermark for the overflow message. Moved forward once the pack
    // has been emptied, so one past overflow does not pin BANK forever.
    i64  overloadWatchMs_ = 0;
    // Bounded bank trips, for the same reason gathering needed one.
    static constexpr i32 kMaxBankTrips = 4;
    i32  bankTrips_ = 0;
    // --- buying a skill from an NPC ------------------------------------
    static constexpr i32 kMaxTrainTrips = 3;
    std::string trainerTrade_;          // paperdoll-title substring to look for
    wm::Service trainerService_ = wm::Service::None;
    i32  trainTrips_ = 0;
    bool trainAsked_ = false;
    bool trainPaid_ = false;
    i64  trainAskedMs_ = 0;       // journal mark: read replies after this
    i64  trainAskedTickMs_ = 0;   // tick mark: how long have we waited
    i64  trainPaidMs_ = 0;
    i32  trainQuoted_ = 0;
    i32  trainSkillBefore_ = 0;
    // Sphere does not push a new skill number after training; a player's
    // client asks for one. Until it does, the old value is all we can see.
    bool trainSkillsAsked_ = false;
    // Asks that got no answer at all. Bounded, and NOT persisted: silence is
    // not evidence about the NPC -- the first live case was the character
    // standing seven z below a scribe on another floor of the castle.
    static constexpr i32 kMaxSilentAsks = 3;
    i32  trainSilentAsks_ = 0;
    u32  trainerSerial_ = 0;
    bool trainerApproached_ = false;
    // Gold-stack serials go stale the moment Sphere splits a stack to make
    // change, and a give to a dead serial is a silent no-op. One refresh
    // before paying, and a bounded number of attempts.
    static constexpr i32 kMaxPayAttempts = 3;
    bool trainPackRefreshed_ = false;
    i32  trainPayAttempts_ = 0;
    // --- EARN_GOLD: selling what this life makes --------------------------
    static constexpr i32 kMaxSellTrips = 3;
    std::string sellItem_;             // defname currently being sold
    std::string sellTrade_;            // paperdoll-title substring to look for
    wm::Service sellService_ = wm::Service::None;
    usize sellBuyerIndex_ = 0;         // which buyer of sellItem_ we are trying
    i32   sellTrips_ = 0;
    i32   sellWanted_ = 0;             // how many units we mean to sell
    i32   sellGoldBefore_ = -1;        // purse before the sale, to verify it
    bool  sellAsked_ = false;          // 0x9E requested
    i64   sellAskedMs_ = 0;
    bool  sellSent_ = false;           // ActionVendorSell issued
    u32   sellVendorSerial_ = 0;       // the vendor we mean to deal with
    bool  sellApproached_ = false;     // walked to them before speaking

    // --- TRADE_WITH_PLAYER ------------------------------------------------
    // How often to repeat an offer. Every tick would be spam a human watching
    // the shard would read as broken, and players do not shout continuously.
    static constexpr i64 kAnnounceIntervalMs = 8000;
    static constexpr i32 kMaxAnnounces = 6;
    // A partner that opens a window and puts nothing in is either stuck or
    // gone; either way this side must not wait on it forever.
    static constexpr i64 kTradeGiveUpMs = 25000;
    market::TradePolicy tradePolicy_;
    market::TradeIntent tradeOffer_;      // what we are announcing
    u32         tradePartner_ = 0;
    std::string tradePartnerName_;
    std::string tradeItem_;
    i32  tradeSellingQty_ = 0;   // >0 = we are the SELLER
    i32  tradeWantQty_ = 0;      // buyer: how many we want
    i32  tradeOfferPrice_ = 0;   // buyer: the price the seller named
    bool tradeOffered_ = false;
    i32  tradePackBefore_ = 0;   // the PACK is the proof, not the packet
    i32  tradeGoldBefore_ = 0;
    i64  tradeHeardMs_ = 0;
    i64  tradeAnnouncedMs_ = 0;
    i64  tradeOpenedMs_ = 0;
    i32  tradeAnnounceCount_ = 0;

    // --- FISH. skill18_fishing.scp: DELAY=8.0, RANGE=4 ---------------------
    // The eight seconds is a CEILING, not a delay: the goal polls for one of
    // Sphere's own verdicts, the same lesson the axe taught.
    static constexpr i64 kFishResolveMs = 9000;
    static constexpr i64 kFishPollMs = 400;
    static constexpr i32 kMaxFishTrips = 3;
    i64 fishCastMs_ = 0;
    i64 fishCastJournalMs_ = 0;
    i32 fishX_ = 0, fishY_ = 0;
    i32 fishSeen_ = 0;
    i32 fishTrips_ = 0;
    bool fishCursorPending_ = false;

    i64  bankOpenedMs_ = 0;   // when the box was last asked for
    i64  lastChopMs_ = 0;
    i32  travelAttempts_ = 0;
    bool travelInFlight_ = false;
    // Foes we proved we could not reach, so a mob behind a wall does not
    // restart the approach every tick (audit section 3.7).
    std::vector<std::pair<u32, i64>> unreachable_;

    bool IsUnreachable(u32 serial, i64 nowMs) const;
    void MarkUnreachable(u32 serial, i64 nowMs);
};

}  // namespace life
}  // namespace uo
