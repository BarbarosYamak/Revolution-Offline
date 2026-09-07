// M4 Slice 1: the persistent-life layer, tested without a server.
//
//   * the JSON subset the state file is written in -- round trip, escapes,
//     and a parse failure that reports WHERE
//   * build-plan legality: the 700-point budget, the 225/100 stat rule, the
//     inactive-skill refusal, and the creation split that killed six characters
//   * memory: proximity dedupe, exponential danger decay, bounded histories
//   * the need model: what it says and, more importantly, WHY
//   * the utility planner: hard filters, additive scoring, commitment,
//     hysteresis and bounded failure
//   * persistence: schema round trip, forward-version refusal, atomic save
//   * login reconciliation: the server wins, and an older save never rolls
//     server progression back
//
// No server, no MULs, no world data. The store test writes into a temp dir
// passed by CTest.

#include "uo/activities/recovery.h"
#include "uo/json.h"
#include "uo/life.h"
#include "uo/market.h"
#include "uo/needgate.h"
#include "uo/production.h"
#include "uo/professions.h"
#include "uo/rules.h"
#include "uo/spellcast.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

using namespace uo;

// --------------------------------------------------------------------------
void TestJsonRoundTrip() {
    Section("json: round trip and errors");

    json::Value root = json::Value::MakeObject();
    root.Set("schema_version", static_cast<i64>(1));
    root.Set("name", std::string("Bal\"tazar\\ \n\t"));
    root.Set("ratio", 0.5);
    root.Set("flag", true);
    root.Set("nothing", json::Value());
    json::Value arr = json::Value::MakeArray();
    arr.Push(static_cast<i64>(1));
    arr.Push(static_cast<i64>(-2));
    json::Value inner = json::Value::MakeObject();
    inner.Set("deep", std::string("yes"));
    arr.Push(std::move(inner));
    root.Set("list", std::move(arr));

    const std::string text = root.Serialize(2);
    json::ParseError err;
    const json::Value back = json::Parse(text, &err);
    Check(!err.failed, "a document we wrote parses back");
    Check(back["schema_version"].AsInt(0) == 1, "integer survives the trip");
    Check(back["name"].AsString() == "Bal\"tazar\\ \n\t", "escapes survive the trip");
    Check(back["ratio"].AsDouble(0.0) == 0.5, "fraction survives the trip");
    Check(back["flag"].AsBool(false), "bool survives the trip");
    Check(back["nothing"].isNull(), "null survives the trip");
    Check(back["list"].Size() == 3, "array length survives the trip");
    Check(back["list"].At(1).AsInt(0) == -2, "negative number survives the trip");
    Check(back["list"].At(2)["deep"].AsString() == "yes", "nested object survives");

    // A big millisecond stamp must not drift through a double.
    json::Value stamp = json::Value::MakeObject();
    stamp.Set("at", static_cast<i64>(1772150400123LL));
    const json::Value stampBack = json::Parse(stamp.Serialize(0), nullptr);
    Check(stampBack["at"].AsInt(0) == 1772150400123LL,
          "a millisecond timestamp round-trips exactly");

    // Missing and wrong-typed fields fall back rather than guessing.
    Check(back["absent"].AsInt(42) == 42, "absent field returns the caller's default");
    Check(back["name"].AsInt(7) == 7, "wrong-typed field returns the caller's default");
    Check(back["absent"]["deeper"].AsString("d") == "d", "chained lookup on null is safe");

    // \u escapes, including a surrogate pair.
    const json::Value uni = json::Parse("{\"s\":\"\\u00e7\\ud83d\\ude00\"}", &err);
    Check(!err.failed, "unicode escapes parse");
    Check(uni["s"].AsString() == "\xc3\xa7\xf0\x9f\x98\x80",
          "BMP escape and surrogate pair both decode to UTF-8");

    json::ParseError bad;
    json::Parse("{\"a\": }", &bad);
    Check(bad.failed, "malformed JSON fails");
    Check(!bad.message.empty(), "the failure names a reason");

    json::ParseError trailing;
    json::Parse("{} junk", &trailing);
    Check(trailing.failed, "trailing data is a failure, not ignored");
}

// --------------------------------------------------------------------------
void TestBuildPlanLegality() {
    Section("build plan: 700 points, 225/100 stats");

    const rules::Profile& p = rules::Revolution();
    const life::BuildPlan plan = life::FrontierLumberjackSwordsman();
    const life::PlanCheck ok = life::ValidatePlan(p, plan);

    Check(ok.ok, "the Slice 1 character's plan is legal");
    Check(ok.resolvedTenths == 5000, "five named skills at 100.0 = 500.0 resolved");
    Check(plan.unresolvedTenths == 2000,
          "200.0 is left UNRESOLVED on purpose, not silently filled");
    Check(ok.plannedTotalTenths == p.totalSkillCapTenths,
          "resolved + unresolved is exactly the 700-point budget");
    Check(ok.statTotal == 225, "target stats sum to exactly 225");
    Check(plan.targetStr <= 100 && plan.targetDex <= 100 && plan.targetInt <= 100,
          "no target stat exceeds 100");
    Check(plan.createStr + plan.createDex + plan.createInt == 80,
          "the creation request matches the server's own 80-point ceiling");
    Check(plan.createStr == 40 && plan.createDex == 35 && plan.createInt == 5,
          "the creation split is the measured survivor, 40/35/5");

    i32 createSkillTotal = 0;
    for (const life::SkillTarget& s : plan.createSkills) createSkillTotal += s.tenths;
    Check(createSkillTotal == 1000,
          "creation skills sum to the server's 100.0 ceiling");

    // The kit matters as much as the skill: Lumberjacking is what hands over
    // the hatchet, and without it the whole economic loop cannot start.
    bool asksLumberjacking = false, asksSwords = false, asksHealing = false;
    for (const life::SkillTarget& s : plan.createSkills) {
        if (s.skillId == rules::kLumberjacking) asksLumberjacking = true;
        if (s.skillId == rules::kSwordsmanship) asksSwords = true;
        if (s.skillId == rules::kHealing)       asksHealing = true;
    }
    Check(asksLumberjacking, "creation asks for Lumberjacking (the hatchet)");
    Check(asksSwords, "creation asks for Swordsmanship (the katana)");
    Check(asksHealing, "creation asks for Healing (50 bandages)");

    // --- refusals ----------------------------------------------------------
    {
        life::BuildPlan over = plan;
        over.unresolvedTenths = 2001;
        const life::PlanCheck c = life::ValidatePlan(p, over);
        Check(!c.ok && c.violation == life::PlanViolation::SkillBudgetExceeded,
              "700.1 points is refused");
    }
    {
        life::BuildPlan tall = plan;
        tall.skills[0].tenths = 1001;
        const life::PlanCheck c = life::ValidatePlan(p, tall);
        Check(!c.ok && c.violation == life::PlanViolation::PerSkillCap,
              "a skill above 100.0 is refused");
    }
    {
        // The uo-offline T2A dexxer template carries MagicResist. Revolution
        // did not run it, and a generic T2A template is not evidence about
        // Revolution -- so the plan validator must reject it outright.
        life::BuildPlan resist = plan;
        resist.skills.push_back({rules::kMagicResistance, 100});
        resist.unresolvedTenths = 1900;
        const life::PlanCheck c = life::ValidatePlan(p, resist);
        Check(!c.ok && c.violation == life::PlanViolation::InactiveSkill,
              "Resisting Spells is refused as an inactive skill");
        Check(c.skillId == rules::kMagicResistance, "the refusal names the skill");
    }
    {
        life::BuildPlan fat = plan;
        fat.targetInt = 26;    // 100 + 100 + 26
        const life::PlanCheck c = life::ValidatePlan(p, fat);
        Check(!c.ok && c.violation == life::PlanViolation::StatTotalExceeded,
              "226 total stats is refused");
    }
    {
        life::BuildPlan tallStat = plan;
        tallStat.targetStr = 101;
        tallStat.targetDex = 99;
        tallStat.targetInt = 25;
        const life::PlanCheck c = life::ValidatePlan(p, tallStat);
        Check(!c.ok && c.violation == life::PlanViolation::StatPerCapExceeded,
              "a single stat above 100 is refused even when the total fits");
    }
    {
        life::BuildPlan rich = plan;
        rich.createStr = 60; rich.createDex = 60; rich.createInt = 60;
        const life::PlanCheck c = life::ValidatePlan(p, rich);
        Check(!c.ok && c.violation == life::PlanViolation::CreationTooRich,
              "a creation request the server would clamp is refused up front");
    }
    {
        // Six characters died on this split; one from full health in about
        // 34 seconds. It is refused by value, not by warning.
        life::BuildPlan doomed = plan;
        doomed.createStr = 55; doomed.createDex = 15; doomed.createInt = 10;
        const life::PlanCheck c = life::ValidatePlan(p, doomed);
        Check(!c.ok && c.violation == life::PlanViolation::RejectedCreationSplit,
              "the rejected 55/15/10 creation split cannot be reused");
    }
}

// --------------------------------------------------------------------------
void TestMemory() {
    Section("memory: dedupe, decay, bounds");

    life::Memory mem;
    const i64 t0 = 1000000;

    mem.NotePlace("bank", "Britain bank", 1820, 2824, 0, t0);
    mem.NotePlace("bank", "Britain bank", 1822, 2825, 0, t0 + 1000);
    Check(mem.Places().size() == 1,
          "two sightings a few tiles apart are one remembered place");
    Check(mem.Places()[0].visits == 2, "the revisit is counted");

    mem.NotePlace("bank", "Minoc bank", 2500, 550, 0, t0 + 2000);
    Check(mem.Places().size() == 2, "a bank in another city is a different place");

    mem.NoteResource("logs", 1776, 2774, 0, true, t0);
    mem.NoteResource("logs", 1778, 2776, 0, true, t0 + 500);
    mem.NoteResource("logs", 1778, 2776, 0, false, t0 + 900);
    Check(mem.Resources().size() == 1, "one stand, not three");
    Check(mem.Resources()[0].successes == 2 && mem.Resources()[0].failures == 1,
          "successes and failures are tracked separately");

    // --- danger decays rather than switching off --------------------------
    mem.NoteDanger(1900, 2800, 12, "grey wolf", 1.0, t0);
    const double atOnce = mem.DangerHeatAt(1900, 2800, t0);
    const double oneHalfLife = mem.DangerHeatAt(1900, 2800, t0 + life::kDangerHalfLifeMs);
    const double twoHalfLives =
        mem.DangerHeatAt(1900, 2800, t0 + 2 * life::kDangerHalfLifeMs);
    Check(atOnce > 0.99 && atOnce < 1.01, "fresh danger reads at full heat");
    Check(oneHalfLife > 0.49 && oneHalfLife < 0.51, "one half-life halves the heat");
    Check(twoHalfLives > 0.24 && twoHalfLives < 0.26, "decay is exponential");
    Check(mem.DangerHeatAt(4000, 1000, t0) == 0.0, "danger is local, not global");

    // Trouble at the same spot compounds ON TOP OF the decayed value, so a
    // place that keeps killing you stays hot and a single scare fades.
    mem.NoteDanger(1901, 2801, 12, "grey wolf", 1.0, t0 + life::kDangerHalfLifeMs);
    const double compounded =
        mem.DangerHeatAt(1900, 2800, t0 + life::kDangerHalfLifeMs);
    Check(compounded > 1.49 && compounded < 1.51,
          "a second scare compounds onto the decayed heat, not the original");

    mem.ExpireDanger(t0 + 40 * life::kDangerHalfLifeMs);
    Check(mem.Dangers().empty(), "fully decayed danger is pruned");

    // --- bounded history ---------------------------------------------------
    for (int i = 0; i < static_cast<int>(life::kMaxEvents) + 50; ++i) {
        mem.NoteEvent("chop", "one log", "forest", 1776, 2774, t0 + i);
    }
    Check(mem.Events().size() == life::kMaxEvents,
          "the event history is a bounded ring, not an unbounded log");
    Check(mem.Events().back().atMs == t0 + static_cast<i64>(life::kMaxEvents) + 49,
          "the newest event is kept");

    // --- suppliers: recorded, but only returned when policy allows --------
    life::KnownSupplier refused;
    refused.need = "cloth";
    refused.name = "a weaver";
    refused.serial = 0x1234;
    refused.policyAllows = false;
    refused.lastVerifiedMs = t0;
    mem.NoteSupplier(refused);
    Check(mem.Suppliers().size() == 1,
          "a policy-refused supplier is still recorded as a fact about the world");
    Check(mem.BestSupplier("cloth") == nullptr,
          "a policy-refused supplier is never returned as usable");

    life::KnownSupplier allowed = refused;
    allowed.need = "hatchet";
    allowed.name = "a smith";
    allowed.serial = 0x5678;
    allowed.policyAllows = true;
    mem.NoteSupplier(allowed);
    Check(mem.BestSupplier("hatchet") != nullptr, "an allowed supplier is returned");

    // --- and a supplier can be DISPROVED -----------------------------------
    //
    // A remembered supplier is a position, and the NPC that earned it can be
    // gone. Live, a lumberjack stood on the exact tile it remembered a
    // carpenter at, saw nobody, and travelled to that tile twice more --
    // "back to a trainer we have used before, 'carpenter' at 2629,2099", two
    // seconds apart -- before blaming the world for having no carpenter.
    // Forgetting is what stops a wrong belief becoming a loop.
    life::KnownSupplier trainer;
    trainer.need = "trainer:carpenter";
    trainer.name = "carpenter";
    trainer.serial = 0x9abc;
    trainer.policyAllows = true;
    trainer.x = 2629;
    trainer.y = 2099;
    trainer.lastVerifiedMs = t0;
    mem.NoteSupplier(trainer);
    Check(mem.BestSupplier("trainer:carpenter") != nullptr,
          "the remembered trainer is there to begin with");
    Check(!mem.ForgetSupplier("trainer:carpenter", 2629, 2100),
          "forgetting the WRONG tile changes nothing -- an off-by-one must not "
          "silently erase a good memory");
    Check(mem.BestSupplier("trainer:carpenter") != nullptr,
          "so the trainer survives that");
    Check(mem.ForgetSupplier("trainer:carpenter", 2629, 2099),
          "forgetting the tile we actually stood on reports that it did something");
    Check(mem.BestSupplier("trainer:carpenter") == nullptr,
          "and the disproved trainer is gone, so the next trip searches "
          "properly instead of walking to where we already are");
    Check(mem.BestSupplier("hatchet") != nullptr,
          "forgetting one supplier leaves the others alone");
}

// --------------------------------------------------------------------------
life::Observation HealthyLumberjackAtWork() {
    life::Observation obs;
    obs.nowMs = 5000000;
    obs.inWorld = true;
    obs.x = 1776; obs.y = 2774;
    obs.hp = 60; obs.hpMax = 60;
    obs.str = 40; obs.dex = 35; obs.intel = 5;
    obs.gold = 400;
    obs.weight = 100; obs.maxWeight = 400;
    obs.bandages = 40;
    obs.food = 3;
    obs.axeInPack = true;
    obs.axeEquipped = true;
    obs.weaponEquipped = true;
    obs.atWorkSite = true;
    obs.treeAdjacent = true;
    obs.skills = {
        {rules::kLumberjacking, 350},
        {rules::kSwordsmanship, 400},
        {rules::kTactics,       200},
        {rules::kAnatomy,       200},
        {rules::kHealing,       200},
    };
    return obs;
}

const life::Need* Find(const std::vector<life::Need>& needs, life::NeedKind k) {
    for (const life::Need& n : needs) {
        if (n.kind == k) return &n;
    }
    return nullptr;
}

void TestNeeds() {
    Section("needs: what, and why");

    const life::BuildPlan plan = life::FrontierLumberjackSwordsman();
    const life::NeedConfig cfg;
    life::Memory mem;

    // --- working normally --------------------------------------------------
    {
        const life::Observation obs = HealthyLumberjackAtWork();
        const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, obs, cfg);
        Check(Find(needs, life::NeedKind::NeedTool) == nullptr,
              "an armed lumberjack does not need a tool");
        Check(Find(needs, life::NeedKind::StayAlive) == nullptr,
              "nothing threatens, so survival is not a need");
        const life::Need* logs = Find(needs, life::NeedKind::NeedLogs);
        Check(logs != nullptr && !logs->blocked, "gathering logs is an open need");
        Check(Find(needs, life::NeedKind::NeedTraining) != nullptr,
              "skills below target produce a training need");
        for (const life::Need& n : needs) {
            Check(!n.what.empty(), "every need names WHAT it is about");
            Check(!n.reason.empty(), "every need states WHY");
        }
    }

    // --- no axe: the profession is blocked, and it says so ----------------
    {
        life::Observation obs = HealthyLumberjackAtWork();
        obs.axeInPack = false;
        obs.axeEquipped = false;
        const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* tool = Find(needs, life::NeedKind::NeedTool);
        Check(tool != nullptr, "no axe produces a tool need");
        Check(tool->what == "hatchet", "the need names the tool, not just 'tool'");
        Check(tool->blocked, "with no known supplier the need is BLOCKED, not pretended");
        const life::Need* logs = Find(needs, life::NeedKind::NeedLogs);
        Check(logs != nullptr && logs->blocked,
              "gathering is blocked too -- an axeless lumberjack cannot work");

        // Learning a supplier unblocks it. Nothing is conjured.
        life::Memory withSupplier;
        life::KnownSupplier s;
        s.need = "hatchet";
        s.name = "a blacksmith";
        s.serial = 0x99;
        s.x = 1500; s.y = 1600;
        s.policyAllows = true;
        s.lastVerifiedMs = obs.nowMs;
        withSupplier.NoteSupplier(s);
        const std::vector<life::Need> needs2 =
            life::AssessNeeds(plan, withSupplier, obs, cfg);
        const life::Need* tool2 = Find(needs2, life::NeedKind::NeedTool);
        Check(tool2 != nullptr && !tool2->blocked,
              "a learned supplier turns a blocked need into an actionable one");
    }

    // --- gang pressure raises the bail threshold --------------------------
    {
        life::Observation solo = HealthyLumberjackAtWork();
        solo.hp = 24;   // 40%
        solo.underAttack = true;
        solo.hostilesNear = 1;
        solo.attackersOnMe = 1;
        // Bind the vector to a named local: taking a pointer INTO a temporary
        // container leaves it dangling at the end of the full expression.
        const std::vector<life::Need> soloNeeds = life::AssessNeeds(plan, mem, solo, cfg);
        const life::Need* n1 = Find(soloNeeds, life::NeedKind::StayAlive);
        Check(n1 != nullptr, "an attack always produces a survival need");
        const double soloUrgency = n1 ? n1->urgency : 0.0;

        life::Observation swarmed = solo;
        swarmed.hostilesNear = 3;
        swarmed.attackersOnMe = 3;
        const std::vector<life::Need> swarmNeeds =
            life::AssessNeeds(plan, mem, swarmed, cfg);
        const life::Need* n3 = Find(swarmNeeds, life::NeedKind::StayAlive);
        Check(n3 != nullptr && n3->urgency > soloUrgency,
              "40% health is survivable 1v1 and a bail-out against three");
    }

    // --- weight -----------------------------------------------------------
    {
        life::Observation heavy = HealthyLumberjackAtWork();
        heavy.weight = 380;   // 95% of 400
        heavy.logs = 60;
        const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, heavy, cfg);
        const life::Need* bank = Find(needs, life::NeedKind::NeedBank);
        Check(bank != nullptr, "a nearly full pack produces a banking need");
        Check(bank && bank->evidence.find("95%") != std::string::npos,
              "the banking need shows the measured weight, not a bare flag");
    }

    // --- dead: a ghost has no economic needs ------------------------------
    {
        life::Observation dead = HealthyLumberjackAtWork();
        dead.dead = true;
        dead.corpseKnown = true;
        dead.corpseX = 1780; dead.corpseY = 2780;
        const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, dead, cfg);
        Check(Find(needs, life::NeedKind::NeedLogs) == nullptr,
              "a ghost is not asked to chop wood");
        Check(Find(needs, life::NeedKind::StayAlive) != nullptr,
              "a ghost needs resurrection");
        const life::Need* corpse = Find(needs, life::NeedKind::RecoverCorpse);
        Check(corpse != nullptr && !corpse->blocked, "a known corpse is recoverable");

        dead.corpseRecoveryAttempts = 3;
        const std::vector<life::Need> tired = life::AssessNeeds(plan, mem, dead, cfg);
        const life::Need* exhausted = Find(tired, life::NeedKind::RecoverCorpse);
        Check(exhausted != nullptr && !exhausted->blocked,
              "exhausted recovery must run once to clear the corpse record");
    }
}

// --------------------------------------------------------------------------
void TestCombatWaitsForBandages() {
    Section("combat does not repeatedly cancel its own supply prerequisite");
    life::Planner planner;
    life::Memory memory;
    auto obs = HealthyLumberjackAtWork();
    obs.hp = obs.hpMax;
    life::Need bandages;
    bandages.kind = life::NeedKind::NeedMakeBandages;
    bandages.urgency = 0.27;
    std::vector<life::Need> needs = {bandages};
    std::string why;
    planner.Select(needs, obs, memory, obs.nowMs, &why);
    Check(planner.Current().kind == life::GoalKind::MakeBandages, "supply errand started");
    life::Need combat;
    combat.kind = life::NeedKind::NeedTraining;
    combat.urgency = 0.9;
    needs.push_back(combat);
    obs.nowMs += 31000;
    planner.Select(needs, obs, memory, obs.nowMs, &why);
    Check(planner.Current().kind == life::GoalKind::MakeBandages,
          "combat cooldown expiring does not restart the cloth journey");
    life::Need danger;
    danger.kind = life::NeedKind::StayAlive;
    danger.urgency = 1.0;
    needs.push_back(danger);
    planner.Select(needs, obs, memory, ++obs.nowMs, &why);
    Check(planner.Current().kind == life::GoalKind::Survive, "danger still interrupts supplies");
    life::Planner finished;
    finished.Select({bandages}, obs, memory, obs.nowMs, &why);
    obs.nowMs += 31000;
    finished.Select({combat}, obs, memory, obs.nowMs, &why);
    Check(finished.Current().kind == life::GoalKind::TrainCombat,
          "combat resumes when the supply need disappears");
}

void TestPlanner() {
    Section("planner: scoring, commitment, bounded failure");

    const life::BuildPlan plan = life::FrontierLumberjackSwordsman();
    const life::NeedConfig needCfg;
    life::Memory mem;

    life::PlannerConfig cfg;
    cfg.minCommitMs = 20000;
    cfg.maxGoalMs = 60000;
    cfg.maxAttempts = 3;
    life::Planner planner(cfg);

    life::Observation obs = HealthyLumberjackAtWork();
    std::string why;

    // --- first selection ---------------------------------------------------
    {
        const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, obs, needCfg);
        const bool changed = planner.Select(needs, obs, mem, obs.nowMs, &why);
        Check(changed, "the first tick selects a goal");
        Check(planner.Current().active, "the goal is running");
        Check(planner.Current().kind == life::GoalKind::GatherLogs,
              "a healthy, armed, empty-packed lumberjack chooses to gather");

        const std::vector<life::ScoredGoal> scored = planner.Score(needs, obs, mem);
        Check(!scored.empty(), "scoring produces candidates");
        Check(!scored[0].reasons.empty(), "the winning goal explains itself");
        bool sawIdle = false;
        for (const life::ScoredGoal& g : scored) {
            if (g.kind == life::GoalKind::IdleBriefly) sawIdle = true;
        }
        Check(sawIdle, "a bounded no-op always exists, so there is never 'no goal'");
    }

    // --- commitment: a better goal still waits out the floor ---------------
    //
    // The pack fills up, so BANK genuinely outscores GATHER_LOGS. Three
    // seconds into the gathering goal, that must NOT be enough to switch --
    // this is the exact gather/bank/gather flapping the M4 brief forbids.
    {
        life::Observation nudged = obs;
        nudged.nowMs += 3000;
        nudged.weight = 380;              // 95% of capacity: banking clearly wins
        nudged.logs = 60;
        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, mem, nudged, needCfg);
        const std::vector<life::ScoredGoal> scored = planner.Score(needs, nudged, mem);
        Check(!scored.empty() && scored[0].kind == life::GoalKind::Bank,
              "with a full pack, banking is the higher-scoring goal");

        const bool changed = planner.Select(needs, nudged, mem, nudged.nowMs, &why);
        Check(!changed, "a goal three seconds old is not swapped out for a better one");
        Check(why.find("commitment floor") != std::string::npos,
              "and the refusal says why");
        Check(planner.Current().kind == life::GoalKind::GatherLogs,
              "the incumbent goal is still running");
    }

    // --- past the floor, the better goal DOES take over --------------------
    {
        life::Observation later = obs;
        later.nowMs += 30000;
        later.weight = 380;
        later.logs = 60;
        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, mem, later, needCfg);
        const bool changed = planner.Select(needs, later, mem, later.nowMs, &why);
        Check(changed && planner.Current().kind == life::GoalKind::Bank,
              "once the commitment floor has passed, a clearly better goal wins");
        Check(why.find("superseded") != std::string::npos,
              "and the handover is logged with both scores");
    }

    // --- hysteresis: a near-win past the floor still keeps the incumbent ---
    //
    // Carrying enough logs to be worth banking, with a bank already learned,
    // puts BANK slightly ahead of GATHER_LOGS. "Slightly" is the whole point:
    // inside the margin, the running goal keeps the body.
    {
        life::PlannerConfig tight = cfg;
        tight.incumbentBonus = 0.25;
        life::Planner p6(tight);

        life::Memory withBank;
        withBank.NotePlace("bank", "Britain bank", 1820, 2824, 0, 1);

        life::Observation work = HealthyLumberjackAtWork();
        p6.Select(life::AssessNeeds(plan, withBank, work, needCfg), work, withBank,
                  work.nowMs, &why);
        Check(p6.Current().kind == life::GoalKind::GatherLogs, "gathering first");

        life::Observation tie = work;
        tie.nowMs += 40000;               // well past the commitment floor
        tie.logs = 22;                    // worth banking, but the pack is not full
        // BETWEEN STANDS, NOT ON ONE. A lumberjack standing in the trees with
        // the axe in hand is deliberately no longer a near-tie with anything:
        // NeedLogs reads 0.65 there and GATHER_LOGS takes "the walk is already
        // paid for +60" (Needs.cpp / Goals.cpp, the Vorar case of 2026-09-06).
        // The hysteresis this block is about needs two goals within 25% of
        // each other, and that is a character walking between stands -- which
        // is also when a full-ish pack genuinely competes with the bank.
        tie.atWorkSite = false;
        tie.treeAdjacent = false;
        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, withBank, tie, needCfg);
        const std::vector<life::ScoredGoal> scored = p6.Score(needs, tie, withBank);
        double bankScore = 0.0, gatherScore = 0.0;
        for (const life::ScoredGoal& g : scored) {
            if (g.kind == life::GoalKind::Bank) bankScore = g.score;
            if (g.kind == life::GoalKind::GatherLogs) gatherScore = g.score;
        }
        Check(bankScore > gatherScore, "banking now scores higher than gathering");
        Check(bankScore < gatherScore * (1.0 + tight.incumbentBonus),
              "but only inside the hysteresis margin");

        const bool changed = p6.Select(needs, tie, withBank, tie.nowMs, &why);
        Check(!changed, "a near-win past the floor still favours the incumbent");
        Check(why.find("does not beat") != std::string::npos,
              "and says by how much it fell short");
        Check(p6.Current().kind == life::GoalKind::GatherLogs,
              "the character keeps chopping instead of flapping to the bank");
    }

    // --- an emergency preempts regardless of commitment -------------------
    {
        life::Planner p2(cfg);
        life::Observation work = HealthyLumberjackAtWork();
        p2.Select(life::AssessNeeds(plan, mem, work, needCfg), work, mem, work.nowMs, &why);
        Check(p2.Current().kind == life::GoalKind::GatherLogs, "gathering first");

        life::Observation ambush = work;
        ambush.nowMs += 2000;             // well inside the commitment floor
        ambush.hp = 12;                   // 20%
        ambush.underAttack = true;
        ambush.hostilesNear = 2;
        ambush.attackersOnMe = 2;
        const bool changed =
            p2.Select(life::AssessNeeds(plan, mem, ambush, needCfg), ambush, mem,
                      ambush.nowMs, &why);
        Check(changed, "a life-threatening interruption preempts a committed goal");
        Check(p2.Current().kind == life::GoalKind::Survive,
              "and the new goal is survival");
        Check(why.find("emergency preempt") != std::string::npos,
              "the preemption is logged as one");
    }

    // --- bounded failure: attempts ----------------------------------------
    {
        life::Planner p3(cfg);
        life::Observation work = HealthyLumberjackAtWork();
        p3.Select(life::AssessNeeds(plan, mem, work, needCfg), work, mem, work.nowMs, &why);
        for (int i = 0; i < cfg.maxAttempts; ++i) p3.NoteAttempt(work.nowMs);
        std::string exhaustedWhy;
        Check(p3.Exhausted(work.nowMs, &exhaustedWhy),
              "a goal that keeps failing is exhausted, not retried forever");
        Check(exhaustedWhy.find("attempts") != std::string::npos,
              "and the exhaustion names the attempt count");

        p3.NoteProgress();
        Check(!p3.Exhausted(work.nowMs, nullptr),
              "real progress clears the failure ladder");
    }

    // --- exhausting the attempts is a NO-OP COMPLETION, and it cools ------
    //
    // Bruin abandoned REPLACE_EQUIPMENT on "attempts 5 >= 5" thirty-nine times
    // and was handed it straight back, with a fresh budget, every time
    // (run_r4/w_Bruin.console.txt:307-386). Select cleared goal_.active by
    // hand, so Finish never ran -- and with it neither did the noop-spin
    // backstop that exists precisely to cool a goal which terminates having
    // changed nothing. A goal that spends its entire allowance on nothing is
    // spinning, whichever door it leaves by.
    {
        life::Planner p6(cfg);
        life::Observation work = HealthyLumberjackAtWork();
        p6.Select(life::AssessNeeds(plan, mem, work, needCfg), work, mem,
                  work.nowMs, &why);
        const life::GoalKind first = p6.Current().kind;

        life::GoalKind spun = life::GoalKind::Count;
        int rounds = 0;
        // Five consecutive zero-progress terminations is kNoopSpinLimit.
        for (int round = 0; round < 5 && spun == life::GoalKind::Count; ++round) {
            for (int i = 0; i < cfg.maxAttempts; ++i) p6.NoteAttempt(work.nowMs);
            p6.Select(life::AssessNeeds(plan, mem, work, needCfg), work, mem,
                      work.nowMs, &why);
            ++rounds;
            if (round == 0) {
                // The log line and every grep over it keep working.
                Check(why.find("previous goal abandoned: attempts") !=
                          std::string::npos,
                      "the abandonment still reads 'previous goal abandoned: "
                      "attempts N >= N'");
            }
            spun = p6.TakeSpinDetected();
        }

        Check(spun == first,
              "five attempts-exhausted terminations with no progress trip the "
              "anti-spin backstop, exactly as five no-op completions do");
        Check(rounds == 5, "and it takes the full five, not fewer");
        Check(p6.Cooling(first, work.nowMs),
              "so the goal is COOLING afterwards rather than being handed "
              "straight back with a fresh attempt budget");
        Check(p6.Current().kind != first,
              "and something else gets the turn -- the cooldown is applied "
              "BEFORE the re-score, or the cooled goal simply wins again");
    }

    // --- bounded failure: time --------------------------------------------
    {
        life::Planner p4(cfg);
        life::Observation work = HealthyLumberjackAtWork();
        p4.Select(life::AssessNeeds(plan, mem, work, needCfg), work, mem, work.nowMs, &why);
        std::string exhaustedWhy;
        Check(p4.Exhausted(work.nowMs + cfg.maxGoalMs + 1, &exhaustedWhy),
              "a goal that never finishes is abandoned on the clock");
        Check(exhaustedWhy.find("without finishing") != std::string::npos,
              "and says so");
    }

    // --- a blocked goal is reported, not silently dropped -----------------
    {
        life::Observation noAxe = HealthyLumberjackAtWork();
        noAxe.axeInPack = false;
        noAxe.axeEquipped = false;
        life::Planner p5(cfg);
        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, mem, noAxe, needCfg);
        const std::vector<life::ScoredGoal> scored = p5.Score(needs, noAxe, mem);
        bool sawBlockedTool = false;
        for (const life::ScoredGoal& g : scored) {
            if (g.kind != life::GoalKind::GetTool) continue;
            sawBlockedTool = !g.feasible && !g.blockedWhy.empty();
        }
        Check(sawBlockedTool,
              "with no supplier, GET_TOOL is reported as blocked WITH a reason");
        for (const life::ScoredGoal& g : scored) {
            if (!g.feasible) {
                Check(!g.blockedWhy.empty(), "every infeasible goal carries a reason");
            }
        }
    }
}

