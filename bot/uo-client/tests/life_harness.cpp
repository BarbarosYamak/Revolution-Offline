// Deterministic offline life harness.
//
// The REAL Runner and the REAL Client, with the three things a test cannot
// have replaced and nothing else: the wall clock (Client::SetClockForTest --
// every clock read in Client goes through NowMs()), the socket
// (SetOfflineForTest -- Send() captures instead of writing), and the world
// (Runner::SetObservationOverrideForTest -- Observe() returns the scripted
// Observation with this tick's nowMs patched in). The life layer itself has
// no clock and no randomness, so everything between the Observation and the
// captured packets is the same code the live bot runs. No server, no socket,
// no MULs, no atlas.
//
// What it is for: goal LIFETIME. Three scenarios, each one a failure family
// taken from a live run:
//
//   S1  an errand must not survive a genuine goal change
//       (review_combat_trace_2026-09-05.md section 3: bandageBuy_ resumed
//       mid-Verify after a Survive preempt and blacklisted a shopkeeper that
//       never refused anything).
//   S2  a same-kind re-pick must keep its journey -- the Corran rule at
//       runner/Core.cpp, and the regression guard that S1's fix must not
//       over-cancel.
//   S3  an in-flight client action must be finished when the goal changes
//       (review_runtime_evidence_2026-09-05.md family 2a: Xerxes reissued
//       open_container 0x4000CB1D across four goal boundaries).

#include "Client.h"
#include "life/Runner.h"
#include "uo/life.h"
#include "uo/professions.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace uo;

namespace uo::life {
struct RunnerHarnessAccess {
    static bool ClothAfterCompletedJourney(Runner& runner, Client& client,
                                           const Observation& obs) {
        runner.travelInFlight_ = true;
        runner.DoMakeCloth(client, obs);
        return runner.travelInFlight_;
    }
    static bool ClothLoadAfterRestart(Runner& runner, Client& client,
                                     const Observation& obs) {
        runner.needCfg_.profession = nullptr;
        runner.planner_.Mutable().kind = GoalKind::MakeCloth;
        runner.DoMakeCloth(client, obs);
        return runner.clothHeadingToWheel_;
    }
    static void SeedLiveFinalSale(Runner& runner, const Observation& obs) {
        SeedFinalNpcSale(runner, obs);
        runner.phase_ = Runner::Phase::Live;
        runner.nextActionMs_ = 0;
        auto& goal = runner.planner_.Mutable();
        goal.kind = GoalKind::EarnGold;
        goal.active = true;
        goal.startedAtMs = obs.nowMs;
        goal.attempts = 0;
        runner.state_.productionBatch.item = "i_dagger";
        runner.state_.productionBatch.phase = ProductionPhase::Sell;
    }
    static bool SaleRecorded(const Runner& runner) {
        return runner.state_.memory.HasEvent("sold_to_vendor");
    }
    static void SeedLivePurchase(Runner& runner, const Observation& obs) {
        runner.phase_ = Runner::Phase::Live;
        runner.nextActionMs_ = 0;
        auto& goal = runner.planner_.Mutable();
        goal = {};
        goal.kind = GoalKind::BuySupplies;
        goal.active = true;
        goal.startedAtMs = obs.nowMs;
        runner.pendingBuyItem_ = "i_bottle_empty";
        runner.pendingBuyGoldBefore_ = obs.gold + 12;
    }
    static i32 PurchasesRecorded(const Runner& runner) {
        return runner.state_.ledger.TotalFor(market::GoldFlow::DestroyedVendorPurchase);
    }
    static void SeedSmelt(Runner& runner, const Observation& obs) {
        runner.smeltStartedMs_ = obs.nowMs - 1000;
        runner.smeltIngotName_ = "i_ingot_iron";
        runner.smeltIngotsBefore_ = 2;
    }
    static bool Smelt(Runner& runner, Client& client, const Observation& obs) {
        return runner.DoSmelt(client, obs);
    }
    static void SettleSmeltBeforePlanning(Runner& runner, const Observation& obs) {
        runner.NoteSmeltProgress(obs);
    }
    static bool HasFirstSmelt(const Runner& runner) {
        return runner.state_.memory.HasEvent("first_smelt");
    }
    static void SeedFinalNpcSale(Runner& runner, const Observation& obs) {
        runner.sellItem_ = "i_dagger";
        runner.sellVerifyItem_ = "i_dagger";
        runner.sellTrade_ = "blacksmith";
        runner.sellVendorSerial_ = 0x9693;
        runner.sellSent_ = true;
        runner.sellAsked_ = true;
        runner.sellAskedMs_ = obs.nowMs;
        runner.sellWanted_ = 1;
        runner.sellItemBefore_ = 1;
        runner.sellGoldBefore_ = obs.gold - 24;
        runner.sellSweeps_ = Runner::kMaxSellSweeps - 1;
        runner.sellSweepGold_ = 168;
    }
    static bool EarnGold(Runner& runner, Client& client, const Observation& obs) {
        return runner.DoEarnGold(client, obs);
    }
    static bool Survive(Runner& runner, Client& client, const Observation& obs) {
        return runner.DoSurvive(client, obs);
    }
    static void Dispatch(Runner& runner, Client& client, const Observation& obs) {
        runner.RunGoal(client, obs);
    }
    static void SetActiveGoal(Runner& runner, GoalKind kind, i64 nowMs) {
        auto& goal = runner.planner_.Mutable();
        goal = {};
        goal.kind = kind;
        goal.active = true;
        goal.startedAtMs = nowMs;
    }
    static bool Retreating(const Runner& runner) { return runner.survivalRetreat_; }
    static bool HealDone(Runner& runner, Client& client, const Observation& obs) {
        return runner.DoHeal(client, obs);
    }
    static bool RecoverJourney(Runner& runner, Client& client,
                               const Observation& obs, RecoveryStep previous) {
        runner.lastRecoveryPlan_ = previous;
        runner.travelInFlight_ = true;
        runner.DoRecoverCorpse(client, obs);
        return runner.travelInFlight_;
    }
    static HealStep Heal(Runner& runner, Client& client, const Observation& obs,
                         const prof::Profession& profession) {
        const auto* saved = runner.needCfg_.profession;
        runner.needCfg_.profession = &profession;
        runner.DoHeal(client, obs);
        runner.needCfg_.profession = saved;
        return runner.lastHealPlan_;
    }
    // D12: which bandage line DoTrainCombat's per-tick gate used. Returns the
    // hand-off reason when the gate fired, "" when it did not. leavePending_
    // is cleared first because nothing ticks between direct calls.
    static std::string HuntGateReason(Runner& runner, Client& client,
                                      const Observation& obs) {
        runner.leavePending_ = false;
        runner.leavePendingWhy_.clear();
        runner.DoTrainCombat(client, obs);
        return runner.leavePending_ ? runner.leavePendingWhy_ : std::string();
    }
    static void AuthorizeUnderstockHuntForTest(Runner& runner) {
        runner.huntUnderstockAuthorized_ = true;
    }
    // D10: drive the equipment errand once, as the planner would.
    static void ReplaceEquipment(Runner& runner, Client& client,
                                 const Observation& obs, bool medicineOnly) {
        runner.DoReplaceEquipment(client, obs, medicineOnly);
    }
    // D14: did the wounded character leave the spot, rather than rest on it?
    static bool RetreatedInsteadOfResting(Runner& runner, Client& client,
                                          const Observation& obs) {
        runner.survivalRetreat_ = false;
        runner.DoHeal(client, obs);
        return runner.survivalRetreat_;
    }
    // Cause A (artifacts/fleet100_triage_2026-09-06.md): a confirmed kill
    // must reach the goal that went hunting. ProcessHuntAftermath records
    // the kill in huntKillsPending_; this drives the consumption side --
    // one DoTrainCombat tick, then the same Finish/NoteRan pair RunGoal
    // performs when a handler returns true.
    static bool CreditedKillEndsTheHunt(Runner& runner, Client& client,
                                        const Observation& obs, int kills,
                                        int* progressGained, bool* spun) {
        runner.huntKillsPending_ = kills;
        const int before = runner.planner_.Current().progress;
        const bool done = runner.DoTrainCombat(client, obs);
        *progressGained = runner.planner_.Current().progress - before;
        if (done) {
            runner.planner_.NoteRan(runner.planner_.Current().kind, obs.nowMs);
            runner.planner_.Finish(true, nullptr, obs.nowMs);
        }
        *spun = runner.planner_.TakeSpinDetected() != GoalKind::Count;
        return done;
    }
    static i32 PendingHuntKills(const Runner& runner) {
        return runner.huntKillsPending_;
    }
    // A PRACTICE BOUT IS JUDGED BY THE SKILL, NOT BY THE ATTEMPT COUNT.
    //
    // Selene sent ten ActionUseSkill(Meditation) calls and PRACTICE_SKILL
    // completed twice with the skill on 20.0 throughout
    // (artifacts/selene_train_false_positive_2026-09-06.md). Drives the
    // handler until it claims an outcome, moving the observed skill up by one
    // tenth on tick `gainAtTick` (negative: never). Returns the handler's
    // success return.
    static bool PracticeBoutClaimsSuccess(Runner& runner, Client& client,
                                          Observation& obs, int skillId,
                                          int gainAtTick, i32* gains) {
        obs.wantPracticeSkill = skillId;
        bool done = false;
        for (int i = 0; i < 8 && !done; ++i) {
            if (gainAtTick >= 0 && i == gainAtTick) {
                for (SkillTarget& sk : obs.skills)
                    if (sk.skillId == skillId) sk.tenths += 1;
            }
            done = runner.DoPracticeSkill(client, obs);
            // The counter belongs to the bout and EndPracticeBout clears it,
            // so the high-water mark is what the bout actually reported.
            if (runner.practiceGains_ > *gains) *gains = runner.practiceGains_;
            if (client.ActionBusy())
                client.CompleteActionForTest(act::Result::Success, "harness");
            obs.nowMs += 12000;
        }
        return done;
    }
    static HealStep LastHealPlan(const Runner& runner) {
        return runner.lastHealPlan_;
    }
    static const NeedConfig& Needs(const Runner& runner) {
        return runner.needCfg_;
    }
    // Kharain (wave 2): trainTrips_ survived goal supersession, so a
    // TRAIN_AT_NPC that was interrupted resumed already on its ceiling and
    // failed "no 'tinker' reachable after 3 trips" without travelling. The
    // allowance belongs to the errand.
    static i32 TrainTrips(const Runner& runner) { return runner.trainTrips_; }
    static void SetTrainTrips(Runner& runner, i32 n) { runner.trainTrips_ = n; }
    static i32 MaxTrainTrips() { return Runner::kMaxTrainTrips; }
    static void LeaveGoalForTest(Runner& runner, Client& client, GoalKind from,
                                 GoalKind to) {
        runner.LeaveGoal(client, from, to, from == to, "harness");
    }
    // S5: one tick of the bandage chain, called as the planner would call it.
    static bool MakeBandages(Runner& runner, Client& client,
                             const Observation& obs) {
        return runner.DoMakeBandages(client, obs);
    }
    // WHICH ROUTE THE BANDAGE STAND-DOWN TOOK. bandageWtbHandedOffMs_ is the
    // clock the WTB branch starts and the cut branch clears, so after the call
    // "non-zero" IS "took the ask route" -- no second flag to drift from it.
    //
    // It used to read bandageWtbAskedMs_, which was the same clock until the
    // two facts were split: "I decided to ask" (this one) and "the words went
    // out on the wire" (bandageWtbAskedMs_, now stamped only by the WTB
    // announce in runner/Economy.cpp). Only the second may ever be reported as
    // a market result -- ten of sixteen fighters concluded "no seller came"
    // without ever speaking (artifacts/gate_bandage20_20260907/triage.md).
    // The assertions below are unchanged; this reads the field that now
    // carries the fact they were always about.
    struct BandageRoute { bool askPlayers = false; std::string why; };
    static BandageRoute BandageStandDown(Runner& runner, const Observation& obs,
                                         i64 askedMs, bool sellersDeclined) {
        runner.bandageWtbAskedMs_ = askedMs;
        runner.bandageWtbHandedOffMs_ = askedMs;
        runner.bandageCountersDrained_ = 1;
        if (sellersDeclined) {
            runner.state_.memory.NoteEvent("no_player_seller", "i_bandage", "",
                                           obs.x, obs.y, obs.nowMs);
        }
        runner.leavePending_ = false;
        runner.leavePendingWhy_.clear();
        runner.StandDownBandageShopping(obs, "every counter in town is empty",
                                        1000);
        BandageRoute r;
        r.askPlayers = runner.bandageWtbHandedOffMs_ != 0;
        r.why = runner.leavePendingWhy_;
        return r;
    }
    // The tailor's cut-for-sale step, driven directly. Returns true when it
    // spent the tick's gesture on a cut.
    static bool CutClothForSale(Runner& runner, Client& client,
                                const Observation& obs, i32 cloth) {
        return runner.CutClothForSale(client, obs, cloth);
    }
    static i32 BandageSaleTarget(const Runner& runner) {
        return runner.BandageSaleTarget();
    }
    static void NoteBandageSale(Runner& runner, const Observation& obs) {
        runner.state_.memory.NoteEvent("traded_with_player", "i_bandage",
                                       "a fencer", obs.x, obs.y, obs.nowMs);
    }
    // Trips to a flock this chain has actually STARTED. Zero after a tick
    // that bought cloth or stood down; one after a tick that set off.
    static i32 BandageFlockTrips(const Runner& runner) {
        return runner.bandageTrips_;
    }
    static bool SuccessfulDepositResetsBudget(Runner& runner, Client& client,
                                             const Observation& obs) {
        runner.bankDepositTries_ = 4;
        runner.bankDepositItem_ = "i_ingot_iron";
        runner.bankItemMovePending_ = true;
        client.ActionOpenContainer(0x4000AAAAu);
        client.CompleteActionForTest(act::Result::Success, "confirmed deposit");
        runner.SettleBankItemMove(client, obs);
        return runner.bankDepositTries_ == 0 && runner.bankDepositItem_.empty();
    }
    // S16: the session's own death tally, tracked once per Tick() by
    // TrackDeathEdge -- independent of Phase and of obsOverride_'s scripted
    // Observation (TrackDeathEdge reads client.IsDead() directly, the same
    // fact death_location's RecordOwnDeath is built from).
    static i32 SessionDeaths(const Runner& runner) { return runner.session_.deaths; }
};
}

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

