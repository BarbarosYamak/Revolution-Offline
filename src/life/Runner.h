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
#include "uo/activities/acquire.h"
#include "uo/activities/buy.h"
#include "uo/activities/craft.h"
#include "uo/activities/disposal.h"
#include "uo/activities/heal.h"
#include "uo/activities/rest.h"
#include "uo/activities/recovery.h"
#include "uo/activities/train.h"
#include "uo/interaction/bank_errand.h"
#include "uo/spellcast.h"
#include "uo/vendor_errand.h"
#include "uo/types.h"

#include <map>
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
    // What every new player already knows: the home bank/healer/provisioner
    // and a resource-area lead near home. Delegates to the pure
    // uo::life::SeedNewbieKnowledge (newbie_knowledge.h); runs once per life,
    // guarded by Memory event "newbie_knowledge_seeded". See Runner.cpp.
    void        SeedNewbieKnowledge(Client& client, i64 nowMs);
    // Hold the build to its caps: LOCK a planned skill or stat that has
    // reached its target, keep the rest training up. Nothing here raises a
    // value -- it moves the arrow a player clicks.
    void        MaintainBuildLocks(Client& client, const Observation& obs);
    void        RunGoal(Client& client, const Observation& obs);
    void        LogGoalChange(const Observation& obs, const std::string& why);
    // HOW THE DAY WAS SPENT, as one greppable line -- R1's exit proof
    // (families>=4, none above half the picks) plus self_superseded, the
    // "goal_changed=X from=X" count. Called from the clean WindDown logout
    // AND from Checkpoint (gated, see kHistogramIntervalMs) so a crash, a
    // disconnect, or an operator-killed session still leaves a verdict.
    // Pure formatting -- the arithmetic is uo::life::SummariseGoalPicks
    // (Goals.cpp), reachable by ctest independent of this method.
    void        LogGoalHistogram() const;
    void        LogLine(const char* fmt, ...) const;
    // The one place a plan's step is logged -- once per plan change, not per
    // tick. Callers pass e.g. HealStepName(p.step); see S2_WIRING_PLAN.md S2.0.
    void        LogPlan(const char* kind, const char* reason) const;
    // Drain Planner::TakeSpinDetected and say so out loud. Called from BOTH
    // places a goal can end -- the completion path in RunGoal and the
    // attempts-exhausted path inside Planner::Select -- because a spin
    // reported only on completion is invisible to the goal that never
    // completes.
    void        LogSpinIfDetected();
    // An ERRAND's per-tick reason, logged only when it CHANGES or once a
    // minute -- the same rule as LogPlan's step sentinels, applied to text
    // rather than to an enum.
    //
    // The errands report a reason every tick by design (a refusal nobody can
    // read is the defect that layer exists to end), but a caller that prints
    // every one of them prints the tick rate: 214 "potions:" lines in
    // run_r4/w_Bruin.console.txt, of which 209 said "an action is already in
    // flight". `tag` is the caller's own label ("potions", "bank"), and each
    // tag keeps its own sentinel so one errand's chatter cannot silence
    // another's.
    void        LogErrandReason(const char* tag, const char* reason,
                                i64 nowMs) const;
    // The ONLY legal way a plan hands the turn to another goal: cools `from`,
    // finishes it as a no-op, logs the handoff, and delays the next action.
    // `to` is advisory only -- Planner::Select picks the actual receiver.
    // Always returns false (the goal did not complete this tick). `nowMs` is
    // `obs.nowMs` at every call site -- there is no cached Runner-side clock
    // member, so it is taken as a parameter rather than read from one.
    bool        HandOff(GoalKind from, GoalKind to, i64 restMs, const char* why,
                        i64 nowMs);

    // GENERALISES kMarketTripBudgetMs (below) past TRADE_WITH_PLAYER. A
    // service pick can land far enough away that the walk alone eats the
    // rest of the session -- BUY_SUPPLIES sent a Skara Brae fisher through a
    // working moongate hop to an island 232 tiles and one gate away with 24
    // minutes of session left, and nothing asked whether that fit before
    // wind-down had to start (docs/LIFE_GATE_WAVE1.md theme 2,
    // run_gates/g_Dorvar.console.txt 00:40-01:04: "wind-down: the trip has
    // run past its deadline ... logging out where I stand", in the open,
    // near Ocllo).
    //
    // Call this ONLY while `client.TravelBusy()` is already true: the tile
    // count is not known until the route planner has actually run
    // (Client::TravelLastPlannedTiles(), the same number the
    // "[travel] plan ... ~N tiles" log line reports), which happens a tick
    // after TravelToXxx() returns, not inside that call. Returns true when
    // the trip in flight still fits (or the plan is not in yet, or session
    // limits are off -- nothing to veto). Returns false AND ends the goal --
    // aborts the trip, cools the goal down, calls planner_.Finish(false, ...)
    // -- when the plan turns out to cost more than the session has left,
    // after reserving kWindDownBudgetMs for wind-down itself.
    bool VetoTripOverSessionBudget(Client& client, const Observation& obs,
                                   GoalKind goal, const char* goalName,
                                   i64 cooldownMs);

    // --- goal bodies. Each returns true when the goal is finished. --------
    bool DoSurvive(Client& client, const Observation& obs);
    bool DoHeal(Client& client, const Observation& obs);
    bool DoRecoverCorpse(Client& client, const Observation& obs);
    bool DoGetTool(Client& client, const Observation& obs);
    bool DoReplaceEquipment(Client& client, const Observation& obs);
    bool DoBank(Client& client, const Observation& obs);
    // Send one item into the bank box and remember that it is unsettled.
    // Deliberately does NOT call NoteProgress() -- see bankItemMovePending_.
    void IssueBankItemMove(Client& client, const Observation& obs, u32 serial,
                           u16 amount, u32 box);
    // Read the outcome of the last IssueBankItemMove. Returns true when the
    // caller should give up this tick (the box was let go / the goal stood
    // down); false when there is nothing outstanding or it landed.
    bool SettleBankItemMove(Client& client, const Observation& obs);
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
    // Is the shard's market place (market::kMarketBankPlaceId) usable at all:
    // present in the atlas, offering Service::Banker, and guarded? Resolved
    // once per life and cached; logs the answer the first time. When it is
    // not, the trade errand keeps today's nearest-bank behaviour rather than
    // inventing coordinates for a place the atlas does not have.
    bool MarketPlaceUsable(Client& client);
    // Standing at the market, judged by GEOMETRY. `obs.atBank` means the BOX
    // IS OPEN (Runner::Observe), so a buyer standing at the Britain bank with
    // a shut box would re-issue the journey forever.
    bool AtMarketBank(const Client& client) const;
    // Standing near ANY bank -- geometry, judged against the nearest bank the
    // atlas knows of at all, not the one designated market place AtMarketBank
    // tests. DoBank's own "have I actually reached a counter yet" question,
    // used before handing off to bankErrand_ (see DoBank).
    bool NearAnyBank(Client& client, const Observation& obs) const;
    // THE BOX IS THE TRUTH; state_.bank is only a memory of it. Called when an
    // open box has been asked for a remembered stock and has none of it, so
    // the memory stops sending the character on trips it cannot honour.
    void ForgetBankedStock(const char* item);
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
    // What to cast for practice, or what the pack is short of. Reads the book
    // and the pack and defers the actual choice to uo::spell (unit-tested in
    // tests/m4_life.cpp); this wrapper only gathers the observation.
    spell::PracticeChoice PickPracticeSpell(Client& client,
                                            const Observation& obs) const;
    bool DoFillSpellbook(Client& client, const Observation& obs);
    bool DoMakeBandages(Client& client, const Observation& obs);
    // Sheep -> wool -> yarn -> bolt -> cloth, for a life whose CRAFT is
    // blocked on cloth and whose WTB window found no seller. Walks the same
    // five gestures DoMakeBandages does and stops at cloth instead of going
    // on to bandages. See the definition for why every step is measured by an
    // inventory delta rather than by having issued the click.
    bool DoMakeCloth(Client& client, const Observation& obs);
    // BUY A RIDING HORSE FROM AN ANIMAL TRAINER AND MOUNT IT. Same shop
    // shape as DoGetTool (travel to the trade, scan titles, walk up, open,
    // read the offer, buy). The purchase releases the animal at our feet;
    // the last step double-clicks it and the paperdoll (obs.mounted) is
    // the proof.
    bool DoBuyMount(Client& client, const Observation& obs);
    // WALK UP TO A SPINNING WHEEL OR A LOOM BEFORE CLICKING IT. True only when
    // the station is within reach NOW; otherwise the walk (or the strike-off)
    // has already been started and the caller should return false. Same shape
    // as the forge approach in DoSmelt -- TravelToPoint to a walkable tile
    // BESIDE the station, and two approaches, never four (owner rule,
    // 2026-09-02).
    bool ReachStation(Client& client, const Observation& obs, u32 station,
                      const char* what);
    // DecideRest (include/uo/activities/rest.h), shared by DoExplore and
    // DoIdle -- both are now two-line forwarders into this. `owner` is
    // whichever of the two the planner actually picked, purely for the
    // goal_stagnant log line and the HandOff `from`; the STEP taken (explore,
    // rest, settle, stand down as stagnant) is the same regardless of which
    // one asked. See S2_WIRING_PLAN.md S2.2.
    bool RestTick(Client& client, const Observation& obs, GoalKind owner);
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
    // Is ANY armour piece this life may legally wear (MayWear) actually on
    // the paperdoll right now? Same kArmorPieces table DoUpgradeGear's wear
    // pass uses, read rather than acted on -- see the early-hunting-grounds
    // gear-first check in DoTrainCombat.
    bool HasBasicArmor(Client& client, const Observation& obs) const;
    bool BookHasGraphic(Client& client, u32 book, u16 graphic) const;
    // The same question in the book's own currency -- see the note on
    // BookHasSpell for why a spellbook row's GRAPHIC answers nothing.
    bool BookHasSpell(Client& client, u32 book, int spell) const;
    bool BuyScrollFrom(Client& client, const Observation& obs, const char* trade,
                       wm::Service svc, u16 graphic, bool skipKnown, u16 qty,
                       const char* what, GoalKind owner);
    // The scroll errand giving up: bumps the consecutive count, cools
    // FILL_SPELLBOOK for the escalating rest and clears the shopping clock.
    // Returns the rest in ms so the caller can say so in its log line.
    i64  StandDownFromScrollShopping(const Observation& obs, const char* why);
    bool DoIdle(Client& client, const Observation& obs);

    RunnerConfig    cfg_;
    Store           store_{"bot_data"};
    PersistentState state_;
    Planner         planner_;
    NeedConfig      needCfg_;
    // Which product this life has been sitting on, so a full_crafter's day is
    // several crafts and not one repeated. Session-scoped on purpose: the
    // owner's rule is about the shape of a DAY, and a preference that survived
    // a logout would decide tomorrow's first sitting from yesterday's mood.
    CraftFocus      craftFocus_;

    Phase phase_ = Phase::AwaitWorld;
    bool  finished_ = false;
    bool  configured_ = false;

    i64 sessionStartMs_ = 0;
    i64 lastCheckpointMs_ = 0;
    // Gate for LogGoalHistogram's Checkpoint call -- the clean-WindDown call
    // is unconditional and stamps this too, so the periodic Checkpoint right
    // after a wind-down logout does not immediately reprint it.
    i64 lastHistogramMs_ = 0;
    static constexpr i64 kHistogramIntervalMs = 10 * 60 * 1000;
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
    // Selecting an opponent starts Sphere's normal attack timer. Re-sending
    // the same target every life tick restarts that timer, producing a
    // perfectly acknowledged but completely harmless "fight".
    i64  lastAttackOrderMs_ = 0;
    u32  lastAttackOrderTarget_ = 0;
    // When the foe was last asked for its health (0x34).
    i64  foeHpAskedMs_ = 0;
    double foeHpAtStart_ = -1.0;
    i64  lastBandageMs_ = 0;
    i64  lastDangerNoteMs_ = 0;   // one danger note per fight, not per tick
    // --- combat (S2.6, AvoidCombat branch only) --------------------------
    // The last CombatMove logged, so LogPlan fires on transition only -- not
    // once per tick. Wait is the harmless default: DoSurvive never reaches
    // the AvoidCombat call with nothing decided yet.
    CombatMove lastCombatMove_ = CombatMove::Wait;
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
    // Every mage/scribe shop already attempted for the current spellbook
    // errand.  A scroll buyer is allowed to travel between cities; retrying
    // the same empty local shop three times is not exploration.
    std::vector<std::string> spellbookSkipPlaces_;
    // Shopkeepers already found to stock nothing this errand lacks. A mage
    // shop's scroll shelf is FOUR RANDOM SCROLLS, not a fixed list -- the
    // template sells random_first_circle .. random_fourth_circle, {4 24}
    // (templates/tm_vend.scp:721-724), which is why Aurelius's window showed
    // exactly Feeblemind/Strength/Telekinisis/Fire Field
    // (run_gates/g_Aurelius.console.txt:430-433). So "this mage has none the
    // book lacks" is a fact about ONE shopkeeper's current roll, not about
    // mages; Thalia failed FILL_SPELLBOOK outright on it with "4 already
    // known" (g_Thalia.console.txt:523). Remembering the exhausted one lets
    // travel pick a different shop instead of walking back to it.
    std::vector<u32> spellbookSkipSellers_;
    // Set when the scribe -- the only seller that lets a spell be CHOSEN --
    // turns out to be unreachable or to stock nothing this book lacks. After
    // that the mage shop's random scroll is better than no scroll.
    bool scribeExhausted_ = false;
    // HOW MANY TIMES THIS LIFE HAS GONE SHOPPING FOR A SCROLL AND COME BACK
    // WITH NOTHING. Consecutive: any scroll that actually enters the book
    // clears it. Drives life::ScrollShoppingRestMs, so the second empty errand
    // rests twice as long as the first -- a mage still WANTS scrolls, it just
    // stops asking a street that has already answered.
    i32  scrollStandDowns_ = 0;
    // When the current scroll-shopping stretch began, and when it last ran.
    // The errand is bounded in TIME, not only in trips, because the cost that
    // ate Aurelius's session was travel: three trips is under the trip budget
    // and still four minutes of walking. 0 means "not shopping".
    i64  scrollShopSinceMs_ = 0;
    i64  scrollShopTickMs_  = 0;
    // A stretch is only a stretch while the goal keeps getting the turn. The
    // planner may hand FILL_SPELLBOOK back twenty minutes later; comparing
    // against a mark that old would blow the budget on the first tick, the
    // same staleness trap clothMarkMs_ exists for.
    static constexpr i64 kScrollShopGapStaleMs = 60000;
    // Ninety seconds is more than one shop visit and less than a session. The
    // measured cost of one shop trip on this shard is ~60 s of travel.
    static constexpr i64 kScrollShopBudgetMs = 90000;
    i32  tradeTrips_ = 0;
    i32  vendorChases_ = 0;
    i32  bandageTrips_ = 0;

    // --- MAKE_CLOTH -------------------------------------------------------
    // Trips to a pasture that found no sheep, and steps that moved nothing.
    // Both are bounded by the escalate-after-three rule: a gesture that
    // yields nothing three times running is not a slow gesture, it is the
    // wrong one, and the goal stands down with a cooldown so the planner can
    // look at the rest of the life.
    i32  clothTrips_ = 0;
    i32  clothEmptySteps_ = 0;
    // The four counts the chain moves, as they were BEFORE the last gesture.
    // Progress is claimed on the next tick and only if one of them changed --
    // issuing a click is not the same as the server honouring it, and every
    // spinning goal this project has had claimed progress for the click.
    // -1 means "nothing pending".
    // When the mark below was taken. A mark from a previous visit to this goal
    // -- the planner may have taken the turn away and given it back minutes
    // later -- would compare the pack against ancient numbers and read an
    // unrelated purchase as this chain making progress. Marks go stale.
    i64  clothMarkMs_ = 0;
    static constexpr i64 kClothMarkStaleMs = 30000;
    i32  clothWoolBefore_  = -1;
    i32  clothYarnBefore_  = -1;
    i32  clothBoltBefore_  = -1;
    i32  clothClothBefore_ = -1;
    // Sheep that answered the shears with "wait for the wool to grow back".
    // Sphere flips a sheared sheep to CREID_SHEEP_SHORN (body 0x00DF) so the
    // body filter usually drops it on its own; this is the belt for that
    // brace, because a client whose 0x77 has not arrived yet would otherwise
    // walk back to the same animal.
    std::vector<u32> clothShornSheep_;
    // A SHORN SHEEP IS THREE MORE WOOL. Owner ruling 2026-09-02 (verified
    // live): kill the sheep after shearing and carve the corpse -- Sphere
    // carves it as the woolly body (CItemCorpse.cpp:191 `_iPrev_id`), and the
    // carve output lands IN the corpse (CCharUse.cpp:187), so it has to be
    // opened and emptied. clothKillSheep_ is the animal being put down (0 =
    // none), clothCarveCorpse_ its corpse once found, and the timestamps bound
    // each phase so a sheep that will not die or a corpse that will not open
    // costs one bounded try, not the session.
    u32  clothKillSheep_    = 0;
    i64  clothKillStartMs_  = 0;
    u32  clothCarveCorpse_  = 0;
    i64  clothCarveMs_      = 0;
    bool clothCarved_       = false;
    bool clothCorpseOpened_ = false;  // the carve output only shows once the corpse is opened
    i32  clothKillsThisTrip_ = 0;
    // When the flock this character is standing in first read as shorn out, so
    // the wait for a fresh animal to wander over has a bound. 0 = not waiting.
    // The regrow itself is 30 minutes (runtime/sphere.ini WoolGrowthTime) and
    // is never waited on; see DoMakeCloth step 4b.
    i64  clothFlockBareMs_ = 0;
    // Which pasture the last trip aimed at, so the next one tries another.
    // Indexes the DISTANCE-ordered view of the table, not the file order.
    i32  clothPastureIdx_ = 0;
    // The station (wheel or loom) currently being walked up to, and how many
    // times this character has set off for THAT one. A station it cannot get
    // beside is struck off after the second try and the next-nearest is used
    // instead -- Britain's tailor holds two of each.
    u32  clothStationSerial_ = 0;
    i32  clothStationApproaches_ = 0;
    std::vector<u32> clothDeadStations_;
    // Set once this trip's shearing is over -- the pack is as full as the
    // gatherers carry, or the flock is bare. Without it, a character that
    // left a bare flock with half a load and arrived at a tailor whose wheel
    // is not yet in item range would read "not full, keep shearing" and walk
    // straight back to the pasture. Cleared when the wool is all spun.
    bool clothHeadingToWheel_ = false;
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
    // --- acquiring gear (S2.7) -------------------------------------------
    // The last AcquireStep logged for each of the three DoReplaceEquipment
    // items, so LogPlan fires on transition only. Garment also remembers
    // WHICH piece it was logged for -- the missing item can change between
    // ticks (shirt bought, trousers next) without the step itself changing.
    AcquireStep lastBandageAcquirePlan_ = AcquireStep::Done;
    AcquireStep lastPotionAcquirePlan_  = AcquireStep::Done;
    AcquireStep lastGarmentAcquirePlan_ = AcquireStep::Done;
    std::string lastGarmentAcquireItem_;
    // Same, for DoGetTool's one-request-per-tool loop -- but PER TOOL NAME,
    // not one shared sentinel. A profession with two or more tools
    // (miner_smith: pickaxe AND smith hammer) checks every one of them each
    // tick up to the first still-missing entry, and a single shared sentinel
    // cannot remember two tools' states at once: pickaxe (Done) looks like a
    // transition away from whatever hammer last set the sentinel to, and
    // hammer (Buy) looks like a transition away from whatever pickaxe just
    // set it back to -- so BOTH logged, every tick, forever, with neither
    // tool's own status actually moving. 341 lines in 10 seconds, alternating
    // plan=done/plan=buy. A map keyed by tool name gives each tool its own
    // memory instead of trampling its neighbour's.
    std::map<std::string, AcquireStep> lastToolAcquirePlanByItem_;
    // HOW MANY TIMES WE HAVE ASKED THE SERVER TO PUT THIS TOOL IN HAND, and
    // it has not appeared on the paperdoll. Cleared the tick the equip is seen
    // to have landed -- which is the ONLY place DoGetTool calls NoteProgress
    // for a wear. Without the count an equip the shard silently refuses is an
    // infinite 1.2-second loop that keeps resetting the planner's own
    // failure ladder. (audit 2026-08-30, finding 4.)
    std::map<std::string, int> toolWearAttemptsByItem_;

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
    // A WantsToHunt fighter's own weapon-school basic (katana/kryss/club/
    // bow), bought when arming from the pack finds nothing. See
    // SchoolWeaponFor in Runner.cpp.
    life::BuyActivity weaponBuy_;
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
    // WHICH ingot smeltIngotsBefore_ is a count of (S1). Ore is one graphic
    // for every metal, so the picker falls back to a coloured vein when the
    // iron runs out -- and a baseline taken against i_ingot_iron then never
    // moves again no matter how much valorite is melted. The count and the
    // name have to travel together, and the baseline is retaken whenever the
    // metal changes.
    std::string smeltIngotName_;
    i32  smeltTrips_ = 0;
    i64  toolTitlesAskedMs_ = 0;
    i64  mountTitlesAskedMs_ = 0;
    i32  mountTrips_ = 0;
    i32  mountClicks_ = 0;        // double-clicks sent to the horse this goal
    i64  mountBoughtMs_ = 0;      // when the purchase packet went out
    // Has this session already written the durable "the horse errand stood
    // itself down" record? One per session; see NeedConfig::sessionIndex.
    bool mountStandDownNoted_ = false;
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
    // FIRST VISIT, NO MEMORY: consecutive server refusals (kBadTile) at the
    // current spot since the last genuine hit (ore or a failed-roll swing).
    // Three in a row with no nearby remembered vein means the entrance is
    // picked clean -- see the deeper-advance branch in DoMine. mineAdvances_
    // bounds how many times a single goal attempt will walk deeper before
    // falling back to the ordinary give-up path, so an actually rock-less
    // cave still fails honestly instead of pacing forever.
    i32  mineConsecRefusals_ = 0;
    i32  mineAdvances_ = 0;
    i32  tameTrips_ = 0;
    // Taming: the name scan is a STEP, not a free read. tameScanMs_ is when
    // ActionScanMobiles was issued for the spot the character is standing on
    // (0 = not asked yet, so no emptiness verdict is allowed); tameAskedMs_ is
    // the journal mark for the last taming attempt, read back for the shard's
    // own answer. See include/uo/activities/tame.h.
    i64  tameScanMs_ = 0;
    i64  tameAskedMs_ = 0;
    i32  tameAttempts_ = 0;
    u32  tameTarget_ = 0;
    std::string tameTargetName_;
    // The animal's own TAMING requirement, kept so the success line can say
    // what was actually beaten, and the herds already walked to this session
    // so the three-trip budget is spent on three DIFFERENT places.
    double tameTargetReq_ = -1.0;
    std::vector<std::pair<i32, i32>> tameVisited_;
    // Animals this goal has been refused by -- somebody's pet, an already
    // tame sheep, a creature the shard says cannot be tamed at all. Skipped
    // when choosing the next target, so one dead end does not eat the goal.
    std::vector<u32> tameRefused_;
    i32  mineTrips_ = 0;
    std::string exploreTarget_;
    // --- rest and roam (S2.2) -----------------------------------------------
    // TravelToUnexploredPlace both CHOOSES and STARTS a journey, so it cannot
    // be used as a query for DecideRest's `worthExploring`. Latched instead:
    // set true at DoExplore's "nowhere new to go" branch, cleared at the
    // arrival NotePlace("explored", ...) -- see RestTick.
    bool exploredEverything_ = false;
    // When a goal outside the Wander family (Explore/IdleBriefly/
    // TravelToRequiredPlace, per FamilyOf) was last picked -- written in the
    // Select success block. DecideRest's `blockedForMs` is how long every
    // REAL errand has been unavailable.
    i64  lastRealErrandMs_ = 0;
    // Last RestStep passed to LogPlan, so RestTick logs only on a plan
    // change, not once per tick. Out-of-range sentinel guarantees the first
    // real step always logs, matching lastRecoveryPlan_'s pattern.
    RestStep lastRestPlan_ = static_cast<RestStep>(0xFF);
    // Past this point in a session, standing still stops being idle and
    // becomes settling down somewhere safe -- read against sessionLimitMs so
    // WindDown has time to walk to a bank before the hard deadline, not the
    // instant it is reached. A threshold, not a mechanic.
    static constexpr i64 kRestSettleLeadMs = 3 * 60 * 1000;
    // Per-tag sentinels for LogErrandReason: the last reason printed and when.
    // Mutable because the logging path is const, like LogLine and LogPlan.
    struct ErrandLogSentinel { std::string reason; i64 atMs = 0; };
    mutable std::map<std::string, ErrandLogSentinel> errandLogSeen_;
    static constexpr i64 kErrandReasonRepeatMs = 60 * 1000;

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
    // --- practice casting (reagent defect, wave 2026-09-02) ----------------
    // Journal mark taken the instant a practice cast is sent, so the reply --
    // "You lack Sulfurous Ash for this spell" -- can be read back and believed.
    // Deliberately NOT createFoodMark_: the food goal casts too, and one mark
    // shared between two goals attributes one goal's refusal to the other.
    i64  practiceCastMark_ = 0;
    int  practiceCastSpell_ = -1;
    // Spells the SERVER refused this session, and how many casts were actually
    // sent. The refusal list is session-scoped on purpose: reagents bought
    // later make the same spell castable again, and a new session re-learns.
    std::vector<int> practiceRefusedSpells_;
    i32  practiceCasts_ = 0;
    // How often each spell has been practised this session, so the chooser can
    // ROTATE within a circle instead of spamming one word (owner ruling
    // 2026-09-02: practice reads the whole Magery table, not a fixed list).
    std::vector<std::pair<int, i32>> practiceCastCounts_;
    // The reagent shopping list PRACTICE_SKILL handed to BUY_SUPPLIES, front
    // first, with the per-reagent quantity derived in DoPracticeSkill from the
    // observed cast rate (uo::spell::PlanReagentBuy).
    std::vector<std::string> reagentWants_;
    i32  reagentWantQty_ = 0;
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
    // --- healing (S2.1) -----------------------------------------------------
    // The last HealStep logged, so LogPlan fires on transition only -- not
    // once per tick, which is what produced the 311-line forge spam this
    // slice exists to end. HealStep::None at construction matches DoHeal's
    // "healthy enough" starting assumption.
    HealStep lastHealPlan_ = HealStep::None;
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
    // The last TrainStep logged (S2.4), so LogPlan fires on transition only.
    // Shared by both DecideTrain call sites in DoTrainAtNpc -- they decide
    // the same question at two different moments, not two questions.
    TrainStep lastTrainPlan_ = TrainStep::Done;
    // Deposits of one item that keep failing. See DoBank.
    std::string bankDepositItem_;
    int         bankDepositTries_ = 0;
    static constexpr int kMaxBankDepositTries = 5;
    // A gold deposit ASKED FOR but not yet settled -- confirmed from the
    // pack's own gold count on the next tick, never from having merely
    // issued the drag (the same "ledger records what happened" rule
    // pendingBuyItem_/pendingBuyGoldBefore_ already keep for BUY_SUPPLIES).
    // Bounded by kMaxBankDepositTries exactly like bankDepositItem_ above:
    // a box that keeps answering "landed elsewhere" is not really open.
    bool bankGoldDepositPending_ = false;
    i32  pendingGoldDepositBefore_ = 0;
    int  bankGoldDepositTries_ = 0;
    // An ITEM deposit that has been ISSUED but whose outcome has not been
    // read yet. Every deposit branch in DoBank used to credit NoteProgress()
    // the instant it sent the drag, so a move that bounced straight back
    // still counted -- 1083 of them for one character in one thirty-minute
    // session, each one re-picking BANK and starving every other goal
    // (run_gates/wave15/wave15_RevGen3_02_Kharain.console.txt 18:09:03
    // onwards; Titus, 1626). Progress is now credited from
    // Client::ActionResult(), and three failures in a row let the box go and
    // stand the goal down instead of trying a fourth time.
    bool bankItemMovePending_ = false;
    int  bankItemMoveFails_ = 0;
    static constexpr int kMaxBankItemMoveFails = 3;
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
    // Places TravelToServiceSkipping has ever been SENT to for this input,
    // success or not -- cleared alongside supplyTrips_ whenever supplyItem_
    // changes. Without this, a failed trip re-ran PickServicePlace with an
    // empty skip list and picked the SAME shop again: a Skara Brae fisher
    // asked for kindling was sent through a moongate to "Ocllo provisioner"
    // on trip 1, the transit stalled, and trip 2 sent him right back to the
    // same island rather than falling through to the next-best candidate
    // (docs/LIFE_GATE_WAVE1.md theme 2, run_gates/g_Dorvar.console.txt
    // 00:40-00:50, "supplies: looking for a 'provisioner' ... (trip 1)" then
    // "(trip 2)" both landing on 'Ocllo provisioner').
    std::vector<std::string> supplySkipPlaces_;
    // A buy that has been ASKED FOR but not yet settled. The ledger entry is
    // written from the gold the server actually took, on the tick after the
    // action resolves -- never at request time. See DoBuySupplies.
    std::string pendingBuyItem_;
    i32         pendingBuyGoldBefore_ = 0;
    std::string craftItem_;       // what is being made
    i32         craftHadBefore_ = 0;
    // THE JOURNAL MARK FOR THIS SWING. Section 18's craft rule has two
    // halves -- "the crafted item count increased, OR a definitive craft
    // failure received" -- and the second half needs a point to read from.
    // The tick clock will not do: JournalSaidSince measures against the
    // journal's own clock, and mixing the two is what made a 12-second
    // window expire in 2.5 seconds in the trainer path.
    i64         craftJournalMs_ = 0;
    int         craftMade_ = 0;
    // --- crafting (S2.5) ------------------------------------------------
    // The last CraftStep logged, so LogPlan fires on transition only -- not
    // once per tick. Sentinel rather than CraftStep::Done: Done is a real,
    // reachable verdict and must still log the first time it is seen.
    CraftStep   lastCraftPlan_ = static_cast<CraftStep>(0xFF);
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
    // How long past the session deadline a corpse run (or a ghost) may hold
    // the session open before the clock wins outright. A deferral with no
    // bound is not a deadline -- that is how a stuck RECOVER_CORPSE kept
    // Hector connected 5 minutes past his 30-minute window.
    static constexpr i64 kSessionOverrunGraceMs = 60 * 1000;
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
    // HAS THE PLAYER-FIRST WINDOW CLOSED FOR THIS ITEM?
    //
    // The gate on the NPC price floor (owner ruling, 2026-09-02): a material
    // may take the counter only after a complete WTS announce cycle nobody
    // answered. DoTradeWithPlayer already writes exactly that fact -- a
    // `no_player_buyer` memory event keyed on the item -- and until now
    // nothing read it back. This is that reader.
    //
    // Bounded by kPlayerWindowMemoryMs so the answer is "nobody wanted it
    // RECENTLY", not "nobody wanted it once, weeks ago": a permanent verdict
    // would quietly convert the floor into the market.
    static constexpr i64 kPlayerWindowMemoryMs = 60 * 60 * 1000;   // one hour
    bool PlayersDeclined(const std::string& item, i64 nowMs) const;
    // THE BUY SIDE OF THE SAME READER. `no_player_seller` is written by
    // DoTradeWithPlayer when a WTB window expires with nobody answering, and
    // it is what licenses a life to go and make the thing itself rather than
    // wait (owner ruling, tailor cloth, 2026-09-02). Same one-hour bound and
    // for the same reason.
    bool SellersDeclined(const std::string& item, i64 nowMs) const;
    // THE OTHER HALF OF THE SAME RULING (2026-09-02): a material reaches an NPC
    // counter only when the player-first window has closed AND what this
    // character holds -- pack plus bank -- is genuinely above its own
    // plan-derived cap. Below the cap it banks and waits for a crafter.
    //
    // The cap formula and its evidence live in market::MaterialSurplusCap. All
    // this does is supply the two things that function cannot see for itself:
    // what the build plan still has to climb, and what the boxes hold.
    market::MaterialSaleGate MaterialSaleGateFor(const std::string& item,
                                                 const Observation& obs) const;
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
    // How long a life rests after a handshake that failed rather than one that
    // found no audience at all. DERIVED FROM THE HANDSHAKE'S OWN TURN TIMES:
    // one full give-up window plus the two announce turns it takes the room to
    // say anything new. A flat kMarketQuietMs here would punish the seller that
    // did everything right and merely lost the race to another seller.
    static constexpr i64 kTradeRetryRestMs =
        kTradeGiveUpMs + 2 * kAnnounceIntervalMs;   // 41s
    market::TradePolicy tradePolicy_;
    market::TradeIntent tradeOffer_;      // what we are announcing
    u32         tradePartner_ = 0;
    std::string tradePartnerName_;
    std::string tradeItem_;
    i32  tradeSellingQty_ = 0;   // >0 = we are the SELLER
    i32  tradeWantQty_ = 0;      // buyer: how many we want
    i32  tradeOfferPrice_ = 0;   // buyer: the price the seller named
    // WHAT THIS BUYER SHOUTED IT WANTED, and when. A seller answers a WTB by
    // opening a trade window; from that moment DoTradeWithPlayer short-circuits
    // into DriveOpenTrade and the listen loop -- the only place that ever set
    // the two fields above -- never runs again. So the announcement has to be
    // written down when it is made, or the buyer meets the window with nothing
    // to fund it from. See market::FundOpenWindow.
    market::TradeIntent tradeWant_;
    i64  tradeWantAskedMs_ = 0;
    bool tradeOffered_ = false;
    i32  tradePackBefore_ = 0;   // the PACK is the proof, not the packet
    i32  tradeGoldBefore_ = 0;
    i64  tradeHeardMs_ = 0;
    i64  tradeAnnouncedMs_ = 0;
    i64  tradeOpenedMs_ = 0;
    i32  tradeAnnounceCount_ = 0;
    // Sellers we have already told "sorted", so the decline is said once each
    // rather than every tick they keep offering.
    std::vector<u32> tradeDeclined_;
    // Until when the player market counts as tried-and-empty.
    i64  marketQuietUntilMs_ = 0;
    static constexpr i64 kMarketQuietMs = 10 * 60 * 1000;   // ten minutes
    // AN EMPTY ROOM IS NOT A DECLINED OFFER. "no audience" fires before a
    // word is ever said -- nobody was there to answer -- which is a much
    // weaker signal than "audience already declined" (an offer WAS made and
    // ignored). Cooling both for the full ten minutes meant a seller and a
    // buyer arriving out of step (one leg is 250s one-way,
    // docs/S5_MARKET_TRIP_PLAN.md section 3) rested past the point the other
    // side could plausibly still be there. Two minutes is long enough to not
    // spam an empty room every tick, short enough that the pair still has a
    // chance to overlap within the same session.
    static constexpr i64 kNoAudienceMs = 2 * 60 * 1000;     // two minutes
    // Is market::kMarketBankPlaceId usable? -1 not resolved yet, 0 no, 1 yes.
    int  marketPlaceOk_ = -1;
    // A BUYER HAS NOTHING TO SAY. It answers what it hears, so its whole
    // errand at the market is to be present while somebody else announces.
    // Bounded: one full announce cycle is kMaxAnnounces x kAnnounceIntervalMs
    // = 48s nominal, measured at 42.4s live (run_m7/fleet7.console.txt, first
    // announce 16:23:27.633 -> stand-down 16:24:10.031).
    //
    // 60s was the original bound -- guarantees a listener present at the
    // market hears a complete announce cycle, but nothing more. A seller and
    // a buyer are two lives each walking a 250s one-way leg to the same
    // rendezvous (docs/S5_MARKET_TRIP_PLAN.md section 3); at 60s the pair
    // almost never actually overlaps, and kNoAudienceMs above only cools two
    // minutes before the same buyer is willing to come back and listen again.
    // Three minutes gives real slack for the two arrivals to land inside the
    // same window without either side listening indefinitely.
    static constexpr i64 kListenMs = 3 * 60 * 1000;
    // When the wait at the market began; 0 = not waiting. SHARED by the buyer
    // (nothing to say, listening for a seller) and the seller (goods in hand,
    // nobody yet in earshot). Both are the same fact -- a character that has
    // paid for the journey standing at the rendezvous -- and both end the same
    // way, so they end up on one clock.
    i64  marketListenFromMs_ = 0;
    // --- the withdrawal, and why it needs three counters -------------------
    //
    // run_r4/pair_Durnholde.console.txt:4382-4672: seventy-six identical
    // "market: withdrawing 20 i_ingot_iron from the bank to sell" lines in two
    // minutes forty-four seconds, each answered by `drag_cancel: reason=0
    // cannot lift that`, because the box serial and its cached contents both
    // survived a walk to the blacksmith guild and back while the server's own
    // box did not.
    //
    // Has a banker opened the box during THIS visit to the market? BankErrand
    // reports Success the instant Client::BankContainer() is set, so an
    // inherited box would be rubber-stamped; this is the flag that makes the
    // handler drop it and ask again.
    bool marketBoxOpened_ = false;
    // Refused lifts of the same stack with the pack unchanged, and how many
    // times the box has been re-asked for over one errand. Both bounded so a
    // box that genuinely does not hold the goods ends the errand instead of
    // cycling.
    i32  marketLiftFails_ = 0;
    i32  marketLiftPack_ = -1;      // pack count at the last attempt
    std::string marketLiftItem_;
    i32  marketBoxReopens_ = 0;
    static constexpr i32 kMaxMarketLiftFails = 2;   // as coinLiftFails_ in DoBank
    static constexpr i32 kMaxMarketBoxReopens = 2;
    // RETRY LONGER THAN THE DEADLINE. Client.cpp's kMoveTimeoutMs is 4000 ms;
    // the old 2000 ms gap meant every retry superseded its own predecessor
    // before the server's answer could land ("Retry shorter than timeout").
    static constexpr i64 kMarketWithdrawRetryMs = 6000;
    // WHAT A MARKET TRIP COSTS, end to end: 250s out + 3-min listen + 250s back
    // (docs/S5_MARKET_TRIP_PLAN.md section 3, all three legs measured), plus
    // kWindDownBudgetMs so the life is not still walking when the session
    // clock runs out and wind-down finds it in open country. Matches
    // Planner::TimeLimitFor(TradeWithPlayer).
    static constexpr i64 kMarketTripMs = 250000 + kListenMs + 250000;  // 680 s: two 250 s legs + the 3-min listen (2026-08-30)
    static constexpr i64 kMarketTripBudgetMs = kMarketTripMs + kWindDownBudgetMs;

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
    // Consecutive attempts to get the pole into a hand that did not stick. A
    // refused equip puts the item back in the pack, which is indistinguishable
    // from never having tried, so the retry needs its own counter and backoff.
    i32 fishArmTries_ = 0;
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
    // Last RecoveryStep passed to LogPlan, so DoRecoverCorpse logs only on a
    // plan change (S2_WIRING_PLAN.md S2.3) instead of once per tick. The
    // out-of-range sentinel guarantees the first real step always logs.
    RecoveryStep lastRecoveryPlan_ = static_cast<RecoveryStep>(0xFF);
    // Decisions taken while standing on the death tile with no corpse serial
    // bound. A corpse decays in 7 minutes; the death record does not.
    i32 corpseProbesAtSite_ = 0;
    // Foes we proved we could not reach, so a mob behind a wall does not
    // restart the approach every tick (audit section 3.7).
    std::vector<std::pair<u32, i64>> unreachable_;

    bool IsUnreachable(u32 serial, i64 nowMs) const;
    void MarkUnreachable(u32 serial, i64 nowMs);
};

}  // namespace life
}  // namespace uo