// --------------------------------------------------------------------------
// The bug from tonight's live run (run_r4/pair_Tarath.console.txt): with a
// proven stand AND the axe already in hand, GATHER_LOGS scored 97
// (52 need + 25 proven stand + 20 axe in hand) over TRADE_WITH_PLAYER's 80
// while Tarath was sitting on 97 spare logs -- the two flat bonuses that
// exist to get an idle character moving were instead dragging a character
// who already had plenty back to the axe, instead of to the buyer who would
// take the pile. keepOfOwnOutput is 20 (market.h); 113 logs held is 93
// spare, comfortably past the 2x-keep (40) damper threshold, and matches the
// exact figure E's NeedTrade test uses (m8_market_trip.cpp) for urgency 0.55.
void TestGatherLogsSurplusYieldsToTrade() {
    Section("planner: a big log surplus damps GATHER_LOGS toward TRADE_WITH_PLAYER");

    const prof::Profession* jack = prof::Find("lumberjack_swordsman");
    if (!jack) { Check(false, "no lumberjack_swordsman"); return; }

    life::NeedConfig needCfg;
    needCfg.profession = jack;
    const life::BuildPlan plan = life::PlanFromProfession(*jack);

    life::Memory mem;
    life::Observation obs = HealthyLumberjackAtWork();
    obs.pack.push_back({"i_log", 113});
    obs.gold = 20000;
    obs.goldOnHand = 800;
    // A stand that has ACTUALLY PAID OUT, right where the character stands --
    // otherwise the "proven stand +25" bonus this test is about never fires,
    // and the pre-fix bug (which needed it) would not reproduce.
    mem.NoteResource("logs", obs.x, obs.y, 0, /*success=*/true, obs.nowMs);

    const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, obs, needCfg);
    const life::Need* logsNeed = Find(needs, life::NeedKind::NeedLogs);
    // 0.15, THE LOGGING FLOOR, NOT THE OLD FLAT 0.40. The need itself now
    // tapers with the same glut the damper below reads -- 113 logs is past
    // 2x keep (40), so cutting more is the least valuable thing this life
    // could do and the urgency says so rather than leaving the whole
    // judgement to the score modifiers.
    Check(logsNeed != nullptr && logsNeed->urgency > 0.14 && logsNeed->urgency < 0.16,
          "NeedLogs has tapered to its floor: 113 logs is well past 2x keep");
    const life::Need* tradeNeed = Find(needs, life::NeedKind::NeedTrade);
    Check(tradeNeed != nullptr && tradeNeed->urgency > 0.54 && tradeNeed->urgency < 0.56,
          "NeedTrade urgency is 0.55 -- 93 spare logs against a 20-log trip is a full load");

    life::Planner planner;
    const std::vector<life::ScoredGoal> scored = planner.Score(needs, obs, mem);
    double gatherScore = -1.0, tradeScore = -1.0;
    std::string gatherReasons, tradeReasons;
    for (const life::ScoredGoal& g : scored) {
        if (g.kind == life::GoalKind::GatherLogs) {
            gatherScore = g.score;
            for (const std::string& r : g.reasons) gatherReasons += r + " | ";
        }
        if (g.kind == life::GoalKind::TradeWithPlayer) {
            tradeScore = g.score;
            for (const std::string& r : g.reasons) tradeReasons += r + " | ";
        }
    }
    Check(gatherScore >= 0.0, "GATHER_LOGS is scored at all");
    Check(tradeScore >= 0.0, "TRADE_WITH_PLAYER is scored at all");
    // 130 x 0.15 = 19.5, with no bonus added: the damper dropped the
    // proven-stand, axe-in-hand and standing-in-the-trees bonuses, and the
    // need had already tapered to its floor.
    Check(gatherScore > 18.5 && gatherScore < 20.5,
          "GATHER_LOGS keeps only its bare need score -- the proven-stand, "
          "axe-in-hand and at-the-stand bonuses were dropped by the surplus "
          "damper");
    // 145 x 0.55 = 79.75, unaffected by the gathering-side damper.
    Check(tradeScore > 79.0 && tradeScore < 80.5,
          "TRADE_WITH_PLAYER scores its ordinary 79.75");
    Check(tradeScore > gatherScore,
          "with the surplus this large, TRADE_WITH_PLAYER now outscores "
          "GATHER_LOGS -- the live-run bug (97 vs 80, backwards) is fixed");
    Check(gatherReasons.find("spare") != std::string::npos &&
              gatherReasons.find("dropping the stand/axe bonuses") != std::string::npos,
          "the damper explains itself in the reasons vector");
}

// --------------------------------------------------------------------------
// The other half of the same decision, from the same day's run log: a
// lumberjack that walked 216 tiles to the woods must CUT before it walks back
// to town for a comfort-level errand.
//
// Vorar, 2026-09-06: arrived at Britain Territory woods (1334,1496) at
// 18:20:26 with a hatchet in hand, an empty pack and 84 bandages against a
// floor of 100. GATHER_LOGS scored 72.0 (0.40 x 130 + 20 axe),
// REPLACE_EQUIPMENT scored 130, the planner never came back to the woods in
// the four minutes left, and session_summary read logs=+0
// (run_gates/g_Vorar.console.txt:88,136,189).
void TestStandingInTheWoodsOutscoresAComfortErrand() {
    Section("planner: a gatherer at its stand cuts before it shops");

    const prof::Profession* jack = prof::Find("lumberjack_swordsman");
    if (!jack) { Check(false, "no lumberjack_swordsman"); return; }

    life::NeedConfig needCfg;
    needCfg.profession = jack;
    const life::BuildPlan plan = life::PlanFromProfession(*jack);

    life::Memory mem;
    life::Observation obs = HealthyLumberjackAtWork();   // atWorkSite, axe in hand
    obs.gold = 1000;
    obs.goldOnHand = 1000;
    obs.bandages = 40;      // short of the floor, at full health: a top-up
    obs.weight = 40;        // room to carry what gets cut

    const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, obs, needCfg);
    const life::Need* logsNeed = Find(needs, life::NeedKind::NeedLogs);
    Check(logsNeed != nullptr && logsNeed->urgency > 0.64 && logsNeed->urgency < 0.66,
          "standing in the trees with an axe and an empty pack reads 0.65");

    life::Planner planner;
    const std::vector<life::ScoredGoal> scored = planner.Score(needs, obs, mem);
    double gather = -1.0, shop = -1.0;
    std::string gatherReasons;
    for (const life::ScoredGoal& g : scored) {
        if (g.kind == life::GoalKind::GatherLogs) {
            gather = g.score;
            for (const std::string& r : g.reasons) gatherReasons += r + " | ";
        }
        if (g.kind == life::GoalKind::ReplaceEquipment) shop = g.score;
    }
    std::printf("  GATHER_LOGS %.1f vs REPLACE_EQUIPMENT %.1f\n", gather, shop);
    Check(gather > 0.0 && shop > 0.0, "both goals are scored");
    // The margin matters, not just the ordering: PlannerConfig::incumbentBonus
    // is 0.15, and REPLACE_EQUIPMENT was the incumbent when Vorar arrived.
    Check(gather > shop * 1.15,
          "cutting beats the bandage top-up by more than the incumbent margin");
    Check(gatherReasons.find("standing in the trees") != std::string::npos,
          "and the score explains itself");

    // AND IT IS NOT A LICENCE TO STAND THERE FOREVER: walk off the stand and
    // the same character is back to its ordinary 0.40.
    life::Observation away = obs;
    away.atWorkSite = false;
    away.treeAdjacent = false;
    const life::Need* awayNeed =
        Find(life::AssessNeeds(plan, mem, away, needCfg), life::NeedKind::NeedLogs);
    Check(awayNeed != nullptr && awayNeed->urgency > 0.39 && awayNeed->urgency < 0.41,
          "away from the trees the need is the ordinary 0.40");
}

// --------------------------------------------------------------------------
life::PersistentState SampleState() {
    life::PersistentState st;
    st.identity.identityId    = "revolutionlumber01.balthasar";
    st.identity.accountName   = "RevolutionLumber01";
    st.identity.characterName = "Balthasar";
    st.identity.createdAtMs   = 1000;
    st.identity.firstSeenAtMs = 1000;
    st.identity.lastSeenAtMs  = 9000;
    st.identity.totalPlayTimeMs = 8000;
    st.identity.sessions = 2;

    st.plan = life::FrontierLumberjackSwordsman();

    st.memory.NotePlace("bank", "Britain bank", 1820, 2824, 0, 2000);
    st.memory.NoteResource("logs", 1776, 2774, 0, true, 3000);
    life::KnownSupplier s;
    s.need = "hatchet";
    s.name = "a blacksmith";
    s.sourceType = "npc_vendor";
    s.serial = 0xABCD;
    s.x = 1500; s.y = 1600; s.z = 0;
    s.observedQuantity = 4;
    s.observedPricePerUnit = 21;
    s.lastVerifiedMs = 4000;
    s.policyAllows = true;
    st.memory.NoteSupplier(s);
    st.memory.NoteDanger(1900, 2800, 12, "grey wolf", 1.0, 5000);
    st.memory.NoteCreatureOutcome("a lich", life::kCreatureEvidenceDeath, 5500);
    st.memory.NoteEvent("first_logs", "8 logs", "forest", 1776, 2774, 6000);

    st.goal.kind = life::GoalKind::GatherLogs;
    st.goal.active = true;
    st.goal.startedAtMs = 7000;
    st.goal.attempts = 1;
    st.goal.progress = 12;

    st.lastKnownGold = 312;
    st.lastKnownStr = 41;
    st.lastKnownDex = 36;
    st.lastKnownInt = 5;
    st.lastKnownSkills = {{rules::kLumberjacking, 437}, {rules::kSwordsmanship, 402}};
    st.lastKnownX = 1776;
    st.lastKnownY = 2774;
    st.checkpointMs = 9000;

    life::SessionSummary sum;
    sum.startedMs = 1000;
    sum.endedMs = 9000;
    sum.goalsAttempted = 5;
    sum.goalsCompleted = 4;
    sum.logsGathered = 41;
    sum.cleanLogout = true;
    st.sessions.push_back(sum);
    return st;
}

void TestStateRoundTrip() {
    Section("persistence: schema round trip");

    life::PersistentState st = SampleState();
    st.huntReturnPending = true;
    const json::Value doc = life::ToJson(st);
    const std::string text = doc.Serialize(2);

    Check(text.find("\"schema_version\"") != std::string::npos,
          "the file carries a schema_version");
    Check(text.find("password") == std::string::npos,
          "no password ever reaches the state file");

    json::ParseError perr;
    const json::Value back = json::Parse(text, &perr);
    Check(!perr.failed, "the written state parses");

    life::PersistentState loaded;
    std::string err;
    Check(life::FromJson(back, &loaded, &err), "the written state loads");

    Check(loaded.huntReturnPending, "unfinished hunt bank return survives a reconnect");
    Check(loaded.identity.identityId == st.identity.identityId, "identity survives");
    Check(loaded.identity.characterName == st.identity.characterName, "name survives");
    Check(loaded.identity.totalPlayTimeMs == st.identity.totalPlayTimeMs,
          "play time survives");
    Check(loaded.plan.family == st.plan.family, "build family survives");
    Check(loaded.plan.skills.size() == st.plan.skills.size(), "target skills survive");
    Check(loaded.plan.unresolvedTenths == st.plan.unresolvedTenths,
          "the deliberately unresolved budget survives as a number");
    Check(loaded.plan.targetStr == 100 && loaded.plan.targetDex == 100 &&
          loaded.plan.targetInt == 25, "target stats survive");
    Check(loaded.memory.Places().size() == 1, "learned places survive");
    Check(loaded.memory.Places()[0].name == "Britain bank", "place names survive");
    Check(loaded.memory.Resources().size() == 1, "learned resource stands survive");
    Check(loaded.memory.Suppliers().size() == 1, "learned suppliers survive");
    Check(loaded.memory.Suppliers()[0].observedPricePerUnit == 21,
          "the observed price survives -- it is a fact we measured");
    Check(loaded.memory.Dangers().size() == 1, "danger memory survives");
    Check(loaded.memory.Creatures().size() == 1, "learned creature verdicts survive");
    Check(loaded.memory.Creatures()[0].name == "a lich",
          "the creature's client-visible name survives");
    Check(loaded.memory.CreatureDanger("a lich", 5500) > 1.99,
          "the learned verdict itself round-trips, not just the record shape");
    Check(loaded.memory.Events().size() == 1, "the event history survives");
    Check(loaded.goal.kind == life::GoalKind::GatherLogs, "the current objective survives");
    Check(loaded.goal.progress == 12, "goal progress survives");
    Check(loaded.lastKnownGold == 312, "the last server report survives for the diff");
    Check(loaded.sessions.size() == 1, "session history survives");
    Check(loaded.sessions[0].logsGathered == 41, "session figures survive");

    const life::PlanCheck check = life::ValidatePlan(rules::Revolution(), loaded.plan);
    Check(check.ok, "a plan reloaded from disk is still a legal Revolution build");

    // --- forward compatibility --------------------------------------------
    {
        json::Value future = back;
        future.Set("schema_version", static_cast<i64>(life::kSchemaVersion + 1));
        life::PersistentState dummy;
        std::string ferr;
        Check(!life::FromJson(future, &dummy, &ferr),
              "a state file from a NEWER build is refused, not half-read");
        Check(!ferr.empty(), "and the refusal explains itself");
    }
    {
        json::Value versionless = json::Value::MakeObject();
        versionless.Set("identity", json::Value::MakeObject());
        life::PersistentState dummy;
        std::string ferr;
        Check(!life::FromJson(versionless, &dummy, &ferr),
              "a file with no schema_version is refused");
    }
    {
        // Missing sections default rather than failing -- that is what makes
        // an OLDER file loadable under a newer reader.
        json::Value sparse = json::Value::MakeObject();
        sparse.Set("schema_version", static_cast<i64>(1));
        life::PersistentState thin;
        std::string ferr;
        Check(life::FromJson(sparse, &thin, &ferr),
              "a minimal file loads with defaults instead of failing");
        Check(thin.memory.Places().empty(), "absent memory defaults to empty");
        Check(!thin.goal.active, "absent goal defaults to inactive");
    }
}

void TestStore(const std::string& tmpDir) {
    Section("persistence: store on disk");

    const life::Store store(tmpDir + "/bot_data");
    life::PersistentState st = SampleState();

    std::string err;
    Check(store.Save(st, &err), "state saves to a fresh directory tree");
    Check(err.empty(), "and reports no error");
    Check(store.Exists(st.identity.identityId), "the saved state is found again");

    // The atomic write must not leave its scratch file behind -- a stray
    // state.json.tmp is how a half-written save survives a crash.
    const std::string tmpPath = store.PathFor(st.identity.identityId) + ".tmp";
    std::string leftover;
    Check(!json::ReadFile(tmpPath.c_str(), &leftover),
          "the atomic write leaves no .tmp behind");

    life::PersistentState loaded;
    Check(store.Load(st.identity.identityId, &loaded, &err), "state loads back");
    Check(loaded.identity.characterName == "Balthasar", "the right character loads");
    Check(loaded.memory.Suppliers().size() == 1, "learned suppliers came back");

    // A character that has never played is not an error.
    life::PersistentState missing;
    std::string missErr = "sentinel";
    Check(!store.Load("nobody.nothing", &missing, &missErr),
          "loading a character with no history returns false");
    Check(missErr.empty(),
          "and is NOT reported as an error -- a new character simply has no state");

    // Saving twice must overwrite in place, not accumulate.
    st.lastKnownGold = 999;
    Check(store.Save(st, &err), "a second save succeeds");
    life::PersistentState again;
    Check(store.Load(st.identity.identityId, &again, &err), "and reloads");
    Check(again.lastKnownGold == 999, "the second save replaced the first");
}

// --------------------------------------------------------------------------
void TestReconciliation() {
    Section("reconciliation: the server wins");

    life::PersistentState st = SampleState();

    life::Observation obs;
    obs.nowMs = 100000;
    obs.inWorld = true;
    obs.gold = 328;                 // the character earned 16 gold we never saw
    obs.str = 41; obs.dex = 36; obs.intel = 5;
    obs.x = 1800; obs.y = 2800;
    obs.axeInPack = true;
    obs.skills = {
        {rules::kLumberjacking, 441},   // 43.7 -> 44.1: real server progression
        {rules::kSwordsmanship, 402},
        {rules::kTactics, 200},
        {rules::kAnatomy, 200},
        {rules::kHealing, 200},
    };

    const life::ReconcileReport rep = life::Reconcile(&st, obs);

    Check(!rep.lines.empty(), "reconciliation produces an explicit log");
    Check(rep.driftFields > 0, "and notices that things moved while we were away");
    Check(st.lastKnownGold == 328, "gold reconciles to the server's figure");
    Check(st.lastKnownX == 1800 && st.lastKnownY == 2800,
          "position reconciles to the server's figure");

    bool sawLumberjacking = false;
    for (const life::ReconcileLine& l : rep.lines) {
        if (l.field != "skill_44") continue;
        sawLumberjacking = true;
        Check(l.persisted == "43.7", "the log shows what we believed");
        Check(l.server == "44.1", "the log shows what the server said");
        Check(l.result.find("server value accepted") != std::string::npos,
              "and which one was kept");
    }
    Check(sawLumberjacking, "every planned skill appears in the reconciliation log");

    // --- the crash case: our save is BEHIND the server --------------------
    {
        life::PersistentState behind = SampleState();
        behind.lastKnownSkills = {{rules::kLumberjacking, 600}};   // stale, too high
        life::Observation server = obs;
        server.skills = {{rules::kLumberjacking, 441}};
        life::Reconcile(&behind, server);
        i32 kept = 0;
        for (const life::SkillTarget& s : behind.lastKnownSkills) {
            if (s.skillId == rules::kLumberjacking) kept = s.tenths;
        }
        Check(kept == 441,
              "an out-of-date save never overwrites server progression in either direction");
    }

    // --- a restored goal is a hypothesis, not a resumable process ---------
    {
        life::PersistentState dropped = SampleState();
        life::Observation noAxe = obs;
        noAxe.axeInPack = false;
        noAxe.axeEquipped = false;
        const life::ReconcileReport r2 = life::Reconcile(&dropped, noAxe);
        Check(r2.goalDropped, "a gathering goal is dropped when the axe is gone");
        Check(!r2.goalDropReason.empty(), "and the reason is recorded");
        Check(!dropped.goal.active, "the goal is no longer running");
    }
    {
        life::PersistentState ghost = SampleState();
        life::Observation dead = obs;
        dead.dead = true;
        const life::ReconcileReport r3 = life::Reconcile(&ghost, dead);
        Check(r3.goalDropped, "a work goal cannot resume into a ghost");
    }
    {
        // A goal that IS still satisfiable survives -- but its clock does not.
        life::PersistentState kept = SampleState();
        const life::ReconcileReport r4 = life::Reconcile(&kept, obs);
        Check(!r4.goalDropped, "a still-valid objective is kept across the restart");
        Check(kept.goal.active, "and stays active");
        Check(kept.goal.startedAtMs == obs.nowMs,
              "but its clock restarts -- a goal does not carry a stale age");
        Check(kept.goal.attempts == 0, "and its attempt counter is transient");
    }

    // --- death spiral decays ----------------------------------------------
    {
        life::PersistentState spiral = SampleState();
        spiral.recentDeaths = 3;
        spiral.lastDeathMs = 1000;
        life::Observation muchLater = obs;
        muchLater.nowMs = 1000 + 2 * 60 * 60 * 1000;
        life::Reconcile(&spiral, muchLater);
        Check(spiral.recentDeaths == 0, "the death-spiral counter decays after a quiet hour");
    }

    // --- identity is never lost -------------------------------------------
    {
        life::PersistentState id = SampleState();
        life::Reconcile(&id, obs);
        Check(id.identity.characterName == "Balthasar",
              "reconciliation never touches identity");
        Check(id.plan.family == "frontier_lumberjack_swordsman",
              "reconciliation never touches the build plan");
    }
}

void TestHintsVersusEarnedStands() {
    Section("memory: a hint is a lead, a stand is earned");

    life::Memory mem;
    const i64 t0 = 1000;

    mem.HintResource("logs", "Yew woods", 664, 1030, 0, t0);
    mem.HintResource("logs", "Britain Territory woods", 1416, 1288, 0, t0);
    Check(mem.Resources().size() == 2, "two forests seeded as leads");
    Check(mem.Resources()[0].hinted, "a seeded forest is marked as a hint");
    Check(mem.Resources()[0].successes == 0,
          "a hint claims no yield -- nobody knows that without chopping");
    Check(mem.Resources()[0].label == "Yew woods", "the hint keeps its atlas name");

    Check(mem.BestProvenResource("logs", 650, 820, t0) == nullptr,
          "nothing is proven yet, so there is no proven stand");
    const life::KnownResourceSource* lead = mem.BestHint("logs", 650, 820, t0);
    Check(lead != nullptr && lead->label == "Yew woods",
          "with nothing proven, the NEAREST lead is chosen");

    // Working an area dry charges the LEAD, so the next trip picks elsewhere.
    mem.NoteResource("logs", 664, 1030, 0, false, t0);
    const life::KnownResourceSource* next = mem.BestHint("logs", 650, 820, t0);
    Check(next != nullptr && next->label == "Britain Territory woods",
          "a disappointing lead drops below a farther untried one");

    // A real yield creates a proven stand, which then outranks every lead.
    mem.NoteResource("logs", 1420, 1290, 0, true, t0 + 10);
    const life::KnownResourceSource* proven =
        mem.BestProvenResource("logs", 650, 820, t0 + 10);
    Check(proven != nullptr && proven->successes == 1,
          "a chop that yielded creates a proven stand");
    Check(!proven->hinted || proven->successes > 0,
          "proven-ness comes from successes, never from being seeded");

    // Seeding is idempotent and never overwrites what was earned.
    mem.HintResource("logs", "Britain Territory woods", 1416, 1288, 0, t0 + 20);
    Check(mem.Resources().size() == 2, "re-seeding the same forest adds nothing");
}

void TestDangerHeatIsCapped() {
    Section("memory: fear is bounded");

    life::Memory mem;
    const i64 t0 = 1000;
    // The live failure: a twenty-minute fight added heat on every tick and
    // reached 499.89, which drove the character's own profession negative.
    for (int i = 0; i < 1000; ++i) mem.NoteDanger(600, 600, 12, "grey wolf", 0.5, t0);
    const double heat = mem.DangerHeatAt(600, 600, t0);
    Check(heat <= life::kMaxDangerHeat + 0.001,
          "a thousand scares at one spot cannot exceed the cap");
    Check(heat > 1.0, "but repeated trouble still reads as much worse than one scare");
}

