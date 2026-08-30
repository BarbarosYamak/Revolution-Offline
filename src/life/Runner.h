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
#include "uo/activities/buy.h"
#include "uo/activities/disposal.h"
#include "uo/interaction/bank_errand.h"
#include "uo/vendor_errand.h"
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
    // WHAT THIS GRAPHIC MEANS TO THIS CHARACTER. The same heater shield is
    // Produce to the smith who makes them and Wearable to the fencer who
    // fights with one, and how many of it stays in the pack follows from
    // that. See uo/activities/disposal.h -- the count lives there, the
    // classification here, because only the Runner knows the profession.
    ItemRole RoleOfGraphic(u16 gfx) const;
    bool DoGetFood(Client& client, const Observation& obs);
    bool DoPracticeSkill(Client& client, const Observation& obs);
    int  PickPracticeSpell(Client& client, const Observation& obs) const;
    bool DoFillSpellbook(Client& client, const Observation& obs);
    bool DoMakeBandages(Client& client, const Observation& obs);
    bool DoExplore(Client& client, const Observation& obs);
    bool DoMine(Client& client, const Observation& obs);
    bool DoSmelt(Client& client, const Observation& obs);
    // Put enough coin in the pack for a purchase, drawing on the bank. True
    // when it has taken over the tick.
    bool FetchCoinForPurchase(Client& client, const Observation& obs,
                              i32 needed);
    bool DoTameAnimal(Client& client, const Observation& obs);
    bool DoUpgradeGear(Client& client, const Observation& obs);
    bool MayWear(const ArmorPiece& a, const Observation& obs) const;
    bool BookHasGraphic(Client& client, u32 book, u16 graphic) const;
    bool BuyScrollFrom(Client& client, const Observation& obs, const char* trade,
                       wm::Service svc, u16 graphic, bool skipKnown, u16 qty,
                       const char* what, GoalKind owner);
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
    // M7 disposal: the "will not wear" inventory is reported once per
    // session, not on every gear tick.
    bool dispositionLogged_ = false;
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
    // When the foe was last asked for its health (0x34).
    i64  foeHpAskedMs_ = 0;
    double foeHpAtStart_ = -1.0;
    i64  lastBandageMs_ = 0;
    i64  lastDangerNoteMs_ = 0;   // one danger note per fight, not per tick
    // Journal watermark for the overflow message. Moved forward once the pack
    // has been emptied, so one past overflow does not pin BANK forever.
    i64  overloadWatchMs_ = 0;
    // Bounded bank trips, for the same reason gathering needed one.
    static constexpr i32 kMaxBankTrips = 4;
    // Bounded food errands, and a rest when there is no provisioner.
    // A ghost walking to a healer. Bounded, like every other errand.
    // Who we were last fighting, by NAME, and whether this death has
    // already been credited to it. Latched so one death is one verdict.
    std::string currentFoeName_;
    bool deathBlamed_ = false;
    i32  ghostTrips_ = 0;
    static constexpr i32 kMaxGhostTrips = 4;
    // A SPELL YOU DO NOT HAVE STAYS UNCAST, however much Magery you own.
    // Voris had Magery 50.0 and no Create Food in his book, and asked for it
    // every six seconds for a whole session: "The spell is not in your
    // spellbook." Skill is not capability -- the book is.
    bool noCreateFoodSpell_ = false;
    i32  spellbookTrips_ = 0;
    GoalKind buyTripsOwner_ = GoalKind::Count;
    // Set when the scribe -- the only seller that lets a spell be CHOSEN --
    // turns out to be unreachable or to stock nothing this book lacks. After
    // that the mage shop's random scroll is better than no scroll.
    bool scribeExhausted_ = false;
    i32  tradeTrips_ = 0;
    i32  vendorChases_ = 0;
    i32  bandageTrips_ = 0;
    // How many times the bandage errand has asked who is standing in the
    // healer's shop. Reset on success; three unanswered scans stand the
    // goal down instead of re-walking to the same tile.
    i32  healerScans_ = 0;
    // Consecutive "you can't reach that" refusals from the forge. A
    // refusal means walk, not click again; see DoSmelt.
    i32  smeltReachFails_ = 0;
    // The bandage purchase, as a shared errand rather than inline steps.
    // One per buying goal, deliberately: the trip and chase counters used to
    // be Runner members shared between goals, and a gear trip spent the
    // spellbook's allowance.
    // Purchases, as activities rather than inline steps. One per buying
    // goal deliberately: the trip and chase counters used to be Runner
    // members shared between goals, and a gear trip spent the spellbook's
    // allowance.
    life::BuyActivity bandageBuy_;
    // Heal potions, for lives whose Healing skill cannot make a bandage work.
    life::BuyActivity potionBuy_;

    // The resurrection robe is worth sixteen bandages, and it is only safely
    // identifiable in the minutes right after coming back. See
    // CutResurrectionRobe.
    bool wasDead_ = false;
    i64  resurrectedAtMs_ = 0;
    // Atlas ids already walked to this session. A place record cannot hold
    // this: NotePlace collapses two places that share a tile into one, and
    // the survivor's name flips between them. See DoExplore.
    std::vector<std::string> exploredIds_;
    bool CutResurrectionRobe(Client& client, const Observation& obs);
    // Shirt, trousers, shoes. Not armour and not decoration -- a character
    // that has just been resurrected owns nothing else.
    bool WearBasicClothing(Client& client, const Observation& obs);
    life::BuyActivity clothingBuy_;
    life::BankErrand   bankErrand_;
    life::VendorErrand foodErrand_;
    i32  toolTrips_ = 0;
    // The rock currently being struck: position, the z of its visible
    // surface, and 0 for rock land or the rock static's id (a cave floor is a
    // static and is answered as one -- see DoMine's cursor reply).
    i32  mineX_ = 0, mineY_ = 0;
    // Smelting: when the last double-click went out, how much metal was in the
    // pack before it, and how many fruitless trips to a smithy have been made.
    i64  smeltStartedMs_ = 0;
    i32  smeltIngotsBefore_ = 0;
    i32  smeltTrips_ = 0;
    i64  toolTitlesAskedMs_ = 0;
    i32  coinLiftFails_ = 0;
    // Who was standing there when an offer went unanswered, so the same room
    // is not shouted at twice.
    u32  tradeAudienceIgnored_ = 0;
    // How much coin a pending purchase needs in the PACK. Drives NeedBank so
    // the existing bank goal fetches it; zero when nothing is waiting on money.
    i32  coinWanted_ = 0;
    // Blacksmithing: the hammer arms a cursor that wants an ingot before the
    // menu will open.
    // Whether ".makelast" has already been issued for the item being made, so
    // the batch command goes out once per sitting rather than once per item.
    // Consecutive turns spent on a self-use skill that cannot fail.
    i32  selfPracticeRuns_ = 0;
    // When the current item was ordered from the craft menu, so the next one
    // is not started on top of it.
    // The wait for a craft to actually produce something. A Handshake, not
    // a timestamp: it counts attempts, so a recipe that never lands gives
    // up instead of repeating for a whole session.
    life::Handshake craftWait_;
    bool makeLastIssued_ = false;
    bool craftCursorPending_ = false;
    i64  craftClickedMs_ = 0;
    // Forges that refused from every tile that could be reached, so the next
    // look offers a different one.
    std::vector<std::pair<i32, i32>> deadForges_;
    i32  smeltForgeX_ = 0, smeltForgeY_ = 0;
    i32  smeltRefusals_ = 0;
    i32  smeltApproaches_ = 0;
    bool smeltCursorPending_ = false;
    i64  smeltClickedMs_ = 0;
    // Smithies already walked to and found wanting, so the next trip goes to a
    // different one instead of the same nearest.
    std::vector<std::string> smeltSkipPlaces_;
    i8   mineZ_ = 0;
    u16  mineGraphic_ = 0;
    i64  mineSwungMs_ = 0, mineJournalMs_ = 0;
    bool mineCursorPending_ = false;
    // Set when the current vein dies (dead-listed by a server refusal): the
    // next scan starts from a jittered point so the miner works INTO the mine
    // instead of camping its mouth -- "there is more space in the mine dont
    // only mine at the entrance" (project owner, 2026-08-29).
    bool mineRoam_ = false;
    i32  tameTrips_ = 0;
    i32  mineTrips_ = 0;
    std::string exploreTarget_;
    bool spellbookOpened_ = false;
    // The scroll graphic last dropped on the book, and the spell count before
    // it, so the next tick can tell a real add from a refusal.
    u16  scrollOfferedGraphic_ = 0;
    i32  spellsBeforeOffer_ = 0;
    // Graphics this book refused -- spells it already knows. Never offered
    // again, which is what stops the drop/refuse/retry loop.
    std::vector<u16> scrollBookRefused_;
    i64  scrollBuyMark_ = 0;
    i64  createFoodMark_ = 0;
    static constexpr i32 kMaxFoodTrips = 3;
    // GET_TOOL is in the Emergency family and therefore exempt from
    // satiation, so a cooldown is its only brake. It had none.
    static constexpr i64 kNoToolCooldownMs = 3 * 60 * 1000;
    static constexpr i64 kNoFoodCooldownMs = 3 * 60 * 1000;
    // Below this there is no point walking to a shop. A loaf is a few coins;
    // this is "can I buy anything at all", not a price.
    static constexpr i32 kFoodMoney = 20;
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
    // The purse before a lesson, so a fee that was taken can be told from
    // one that was not. See DoTrainAtNpc.
    i32  trainGoldBefore_ = 0;
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
    // and the SERVICE it maps to, so a shopkeeper whose title differs from
    // the trade word -- "fisherwoman" against "fisher" -- is still found.
    wm::Service supplyService_ = wm::Service::None;
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
    // How far a REMEMBERED safe spot may be before the wind-down asks the
    // atlas for a nearer one instead. Roughly "still in this town": a walk
    // across Britain is fine, a walk to the next city is how a character ends
    // its session in open country and is killed after the disconnect.
    static constexpr i32 kWindDownPreferKnownWithin = 150;
    // Same rule for a remembered shop: familiar is worth a walk across town,
    // never a walk across the world. Corwyn died twice making the latter.
    static constexpr i32 kReturnToKnownBuyerWithin = 150;
    // How long the wind-down may spend reaching safety, and the longer grace
    // it gets once safety is nearly in reach. An unreachable target must not
    // hold the session open forever; a target thirty tiles away is worth
    // waiting for, because the alternative is a corpse.
    static constexpr i64 kWindDownBudgetMs = 2 * 60 * 1000;
    static constexpr i64 kWindDownGraceMs  = 5 * 60 * 1000;
    // No step in this long means stuck, not slow.
    static constexpr i64 kWindDownStalledMs = 12 * 1000;
    i32 windDownLastX_ = -1;
    i32 windDownLastY_ = -1;
    i64 windDownMovedMs_ = 0;

    i32   sellLotCap_ = 0;
    // ONE VISIT, EVERYTHING SPARE. The vendor names its whole buy list in a
    // single 0x9E, and the old path sold the one item the goal was about and
    // reported success -- v4_Corwyn took 72 gold for two daggers and left six
    // heater shields worth 366 in his pack. After each confirmed sale the
    // errand re-asks the same vendor and offers whatever else is surplus;
    // this bounds that, so a mispriced list cannot loop.
    static constexpr i32 kMaxSellSweeps = 8;
    i32   sellSweeps_ = 0;
    i32   sellSweepGold_ = 0;          // gold taken across this whole visit
    // WHICH ITEM THE PENDING SALE SHOULD BE VERIFIED AGAINST. Usually
    // sellItem_, but a surplus sweep offers whatever the vendor listed, and
    // checking the pack for the goal's item while selling shields would credit
    // a sale that never happened. Empty means the pack half cannot be checked
    // -- econ maps only 63 graphics to defnames -- and the purse alone decides.
    std::string sellVerifyItem_;
    // How many disposal tuning says stays behind, per role.
    life::DisposalTuning disposal_;
    // How long EARN_GOLD stands down once every buyer trade has failed.
    // The vendors need a restock cycle; nothing changes in three seconds.
    static constexpr i64 kNoBuyerCooldownMs = 3 * 60 * 1000;
    i32   sellGoldBefore_ = -1;        // purse before the sale, to verify it
    // ...and what the pack held, so a sale is confirmed by goods LEAVING as
    // well as gold arriving. See DoEarnGold.
    i32  sellItemBefore_ = -1;
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
