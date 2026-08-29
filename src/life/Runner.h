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
    bool DoBuySupplies(Client& client, const Observation& obs);
    bool DoCraft(Client& client, const Observation& obs);
    bool DriveOpenTrade(Client& client, const Observation& obs);
    void ResetTradeState();
    // Gold, declared tools, stocked consumables, what this life makes and
    // what it makes those from. Everything else is spare -- bankable as dead
    // weight, or sellable as loot.
    bool LifeNeedsGraphic(u16 gfx) const;
    bool DoGetFood(Client& client, const Observation& obs);
    bool DoPracticeSkill(Client& client, const Observation& obs);
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
    // Bounded food errands, and a rest when there is no provisioner.
    // A ghost walking to a healer. Bounded, like every other errand.
    i32  ghostTrips_ = 0;
    static constexpr i32 kMaxGhostTrips = 4;
    i32  foodTrips_ = 0;
    static constexpr i32 kMaxFoodTrips = 3;
    static constexpr i64 kNoFoodCooldownMs = 3 * 60 * 1000;
    // Journal mark taken once at session start: hunger is a STATE, and the
    // last thing the server said about it is still true until it speaks again.
    i64  sessionStartJournalMs_ = 0;
    // --- buying a skill from an NPC ------------------------------------
    static constexpr i32 kMaxTrainTrips = 3;
    // How long TRAIN_AT_NPC rests after finding no trainer, or after one
    // that never answered. The trainers do not move in two seconds, and
    // without a rest the bounded trip count simply restarts forever.
    static constexpr i64 kNoTrainerCooldownMs = 3 * 60 * 1000;
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
    // Deposits of one item that keep failing. See DoBank.
    std::string bankDepositItem_;
    int         bankDepositTries_ = 0;
    static constexpr int kMaxBankDepositTries = 5;
    // --- asking a banker for the box ------------------------------------
    // Bankers who were asked and never opened anything, so the next ask goes
    // to a DIFFERENT one. Hyman, two tiles away, was asked sixty-three times
    // in one session while Lyndon -- who had opened the box six minutes
    // earlier from three tiles -- stood four tiles off and was never asked
    // (run_m5/pair3). Not persisted: silence is about this visit, not about
    // the NPC, and the same rule already governs silent trainers.
    std::vector<u32> bankerSilent_;
    u32  bankerAsked_ = 0;        // who the outstanding ask went to
    u32  bankerCounted_ = 0;      // who bankOpenTries_ is a tally ABOUT
    i32  bankOpenTries_ = 0;      // asks to THIS banker with no box back
    static constexpr i32 kMaxBankOpenTries = 3;
    // Longer than kBankTimeoutMs (6 s, Client.cpp). An ask re-issued inside
    // its own deadline supersedes itself and can never resolve either way.
    static constexpr i64 kBankAskGapMs = 7000;
    // How long BANK stands down after a visit that deposited nothing.
    static constexpr i64 kBankCooldownMs = 5 * 60 * 1000;
    bool trainerApproached_ = false;
    // How many times we have tried to close the distance to THIS trainer.
    // One attempt was the old behaviour and it cost a whole session of
    // shouting at a shop from the street.
    int  trainApproaches_ = 0;
    static constexpr int kMaxTrainApproaches = 3;
    // NPCs of the right trade that were asked and never answered. Session-only
    // and never persisted: silence is not a fact the world stated, so it is
    // not a belief worth keeping -- only somewhere already tried today.
    std::vector<u32> trainerSilent_;
    // Trips taken looking for a hunting ground this goal.
    int huntTrips_ = 0;
    static constexpr int kMaxHuntTrips = 3;
    // --- crafting ----------------------------------------------------------
    std::string supplyItem_;      // the input currently being shopped for
    std::string supplyTrade_;     // the trade that sells it
    int         supplyTrips_ = 0;
    static constexpr int kMaxSupplyTrips = 3;
    // A buy that has been ASKED FOR but not yet settled. The ledger entry is
    // written from the gold the server actually took, on the tick after the
    // action resolves -- never at request time. See DoBuySupplies.
    std::string pendingBuyItem_;
    i32         pendingBuyGoldBefore_ = 0;
    std::string craftItem_;       // what is being made
    i32         craftHadBefore_ = 0;
    i64         craftStartedMs_ = 0;
    int         craftMenuStep_ = 0;   // 0 = not started, 1 = top menu answered
    int         craftMade_ = 0;
    // Shops of the trade already walked to this session. Britain has three
    // mage shops in the atlas; without this the character walks to the
    // nearest one forever, however many times it comes away with nothing.
    std::vector<std::string> trainerShopsTried_;
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
    // A ceiling this buyer has proved it can afford. Halved each time a
    // quoted sale leaves the purse unmoved -- an NPC vendor's own gold is
    // finite and it will list an offer it cannot pay for. 0 = no cap.
    i32   sellLotCap_ = 0;
    // How long EARN_GOLD stands down once every buyer trade has failed.
    // The vendors need a restock cycle; nothing changes in three seconds.
    static constexpr i64 kNoBuyerCooldownMs = 3 * 60 * 1000;
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
    // Until when the player market counts as tried-and-empty.
    i64  marketQuietUntilMs_ = 0;
    static constexpr i64 kMarketQuietMs = 10 * 60 * 1000;   // ten minutes

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
    // The water tile this character has COMMITTED to walking to. Re-picking
    // the nearest tile every tick makes the target move as the character
    // does, so it walks toward a spot it never reaches.
    i32  fishTargetX_ = 0, fishTargetY_ = 0;
    bool fishTargetSet_ = false;
    i64  fishWalkMs_ = 0;
    // Has this character actually REACHED a dock? Until it has, it must not
    // start chasing whatever water it happens to see.
    bool fishAtDock_ = false;

    i32  toolGoldBefore_ = 0;
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