void TestCreatureMemory() {
    Section("memory: per-creature verdicts are learned, decay, and are bounded");

    life::Memory mem;
    const i64 t0 = 1000000;

    // Unknown creature: no verdict, no opinion.
    Check(mem.CreatureDanger("a lich", t0) == 0.0,
          "a creature never fought reads as unknown, not safe or dangerous");

    // --- danger accumulates and decays exactly like DangerMemory ----------
    mem.NoteCreatureOutcome("a lich", life::kCreatureEvidenceDeath, t0);
    const double atOnce = mem.CreatureDanger("a lich", t0);
    const double oneHalfLife =
        mem.CreatureDanger("a lich", t0 + life::kDangerHalfLifeMs);
    const double twoHalfLives =
        mem.CreatureDanger("a lich", t0 + 2 * life::kDangerHalfLifeMs);
    Check(atOnce > 1.99 && atOnce < 2.01, "a death reads at full evidence weight");
    Check(atOnce > 0.0, "a death is POSITIVE evidence -- dangerous");
    Check(oneHalfLife > atOnce * 0.49 && oneHalfLife < atOnce * 0.51,
          "one half-life halves the verdict, same shape as DangerMemory");
    Check(twoHalfLives > atOnce * 0.24 && twoHalfLives < atOnce * 0.26,
          "decay is exponential");

    // A different creature TYPE is unaffected -- this is per-creature, not
    // positional or global.
    Check(mem.CreatureDanger("a rat", t0) == 0.0,
          "learning about a lich says nothing about a rat");

    // --- safety accumulates too, and moves the verdict the OTHER way ------
    life::Memory clean;
    clean.NoteCreatureOutcome("a rat", life::kCreatureEvidenceCheapKill, t0);
    const double ratVerdict = clean.CreatureDanger("a rat", t0);
    Check(ratVerdict < 0.0, "a cheap kill is NEGATIVE evidence -- it reads as safe");

    // --- repeated trouble compounds ONTO the decayed value ----------------
    mem.NoteCreatureOutcome("a lich", life::kCreatureEvidenceDeath,
                            t0 + life::kDangerHalfLifeMs);
    const double compounded = mem.CreatureDanger("a lich", t0 + life::kDangerHalfLifeMs);
    Check(compounded > atOnce, "a second death compounds onto the decayed verdict");

    // --- bounded, both directions ------------------------------------------
    life::Memory grudge;
    for (int i = 0; i < 100; ++i) {
        grudge.NoteCreatureOutcome("an ogre", life::kCreatureEvidenceDeath, t0);
    }
    Check(grudge.CreatureDanger("an ogre", t0) <= life::kMaxDangerHeat + 0.001,
          "the dangerous side is capped, exactly like DangerMemory's heat");

    life::Memory confidence;
    for (int i = 0; i < 100; ++i) {
        confidence.NoteCreatureOutcome("a chicken", life::kCreatureEvidenceCheapKill, t0);
    }
    Check(confidence.CreatureDanger("a chicken", t0) >= -life::kMaxDangerHeat - 0.001,
          "the safe side is capped too -- confidence is bounded, not infinite");

    // --- a costly kill and a near-death flee are both dangerous evidence,
    // but weaker than an outright death ------------------------------------
    life::Memory costly;
    costly.NoteCreatureOutcome("a troll", life::kCreatureEvidenceCostlyKill, t0);
    Check(costly.CreatureDanger("a troll", t0) < 0.0,
          "a costly kill is still net evidence of safety -- we won");

    life::Memory fled;
    fled.NoteCreatureOutcome("a troll", life::kCreatureEvidenceNearDeathFlee, t0);
    Check(fled.CreatureDanger("a troll", t0) > 0.0,
          "fleeing near death is evidence of danger, even without dying");
    Check(fled.CreatureDanger("a troll", t0) < life::kCreatureEvidenceDeath,
          "but a flee is not treated as harshly as an actual death");
}

void TestSchemaV1StillLoads() {
    Section("persistence: a v1 file loads under the v2 reader");

    // Exactly the shape v1 wrote: a resource with no `hinted` and no `label`.
    const std::string v1 =
        "{\"schema_version\":1,"
        "\"identity\":{\"identity_id\":\"acc.char\",\"character_name\":\"Tarath\"},"
        "\"memory\":{\"resources\":[{\"resource\":\"logs\",\"x\":649,\"y\":820,"
        "\"successes\":3,\"failures\":1}]}}";
    json::ParseError perr;
    const json::Value v = json::Parse(v1, &perr);
    Check(!perr.failed, "the v1 document parses");

    life::PersistentState st;
    std::string err;
    Check(life::FromJson(v, &st, &err), "a v1 state file loads under the v2 reader");
    Check(st.identity.characterName == "Tarath", "identity survives the migration");
    Check(st.memory.Resources().size() == 1, "its resources survive");
    Check(st.memory.Resources()[0].successes == 3, "and their earned counts survive");
    Check(!st.memory.Resources()[0].hinted,
          "an absent `hinted` defaults to false -- v1 recorded only observation, "
          "never seeded knowledge");
    Check(st.memory.Resources()[0].label.empty(), "an absent label defaults empty");
    Check(st.memory.Creatures().empty(),
          "a field this file predates -- per-creature verdicts -- defaults to "
          "empty rather than failing the load");
}

void TestIdentityId() {
    Section("identity: filesystem-safe ids");
    Check(life::MakeIdentityId("RevolutionLumber01", "Balthasar") ==
              "revolutionlumber01.balthasar",
          "an identity id is lowercase account.character");
    const std::string weird = life::MakeIdentityId("acc/../evil", "na me");
    Check(weird.find('/') == std::string::npos && weird.find("..") == std::string::npos,
          "path separators and traversal cannot reach the filesystem");
    Check(weird.find(' ') == std::string::npos, "spaces are replaced");
}

// --------------------------------------------------------------------------
// M7: a surplus is a reason to go to town ONLY when somebody buys it.
void TestSurplusNeedsSomewhereToGo() {
    Section("needs: a surplus with no buyer is blocked, not a goal");

    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    const prof::Profession* mg = prof::Find("scribe");
    Check(lj && mg, "the two lives exist");
    if (!lj || !mg) return;

    life::Memory mem;

    auto needsFor = [&](const prof::Profession& p, const char* item, i32 qty, i32 gold = 1000) {
        life::NeedConfig cfg;
        cfg.profession = &p;
        life::BuildPlan plan = life::PlanFromProfession(p);
        life::Observation obs;
        obs.inWorld = true;
        obs.hp = obs.hpMax = 25;
        obs.gold = gold;
        obs.weight = 10; obs.maxWeight = 500;   // nowhere near encumbered
        obs.pack.push_back({item, qty});
        obs.axeEquipped = true;
        return life::AssessNeeds(plan, mem, obs, cfg);
    };

    auto sellNeed = [](const std::vector<life::Need>& needs) -> const life::Need* {
        for (const life::Need& n : needs) {
            if (n.kind == life::NeedKind::NeedGold && n.what == "sell surplus") {
                return &n;
            }
        }
        return nullptr;
    };

    // A LUMBERJACK. On Revolution logs go to players, never to an NPC, so
    // after the sell-policy correction this life has no NPC route at all. The
    // need must still be REPORTED -- the character really is carrying goods it
    // cannot move -- but BLOCKED, or the goal wins the scoring, discovers on
    // entry that nothing buys logs, completes with progress 0, and is re-picked
    // two seconds later. That is the exhausted-area churn in a second costume.
    const std::vector<life::Need> woodcutter = needsFor(*lj, "i_log", 40);
    const life::Need* wood = sellNeed(woodcutter);
    Check(wood != nullptr, "the surplus is still reported, not silently dropped");
    if (wood) {
        Check(wood->blocked, "and BLOCKED, because nothing buys a log");
        Check(!wood->evidence.empty(), "with the reason spelled out");
    }

    // A MAGE. Scrolls are one of the three taps, so this one does have a
    // route, and the urgency has to grow with the load or it never outranks
    // working -- flat, it lost to gathering every single time.
    //
    // The vectors are NAMED. Passing needsFor(...) straight into sellNeed()
    // binds a pointer into a temporary that dies at the end of the full
    // expression, and the resulting read is garbage that happens to look like
    // a blocked need. This project has been bitten by exactly that once
    // before, in M4.
    const std::vector<life::Need> fewNeeds  = needsFor(*mg, "i_scroll_poison", 6);
    const auto tinyNeeds = needsFor(*mg, "i_scroll_poison", 2);
    Check(sellNeed(tinyNeeds) == nullptr,
          "bank-funded scribe does not make a tiny sale because pack coin is zero");
    const auto poorNeeds = needsFor(*mg, "i_scroll_poison", 2, 100);
    Check(sellNeed(poorNeeds) != nullptr,
          "poor scribe can still sell a tiny lot to fund supplies");
    const std::vector<life::Need> manyNeeds = needsFor(*mg, "i_scroll_poison", 40);
    const life::Need* few  = sellNeed(fewNeeds);
    const life::Need* many = sellNeed(manyNeeds);
    Check(few && many, "a scribe's scrolls are a surplus at both sizes");
    if (few && many) {
        Check(!few->blocked && !many->blocked,
              "and NOT blocked -- the mage shop buys scrolls back");
        Check(many->urgency > few->urgency,
              "urgency grows with the size of the load");
    }
}

// --------------------------------------------------------------------------
// A need with no legitimate route must be BLOCKED, not selected and then
// discovered impossible inside the goal body.
void TestUnsatisfiableNeedIsBlocked() {
    Section("needs: bandages with no route are blocked, not chosen");

    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    if (!lj) { Check(false, "no lumberjack"); return; }

    life::NeedConfig cfg;
    cfg.profession = lj;
    life::BuildPlan plan = life::PlanFromProfession(*lj);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 25;
    obs.gold = 1000;          // rich, and it still cannot buy one
    obs.bandages = 0;
    obs.axeEquipped = true;
    obs.weight = 10; obs.maxWeight = 500;

    // UPDATED 2026-08-30. This used to assert the OPPOSITE, and the comment
    // explained why: i_bandage was not in the vendor matrix at all, so it
    // graded UNKNOWN and the policy refused it, and the test pinned that
    // refusal in place as correct behaviour.
    //
    // It was not correct, it was a table gap -- the same one as i_kindling and
    // i_bottle_empty. VENDOR_S_HEALER_SHOP (tm_vend.scp:1072, {5 20}) and
    // VENDOR_S_VET (:509, {6 66}) both sell bandages outright, and the whole
    // buy path in DoReplaceEquipment was already wired and waiting. A fencer
    // could not fight for want of a row in a table, and this test was part of
    // what kept it that way: it would have failed the moment anyone fixed it.
    //
    // So the expectation is inverted: with gold in the purse and a real seller
    // in the world, wanting bandages is an ERRAND, not a blocked state.
    const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, obs, cfg);
    bool found = false;
    for (const life::Need& n : needs) {
        if (n.kind != life::NeedKind::NeedEquipment) continue;
        if (n.what != "bandages") continue;
        found = true;
        Check(!n.blocked,
              "a purse full of gold and a healer who sells bandages makes "
              "this an errand, not a blocked state");
        Check(!n.evidence.empty(), "and says why, so the reasoning is legible");
    }
    Check(found, "the bandage need is still reported, not silently dropped");

    // With the need blocked, real work wins.
    life::Planner planner;
    const std::vector<life::ScoredGoal> goals = planner.Score(needs, obs, mem);
    for (const life::ScoredGoal& g : goals) {
        if (g.kind != life::GoalKind::ReplaceEquipment) continue;
        Check(g.feasible, "REPLACE_EQUIPMENT is a feasible goal here");
    }
}


// --------------------------------------------------------------------------
// A life must ask for ITS OWN tools. Five separate behaviours in this codebase
// were written for the lumberjack and silently did nothing for anybody else;
// this is the one that left a fisher standing beside a lake with no pole,
// never generating a tool need and so never getting a GET_TOOL goal.
void TestEveryLifeAsksForItsOwnTools() {
    Section("needs: each profession asks for the tools IT needs");

    life::Memory mem;
    for (const prof::Profession& p : prof::All()) {
        if (p.tools.empty()) continue;

        life::NeedConfig cfg;
        cfg.profession = &p;
        life::BuildPlan plan = life::PlanFromProfession(p);

        life::Observation obs;
        obs.inWorld = true;
        obs.hp = obs.hpMax = 25;
        obs.gold = 1000;                 // rich, so nothing is blocked on cost
        obs.weight = 10; obs.maxWeight = 500;
        // obs.toolsHeld deliberately EMPTY: this life owns nothing yet.

        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, mem, obs, cfg);

        for (const prof::ToolNeed& t : p.tools) {
            bool asked = false;
            for (const life::Need& n : needs) {
                if (n.kind == life::NeedKind::NeedTool && n.what == t.name) {
                    asked = true;
                    if (n.blocked) {
                        std::printf("  FAIL: %s asks for %s but it is blocked "
                                    "with 1000gp in hand\n",
                                    p.id.c_str(), t.name.c_str());
                        ++g_failures;
                    }
                    ++g_checks;
                }
            }
            if (!asked) {
                std::printf("  FAIL: %s never asks for its %s\n",
                            p.id.c_str(), t.name.c_str());
                ++g_failures;
            }
            ++g_checks;
        }

        // ...and stops asking once it has one.
        life::Observation armed = obs;
        for (const prof::ToolNeed& t : p.tools) armed.toolsHeld.push_back(t.name);
        const std::vector<life::Need> after =
            life::AssessNeeds(plan, mem, armed, cfg);
        for (const life::Need& n : after) {
            if (n.kind != life::NeedKind::NeedTool) continue;
            std::printf("  FAIL: %s still wants a %s while holding it\n",
                        p.id.c_str(), n.what.c_str());
            ++g_failures;
        }
        ++g_checks;
    }

    // Every tool must have at least one graphic, or Observe can never find it
    // and the character asks forever for something it already owns.
    for (const prof::Profession& p : prof::All()) {
        for (const prof::ToolNeed& t : p.tools) {
            if (t.graphics.empty()) {
                std::printf("  FAIL: %s tool %s has no graphics\n",
                            p.id.c_str(), t.name.c_str());
                ++g_failures;
            }
            ++g_checks;
        }
    }
}


// --------------------------------------------------------------------------
// riskTolerance was dead data: every profession carried a distinct value and
// nothing read it, so a fisher and a swordsman fled at the same threshold.
// --------------------------------------------------------------------------
// A GOAL THAT ACHIEVED NOTHING MUST NOT BE RE-PICKED ON THE NEXT TICK.
//
// Ysolde the scribe, run_m5/pair2: STR 10, carry limit 75 stones, and a
// starting kit of 73 -- two chainmail coifs, two books, a candle and three
// cast scrolls, none of which the old DoBank was allowed to deposit. She
// stood at an open bank box and logged
//
//   goal=BANK ... weight=73/75 (97%)
//   goal_completed=BANK progress=0
//   checkpoint (goal completed) -> state.json
//
// every 60 ms for five straight minutes, fsyncing state.json each lap. The
// commitment floor does not catch this: it governs transitions away from a
// RUNNING goal, and Finish() had already cleared `active`. So the goal has to
// be able to stand itself down, and the need must still be REPORTED while it
// does -- "why isn't it banking" stays answerable.
void TestGoalCooldownStopsChurn() {
    Section("planner: a goal that did nothing can stand itself down");

    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    if (!lj) { Check(false, "no lumberjack"); return; }

    life::NeedConfig cfg;
    cfg.profession = lj;
    life::BuildPlan plan = life::PlanFromProfession(*lj);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 25;
    obs.gold = 500;
    obs.axeEquipped = true;
    obs.nowMs = 1000;
    // Fully equipped, so the housekeeping need is the top one: this test is
    // about the churn, not about what outranks it.
    for (const prof::ToolNeed& t : lj->tools) obs.toolsHeld.push_back(t.name);
    obs.bandages = 10;
    obs.weaponEquipped = true;
    // At the weight line with nothing sellable: exactly the shape that scores
    // NeedBank and nothing above it.
    obs.weight = 73; obs.maxWeight = 75;

    const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, obs, cfg);

    life::Planner planner;
    std::string why;
    Check(planner.Select(needs, obs, mem, obs.nowMs, &why),
          "a full pack picks a goal to begin with");
    Check(planner.Current().kind == life::GoalKind::Bank,
          "and the goal it picks is BANK");

    // The visit deposited nothing. Stand down for five minutes and fail.
    planner.Cooldown(life::GoalKind::Bank, obs.nowMs + 5 * 60 * 1000);
    planner.Finish(false, "nothing to deposit", obs.nowMs);
    Check(planner.Cooling(life::GoalKind::Bank, obs.nowMs),
          "BANK is cooling immediately after standing down");

    // 60 ms later -- the interval the live churn actually ran at.
    obs.nowMs += 60;
    const std::vector<life::ScoredGoal> cooling = planner.Score(needs, obs, mem);
    bool reported = false;
    for (const life::ScoredGoal& g : cooling) {
        if (g.kind != life::GoalKind::Bank) continue;
        reported = true;
        Check(!g.feasible, "BANK is not feasible again 60 ms later");
        Check(!g.blockedWhy.empty(), "and it says why it is standing down");
    }
    Check(reported, "the BANK goal is still REPORTED, not silently dropped");

    planner.Select(needs, obs, mem, obs.nowMs, &why);
    Check(planner.Current().kind != life::GoalKind::Bank,
          "so the very next decision is something other than BANK");

    // And it comes back on its own once the rest is served.
    obs.nowMs += 5 * 60 * 1000;
    Check(!planner.Cooling(life::GoalKind::Bank, obs.nowMs),
          "the stand-down expires; banking is not disabled forever");

    // A second, shorter cooldown must not shorten one already running.
    planner.Cooldown(life::GoalKind::Bank, obs.nowMs + 60000);
    planner.Cooldown(life::GoalKind::Bank, obs.nowMs + 1000);
    Check(planner.Cooling(life::GoalKind::Bank, obs.nowMs + 30000),
          "the longer rest wins when two callers cool the same goal");
}


// --------------------------------------------------------------------------
// ONE TRAINER'S REFUSAL IS NOT THE TRADE'S ANSWER.
//
// Source-X caps what an NPC may teach at min(THAT NPC's own skill x
// NPCTrainPercent, NPCTrainMax, the student's cap) -- CCharNPCStatus.cpp:514.
// The ceiling is therefore a property of the individual, so two mages cap in
// different places. Recording the refusal against the TRADE made every mage in
// Britain one mage: Alenne stopped teaching Ysolde Meditation at 21.9 (which
// puts Alenne's own Meditation near 73.0), the whole "mage" trade was written
// off for good, and with Inscription and Magery both already past the generic
// 30.0 ceiling the character was left with nothing any trainer could sell her.
// run_m5/p0gate2 logged it as `want_train=nothing` while she stood on 205 gold
// she had just earned.
void TestOneTrainerIsNotTheTrade() {
    Section("memory: one trainer's refusal is not the whole trade's");

    life::Memory mem;
    auto refuse = [&mem](u32 npc, i32 at) {
        life::TrainerVerdict v;
        v.skillId   = rules::kMeditation;
        v.trade     = "mage";
        v.npcSerial = npc;
        v.taught    = false;
        v.atTenths  = at;
        v.why       = "the trainer has nothing left to give";
        v.whenMs    = 1000;
        mem.NoteTrainerVerdict(v);
    };

    refuse(0x1111, 219);
    Check(mem.TrainerRefusedByNpc(rules::kMeditation, 0x1111),
          "the NPC that refused is remembered by serial");
    Check(!mem.TrainerRefusedByNpc(rules::kMeditation, 0x2222),
          "a mage who was never asked has not refused anything");
    Check(!mem.TrainerRefused(rules::kMeditation, "mage"),
          "one refusal does NOT write off the trade");
    Check(mem.TrainersWhoRefused(rules::kMeditation, "mage").size() == 1,
          "and the skip list names exactly the one who said no");

    // A second answer from the SAME mouth is the same one refusal.
    refuse(0x1111, 219);
    Check(mem.TrainersWhoRefused(rules::kMeditation, "mage").size() == 1,
          "asking the same NPC twice is still one refusal, not two");
    Check(!mem.TrainerRefused(rules::kMeditation, "mage"),
          "so it cannot exhaust the trade on its own");

    refuse(0x2222, 219);
    Check(!mem.TrainerRefused(rules::kMeditation, "mage"),
          "two different mages is still not enough to give up on mages");

    refuse(0x3333, 219);
    Check(mem.TrainerRefused(rules::kMeditation, "mage"),
          "three different mages refusing DOES exhaust the trade");
    Check(mem.TrainersWhoRefused(rules::kMeditation, "mage").size() == 3,
          "and all three are on the skip list");

    // A refusal about one skill says nothing about another.
    Check(!mem.TrainerRefused(rules::kMagery, "mage"),
          "refusing Meditation is not refusing Magery");

    // The character must still WANT the skill after a single refusal --
    // this is the half that actually unblocks the earn-then-train gate.
    const prof::Profession* scribe = prof::Find("scribe");
    if (!scribe) { Check(false, "no scribe"); return; }
    life::BuildPlan plan = life::PlanFromProfession(*scribe);
    life::Observation obs;
    obs.inWorld = true;
    obs.skills.push_back({rules::kInscription, 500});
    obs.skills.push_back({rules::kMagery,      500});
    obs.skills.push_back({rules::kMeditation,  219});

    Check(life::NextSkillToBuy(plan, obs, 300) == rules::kMeditation,
          "with nobody refused, the scribe wants to buy Meditation");

    // The old behaviour, restated as the bug it was: mark the skill refused
    // outright and the character wants nothing at all.
    obs.trainerRefusedSkills.push_back(rules::kMeditation);
    Check(life::NextSkillToBuy(plan, obs, 300) == -1,
          "and a skill the whole trade has refused is correctly dropped");
}


// --------------------------------------------------------------------------
// A LIFE IS NOT ONE ERRAND REPEATED.
//
// The owner's rule for this project: a character should "sometimes train,
// sometimes make money, sometimes sell, sometimes PvM, socialise in between",
// and goods that found no buyer simply going into the bank is a fine outcome
// rather than something to retry. Pure scoring cannot produce that -- it picks
// the same winner every tick until its need is gone, which is how a scribe
// logged 4,717 BANK goals in twenty minutes.
//
// Satiation is the smallest thing that breaks the monotony: a goal that keeps
// winning gets progressively less attractive WHILE IT IS FRESH, so the
// runner-up gets a turn. It is not a ban -- it decays with time, it clears the
// moment another goal runs, and it never touches an emergency.
void TestSatiationLetsSomethingElseHaveATurn() {
    Section("planner: a goal that keeps winning eases off");

    life::Planner planner;
    const i64 t0 = 1000;

    Check(planner.Satiation(life::GoalKind::Bank, t0) == 0.0,
          "a goal that has never run is not satiated");

    planner.NoteRan(life::GoalKind::Bank, t0);
    Check(planner.Satiation(life::GoalKind::Bank, t0) == 0.0,
          "running ONCE is not repetition -- an errand may finish in peace");

    planner.NoteRan(life::GoalKind::Bank, t0);
    const double twice = planner.Satiation(life::GoalKind::Bank, t0);
    planner.NoteRan(life::GoalKind::Bank, t0);
    const double thrice = planner.Satiation(life::GoalKind::Bank, t0);
    Check(twice > 0.0, "a second run in a row starts to ease off");
    Check(thrice > twice, "and a third eases off further");
    Check(thrice < 1.0, "but it never zeroes the goal outright");

    // The damping is bounded, however stubborn the character is.
    for (int i = 0; i < 40; ++i) planner.NoteRan(life::GoalKind::Bank, t0);
    Check(planner.Satiation(life::GoalKind::Bank, t0) <= 0.45 + 1e-9,
          "the easing off is capped, so a real need can still win through");

    // It FADES. A character that banked a lot ten minutes ago is perfectly
    // happy to bank again -- this is satiation, not a grudge.
    Check(planner.Satiation(life::GoalKind::Bank, t0 + 3 * 60 * 1000) == 0.0,
          "and it has worn off entirely once the goal is no longer fresh");

    // Doing something ELSE clears the streak: the point is variety, not a
    // permanent tax on whatever the character happens to be good at.
    life::Planner p2;
    for (int i = 0; i < 5; ++i) p2.NoteRan(life::GoalKind::Bank, t0);
    Check(p2.Satiation(life::GoalKind::Bank, t0) > 0.0, "streak is running");
    p2.NoteRan(life::GoalKind::EarnGold, t0);
    Check(p2.Satiation(life::GoalKind::Bank, t0) == 0.0,
          "one turn at something else and banking is welcome again");

    // AN EMERGENCY IS NEVER DAMPED. A character does not get bored of not
    // dying, and must not hesitate over a corpse because it died twice today.
    life::Planner p3;
    for (int i = 0; i < 10; ++i) p3.NoteRan(life::GoalKind::Survive, t0);
    Check(p3.Satiation(life::GoalKind::Survive, t0) == 0.0,
          "SURVIVE is never eased off, however often it has just run");
    life::Planner p4;
    for (int i = 0; i < 10; ++i) p4.NoteRan(life::GoalKind::Heal, t0);
    Check(p4.Satiation(life::GoalKind::Heal, t0) == 0.0, "nor is HEAL");
    life::Planner p5;
    for (int i = 0; i < 10; ++i) p5.NoteRan(life::GoalKind::RecoverCorpse, t0);
    Check(p5.Satiation(life::GoalKind::RecoverCorpse, t0) == 0.0,
          "nor is RECOVER_CORPSE");
}