void Section(const char* name) { std::printf("[%s]\n", name); }

// --- the harness ----------------------------------------------------------

struct Harness {
    std::unique_ptr<Client> client;
    life::Runner            runner;
    life::Observation       obs;
    i64                     nowMs = 1'000'000;

    // Where Client::DataDir() should point. Only the scenarios that read a
    // shard data table (S5 reads revolution_pastures.tsv) set it; the rest
    // keep the harness's deliberate emptiness.
    const char* atlasPathForDataDir = nullptr;

    bool Boot(const std::string& dataRoot, const char* professionId) {
        Client::Config cfg{};
        cfg.loginHost = "127.0.0.1";
        cfg.atlasPath = atlasPathForDataDir;
        cfg.username = "life_harness";
        cfg.password = "life_harness";
        cfg.version = "2.0.7";
        cfg.sendSeed = false;
        cfg.sessionTag = "life_harness";
        client = std::make_unique<Client>(cfg);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(nowMs);

        life::RunnerConfig rc;
        rc.dataRoot = dataRoot;
        rc.accountName = "life_harness";
        rc.characterName = professionId;   // one save file per scenario
        rc.professionId = professionId;
        rc.sessionLimitMs = 0;             // no session clock in the harness
        rc.verbose = true;
        std::string err;
        if (!runner.Configure(rc, &err)) {
            std::printf("  FAIL  Configure: %s\n", err.c_str());
            ++g_failures;
            return false;
        }
        runner.SetObservationOverrideForTest(&obs);
        return true;
    }

    // One tick, dt milliseconds after the last one.
    void Step(i64 dtMs = 500) {
        nowMs += dtMs;
        client->SetClockForTest(nowMs);
        obs.nowMs = nowMs;
        runner.Tick(*client, nowMs);
    }

    void Steps(int n, i64 dtMs = 500) {
        for (int i = 0; i < n; ++i) Step(dtMs);
    }

    life::GoalKind Goal() const { return runner.GetPlanner().Current().kind; }
    const char* GoalName() const { return life::GoalKindName(Goal()); }

    // Walk out of AwaitWorld/Reconcile into Phase::Live.
    void EnterLive() { Steps(4, 1000); }
};

// A living, unremarkable fencer: alive, unhurt, on foot, in Britain, with
// money and an empty pack. Every scenario starts from this and changes one
// thing.
life::Observation BaselineFencer(i64 nowMs) {
    life::Observation o;
    o.nowMs = nowMs;
    o.inWorld = true;
    o.x = 1420;
    o.y = 1690;
    o.hp = 60;
    o.hpMax = 60;
    o.mana = 20;
    o.str = 60;
    o.dex = 60;
    o.intel = 20;
    o.gold = 2000;
    o.goldOnHand = 2000;
    o.weight = 20;
    o.maxWeight = 400;
    o.bandages = 0;          // the errand this suite drives
    o.healPotions = 5;
    o.food = 5;
    o.weaponEquipped = true;
    o.schoolWeaponEquipped = true;
    o.hasBasicArmor = true;
    o.marketQuiet = true;    // no player market to wander off to
    o.atWorkSite = false;
    return o;
}

