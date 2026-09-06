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

using namespace uo;

namespace uo::life {
struct RunnerHarnessAccess {
    static bool Survive(Runner& runner, Client& client, const Observation& obs) {
        return runner.DoSurvive(client, obs);
    }
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

    bool Boot(const std::string& dataRoot, const char* professionId) {
        Client::Config cfg{};
        cfg.loginHost = "127.0.0.1";
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
// An errand does not survive a genuine goal change.
void ScenarioErrandDoesNotSurviveGoalChange(const std::string& tmpDir) {
    Section("S1 errand does not survive a genuine goal change");
    Harness h;
    h.obs = BaselineFencer(h.nowMs);
    if (!h.Boot(tmpDir + "/s1", "fencer")) return;
    h.EnterLive();

    // Let the bandage errand start. No mobiles are cached (the Client has
    // never seen a 0x78), so the VendorErrand runs its find/scan legs -- the
    // shape that is mid-errand when something interrupts it.
    for (int i = 0; i < 40 && !h.runner.ErrandRunningForTest("bandage"); ++i)
        h.Step(1000);
    const bool started = h.runner.ErrandRunningForTest("bandage");
    std::printf("  goal after startup: %s, bandage errand running=%d\n",
                h.GoalName(), started ? 1 : 0);
    Check(started, "the bandage errand is running before the interruption");
    if (!started) return;
    const life::GoalKind before = h.Goal();

    // The interruption: hp critical, hostiles on us. StayAlive scores at or
    // above the preempt floor, so it takes the goal away mid-errand.
    h.obs.hp = 8;
    h.obs.underAttack = true;
    h.obs.hostilesNear = 2;
    h.obs.attackersOnMe = 1;
    for (int i = 0; i < 20 && h.Goal() == before; ++i) h.Step(1000);
    std::printf("  goal after the emergency: %s\n", h.GoalName());
    Check(h.Goal() != before, "a higher-priority goal preempted the errand");
    Check(!h.runner.ErrandRunningForTest("bandage"),
          "the bandage errand was cancelled, not left mid-Verify");

    // ... and when the emergency passes it starts a FRESH visit rather than
    // resuming the interrupted one.
    h.obs.hp = 60;
    h.obs.underAttack = false;
    h.obs.hostilesNear = 0;
    h.obs.attackersOnMe = 0;
    bool restarted = false;
    for (int i = 0; i < 60 && !restarted; ++i) {
        h.Step(1000);
        restarted = h.runner.ErrandRunningForTest("bandage");
    }
    std::printf("  goal after recovery: %s, bandage errand running=%d\n",
                h.GoalName(), restarted ? 1 : 0);
    Check(restarted, "the errand is begun afresh once the emergency is over");
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

    // A SELF-SUPERSESSION IS NOT A GOAL CHANGE. "goal_changed=X from=X" is the
    // planner clearing an exhausted goal and picking the identical kind again;
    // Corran lost a whole errand to a reset on that path (see the comment in
    // Runner::Tick). What is in flight when it happens must still be in flight
    // afterwards -- so an action issued just before the re-pick is the probe.
    // No Client::Tick runs in the harness, so nothing expires it behind our
    // back: only the goal machinery can end it.
    bool sawSelfSupersession = false;
    bool actionSurvived = false;
    life::GoalKind kind = h.Goal();
    i64 startedAt = h.runner.GetPlanner().Current().startedAtMs;
    for (int i = 0; i < 600 && !sawSelfSupersession; ++i) {
        if (!h.client->ActionBusy())
            h.client->ActionOpenContainer(0x40001234u);
        const bool busyBefore = h.client->ActionBusy();
        const life::GoalKind kindBefore = h.Goal();
        h.Step(1000);
        const life::GoalState& g = h.runner.GetPlanner().Current();
        const bool repick = (g.kind == kindBefore && g.startedAtMs != startedAt);
        startedAt = g.startedAtMs;
        kind = g.kind;
        if (repick && busyBefore) {
            sawSelfSupersession = true;
            actionSurvived = h.client->ActionBusy();
        }
    }
    std::printf("  self-supersession of %s observed=%d, in-flight action "
                "survived=%d\n", life::GoalKindName(kind),
                sawSelfSupersession ? 1 : 0, actionSurvived ? 1 : 0);
    Check(sawSelfSupersession,
          "a same-kind re-pick (goal_changed=X from=X) happened with an "
          "action in flight");
    Check(actionSurvived,
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

    // Without the rule this fencer opens a bandage errand on its first Live
    // tick and walks the town (S1 shows exactly that shape).
    bool errandStarted = false;
    h.EnterLive();
    for (int i = 0; i < 20; ++i) {
        h.Step(1000);
        if (h.runner.ErrandRunningForTest("bandage")) errandStarted = true;
    }
    std::printf("  goal: %s, a bandage visit was begun=%d\n", h.GoalName(),
                errandStarted ? 1 : 0);
    Check(!errandStarted,
          "no fresh visit was begun -- every counter this life knows of is "
          "empty");

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
// field line to the floor less one fight's worth (100 - 8 = 92); the harness
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

}  // namespace

int main(int argc, char** argv) {
    const std::string tmpDir = (argc > 1) ? argv[1] : ".";
    std::printf("life_harness: deterministic offline life harness\n");

    ScenarioErrandDoesNotSurviveGoalChange(tmpDir);
    ScenarioSameKindRepickKeepsItsJourney(tmpDir);
    ScenarioInFlightActionIsFinishedOnGoalChange(tmpDir);
    ScenarioDrainedCountersEndTheShopRoute(tmpDir);
    ScenarioRecoveryPreservesMedicalJourneys(tmpDir);
    ScenarioMedicalSuppliesAndDepositBudget(tmpDir);
    ScenarioAttackerLeavingSightIsNotRecovery(tmpDir);
    ScenarioAnEmptyPotionShelfIsNotAnEmptyHealer(tmpDir);
    ScenarioAHurtCharacterDoesNotRestBesideAHostile(tmpDir);
    ScenarioADrainedPotionShelfIsSkipped(tmpDir);
    ScenarioTheFieldLineIsNotTheTownFloor(tmpDir);
    ScenarioAKillIsCombatTrainingProgress(tmpDir);
    ScenarioPracticeSucceedsOnlyOnASkillGain(tmpDir);
    ScenarioTrainTripsAreHandedBackOnAGoalChange(tmpDir);

    std::printf("%s: %d checks, %d failures\n",
                g_failures ? "FAILED" : "PASSED", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