// --------------------------------------------------------------------------
// DAMPING ONE GOAL IS NOT ENOUGH -- THE CROWDING-OUT IS DONE BY A FAMILY.
//
// A crafter alternates BUY_SUPPLIES, CRAFT and EARN_GOLD. It never repeats a
// single goal twice in a row, so per-goal satiation never fires even once --
// and the whole day is still nothing but work. That is exactly
// run_m5/p0gate10: three goals in a ring at 47/33/20%, which reads as variety
// and is not.
//
// The family is what has to yield, and it has to yield HARD. BANK scores
// 240 x 0.72 = 173; TRAIN_COMBAT scores 110 x 0.4 = 44. Nothing short of a
// ~60% haircut on upkeep ever lets a fighter go hunting, which is why no bot
// in this project has ever fought.
void TestFamilySatiationBreaksAMonotonousDay() {
    Section("planner: a whole FAMILY yields, not just one errand");

    Check(life::FamilyOf(life::GoalKind::Craft) == life::GoalFamily::Work,
          "crafting is work");
    Check(life::FamilyOf(life::GoalKind::BuySupplies) == life::GoalFamily::Work,
          "so is buying the inputs for it");
    Check(life::FamilyOf(life::GoalKind::EarnGold) == life::GoalFamily::Work,
          "and so is selling the result -- one family, not three activities");
    Check(life::FamilyOf(life::GoalKind::Bank) == life::GoalFamily::Upkeep,
          "banking is upkeep");
    Check(life::FamilyOf(life::GoalKind::TrainAtNpc) == life::GoalFamily::Training,
          "buying a skill is training");
    Check(life::FamilyOf(life::GoalKind::Survive) == life::GoalFamily::Emergency,
          "staying alive is an emergency");

    // THE CASE THAT MATTERS: alternate three work goals, never repeating one.
    life::Planner p;
    const i64 t0 = 1000;
    p.NoteRan(life::GoalKind::BuySupplies, t0);
    p.NoteRan(life::GoalKind::Craft, t0);
    p.NoteRan(life::GoalKind::EarnGold, t0);
    Check(p.Satiation(life::GoalKind::Craft, t0) == 0.0,
          "no single goal repeated, so per-goal satiation never fires");
    p.NoteRan(life::GoalKind::BuySupplies, t0);
    p.NoteRan(life::GoalKind::Craft, t0);
    const double fam = p.FamilySatiation(life::GoalKind::Craft, t0);
    Check(fam > 0.0,
          "but the WORK family has run five times and must start to yield");
    Check(p.FamilySatiation(life::GoalKind::EarnGold, t0) > 0.0,
          "and it yields for every goal in that family, not just the last one");

    // Hard enough to actually matter against a heavier family.
    life::Planner p2;
    for (int i = 0; i < 30; ++i) p2.NoteRan(life::GoalKind::Bank, t0);
    const double up = p2.FamilySatiation(life::GoalKind::Bank, t0);
    // RAISED 2026-08-29 from 0.60 to 0.85, deliberately, to finish R1. At the
    // old ceiling a family that had just run six times still kept 40% of its
    // score, and against TRAIN_AT_NPC's weight of 150 that was enough to keep
    // winning -- Maribel took 15 of 19 picks in Training and Halric 5 of 6.
    // The damping was real and simply too gentle to change the outcome.
    Check(up >= 0.80,
          "a family that has monopolised the day yields hard -- ~85%, enough "
          "that the turn actually passes to something else");
    Check(up <= 0.85 + 1e-9,
          "but the yielding is BOUNDED. It must never reach 1.0: a family "
          "silenced completely could not come back even when it was the only "
          "sensible thing left to do, and satiation is meant to produce a "
          "rounded day, not an abandoned trade");
    // 240 * 0.72 = 172.8 upkeep, versus 110 * 0.4 = 44 for hunting.
    Check(172.8 * (1.0 - up) < 110.0 * 0.4 + 30.0,
          "which brings upkeep down near where hunting can reach it");

    // A different family clears it -- variety, not a standing tax.
    p2.NoteRan(life::GoalKind::Craft, t0);
    Check(p2.FamilySatiation(life::GoalKind::Bank, t0) == 0.0,
          "one turn at something else and upkeep is welcome again");

    // Emergencies are never damped, at either level.
    life::Planner p3;
    for (int i = 0; i < 20; ++i) p3.NoteRan(life::GoalKind::Survive, t0);
    Check(p3.FamilySatiation(life::GoalKind::Survive, t0) == 0.0,
          "the emergency family is never eased off");

    // And it fades, like the per-goal measure.
    life::Planner p4;
    for (int i = 0; i < 10; ++i) p4.NoteRan(life::GoalKind::Craft, t0);
    Check(p4.FamilySatiation(life::GoalKind::Craft, t0 + 3 * 60 * 1000) == 0.0,
          "a family worked hard an hour ago is not still being punished");
}


// --------------------------------------------------------------------------
// NO SKILL ADVANCES INSIDE A REGION_FLAG_SAFE AREA.
//
// Source-X Skill_Experience refuses to advance ANY skill there
// (docs/REVOLUTION_GAMEPLAY_TRUTH.md 3.2, point 1), and twenty-five regions on
// map 0 carry the flag: every shrine, every jail, Lord British's and
// Blackthorne's castles, the Lycaeum, Empath Abbey, Green Acres, the Moonglow
// zoo.
//
// This is the cruellest of the gates because NOTHING SAYS SO. The server sends
// no message, the spell succeeds, the mana is spent, and the skill simply does
// not move. A bot would happily stand at a shrine -- quiet, safe, no monsters,
// exactly where a sensible character would go to meditate -- and burn an
// entire session for nothing.
void TestNoSkillGainRegionBlocksPractice() {
    Section("needs: practice is pointless where no skill can advance");

    const prof::Profession* mage = prof::Find("scribe");
    if (!mage) { Check(false, "no mage"); return; }

    life::NeedConfig cfg;
    cfg.profession = mage;
    life::BuildPlan plan = life::PlanFromProfession(*mage);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 20;
    obs.mana = 30;                       // plenty to cast with
    obs.gold = 500;
    obs.weight = 10; obs.maxWeight = 200;
    obs.toolsHeld.push_back("spellbook");
    obs.skills.push_back({rules::kMagery,     500});
    obs.skills.push_back({rules::kMeditation, 500});

    auto practiceNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedPractice) return &n;
        return nullptr;
    };

    // Ordinary ground: practice is on.
    obs.inNoGainRegion = false;
    // NAME THE VECTOR. practiceNeed returns a pointer INTO it, and passing
    // the call inline let the temporary die at the end of the expression --
    // the pointer then read freed memory, which happened to report blocked=1
    // and empty strings. A dangling read that looks like a passing check is
    // exactly the kind of "evidence" this project must not accept.
    const std::vector<life::Need> nsOk = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* ok = practiceNeed(nsOk);
    Check(ok != nullptr, "a mage below target wants to practise");
    if (ok) Check(!ok->blocked, "and out in the world it is actionable");

    obs.mana = 0;
    const std::vector<life::Need> nsRest = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* rest = practiceNeed(nsRest);
    Check(rest && !rest->blocked,
          "an exhausted mage can enter practice to recover mana");

    // At a shrine: reported, but blocked, and the reason says why.
    obs.inNoGainRegion = true;
    const std::vector<life::Need> nsNo = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* no = practiceNeed(nsNo);
    Check(no != nullptr,
          "the need is still REPORTED in a no-gain region, not dropped");
    if (no) {
        Check(no->blocked, "but blocked: casting here advances nothing");
        Check(no->reason.find("no skill advances") != std::string::npos,
              "and the reason names the real cause, not 'not enough mana'");
        Check(no->evidence.find("no_gain_region=1") != std::string::npos,
              "with the flag in the evidence so a log can be grepped");
    }

    // Mana can recover during practice; a no-gain region cannot.
    obs.inNoGainRegion = false;
    obs.mana = 0;
    const std::vector<life::Need> nsDry = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* dry = practiceNeed(nsDry);
    if (dry) {
        Check(!dry->blocked, "no mana permits a meditation-and-cast loop");
    }
}


// A mage's book is equipment, and this test exists because a character on this
// shard carried Magery 50.0 and could cast nothing at all: Voris asked for
// Create Food 26 times in one session and was told every time that the spell
// was not in his spellbook. Skill is not capability.
void TestAMageWantsItsBookFilled() {
    Section("needs: a mage with an empty book wants spells");

    const prof::Profession* mage = prof::Find("mage");
    if (!mage) { Check(false, "no mage profession"); return; }

    life::NeedConfig cfg;
    cfg.profession = mage;
    life::BuildPlan plan = life::PlanFromProfession(*mage);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 20;
    obs.mana = 30;
    obs.gold = 500;
    obs.weight = 10; obs.maxWeight = 200;
    obs.skills.push_back({rules::kMagery, 500});

    auto spellNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedSpells) return &n;
        return nullptr;
    };

    // No book at all: the need must say so, because buying a book and buying
    // a scroll are different errands.
    obs.spellbookSerial = 0;
    obs.spellsKnown = 0;
    const std::vector<life::Need> nsNone = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* none = spellNeed(nsNone);
    Check(none != nullptr, "a mage with no spellbook wants one");
    if (none) {
        Check(!none->blocked, "and it is actionable -- shops sell spellbooks");
        Check(none->reason.find("no spellbook") != std::string::npos,
              "the reason distinguishes NO BOOK from an empty one");
        Check(none->evidence.find("book=none") != std::string::npos,
              "and the evidence is greppable");
    }

    // A part-filled book still wants more, but less badly than an empty one.
    obs.spellbookSerial = 0x4001;
    obs.spellsKnown = 4;
    const std::vector<life::Need> nsFew = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* few = spellNeed(nsFew);
    Check(few != nullptr, "four spells is not a finished book");
    obs.spellsKnown = 20;
    const std::vector<life::Need> nsMany = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* many = spellNeed(nsMany);
    Check(many != nullptr, "twenty is still short of the working target");
    if (few && many)
        Check(many->urgency < few->urgency,
              "URGENCY FALLS AS THE BOOK FILLS. The cheap spells go in first "
              "and what remains gets harder to obtain -- circles 7-8 are sold "
              "by nobody on this shard -- so a book that nags harder the "
              "fuller it gets would have it exactly backwards");

    // A FULL PURSE RAISES THE PRIORITY. "mage should also give priority to buy
    // new spells not on the book if economy is good enough" (project owner).
    // Measured against this profession's OWN reserve, so "good enough" means
    // good enough for this life -- a mage holds back 800 for reagents where a
    // lumberjack holds 300 for a trainer.
    obs.spellbookSerial = 0x4001;
    obs.spellsKnown = 12;
    const i32 reserve = mage->goldReserve;

    obs.gold = reserve;                       // nothing spare at all
    const std::vector<life::Need> nsBroke = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* broke = spellNeed(nsBroke);

    obs.gold = reserve + 1000;                // comfortably clear
    const std::vector<life::Need> nsRich = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* rich = spellNeed(nsRich);

    Check(broke != nullptr && rich != nullptr,
          "the same short book is a need either way");
    if (broke && rich) {
        Check(rich->urgency > broke->urgency,
              "spare gold RAISES the priority of buying spells -- every scroll "
              "is a permanent increase in what the character can do, unlike "
              "food or reagents, which are spent again");
        Check(rich->reason.find("spare gold") != std::string::npos,
              "and the reason says so, rather than only citing the shortfall");
    }

    // The reserve is never raided: at exactly the reserve there is no wealth
    // bonus, so this can not pull gold out from under the running costs.
    obs.gold = reserve - 500;
    const std::vector<life::Need> nsUnder = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* under = spellNeed(nsUnder);
    if (under && broke)
        Check(under->urgency <= broke->urgency + 1e-9,
              "below the reserve there is no wealth bonus at all");

    // A working book switches the need off entirely, rather than nagging for
    // spells that cannot be bought at any price.
    obs.spellsKnown = 24;
    const std::vector<life::Need> nsOk = life::AssessNeeds(plan, mem, obs, cfg);
    Check(spellNeed(nsOk) == nullptr,
          "a serviceable book stops asking -- the rest is scribe and dungeon "
          "work, not shopping");

    // And a character with no Magery at all never wants a spellbook.
    life::Observation warrior;
    warrior.inWorld = true;
    warrior.hp = warrior.hpMax = 40;
    warrior.gold = 500;
    warrior.weight = 10; warrior.maxWeight = 200;
    warrior.skills.push_back({rules::kSwordsmanship, 500});
    const prof::Profession* fencer = prof::Find("fencer");
    if (fencer) {
        life::NeedConfig wcfg;
        wcfg.profession = fencer;
        life::BuildPlan wplan = life::PlanFromProfession(*fencer);
        const std::vector<life::Need> nsW =
            life::AssessNeeds(wplan, mem, warrior, wcfg);
        Check(spellNeed(nsW) == nullptr,
              "a fencer with no Magery is never sent shopping for scrolls");
    }
}

// A MAGE MUST NOT SPEND ITS WHOLE DAY ASKING AN EMPTY STREET FOR SCROLLS.
//
// Aurelius's five-minute gate (run_gates/g_Aurelius.console.txt, 2026-09-02)
// went entirely on FILL_SPELLBOOK: 16 mentions, three shop trips, four minutes
// of walking, not one cast and not one scroll. The errand was never wrong -- a
// mage does want a fuller book -- but "this shelf is empty" was answered by
// walking to the next shelf, forever, because the only stand-downs were a
// three-trip budget (which four minutes of travel never exhausts inside a
// session) and a four-minute rest (short enough to re-arm inside the same one).
//
// The rule this test pins: an empty scroll errand rests for a LONG time, and
// each further empty one rests longer, so practice, earning and training get
// the turn. The want itself is untouched -- NeedSpells still fires.
void TestScrollShoppingStandsDownWhenNobodySells() {
    Section("planner: an empty scroll errand yields the rest of the session");

    // --- the rest itself, as arithmetic ----------------------------------
    const i64 first  = life::ScrollShoppingRestMs(1);
    const i64 second = life::ScrollShoppingRestMs(2);
    const i64 third  = life::ScrollShoppingRestMs(3);
    Check(first >= 15 * 60 * 1000,
          "the FIRST empty scroll errand rests at least fifteen minutes -- far "
          "longer than BUY_SUPPLIES's 119 s, because a reagent shelf restocks "
          "and a spell-scroll seller may simply not exist");
    Check(first > 4 * 60 * 1000,
          "and longer than the old four-minute spellbook rest, which re-armed "
          "inside the same session that had just proved the street empty");
    Check(second > first && third > second,
          "REPEATED 'nobody is selling' DOES NOT RE-ARM AT THE SAME RATE: each "
          "empty errand backs off further");
    Check(life::ScrollShoppingRestMs(20) == life::ScrollShoppingRestMs(30) &&
          life::ScrollShoppingRestMs(20) <= 60 * 60 * 1000,
          "the back-off is capped, so the errand is deferred, never disabled");
    Check(life::ScrollShoppingRestMs(0) == first,
          "a nonsense count is treated as the first stand-down, not as zero rest");

    // --- and what it does to the day -------------------------------------
    const prof::Profession* mage = prof::Find("mage");
    if (!mage) { Check(false, "no mage profession"); return; }

    life::NeedConfig cfg;
    cfg.profession = mage;
    life::BuildPlan plan = life::PlanFromProfession(*mage);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 20;
    obs.mana = 30;                       // enough to cast, so practice is live
    obs.gold = mage->goldReserve + 1000; // and rich enough to want scrolls
    obs.weight = 10; obs.maxWeight = 200;
    obs.nowMs = 1000;
    obs.spellbookSerial = 0x4001;
    obs.spellsKnown = 12;
    obs.skills.push_back({rules::kMagery, 500});
    for (const prof::ToolNeed& t : mage->tools) obs.toolsHeld.push_back(t.name);

    const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, obs, cfg);

    auto find = [](const std::vector<life::ScoredGoal>& gs, life::GoalKind k)
        -> const life::ScoredGoal* {
        for (const life::ScoredGoal& g : gs) if (g.kind == k) return &g;
        return nullptr;
    };

    life::Planner planner;
    const std::vector<life::ScoredGoal> before = planner.Score(needs, obs, mem);
    const life::ScoredGoal* fillBefore = find(before, life::GoalKind::FillSpellbook);
    const life::ScoredGoal* practBefore = find(before, life::GoalKind::PracticeSkill);
    Check(fillBefore && fillBefore->feasible,
          "a mage with a short book and spare gold wants to go scroll shopping");
    Check(practBefore && practBefore->feasible,
          "and it also has Magery to practise -- both are on the table");
    const double fillScoreBefore = fillBefore ? fillBefore->score : 0.0;

    // The errand went out and came back empty: nobody within reach stocks a
    // spell this book lacks. Runner::StandDownFromScrollShopping does exactly
    // this pair of calls.
    planner.Cooldown(life::GoalKind::FillSpellbook,
                     obs.nowMs + life::ScrollShoppingRestMs(1));
    planner.Finish(false, "nobody selling scrolls", obs.nowMs);

    obs.nowMs += 60;                     // the interval the churn ran at
    const std::vector<life::ScoredGoal> after = planner.Score(needs, obs, mem);
    const life::ScoredGoal* fillAfter = find(after, life::GoalKind::FillSpellbook);
    const life::ScoredGoal* practAfter = find(after, life::GoalKind::PracticeSkill);
    Check(fillAfter != nullptr,
          "FILL_SPELLBOOK is still REPORTED -- the want did not go away, so the "
          "telemetry must still show it");
    if (fillAfter) {
        Check(!fillAfter->feasible && fillAfter->score < fillScoreBefore,
              "but it scores below what it scored a moment ago and is not "
              "pickable, which is what lets the rest of the life run");
        Check(fillAfter->blockedWhy.find("cooldown") != std::string::npos,
              "and it says it is on cooldown rather than looking unwanted");
    }
    Check(practAfter && practAfter->feasible,
          "PRACTISING MAGERY IS STILL ON, which is the point: the mage stops "
          "shopping and starts casting");

    // The need is untouched -- this is a rest, not a demotion.
    const std::vector<life::Need> stillNeeds =
        life::AssessNeeds(plan, mem, obs, cfg);
    bool wantsSpells = false;
    for (const life::Need& n : stillNeeds)
        if (n.kind == life::NeedKind::NeedSpells) wantsSpells = true;
    Check(wantsSpells,
          "the mage STILL WANTS the scrolls -- nothing global was turned down, "
          "it just stopped asking a street that already answered");

    std::string why;
    planner.Select(needs, obs, mem, obs.nowMs, &why);
    Check(planner.Current().kind != life::GoalKind::FillSpellbook,
          "so the very next decision is something other than scroll shopping");

    // It does not come back four minutes later, which is what the old rest did.
    obs.nowMs += 4 * 60 * 1000;
    Check(planner.Cooling(life::GoalKind::FillSpellbook, obs.nowMs),
          "four minutes on it is still resting -- the old kNoSpellbookCooldown "
          "would have re-armed here and eaten the rest of the session");
    obs.nowMs += 12 * 60 * 1000;
    Check(!planner.Cooling(life::GoalKind::FillSpellbook, obs.nowMs),
          "and sixteen minutes on it is willing to try again: deferred, not "
          "disabled");
}

// THE BACKSTOP FOR A BUG THIS PROJECT KEEPS REDISCOVERING.
//
// Three separate goals have each burned an entire session by completing with
// progress 0 and being handed straight back: GET_TOOL 2,058 times, GET_FOOD
// for whole sessions at 100% of picks, EARN_GOLD 13,111 times at 60ms
// intervals. Every one was fixed at its own call site, and the next goal did
// it again. This test is here so the general guard cannot be removed quietly.
void TestAGoalThatSucceedsAtNothingIsStopped() {
    Section("planner: a goal that keeps succeeding at nothing is spinning");

    // Select() is what normally starts a goal, and it needs a whole need list
    // and observation to do it. What is under test here is the bookkeeping in
    // Finish(), so the goal is placed directly -- the same fields Select sets.
    auto start = [](life::Planner& pl, life::GoalKind k, i64 now) {
        life::GoalState& g = pl.Mutable();
        g.kind = k;
        g.active = true;
        g.startedAtMs = now;
        g.attempts = 0;
        g.progress = 0;
        g.failureReason.clear();
    };

    life::Planner p;
    i64 t = 1000000;

    // A goal that completes having actually DONE something is never punished,
    // however often it runs. This half matters as much as the other: a working
    // errand that got cooled off would be a worse bug than the one guarded.
    for (int i = 0; i < 20; ++i) {
        start(p, life::GoalKind::Bank, t);
        p.NoteProgress();
        p.Finish(true, nullptr, t);
        Check(p.TakeSpinDetected() == life::GoalKind::Count,
              "a goal that made progress is never flagged, however often it runs");
        t += 1000;
    }
    Check(!p.Cooling(life::GoalKind::Bank, t),
          "and it is not cooled off");

    // A goal that says "done" without doing anything gets four free passes and
    // is stopped on the fifth.
    life::GoalKind flagged = life::GoalKind::Count;
    for (int i = 0; i < 5; ++i) {
        start(p, life::GoalKind::EarnGold, t);
        p.Finish(true, nullptr, t);
        const life::GoalKind got = p.TakeSpinDetected();
        if (got != life::GoalKind::Count) flagged = got;
        if (i < 4)
            Check(got == life::GoalKind::Count,
                  "a few no-op completions are tolerated -- some errands really "
                  "do have nothing to do this minute");
        t += 60;   // the observed interval: sixty milliseconds
    }
    Check(flagged == life::GoalKind::EarnGold,
          "the fifth consecutive empty success names the goal that is spinning");
    Check(p.Cooling(life::GoalKind::EarnGold, t),
          "and cools it off, so one broken goal cannot own a whole session");
    Check(p.TakeSpinDetected() == life::GoalKind::Count,
          "reading the flag clears it, so it is reported exactly once");

    // A GOAL THAT FAILS AT SIXTY MILLISECONDS IS THE SAME SPIN.
    //
    // The first version of the guard counted successes only, and the very next
    // live run produced the failure-side instance: 746 x
    // goal_failed=BUY_SUPPLIES "this 'mage' does not stock i_scroll_blank".
    life::Planner f;
    t = 3000000;
    life::GoalKind failFlagged = life::GoalKind::Count;
    for (int i = 0; i < 5; ++i) {
        start(f, life::GoalKind::BuySupplies, t);
        f.Finish(false, "this vendor does not stock it", t);
        const life::GoalKind got = f.TakeSpinDetected();
        if (got != life::GoalKind::Count) failFlagged = got;
        t += 60;
    }
    Check(failFlagged == life::GoalKind::BuySupplies,
          "repeated FAILURE with no progress is caught too -- from the "
          "planner's seat it is the same thing as repeated empty success");
    Check(f.Cooling(life::GoalKind::BuySupplies, t),
          "and that goal is cooled off as well");

    // IDLING IS EXEMPT. Its whole purpose is to achieve nothing, so counting
    // it would flag the one goal that is working as designed -- which it did,
    // three times in one session, before this exemption existed.
    life::Planner idle;
    t = 4000000;
    for (int i = 0; i < 30; ++i) {
        start(idle, life::GoalKind::IdleBriefly, t);
        idle.Finish(true, nullptr, t);
        Check(idle.TakeSpinDetected() == life::GoalKind::Count,
              "idling is never reported as spinning, however long it goes on");
        t += 60;
    }
    Check(!idle.Cooling(life::GoalKind::IdleBriefly, t),
          "and is never cooled off -- it is the fallback when nothing else "
          "can run, so disabling it would leave a character with no goal");

    // Real progress in the middle breaks the streak.
    life::Planner q;
    t = 2000000;
    for (int i = 0; i < 4; ++i) {
        start(q, life::GoalKind::Fish, t);
        q.Finish(true, nullptr, t);
        t += 60;
    }
    start(q, life::GoalKind::Fish, t);
    q.NoteProgress();                       // one real catch
    q.Finish(true, nullptr, t);
    t += 60;
    for (int i = 0; i < 4; ++i) {
        start(q, life::GoalKind::Fish, t);
        q.Finish(true, nullptr, t);
        Check(q.TakeSpinDetected() == life::GoalKind::Count,
              "the streak restarted after real work, so four more empties are "
              "not yet a spin");
        t += 60;
    }
}

// THE DEADLOCK THAT COST KAELEN A WHOLE SESSION.
//
// Hungry so no HP regeneration, wounded so under the 80% hunting bar, no
// bandages so HEAL was blocked, no gold so REPLACE_EQUIPMENT and GET_FOOD both
// stood down. He climbed from 10/32 to 25/32 -- two points short of the bar --
// and idled through 73% of his picks with every other need reporting BLOCKED.
void TestACorneredFighterMayHunt() {
    Section("needs: when resting cannot help, a fighter hunts anyway");

    const prof::Profession* fencer = prof::Find("fencer");
    if (!fencer) { Check(false, "no fencer"); return; }

    life::NeedConfig cfg;
    cfg.profession = fencer;
    life::BuildPlan plan = life::PlanFromProfession(*fencer);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hpMax = 32;
    obs.hp = 25;                      // 78%: Kaelen's exact ceiling
    obs.weight = 10; obs.maxWeight = 200;
    obs.attackersOnMe = 0;
    obs.skills.push_back({rules::kFencing, 171});

    auto combatNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedTraining) return &n;
        return nullptr;
    };

    // Cornered: no bandages, no money, hungry. Hunting is the only door out.
    obs.bandages = 0;
    obs.gold = 0;
    obs.hungry = true;
    const std::vector<life::Need> nsStuck = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* stuck = combatNeed(nsStuck);
    Check(stuck != nullptr, "the combat need is reported");
    if (stuck) {
        Check(!stuck->blocked,
              "a fighter with no bandages, no money and an empty stomach is "
              "NOT blocked at 78% -- resting cannot help, because nothing is "
              "coming to heal or feed him");
        Check(stuck->reason.find("only way out") != std::string::npos,
              "and the reason says why the usual caution is suspended");
    }

    // Any ONE of the three restored, and the ordinary bar applies again:
    // resting genuinely works for that character, so it should wait.
    obs.gold = 500;
    const std::vector<life::Need> nsMoney = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* withMoney = combatNeed(nsMoney);
    if (withMoney)
        Check(withMoney->blocked,
              "with money in the purse the 80% bar is back -- he can buy "
              "bandages and food, so going wounded is recklessness not need");

    obs.gold = 0;
    obs.bandages = 10;
    const std::vector<life::Need> nsBand = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* withBandages = combatNeed(nsBand);
    if (withBandages)
        Check(withBandages->blocked,
              "and with bandages he can heal himself up to the bar first");

    // Healthy is healthy: the exception changes nothing for a fit character.
    obs.bandages = 0;
    obs.hp = 32;
    const std::vector<life::Need> nsFit = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* fit = combatNeed(nsFit);
    if (fit)
        Check(!fit->blocked, "a fighter at full health hunts either way");
}

// "if warrior economy is good then he can buy bandage and potion, otherwise go
// get yourself wool make bandage" (project owner, 2026-08-29).
// THE OWNER'S BANDAGE FLOOR, AND THAT IT IS NOT A GLOBAL.
//
// "every fighting bot carries at least 100 bandages" (project owner,
// 2026-09-05). The numbers used to be two constants in NeedConfig that no
// profession ever touched, so a fencer and a scribe wanted the same thirty.
void TestTheFightingFloorIsAHundred() {
    Section("needs: a fighter's bandage floor is the owner's hundred");

    const prof::Profession* fencer = prof::Find("fencer");
    const prof::Profession* scribe = prof::Find("scribe");
    if (!fencer) { Check(false, "no fencer"); return; }

    life::NeedConfig fighter;
    fighter.profession = fencer;
    life::ResolveConsumableThresholds(fighter, 0);
    Check(fighter.bandageLow >= life::kFighterBandageFloor,
          "a broke fencer still triggers a restock at 100");
    Check(fighter.bandageFull > fighter.bandageLow,
          "the restock total sits above the trigger, or the errand ends "
          "where it starts");

    // DYNAMIC, NOT A SECOND CONSTANT: the total follows the purse.
    life::NeedConfig rich;
    rich.profession = fencer;
    life::ResolveConsumableThresholds(rich, 5000);
    Check(rich.bandageFull > fighter.bandageFull,
          "a full purse buys a deeper pack than an empty one");
    Check(rich.bandageLow == fighter.bandageLow,
          "the floor is the owner's and does not move with money");

    // Idempotent: the planner resolves these every tick.
    life::NeedConfig again = rich;
    life::ResolveConsumableThresholds(again, 5000);
    Check(again.bandageLow == rich.bandageLow &&
              again.bandageFull == rich.bandageFull,
          "resolving twice gives the same answer");

    if (scribe) {
        life::NeedConfig quiet;
        quiet.profession = scribe;
        life::ResolveConsumableThresholds(quiet, 5000);
        Check(quiet.bandageLow < life::kFighterBandageFloor,
              "a life that does not hunt keeps its own smaller numbers");
    }
}

