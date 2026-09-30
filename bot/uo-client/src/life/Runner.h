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
#include "uo/needgate.h"
#include "uo/chatter.h"
#include "uo/pvp.h"
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
#include <set>
#include <string>
#include <unordered_map>
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
    i32 eraDate = 0;                // uo/era.h yyyymmdd; 0 = era::kDefaultDate
    bool noPvp = false;             // --no-pvp: no PK ambushes, no anti-PK hunting

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

    // --- offline harness seams (tests/life_harness.cpp) -------------------
    // The life layer has no clock and no randomness of its own, so the only
    // thing between a deterministic test and the REAL goal loop is where the
    // Observation comes from. While an override is set, Observe() returns it
    // (with this tick's nowMs patched in) instead of reading the Client.
    // Nothing else changes: the same Planner, the same Do* handlers, the same
    // Client. Live sessions never set it.
    void SetObservationOverrideForTest(const Observation* obs) {
        obsOverride_ = obs;
    }
    // Is a Runner-owned errand still mid-purchase? Nothing else exposes it,
    // and "the errand did not survive the goal change" is exactly the fact
    // LeaveGoal exists to make true. Names: "bandage", "bandageCloth",
    // "potion", "clothing", "weapon", "food", "bank".
    bool ErrandRunningForTest(const char* which) const;

    // Test seam: pretend this character has just bought a bandage counter
    // out. Live sessions reach the same state through a partial purchase in
    // DoReplaceEquipment; there is no vendor in the harness to buy from.
    void NoteBandageCounterDrainedForTest(u32 serial, i64 nowMs) {
        NoteDrainedShelf(serial, nowMs);
        ++bandageCountersDrained_;
    }
    // The same seam for the heal-potion shelf.
    void NotePotionCounterDrainedForTest(u32 serial, i64 nowMs) {
        NoteDrainedShelf(serial, nowMs);
        ++potionCountersDrained_;
    }

    // Persist immediately (clean logout, host shutdown, a meaningful change).
    bool Checkpoint(Client& client, i64 nowMs, const char* why);
    // The observer's window (status.json, tools/observer.py): every
    // kStatusIntervalMs while live, and once more at logout marked offline.
    void PublishStatus(Client& client, const Observation& obs, const char* phase);
    void PublishOffline();
    void TickRunebook(Client& client, const Observation& obs);
    // PvP (runner/Pvp.cpp, uo/pvp.h).
    pvp::Role PvpRole() const;
    pvp::Self PvpSelf(Client& client, const Observation& obs) const;
    std::vector<pvp::Target> PvpTargets(Client& client, const Observation& obs) const;
    void ObservePvp(Client& client, const Observation& obs);
    void AddPvpNeeds(Client& client, const Observation& obs, std::vector<Need>& needs);
    bool DoHuntPlayers(Client& client, const Observation& obs);
    // Housing (runner/Housing.cpp, uo/housing.h).
    bool OwnsHouse() const;
    void AddHousingNeeds(Client& client, const Observation& obs, std::vector<Need>& needs);
    bool DoBuyHouse(Client& client, const Observation& obs);
    life::VendorErrand houseErrand_;
    std::vector<std::pair<i32, i32>> houseSites_;
    int  houseSite_ = 0, houseTriesHere_ = 0;
    i64  houseTriedMs_ = 0;
    bool houseDeedFailed_ = false;
    i64  pvpAlarmSaidMs_ = 0, pvpHeardMs_ = 0, pvpAlarmUntilMs_ = 0, pvpRoamMs_ = 0;
    i32  pvpAlarmX_ = 0, pvpAlarmY_ = 0;
    bool pvpRoamFlip_ = false;
    // Treasure hunting (runner/Treasure.cpp, uo/treasure.h).
    bool WantsTreasure() const;
    void AddTreasureNeeds(Client& client, const Observation& obs, std::vector<Need>& needs);
    bool DoHuntTreasure(Client& client, const Observation& obs);
    void EndTreasure(const char* why);
    u32  treasureMap_ = 0, treasureChest_ = 0;
    bool treasurePointKnown_ = false;
    i32  treasureX_ = 0, treasureY_ = 0;
    int  treasureDecodeTries_ = 0, treasureDigTries_ = 0, treasureOpenTries_ = 0, treasurePickTries_ = 0;
    int  treasureCursorPending_ = 0;     // 1 = dig cursor, 2 = lockpick cursor
    i64  treasureLastMs_ = 0;
    i64  runebookTryMs_ = 0;
    bool runebookLogged_ = false;

    // Ends the session deliberately: finish the current safe action, head
    // somewhere safe, persist, and log out.
    void EndSession(const char* why);