// --- S1 -------------------------------------------------------------------
// An in-flight activity does not survive a genuine goal change. The old probe
// was the NPC bandage errand, but ordinary bandages are now player-market then
// self-supply work; lifecycle correctness belongs to the goal boundary, not
// to that retired NPC-first route.
void ScenarioErrandDoesNotSurviveGoalChange(const std::string& tmpDir) {
    Section("S1 errand does not survive a genuine goal change");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    if (!h.Boot(tmpDir + "/s1", "fencer")) return;

    // An action issued by whichever ordinary goal won is enough to exercise
    // the invariant. No client tick runs in this harness, so only the goal
    // transition below can settle it.
    h.EnterLive();
    h.Steps(6, 1000);
    const life::GoalKind before = h.Goal();
    h.client->ActionOpenContainer(0x40001234u);
    Check(h.client->ActionBusy(), "an ordinary goal has work in flight");

    life::RunnerHarnessAccess::LeaveGoalForTest(
        h.runner, *h.client, before, life::GoalKind::Survive);
    Check(!h.client->ActionBusy(),
          "a different goal cancels the departing activity, not its successor's");
}

// --- S2 -------------------------------------------------------------------
// A same-kind re-pick keeps its journey (the Corran rule). The guard against
// S1's fix over-cancelling.
void ScenarioSameKindRepickKeepsItsJourney(const std::string& tmpDir) {
    Section("S2 same-kind re-pick keeps its journey");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    if (!h.Boot(tmpDir + "/s2", "fencer")) return;
    h.EnterLive();
    h.Steps(6, 1000);

    // A SELF-SUPERSESSION IS NOT A GOAL CHANGE. Use the same LeaveGoal path
    // directly so the assertion does not depend on a particular bandage
    // supply route winning the planner on this simulated empty world.
    const life::GoalKind kind = h.Goal();
    h.client->ActionOpenContainer(0x40001234u);
    Check(h.client->ActionBusy(), "a same-kind re-pick begins with work in flight");
    life::RunnerHarnessAccess::LeaveGoalForTest(h.runner, *h.client, kind, kind);
    Check(h.client->ActionBusy(),
          "the re-pick reset nothing -- the Corran rule still holds");
}

// --- S3 -------------------------------------------------------------------
// An in-flight client action is finished when the goal changes.
void ScenarioInFlightActionIsFinishedOnGoalChange(const std::string& tmpDir) {
    Section("S3 in-flight action is finished when the goal changes");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    if (!h.Boot(tmpDir + "/s3", "fencer")) return;
    h.EnterLive();
    h.Steps(5, 1000);
    const life::GoalKind before = h.Goal();

    // An action the departing goal is waiting on. This is the Xerxes shape:
    // an open_container on a serial the server never answers.
    h.client->ClearSentForTest();
    h.client->ActionOpenContainer(0x4000CB1Du);
    Check(h.client->ActionBusy(), "an action is in flight before the switch");
    Check(h.client->ActionKind() == act::Kind::OpenContainer,
          "the in-flight action is the open_container we issued");
    const usize sentBefore = h.client->SentForTest().size();
    Check(sentBefore > 0, "the request reached the wire (captured offline)");

    h.obs.hp = 8;
    h.obs.underAttack = true;
    h.obs.hostilesNear = 2;
    h.obs.attackersOnMe = 1;
    for (int i = 0; i < 20 && h.Goal() == before; ++i) h.Step(1000);
    std::printf("  goal after the emergency: %s, action busy=%d result=%s\n",
                h.GoalName(), h.client->ActionBusy() ? 1 : 0,
                act::ResultName(h.client->ActionResult()));
    Check(h.Goal() != before, "the goal changed");
    Check(!h.client->ActionBusy(),
          "the departing goal's action is no longer in flight");
    Check(h.client->ActionResult() == act::Result::InvalidState ||
              h.client->ActionKind() != act::Kind::OpenContainer,
          "it was finished with invalid_state (or superseded by the new "
          "goal's own action)");
}

// --- S4 -------------------------------------------------------------------
// The shop route for bandages ENDS when every counter this character knows of
// is empty, instead of walking the town again.
//
// Live shape (Hector, 2026-09-05 14:39-14:48): a healer shelf is i_bandage
// {5 20} against the owner's floor of 100, so the pack crosses no threshold
// that stops the errand -- 577 s and zero fights. The early stop at
// Gear.cpp only fired at `nowHeld >= bandageLow`, which those shelves cannot
// reach. Here the drain is injected (there is no vendor in an offline world);
// what is under test is the DECISION that follows it.
void ScenarioDrainedCountersEndTheShopRoute(const std::string& tmpDir) {
    Section("S4 drained counters end the bandage shop route");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    if (!h.Boot(tmpDir + "/s4", "fencer")) return;

    // The town's counters, already bought out. The drain is injected because
    // an offline world has no vendor to buy from; what is under test is the
    // DECISION that follows it, and the live path reaches the same state
    // through a partial purchase in DoReplaceEquipment. Serial values are
    // arbitrary -- nothing in the empty harness world carries a healer's or a
    // vet's trade, so "no undrained counter left" is true once one is dry.
    h.runner.NoteBandageCounterDrainedForTest(0x4000AAAAu, h.nowMs);

    // Exercise the decision directly. Ordinary restocks now hand to the
    // player market or self-supply before a healer; the invariant is that a
    // confirmed exhausted shelf is recorded and the NPC route is not retried.
    life::RunnerHarnessAccess::BandageStandDown(
        h.runner, h.obs, /*askedMs=*/0, /*sellersDeclined=*/true);

    // And the need model was told, so MAKE_BANDAGES is no longer blocked on
    // "there is money to buy them".
    Check(h.runner.State().memory.HasEvent("bandage_counters_empty"),
          "the town being out of bandages was recorded, so the cloth route "
          "can unblock");
}

void ScenarioRecoveryPreservesMedicalJourneys(const std::string& tmpDir) {
    Section("recovery preserves medical journeys across archetypes");
    for (const char* id : {"fencer", "archer", "scribe", "tamer", "fisher", "miner_smith"}) {
        Harness h;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.hp = 12;
        h.obs.healPotions = 0;
        h.obs.gold = 0;
        h.obs.corpseKnown = true;
        if (!h.Boot(tmpDir + "/recovery_" + id, id)) continue;
        Check(life::RunnerHarnessAccess::RecoverJourney(
                  h.runner, *h.client, h.obs, life::RecoveryStep::Recover),
              "continued recovery retains the medical journey");
        Check(!life::RunnerHarnessAccess::RecoverJourney(
                  h.runner, *h.client, h.obs, life::RecoveryStep::TravelToCorpse),
              "becoming wounded interrupts the corpse journey");
    }
}

void ScenarioMedicalSuppliesAndDepositBudget(const std::string& tmpDir) {
    Section("medical purchases require self-healing skill; confirmed banking resets retries");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    if (!h.Boot(tmpDir + "/medical", "tamer")) return;
    prof::Profession veterinaryOnly = *prof::Find("tamer");
    veterinaryOnly.consumables.clear();
    prof::ConsumableNeed bandages;
    bandages.name = "bandage";
    veterinaryOnly.consumables.push_back(bandages);
    h.obs.hp = 40;
    h.obs.healPotions = 0;
    h.obs.skills = {{rules::kVeterinary, 1000}};
    Check(life::RunnerHarnessAccess::Heal(h.runner, *h.client, h.obs, veterinaryOnly)
              == life::HealStep::Rest,
          "Veterinary alone does not start a self-healing bandage shopping loop");
    h.obs.skills.push_back({rules::kHealing, 200});
    Check(life::RunnerHarnessAccess::Heal(h.runner, *h.client, h.obs, veterinaryOnly)
              == life::HealStep::Rest,
          "low Healing respects the medical errand's purchase threshold");
    Check(life::RunnerHarnessAccess::SuccessfulDepositResetsBudget(
              h.runner, *h.client, h.obs),
          "a confirmed deposit clears earlier failures for the next stock batch");
}

void ScenarioAttackerLeavingSightIsNotRecovery(const std::string& tmpDir) {
    Section("out-of-sight attacker does not cause emergency completion loops");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    h.obs.hp = 33;
    h.obs.hpMax = 50;
    h.obs.attackersOnMe = 1;
    h.obs.underAttack = true;
    if (!h.Boot(tmpDir + "/retreat", "mage")) return;
    Check(!life::RunnerHarnessAccess::Survive(h.runner, *h.client, h.obs),
          "survival keeps ownership while the recent attacker is out of sight");
    Check(!life::RunnerHarnessAccess::HealDone(h.runner, *h.client, h.obs),
          "delegating retreat does not report a wounded character healed");
    h.obs.attackersOnMe = 0;
    h.obs.underAttack = false;
    Check(life::RunnerHarnessAccess::Survive(h.runner, *h.client, h.obs),
          "survival ends after the attack observation clears");
}


