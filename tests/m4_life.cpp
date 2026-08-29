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

#include "uo/json.h"
#include "uo/life.h"
#include "uo/professions.h"
#include "uo/rules.h"

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
        Check(exhausted != nullptr && exhausted->blocked,
              "corpse recovery is bounded: three failed attempts and it stops");
    }
}

// --------------------------------------------------------------------------
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

    const life::PersistentState st = SampleState();
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

    auto needsFor = [&](const prof::Profession& p, const char* item, i32 qty) {
        life::NeedConfig cfg;
        cfg.profession = &p;
        life::BuildPlan plan = life::PlanFromProfession(p);
        life::Observation obs;
        obs.inWorld = true;
        obs.hp = obs.hpMax = 25;
        obs.gold = 1000;                        // NOT broke -- the whole point
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

    // i_bandage is not in the M3.7 vendor matrix at all, so it grades UNKNOWN
    // and the policy refuses it. The live lumberjack had 1000 gold, so this
    // need looked satisfiable, won the scoring at 130, and sat on a 30-second
    // retry forever. It never chopped a log.
    const std::vector<life::Need> needs = life::AssessNeeds(plan, mem, obs, cfg);
    bool found = false;
    for (const life::Need& n : needs) {
        if (n.kind != life::NeedKind::NeedEquipment) continue;
        if (n.what != "bandages") continue;
        found = true;
        Check(n.blocked,
              "the bandage need is BLOCKED despite a purse full of gold");
        Check(!n.evidence.empty(), "and says why, so the gap is legible");
    }
    Check(found, "the bandage need is still reported, not silently dropped");

    // With the need blocked, real work wins.
    life::Planner planner;
    const std::vector<life::ScoredGoal> goals = planner.Score(needs, obs, mem);
    for (const life::ScoredGoal& g : goals) {
        if (g.kind != life::GoalKind::ReplaceEquipment) continue;
        Check(!g.feasible, "REPLACE_EQUIPMENT is not a feasible goal here");
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
    Check(up >= 0.55,
          "a family that has monopolised the day yields at least ~60%");
    Check(up <= 0.60 + 1e-9, "but the yielding is bounded");
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

    // Mana is a SEPARATE gate and must not be confused with it: plenty of
    // mana in a shrine is still pointless, and no mana in the world is a
    // different problem with a different answer.
    obs.inNoGainRegion = false;
    obs.mana = 0;
    const std::vector<life::Need> nsDry = life::AssessNeeds(plan, mem, obs, cfg);
    const life::Need* dry = practiceNeed(nsDry);
    if (dry) {
        Check(dry->blocked, "no mana also blocks");
        Check(dry->reason.find("mana") != std::string::npos,
              "and that one blames the mana, which is the fixable thing");
    }
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

}  // namespace


int main(int argc, char** argv) {
    std::printf("m4_life\n");
    const std::string tmpDir = (argc > 1) ? argv[1] : ".";

    TestJsonRoundTrip();
    TestBuildPlanLegality();
    TestMemory();
    TestNeeds();
    TestPlanner();
    TestStateRoundTrip();
    TestStore(tmpDir);
    TestReconciliation();
    TestHintsVersusEarnedStands();
    TestDangerHeatIsCapped();
    TestSchemaV1StillLoads();
    TestIdentityId();
    TestSurplusNeedsSomewhereToGo();
    TestUnsatisfiableNeedIsBlocked();
    TestEveryLifeAsksForItsOwnTools();
    TestNerveIsPerProfession();
    TestNoSkillGainRegionBlocksPractice();
    TestFamilySatiationBreaksAMonotonousDay();
    TestSatiationLetsSomethingElseHaveATurn();
    TestOneTrainerIsNotTheTrade();
    TestGoalCooldownStopsChurn();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