void TestAPoorFighterMakesItsOwnBandages() {
    Section("needs: a broke fighter shears a sheep, a rich one visits a shop");

    const prof::Profession* fencer = prof::Find("fencer");
    if (!fencer) { Check(false, "no fencer"); return; }

    life::NeedConfig cfg;
    cfg.profession = fencer;
    life::BuildPlan plan = life::PlanFromProfession(*fencer);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 32;
    obs.weight = 10; obs.maxWeight = 200;
    obs.bandages = 0;
    obs.skills.push_back({rules::kFencing, 500});

    auto makeNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedMakeBandages) return &n;
        return nullptr;
    };

    // Broke: making them is the only way, so the need is actionable.
    obs.gold = 0;
    const std::vector<life::Need> nsPoor = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* poor = makeNeed(nsPoor);
    Check(poor != nullptr, "a fighter with no bandages considers making them");
    if (poor) {
        Check(!poor->blocked,
              "and with no money it is ACTIONABLE -- a sheep costs nothing, "
              "which is the whole point of the goal");
        Check(poor->reason.find("no money") != std::string::npos,
              "the reason names poverty, not a missing shop");
    }

    // Money in the purse: buying is faster, so this branch stands aside.
    obs.gold = 500;
    const std::vector<life::Need> nsRich = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* rich = makeNeed(nsRich);
    if (rich)
        Check(rich->blocked,
              "with money it is BLOCKED -- shearing, spinning, weaving and "
              "cutting is a poor character's errand, not a rich one's");

    // A life that does not fight is never sent to a pasture for bandages.
    const prof::Profession* scribe = prof::Find("scribe");
    if (scribe) {
        life::NeedConfig scfg;
        scfg.profession = scribe;
        life::BuildPlan splan = life::PlanFromProfession(*scribe);
        life::Observation so = obs;
        so.gold = 0;
        so.bandages = 0;
        so.skills.clear();
        so.skills.push_back({rules::kInscription, 500});
        const std::vector<life::Need> nsS =
            life::AssessNeeds(splan, mem, so, scfg);
        Check(makeNeed(nsS) == nullptr,
              "a scribe with no bandages is not in danger and is not sent "
              "shearing -- only lives that actually fight are");
    }

    // Stocked up: nothing to do.
    obs.gold = 0;
    obs.bandages = 30;
    const std::vector<life::Need> nsFull = life::AssessNeeds(plan, mem, obs, cfg);
    Check(makeNeed(nsFull) == nullptr,
          "a fighter already carrying bandages does not go looking for sheep");
}

// "bots shouldnt be idle unless its state specifically" (project owner,
// 2026-08-29). Idling was winning 73-85% of picks on some characters because
// every other goal was blocked and the no-op was the only thing that scored.
void TestExploringBeatsStandingStill() {
    Section("planner: with nothing to do, a bot goes somewhere new");

    life::Planner p;
    life::Memory mem;
    life::Observation obs;
    obs.inWorld = true;
    obs.nowMs = 1000000;
    obs.hp = obs.hpMax = 40;
    obs.weight = 10; obs.maxWeight = 200;

    // No needs at all: the worst case, and the one that produced the idling.
    const std::vector<life::Need> none;
    const std::vector<life::ScoredGoal> scored = p.Score(none, obs, mem);

    const life::ScoredGoal* explore = nullptr;
    const life::ScoredGoal* idle = nullptr;
    for (const life::ScoredGoal& g : scored) {
        if (g.kind == life::GoalKind::Explore)     explore = &g;
        if (g.kind == life::GoalKind::IdleBriefly) idle    = &g;
    }
    Check(explore != nullptr, "exploring is always on the table");
    Check(idle != nullptr,
          "and so is idling -- there must never be NO goal at all");
    if (explore && idle) {
        Check(explore->feasible && idle->feasible,
              "both fallbacks are feasible; the question is which wins");
        Check(explore->score > idle->score,
              "GOING SOMEWHERE NEW BEATS STANDING STILL. Almost every blocked "
              "need in this project is blocked for want of knowing where "
              "something is, so exploring is the useful answer, not filler");
    }
    Check(!scored.empty() && scored.front().kind != life::GoalKind::IdleBriefly,
          "idling is never the top choice when nothing else scores");

    // And exploring belongs to Wander, so family satiation still damps it --
    // a character must not spend a whole session sightseeing either.
    Check(life::FamilyOf(life::GoalKind::Explore) == life::GoalFamily::Wander,
          "exploring is Wander, so the family damping that stops one kind of "
          "thing owning a day applies to it too");
}

// "always try to wear better equipment based on your class" (project owner,
// 2026-08-29). On this shard the class rule is absolute, not a preference:
// revolutionuo.net states that characters wearing ore-smithed metal sets
// "buyu atamazlar" -- cannot cast at all.
void TestGearIsCheckedAndClassBound() {
    Section("needs: every life keeps an eye on its gear");

    const prof::Profession* fencer = prof::Find("fencer");
    const prof::Profession* mage   = prof::Find("mage");
    if (!fencer || !mage) { Check(false, "missing professions"); return; }

    life::Memory mem;
    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 40;
    obs.str = 60;
    obs.gold = 1000;
    obs.bandages = 30;
    obs.weight = 10; obs.maxWeight = 200;

    auto gearNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedGear) return &n;
        return nullptr;
    };

    life::NeedConfig fcfg; fcfg.profession = fencer;
    life::BuildPlan fplan = life::PlanFromProfession(*fencer);
    const std::vector<life::Need> nsF = life::AssessNeeds(fplan, mem, obs, fcfg);
    const life::Need* gf = gearNeed(nsF);
    Check(gf != nullptr,
          "a fighter always has gear on its mind -- loot arrives all life "
          "long and nothing looks at it unless this need exists");
    if (gf) {
        Check(!gf->blocked, "and it is actionable");
        Check(gf->urgency < 0.5,
              "but modestly, because armour is an improvement and must never "
              "outrank eating or bandages");
    }

    // ...AND NOT FOR A LIFE THAT DOES NOT PICK FIGHTS.
    //
    // This used to assert the opposite ("a mage checks its gear as well"), on
    // the theory that the GOAL would sort out what a caster may wear. It does
    // not: DoUpgradeGear's first question is WantsToHunt, and for anything
    // that answers no it logs one line and Finish(true)es having done nothing
    // ("for crafter upgrade gear just wear normal clothing for now", project
    // owner, 2026-08-29). A need whose goal can only ever no-op is a spin
    // generator, and Ilyandra -- a mage -- ran exactly that loop until the
    // anti-spin backstop cooled UPGRADE_GEAR, then ran it again
    // (run_r4/w_Ilyandra.console.txt:742-758).
    //
    // So the need now asks the same question the goal asks. WantsToHunt reads
    // the BUILD, not the archetype name: it is true for a life that wants more
    // than creation's 50.0 in a weapon school, which a pure mage does not.
    life::NeedConfig mcfg; mcfg.profession = mage;
    life::BuildPlan mplan = life::PlanFromProfession(*mage);
    life::Observation mobs = obs;
    mobs.skills.push_back({rules::kMagery, 500});
    const std::vector<life::Need> nsM = life::AssessNeeds(mplan, mem, mobs, mcfg);
    Check(!life::WantsToHunt(*mage),
          "a pure mage is not a life that goes looking for fights");
    Check(gearNeed(nsM) == nullptr,
          "so it raises no gear need -- the need and DoUpgradeGear must agree, "
          "or the goal is picked only to no-op");
}

void TestNerveIsPerProfession() {
    Section("needs: a cautious life bails earlier than a bold one");

    const prof::Profession* sword = prof::Find("lumberjack_swordsman");
    const prof::Profession* fish  = prof::Find("fisher");
    Check(sword && fish, "the bold and the cautious both exist");
    if (!sword || !fish) return;
    Check(fish->riskTolerance < sword->riskTolerance,
          "the catalogue really does rate them differently");

    life::Memory mem;
    auto bailUrgency = [&](const prof::Profession& p, double hpFrac) {
        life::NeedConfig cfg;
        cfg.profession = &p;
        life::BuildPlan plan = life::PlanFromProfession(p);
        life::Observation obs;
        obs.inWorld = true;
        obs.hpMax = 100;
        obs.hp = static_cast<i32>(hpFrac * 100);
        obs.underAttack = true;
        obs.attackersOnMe = 1;
        obs.weight = 10; obs.maxWeight = 500;
        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, mem, obs, cfg);
        for (const life::Need& n : needs) {
            if (n.kind == life::NeedKind::StayAlive) return n.urgency;
        }
        return -1.0;
    };

    // 40% sits BETWEEN the two thresholds -- the fisher bails at 0.44, the
    // swordsman at 0.30 -- so this is the health where they genuinely differ.
    // 45% was above both and produced identical urgency, which is correct
    // behaviour and a useless test.
    const double fisherAt40 = bailUrgency(*fish, 0.40);
    const double swordAt40  = bailUrgency(*sword, 0.40);
    Check(fisherAt40 > 0.0 && swordAt40 > 0.0,
          "both register a fight in progress");
    Check(fisherAt40 > swordAt40,
          "the fisher is already disengaging where the swordsman fights on");

    // And well above both thresholds they agree, which is equally required:
    // nerve changes WHERE the line is, not whether there is one.
    Check(bailUrgency(*fish, 0.90) == bailUrgency(*sword, 0.90),
          "at 90% neither is bailing and they read the same");

    // And at full health neither is bailing.
    Check(bailUrgency(*fish, 1.0) < 1.0,
          "nobody flees at full health, however cautious");
}


// --------------------------------------------------------------------------
// S2.8: THE ARITHMETIC BEHIND session_goals, PULLED OUT SO IT IS TESTABLE.
//
// R1's exit proof is "at least four goal families, none above half the
// picks" -- and the half of that a naive "count the kinds" implementation
// gets wrong is the SHARE, not the count. A crafter alternating
// BUY_SUPPLIES / CRAFT / EARN_GOLD scores three "kinds" and is still doing
// one thing all day (run_m5/p0gate10, 47/33/20%); MINE at 60% of the picks
// with three other families sharing the rest also has four families present
// and is still monotonous. Both are guarded here, against
// life::SummariseGoalPicks directly -- no Runner, no server, no log line.
void TestGoalHistogramArithmetic() {
    Section("goals: session histogram arithmetic (S2.8)");

    // p0gate10's ring: CRAFT/BUY_SUPPLIES/EARN_GOLD are all GoalFamily::Work,
    // so three "kinds" collapse to one family.
    {
        i32 picks[static_cast<int>(life::GoalKind::Count)] = {};
        picks[static_cast<int>(life::GoalKind::Craft)] = 47;
        picks[static_cast<int>(life::GoalKind::BuySupplies)] = 33;
        picks[static_cast<int>(life::GoalKind::EarnGold)] = 20;
        const life::GoalHistogram h = life::SummariseGoalPicks(picks);
        Check(h.picks == 100, "all three ring goals counted");
        Check(h.families == 1,
              "CRAFT/BUY_SUPPLIES/EARN_GOLD are all the WORK family -- three "
              "kinds, one family, exactly p0gate10");
        Check(!h.varied, "one family all day is not a rounded day");
    }

    // Four distinct families, evenly split -- the actual R1 bar.
    {
        i32 picks[static_cast<int>(life::GoalKind::Count)] = {};
        picks[static_cast<int>(life::GoalKind::Heal)] = 10;        // Emergency
        picks[static_cast<int>(life::GoalKind::Bank)] = 10;        // Upkeep
        picks[static_cast<int>(life::GoalKind::Mine)] = 10;        // Work
        picks[static_cast<int>(life::GoalKind::TrainAtNpc)] = 10;  // Training
        const life::GoalHistogram h = life::SummariseGoalPicks(picks);
        Check(h.families == 4, "four distinct families");
        Check(h.topFrac >= 0.25 - 1e-9 && h.topFrac <= 0.25 + 1e-9,
              "each family holds exactly a quarter of the day");
        Check(h.varied, "four families, none above half -- a rounded day");
    }

    // The half a "count the kinds" implementation gets wrong: MINE alone at
    // 60% still leaves four families present, but one of them owns most of
    // the day.
    {
        i32 picks[static_cast<int>(life::GoalKind::Count)] = {};
        picks[static_cast<int>(life::GoalKind::Mine)] = 60;         // Work
        picks[static_cast<int>(life::GoalKind::Heal)] = 15;         // Emergency
        picks[static_cast<int>(life::GoalKind::Bank)] = 15;         // Upkeep
        picks[static_cast<int>(life::GoalKind::TrainAtNpc)] = 10;   // Training
        const life::GoalHistogram h = life::SummariseGoalPicks(picks);
        Check(h.families == 4, "four families are present");
        Check(h.topFrac > 0.50,
              "but MINE alone holds 60% of the picks -- the top-share half "
              "of the bar, not just the family count");
        Check(!h.varied,
              "four families is not enough on its own -- one still "
              "dominates the day");
    }
}

// S2.2's prerequisite defect: Planner::Score used to add Explore and
// IdleBriefly unconditionally, feasible, after the main loop -- so neither
// consulted Cooling(), and the cooldown DoExplore already issues on
// "nowhere new to go" (kExploredAllCooldownMs) had never had any effect.
// Explore must now honour a cooldown like every other goal; IdleBriefly must
// NEVER be cooled -- it is the floor that guarantees there is never no goal
// at all, and RunGoal dispatches on Current().kind whether or not Select
// succeeded, so an empty feasible set would run a stale handler.
void TestACooledExploreYieldsToIdleBriefly() {
    Section("planner: a cooled EXPLORE is not offered as a feasible fallback");

    life::Planner p;
    life::Memory mem;
    life::Observation obs;
    obs.inWorld = true;
    obs.nowMs = 1000000;
    obs.hp = obs.hpMax = 40;
    obs.weight = 10; obs.maxWeight = 200;

    p.Cooldown(life::GoalKind::Explore, obs.nowMs + 5 * 60 * 1000);

    const std::vector<life::Need> none;
    const std::vector<life::ScoredGoal> scored = p.Score(none, obs, mem);

    const life::ScoredGoal* explore = nullptr;
    const life::ScoredGoal* idle = nullptr;
    for (const life::ScoredGoal& g : scored) {
        if (g.kind == life::GoalKind::Explore)     explore = &g;
        if (g.kind == life::GoalKind::IdleBriefly) idle    = &g;
    }
    Check(explore != nullptr,
          "a cooling EXPLORE is still REPORTED, not silently dropped");
    if (explore) {
        Check(!explore->feasible,
              "and it is NOT feasible while cooling -- before this fix, "
              "Explore's fallback entry ignored Cooling() entirely");
        Check(!explore->blockedWhy.empty(), "and it says why");
    }
    Check(idle != nullptr && idle->feasible,
          "IDLE_BRIEFLY is never cooled -- it is the floor that guarantees "
          "there is never no goal at all");

    std::string why;
    Check(p.Select(none, obs, mem, obs.nowMs, &why),
          "with nothing else on the table, something is still picked");
    Check(p.Current().kind == life::GoalKind::IdleBriefly,
          "and with EXPLORE cooling, that something is IDLE_BRIEFLY");
    p.ClearCooldown(life::GoalKind::Explore);
    obs.huntingRoutine = true;
    for (const auto& goal : p.Score(none, obs, mem)) {
        if (goal.kind == life::GoalKind::Explore)
            Check(!goal.feasible, "combat routine does not turn downtime into sightseeing");
    }
}

// --------------------------------------------------------------------------
// A coloured vein is still ore. Since the pack has been hue-resolved, rusty /
// copper / bronze ore sit in obs.pack under their own names, and NeedSmelt
// used to count "i_ore_iron" alone -- so a miner whose pack held only
// coloured ore never wanted the forge. Owner saw it live, 2026-09-01.
void TestColouredOreStillWantsTheForge() {
    Section("needs: coloured ore raises NeedSmelt like iron does");

    const prof::Profession* ms = prof::Find("miner_smith");
    Check(ms != nullptr, "the miner/smith exists");
    if (!ms) return;

    life::Memory mem;
    auto smeltFor = [&](const char* item, i32 qty) -> const life::Need* {
        life::NeedConfig cfg;
        cfg.profession = ms;
        static std::vector<life::Need> needs;   // keep the pointer alive
        const life::BuildPlan plan = life::PlanFromProfession(*ms);
        life::Observation obs;
        obs.inWorld = true;
        obs.hp = obs.hpMax = 40;
        obs.gold = 1000;
        obs.weight = 50; obs.maxWeight = 400;
        obs.pack.push_back({item, qty});
        needs = life::AssessNeeds(plan, mem, obs, cfg);
        return Find(needs, life::NeedKind::NeedSmelt);
    };

    const life::Need* iron = smeltFor("i_ore_iron", 12);
    Check(iron != nullptr, "twelve iron ore: the forge is wanted");

    const life::Need* rusty = smeltFor("i_ore_rusty", 12);
    Check(rusty != nullptr,
          "twelve RUSTY ore: the forge is wanted just the same -- before this "
          "fix the need counted i_ore_iron only and a coloured-only pack never "
          "smelted");
    if (iron && rusty) {
        Check(std::fabs(iron->urgency - rusty->urgency) < 1e-9,
              "and it is weighed like iron: ore is ore until the forge says "
              "which ingot it becomes");
    }

    Check(smeltFor("i_ingot_iron", 12) == nullptr,
          "ingots are not ore: nothing to melt, no NeedSmelt");
}

// --------------------------------------------------------------------------
// THE TAILOR'S CLOTH: PLAYERS FIRST, THEN THE SHEEP.
//
// Owner ruling, 2026-09-02. The whole point of NeedCloth is that it is the
// SECOND half of a two-step policy, so the thing worth asserting is not that
// it fires -- it is that it stays silent until the player market has actually
// been asked and come back empty. A tailor that walks to Yew while a
// lumberjack stands at the bank with a bale to sell is the failure this test
// exists to catch.
void TestClothIsBoughtFromPlayersBeforeItIsSheared() {
    Section("needs: NeedCloth waits for the player market to decline");

    const prof::Profession* t = prof::Find("tailor");
    Check(t != nullptr, "the tailor exists");
    if (!t) return;

    life::Memory mem;
    const life::BuildPlan plan = life::PlanFromProfession(*t);

    auto clothNeed = [&](bool declined, const char* item,
                         std::vector<life::Need>& out) -> const life::Need* {
        life::NeedConfig cfg;
        cfg.profession = t;
        life::Observation obs;
        obs.inWorld = true;
        obs.hp = obs.hpMax = 40;
        obs.gold = obs.goldOnHand = 1000;
        obs.weight = 50; obs.maxWeight = 400;
        obs.nowMs = 60000;
        if (declined) obs.noSellerFor.push_back(item);
        out = life::AssessNeeds(plan, mem, obs, cfg);
        return Find(out, life::NeedKind::NeedCloth);
    };

    // WHICH material the tailor is actually short of comes from its own
    // recipe list, not from this test's opinion: `produces` leads with
    // i_cloth_bolt, whose input is yarn.
    std::vector<life::Need> quiet;
    const life::Need* silent = clothNeed(false, "i_yarn_ball", quiet);
    Check(silent != nullptr,
          "an empty-handed tailor raises NeedCloth at all -- the errand is "
          "visible in telemetry even when it may not run");
    if (silent) {
        Check(silent->blocked,
              "but BLOCKED while the player market has not been asked");
        Check(silent->urgency == 0.0,
              "and at zero urgency, so TRADE_WITH_PLAYER's own NeedTrade wins "
              "the trip to the bank");
    }

    std::vector<life::Need> asked;
    const life::Need* fires = clothNeed(true, "i_yarn_ball", asked);
    Check(fires != nullptr && !fires->blocked,
          "once a WTB window has expired unanswered -- the no_player_seller "
          "Observe copies into noSellerFor -- the sheep are fair game");
    if (fires) {
        Check(fires->urgency > 0.0, "and it now carries real urgency");
        Check(fires->evidence.find("i_yarn_ball") != std::string::npos,
              "and it names the material it is short of");
    }

    // A DECLINE ABOUT SOMETHING ELSE IS NOT A DECLINE ABOUT THIS.
    // marketQuiet alone would have said yes here, which is exactly why the
    // gate is per-item.
    std::vector<life::Need> other;
    const life::Need* unrelated = clothNeed(true, "i_log", other);
    Check(unrelated != nullptr && unrelated->blocked,
          "a failed attempt to buy LOGS teaches a tailor nothing about cloth");

    // --- "CANNOT BUY NOW" COUNTS AS A DECLINE ------------------------------
    //
    // Owner ruling via lead, 2026-09-02: WTB-first stays while it is
    // affordable and reachable; below the reserve, or with no session left for
    // the trip, the market has effectively said no and the self chain
    // proceeds. Both refusals happen BEFORE any WTB is announced, so neither
    // ever produces the no_player_seller the case above waits on -- Aelia ran
    // a whole gate at gold=0 and never left "the player market has not been
    // asked for it yet" (run_gates/g_Aelia.console.txt:83-710).
    auto clothNeedWith = [&](i32 goldOnHand, bool tripFits,
                             std::vector<life::Need>& out) -> const life::Need* {
        life::NeedConfig cfg;
        cfg.profession = t;
        life::Observation obs;
        obs.inWorld = true;
        obs.hp = obs.hpMax = 40;
        obs.gold = obs.goldOnHand = goldOnHand;
        obs.weight = 50; obs.maxWeight = 400;
        obs.nowMs = 60000;
        obs.marketTripFitsSession = tripFits;
        out = life::AssessNeeds(plan, mem, obs, cfg);
        return Find(out, life::NeedKind::NeedCloth);
    };

    std::vector<life::Need> broke;
    const life::Need* pennilessN = clothNeedWith(0, true, broke);
    Check(pennilessN != nullptr && !pennilessN->blocked,
          "a tailor with 0 gold shears without waiting for a market answer -- "
          "it could never have bought the yarn it is waiting to be offered");
    if (pennilessN) {
        Check(pennilessN->urgency > 0.0,
              "and the need carries real urgency, so MAKE_CLOTH can be picked");
        Check(pennilessN->evidence.find("0 gold on hand") != std::string::npos,
              "and the telemetry says WHY it stopped asking: the purse");
    }

    std::vector<life::Need> late;
    const life::Need* noTimeN = clothNeedWith(1000, false, late);
    Check(noTimeN != nullptr && !noTimeN->blocked,
          "so does a funded tailor with no session left for the market trip -- "
          "the same refusal DoTradeWithPlayer makes on entry");

    // AND THE RULE IS NOT REPEALED. A tailor that CAN afford the trip and has
    // the session for it still asks the players first.
    std::vector<life::Need> funded;
    const life::Need* stillAsks = clothNeedWith(1000, true, funded);
    Check(stillAsks != nullptr && stillAsks->blocked &&
              stillAsks->urgency == 0.0,
          "but a funded tailor with time to spare still WTBs before it shears");

    // PER-NEED, NOT GLOBAL: an empty purse is not a general licence to skip
    // the player market. Every OTHER need a broke tailor raises that depends
    // on buying must still be BLOCKED -- only cloth has a self chain to fall
    // back on, and only cloth is allowed to take it.
    for (const life::Need& n : broke) {
        if (n.kind == life::NeedKind::NeedSupplies ||
            n.kind == life::NeedKind::NeedTrade) {
            Check(n.blocked || n.urgency == 0.0,
                  "a penniless tailor's shopping needs stay blocked -- the "
                  "decline is recorded on NeedCloth alone");
        }
    }

    // AND A LIFE WITH NO CLOTH IN ITS RECIPES NEVER SEES THIS NEED.
    const prof::Profession* ms = prof::Find("miner_smith");
    if (ms) {
        life::NeedConfig cfg;
        cfg.profession = ms;
        life::Observation obs;
        obs.inWorld = true;
        obs.hp = obs.hpMax = 40;
        obs.gold = obs.goldOnHand = 1000;
        obs.weight = 50; obs.maxWeight = 400;
        obs.noSellerFor.push_back("i_cloth");
        const std::vector<life::Need> needs =
            life::AssessNeeds(life::PlanFromProfession(*ms), mem, obs, cfg);
        Check(Find(needs, life::NeedKind::NeedCloth) == nullptr,
              "a smith is never sent to shear a sheep, however quiet the "
              "cloth market is");
    }

    // --- THE TRADE ERRAND'S OWN STAND-DOWN IS ALSO AN ANSWER ---------------
    //
    // Amara: NeedCloth BLOCKED at 0.00 on "the player market has not been
    // asked for it yet" for fourteen consecutive assessments while
    // TRADE_WITH_PLAYER was cooling for another 599s after achieving nothing.
    // Nothing else scored, so EXPLORE won and took 50% of the session's picks
    // (run_gates/g_Amara.console.txt:148,200,204,1151).
    {
        life::NeedConfig cfg;
        cfg.profession = t;
        life::Observation obs;
        obs.inWorld = true;
        obs.hp = obs.hpMax = 40;
        obs.gold = obs.goldOnHand = 9700;   // funded, so `broke` is false
        obs.weight = 50; obs.maxWeight = 400;
        obs.nowMs = 60000;
        obs.marketTripFitsSession = true;   // and there IS time, so `noTime` too
        obs.marketAskOnCooldown = true;
        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* cloth = Find(needs, life::NeedKind::NeedCloth);
        Check(cloth != nullptr && !cloth->blocked && cloth->urgency > 0.0,
              "a funded tailor whose TRADE_WITH_PLAYER is on cooldown shears "
              "instead of waiting on an answer nobody can be asked for");
    }
}

// --------------------------------------------------------------------------
// A BOLT IS CUT, NOT BANKED.
//
// Owner, 2026-09-02, watching Aelia live: she wove her first bolt at the
// Britain loom (1473,1685) and forty milliseconds later BANK superseded
// MAKE_CLOTH -- "BANK 142.3 superseded MAKE_CLOTH 68.8" -- walked 47 tiles and
// deposited it, with scissors in her pack the whole time
// (run_gates/g_Aelia.console.txt:442,460,519).
//
// TWO INDEPENDENT ROUTES PUT IT IN THE BOX, so both are asserted here:
//   * the bolt is the FIRST entry of the tailor's `produces`, so
//     market::Surplus called one bolt spare stock;
//   * a bolt weighs 50 stones (i_profession_tailor_tanner.scp:74-80), enough
//     on its own to cross bankWeightFrac and raise "deposit carried load".
void TestABoltIsCutNotBanked() {
    Section("needs: half-made cloth is finished, not stored");

    const prof::Profession* t = prof::Find("tailor");
    Check(t != nullptr, "the tailor exists");
    if (!t) return;

    life::NeedConfig cfg;
    cfg.profession = t;

    // THE PREDICATE ITSELF, since two subsystems read it.
    std::vector<market::Stock> justABolt;
    justABolt.push_back({"i_cloth_bolt", 1});
    Check(life::WoolChainWorkInProgress(*t, justABolt, cfg.craftBatch,
                                        "i_cloth_bolt"),
          "one bolt and no cloth: the bolt is work in progress");
    Check(life::WoolChainWorkInProgress(*t, justABolt, cfg.craftBatch, "i_wool"),
          "so are wool and yarn -- every step the scissors have not reached");
    Check(!life::WoolChainWorkInProgress(*t, justABolt, cfg.craftBatch,
                                         "i_cloth"),
          "but cut cloth is the finished material, never 'in progress'");

    std::vector<market::Stock> stocked;
    stocked.push_back({"i_cloth_bolt", 3});
    stocked.push_back({"i_cloth", 500});
    Check(!life::WoolChainWorkInProgress(*t, stocked, cfg.craftBatch,
                                         "i_cloth_bolt"),
          "and a tailor already holding a sitting's cloth may bank or sell "
          "spare bolts -- this is not a ban on ever storing one");

    const prof::Profession* ms = prof::Find("miner_smith");
    if (ms) {
        Check(!life::WoolChainWorkInProgress(*ms, justABolt, cfg.craftBatch,
                                             "i_cloth_bolt"),
              "a life whose recipes never eat cloth holds a bolt as plain "
              "goods -- the gate is the recipe graph, not the job title");
    }

    // THE LIVE SHAPE: Aelia's numbers at the loom, 120 of 124 stones with the
    // bolt in the pack.
    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 40;
    obs.gold = obs.goldOnHand = 0;
    obs.weight = 120; obs.maxWeight = 124;
    obs.nowMs = 60000;
    obs.pack.push_back({"i_cloth_bolt", 1});

    life::Memory mem;
    const std::vector<life::Need> needs =
        life::AssessNeeds(life::PlanFromProfession(*t), mem, obs, cfg);

    for (const life::Need& n : needs) {
        if (n.kind != life::NeedKind::NeedBank) continue;
        Check(n.blocked || n.urgency == 0.0,
              "with nothing in the pack but a bolt it means to cut, no bank "
              "errand is live -- the weight IS the work");
    }
    for (const life::Need& n : needs) {
        Check(n.evidence.find("x i_cloth_bolt spare") == std::string::npos,
              "and no need calls that bolt spare stock");
    }

    const life::Need* cloth = Find(needs, life::NeedKind::NeedCloth);
    Check(cloth != nullptr && !cloth->blocked && cloth->urgency > 0.0,
          "MAKE_CLOTH is the live errand instead: penniless counts as a market "
          "decline, so the scissors step may run");
    const life::Need* bank = Find(needs, life::NeedKind::NeedBank);
    if (cloth && bank) {
        Check(cloth->urgency > bank->urgency,
              "and cutting outranks banking, which is the order the owner "
              "asked for: bolt -> scissors -> cloth");
    }
}