// --- D13 ------------------------------------------------------------------
// A PATIENT WITH NOTHING TO HEAL WITH BUYS THE MEDICINE THE COUNTER HAS.
//
// Live shape (Odessa, merchant_tinker, 2026-09-06 00:33:25-00:34:22):
// resurrected at 6/50 beside the Britain healer with Heal(bandage 1.00) in
// the needs list. Her catalogue drops bandages in favour of heal potions
// ("so crafter do not buy bandages", project owner 2026-08-30), the healer's
// potion shelf was empty for the third time, and the medical errand had
// nothing left it was allowed to ask for -- so she walked to a provisioner
// at 14% HP. i_bandage is {5 20} on that same healer's list
// (tm_vend.scp:1110) and gold pays from the bank.
void ScenarioAnEmptyPotionShelfIsNotAnEmptyHealer(const std::string& tmpDir) {
    Section("D13 nothing to heal with -- the crafter buys bandages");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    // The live numbers: 6 of 50, well under this life's flee line.
    h.obs.hp = 6;
    h.obs.hpMax = 50;
    h.obs.bandages = 0;
    h.obs.healPotions = 0;      // the shelf was empty
    h.obs.mana = 0;
    if (!h.Boot(tmpDir + "/d13", "miner_smith")) return;
    h.EnterLive();
    Check(!h.runner.ErrandRunningForTest("bandage"),
          "a crafter does not open a bandage errand as upkeep");

    for (int i = 0; i < 10 && !h.runner.ErrandRunningForTest("bandage"); ++i) {
        life::RunnerHarnessAccess::HealDone(h.runner, *h.client, h.obs);
        h.Step(1000);
    }
    std::printf("  heal plan: %s, bandage errand running=%d\n",
                life::HealStepName(
                    life::RunnerHarnessAccess::LastHealPlan(h.runner)),
                h.runner.ErrandRunningForTest("bandage") ? 1 : 0);
    Check(h.runner.ErrandRunningForTest("bandage"),
          "with no bandage, no potion and no heal spell the patient buys the "
          "medicine the counter does stock");
}

// --- D14 ------------------------------------------------------------------
// RESTING IS A THING YOU DO SOMEWHERE SAFE.
//
// Live shape (Odessa, 2026-09-06 00:37:28-00:38:38): plan "rest -- recover
// health here before a shopping trip" at 24% HP on the road with a named
// lizardman that had followed her; hp 12 -> 6 -> dead. The harness has no
// atlas, so CurrentRegion() is null -- which is exactly the "no guard
// protection here" case, and the answer is to leave, not to stand.
void ScenarioAHurtCharacterDoesNotRestBesideAHostile(const std::string& tmpDir) {
    Section("D14 hurt with a hostile in sight -- leave, do not rest");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    h.obs.hp = 12;
    h.obs.hpMax = 50;
    h.obs.bandages = 0;
    h.obs.healPotions = 0;
    h.obs.mana = 0;
    h.obs.gold = 0;             // nothing to buy with: the rest branch
    h.obs.goldOnHand = 0;
    if (!h.Boot(tmpDir + "/d14", "fencer")) return;
    h.EnterLive();

    h.obs.hostilesNear = 0;
    Check(!life::RunnerHarnessAccess::RetreatedInsteadOfResting(
              h.runner, *h.client, h.obs),
          "an empty road is a fine place to sit and regenerate");

    h.obs.hostilesNear = 2;
    Check(life::RunnerHarnessAccess::RetreatedInsteadOfResting(
              h.runner, *h.client, h.obs),
          "with a hostile in sight and no guard protection the wounded "
          "character leaves instead of resting");
}

// --- D10 ------------------------------------------------------------------
// A DRAINED POTION SHELF IS REMEMBERED, LIKE A DRAINED BANDAGE SHELF.
//
// Live shape (Odessa, 2026-09-06): "this 'healer' does not stock heal potion"
// at 00:16:26, then the SAME healer re-targeted from the Brit mine at
// 00:23:35 -- a 363-tile walk inside Sphere's ten-minute restock window --
// and again at 00:33:25.
void ScenarioADrainedPotionShelfIsSkipped(const std::string& tmpDir) {
    Section("D10 a drained heal-potion counter is not walked to again");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    h.obs.healPotions = 0;
    if (!h.Boot(tmpDir + "/d10", "miner_smith")) return;
    h.EnterLive();
    // Injected, because an offline world has no vendor to buy out; the live
    // path reaches this state through a refused or partial potion purchase.
    h.runner.NotePotionCounterDrainedForTest(0x4000BBBBu, h.nowMs);

    for (int i = 0; i < 5; ++i) {
        life::RunnerHarnessAccess::ReplaceEquipment(h.runner, *h.client, h.obs,
                                                    false);
        h.Step(1000);
    }
    std::printf("  potion errand running=%d\n",
                h.runner.ErrandRunningForTest("potion") ? 1 : 0);
    Check(!h.runner.ErrandRunningForTest("potion"),
          "no fresh potion visit was begun -- every counter this life knows "
          "of is empty");
    Check(h.runner.State().memory.HasEvent("potion_counters_empty"),
          "the town being out of heal potions was recorded");
}

// --- D12 ------------------------------------------------------------------
// TWO BANDAGE LINES: THE FLOOR IN TOWN, THE FIELD LINE AT THE GROUND.
//
// Live shape (Hector, 2026-09-06 00:29:47): REPLACE_EQUIPMENT superseded
// SURVIVE one second after a kill for "bandages=99 low=100" -- a walk back to
// Britain for a single bandage. ResolveConsumableThresholds resolves the
// field line to a three-fight reserve (100 - 24 = 76); the harness
// has no atlas, so CurrentRegion() is null and this is the FIELD case.
void ScenarioTheFieldLineIsNotTheTownFloor(const std::string& tmpDir) {
    Section("D12 the hunt gate uses the field line outside town");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    h.obs.healPotions = 5;
    if (!h.Boot(tmpDir + "/d12", "fencer")) return;
    h.EnterLive();
    const life::NeedConfig& cfg = life::RunnerHarnessAccess::Needs(h.runner);
    std::printf("  bandage floor=%d field line=%d\n", cfg.bandageLow,
                cfg.bandageFieldLow);
    Check(cfg.bandageFieldLow > 0 && cfg.bandageFieldLow < cfg.bandageLow,
          "a hunter's field line sits below its town floor");

    h.obs.bandages = cfg.bandageLow - 1;      // 99 of 100: the live number
    const std::string above =
        life::RunnerHarnessAccess::HuntGateReason(h.runner, *h.client, h.obs);
    std::printf("  at %d bandages: \"%s\"\n", h.obs.bandages, above.c_str());
    Check(above.find("bandage floor") == std::string::npos,
          "one bandage under the town floor does not end the hunt");

    h.obs.bandages = cfg.bandageFieldLow - 1;
    const std::string below =
        life::RunnerHarnessAccess::HuntGateReason(h.runner, *h.client, h.obs);
    std::printf("  at %d bandages: \"%s\"\n", h.obs.bandages, below.c_str());
    Check(below.find("bandage floor") != std::string::npos,
          "under the field line the hunt still stops to restock");

    // Once both stock routes have already stood down, the current trip has
    // been deliberately allowed to continue.  Its approval must survive a
    // shop cooldown expiring while the character walks; otherwise the bot
    // walks most of the way to the graveyard, turns around without spending a
    // bandage, and repeats the market loop instead of ever fighting.
    life::RunnerHarnessAccess::AuthorizeUnderstockHuntForTest(h.runner);
    const std::string authorised =
        life::RunnerHarnessAccess::HuntGateReason(h.runner, *h.client, h.obs);
    Check(authorised.find("bandage floor") == std::string::npos,
          "an authorised understock hunt is not pulled back into restocking "
          "mid-trip");
}

// An attack packet can arrive after BANK (or any other ordinary goal) was
// selected but before the planner has made its next pass.  The dispatcher must
// give that packet to survival immediately; otherwise the stale goal starts a
// route through the combat board, which is exactly how Ghalys died in the
// fleet122g30 run.
void ScenarioAttackPreemptsAnAlreadySelectedErrand(const std::string& tmpDir) {
    Section("attack packets interrupt an already-selected errand");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    h.obs.underAttack = true;
    h.obs.attackersOnMe = 1;
    if (!h.Boot(tmpDir + "/attack_preempt", "fencer")) return;
    life::RunnerHarnessAccess::SetActiveGoal(h.runner, life::GoalKind::Bank,
                                             h.obs.nowMs);
    life::RunnerHarnessAccess::Dispatch(h.runner, *h.client, h.obs);
    Check(life::RunnerHarnessAccess::Retreating(h.runner),
          "a live attack takes the tick away from BANK and starts survival retreat");
}