private:
    friend struct RunnerHarnessAccess;
    enum class Phase : u8 {
        AwaitWorld = 0,
        Reconcile,
        Live,
        WindDown,      // heading somewhere safe
        LoggingOut,
        Done,
    };

    Observation Observe(Client& client, i64 nowMs) const;
    // The alive<->dead edge, tracked once per tick regardless of phase_. Not
    // folded into Observe() because Observe() is phase-agnostic and pure
    // (obsOverride_ replays it verbatim for the offline harness), while this
    // writes session/persistent counters -- see Tick()'s call site.
    void        TrackDeathEdge(Client& client, i64 nowMs);
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
    // THE ONE PLACE A GOAL ENDS. Called from every exit -- the planner picking
    // a different goal, RunGoal's Exhausted branch, HandOff, and the session
    // limit -- so the transient slate a goal leaves behind is cleared once,
    // by name, instead of by whichever handler happens to run next.
    //
    // `sameKind` (a re-pick of the identical GoalKind) keeps the journey and
    // the errand exactly as they were: that is the Corran rule the reset block
    // in Tick() documents, and widening it is what sent him to Britain 61ms
    // after arriving in Minoc.
    void        LeaveGoal(Client& client, GoalKind from, GoalKind to,
                          bool sameKind, const char* why);
    bool        HandOff(GoalKind from, GoalKind to, i64 restMs, const char* why,
                        i64 nowMs);

    // ARM B OF THE NEED/HANDLER CONTRACT (docs/NEED_HANDLER_CONTRACT.md).
    //
    // The one way a handler tells the NEED MODEL why it refused, for a reason
    // the need model cannot compute for itself -- a runner-private table, a
    // failed walk, an empty shelf. Ends the goal exactly as the sites it
    // replaces did (log, cooldown, Finish(false)) AND records the refusal in
    // Memory, where life::CanAct reads it back with the same words. Without
    // the second half the need re-scores the same impossible thing every
    // cooldown until the anti-spin backstop fires; that is the whole defect
    // this contract exists to end.
    //
    // `scope` is Session for a fact that will not change today (no pasture
    // near home) and Window for one the world undoes on its own (a drained
    // shelf). Always returns false, so a handler can `return BlockNeed(...)`.
    bool        BlockNeed(GoalKind goal, life::NeedKind need,
                          life::BlockScope scope, const char* why,
                          i64 cooldownMs, i64 nowMs);

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
    bool DoReplaceEquipment(Client& client, const Observation& obs, bool medicineOnly = false);
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
    i32 HuntSupport(Client& client, const Observation& obs, u32 target, bool announce = false);
    // STAT_FARM: the Wrestling detour that is the only way a caster's STR
    // ever moves. Train.cpp, beside the other training errands.
    bool DoStatFarm(Client& client, const Observation& obs);
    // Put the locks in the farming configuration and empty both hands.
    // Returns true while it is still arranging (the caller waits).
    bool BeginStatFarm(Client& client, const Observation& obs);
    // Undo it: DEX lock back to what the build wants, weapon back in hand.
    // Safe to call when no farm is running.
    void EndStatFarm(Client& client, const Observation& obs);
    bool DoEarnGold(Client& client, const Observation& obs);
    bool DoTravel(Client& client, const Observation& obs);
    // Walk back to the city this character lives in, by ordinary travel.
    // See GoalKind::ReturnHome and Observation::homeKnown.
    bool DoReturnHome(Client& client, const Observation& obs);
    bool DoTrainAtNpc(Client& client, const Observation& obs);
    bool DoTradeWithPlayer(Client& client, const Observation& obs);
    void ObserveSocial(Client& client, const Observation& obs);
    void AddSocialNeeds(Client& client, const Observation& obs, std::vector<Need>& needs);
    bool DoSocialize(Client& client, const Observation& obs);
    bool TickSparring(Client& client, const Observation& obs);
    bool TickPoisonPractice(Client& client, const Observation& obs);
    bool WantsPoisonPractice(const Observation& obs) const;
    bool poisonStudent_ = false, poisonSeen_ = false;
    i32 poisonRound_ = 1;
    i64 poisonReadyMs_ = 0;
    // Sparring v2 (uo/sparring.h DecideRound): one "Ready to spar." per
    // meeting, rounds leased automatically, the meeting clock starting when
    // the PARTY forms.
    bool sparActive_ = false, sparRoundStarted_ = false, sparPeerReady_ = false;
    i64 sparReadyMs_ = 0, sparPollMs_ = 0, sparRoundEndMs_ = 0, sparMeetingStartMs_ = 0;
    i32 sparRounds_ = 0;
    int sparKit_ = 0;   // 1 fists, 2 iron + training weapon (the invitation's kind)
    // Bystander healer: heard sparring consent nearby and walks over to
    // bandage the sparrers while it still wants Healing.
    i64 sparWatchUntilMs_ = 0, sparHealMs_ = 0;
    u32 sparWatchA_ = 0, sparWatchB_ = 0;
    bool TickSparHealer(Client& client, const Observation& obs);
    bool FollowHuntingParty(Client& client, const Observation& obs);
    void EndSocialGroup(Client& client, const char* reason);
    bool SocialFoe(const std::string& name) const;
    bool SocialSay(Client& client, i64 nowMs, const std::string& text);
    social::Activity socialActivity_ = social::Activity::None;
    u32 socialPeer_ = 0;
    u32 socialPreferred_ = 0;
    std::string socialPeerName_;
    i64 socialHeardMs_ = 0, socialChatMs_ = 0, socialRestUntilMs_ = 0;
    // Small talk (uo/chatter.h). chatWith_ remembers when we last exchanged
    // words with each name this session, so two bots answering each other's
    // "selam" cannot ping-pong forever.
    std::unordered_map<std::string, i64> chatWith_;
    i64  chatIdleMs_ = 0, chatKillMs_ = 0;
    bool chatAfterDeath_ = false;
    bool Chat(Client& client, i64 nowMs, chatter::Topic topic, const std::string& to = "");
    void TickSmallTalk(Client& client, const Observation& obs, bool safe);
    i64 socialStartedMs_ = 0, socialAgreedMs_ = 0, socialLastSeenMs_ = 0;
    i64 socialInviteMs_ = 0, socialScanMs_ = 0, socialGroupUntilMs_ = 0;
    i64 socialMarketUntilMs_ = 0, socialPracticeMs_ = 0;
    u32 socialTargetGeneration_ = 0;
    bool socialConsented_ = false, socialOwnParty_ = false;
    bool socialLeavePending_ = false;
    bool socialMeetingPicked_ = false;
    i32 socialMeetingX_ = 0, socialMeetingY_ = 0;
    i32 socialTrainingStart_ = 0;
    i32 socialTrainingGains_ = 0;
    std::vector<SkillTarget> socialTrainingSkills_;
    std::string socialDemandItem_;
    i64 socialDemandUntilMs_ = 0;
    usize socialMeetingRotation_ = 0;
    bool DoFish(Client& client, const Observation& obs);
    bool DoBuySupplies(Client& client, const Observation& obs);
    bool DoCraft(Client& client, const Observation& obs);
    bool DriveOpenTrade(Client& client, const Observation& obs);
    void ResetTradeState();
    // WHICH BANK IS THIS CHARACTER'S MARKET. TWO HUBS ONLY (owner ruling
    // 2026-09-07, superseding the interim home-town-bank rule): Britain bank
    // and Minoc bank, and every character -- however it lives -- trades at
    // whichever is cheaper to reach, by the same moongates-allowed travel
    // cost RETURN_HOME already uses (runner_detail::ResolveMarketHub,
    // TravelTilesWithGates). Measured from wherever the character is
    // actually standing when it is in world; from its own home bank when it
    // is not (the atlas loaded before the login handshake finished, and
    // there is no live position yet to measure from). Falls back to
    // market::kMarketBankPlaceId only when the atlas carries neither bank at
    // all. Resolved once per life and cached in marketPlaceId_; the
    // comparison that decided it is cached alongside in marketPlaceWhy_ for
    // the one-time log line.
    std::string ResolveHomeMarketPlaceId(Client& client) const;
    // Is this character's market place (ResolveHomeMarketPlaceId) usable at
    // all: present in the atlas, offering Service::Banker, and guarded?
    // Resolved once per life and cached; logs the answer the first time.
    // When it is not, the trade errand keeps today's nearest-bank behaviour
    // rather than inventing coordinates for a place the atlas does not have.
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
    // Pet hunger is a server-emitted, owner-specific emergency.  This is not
    // a planner goal: letting normal scoring delay a ravenous mount behind a
    // bank or a training trip is how animals were neglected in fleet 122.
    // The tick is bounded and resumes the interrupted life after feeding.
    bool PetCareTick(Client& client, const Observation& obs);
    bool RefillManaWhenSafe(Client& client, const Observation& obs);
    // Mages and warlocks rotate long-lived self buffs while safe.  The server
    // does not expose these effect layers in an observation, so the cadence is
    // deliberately shorter than their documented minimum durations.
    bool MaintainCasterBuffs(Client& client, const Observation& obs);
    bool refillingMana_ = false;
    i64 manaRetryMs_ = 0;
    i32 manaLastSeen_ = 0;
    i64 combatMeditationRetryMs_ = 0;
    i64 casterBuffRetryMs_ = 0;
    usize casterBuffCursor_ = 0;
    bool DoPracticeSkill(Client& client, const Observation& obs);
    // What to cast for practice, or what the pack is short of. Reads the book
    // and the pack and defers the actual choice to uo::spell (unit-tested in
    // tests/m4_life.cpp); this wrapper only gathers the observation.
    spell::PracticeChoice PickPracticeSpell(Client& client,
                                            const Observation& obs) const;
    bool DoFillSpellbook(Client& client, const Observation& obs);
    bool DoMakeBandages(Client& client, const Observation& obs);
    // ONE TICK OF A BANDAGE-INPUT PURCHASE (loose cloth, or a bolt of it).
    // The two rows sit on the same weaver's shelf and are bought by the same
    // handshake, so the errand plumbing -- reason logging, the landed/attempt
    // classification, the drained-shelf note -- is stated once here. Returns
    // true while the errand is still live (the caller's tick is over);
    // false when it has reached a terminal status, with `bought` set if the
    // goods actually arrived.
    bool TickBandageInputBuy(Client& client, const Observation& obs,
                             life::BuyActivity& buy, const char* tag,
                             bool& bought);
    // Sheep -> wool -> yarn -> bolt -> cloth, for a life whose CRAFT is
    // blocked on cloth and whose WTB window found no seller. Walks the same
    // five gestures DoMakeBandages does and stops at cloth instead of going
    // on to bandages. See the definition for why every step is measured by an
    // inventory delta rather than by having issued the click.
    bool DoMakeCloth(Client& client, const Observation& obs);
    // FETCH THE CLOTH SURPLUS OUT OF THE BANK BOX AND CUT IT FOR SALE.
    //
    // The other end of DoMakeCloth: that one is short of cloth, this one is
    // long of it. CutClothForSale could only ever see the PACK and only ever
    // ran from MAKE_CLOTH's "batch covered" exit, so a tailor whose finished
    // cloth sits in the box -- which is where it goes -- never reached it.
    // One withdraw-and-cut batch per run, a cooldown on every exit.
    bool DoMakeBandagesForSale(Client& client, const Observation& obs);
    // Put the bandages above the sale shelf back in the box, but only while
    // the box is already open. True when it issued the move.
    bool BankSaleBandages(Client& client, const Observation& obs, i32 held,
                          i32 target);
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
    // GET OFF THE HORSE TO WORK, GET BACK ON TO LEAVE (project owner,
    // 2026-09-04). Mining and lumberjacking only: fishing is untouched.
    // Mining is not merely unseemly from the saddle, it is refused --
    // skill45_mining.scp @PreStart returns 1 on FINDLAYER.layer_horse.
    //
    // Both return TRUE when they have taken over the tick (a click is in
    // flight and the caller must return without acting), FALSE when there is
    // nothing left to do -- already on foot, already back in the saddle, or
    // the horse is gone and we have decided to walk. Neither ever loops: the
    // click budgets below are the whole retry policy.
    bool DismountToWork(Client& client, const Observation& obs);
    bool RemountAfterWork(Client& client, const Observation& obs);
    bool DoMine(Client& client, const Observation& obs);
    bool DoSmelt(Client& client, const Observation& obs);
    void NoteSmeltProgress(const Observation& obs);
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
    // `prefer` is a PREFERENCE, not a filter (`graphic` is the filter): when
    // the shelf has that exact graphic it is bought first, and when it does not
    // the errand falls through to its ordinary choice instead of walking away.
    // It exists so a scribe whose craft ladder is stuck on one missing spell
    // asks the shop for THAT scroll rather than a random one.
    bool BuyScrollFrom(Client& client, const Observation& obs, const char* trade,
                       wm::Service svc, u16 graphic, bool skipKnown, u16 qty,
                       const char* what, GoalKind owner, u16 prefer = 0);
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
    bool productionWithdrawalPending_ = false;

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
    // THE ARMOUR THIS GOAL JUST ASKED TO PUT ON, so the server's answer can be
    // attributed to a graphic. The equip action itself carries only a serial,
    // and the item is in flight between pack and paperdoll while the answer is
    // outstanding -- recording the graphic at the moment of asking is the one
    // reading that cannot be wrong. Cleared when the answer arrives.
    u32 pendingWearSerial_ = 0;
    u16 pendingWearGraphic_ = 0;
    // AND WHAT WAS ALREADY ON THAT LAYER when we asked, because it changes
    // what a refusal MEANS. Corus asked to wear leather leggings over the
    // cloth long pants he was already in and the server bounced them -- "You
    // put the long pants in your pack." (smoke 2026-09-07 22:04:31). That is
    // a fact about the OCCUPANT, not about leather leggings, and writing it
    // down as "this body cannot wear leather leggings" would be a lie that
    // outlived the trousers. An empty layer leaves no such excuse.
    u16 pendingWearOverGraphic_ = 0;
    // Serials this session's server has refused to put on. Session-scoped on
    // purpose: PersistentState holds no serials by design, and the durable
    // half of the lesson is the GRAPHIC, which goes to Memory::NoteUnwearable.
    // This is the belt to that braces -- it stops the retry even in the case
    // where the graphic could not be attributed.
    std::vector<u32> unwearableSerials_;
    i64 windDownStartedMs_ = 0;
    bool windDownCleanupPending_ = false;
    i32 windDownTrips_ = 0;
    bool windDownArrived_ = false;
    // Say "I cannot get out of here" ONCE. The blocked branch below re-arms
    // itself every 30 s on purpose (never logging out is worse than trying
    // again), but Kharain printed the same two lines 58 times from inside
    // Minoc Mine 1 and the one fact worth reading -- which cell -- was in
    // none of them.
    bool windDownBlockedLogged_ = false;
    // A SESSION PAST ITS LIMIT ALWAYS ENDS. Reaching the "no safe ground"
    // give-up branch once is worth one more retreat lap (never logging out
    // is worse than trying again); reaching it a second time means the spot
    // is a genuinely sealed pocket, and the fix is to log out here rather
    // than retry forever -- Kharazar (2026-09-07) ran wind-down past its
    // 300s deadline, printed "no safe logout", and then never logged out at
    // all, just survival ticks until the process was killed by hand.
    i32  windDownStuckCycles_ = 0;
    bool windDownUnsafeLogout_ = false;

    SessionSummary session_;
    i32 eraDate_ = 0;                          // uo/era.h, set in Configure
    static constexpr i64 kStatusIntervalMs = 10000;
    i64 lastStatusMs_ = 0;
    LiveStatus status_;
    std::vector<RecentGoal> recentGoals_;

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
    // A caster must first reach a tile from which the selected prey is
    // visible.  Keep this separate from currentFoe_: that field describes a
    // real fight and is also used by the survival response once the prey
    // retaliates.
    u32  huntApproachTarget_ = 0;
    // The server, rather than the local map, has the final word on a spell
    // target's visibility.  Remember the completed refusal long enough to
    // consume it once; otherwise the finished action would be read and logged
    // again on every tick before a new target is chosen.
    u32  casterReachRefusedTarget_ = 0;
    int PickPoisonOpener(Client& client, const Observation& obs) const;
    u32 poisonOpenedTarget_ = 0;
    // A TARGET THAT NEVER RETALIATES MUST NOT BE RE-PICKED FOREVER (audit
    // section 3.8). ScanHostiles has no reachability filter and ChoosePrey
    // returns the same best candidate every tick, so a foe that cannot be
    // hit or will not fight back was attacked and re-attacked with no
    // exchange (fleet122c30_20260907: "hunt: picked 'Spectre'" x28, zero
    // swings). Counted per serial and cleared at kill-success and at
    // travel-arrival (Train.cpp) -- NOT time-boxed like unreachable_ below,
    // because one hunting trip can easily outlast that 30 s window.
    static constexpr int kMaxHuntEngageTries = 3;
    std::vector<std::pair<u32, int>> huntEngageTries_;
    std::vector<u32> huntExcludedThisTrip_;
    // AN ATTEMPT ONLY COUNTS WHEN NOTHING WAS EXCHANGED (2026-09-07,
    // fleet122d30_20260907: "giving up on 'Chickadee'"/"'Rat'" excluded two
    // animals that were fleeing or dying under real damage and had simply
    // never gotten in range to hit back -- correct for Spectre, wrong for
    // them). Source-X sends no per-swing packet, so the target's own health
    // bar dropping since the FIRST attack on this serial is the only
    // observable proof a hit landed; recorded once per serial (first write
    // wins) and cleared whenever the budget resets or the trip ends.
    std::vector<std::pair<u32, double>> huntEngageStartHp_;
    double HuntEngageStartHp(u32 serial) const;      // -1.0 if unknown
    void SetHuntEngageStartHp(u32 serial, double hpFrac);
    void ResetHuntEngageTries(u32 serial);           // damage landed: fresh budget
    i32  HuntEngageTries(u32 serial) const;
    void BumpHuntEngageTries(u32 serial);
    bool IsHuntExcluded(u32 serial) const;
    void MarkHuntExcluded(u32 serial);
    void ClearHuntEngageState();
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
    // WHO KILLED US, by client-visible name, resolved at the alive->dead edge
    // from Client::LastAttackerName. Empty when nothing had swung at us inside
    // the window (or its name never arrived), which is when the older
    // "blame whatever we were fighting" fallback in DoSurvive still applies.
    std::string deathKillerName_;
    i32  ghostTrips_ = 0;
    static constexpr i32 kMaxGhostTrips = 4;
    // A healer can be visible yet unreachable behind a wall or a counter.
    // Remember that fact for this death so the ghost does not path to the
    // same sealed tile until its session expires.
    std::vector<u32> ghostHealerAvoid_;
    u32 ghostHealerTarget_ = 0;
    // Reaching a healer proves proximity, not that the NPC has already
    // noticed the ghost.  Keep the last direct resurrection request so a
    // crowded healer room receives a normal player prompt without speech
    // spam.
    u32 ghostHealerAsked_ = 0;
    i64 ghostHealerAskMs_ = 0;
    // A healer can wander into view while the ghost is walking to a fixed
    // atlas marker. Re-inspect the nearby human NPCs on a short cadence so
    // that live opportunity wins over a stale landmark journey.
    i64 ghostHealerScanAtMs_ = 0;
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
    // "You can't reach the Vendor" answers to a supply buy. Owner rule:
    // unreachable = 1 try, max 2 -- Elara asked a shopkeeper she had walked
    // away from twenty times in three minutes (2026-09-04 01:09-01:11).
    i32  supplyReachFails_ = 0;
    static constexpr i32 kMaxSupplyReachFails = 2;
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
    // --- cutting cloth into bandages TO SELL ---------------------------------
    //
    // A tailor's own bandages are stock, not kit. The shard makes this the one
    // good she can make with no skill and no menu -- type_scissors.scp hands
    // t_cloth straight to Source-X's hardcoded cut -- and the whole fleet's
    // fighting half is structurally short of them
    // (docs/BANDAGE_SUPPLY_SPEC.md section 1). Runs at the END of MAKE_CLOTH,
    // when the batch already has the cloth it came for, so it can never take
    // cloth the bench is waiting on.
    //
    // Returns true when it spent this tick's gesture.
    bool CutClothForSale(Client& client, const Observation& obs, i32 cloth);
    // HOW MANY BANDAGES ARE WORTH CUTTING FOR SALE, for THIS character.
    // Plan-derived (the life's own craft batch) and grown by what this tailor
    // has actually sold, so two tailors get two numbers and a tailor nobody
    // buys from stops at one batch ("thresholds must be dynamic", owner rule).
    i32  BandageSaleTarget() const;
    // --- one withdraw-and-cut batch per BANDAGES_FOR_SALE run ---------------
    //
    // Both are cleared by LeaveGoal, so a fresh pick gets a fresh batch and a
    // supersession cannot leave the goal believing it already did its work.
    // `saleClothTaken_` exists because a withdrawal can LAND and still not put
    // enough cloth in the pack to cut (a clamped move, a partial stack); asking
    // the box again on the next tick is precisely how a goal spins.
    bool saleCutMade_ = false;
    bool saleClothTaken_ = false;
    // How many times the bandage errand has asked who is standing in the
    // healer's shop. Reset on success; three unanswered scans stand the
    // goal down instead of re-walking to the same tile.
    i32  healerScans_ = 0;
    // How many DISTINCT tiles beside the current forge have answered "you
    // can't reach that". A refusal means move to another tile, not click
    // again from the same one; see DoSmelt.
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
    // SHELVES THIS CHARACTER HAS ALREADY EMPTIED, and when.
    //
    // A healer's shelf holds {5 20} bandages and refills on Sphere's
    // hardcoded ten-minute timer, so a fighter who wants a hundred empties
    // the first counter and must walk to the next one rather than reopening
    // the same drained window. Remembered here, on the life, because it is
    // knowledge about the town -- the purchase activity is deliberately
    // stateless between errands.
    struct DrainedShelf { u32 serial = 0; i64 whenMs = 0; };
    std::vector<DrainedShelf> drainedShelves_;
    void NoteDrainedShelf(u32 serial, i64 nowMs);
    // Serials still inside the restock window, freshest first.
    std::vector<u32> DrainedShelves(i64 nowMs) const;
    // How many BANDAGE counters this pass has emptied. Not the same list as
    // drainedShelves_, which also holds cloth and clothing shelves.
    i32 bandageCountersDrained_ = 0;
    // True once a counter has been emptied and no other healer or vet this
    // character can see is still worth walking to.
    bool BandageCountersAllDrained(Client& client, const Observation& obs) const;
    // The same fact for the OTHER medicine. i_potion_heal is stocked {3 12} by
    // VENDOR_S_HEALER_SHOP (tm_vend.scp:1111) and by the alchemist, and the
    // shelf rolls empty: Odessa opened the Britain healer's at 00:16:26,
    // 00:23:35 and 00:33:25 on 2026-09-06 and was told "does not stock heal
    // potion" all three times, each one a fresh cross-town trip (D10,
    // artifacts/validation_wave_2026-09-06.md). A drained potion shelf is
    // remembered exactly like a drained bandage shelf.
    i32 potionCountersDrained_ = 0;
    bool PotionCountersAllDrained(Client& client, const Observation& obs) const;
    // THE PATIENT HAS NOTHING TO HEAL WITH AND IS STANDING AT A COUNTER.
    // Set for the duration of one DoReplaceEquipment(medicineOnly) call made
    // from DoHeal, and only when the pack holds no bandage, no potion and the
    // book no heal spell. It is what lets a life whose catalogue deliberately
    // drops bandages (every crafter -- "so crafter do not buy bandages",
    // project owner 2026-08-30) still buy the ONE medicine the healer has in
    // stock rather than walk away at 12% HP.
    bool emergencySelfHeal_ = false;
    // The single exit from the bandage shop route (see Gear.cpp).
    bool StandDownBandageShopping(const Observation& obs, const char* why,
                                  i64 restMs);
    // WHEN THIS CHARACTER LAST SENT ITS OWN BANDAGE WTB OUT.
    //
    // The exit above now has TWO routes -- ask a player, or cut cloth -- and
    // the first one has to be bounded by something this life can see. It is
    // not bounded by the market goal: DoTradeWithPlayer listens for kListenMs
    // (three minutes) and writes `no_player_seller` for whatever want happened
    // to be FIRST in its list, which need not be the bandage. So the bandage
    // route keeps its own clock: one full announce cycle
    // (kMaxAnnounces x kAnnounceIntervalMs = 48 s, the same bound the buyer's
    // own listen window quotes), after which the scissors come out whatever
    // the market goal is still doing. Well inside kNoBandageCooldownMs.
    // 0 means never asked. Per-process, like every other ms clock here.
    //
    // STAMPED BY THE ANNOUNCE, NOT BY THE HAND-OFF. It used to be set the
    // instant StandDownBandageShopping chose the AskPlayers route, which made
    // the 48 s a clock on an intention rather than on a question: ten of
    // sixteen fighters logged "waited out my own bandage WTB and no seller
    // came" in a gate containing zero `trade: announcing 'WTB ... i_bandage'`
    // lines (artifacts/gate_bandage20_20260907/triage.md section 1). Only
    // runner/Economy.cpp's WTB announce writes it now.
    i64 bandageWtbAskedMs_ = 0;
    // WHEN THE ASK WAS DECIDED ON, as opposed to made. Two clocks because
    // they answer two different questions: bandageWtbAskedMs_ bounds "the
    // market heard me and said nothing" (real evidence about sellers) and
    // this one bounds "I meant to ask and never got the words out" (no
    // evidence about anyone). Only the first may ever be reported as a market
    // result. 0 means no ask is outstanding.
    i64 bandageWtbHandedOffMs_ = 0;
    // Bandage errands that ended without a purchase since the last one that
    // worked. Bounds the walk from healer to healer: three silent counters
    // is a town without stock, and the cloth route answers that.
    i32 bandageShopFails_ = 0;
    // TOPPING UP, not merely short. `bandageLow` is the trigger that OPENS
    // the restock; once open the errand runs to `bandageFull`, because a
    // request phrased as a total that stops the instant it crosses the floor
    // is the reason Castor walked out of Britain with 27.
    bool bandageTopUp_ = false;
    // Loose cloth, bought to be cut into bandages when no counter in town
    // still has any. See DoMakeBandages.
    life::BuyActivity bandageClothBuy_;
    // The same shelf's other row. Loose cloth sells in rows of a dozen or two
    // and runs out; a bolt is fifty cloth in one purchase, and it is what a
    // player with a purse buys rather than walking to Yew for sheep.
    life::BuyActivity bandageBoltBuy_;
    // "The weavers I can reach have no bolts today." A session latch, not a
    // shop note: a drained-shelf note is keyed by SHOPKEEPER and would also
    // hide that keeper's loose-cloth row, which is the row the fallback
    // wants. Cleared when the bandage goal is satisfied or stands down.
    bool bandageBoltsOut_ = false;
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
    // Has this session ever seen the character ALIVE? A bot that logs in as a
    // leftover ghost reads as a fresh alive->dead edge on its first Live tick,
    // and marking the tile it happens to be standing on as lethal would be a
    // claim about a death nobody observed. (g_Hector.console.txt 19:17:41,
    // 2026-09-04: "died here" logged at 5735,3193, a ghost's login position.)
    bool sawAliveOnce_ = false;
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
    // Offline harness seam; null in every live session.
    const Observation* obsOverride_ = nullptr;
    // A HandOff ends the goal but has no Client to clean up with; Tick runs
    // LeaveGoal from these on the next Live tick. See Runner::HandOff.
    bool        leavePending_ = false;
    GoalKind    leavePendingFrom_ = GoalKind::IdleBriefly;
    std::string leavePendingWhy_;
    life::VendorErrand foodErrand_;
    // Crop serials already tried during the current food errand. A ripe plant
    // changes after harvesting and an unripe one answers no useful action, so
    // never click either one again in a tight loop.
    std::vector<u32> foodCropsTried_;
    std::vector<std::string> foodFarmTried_;
    i32  foodFarmTrips_ = 0;
    i32  toolTrips_ = 0;
    // SHOPKEEPERS WHOSE STOCK LIST HAS BEEN READ AND DID NOT HOLD THE TOOL.
    // "One NPC is not the trade": a blacksmith whose restock roll came up
    // short is not evidence that no blacksmith sells a dagger, so the errand
    // remembers who it has already asked and walks to the next one instead of
    // reopening the same four-item shelf every eight seconds. Cleared when the
    // tool being sought changes, and when one is finally bought.
    std::vector<u32> toolVendorsTried_;
    std::string      toolVendorsTriedFor_;
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
    // The gathering dismount (DismountToWork/RemountAfterWork). `gatherOnFoot_`
    // means WE put this character on the ground for a mining or chopping
    // sitting, so somebody owes it a remount before it travels again. After
    // dismounting, the horse is explicitly told to come and follow rather
    // than assumed to keep pace through a mine or retreat. The two click
    // counters are the retry budgets, and `gatherMountLostMs_` is when we
    // first looked for it.
    bool gatherOnFoot_ = false;
    bool gatherComeCalled_ = false;
    bool gatherFollowCalled_ = false;
    i32  gatherDismountClicks_ = 0;
    i32  gatherRemountClicks_ = 0;
    i64  gatherMountLostMs_ = 0;
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
    // WHAT THE SKILL READ WHEN THIS BOUT OF PRACTICE BEGAN.
    //
    // A self-use skill never answers "that failed", so the only honest
    // measure of a practice bout is the server's own skill table before and
    // after it (Client::OnSkills, 0x3A). Selene issued ten
    // ActionUseSkill(Meditation) calls and stood down `Finish(true)` twice
    // with the skill at 20.0 the whole time (artifacts/
    // selene_train_false_positive_2026-09-06.md). Baseline is per skill: a
    // different wantPracticeSkill starts a new bout.
    int  practiceBaselineSkill_ = -1;
    i32  practiceBaselineTenths_ = 0;
    i32  practiceGains_ = 0;
    // Which selection of the goal the baseline belongs to
    // (GoalState::startedAtMs). Without it a bout that gained nothing leaves
    // its baseline behind, and a tenth won later at a trainer or in a fight
    // would be reported by the NEXT bout as won by practice.
    i64  practiceBoutMs_ = -1;
    // When the current item was ordered from the craft menu, so the next one
    // is not started on top of it.
    // The wait for a craft to actually produce something. A Handshake, not
    // a timestamp: it counts attempts, so a recipe that never lands gives
    // up instead of repeating for a whole session.
    life::Handshake craftWait_;
    bool makeLastIssued_ = false;
    i64 orderHeardMs_ = 0, orderAnnouncedMs_ = 0;
    void TickCraftOrders(Client& client, const Observation& obs);
    void AddCraftOrderNeeds(const Observation& obs, std::vector<Need>& needs);
    CraftOrder* ActiveCraftOrder(bool buying, i64 now);
    bool DriveCraftOrder(Client& client, const Observation& obs);
    void SettleCraftOrder(const Observation& obs);
    // THE SHARD IS STILL REPEATING. revolution_makelast.scp re-fires
    // MAKEITEM one second after every make, fail OR abort until
    // TAG.revo.makelast.remaining hits 0. Opening the menu while that runs
    // aborts the shard's swing, the shard retries a second later, aborting
    // ours -- Elara 2026-09-04 00:55: "You fail to complete the potion" 40
    // times in 30 s and CRAFT abandoned. So while the count is above zero
    // and the deadline has not passed, the bot does nothing but watch the
    // pack.
    i32  makeLastRemaining_ = 0;
    i64  makeLastDeadlineMs_ = 0;
    bool craftCursorPending_ = false;
    i64  craftClickedMs_ = 0;
    // Forges that refused from every tile that could be reached, so the next
    // look offers a different one.
    std::vector<std::pair<i32, i32>> deadForges_;
    i32  smeltForgeX_ = 0, smeltForgeY_ = 0;
    i32  smeltRefusals_ = 0;
    i32  smeltApproaches_ = 0;
    // One exact final hop per forge: TravelToPoint's leg-arrival slack
    // (kLegArriveSlack=3, ClientTravel.cpp) lets a radius-0 trip report ARRIVED
    // up to three tiles off the stand tile it was given, which is outside
    // kForgeReach.  See the comment at the use site in Gather.cpp DoSmelt.
    bool smeltFinalHop_ = false;
    // Tiles beside smeltForgeX_/Y_ the shard has already refused a smelt from.
    // A forge is a multi-tile static and its diagonals are not reliably in
    // reach, so a refusal retires the TILE; only when the ring is exhausted is
    // the forge itself written off. Cleared when the forge changes or a click
    // is answered. In memory only, never persisted: a tile that refused today
    // because of a dynamic blocker must not be condemned for ever.
    std::vector<std::pair<i32, i32>> smeltTriedStands_;
    // When the last step between forge stand tiles was issued, so the click
    // waits for it to land without waiting for ever.
    i64  smeltGotoMs_ = 0;
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
    // A serial becomes ours only through an observed tame success or through
    // mounting the animal we just bought/remounted.  Never infer ownership
    // from a nearby horse: feeding somebody else's pet is not care.
    u32  ownedPetSerial_ = 0;
    i64  petJournalReadMs_ = 0;
    i64  petCareStartedMs_ = 0;
    i64  petCareActionMs_ = 0;
    u32  petCareTarget_ = 0;
    u32  petCareVendor_ = 0;
    i32  petCareTrips_ = 0;
    enum class PetCarePhase : u8 { Idle, Dismount, Acquire, AwaitHay, Feed };
    PetCarePhase petCarePhase_ = PetCarePhase::Idle;
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
    // WHICH book that flag is about. A life can hold two (an empty one it
    // bought and the full one it just took out of the bank), and one bool
    // would give the second book the first one's "already looked".
    u32 spellbookOpenedSerial_ = 0;
    // spellbookOpened_ only means "opened at some point this session"; it says
    // nothing about whether containerItems_ still holds what that open sent.
    // A goal that reads the cache as empty while this flag is already true
    // gets one re-open before it is believed -- see the practice gate in
    // DoPracticeSkill. Reset whenever a non-empty read is seen, or whenever
    // spellbookOpened_ itself is reset to force a fresh open.
    bool practiceRecheckedBook_ = false;
    // The scroll graphic last dropped on the book, and the spell count before
    // it, so the next tick can tell a real add from a refusal.
    u16  scrollOfferedGraphic_ = 0;
    i32  spellsBeforeOffer_ = 0;
    // The scroll the craft ladder is waiting on, as last SAID. DoFillSpellbook
    // runs every tick, so announcing the wanted spell unconditionally printed
    // the same line sixteen times a second -- 600 identical lines in one
    // errand (run_gates/g_Thalia.console.txt:63-138, 2026-09-04). Say it when
    // it changes, not when it is true.
    u16  scrollPreferSaid_ = 0;
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
    // A field may be picked clean or still growing. Try a few distinct crop
    // grounds before spending gold at a counter, never loop a single farm.
    static constexpr i32 kMaxFarmFoodTrips = 3;
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
    int PickSurvivalSpell(Client& client, const Observation& obs, bool healing,
                          bool requireSupplies = true) const;
    // Every attack spell this book and this Magery allow, strongest first,
    // ignoring mana and pack -- the ladder the fight walks down and the list
    // the reagent errand shops for.
    void AttackLadder(Client& client, const Observation& obs,
                      std::vector<const spell::SpellDef*>& out) const;
    bool survivalRetreat_ = false;
    void RetreatToSafety(Client& client);
    // A retreat owns its journey even if the planner's instantaneous threat
    // observations clear before the character reaches safety. Returns true
    // while the caller must leave the survival work alone.
    bool ContinueSurvivalRetreat(Client& client, const Observation& obs);
    // Shout for the guards when this tile is under their protection. True
    // means the shout was the right answer here (so the caller need not also
    // run); false means there is no protection to call on. See Survive.cpp.
    bool CallGuardsIfProtected(Client& client, const Observation& obs);
    // The per-tick keeper for the shout. CallGuardsIfProtected is only
    // reached from two decision points; a retreat that CROSSES into a guard
    // zone reaches neither. Runs from Tick() every tick, whatever the goal.
    void KeepCallingGuards(Client& client, const Observation& obs);
    // When the last "Guards!" was shouted, so a per-tick decision does not
    // become a per-tick packet.
    i64 lastGuardCallMs_ = 0;
    bool ProcessHuntAftermath(Client& client, const Observation& obs);
    // A CONFIRMED KILL BELONGS TO THE GOAL THAT WENT HUNTING, NOT TO THE
    // ERRAND THAT FOUGHT. ProcessHuntAftermath runs once per tick from
    // Tick() (Core.cpp), before the goal is chosen, and the fight itself is
    // usually owned by SURVIVE -- so calling NoteProgress() where the kill
    // is confirmed would credit whichever goal happened to be current
    // (measured: goal_completed=SURVIVE progress=0 while TRAIN_COMBAT, the
    // goal that walked to the graveyard, completed with progress 0 until
    // the anti-spin backstop cooled it off -- Hector 4 kills, Leander 6,
    // artifacts/fleet100_triage_2026-09-06.md Cause A). The kill is
    // RECORDED here and CONSUMED by DoTrainCombat on its next tick, which
    // is the goal that owns the trip.
    i32 huntKillsPending_ = 0;
    // Arrivals at a hunting ground that produced no fight. A yard with
    // nothing in it must end as a failure with a reason, not as a goal that
    // completes having done nothing. Reset when a fight is opened, when a
    // kill is credited, and by HandOffFromHunt.
    i32 huntEmptyArrivals_ = 0;
    // Every skill's value (Meditation excepted) as the last 0x3A left it in
    // the Observation, so a real gain -- in a fight, at the bench, in the
    // field or over a patient -- is said out loud once.
    std::map<int, i32> combatSkillSeen_;
    void NoteCombatSkillGains(const Observation& obs, bool inFight);
    // Practice bout bookkeeping: baseline on entry, one line per real gain.
    // Returns true if the skill has risen since the bout began.
    bool NotePracticeGain(int skillId, i32 have);
    // Closes a practice bout: success only if the skill actually moved.
    bool EndPracticeBout(const Observation& obs, int skillId, i64 cooldownMs);
    u32 huntLootCorpse_ = 0;
    i32 huntLootFailures_ = 0;
    bool huntLootMovePending_ = false;
    i64 huntLootNextMs_ = 0;
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
    i64 bankItemMoveJournalMs_ = 0;
    u32 bankItemMoveSerial_ = 0;
    u32 bankItemMoveDestination_ = 0;
    u16 bankItemMoveAmount_ = 1;
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
    usize huntPatrolStep_ = 0;
    usize huntGroundRotation_ = 0;
    i32 huntCrowdMoves_ = 0;
    i64 huntSocialMs_ = 0;
    // A low-stock departure may be authorised only when both ways to restock
    // have already stood down.  That approval belongs to this hunt, not to
    // the character forever: otherwise a shop cooldown expiring during the
    // walk cancels a safe trip without a bandage being used.  HandOffFromHunt
    // and a credited kill clear it so the next hunt rechecks its supplies.
    bool huntUnderstockAuthorized_ = false;
    // HandOff out of TRAIN_COMBAT, returning the trip allowance: the goal that
    // takes over plans its own journeys and they are not attempts to reach a
    // hunting ground.
    bool HandOffFromHunt(GoalKind to, i64 forMs, const char* why, i64 nowMs);
    // HP at the last per-minute danger note of the fight in progress; -1 when
    // no fight is being watched. What Survive.cpp turns into "how much did
    // this ground actually cost me this minute".
    i32 dangerWatchHp_ = -1;
    static constexpr int kMaxHuntTrips = 3;
    // --- stat farming (STAT_FARM) ------------------------------------------
    // The locks are in the farming configuration right now: STR UP, DEX
    // LOCKED, INT UP. MaintainBuildLocks must not fight this -- it is an
    // explicit, logged farming step, not the end-of-build lock policy.
    bool statFarmActive_ = false;
    // Sent this process. Stat locks are never echoed by the server, so this
    // is a "have I asked" flag rather than a cache of server truth.
    bool statFarmLocksSent_ = false;
    // What came out of the hands to make fists, so it can go back afterwards.
    u32  statFarmStowedWeapon_ = 0;
    // The skill/stat readings the last swing was measured against, so
    // NoteProgress is only called when the server actually moved something.
    i32  statFarmWrestlingAtSwing_ = -1;
    i32  statFarmStrAtSwing_ = -1;
    i32  statFarmSwings_ = 0;
    // Nothing here to punch: a long rest rather than a retry, because
    // "no dummy and no wildlife in view" is a fact about this PLACE.
    static constexpr i64 kStatFarmStandDownMs = 10 * 60 * 1000;
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
    // HOW BIG THIS SITTING IS. Read once, from the pack, on the first plan
    // of a sitting: the material bought or gathered in bulk sets the run,
    // craftBatch is only the floor. 0 = not yet measured. Owner rule
    // 2026-09-04 (crafters-stock-then-sit): Elara bought 73 nightshade and
    // brewed them in ten separate 5-piece sittings because the bench never
    // read the stock the supply errand had sized itself on.
    i32         craftSittingTarget_ = 0;
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
    void FinishNpcSaleVisit(i64 nowMs);
    std::string sellItem_;             // defname currently being sold
    std::string sellTrade_;            // paperdoll-title substring to look for
    wm::Service sellService_ = wm::Service::None;
    usize sellBuyerIndex_ = 0;         // which buyer of sellItem_ we are trying
    i32   sellTrips_ = 0;
    i32   sellWanted_ = 0;             // how many units we mean to sell
    // Shops of this trade already walked to while trying to sell. Without it
    // every trip asked the atlas the same question and got the same answer:
    // Odessa "arrived at 1427,1658" on trip 1 and again on trip 2 four
    // seconds later, then wrote the trade off (run_gates/g_Odessa.console.txt
    // :774,788,798). Same shape, and the same cure, as trainerShopsTried_.
    std::vector<std::string> sellShopsTried_;
    // No mobile is silenced for selling -- the parameter exists so the
    // skipping lookup can be reused, and an empty list keeps that explicit.
    std::vector<u32> sellSilent_;
    // Where a remembered buyer was said to be, while we walk to it. Standing
    // there and seeing nobody of the trade is the disproof (Train.cpp does
    // the same for trainers); the note is dropped on that FIRST miss.
    i32   sellKnownX_ = 0;
    i32   sellKnownY_ = 0;
    // Close enough to a noted spot to call it visited. Three tiles is the
    // same tolerance Train.cpp uses before forgetting a trainer.
    static constexpr i32 kStaleNoteMissWithin = 3;
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
    // Must match the radius passed to the logout_guarded TravelToPoint call.
    // A character already inside this radius of the guarded PLACE is already
    // there -- re-issuing that travel completes in the same tick (0 legs) and,
    // with a hostile still merely IN SCAN, looked identical to the tick
    // before it. Morven, Rhaler and Kharain ping-ponged between "running for
    // guarded ground" and "arrived somewhere safe" about 9,300 times each at
    // Minoc Mine 1 before being killed at the 10-minute mark
    // (fleet122_20260907). Being this close to guarded ground, with nothing
    // actually attacking, is the safest logout this session is going to get.
    static constexpr i32 kWindDownGuardedArrivalRadius = 3;
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
    // THE COUNTING FORM OF THE SAME READER, for market::ChooseSellOffer's
    // `unsoldWindows` -- how many separate `no_player_buyer` events for this
    // item fall inside the same one-hour horizon PlayersDeclined already
    // uses. Each one IS a closed announce window (DoTradeWithPlayer writes
    // one when kMaxAnnounces offers went unanswered), so this is not a new
    // clock, only a count instead of a bool.
    i32  UnsoldWindowsFor(const std::string& item, i64 nowMs) const;
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
    // ONE LOG LINE PER ITEM PER SESSION, when a crafter's own floor
    // (market::ComputeCraftedGoodFloor) applies to `item`: "price: <item>
    // floor N (materials M x labour L; npc payout P)". Not logged for a raw
    // NPC input or an item with no floor -- CraftedGoodFloor::applies is
    // false for those, and this writes nothing. Called from every site that
    // actually uses the floor (WTS announce, WTB announce, answering a heard
    // WTB) so a grader can see the number that decided the price, without
    // repeating the line every announce cycle.
    void NoteCraftedGoodFloorOnce(const std::string& item);
    std::set<std::string> priceFloorLogged_;
    i32   sellGoldBefore_ = -1;        // purse before the sale, to verify it
    // ...and what the pack held, so a sale is confirmed by goods LEAVING as
    // well as gold arriving. See DoEarnGold.
    i32  sellItemBefore_ = -1;
    bool  sellAsked_ = false;          // 0x9E requested
    i64   sellAskedMs_ = 0;
    bool  sellSent_ = false;           // ActionVendorSell issued
    u32   sellVendorSerial_ = 0;       // the vendor we mean to deal with
    bool  sellApproached_ = false;     // walked to them before speaking
    bool  sellReachChecked_ = false;   // stepped into touch range once per list

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
    std::string tradeOrderId_;
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
    // What the BUYER actually offered gold for -- min(delivered, wanted) at
    // the moment the coin went in, counted from the partner's own side of the
    // window rather than promised in advance. If the partner's side no longer
    // matches this by the time both boxes are ready to check, the deal has
    // changed since it was priced and gets cancelled, not accepted. See
    // DriveOpenTrade ("a buyer pays for what is in the window", 2026-09-07).
    i32  tradeOfferedQty_ = 0;
    // THE WINDOW OPENS IN TWO STEPS, ALWAYS. A 2.0.x trade opens by DROPPING
    // one item on the partner (trade.h's own header comment) -- Source-X
    // creates the window on that single-item drop -- and the seller's own
    // DriveOpenTrade code then adds its REAL offered quantity in a second,
    // separate move a tick or two later. A buyer that priced off the
    // opening 1 saw it jump to the seller's true count moments later, read
    // that as the seller changing the deal, and cancelled a perfectly
    // ordinary trade (live smoke evidence, run
    // smoke_Aelia_Wren_Baelos_Calar_20260907_1619: Aelia opened with 1
    // i_bandage, Baelos priced and paid for 1, Aelia's own 39 landed 2.5s
    // later, Baelos read 1->39 as a change and cancelled). So the buyer
    // waits for the delivered count to hold STILL for kTradeSettleMs before
    // pricing anything off it -- these two track what was last seen and
    // when, purely for that debounce.
    i32  tradeSeenQty_ = -1;
    i64  tradeSeenQtyMs_ = 0;
    static constexpr i64 kTradeSettleMs = 2500;
    i32  tradePackBefore_ = 0;   // the PACK is the proof, not the packet
    i32  tradeGoldBefore_ = 0;
    // Secure-trade CLOSE can arrive before the backpack's changed contents.
    // Refresh it once before comparing the after-state to these baselines.
    bool tradePackRefreshPending_ = false;
    // Minoc wood stands already exhausted during this session. A lumberjack
    // begins at the closest town-side stand, then expands only when needed.
    std::vector<std::string> minocLumberTried_;
    i64  tradeHeardMs_ = 0;
    i64  tradeAnnouncedMs_ = 0;
    i64  tradeOpenedMs_ = 0;
    i32  tradeAnnounceCount_ = 0;
    // A market trip scans the room once before it starts speaking. A zero
    // audience is meaningful only after that scan: an untouched client cache
    // cannot distinguish an empty bank from a room whose paperdolls have not
    // arrived yet. The stamp is short-lived so a later visit observes a new
    // crowd instead of inheriting an old empty-room verdict.
    i64  marketAudienceScanMs_ = 0;
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
    static constexpr i64 kAudienceRescanMs = 30 * 1000;
    // Is this character's home-town market place usable? -1 not resolved
    // yet, 0 no, 1 yes.
    int  marketPlaceOk_ = -1;
    // The resolved place id itself (ResolveHomeMarketPlaceId), cached
    // alongside marketPlaceOk_ so it is computed once per life, not once per
    // tick.
    std::string marketPlaceId_;
    // WHY: the two-hub comparison's own explanation ("nearer than Minoc bank
    // by 1132 tiles"), set by ResolveHomeMarketPlaceId (mutable: that method
    // stays const) and read once by MarketPlaceUsable for the session's one
    // `market: the market is ...` log line.
    mutable std::string marketPlaceWhy_;
    // S7 SEQUENTIAL HUBS (owner ruling 2026-09-07): "it can check minoc first
    // britain after, same for buyers as well". ResolveMarketHub's comparison
    // (which of Britain/Minoc is cheaper) depends on where the character was
    // standing the ONE time it was ever run (marketPlaceOk_'s life-long
    // latch), so the pairing itself -- which hub is "primary" and which is
    // "other" -- never changes for this life. marketPrimaryPlaceId_/
    // marketPrimaryLabel_ hold that permanent pairing; marketOtherPlaceId_/
    // marketOtherLabel_ hold the permanent OTHER side of it (empty when the
    // atlas only knows one hub at all -- nothing to fall back to). Neither
    // is ever mutated outside ResolveHomeMarketPlaceId.
    //
    // marketPlaceId_/marketPlaceLabel_ ABOVE, by contrast, are the ACTIVE
    // hub for the errand in progress -- normally equal to the primary pair,
    // but swapped to the other one by TryOtherMarketHub mid-errand, and put
    // back by ResetTradeState so the NEXT independent errand starts at the
    // primary (cheaper) hub again rather than staying wherever the last one
    // left off.
    mutable std::string marketPrimaryPlaceId_;
    mutable std::string marketPrimaryLabel_;
    mutable std::string marketOtherPlaceId_;
    mutable std::string marketOtherLabel_;
    mutable std::string marketPlaceLabel_;
    // Has this errand already tried the second hub? Set the moment
    // TryOtherMarketHub switches marketPlaceId_ over (or vetoes the trip),
    // so a bounced goal re-entry cannot switch back and forth between the
    // two hubs -- one switch per errand, same as tradeTrips_ is one trip
    // allowance per errand. Reset in ResetTradeState.
    bool marketHubTried_ = false;
    // Attempt the OTHER hub after the current one's announce/listen window
    // closed with nobody trading. Returns true when the switch is made (the
    // caller must return false and let the goal keep running -- the normal
    // "arrived" check next tick will see marketPlaceId_ point somewhere new
    // and start the walk) or when the trip was vetoed as unaffordable (the
    // caller falls through to its own "no seller/buyer came" failure).
    // Returns false, and leaves marketPlaceId_ untouched, when there is no
    // second hub to try at all or this errand already tried it.
    bool TryOtherMarketHub(Client& client, const Observation& obs);
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