// --------------------------------------------------------------------------
// "I HAVE THE YARN, I NEED TO WEAVE IT."
//
// The shortfall that is not a shortfall of INPUTS. A tailor's `produces` opens
// with i_cloth_bolt, whose only input is four yarn, so the moment four yarn are
// carried the recipe's missing list is EMPTY -- and NeedCloth, which was built
// from that list alone, never fired. Aelia held 6 yarn and 0 bolts for a whole
// session: CRAFT chose the bolt, found every input present, handed off to
// MAKE_CLOTH because a bolt is loom work, and MAKE_CLOTH was not in the
// planner's list at all -- not even as a BLOCKED_NEED. The session drifted
// CRAFT -> PRACTICE_SKILL -> EARN_GOLD with the loom fifty tiles away
// (artifacts/cloth_walkup_bolt_route_capacity_2026-09-02.md section 4).
void TestYarnInThePackIsWorkNotStock() {
    Section("cloth: yarn in the pack is unfinished work, not a full larder");

    const prof::Profession* t = prof::Find("tailor");
    Check(t != nullptr, "the tailor exists");
    if (!t) return;

    life::Memory mem;
    const life::BuildPlan plan = life::PlanFromProfession(*t);

    // Aelia's live state, not an invented one (tools/world_query.py --char
    // Aelia, 2026-09-02): tooled, unhurt, and penniless. Gold 0 is what makes
    // the market count as declined here -- market::CanAffordToShop refuses to
    // name a want at all below the reserve, so no WTB is ever announced and no
    // no_player_seller is ever written. That is rule B's capital gate, the same
    // one the case below this section already covers for a missing INPUT.
    auto tailorHolding = [&](i32 yarnQty, i32 boltQty,
                             std::vector<life::Need>& out) -> const life::Need* {
        life::NeedConfig cfg;
        cfg.profession = t;
        life::Observation obs;
        obs.inWorld = true;
        obs.hp = obs.hpMax = 40;
        obs.gold = obs.goldOnHand = 0;
        obs.weight = 50; obs.maxWeight = 400;
        obs.nowMs = 60000;
        obs.marketTripFitsSession = true;
        obs.toolsHeld = {"sewing kit", "scissors"};
        obs.healPotions = 4;   // a crafter's self-heal, already bought
        if (yarnQty > 0) obs.pack.push_back({"i_yarn_ball", yarnQty});
        if (boltQty > 0) obs.pack.push_back({"i_cloth_bolt", boltQty});
        out = life::AssessNeeds(plan, mem, obs, cfg);
        return Find(out, life::NeedKind::NeedCloth);
    };

    // Aelia's actual pack at the start of the gate: 6 yarn, no bolts.
    std::vector<life::Need> weaving;
    const life::Need* weave = tailorHolding(6, 0, weaving);
    Check(weave != nullptr,
          "six yarn and no bolt is a NeedCloth -- the yarn is half-finished "
          "work, not a reason to call the errand done");
    if (weave) {
        Check(!weave->blocked && weave->urgency > 0.0,
              "and it carries real urgency, so MAKE_CLOTH can be picked at all");
        Check(weave->evidence.find("i_cloth_bolt") != std::string::npos,
              "and the telemetry names the bolt, not a phantom missing input");
    }

    // The whole point: the planner must actually land on it. NeedCraft also
    // fires here (every input for a bolt IS in the pack), and it used to be the
    // only one -- CRAFT won, discovered the bolt was loom work, handed off, and
    // handed the turn to a goal nothing had scored.
    {
        life::Planner p;
        life::Observation sel;
        sel.inWorld = true;
        sel.hp = sel.hpMax = 40;
        sel.nowMs = 60000;
        std::string why;
        Check(p.Select(weaving, sel, mem, sel.nowMs, &why),
              "a need list this full always selects something");
        Check(p.Current().kind == life::GoalKind::MakeCloth,
              "and what it selects is MAKE_CLOTH -- the loom, not the bench");
    }

    // AND IT SWITCHES OFF. A sitting's worth of bolts already made is not a
    // reason to walk to a loom, or this becomes the goal that never ends.
    std::vector<life::Need> stocked;
    life::NeedConfig batchCfg;
    const life::Need* done =
        tailorHolding(6, batchCfg.craftBatch, stocked);
    Check(done == nullptr,
          "with the batch's bolts already made there is no cloth need at all");
}

// --------------------------------------------------------------------------
// THE CHAIN'S OWN ARITHMETIC, checked against the engine numbers rather than
// against the bot's hopes. Every figure here is cited in Production.cpp from
// Source-X, and the test exists because the loop's stopping condition depends
// on them: 4 yarn is ONE loom gesture, not four, and one bolt is 50 cloth, so
// a tailor that needs 16 cloth for a robe needs exactly one bolt and not four.
void TestTheWoolChainBookkeeping() {
    Section("cloth: wool -> yarn -> bolt -> cloth, as the engine counts it");

    const prod::Recipe* yarn = prod::FindRecipe("i_yarn_ball");
    Check(yarn != nullptr, "the wheel's recipe is declared");
    if (yarn) {
        Check(yarn->outputQty == 3, "one wool spins to THREE yarn");
        Check(yarn->inputs[0].item != nullptr &&
                  std::string(yarn->inputs[0].item) == "i_wool" &&
                  yarn->inputs[0].qty == 1 && yarn->inputs[1].item == nullptr,
              "from exactly one wool");
        Check(yarn->station == prod::Station::SpinningWheel,
              "at a spinning wheel, which is a dynamic item and must be "
              "targeted by serial");
    }

    const prod::Recipe* bolt = prod::FindRecipe("i_cloth_bolt");
    Check(bolt != nullptr, "the loom's recipe is declared");
    if (bolt) {
        Check(bolt->outputQty == 1, "the loom yields one bolt");
        Check(bolt->inputs[0].item != nullptr &&
                  std::string(bolt->inputs[0].item) == "i_yarn_ball" &&
                  bolt->inputs[0].qty == 4 && bolt->inputs[1].item == nullptr,
              "from FOUR yarn -- the loom's message table is five entries and "
              "it emits at ARRAY_COUNT-1");
        Check(bolt->station == prod::Station::Loom, "at a loom");
    }

    const prod::Recipe* cloth = prod::FindRecipe("i_cloth");
    Check(cloth != nullptr, "cutting the bolt is declared");
    if (cloth) {
        Check(cloth->outputQty == 50, "one bolt cuts to FIFTY cloth");
        Check(cloth->tool == prod::Tool::Scissors,
              "with scissors -- the one step of the chain scissors DO perform");
        Check(cloth->station == prod::Station::None,
              "and nowhere in particular, so it needs no second trip");
    }

    // ONE SHEEP IS ONE WOOL, AND A LOOM-LOAD IS FOUR YARN, SO A BOLT COSTS
    // TWO SHEEP. 2 wool -> 6 yarn -> 1 bolt (with 2 yarn left over) -> 50
    // cloth. Written out because it is the number that decides whether the
    // errand is worth the walk, and getting it wrong by 3x is how a bot
    // shears all day for a sash.
    Check(3 * 2 >= 4, "two sheep make enough yarn for one bolt");

    // WHAT THE CHAIN CAN AND CANNOT SUPPLY. Thread is the blocker: it comes
    // from cotton, and every stock Tailoring recipe wants one.
    Check(life::IsWoolChainMaterial("i_wool"), "wool is on the chain");
    Check(life::IsWoolChainMaterial("i_yarn_ball"), "yarn is on the chain");
    Check(life::IsWoolChainMaterial("i_cloth_bolt"), "the bolt is on the chain");
    Check(life::IsWoolChainMaterial("i_cloth"), "cloth is on the chain");
    Check(!life::IsWoolChainMaterial("i_thread"),
          "THREAD IS NOT: it is spun from cotton, six per pile, and no amount "
          "of wool makes any -- so sewing stays blocked and MAKE_CLOTH must "
          "not pretend otherwise");
    const prod::Recipe* thread = prod::FindRecipe("i_thread");
    Check(thread != nullptr && thread->inputs[0].item != nullptr &&
              std::string(thread->inputs[0].item) == "i_cotton",
          "and the recipe table agrees about where thread comes from");
    Check(!life::IsWoolChainMaterial("i_log"), "a log is not textile");
    Check(!life::IsWoolChainMaterial(nullptr), "and nothing is not textile");
}

// --------------------------------------------------------------------------
// A GESTURE THAT MOVES NOTHING THREE TIMES IS THE WRONG GESTURE.
//
// The escalate-after-three rule, applied to MAKE_CLOTH. There is no craft menu
// behind the wheel or the loom -- they answer with a SysMessage and nothing
// else -- so the only honest confirmation is an inventory delta, and the only
// honest response to three empty gestures is to stand down. This asserts the
// planner machinery MAKE_CLOTH leans on, which is the half reachable without a
// live server.
void TestThreeEmptyClothStepsStandTheGoalDown() {
    Section("cloth: three gestures that move nothing cool the goal");

    life::Planner p;
    life::Observation obs;
    obs.inWorld = true;
    obs.nowMs = 1000;
    life::Memory mem;

    std::vector<life::Need> needs;
    needs.push_back({life::NeedKind::NeedCloth, 0.55, "cloth",
                     "nobody was selling it", "20 x i_cloth short", false});

    std::string why;
    Check(p.Select(needs, obs, mem, obs.nowMs, &why),
          "with the market declined, MAKE_CLOTH is selectable");
    Check(p.Current().kind == life::GoalKind::MakeCloth,
          "and it is what a blocked tailor picks -- 135 beats CRAFT's 130 "
          "because the making cannot start without it");

    // The stand-down DoMakeCloth performs on its third empty gesture.
    p.Cooldown(life::GoalKind::MakeCloth, obs.nowMs + 300000);
    p.Finish(false, "the chain moved nothing", obs.nowMs);

    obs.nowMs += 5000;
    const std::vector<life::ScoredGoal> scored = p.Score(needs, obs, mem);
    const life::ScoredGoal* mc = nullptr;
    for (const life::ScoredGoal& g : scored)
        if (g.kind == life::GoalKind::MakeCloth) mc = &g;
    Check(mc != nullptr, "the cooled goal is still REPORTED, not hidden");
    if (mc) {
        Check(!mc->feasible,
              "but it is not feasible, so the planner must find other work -- "
              "Finish(false) alone would have handed it straight back");
        Check(!mc->blockedWhy.empty(), "and the log says for how much longer");
    }

    // ...and it comes back. A cooldown is a rest, not a write-off: wool
    // regrows in thirty minutes and the five-minute rest is well inside that.
    obs.nowMs += 300000;
    const std::vector<life::ScoredGoal> later = p.Score(needs, obs, mem);
    bool backOnTheTable = false;
    for (const life::ScoredGoal& g : later)
        if (g.kind == life::GoalKind::MakeCloth && g.feasible)
            backOnTheTable = true;
    Check(backOnTheTable, "after the rest the sheep are worth another try");
}

// --- practice casting pays for itself ---------------------------------------
//
// Wave 2026-09-02: four mages cast a self-safe spell every six seconds for a
// whole session and were refused every time -- "You lack Sulfurous Ash for this
// spell" x156/x298/x322/x310 -- because the practice goal never asked what the
// spell CONSUMED. This is that question, and the shopping list that follows it.
void TestPracticeChecksTheReagentPouch() {
    Section("practice: a spell is chosen by what the pack can pay for");

    // A FAKE TABLE, in the shape tools/spellgen.py writes out of
    // runtime/scripts/spells/spells_magery.scp. Small on purpose: the point is
    // the RULES, not the shard's 64 rows. Circles sit 10.0 skill apart here as
    // they do there, so the gain window is measured from the data.
    const std::string tsv =
        "spell\tdefname\tname\tcircle\tminskill\tmana\tflags\treagents\n"
        // circle 1: two beneficial, one harmful, one unreadable
        "4\ts_heal\tHeal\t1\t100\t4\tspellflag_targ_char|spellflag_good\t"
        "i_reag_garlic,i_reag_ginseng,i_reag_spider_silk\n"
        "6\ts_night_sight\tNight Sight\t1\t100\t4\t"
        "spellflag_targ_char|spellflag_good|spellflag_playeronly\t"
        "i_reag_spider_silk,i_reag_sulfur_ash\n"
        "5\ts_magic_arrow\tMagic Arrow\t1\t100\t4\t"
        "spellflag_targ_char|spellflag_harm|spellflag_damage\ti_reag_sulfur_ash\n"
        "8\ts_weaken\tWeaken\t1\t100\t4\tspellflag_good|spellflag_wat\t"
        "i_reag_garlic\n"
        // circle 2: two beneficial, and one that needs a ground target
        "9\ts_agility\tAgility\t2\t200\t6\t"
        "spellflag_targ_char|spellflag_good\ti_reag_blood_moss,i_reag_mandrake_root\n"
        "16\ts_strength\tStrength\t2\t200\t6\t"
        "spellflag_targ_char|spellflag_good\ti_reag_mandrake_root,i_reag_nightshade\n"
        "13\ts_wall\tWall of Stone\t2\t200\t6\t"
        "spellflag_targ_xyz|spellflag_field\ti_reag_blood_moss,i_reag_garlic\n"
        // circle 4: expensive in mana, and the hardest thing in this book
        "29\ts_greater_heal\tGreater Heal\t4\t400\t11\t"
        "spellflag_targ_char|spellflag_good|spellflag_heal\t"
        "i_reag_garlic,i_reag_ginseng,i_reag_mandrake_root,i_reag_spider_silk\n";
    Check(spell::LoadSpellTableFromText(tsv) == 8,
          "the spell table is loaded from data, not compiled in");
    Check(spell::CircleSpacingTenths() == 100,
          "the gain window's width is measured off the circle ladder");

    // The starter book of this shard ([NEWBIE MAGERY] MORE1=0382a8c38) holds
    // Night Sight (6), Heal (4) and Strength (16) among others.
    spell::PracticeSight see;
    see.inBook = {6, 4, 16, 9, 5, 8, 13, 29};
    see.magery = 200;              // circle 2 reached, circle 4 not
    see.mana = 40;

    // Empty pouch: nothing may be cast, and the choice says what to buy rather
    // than sending a doomed cast at the server.
    spell::PracticeChoice none = spell::ChoosePracticeSpell(see);
    Check(none.spell < 0, "an empty pouch casts nothing");
    Check(!none.missing.empty(), "and it names what is missing instead");
    Check(std::strcmp(none.reason, "out of reagents") == 0,
          "the reason is the pouch, not the book");
    // THE GAIN WINDOW picks the shopping list too: at Magery 20.0 the spells
    // this character would practise with are the circle-2 pair, so the list is
    // THEIR reagents (three names), not circle 1's.
    Check(none.circle == 2, "the list is for the circle it would practise at");
    Check(none.missing.size() == 3,
          "and covers every reagent those spells consume");
    bool wantsNightshade = false;
    for (const std::string& r : none.missing)
        if (r == "i_reag_nightshade") wantsNightshade = true;
    Check(wantsNightshade, "including the one only Strength uses");

    // Half a Strength is not a Strength -- both reagents are consumed -- and
    // the pack cannot pay for Agility either.
    see.pack.push_back({"i_reag_mandrake_root", 40});
    spell::PracticeChoice half = spell::ChoosePracticeSpell(see);
    Check(half.spell < 0, "half a spell is still not castable");
    Check(half.missing.size() == 2, "and only what is short is on the list");

    // Stocked for circle 2: the HIGHEST circle in the window wins over the
    // circle-1 spells, even though those were reachable first.
    see.pack.push_back({"i_reag_nightshade", 40});
    see.pack.push_back({"i_reag_spider_silk", 40});
    see.pack.push_back({"i_reag_sulfur_ash", 40});
    spell::PracticeChoice go = spell::ChoosePracticeSpell(see);
    Check(go.spell == 16 && go.circle == 2,
          "the hardest spell the pack can pay for is cast, not the easiest");

    // ROTATION: having cast Strength, the next pick is the other spell of that
    // circle rather than Strength again.
    see.pack.push_back({"i_reag_blood_moss", 40});
    see.casts.push_back({16, 1});
    spell::PracticeChoice rot = spell::ChoosePracticeSpell(see);
    Check(rot.spell == 9, "a second cast rotates to the other spell of the ring");
    see.casts.push_back({9, 3});
    Check(spell::ChoosePracticeSpell(see).spell == 16,
          "and back again once that one is the busier of the two");

    // "You lack X" is a hard signal: the refused spell is struck off, and with
    // the whole ring refused the choice drops a circle rather than stopping.
    see.uncastable = {16, 9};
    spell::PracticeChoice next = spell::ChoosePracticeSpell(see);
    Check(next.spell == 6 || next.spell == 4,
          "refused spells are skipped and an easier circle takes over");

    // SKILL is a hard gate: Greater Heal is in the book but 40.0 Magery away.
    Check(next.spell != 29, "a spell above this character's skill is not cast");
    see.uncastable.clear();
    see.magery = 400;
    see.pack.push_back({"i_reag_garlic", 40});
    see.pack.push_back({"i_reag_ginseng", 40});
    Check(spell::ChoosePracticeSpell(see).spell == 29,
          "and at 40.0 Magery it becomes the thing to practise with");
    // MANA is the other hard gate, and it does not strike the spell off -- it
    // just takes this cast.
    see.mana = 8;
    Check(spell::ChoosePracticeSpell(see).spell != 29,
          "with 8 mana an 11-mana spell is not attempted");

    see.mana = 0;
    const spell::PracticeChoice tired = spell::ChoosePracticeSpell(see);
    Check(tired.spell < 0 && tired.manaNeeded > 0 && tired.missing.empty(),
          "a stocked exhausted mage needs meditation, not a new book or reagents");
    see.mana = tired.manaNeeded;
    Check(spell::ChoosePracticeSpell(see).spell >= 0,
          "casting resumes once meditation restores enough mana");
    see.mana = 0;
    see.uncastable = see.inBook;
    Check(spell::ChoosePracticeSpell(see).manaNeeded == 0,
          "meditation cannot fix spells refused by the server");
    see.uncastable.clear();
    see.pack.clear();
    const spell::PracticeChoice depleted = spell::ChoosePracticeSpell(see);
    Check(depleted.spell < 0 && depleted.manaNeeded == 0 && !depleted.missing.empty(),
          "no mana and no reagents still identifies the missing supplies");

    // A book with nothing safe in it is a DIFFERENT problem -- that one
    // belongs to FILL_SPELLBOOK -- and must not be reported as a shopping list.
    // Magic Arrow is harmful; Wall of Stone needs a ground target; Weaken here
    // carries a flag this client cannot read, and an unreadable flag is not
    // assumed harmless.
    spell::PracticeSight bare;
    bare.inBook = {5, 13, 8};
    bare.magery = 400;
    bare.mana = 40;
    spell::PracticeChoice empty = spell::ChoosePracticeSpell(bare);
    Check(empty.spell < 0 && empty.missing.empty(),
          "an unusable book asks for no reagents");
    bare.mana = 0;
    Check(spell::ChoosePracticeSpell(bare).manaNeeded == 0,
          "an unsafe book does not send the mage into meditation");

    // The real export must exist and agree with the shard's own ladder.
    Check(spell::LoadSpellTableFromText(tsv) == 8, "table reload is clean");

    // --- how many to buy: a rate, never a constant -------------------------
    //
    // "keep/bank/surplus counts derive from plans, wealth and prices per
    // character, not global constants" (project owner). One reagent per cast,
    // so the target is the casts still expected this session.
    const i32 prior = spell::ExpectedPracticeCasts(0, 0, 30 * 60000, 6000);
    const i32 shortSession = spell::ExpectedPracticeCasts(0, 0, 5 * 60000, 6000);
    Check(prior > shortSession,
          "a long session plans for more casts than a short one");
    // Observed beats prior: a life that has actually cast 60 times in ten
    // minutes is a six-a-minute life, whatever the prior assumed.
    const i32 observed =
        spell::ExpectedPracticeCasts(60, 10 * 60000, 10 * 60000, 6000);
    Check(observed > spell::ExpectedPracticeCasts(2, 10 * 60000, 10 * 60000, 6000),
          "the busier practiser buys more");

    // The purse is the other half. At 3 gold each (the mage Alenne's own price,
    // observed live 2026-09-02) two kinds cost 6 a cast.
    spell::ReagentPlan rich = spell::PlanReagentBuy(0, 100, 3, 5000, 2);
    Check(rich.buy == 100, "with money the plan buys the whole session's worth");
    spell::ReagentPlan poor = spell::PlanReagentBuy(0, 100, 3, 120, 2);
    Check(poor.buy == 20, "with 120 gold it buys 20 of each and no more");
    spell::ReagentPlan stocked = spell::PlanReagentBuy(100, 100, 3, 5000, 2);
    Check(stocked.buy == 0, "and a full pouch buys nothing at all");
}

// The need model must SAY the mage is out of reagents, or BUY_SUPPLIES never
// gets a turn -- which is exactly what happened live: Aurelius stood at a mage
// stocking Sulfurous Ash x250 and asked her only for scrolls.
void TestAnEmptyPouchIsAShoppingErrand() {
    Section("needs: an empty reagent pouch is a trip to the mage shop");

    const prof::Profession* mage = prof::Find("mage");
    if (!mage) { Check(false, "no mage profession"); return; }

    life::NeedConfig cfg;
    cfg.profession = mage;
    life::BuildPlan plan = life::PlanFromProfession(*mage);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 20;
    obs.mana = 40;
    obs.gold = 500;
    obs.weight = 10; obs.maxWeight = 200;
    obs.skills.push_back({rules::kMagery, 500});
    obs.practiceReagentsShort = {"i_reag_sulfur_ash", "i_reag_spider_silk"};
    obs.practiceReagentQty = 45;

    const std::vector<life::Need> ns = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* sup = nullptr;
    for (const life::Need& n : ns)
        if (n.kind == life::NeedKind::NeedSupplies) { sup = &n; break; }
    Check(sup != nullptr, "the mage asks to go shopping");
    if (sup) {
        Check(!sup->blocked, "with 500 gold the errand is actionable");
        Check(sup->what == "buy spell reagents",
              "and it says what it is shopping for");
        Check(sup->evidence.find("i_reag_sulfur_ash") != std::string::npos,
              "the evidence names the reagent");
    }

    // Broke: still reported, but blocked -- selling is the way out, not a walk
    // to a shop that will refuse the sale. Same rule the craft clause states.
    obs.gold = 40;
    const std::vector<life::Need> broke = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* poor = nullptr;
    for (const life::Need& n : broke)
        if (n.kind == life::NeedKind::NeedSupplies) { poor = &n; break; }
    Check(poor != nullptr && poor->blocked,
          "with no working capital the trip is blocked, not hidden");
}

// A SMITH'S INGOTS ARE ALSO ITS RAW MATERIAL.
//
// DoBank's loaded branch banked every i_ingot_iron in the pack because ingots
// are in `produces`; ChooseCraft counts the PACK only (life/Identity.cpp) and
// nothing withdraws a craft input again, so "stocked, stop mining" and "no
// inputs" landed on the same tick (architecture review 2026-09-05, item 4b).
// The reserve is the same working batch the other two deposit branches keep.
void TestASmithKeepsIngotsToWorkWith() {
    Section("bank: what the next batch is made from is not a deposit");

    const prof::Profession* smith = prof::Find("miner_smith");
    Check(smith != nullptr, "the smith exists");
    if (!smith) return;

    const i32 batch = 5;
    Check(life::CraftInputReserve(*smith, "i_ingot_iron", batch) == batch * 2,
          "the smith keeps a working batch of ingots out of the box");
    Check(life::CraftInputReserve(*smith, "i_gold", batch) == 0,
          "something the smith never crafts from is not reserved");

    const prof::Profession* tailor = prof::Find("tailor");
    if (tailor) {
        Check(life::CraftInputReserve(*tailor, "i_ingot_iron", batch) == 0,
              "a tailor keeps no ingots back -- it makes nothing from them");
    }
}

// A HAND-OFF MUST BE ADDRESSED TO SOMEBODY WHO CAN ANSWER IT.
//
// DoTrainCombat sends a fighter below the bandage floor to REPLACE_EQUIPMENT,
// and that errand's bandage branch is gated on WantsConsumable(cfg,
// "bandage") -- false for a pure mage, whose catalogue entry carries heal
// potions instead (Professions.cpp). The gate on the hand-off has to be the
// SAME question, or the request goes to a goal that will not act on it.
void TestAPureMageIsNotSentShoppingForBandages() {
    Section("gear: the bandage hand-off asks what the errand asks");

    const prof::Profession* mage = prof::Find("mage");
    const prof::Profession* fencer = prof::Find("fencer");
    Check(mage && fencer, "both lives exist");
    if (!mage || !fencer) return;

    life::NeedConfig mageCfg;
    mageCfg.profession = mage;
    life::NeedConfig fencerCfg;
    fencerCfg.profession = fencer;

    Check(!life::WantsConsumable(mageCfg, "bandage"),
          "a pure mage does not carry bandages, so it is never sent for them");
    Check(life::WantsConsumable(mageCfg, "heal potion"),
          "the heal it CAN use is the potion, and it does ask for that");
    Check(life::WantsConsumable(fencerCfg, "bandage"),
          "a fencer does carry them -- the gate is not simply off");
}

// A DRAINED POTION SHELF DAMPS THE NEED, THE BANDAGE CLAUSE'S TWIN.
//
// Gear.cpp's DoReplaceEquipment records "potion_counters_empty" (session=N)
// when every known heal-potion counter is drained, but nothing read it here,
// so NeedEquipment(heal potions) kept scoring 0.5 x 260 = 130 through an
// empty shelf and was re-picked every cooldown (Odessa 00:15:42, 00:20:44,
// 00:26:00, 00:33:25; Hector 00:34:27, same Britain healer, 2026-09-06).
void TestADrainedPotionShelfDampsTheNeed() {
    Section("needs: a drained potion shelf damps the need, like bandages");

    const prof::Profession* smith = prof::Find("miner_smith");
    Check(smith != nullptr, "the smith exists");
    if (!smith) return;

    life::NeedConfig cfg;
    cfg.profession = smith;
    life::BuildPlan plan = life::PlanFromProfession(*smith);

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 32;                 // full health: not currently wounded
    obs.weight = 10; obs.maxWeight = 200;
    obs.healPotions = 0;
    obs.gold = 500;
    obs.skills.push_back({rules::kMining, 500});
    obs.nowMs = 1000000;

    auto potionNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedEquipment && n.what == "heal potions")
                return &n;
        return nullptr;
    };

    // Baseline: no event recorded, the ordinary 0.5 urgency applies.
    life::Memory mem;
    const std::vector<life::Need> nsBaseline = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* baseline = potionNeed(nsBaseline);
    Check(baseline != nullptr && std::fabs(baseline->urgency - 0.5) < 1e-9,
          "with no drained-shelf event the crafter's ordinary 0.5 urgency "
          "applies (score 130, matching the live regression)");

    // Every counter in town drained, THIS session, and health is fine: the
    // need is damped so it no longer beats work.
    mem.NoteEvent("potion_counters_empty", "session=0", "", obs.x, obs.y,
                  obs.nowMs);
    const std::vector<life::Need> nsDrained = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* drained = potionNeed(nsDrained);
    Check(drained != nullptr && std::fabs(drained->urgency - 0.10) < 1e-9,
          "a drained shelf damps the potion need to 0.10 (score 26) -- below "
          "CRAFT (0.5 x 130 = 65) and BUY_SUPPLIES, so it no longer outscores "
          "work");
    if (drained)
        Check(drained->reason.find("drained") != std::string::npos,
              "the reason names the drained shelf, not the old blanket text");

    // A WOUND STILL SAYS SO. A bleeding crafter is not told to wait out a
    // restock window it may not survive.
    life::Observation hurt = obs;
    hurt.hp = 10;   // well under healHpFraction
    const std::vector<life::Need> nsHurt = life::AssessNeeds(plan, mem, hurt, cfg);
    const life::Need* hurtNeed = potionNeed(nsHurt);
    Check(hurtNeed != nullptr && std::fabs(hurtNeed->urgency - 0.5) < 1e-9,
          "wounded and out of potions is NOT damped even with the drained "
          "event live -- the emergency route stays open");

    // Outside the restock window the damping expires like the bandage one.
    life::Observation later = obs;
    later.nowMs = obs.nowMs + 700000;   // past kShelfRestockMs (600000)
    const std::vector<life::Need> nsLater = life::AssessNeeds(plan, mem, later, cfg);
    const life::Need* laterNeed = potionNeed(nsLater);
    Check(laterNeed != nullptr && std::fabs(laterNeed->urgency - 0.5) < 1e-9,
          "past the restock window the ordinary urgency returns");
}

}  // namespace