// --- Cause A --------------------------------------------------------------
// A kill made during TRAIN_COMBAT is that goal's progress, and five of them
// in a row are not a spin.
void ScenarioAKillIsCombatTrainingProgress(const std::string& tmpDir) {
    Section("fleet-100 Cause A a confirmed kill is TRAIN_COMBAT progress");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    h.obs.bandages = 200;      // past the hunt's own readiness gates
    if (!h.Boot(tmpDir + "/causeA", "fencer")) return;
    h.EnterLive();

    int gained = 0;
    bool spun = false;
    const bool done = life::RunnerHarnessAccess::CreditedKillEndsTheHunt(
        h.runner, *h.client, h.obs, 1, &gained, &spun);
    std::printf("  one credited kill: done=%d progress+%d spin=%d\n",
                done ? 1 : 0, gained, spun ? 1 : 0);
    Check(done, "a confirmed kill gives the hunting goal a success return");
    Check(gained > 0, "the kill is counted as progress on the goal that ran");
    Check(life::RunnerHarnessAccess::PendingHuntKills(h.runner) == 0,
          "the credit is consumed once, not re-counted every tick");

    // Without a kill the same tick must NOT report success -- the defect
    // this guards against is a goal that completes having done nothing.
    int again = 0;
    bool spunAgain = false;
    const bool freeWin = life::RunnerHarnessAccess::CreditedKillEndsTheHunt(
        h.runner, *h.client, h.obs, 0, &again, &spunAgain);
    std::printf("  no kill pending: done=%d progress+%d\n",
                freeWin ? 1 : 0, again);
    Check(!freeWin, "with nothing killed the hunt does not claim success");

    // Five credited kills in a row: the anti-spin backstop must stay quiet,
    // because every one of them is real progress.
    bool anySpin = false;
    for (int i = 0; i < 5; ++i) {
        int p = 0;
        bool s = false;
        life::RunnerHarnessAccess::CreditedKillEndsTheHunt(h.runner, *h.client,
                                                           h.obs, 1, &p, &s);
        if (s) anySpin = true;
    }
    std::printf("  five credited kills: goal_spinning=%d\n", anySpin ? 1 : 0);
    Check(!anySpin, "five kills in a row do not read as a spinning goal");
}

// --- D-item 1 -------------------------------------------------------------
// A TRIP ALLOWANCE BELONGS TO THE ERRAND. TRAIN_AT_NPC interrupted by BANK
// and picked up again must travel again, not fail on its stale ceiling.
// --- Selene ---------------------------------------------------------------
// PRACTICE_SKILL may only claim success on an observed skill delta.
void ScenarioPracticeSucceedsOnlyOnASkillGain(const std::string& tmpDir) {
    Section("Selene practice succeeds only on an observed skill gain");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    // Below max mana, so Sphere's Skill_Meditation would actually start.
    h.obs.mana = 5;
    h.obs.manaMax = 20;
    h.obs.skills.push_back({rules::kMeditation, 200});
    if (!h.Boot(tmpDir + "/practice", "alchemist")) return;
    h.EnterLive();

    i32 gains = 0;
    const bool flat = life::RunnerHarnessAccess::PracticeBoutClaimsSuccess(
        h.runner, *h.client, h.obs, rules::kMeditation, -1, &gains);
    std::printf("  skill never moved: done=%d gains=%d\n", flat ? 1 : 0, gains);
    Check(!flat, "a bout that gained nothing does not complete the goal");
    Check(gains == 0, "and nothing is reported as gained by practice");

    i32 gains2 = 0;
    const bool won = life::RunnerHarnessAccess::PracticeBoutClaimsSuccess(
        h.runner, *h.client, h.obs, rules::kMeditation, 2, &gains2);
    std::printf("  one tenth gained: done=%d gains=%d\n", won ? 1 : 0, gains2);
    Check(won, "a bout that gained a tenth completes the goal");
    Check(gains2 == 1, "the gain is reported once");

    // Full mana: the server refuses to start the skill at all, so the handler
    // must not spend a single attempt on it.
    h.obs.mana = h.obs.manaMax;
    i32 gains3 = 0;
    const bool capped = life::RunnerHarnessAccess::PracticeBoutClaimsSuccess(
        h.runner, *h.client, h.obs, rules::kMeditation, -1, &gains3);
    std::printf("  full mana: done=%d\n", capped ? 1 : 0);
    Check(!capped, "meditation at full mana is a refusal, not a completion");
}

void ScenarioTrainTripsAreHandedBackOnAGoalChange(const std::string& tmpDir) {
    Section("D1 the trainer trip allowance does not survive a goal change");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    if (!h.Boot(tmpDir + "/trainTrips", "fencer")) return;
    h.EnterLive();

    const i32 ceiling = life::RunnerHarnessAccess::MaxTrainTrips();

    // Kharain's shape: the errand had spent its whole allowance, then
    // BUY_SUPPLIES/BANK superseded it.
    life::RunnerHarnessAccess::SetTrainTrips(h.runner, ceiling);
    life::RunnerHarnessAccess::LeaveGoalForTest(h.runner, *h.client,
                                                life::GoalKind::TrainAtNpc,
                                                life::GoalKind::Bank);
    const i32 afterLeaving = life::RunnerHarnessAccess::TrainTrips(h.runner);
    std::printf("  trips %d before TRAIN_AT_NPC->BANK, %d after\n", ceiling,
                afterLeaving);
    Check(afterLeaving == 0,
          "leaving TRAIN_AT_NPC for another goal hands the trip allowance back");
    Check(afterLeaving < ceiling,
          "a resumed TRAIN_AT_NPC is not already standing on its ceiling");

    // And the other direction: arriving AT the errand from somewhere else
    // starts it clean too.
    life::RunnerHarnessAccess::SetTrainTrips(h.runner, ceiling);
    life::RunnerHarnessAccess::LeaveGoalForTest(h.runner, *h.client,
                                                life::GoalKind::Bank,
                                                life::GoalKind::TrainAtNpc);
    std::printf("  trips after BANK->TRAIN_AT_NPC: %d\n",
                life::RunnerHarnessAccess::TrainTrips(h.runner));
    Check(life::RunnerHarnessAccess::TrainTrips(h.runner) == 0,
          "a fresh pick of TRAIN_AT_NPC starts from a full allowance");

    // The Corran rule still holds: a same-kind re-pick keeps its journey AND
    // its accounting, so a goal re-selected mid-walk cannot buy itself an
    // unbounded number of trips.
    life::RunnerHarnessAccess::SetTrainTrips(h.runner, 2);
    life::RunnerHarnessAccess::LeaveGoalForTest(h.runner, *h.client,
                                                life::GoalKind::TrainAtNpc,
                                                life::GoalKind::TrainAtNpc);
    std::printf("  trips after a same-kind re-pick: %d\n",
                life::RunnerHarnessAccess::TrainTrips(h.runner));
    Check(life::RunnerHarnessAccess::TrainTrips(h.runner) == 2,
          "a same-kind re-pick keeps the trips it has already spent");
}

// --- S16: the death counter must follow the same fact death_location uses --
//
// session_.deaths used to be incremented only inside case Phase::Live's own
// obs.dead edge check, so a death detected while the runner was in a
// DIFFERENT phase never reached it -- Odessa mid Phase::WindDown (heading
// home to log out), 2026-09-07 00:44 gate: Sphere's "P'Odessa' was killed by
// N'Harpy'" and her own death_location event both fired, but session_summary
// still printed deaths=0. TrackDeathEdge (Core.cpp, called from Tick() before
// the phase switch, reading client.IsDead() straight off the client rather
// than the phase-local Observation) is the fix; this proves it against the
// exact packet death_location itself is built from -- 0x2C, the resurrection
// menu -- with obsOverride_'s scripted Observation left alone throughout
// (TrackDeathEdge never reads obs.dead).
void ScenarioDeathIsCountedOnTheResurrectMenuPacket(const std::string& tmpDir) {
    Section("S16 the resurrect-menu packet increments the session death "
            "tally once");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    if (!h.Boot(tmpDir + "/deathCounter", "fencer")) return;
    h.EnterLive();
    // EnterLive() only reaches Phase::Reconcile's OWN entry tick; Reconcile's
    // one-time session_ = SessionSummary{} (Core.cpp, login reconciliation)
    // runs on the NEXT tick, same as every other scenario in this file that
    // pads with an extra Steps() before touching session state. Without this,
    // a death dispatched immediately after EnterLive() lands on the very tick
    // that resets session_ and looks like TrackDeathEdge lost it.
    h.Steps(2, 1000);

    Check(!h.client->IsDead(), "alive at the start, per the client's own state");
    Check(life::RunnerHarnessAccess::SessionDeaths(h.runner) == 0,
          "no deaths counted yet");

    // 0x2C Resurrection Menu (2 bytes: cmd, action). Action 0 is the death
    // prompt Client::OnResurrectionMenu treats as "this packet IS the death
    // notification for our own character" (Client.cpp).
    u8 resurrectMenu[2] = {0x2C, 0x00};
    h.client->DispatchPacketForTest(resurrectMenu, sizeof(resurrectMenu));
    Check(h.client->IsDead(), "the 0x2C set the client's own dead flag");

    h.Step(1000);
    std::printf("  deaths after the first 0x2C: %d\n",
                life::RunnerHarnessAccess::SessionDeaths(h.runner));
    Check(life::RunnerHarnessAccess::SessionDeaths(h.runner) == 1,
          "one death counted on the alive->dead edge, in whatever phase the "
          "runner was in");

    // A RESEND of the same menu (Sphere can repeat it) must not double count:
    // OnResurrectionMenu itself is idempotent (`if (!IsDead())`), so the
    // client's dead flag never re-flips, and TrackDeathEdge's own edge test
    // (`!wasDead_ && dead`) cannot fire twice in a row either.
    h.client->DispatchPacketForTest(resurrectMenu, sizeof(resurrectMenu));
    h.Step(1000);
    std::printf("  deaths after the resend: %d\n",
                life::RunnerHarnessAccess::SessionDeaths(h.runner));
    Check(life::RunnerHarnessAccess::SessionDeaths(h.runner) == 1,
          "the resend does not count a second death");
}

// --- three packets, so a scenario can put something in the pack ------------
//
// The harness world is empty on purpose, but the bandage chain's first
// gesture needs a real item in a real backpack. These are the shapes
// Client.cpp already parses (the same three tests/trade_verify.cpp builds),
// dispatched through DispatchPacketForTest so the container cache is filled
// by the client's own code and not by a test-only setter.
void StoreBE16(u8* p, u16 v) { p[0] = u8(v >> 8); p[1] = u8(v); }
void StoreBE32(u8* p, u32 v) {
    p[0] = u8(v >> 24); p[1] = u8(v >> 16); p[2] = u8(v >> 8); p[3] = u8(v);
}
// 0x1B LOGIN_CONFIRM: serial(4) .. body@9(2) x@11(2) y@13(2).
std::vector<u8> MakeLoginConfirm(u32 serial, u16 x, u16 y) {
    std::vector<u8> p(37, 0);
    p[0] = 0x1B;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[9], 0x0190);
    StoreBE16(&p[11], x);
    StoreBE16(&p[13], y);
    return p;
}
// 0x2E EQUIP_ITEM: serial(4) graphic(2) pad(1) layer(1) mobile(4) hue(2).
std::vector<u8> MakeEquip(u32 item, u16 graphic, u8 layer, u32 mobile) {
    std::vector<u8> p(15, 0);
    p[0] = 0x2E;
    StoreBE32(&p[1], item);
    StoreBE16(&p[5], graphic);
    p[8] = layer;
    StoreBE32(&p[9], mobile);
    return p;
}
// 0x25 ADD_ITEM_TO_CONTAINER: serial(4) graphic(2) pad(1) amount(2) x(2)
// y(2) container(4) hue(2).
std::vector<u8> MakeAddItem(u32 serial, u16 graphic, u16 amount, u32 container) {
    std::vector<u8> p(20, 0);
    p[0] = 0x25;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[5], graphic);
    StoreBE16(&p[8], amount);
    StoreBE32(&p[14], container);
    return p;
}

}  // namespace


// --- S5 -------------------------------------------------------------------
// THE BANDAGE CHAIN BUYS BEFORE IT WALKS.
//
// Two live traces, one family (2026-09-06/07):
//
//   Ravan (pk, 10,000 gp) emptied the weaver's four LOOSE-CLOTH rows, heard
//   "this weaver does not stock loose cloth", and set off for the Yew
//   pasture -- 23 legs, ~960 tiles, through the Yew moongate -- while three
//   rows of BOLTS (5/23/18 at 173 gp, fifty cloth each) sat on the shelf he
//   was standing at. The session ended with him in the wild at 634,849.
//   (artifacts/extra22_smoke_20260906/Ravan.console.txt:846-857.)
//
//   Hector (fencer, 5,550 gp) came back from a death with 0 bandages, found
//   every healer counter in Britain empty, and spent the session ringing
//   HEAL -> REPLACE_EQUIPMENT -> GET_FOOD without ever reaching the cloth
//   route. (run_gates/g_Hector.console.txt:95-421.)
//
// The current owner rule is player request, then self-supply, then an NPC
// fallback. This scenario begins after the player request failed, so it must
// try a nearby flock before spending at a tailor.
//
// The flock table is the shard's own (data/revolution_pastures.tsv) and the
// radius is kMaxPastureTilesFromHome: the Britain farmland flock at
// 1321,1817 is ~130 tiles from the Britain bank, the next rows are Yew at
// ~750 and Jhelom at ~1900.
void ScenarioBandagesSelfSupplyBeforeNpcFallback(const std::string& tmpDir,
                                                 const char* dataDir) {
    Section("S5 the bandage chain shears before it buys NPC cloth");

    // A pair of scissors in the pack, because without them the chain has no
    // first gesture and the goal goes shopping for shears instead. Three
    // packets, the same ones the server sends: who I am, what I am wearing,
    // what is inside it.
    auto arm = [](Client& c) {
        const u32 me = 0x0000ED04, pack = 0x4000ED02;
        auto login = MakeLoginConfirm(me, 1421, 1690);
        c.DispatchPacketForTest(login.data(), login.size());
        auto worn = MakeEquip(pack, 0x0E75, 0x15, me);      // layer 21
        c.DispatchPacketForTest(worn.data(), worn.size());
        auto shears = MakeAddItem(0x4001A7D0, 0x0F9E, 1, pack);
        c.DispatchPacketForTest(shears.data(), shears.size());
        return c.FindBackpackItemByGraphic(0x0F9E) != 0;
    };

    // (a) EVERY COUNTER EMPTY, PURSE FULL -> the tailor's shelf, not a flock.
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.bandages = 0;
        h.obs.gold = 5550;
        h.obs.goldOnHand = 5550;
        if (!h.Boot(tmpDir + "/s5a", "fencer")) return;
        Check(arm(*h.client), "the fencer is carrying scissors");
        h.runner.NoteBandageCounterDrainedForTest(0x4000AAAAu, h.nowMs);
        h.obs.nowMs = h.nowMs;
        life::RunnerHarnessAccess::MakeBandages(h.runner, *h.client, h.obs);
        std::printf("  bolt errand=%d cloth errand=%d flock trips=%d\n",
                    h.runner.ErrandRunningForTest("bandageBolt") ? 1 : 0,
                    h.runner.ErrandRunningForTest("bandageCloth") ? 1 : 0,
                    life::RunnerHarnessAccess::BandageFlockTrips(h.runner));
        Check(!h.runner.ErrandRunningForTest("bandageBolt") &&
                  !h.runner.ErrandRunningForTest("bandageCloth"),
              "a funded fighter does not buy NPC cloth before trying its flock");
        Check(life::RunnerHarnessAccess::BandageFlockTrips(h.runner) == 1,
              "the first self-supply step is the nearby flock");
        auto bolts = MakeAddItem(0x4001A7E1, 0x0F95, 4, 0x4000ED02);
        h.client->DispatchPacketForTest(bolts.data(), bolts.size());
        h.obs.nowMs += 3000;
        h.client->SetClockForTest(h.obs.nowMs);
        life::RunnerHarnessAccess::MakeBandages(h.runner, *h.client, h.obs);
        Check(!h.runner.ErrandRunningForTest("bandageBolt"),
              "delivered bolts settle the active purchase even with zero shortfall");
        Check(life::RunnerHarnessAccess::BandageFlockTrips(h.runner) == 1,
              "delivered bandage inputs do not add another pasture trip");
    }

    // (b) A SMALL TOP-UP is loose cloth, not a fifty-stone bolt.
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.gold = 5550;
        h.obs.goldOnHand = 5550;
        if (!h.Boot(tmpDir + "/s5b", "fencer")) return;
        Check(arm(*h.client), "the fencer is carrying scissors");
        // FIVE SHORT OF THIS LIFE'S OWN FLOOR, not five short of a constant:
        // bandageFull is resolved per character and per purse
        // (ResolveConsumableThresholds), and for a fencer it sits well above
        // the fifty cloth a bolt yields.
        const i32 want = life::RunnerHarnessAccess::Needs(h.runner).bandageFull;
        h.obs.bandages = (want > 0 ? want : 60) - 5;
        h.obs.nowMs = h.nowMs;
        life::RunnerHarnessAccess::MakeBandages(h.runner, *h.client, h.obs);
        Check(!h.runner.ErrandRunningForTest("bandageCloth"),
              "a five-bandage shortfall still tries self-supply before NPC cloth");
        Check(!h.runner.ErrandRunningForTest("bandageBolt"),
              "no bolt for a top-up");
    }

    // (c) NOTHING TO BUY WITH AND THE ONLY FLOCKS ARE SOMEBODY ELSE'S ->
    //     stand down with a reason. This is the Hector ring: with no shop
    //     and no flock the goal must take itself out of the running instead
    //     of completing having done nothing.
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.bandages = 0;
        h.obs.gold = 2;                  // below the price of one cloth
        h.obs.goldOnHand = 2;
        h.obs.x = 2500;                  // Minoc: no flock within 400 tiles
        h.obs.y = 560;
        if (!h.Boot(tmpDir + "/s5c", "fencer")) return;
        Check(arm(*h.client), "the fencer is carrying scissors");
        h.obs.nowMs = h.nowMs;
        life::RunnerHarnessAccess::MakeBandages(h.runner, *h.client, h.obs);
        std::printf("  flock trips=%d cooling=%d\n",
                    life::RunnerHarnessAccess::BandageFlockTrips(h.runner),
                    h.runner.GetPlanner().Cooling(life::GoalKind::MakeBandages,
                                                  h.nowMs) ? 1 : 0);
        Check(life::RunnerHarnessAccess::BandageFlockTrips(h.runner) == 0,
              "no trip is begun to a flock that belongs to another city");
        Check(h.runner.GetPlanner().Cooling(life::GoalKind::MakeBandages,
                                            h.nowMs),
              "the goal stood down on a cooldown instead of ringing");
    }

    // (d) POOR, BUT THE FLOCK IS NEXT DOOR -> shear. The free step is still
    //     the right answer when it is a short walk and the purse is empty.
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.bandages = 0;
        h.obs.gold = 2;
        h.obs.goldOnHand = 2;
        h.obs.x = 1321;                  // standing in the Britain farmland
        h.obs.y = 1817;
        if (!h.Boot(tmpDir + "/s5d", "fencer")) return;
        Check(arm(*h.client), "the fencer is carrying scissors");
        h.obs.nowMs = h.nowMs;
        life::RunnerHarnessAccess::MakeBandages(h.runner, *h.client, h.obs);
        std::printf("  flock trips=%d cooling=%d\n",
                    life::RunnerHarnessAccess::BandageFlockTrips(h.runner),
                    h.runner.GetPlanner().Cooling(life::GoalKind::MakeBandages,
                                                  h.nowMs) ? 1 : 0);
        Check(life::RunnerHarnessAccess::BandageFlockTrips(h.runner) == 1,
              "a poor fighter beside the home farmland sets off to shear");
        Check(!h.runner.GetPlanner().Cooling(life::GoalKind::MakeBandages,
                                             h.nowMs),
              "and does not stand down while a flock is in reach");
    }
}