// --------------------------------------------------------------------------
// STAT FARMING -- the Wrestling detour (owner order, 2026-09-04).
//
// Source-X rolls a stat gain on every skill use, toward that skill's STAT_x
// and never past it (CCharSkill.cpp Skill_Experience ~:459). So the question
// "must this character wrestle to reach its STR target" is arithmetic over the
// build plan, and these checks are that arithmetic.
// --------------------------------------------------------------------------
void TestStrengthAWizardCannotEarnByCasting() {
    Section("stat farm: STR a build's own skills can never reach");

    // The table itself, against the shard's own skill files.
    Check(rules::SkillStatStr(rules::kMagery) == 20,
          "skill25_magery.scp STAT_STR=20");
    Check(rules::SkillStatStr(rules::kMeditation) == 10,
          "skill46_meditation.scp STAT_STR=10");
    Check(rules::SkillStatStr(rules::kWrestling) == 100,
          "skill43_wrestling.scp STAT_STR=100 -- the reason it is the detour");
    Check(rules::SkillStatStr(rules::kBlacksmithing) == 95,
          "skill7_blacksmithing.scp STAT_STR=95");
    Check(rules::SkillStatStr(999) == -1,
          "an id we have no file for reads as unknown, not as zero STR");

    // A caster's plan: three INT skills and a STR target none of them reach.
    life::BuildPlan mage;
    mage.family = "mage";
    mage.skills = {{rules::kMagery, 1000},
                   {rules::kMeditation, 1000},
                   {rules::kEvaluatingIntel, 1000}};
    mage.targetStr = 80;
    mage.targetDex = 35;
    mage.targetInt = 100;
    Check(life::PlanStrCeiling(mage) == 20,
          "the whole plan tops out at Magery's 20");

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = 100; obs.hpMax = 100;
    obs.str = 50; obs.dex = 25; obs.intel = 90;
    obs.weight = 10; obs.maxWeight = 500;

    const life::StatFarmPlan sf = life::AssessStatFarm(mage, obs);
    Check(sf.wanted, "a caster at STR 50 with a target of 80 must go wrestling");
    Check(sf.ceiling == 20 && sf.have == 50 && sf.target == 80,
          "the plan reports the three numbers it decided on");
    Check(sf.useDummy, "Wrestling 0.0 is under SkillPracticeMax, so a dummy "
                       "will still take the swings");
    Check(sf.urgency > 0.2 && sf.urgency <= 0.5,
          "a detour is weighted as a detour, not as the work");

    // A smith is the control case: his own hammer carries him to 95, so the
    // detour is exactly the wrong answer while he is at 50.
    const prof::Profession* smith = prof::Find("miner_smith");
    Check(smith != nullptr, "the miner/smith exists in the catalogue");
    if (smith) {
        const life::BuildPlan sp = life::PlanFromProfession(*smith);
        Check(life::PlanStrCeiling(sp) >= 85,
              "Blacksmithing 95 / Mining 85 are a smith's own STR ladder");
        life::Observation so = obs;
        so.str = 50;
        const life::StatFarmPlan ssf = life::AssessStatFarm(sp, so);
        Check(!ssf.wanted,
              "a smith at STR 50 keeps swinging his hammer -- his own work "
              "still raises STR, so no Wrestling detour");
        // ...and once his own ladder is run out, the same rule sends him too.
        so.str = 96;
        Check(so.str < sp.targetStr, "the smith's plan really does want 100");
        Check(life::AssessStatFarm(sp, so).wanted,
              "at 96 of a target of 100 even a smith has to wrestle for it");
    }

    // The exit condition.
    life::Observation done = obs;
    done.str = 80;
    Check(!life::AssessStatFarm(mage, done).wanted,
          "at the target the need falls silent");

    // The dummy ceiling is the shard's, not ours (sphere.ini:653).
    Check(life::kDummyPracticeMaxTenths == 300, "SkillPracticeMax=300");
    life::Observation skilled = obs;
    skilled.skills = {{rules::kWrestling, 300}};
    Check(!life::AssessStatFarm(mage, skilled).useDummy,
          "at exactly 30.0 the dummy refuses, so the errand must find a "
          "live opponent");

    // --- the locks -------------------------------------------------------
    Check(life::kStatFarmLocks.str == life::kStatLockUp &&
          life::kStatFarmLocks.intel == life::kStatLockUp,
          "STR and INT are UP during the farm -- only an UP stat is rolled");
    Check(life::kStatFarmLocks.dex == life::kStatLockLocked,
          "DEX is LOCKED during the farm: Wrestling pulls it to 75 and a "
          "caster wants 35");
    Check(life::kStatFarmLocks.wrestling == life::kStatLockUp,
          "Wrestling itself trains up while the farm runs");
    // And the lock the exit restores, which is the ordinary build policy.
    life::Observation lowDex = obs;
    lowDex.dex = 25;
    Check(life::StatFarmDexLockOnExit(mage, lowDex) == life::kStatLockUp,
          "below the build's DEX target the lock goes back to UP");
    life::Observation highDex = obs;
    highDex.dex = 40;
    Check(life::StatFarmDexLockOnExit(mage, highDex) == life::kStatLockLocked,
          "at or past it the ordinary end-of-build LOCK applies");

    // --- the need, and the goal it feeds ----------------------------------
    life::Memory mem;
    life::NeedConfig cfg;
    const prof::Profession* mageProf = prof::Find("mage");
    cfg.profession = mageProf;
    const std::vector<life::Need> needs = life::AssessNeeds(mage, mem, obs, cfg);
    const life::Need* strength = nullptr;
    for (const life::Need& n : needs)
        if (n.kind == life::NeedKind::NeedStrength) strength = &n;
    Check(strength != nullptr && !strength->blocked,
          "NeedStrength is raised for the caster plan");
    Check(strength && strength->urgency > 0.0,
          "and it is raised with an urgency, not as a blocked stub");

    if (smith) {
        const life::BuildPlan sp = life::PlanFromProfession(*smith);
        life::NeedConfig scfg;
        scfg.profession = smith;
        life::Observation so = obs;
        so.str = 50;
        bool raised = false;
        for (const life::Need& n : life::AssessNeeds(sp, mem, so, scfg))
            raised = raised || n.kind == life::NeedKind::NeedStrength;
        Check(!raised, "and NOT for a miner/smith whose own work still pays "
                       "STR");
    }

    // Hurt and loaded are the same two brakes every fighting errand carries.
    life::Observation hurt = obs;
    hurt.hp = 40;
    for (const life::Need& n : life::AssessNeeds(mage, mem, hurt, cfg)) {
        if (n.kind != life::NeedKind::NeedStrength) continue;
        Check(n.blocked, "too hurt to spar blocks the need rather than "
                         "raising it");
    }

    // The goal exists, is fed by this need, and is Training.
    Check(std::strcmp(life::GoalKindName(life::GoalKind::StatFarm),
                      "STAT_FARM") == 0,
          "the log line reads STAT_FARM");
    Check(life::FamilyOf(life::GoalKind::StatFarm) == life::GoalFamily::Training,
          "sparring for a stat counts as training in the day's balance");
    life::Planner planner;
    const std::vector<life::ScoredGoal> scored = planner.Score(needs, obs, mem);
    const life::ScoredGoal* farm = nullptr;
    for (const life::ScoredGoal& g : scored)
        if (g.kind == life::GoalKind::StatFarm) farm = &g;
    Check(farm != nullptr && farm->feasible && farm->score > 0.0,
          "STAT_FARM is scored and feasible for the caster");
}

void TestRecoveryForEveryArchetype() {
    for (const prof::Profession& profession : prof::All()) {
        life::NeedConfig cfg; cfg.profession = &profession;
        const life::BuildPlan plan = life::PlanFromProfession(profession);
        life::Memory memory;
        life::Observation obs = HealthyLumberjackAtWork();
        obs.hp = 12; obs.hpMax = 100; obs.bandages = 0;
        obs.healPotions = 0; obs.corpseKnown = true;
        auto needs = life::AssessNeeds(plan, memory, obs, cfg);
        const life::Need* heal = Find(needs, life::NeedKind::Heal);
        Check(heal && !heal->blocked, profession.id.c_str());
        life::Planner planner;
        planner.Cooldown(life::GoalKind::RecoverCorpse, obs.nowMs + 60000);
        auto goals = planner.Score(needs, obs, memory);
        bool healFeasible = false;
        for (const auto& goal : goals)
            if (goal.kind == life::GoalKind::Heal) healFeasible = goal.feasible;
        Check(healFeasible, "no-bandage recovery handoff reaches HEAL for every build");
        obs.corpseRecoveryAttempts = 3;
        needs = life::AssessNeeds(plan, memory, obs, cfg);
        const life::Need* corpse = Find(needs, life::NeedKind::RecoverCorpse);
        Check(corpse && !corpse->blocked, "attempt limit can clear persisted corpse");
    }
}

void TestHuntBankReturnPriority() {
    {
        const auto* archer = prof::Find("archer");
        auto ammo = HealthyLumberjackAtWork();
        ammo.skills = life::PlanFromProfession(*archer).skills;
        ammo.pack = {{"i_log", 20}};
        const auto shaft = life::ChooseCraft(*archer, ammo, 1);
        Check(shaft.item && std::string(shaft.item) == "i_arrow_shaft", "archer makes shafts before sale bows when ammunition is missing");
        ammo.pack = {{"i_arrow_shaft", 20}, {"i_feather", 20}};
        const auto arrow = life::ChooseCraft(*archer, ammo, 1);
        Check(arrow.item && std::string(arrow.item) == "i_arrow" && arrow.missing.empty(), "archer finishes arrows from shafts and feathers");
        ammo.pack = {{"i_arrow", 100}};
        ammo.bank = {{"i_arrow", 400}};
        Check(!life::NeedsArrowStock(*archer, ammo), "100 carried plus 400 banked satisfies the reserve");
        ammo.pack.clear();
        life::NeedConfig cfg; cfg.profession = archer;
        auto needs = life::AssessNeeds(life::PlanFromProfession(*archer), life::Memory{}, ammo, cfg);
        const auto* bank = Find(needs, life::NeedKind::NeedBank);
        Check(bank && !bank->blocked && bank->urgency == 1.0, "empty quiver withdraws banked arrows before hunting");
    }
    const prof::Profession* fighter = prof::Find("fencer");
    life::NeedConfig cfg; cfg.profession = fighter;
    auto plan = life::PlanFromProfession(*fighter);
    life::Memory memory;
    auto obs = HealthyLumberjackAtWork();
    obs.huntReturnPending = true;
    auto needs = life::AssessNeeds(plan, memory, obs, cfg);
    const auto* bank = Find(needs, life::NeedKind::NeedBank);
    Check(bank && !bank->blocked && bank->urgency == 1.0,
          "a small loot haul still has a bank return, without needing overload");
    const auto* training = Find(needs, life::NeedKind::NeedTraining);
    Check(training && training->blocked, "finish banking before another hunt");
    auto veteran = obs;
    veteran.huntReturnPending = false;
    veteran.skills = plan.skills;
    auto veteranNeeds = life::AssessNeeds(plan, memory, veteran, cfg);
    const auto* income = Find(veteranNeeds, life::NeedKind::NeedTraining);
    Check(income && !income->blocked, "a capped warrior continues hunting for income");
    obs.underAttack = true; obs.attackersOnMe = 1;
    needs = life::AssessNeeds(plan, memory, obs, cfg);
    life::Planner planner;
    auto goals = planner.Score(needs, obs, memory);
    double survival = 0, banking = 0;
    for (const auto& goal : goals) {
        if (goal.kind == life::GoalKind::Survive) survival = goal.score;
        if (goal.kind == life::GoalKind::Bank) banking = goal.score;
    }
    Check(survival > banking, "banking loot never outranks an attacker");
}

void TestMageCombatIsAnActivity() {
    const auto* mage = prof::Find("mage");
    const auto* scribe = prof::Find("scribe");
    Check(mage && life::WantsSpellCombat(*mage), "combat mage hunts with spells");
    Check(scribe && !life::WantsSpellCombat(*scribe), "utility caster keeps its own profession");
    life::NeedConfig cfg; cfg.profession = mage;
    auto plan = life::PlanFromProfession(*mage);
    life::Memory memory;
    auto obs = HealthyLumberjackAtWork();
    obs.weaponEquipped = false; obs.mana = 50; obs.hp = obs.hpMax = 50;
    const auto needs = life::AssessNeeds(plan, memory, obs, cfg);
    const auto* combat = Find(needs, life::NeedKind::NeedTraining);
    const auto* practice = Find(needs, life::NeedKind::NeedPractice);
    Check(combat && !combat->blocked, "mage can choose a hunt without a physical weapon");
    Check(practice && !practice->blocked, "mage retains casting/meditation training choice");
}


void TestAMageFightsWithItsWholeLadder() {
    Section("caster: the strongest spell it can cast now, not the cheapest");

    // Aurelius's own book, as the save recorded it (MORE1=0x386acdff):
    // Magic Arrow, Harm, Fireball, Poison, Lightning. Circles and mana are the
    // spell table's, not invented here.
    std::vector<life::SpellRung> book = {
        {/*spell*/18, /*circle*/1, /*mana*/4,  /*skill*/ 50, true},   // Magic Arrow
        {/*spell*/11, /*circle*/2, /*mana*/6,  /*skill*/150, true},   // Harm
        {/*spell*/27, /*circle*/3, /*mana*/9,  /*skill*/250, true},   // Fireball
        {/*spell*/31, /*circle*/4, /*mana*/11, /*skill*/350, true},   // Poison
        {/*spell*/38, /*circle*/4, /*mana*/20, /*skill*/450, true},   // Lightning
    };

    // Funded and rested: the top of the ladder this Magery reaches.
    Check(life::PickSpellRung(book, 500, 40, false, true) == 38,
          "a mage with skill and mana opens with Lightning, not Magic Arrow");

    // Mana short: it walks back DOWN the ladder rather than refusing to fight.
    Check(life::PickSpellRung(book, 500, 8, false, true) == 11,
          "at 8 mana the same mage casts Harm");
    Check(life::PickSpellRung(book, 500, 5, false, true) == 18,
          "at 5 mana it is Magic Arrow -- the cheap rung is a fallback, not the plan");
    Check(life::PickSpellRung(book, 500, 3, false, true) < 0,
          "with less mana than the cheapest rung there is nothing to cast");

    // Skill gates the ladder the same way -- a 30.0 mage never sees Lightning.
    Check(life::PickSpellRung(book, 300, 40, false, true) == 27,
          "Magery it does not have is not a rung it can stand on");

    // An unstocked rung is not castable, and the choice falls to the next one.
    std::vector<life::SpellRung> unstocked = book;
    unstocked[4].supplied = false;      // no sulfurous ash / mandrake for Lightning
    Check(life::PickSpellRung(unstocked, 500, 40, false, true) == 31,
          "a rung whose reagents are not in the pack is skipped");
    // ... but the reagent errand still has to KNOW about it.
    Check(life::PickSpellRung(unstocked, 500, 40, false, false) == 38,
          "ignoring supplies answers what to shop for, which is the whole ladder");

    // Healing is the other question: cheapest thing that heals.
    std::vector<life::SpellRung> heals = {
        {/*Heal*/4, 1, 11, 50, true},
        {/*Greater Heal*/29, 4, 20, 350, true},
    };
    Check(life::PickSpellRung(heals, 500, 40, true, true) == 4,
          "healing still takes the cheapest spell that does the job");
}

// A LADDER THE FLAGS DO NOT DELETE.
//
// The rows below are copied verbatim out of data/revolution_spells.tsv --
// including the detail that broke it: on this shard Harm and Magic Arrow carry
// `spellflag_targ_char` while Fireball, Poison and Lightning carry
// `spellflag_targ_obj`. The picker used to demand targ_char, so a mage holding
// all five could reach exactly two of them, and Aurelius spent the 2026-09-07
// 00:44 gate casting ids 10/11/12 with Fireball(18), Poison(20) and
// Lightning(30) sitting in her book.
void TestTargObjSpellsAreStillAttackRungs() {
    Section("caster: targ_obj is a rung, and Poison is a rung without a damage flag");

    const std::string tsv =
        "spell\tdefname\tname\tcircle\tminskill\tmana\tflags\treagents\n"
        "4\ts_heal\tHeal\t1\t100\t4\t"
        "spellflag_targ_char|spellflag_good|spellflag_heal\ti_reag_garlic\n"
        "5\ts_magic_arrow\tMagic Arrow\t1\t100\t4\t"
        "spellflag_targ_char|spellflag_harm|spellflag_damage\ti_reag_sulfur_ash\n"
        "12\ts_harm\tHarm\t2\t200\t6\t"
        "spellflag_targ_char|spellflag_harm|spellflag_damage\ti_reag_nightshade\n"
        "18\ts_fireball\tFireball\t3\t300\t9\t"
        "spellflag_targ_obj|spellflag_harm|spellflag_damage\ti_reag_black_pearl\n"
        "20\ts_poison\tPoison\t3\t300\t9\t"
        "spellflag_targ_obj|spellflag_harm|spellflag_tick\ti_reag_nightshade\n"
        "30\ts_lightning\tLightning\t4\t400\t11\t"
        "spellflag_targ_obj|spellflag_harm|spellflag_damage\ti_reag_mandrake_root\n"
        "39\ts_poison_field\tPoison Field\t5\t500\t14\t"
        "spellflag_targ_xyz|spellflag_harm|spellflag_field|spellflag_tick\ti_reag_nightshade\n";
    Check(spell::LoadSpellTableFromText(tsv) == 7, "the shard's own rows load");

    auto isRung = [](int id) {
        const spell::SpellDef* d = spell::DefForSpell(id);
        return d && spell::IsAttackRung(*d);
    };
    Check(isRung(12), "Harm (targ_char, damage) is a rung");
    Check(isRung(18), "Fireball is a rung even though the shard flags it targ_obj");
    Check(isRung(30), "Lightning is a rung even though the shard flags it targ_obj");
    Check(isRung(20),
          "Poison is a rung: harm that ticks is still harm, damage flag or not");
    Check(!isRung(39), "a field is not a rung -- it is not aimed at the mobile");
    Check(!isRung(4), "Heal is not an attack rung");

    const spell::SpellDef* heal = spell::DefForSpell(4);
    Check(heal && spell::IsHealRung(*heal), "Heal is the healing rung");
    const spell::SpellDef* harm = spell::DefForSpell(12);
    Check(harm && !spell::IsHealRung(*harm), "Harm never heals");

    // And the ladder that comes out of those rungs is the one the fight walks.
    // Aurelius's live numbers: Magery 51.2, mana 25, every reagent but black
    // pearl in the pack.
    std::vector<life::SpellRung> book;
    for (int id : {5, 12, 18, 20, 30}) {
        const spell::SpellDef* d = spell::DefForSpell(id);
        if (!d || !spell::IsAttackRung(*d)) continue;
        life::SpellRung r;
        r.spell = d->spell; r.circle = d->circle; r.mana = d->mana;
        r.minSkillTenths = d->minSkillTenths;
        r.supplied = std::strcmp(d->reagents[0], "i_reag_black_pearl") != 0;
        book.push_back(r);
    }
    Check(book.size() == 5, "all five of Aurelius's attack spells survive the filter");
    // Magery 51.2 is 512 tenths -- above Lightning's 400 -- and 25 mana pays
    // for its 11.
    Check(life::PickSpellRung(book, 512, 25, false, true) == 30,
          "Aurelius opens with Lightning, not with Harm");
    Check(life::PickSpellRung(book, 512, 10, false, true) == 20,
          "at 10 mana Poison (circle 3) outranks Harm (circle 2) -- and Poison "
          "only exists as a rung because harm-that-ticks now counts");
    Check(life::PickSpellRung(book, 512, 9, false, true) == 20,
          "at 9 mana it is still Poison; Fireball costs the same but the pack "
          "holds no black pearl");
    Check(life::PickSpellRung(book, 512, 6, false, true) == 12,
          "at 6 mana it walks down to Harm");
    Check(life::PickSpellRung(book, 512, 4, false, true) == 5,
          "at 4 mana Magic Arrow is the fallback, not the plan");
}

// WHAT A MAGE CARRIES, not what its current spell happens to need.
void TestReagentBand() {
    Section("caster: a standing band of all eight reagents, sized by the purse");

    int kinds = 0;
    const char* const* all = spell::Reagents(&kinds);
    Check(kinds == 8, "there are eight Magery reagents");
    for (int i = 0; i < kinds; ++i)
        Check(std::string(all[i]).compare(0, 7, "i_reag_") == 0,
              "every one is named by its defname");

    // Poor mage: the owner's 60-70 band.
    Check(spell::ReagentBandFor("i_reag_garlic", 500) == 60,
          "sixty of a plain reagent");
    Check(spell::ReagentBandFor("i_reag_mandrake_root", 500) == 70,
          "seventy of mandrake -- Recall and Lightning share the pile");
    Check(spell::ReagentBandFor("i_reag_black_pearl", 500) == 70,
          "seventy of black pearl -- Recall and Fireball share the pile");
    Check(spell::ReagentBandFor("i_reag_blood_moss", 500) == 60,
          "bloodmoss is Recall's alone, so it stays at the base band");

    // Rich mage: the band moves with the purse, it is not a constant.
    Check(spell::ReagentBandFor("i_reag_garlic", 12000) > 60 &&
          spell::ReagentBandFor("i_reag_garlic", 12000) <
              spell::ReagentBandFor("i_reag_garlic", 40000),
          "a deeper purse carries a deeper pouch");
    Check(spell::ReagentBandFor("i_reag_mandrake_root", 40000) >
          spell::ReagentBandFor("i_reag_garlic", 40000),
          "the burners keep their premium at every wealth");

    // The restock line is a low-water mark, not the band itself: a mage that
    // shopped every time it was one short would live at the counter, and the
    // Britain shelf does not restock that fast.
    for (int i = 0; i < kinds; ++i) {
        const i32 floorQty = spell::ReagentRestockFloor(all[i], 500);
        Check(floorQty > 0 && floorQty < spell::ReagentBandFor(all[i], 500),
              "the restock floor sits under the band and above empty");
    }
    // Aurelius's own pack, 2026-09-07 00:44: black pearl 0 and nightshade 13
    // are under the floor; mandrake 50 and spider silk 44 are not.
    Check(0 < spell::ReagentRestockFloor("i_reag_black_pearl", 9330),
          "an empty pearl pouch is short");
    Check(13 < spell::ReagentRestockFloor("i_reag_nightshade", 9330),
          "thirteen nightshade is short of the floor");
    Check(50 >= spell::ReagentRestockFloor("i_reag_mandrake_root", 9330),
          "fifty mandrake is not a reason to walk to the shop");
}

void TestAWonFightDoesNotHeatTheGround() {
    Section("memory: a ground that pays cools, a ground that hurts heats");

    life::Memory mem;
    const i64 t0 = 1000000;

    // The graveyard scared this character once: a low-health retreat.
    mem.NoteDanger(1420, 1500, 14, "a skeleton", 1.5, t0);
    const double afterScare = mem.DangerHeatAt(1420, 1500, t0);
    Check(afterScare > 1.49 && afterScare < 1.51, "a retreat is remembered at full weight");

    // Then it killed three things there without being hurt. Aurelius left this
    // yard at heat 3.20 with one kill and ZERO deaths and was refused it
    // (artifacts/validation_wave_2026-09-06.md D11).
    for (int i = 0; i < 3; ++i)
        mem.CoolDanger(1420, 1500, -life::kCreatureEvidenceCheapKill, t0);
    const double afterKills = mem.DangerHeatAt(1420, 1500, t0);
    Check(afterKills < afterScare, "kills at a ground lower its remembered heat");
    Check(afterKills >= 0.0, "a place is never remembered as safer than unknown");

    // And the ground stays usable: below the limit the hunt picker enforces.
    Check(afterKills < 3.0,
          "a winning session does not lock a novice out of its only ground");

    // Cooling somewhere never feared records nothing at all -- relief is not
    // a memory, it only ever answers one.
    Check(mem.DangerHeatAt(3000, 3000, t0) == 0.0, "precondition: unknown ground");
    mem.CoolDanger(3000, 3000, 0.5, t0);
    Check(mem.DangerHeatAt(3000, 3000, t0) == 0.0,
          "a kill somewhere never feared writes no memory");

    // A place that keeps hurting still heats: relief does not disarm fear.
    mem.NoteDanger(1420, 1500, 14, "a lich", 2.0, t0);
    Check(mem.DangerHeatAt(1420, 1500, t0) > afterKills + 1.9,
          "harm after relief still compounds onto what is left");
}