// --- S6 -------------------------------------------------------------------
// BANDAGES TRADE PLAYER-FIRST.
//
// The shard's own numbers make this structural rather than a nicety: filling
// 84 fighters to the owner's floor of a hundred needs ~420 twenty-at-a-time
// counter purchases, against seventeen healers and two vets restocking a few
// dozen between them every ten minutes (docs/BANDAGE_SUPPLY_SPEC.md section
// 1). The town cannot supply the fleet, and the only other source on the shard
// is a pair of scissors -- which needs NO Tailoring skill at all
// (type_scissors.scp:8-49 hands t_cloth to Source-X's hardcoded cut).
//
// Before this slice the drained-counter stand-down went straight to
// MAKE_BANDAGES, so no character ever said "WTB i_bandage" and the tailor who
// could have cut them had no customer. Owner rule: materials trade
// player-to-player by default.
void ScenarioBandagesTradePlayerFirst(const std::string& tmpDir,
                                      const char* dataDir) {
    Section("S6 bandages trade player-first: fighters ask, tailors cut");

    // 0x25 into the pack, the same packets S5 uses.
    auto arm = [](Client& c, u16 graphic, u16 amount, u32 serial) {
        auto add = MakeAddItem(serial, graphic, amount, 0x4000ED02);
        c.DispatchPacketForTest(add.data(), add.size());
    };
    auto boot = [](Client& c) {
        const u32 me = 0x0000ED04, pack = 0x4000ED02;
        auto login = MakeLoginConfirm(me, 1421, 1690);
        c.DispatchPacketForTest(login.data(), login.size());
        auto worn = MakeEquip(pack, 0x0E75, 0x15, me);      // layer 21
        c.DispatchPacketForTest(worn.data(), worn.size());
    };

    // (a) COUNTERS DRY, PURSE FULL -> ask a player before cutting cloth.
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.bandages = 0;
        h.obs.gold = 5550;
        h.obs.goldOnHand = 5550;
        h.obs.nowMs = h.nowMs;
        h.obs.marketQuiet = false;   // the baseline mutes the market; this
                             // case is about reaching it
        if (!h.Boot(tmpDir + "/s6a", "fencer")) return;
        const auto r = life::RunnerHarnessAccess::BandageStandDown(
            h.runner, h.obs, /*askedMs=*/0, /*sellersDeclined=*/false);
        std::printf("  route=%s why=\"%s\"\n",
                    r.askPlayers ? "ASK_PLAYERS" : "CUT_CLOTH", r.why.c_str());
        Check(r.askPlayers,
              "a fighter with money asks the player market for bandages "
              "before it reaches for the scissors");
    }

    // (b) ASKED AND NOBODY ANSWERED -> the self-cut chain, unchanged.
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.bandages = 0;
        h.obs.gold = 5550;
        h.obs.goldOnHand = 5550;
        h.obs.nowMs = h.nowMs;
        if (!h.Boot(tmpDir + "/s6b", "fencer")) return;
        const auto r = life::RunnerHarnessAccess::BandageStandDown(
            h.runner, h.obs, /*askedMs=*/0, /*sellersDeclined=*/true);
        std::printf("  route=%s why=\"%s\"\n",
                    r.askPlayers ? "ASK_PLAYERS" : "CUT_CLOTH", r.why.c_str());
        Check(!r.askPlayers,
              "a `no_player_seller` for i_bandage falls the fighter through "
              "to the scissors");
        Check(h.runner.GetPlanner().Cooling(h.Goal(), h.nowMs),
              "and the exit carries a planner cooldown, so the goal cannot "
              "be re-picked on the next tick");
    }

    // (c) THE WTB WINDOW RUNS OUT ON ITS OWN. A hand-off is advice: if
    //     TRADE_WITH_PLAYER never out-scores the field, nothing else would
    //     ever release the fighter, so the bandage route keeps its own
    //     48-second clock (kMaxAnnounces x kAnnounceIntervalMs).
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.bandages = 0;
        h.obs.gold = 5550;
        h.obs.goldOnHand = 5550;
        h.obs.nowMs = h.nowMs;
        h.obs.marketQuiet = false;   // the baseline mutes the market; this
                             // case is about reaching it
        if (!h.Boot(tmpDir + "/s6c", "fencer")) return;
        const auto still = life::RunnerHarnessAccess::BandageStandDown(
            h.runner, h.obs, /*askedMs=*/h.nowMs - 20000, false);
        Check(still.askPlayers, "20s into the window it is still waiting");
        const auto out = life::RunnerHarnessAccess::BandageStandDown(
            h.runner, h.obs, /*askedMs=*/h.nowMs - 60000, false);
        std::printf("  after 60s route=%s why=\"%s\"\n",
                    out.askPlayers ? "ASK_PLAYERS" : "CUT_CLOTH",
                    out.why.c_str());
        Check(!out.askPlayers,
              "60s later the window is spent and the scissors come out");
    }

    // (d) AND IF IT COULD NEVER ASK. A purse that cannot pay for one bandage
    //     at the price this life would accept must not wait for a seller --
    //     "every waiting gate needs an 'and if I can never ask' branch".
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.bandages = 0;
        h.obs.gold = 0;
        h.obs.goldOnHand = 0;
        h.obs.nowMs = h.nowMs;
        h.obs.marketQuiet = false;   // the baseline mutes the market; this
                             // case is about reaching it
        if (!h.Boot(tmpDir + "/s6d", "fencer")) return;
        const auto r = life::RunnerHarnessAccess::BandageStandDown(
            h.runner, h.obs, 0, false);
        std::printf("  broke route=%s why=\"%s\"\n",
                    r.askPlayers ? "ASK_PLAYERS" : "CUT_CLOTH", r.why.c_str());
        Check(!r.askPlayers,
              "a fighter who cannot pay a player cuts cloth instead of "
              "standing at a bank waiting for one");
        Check(r.why.find("pay") != std::string::npos,
              "and the reason is the purse, not the market being quiet");
    }

    // (e) THE TAILOR'S SIDE: spare cloth becomes stock, and the bench keeps
    //     its own. Bounded at both ends -- the sale target above, the batch's
    //     cloth below.
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.gold = 500;
        h.obs.goldOnHand = 500;
        h.obs.nowMs = h.nowMs;
        if (!h.Boot(tmpDir + "/s6e", "tailor")) return;
        boot(*h.client);
        arm(*h.client, 0x0F9E, 1, 0x4001A7D0);       // scissors
        arm(*h.client, 0x175D, 40, 0x4001A7D1);      // 40 loose cloth

        const i32 target = life::RunnerHarnessAccess::BandageSaleTarget(h.runner);
        const i32 keep = life::RunnerHarnessAccess::Needs(h.runner).craftBatch;
        std::printf("  tailor sale target=%d bench keep=%d\n", target, keep);
        Check(target > 0, "a tailor has a bandage stock target at all");

        Check(life::RunnerHarnessAccess::CutClothForSale(h.runner, *h.client,
                                                         h.obs, 40),
              "spare cloth above the bench's own batch is cut for sale");
        Check(!life::RunnerHarnessAccess::CutClothForSale(h.runner, *h.client,
                                                          h.obs, keep),
              "and the batch's own cloth is never touched");

        // Stock the target, and the step stands down rather than cutting the
        // rest of the shelf into bandages nobody asked for.
        arm(*h.client, 0x0E21, static_cast<u16>(target), 0x4001A7D2);
        Check(!life::RunnerHarnessAccess::CutClothForSale(h.runner, *h.client,
                                                          h.obs, 40),
              "cutting stops at the sale target -- it is bounded");

        // DEMAND MOVES THE TARGET, and it is this character's own observed
        // demand: one completed bandage trade, one more batch worth cutting.
        const i32 before = life::RunnerHarnessAccess::BandageSaleTarget(h.runner);
        life::RunnerHarnessAccess::NoteBandageSale(h.runner, h.obs);
        const i32 after = life::RunnerHarnessAccess::BandageSaleTarget(h.runner);
        std::printf("  target before a sale=%d after=%d\n", before, after);
        Check(after > before,
              "a completed bandage sale raises this tailor's stock target");
    }

    // (f) A FIGHTER RUNNING THE SAME CHAIN FOR WOOL INCOME DOES NOT CUT. Its
    //     cloth is the tailor's material and is already spoken for.
    {
        Harness h;
        h.atlasPathForDataDir = dataDir;
        h.obs = BaselineFencer(h.nowMs);
        h.obs.nowMs = h.nowMs;
        if (!h.Boot(tmpDir + "/s6f", "fencer")) return;
        boot(*h.client);
        arm(*h.client, 0x0F9E, 1, 0x4001A7D0);       // scissors
        arm(*h.client, 0x175D, 40, 0x4001A7D1);      // 40 loose cloth
        Check(!life::RunnerHarnessAccess::CutClothForSale(h.runner, *h.client,
                                                          h.obs, 40),
              "a fighter's wool-income cloth is not cut into bandages -- it "
              "is what the tailor buys");
    }
}

void ScenarioNpcSaleCreditIsConsumed(const std::string& tmpDir) {
    Section("completed NPC sale gives production a turn and cannot be credited twice");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    if (!h.Boot(tmpDir + "/sale_credit", "miner_smith")) return;
    life::RunnerHarnessAccess::SeedFinalNpcSale(h.runner, h.obs);
    Check(life::RunnerHarnessAccess::EarnGold(h.runner, *h.client, h.obs),
          "item loss and gold gain finish the last sale in a visit");
    Check(h.runner.GetPlanner().Cooling(life::GoalKind::EarnGold, h.obs.nowMs),
          "a finished counter visit lets mining and crafting run before selling again");
    h.obs.nowMs += 180001;
    Check(!life::RunnerHarnessAccess::EarnGold(h.runner, *h.client, h.obs),
          "after the cooldown, an empty pack cannot reuse the previous sale's credit");

    Harness live;
    live.obs = BaselineFencer(live.nowMs);
    if (!live.Boot(tmpDir + "/sale_before_stock", "miner_smith")) return;
    live.EnterLive();
    live.Steps(2, 1000);
    life::RunnerHarnessAccess::SeedLiveFinalSale(live.runner, live.obs);
    live.Step();
    Check(life::RunnerHarnessAccess::SaleRecorded(live.runner),
          "a completed sale is credited before the new stock phase can replace its goal");
    const i32 spent = life::RunnerHarnessAccess::PurchasesRecorded(live.runner);
    life::RunnerHarnessAccess::SeedLivePurchase(live.runner, live.obs);
    live.Step();
    Check(life::RunnerHarnessAccess::PurchasesRecorded(live.runner) == spent + 12,
          "the purchase that funds a new crafting phase is recorded before switching work");
}

void ScenarioLastOreStillCreditsSmelting(const std::string& tmpDir) {
    Section("last ore stack credits its ingots before reporting completion");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    h.obs.pack.push_back({"i_ingot_iron", 17});
    if (!h.Boot(tmpDir + "/last_smelt", "miner_smith")) return;
    life::RunnerHarnessAccess::SeedSmelt(h.runner, h.obs);
    const int before = h.runner.GetPlanner().Current().progress;
    life::RunnerHarnessAccess::SettleSmeltBeforePlanning(h.runner, h.obs);
    Check(h.runner.GetPlanner().Current().progress == before + 1,
          "production is credited before another need can supersede smelting");
    Check(life::RunnerHarnessAccess::Smelt(h.runner, *h.client, h.obs),
          "a consumed last ore stack ends smelting");
    Check(h.runner.GetPlanner().Current().progress == before + 1,
          "ingot gain from the final stack counts as production progress");
    Check(life::RunnerHarnessAccess::HasFirstSmelt(h.runner),
          "the first-smelt milestone is recorded even with no ore left");
    life::RunnerHarnessAccess::Smelt(h.runner, *h.client, h.obs);
    Check(h.runner.GetPlanner().Current().progress == before + 1,
          "unchanged ingots cannot credit the same smelt twice");
}

int main(int argc, char** argv) {
    const std::string tmpDir = (argc > 1) ? argv[1] : ".";
    // Where Client::DataDir() resolves to for the scenarios that read a
    // shard data table. Named at CONFIGURE time by tests/CMakeLists.txt, not
    // guessed from the working directory: ctest runs from the build tree.
    static const char* kDataDir = LIFE_HARNESS_ATLAS_PATH;
    std::printf("life_harness: deterministic offline life harness\n");

    ScenarioNpcSaleCreditIsConsumed(tmpDir);
    {
        Harness h;
        h.obs = BaselineFencer(h.nowMs);
        if (h.Boot(tmpDir + "/cloth_restarted_load", "tailor")) {
            auto login = MakeLoginConfirm(0xED04, 1317, 1819);
            h.client->DispatchPacketForTest(login.data(), login.size());
            auto pack = MakeEquip(0x4000ED02, 0x0E75, 0x15, 0xED04);
            h.client->DispatchPacketForTest(pack.data(), pack.size());
            auto wool = MakeAddItem(0x4001A7D1, 0x0DF8, 11, 0x4000ED02);
            h.client->DispatchPacketForTest(wool.data(), wool.size());
            auto blade = MakeAddItem(0x4001A7D2, 0x0F51, 1, 0x4000ED02);
            h.client->DispatchPacketForTest(blade.data(), blade.size());
            Check(!life::RunnerHarnessAccess::ClothLoadAfterRestart(
                      h.runner, *h.client, h.obs),
                  "a restarted wool load allows a bounded wait for nearby sheep");
            h.obs.nowMs += 61000;
            h.client->SetClockForTest(h.obs.nowMs);
            Check(life::RunnerHarnessAccess::ClothLoadAfterRestart(
                      h.runner, *h.client, h.obs),
                  "an empty flock sends carried wool to the wheel after the wait");
        }
    }
    {
        Harness h;
        h.obs = BaselineFencer(h.nowMs);
        if (h.Boot(tmpDir + "/cloth_completed_journey", "tailor")) {
            Check(!h.client->TravelBusy(), "the sheep approach has ended");
            Check(!life::RunnerHarnessAccess::ClothAfterCompletedJourney(
                      h.runner, *h.client, h.obs),
                  "cloth work releases a completed journey before its next errand");
        }
    }
    ScenarioLastOreStillCreditsSmelting(tmpDir);
    ScenarioErrandDoesNotSurviveGoalChange(tmpDir);
    ScenarioSameKindRepickKeepsItsJourney(tmpDir);
    ScenarioInFlightActionIsFinishedOnGoalChange(tmpDir);
    ScenarioDrainedCountersEndTheShopRoute(tmpDir);
    ScenarioRecoveryPreservesMedicalJourneys(tmpDir);
    ScenarioMedicalSuppliesAndDepositBudget(tmpDir);
    ScenarioAttackerLeavingSightIsNotRecovery(tmpDir);
    ScenarioAttackPreemptsAnAlreadySelectedErrand(tmpDir);
    ScenarioAnEmptyPotionShelfIsNotAnEmptyHealer(tmpDir);
    ScenarioAHurtCharacterDoesNotRestBesideAHostile(tmpDir);
    ScenarioADrainedPotionShelfIsSkipped(tmpDir);
    ScenarioTheFieldLineIsNotTheTownFloor(tmpDir);
    ScenarioAKillIsCombatTrainingProgress(tmpDir);
    ScenarioPracticeSucceedsOnlyOnASkillGain(tmpDir);
    ScenarioTrainTripsAreHandedBackOnAGoalChange(tmpDir);
    ScenarioDeathIsCountedOnTheResurrectMenuPacket(tmpDir);
    ScenarioBandagesSelfSupplyBeforeNpcFallback(tmpDir, kDataDir);
    ScenarioBandagesTradePlayerFirst(tmpDir, kDataDir);

    std::printf("%s: %d checks, %d failures\n",
                g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