// ---------------------------------------------------------------------------
// D5: A CASTER'S HUNT READINESS INCLUDES THE POOL IT FIGHTS WITH.
//
// Aurelius, 2026-09-06 00:16:51-54: "meditating before the next cast", "You
// enter a meditative trance", and 1.5 s later TRAIN_COMBAT (0.45 x 130 = 58.5)
// superseded PRACTICE (45) and walked him back to the graveyard at mana 0.
// Repeated 00:23:03-00:23:08. Readiness asked about health, weight and
// unbanked loot only (Needs.cpp), so an empty mage read as ready.
// ---------------------------------------------------------------------------
void TestAMageMeditatesBeforeItHunts() {
    Section("needs: a caster with no mana is not ready to hunt");

    // A table in the shape tools/spellgen.py writes out of
    // runtime/scripts/spells/spells_magery.scp. Two attack rungs, so the bar
    // can be seen to come from the LADDER and not from a number.
    const std::string tsv =
        "spell\tdefname\tname\tcircle\tminskill\tmana\tflags\treagents\n"
        "5\ts_magic_arrow\tMagic Arrow\t1\t100\t5\t"
        "spellflag_targ_char|spellflag_harm|spellflag_damage\ti_reag_sulfur_ash\n"
        "27\ts_fireball\tFireball\t3\t300\t9\t"
        "spellflag_targ_char|spellflag_harm|spellflag_damage\ti_reag_black_pearl\n";
    Check(spell::LoadSpellTableFromText(tsv) == 2,
          "the two attack rungs load from data");

    const prof::Profession* mage = prof::Find("mage");
    if (!mage) { Check(false, "no mage profession"); return; }

    life::NeedConfig cfg;
    cfg.profession = mage;
    life::BuildPlan plan = life::PlanFromProfession(*mage);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 30;
    obs.intel = 25;                  // max mana IS Intelligence on this shard
    obs.gold = 500;
    obs.weight = 10; obs.maxWeight = 200;
    obs.skills.push_back({rules::kMagery, 500});
    obs.knownSpells = {5, 27};       // the book has been READ this session
    obs.pack.push_back({"i_reag_sulfur_ash", 20});
    obs.pack.push_back({"i_reag_black_pearl", 20});

    auto huntNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedTraining &&
                n.what == "hunt for income") return &n;
        return nullptr;
    };

    obs.mana = 4;
    const std::vector<life::Need> drained =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* empty = huntNeed(drained);
    Check(empty != nullptr, "the hunting need is still reported, not hidden");
    if (empty) {
        Check(empty->blocked,
              "a mage at 4 of 25 mana is NOT ready to hunt -- it meditates "
              "first, which is what it had already started doing");
        Check(empty->reason.find("meditate") != std::string::npos,
              "and the reason says so in words a log can be read from");
        Check(empty->reason.find("opening cast of 9") != std::string::npos,
              "the bar is the strongest rung a rested pool could cast "
              "(Fireball, 9 mana) -- read off the table, not a constant");
    }

    obs.mana = 9;                    // exactly one opening Fireball
    const std::vector<life::Need> nsRested =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* rested = huntNeed(nsRested);
    Check(rested != nullptr && !rested->blocked,
          "with the opening cast paid for, the same mage may go");

    // THE LADDER SETS THE BAR. Take away the Fireball reagent and the rung the
    // fight would open with falls to Magic Arrow, so 5 mana is now enough.
    obs.pack.clear();
    obs.pack.push_back({"i_reag_sulfur_ash", 20});
    obs.mana = 5;
    const std::vector<life::Need> nsArrow =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* arrow = huntNeed(nsArrow);
    Check(arrow != nullptr && !arrow->blocked,
          "an unsupplied rung is not the bar -- the pack decides what opens "
          "the fight, and Magic Arrow costs 5");
    obs.mana = 4;
    const std::vector<life::Need> nsUnder =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* under = huntNeed(nsUnder);
    Check(under != nullptr && under->blocked &&
              under->reason.find("opening cast of 5") != std::string::npos,
          "one mana under the cheapest rung it can supply is still not ready");

    // AND NO SWORDSMAN IS AFFECTED. Mana is a caster's gate, nobody else's.
    const prof::Profession* fencer = prof::Find("fencer");
    if (fencer) {
        life::NeedConfig fcfg;
        fcfg.profession = fencer;
        life::ResolveConsumableThresholds(fcfg, 5000);
        life::BuildPlan fplan = life::PlanFromProfession(*fencer);
        life::Observation fo;
        fo.inWorld = true;
        fo.hp = fo.hpMax = 40;
        fo.mana = 0;
        fo.gold = 5000;
        fo.bandages = fcfg.bandageFull;
        fo.weight = 10; fo.maxWeight = 400;
        fo.skills.push_back({rules::kFencing, 171});
        const std::vector<life::Need> fns =
            life::AssessNeeds(fplan, mem, fo, fcfg);
        const life::Need* fight = nullptr;
        for (const life::Need& n : fns)
            if (n.kind == life::NeedKind::NeedTraining) { fight = &n; break; }
        Check(fight != nullptr && !fight->blocked,
              "a fencer at 0 mana hunts exactly as before");
    }
}

// ---------------------------------------------------------------------------
// D12: THE FLOOR IS FOR LEAVING TOWN, NOT FOR STANDING IN A GRAVEYARD.
//
// Hector, 2026-09-06 00:29:47: REPLACE_EQUIPMENT at 130 "bandages=99 low=100"
// superseded SURVIVE one second after a kill and took him out of the Britain
// graveyard for a single bandage.
// ---------------------------------------------------------------------------
void TestOneBandageIsNotATripToTown() {
    Section("needs: a hunter does not leave the field one bandage short");

    const prof::Profession* fencer = prof::Find("fencer");
    if (!fencer) { Check(false, "no fencer"); return; }

    life::NeedConfig cfg;
    cfg.profession = fencer;
    life::ResolveConsumableThresholds(cfg, 5000);
    Check(cfg.bandageFieldLow < cfg.bandageLow,
          "a hunter's field line sits below its departure floor");
    Check(cfg.bandageFieldLow >= cfg.bandageLow - cfg.bandageFieldLow,
          "and it is the floor less ONE fight's worth, not half a pack");

    life::BuildPlan plan = life::PlanFromProfession(*fencer);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 51;
    obs.gold = 8000;
    obs.weight = 10; obs.maxWeight = 400;
    obs.skills.push_back({rules::kFencing, 500});

    auto bandageNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedEquipment && n.what == "bandages")
                return &n;
        return nullptr;
    };

    obs.bandages = cfg.bandageLow - 1;          // Hector's 99 against 100
    const std::vector<life::Need> nsOne =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* one = bandageNeed(nsOne);
    Check(one != nullptr, "the shortfall is still reported -- it IS short");
    if (one) {
        Check(one->urgency <= 0.10,
              "but at 0.10 x 260 = 26 it cannot outbid a hunt (58.5) or a "
              "fight in progress -- the top-up waits for the next town trip");
        Check(one->reason.find("one fight") != std::string::npos,
              "and the reason says what the line means");
    }

    obs.bandages = cfg.bandageFieldLow - 1;     // a fight's worth gone
    const std::vector<life::Need> nsGone =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* gone = bandageNeed(nsGone);
    Check(gone != nullptr && gone->urgency >= 0.5,
          "below the field line the errand is the full 0.50 again -- this is "
          "hysteresis, not a lower floor");

    // A LIFE THAT DOES NOT HUNT IS UNTOUCHED: one line, in town and out.
    const prof::Profession* scribe = prof::Find("scribe");
    if (scribe) {
        life::NeedConfig scfg;
        scfg.profession = scribe;
        life::ResolveConsumableThresholds(scfg, 5000);
        Check(scfg.bandageFieldLow == scfg.bandageLow,
              "a scribe restocks at its own single line");
    }
}

// ---------------------------------------------------------------------------
// D2: THE NPC FLOOR OPENS ONCE THE ANNOUNCE CYCLE ENDED UNANSWERED.
//
// Odessa shouted WTS six times to an empty bank (00:15:02-00:15:42) and
// NeedGold still read "no buyer known", so EARN_GOLD stayed BLOCKED --
// market::HasNpcBuyer was asked with its default playersDeclined=false, while
// econ::MaterialFloorOpen (VendorPolicy.cpp) opens exactly that route once the
// players have declined.
// ---------------------------------------------------------------------------
void TestAQuietMarketOpensTheNpcFloor() {
    Section("needs: after an unanswered WTS the route becomes an NPC");

    const prof::Profession* smith = prof::Find("miner_smith");
    if (!smith) { Check(false, "no miner_smith"); return; }

    life::NeedConfig cfg;
    cfg.profession = smith;
    life::BuildPlan plan = life::PlanFromProfession(*smith);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 50;
    obs.gold = 1000;
    obs.weight = 100; obs.maxWeight = 600;
    obs.pack.push_back({"i_ingot_iron", 600});

    auto sellNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedGold && n.what == "sell surplus")
                return &n;
        return nullptr;
    };

    obs.marketQuiet = false;
    const std::vector<life::Need> nsFirst =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* first = sellNeed(nsFirst);
    Check(first != nullptr, "a smith with spare ingots reports a sale need");
    if (first) {
        Check(first->blocked,
              "player-first: while the market has not been asked, an ingot has "
              "no NPC route and the need is a legible blocked state");
        Check(first->evidence.find("no buyer known") != std::string::npos,
              "and the evidence says which half is missing");
    }

    obs.marketQuiet = true;
    const std::vector<life::Need> nsAfter =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* after = sellNeed(nsAfter);
    Check(after != nullptr && !after->blocked,
          "once the announce cycle ended unanswered the floor opens and the "
          "errand is actionable rather than deadlocked");
    if (after)
        Check(after->evidence.find("an NPC") != std::string::npos,
              "and the route it names is the NPC counter");
}

// ---------------------------------------------------------------------------
// D7: A RENDEZVOUS IS NOT A SHOPPING TRIP.
//
// Odessa picked BUY_SUPPLIES (0.95 x 140 = 133) at 00:11:31, 00:16:52,
// 00:18:52, 00:27:46 and 00:29:50 and was handed off every time --
// "i_ingot_iron is a player-market good ... another profession makes this, not
// a shopkeeper" -- with goal_spinning=BUY_SUPPLIES at 00:25:45, while MINE
// (58.5), the work that makes the ingots, lost every cycle.
// ---------------------------------------------------------------------------
void TestAPlayerMadeInputIsNotAShoppingTrip() {
    Section("needs: an input no shopkeeper sells is blocked, not shopped for");

    const prof::Profession* tinker = prof::Find("merchant_tinker");
    if (!tinker) { Check(false, "no merchant_tinker"); return; }

    life::NeedConfig cfg;
    cfg.profession = tinker;
    life::BuildPlan plan = life::PlanFromProfession(*tinker);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 50;
    obs.gold = obs.goldOnHand = 500;
    obs.weight = 10; obs.maxWeight = 400;
    obs.skills.push_back({rules::kTinkering, 500});
    obs.skills.push_back({rules::kMining, 500});
    obs.toolsHeld.push_back("tinker tools");

    const std::vector<life::Need> ns = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* sup = nullptr;
    for (const life::Need& n : ns)
        if (n.kind == life::NeedKind::NeedSupplies &&
            n.what == "buy craft inputs") { sup = &n; break; }
    Check(sup != nullptr, "the tinker still reports what it is short of");
    if (sup) {
        Check(sup->blocked && sup->urgency == 0.0,
              "but a player-market input is BLOCKED at need level: the errand "
              "that would be picked can only hand itself off");
        Check(sup->reason.find("rendezvous") != std::string::npos,
              "and the reason names the market it really belongs to");
    }

    // AND THE VENDOR TABLE STILL KEEPS PRECEDENCE. A scribe's blank scrolls
    // are produced by the lumberjack line too, but the mage shop sells them,
    // so that trip is not touched (market::RouteForInput's own rule).
    Check(life::SupplierTradeFor("i_scroll_blank") != nullptr &&
              life::SupplierTradeFor("i_ingot_iron") == nullptr,
          "the shopkeeper table is what separates the two cases");
}

// ---------------------------------------------------------------------------
// D4: A FUNDED FIGHTER DOES NOT WALK TO A FLOCK.
//
// Hector (7,962 gold against a reserve of 350) picked HARVEST_WOOL at 00:16:55
// and again at 00:24:49 -- NeedWoolIncome 0.30 x 140 = 42 beating blocked
// training -- and both attempts died 75 s later on "no pasture within 400
// tiles of home (1813,2825)", the second after walking him off the graveyard.
// ---------------------------------------------------------------------------
void TestAFundedFighterDoesNotGoShearing() {
    Section("needs: wool is a fighter's INCOME, not a chore for a full purse");

    const prof::Profession* fencer = prof::Find("fencer");
    if (!fencer) { Check(false, "no fencer"); return; }

    life::NeedConfig cfg;
    cfg.profession = fencer;
    life::ResolveConsumableThresholds(cfg, 8000);
    life::BuildPlan plan = life::PlanFromProfession(*fencer);
    life::Memory mem;

    life::Observation obs;
    obs.inWorld = true;
    obs.hp = obs.hpMax = 51;
    obs.gold = 7962;                            // Hector's purse
    obs.bandages = cfg.bandageFull;
    obs.weight = 10; obs.maxWeight = 400;
    obs.skills.push_back({rules::kFencing, 500});

    auto woolNeed = [](const std::vector<life::Need>& ns) -> const life::Need* {
        for (const life::Need& n : ns)
            if (n.kind == life::NeedKind::NeedWoolIncome) return &n;
        return nullptr;
    };

    const std::vector<life::Need> nsRich =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* rich = woolNeed(nsRich);
    Check(rich != nullptr, "the wool need is still reported for a fencer");
    if (rich) {
        Check(rich->blocked && rich->urgency == 0.0,
              "with 7,962 gold against a reserve of 350 the shearing trip is "
              "BLOCKED -- the errand exists for income this life does not need");
        Check(rich->reason.find("reserve") != std::string::npos,
              "and the reason names the test it failed");
    }

    // POVERTY STILL OPENS IT -- the owner's original case, unchanged.
    obs.gold = 100;
    const std::vector<life::Need> nsPoor =
        life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* poor = woolNeed(nsPoor);
    Check(poor != nullptr && !poor->blocked && poor->urgency >= 0.5,
          "under its own reserve the flock is the nearest coin, exactly as "
          "the 2026-09-02 ruling asked");
}

// ---------------------------------------------------------------------------
// THE NEED/HANDLER CONTRACT (docs/NEED_HANDLER_CONTRACT.md).
//
// One assertion, five pairs: "the handler would refuse" must imply "the need
// is blocked, in the handler's own words". Before this contract existed, seven
// needs in one wave scored high, were picked, and were handed away a tick
// later on a fact the need had never been shown.
//
// Arm A is tested by comparing life::CanAct against the decision function the
// HANDLER runs -- DecideRecovery, market::RouteForInput -- not against a copy
// of the rule written here, which would only prove the test agrees with
// itself. Arm B is tested by writing the block the handler writes and
// requiring AssessNeeds to fall silent in the same sentence.
// ---------------------------------------------------------------------------
void TestContractCorpseGateMatchesTheHandler() {
    Section("contract: the corpse need refuses exactly when recovery would");

    const life::BuildPlan plan = life::FrontierLumberjackSwordsman();
    life::NeedConfig cfg;

    // Every combination of the four facts DecideRecovery weighs on a live
    // character: how hurt, whether something is on us, how many trips have
    // been spent, and how far away the corpse is.
    const int  hps[]       = {12, 45, 60};
    const bool threats[]   = {false, true};
    const int  attempts[]  = {0, 2, 3, 5};
    const int  distances[] = {0, 2, 40};

    int compared = 0, agreed = 0;
    for (int hp : hps)
    for (bool threatened : threats)
    for (int tries : attempts)
    for (int dist : distances) {
        life::Memory mem;
        life::Observation obs = HealthyLumberjackAtWork();
        obs.hp = hp; obs.hpMax = 60;
        obs.underAttack = threatened;
        obs.corpseKnown = true;
        obs.corpseRecoveryAttempts = tries;
        obs.corpseX = obs.x + dist; obs.corpseY = obs.y;

        // What the HANDLER would do with the same sight: Survive.cpp fills
        // this struct from exactly these observation fields.
        life::RecoverySight see;
        see.threatened = threatened;
        see.corpseKnown = true;
        see.corpseDistance = dist;
        see.hpFraction = obs.HpFraction();
        see.attemptsSoFar = tries;
        life::RecoveryTuning tune;
        tune.minHpToReturn = cfg.healHpFraction;
        const life::RecoveryPlan handler = life::DecideRecovery(see, tune);

        const std::vector<life::Need> ns = life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* corpse = Find(ns, life::NeedKind::RecoverCorpse);
        if (!corpse) continue;
        ++compared;
        // "Recover" is the handler REFUSING the corpse for now. Every other
        // step is work it would actually do -- including Abandon, which is how
        // the death record gets cleared, so blocking there would leave the
        // need carried forever with no way to end it.
        const bool handlerRefuses = handler.step == life::RecoveryStep::Recover;
        if (corpse->blocked == handlerRefuses) ++agreed;
    }
    Check(compared == 72, "every corpse case produced a need to compare");
    Check(agreed == compared,
          "the need blocks in exactly the cases DecideRecovery answers "
          "'recover first', and in no others");

    // The two ends of that matrix, named, so a failure says which way it broke.
    {
        life::Memory mem;
        life::Observation obs = HealthyLumberjackAtWork();
        obs.hp = 12; obs.hpMax = 60;
        obs.corpseKnown = true;
        obs.corpseX = obs.x + 40; obs.corpseY = obs.y;
        const std::vector<life::Need> hurt = life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* hurtCorpse = Find(hurt, life::NeedKind::RecoverCorpse);
        Check(hurtCorpse && hurtCorpse->blocked &&
                  hurtCorpse->reason.find("heal first") != std::string::npos,
              "at a fifth of its health the corpse run is blocked in the "
              "handler's words, so HEAL wins on its own need");

        obs.corpseRecoveryAttempts = 3;
        const std::vector<life::Need> spent = life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* spentCorpse = Find(spent, life::NeedKind::RecoverCorpse);
        Check(spentCorpse && !spentCorpse->blocked,
              "but with its trips spent the need stays open, because "
              "abandonment is the handler step that clears the death record");
    }
}

void TestContractArmBSilencesTheNeed() {
    Section("contract: a handler's recorded refusal silences the need");

    const life::BuildPlan plan = life::FrontierLumberjackSwordsman();
    life::NeedConfig cfg;
    cfg.sessionIndex = 4;

    life::Observation obs = HealthyLumberjackAtWork();
    obs.hp = 10; obs.hpMax = 60;      // HEAL is wide open on the health bar
    obs.bandages = 0; obs.healPotions = 0; obs.gold = 0;

    {
        life::Memory mem;
        const std::vector<life::Need> before = life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* h = Find(before, life::NeedKind::Heal);
        Check(h && !h->blocked && h->urgency > 0.0,
              "with nothing recorded, a bleeding character wants to heal");
    }
    {
        // The sentence DoHeal writes at HealStep::Stuck (Survive.cpp).
        life::Memory mem;
        const char* stuck = "nothing to heal with and nothing on the way";
        life::NoteNeedBlocked(mem, life::NeedKind::Heal, stuck,
                              life::BlockScope::Window, cfg, obs.nowMs);
        const std::vector<life::Need> after = life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* h = Find(after, life::NeedKind::Heal);
        Check(h && h->blocked && h->urgency == 0.0,
              "once the errand has said it is stuck, the need stops scoring "
              "instead of winning the pick every cooldown (D13)");
        Check(h && h->reason == stuck,
              "and BLOCKED_NEED carries the handler's sentence, not a guess");

        // THE ONE BLOCK THAT MUST NOT OUTLIVE ITS FACT. A bandage in the pack
        // ends "nothing to heal with"; a stand-down with the medicine in hand
        // is the deadlock the block exists to prevent.
        life::Observation healed = obs;
        healed.bandages = 5;
        const std::vector<life::Need> supplied =
            life::AssessNeeds(plan, mem, healed, cfg);
        const life::Need* h2 = Find(supplied, life::NeedKind::Heal);
        Check(h2 && !h2->blocked,
              "a bandage arriving reopens HEAL immediately -- the block is "
              "only good while the fact behind it is");

        // A WINDOW EXPIRES ON ITS OWN. The shelf restocks, the flock walks
        // back; a windowed block is not a life sentence.
        life::Observation later = obs;
        later.nowMs = obs.nowMs + life::kBlockWindowMs + 1;
        const std::vector<life::Need> aged = life::AssessNeeds(plan, mem, later, cfg);
        const life::Need* h3 = Find(aged, life::NeedKind::Heal);
        Check(h3 && !h3->blocked, "and the window runs out by itself");
    }
    {
        // Session scope: what DoMakeCloth writes for "no pasture near home".
        const prof::Profession* fencer = prof::Find("fencer");
        if (!fencer) { Check(false, "no fencer"); return; }
        life::NeedConfig wool;
        wool.profession = fencer;
        wool.sessionIndex = 4;
        life::ResolveConsumableThresholds(wool, 100);
        const life::BuildPlan fplan = life::PlanFromProfession(*fencer);
        life::Memory mem;

        life::Observation poor;
        poor.inWorld = true;
        poor.nowMs = 5000000;
        poor.hp = poor.hpMax = 51;
        poor.gold = 100;
        poor.bandages = wool.bandageFull;
        poor.weight = 10; poor.maxWeight = 400;
        poor.skills.push_back({rules::kFencing, 500});

        const std::vector<life::Need> open = life::AssessNeeds(fplan, mem, poor, wool);
        const life::Need* w = Find(open, life::NeedKind::NeedWoolIncome);
        Check(w && !w->blocked, "a broke fencer would go shearing");

        const char* noPasture =
            "no pasture within 400 tiles of Britain (1500,1600) -- not "
            "walking across the map for wool";
        life::NoteNeedBlocked(mem, life::NeedKind::NeedWoolIncome, noPasture,
                              life::BlockScope::Session, wool, poor.nowMs);
        const std::vector<life::Need> shut = life::AssessNeeds(fplan, mem, poor, wool);
        const life::Need* w2 = Find(shut, life::NeedKind::NeedWoolIncome);
        Check(w2 && w2->blocked && w2->reason == noPasture,
              "after the trip failed on the pasture table the need is quiet in "
              "the same words -- no second three-minute walk (D4)");

        // NEXT SESSION IS A NEW WORLD. The one durable clock a need has.
        life::NeedConfig tomorrow = wool;
        tomorrow.sessionIndex = 5;
        const std::vector<life::Need> next =
            life::AssessNeeds(fplan, mem, poor, tomorrow);
        const life::Need* w3 = Find(next, life::NeedKind::NeedWoolIncome);
        Check(w3 && !w3->blocked,
              "and a session-scoped block ends with the session, not with the "
              "process");
    }
}

void TestContractArmAMatchesTheErrand() {
    Section("contract: the need refuses the errands the handler refuses");

    // --- BUY_SUPPLIES: the gate and market::RouteForInput are one rule -----
    bool routesAgree = true, reasonsGiven = true;
    for (const prof::Profession& p : prof::All()) {
        life::NeedConfig cfg; cfg.profession = &p;
        life::Memory mem;
        life::Observation obs;
        obs.inWorld = true; obs.nowMs = 5000000;
        obs.hp = obs.hpMax = 50;
        obs.gold = 500;
        obs.weight = 10; obs.maxWeight = 400;
        for (const std::string& input : p.consumes) {
            const market::SupplyRoute route = market::RouteForInput(
                p, input.c_str(), life::SupplierTradeFor(input) != nullptr);
            const life::GateVerdict v =
                life::CanAct(life::NeedKind::NeedSupplies,
                             life::GateSubject{input.c_str(), false}, mem, obs, cfg);
            if (v.ok != (route == market::SupplyRoute::NpcVendor)) routesAgree = false;
            if (!v.ok && v.why.empty()) reasonsGiven = false;
        }
    }
    Check(routesAgree,
          "across every profession's inputs, the need shops for exactly what "
          "an NPC vendor can answer for -- the errand's own route rule (D7)");
    Check(reasonsGiven, "and a refused input always carries a reason");

    // --- TRAIN_COMBAT readiness: the loot still to bank -------------------
    {
        const life::BuildPlan plan = life::FrontierLumberjackSwordsman();
        life::NeedConfig cfg;
        life::Memory mem;
        life::Observation obs = HealthyLumberjackAtWork();
        obs.huntReturnPending = true;

        const life::GateVerdict v =
            life::CanAct(life::NeedKind::NeedTraining,
                         life::GateSubject{nullptr, true}, mem, obs, cfg);
        Check(!v.ok, "a hunt's loot still to secure refuses the next hunt");

        const std::vector<life::Need> ns = life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* t = Find(ns, life::NeedKind::NeedTraining);
        Check(t && t->blocked && t->reason == v.why,
              "and the need says so in the same words instead of scoring the "
              "walk to the graveyard first (D1/D5)");
    }

    // --- and the ready case is untouched ----------------------------------
    {
        const life::BuildPlan plan = life::FrontierLumberjackSwordsman();
        life::NeedConfig cfg;
        life::Memory mem;
        const life::Observation obs = HealthyLumberjackAtWork();
        const life::GateVerdict v =
            life::CanAct(life::NeedKind::NeedTraining,
                         life::GateSubject{nullptr, true}, mem, obs, cfg);
        Check(v.ok && v.why.empty(),
              "an armed, unhurt, unburdened character is refused nothing");
        // Not "training is unblocked": a lumberjack at a work site may still
        // be blocked for reasons that have nothing to do with the contract
        // (no hunting ground within reach, say). The claim is narrower and
        // exactly the contract's: the readiness refusal is GONE once the
        // fact behind it is.
        const std::vector<life::Need> ns = life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* t = Find(ns, life::NeedKind::NeedTraining);
        Check(t && t->reason.find("secure the last hunt") == std::string::npos,
              "so no readiness sentence is attached once the loot is banked");
    }
}

int main(int argc, char** argv) {
    Check(uo::life::BankInputReserve(true, "i_reag_mandrake_root", 5) == uo::life::kReagentCarry,
          "caster reagent deposits preserve the same stock withdrawals restore");
    Check(uo::life::BankInputReserve(false, "i_reag_nightshade", 5) == 10 &&
          uo::life::BankInputReserve(true, "i_log", 5) == 10,
          "crafting inputs retain the normal working batch");

    std::printf("m4_life\n");
    const std::string tmpDir = (argc > 1) ? argv[1] : ".";

    TestMageCombatIsAnActivity();
    TestHuntBankReturnPriority();
    TestRecoveryForEveryArchetype();
    TestJsonRoundTrip();
    TestBuildPlanLegality();
    TestMemory();
    TestNeeds();
    TestPlanner();
    TestCombatWaitsForBandages();
    TestGatherLogsSurplusYieldsToTrade();
    TestStandingInTheWoodsOutscoresAComfortErrand();
    TestStateRoundTrip();
    TestStore(tmpDir);
    TestReconciliation();
    TestHintsVersusEarnedStands();
    TestDangerHeatIsCapped();
    TestCreatureMemory();
    TestSchemaV1StillLoads();
    TestIdentityId();
    TestSurplusNeedsSomewhereToGo();
    TestUnsatisfiableNeedIsBlocked();
    TestEveryLifeAsksForItsOwnTools();
    TestNerveIsPerProfession();
    TestNoSkillGainRegionBlocksPractice();
    TestAMageWantsItsBookFilled();
    TestScrollShoppingStandsDownWhenNobodySells();
    TestACorneredFighterMayHunt();
    TestAPoorFighterMakesItsOwnBandages();
    TestTheFightingFloorIsAHundred();
    TestExploringBeatsStandingStill();
    TestGearIsCheckedAndClassBound();
    TestAGoalThatSucceedsAtNothingIsStopped();
    TestFamilySatiationBreaksAMonotonousDay();
    TestSatiationLetsSomethingElseHaveATurn();
    TestOneTrainerIsNotTheTrade();
    TestGoalCooldownStopsChurn();
    TestGoalHistogramArithmetic();
    TestACooledExploreYieldsToIdleBriefly();
    TestColouredOreStillWantsTheForge();
    TestClothIsBoughtFromPlayersBeforeItIsSheared();
    TestABoltIsCutNotBanked();
    TestYarnInThePackIsWorkNotStock();
    TestTheWoolChainBookkeeping();
    TestThreeEmptyClothStepsStandTheGoalDown();
    TestPracticeChecksTheReagentPouch();
    TestAnEmptyPouchIsAShoppingErrand();
    TestStrengthAWizardCannotEarnByCasting();
    TestASmithKeepsIngotsToWorkWith();
    TestAPureMageIsNotSentShoppingForBandages();
    TestADrainedPotionShelfDampsTheNeed();
    TestAMageFightsWithItsWholeLadder();
    TestTargObjSpellsAreStillAttackRungs();
    TestReagentBand();
    TestAWonFightDoesNotHeatTheGround();
    TestAMageMeditatesBeforeItHunts();
    TestOneBandageIsNotATripToTown();
    TestAQuietMarketOpensTheNpcFloor();
    TestAPlayerMadeInputIsNotAShoppingTrip();
    TestAFundedFighterDoesNotGoShearing();
    TestContractCorpseGateMatchesTheHandler();
    TestContractArmBSilencesTheNeed();
    TestContractArmAMatchesTheErrand();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
