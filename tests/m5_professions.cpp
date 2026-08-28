// M5.1 -- the profession catalogue, checked against BOTH rulesets.
//
// Revolution's creation rule (two skills at 50.0, ~50 stat points) and the
// finished-build caps (700.0 skills, 225/100 stats) are different rules about
// different moments in a character's life, and conflating them is exactly how
// a bot ends up either uncreatable or over-budget. Every catalogue entry is
// checked against both here, so a bad table entry fails at build time rather
// than at character creation on a live shard.
//
// No server, no MULs, no world data.

#include "uo/life.h"
#include "uo/professions.h"
#include "uo/rules.h"

#include <cstdio>
#include <set>
#include <string>

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
void TestEveryEntryIsLegal() {
    Section("catalogue: every profession is a legal Revolution life");

    const rules::Profile& rp = rules::Revolution();
    const auto& all = prof::All();
    Check(all.size() >= 5, "the catalogue holds the M5.2 archetypes");

    std::set<std::string> ids;
    for (const prof::Profession& p : all) {
        const prof::ProfCheck c = prof::Validate(rp, p);
        if (!c.ok) {
            std::printf("  FAIL: '%s' is not legal: %s (skill %d)\n",
                        p.id.c_str(), prof::ProfViolationName(c.violation),
                        c.skillId);
            ++g_failures;
        }
        ++g_checks;

        Check(ids.insert(p.id).second, "profession ids are unique");
        Check(!p.label.empty(), "every profession has a human-readable label");

        // The creation rule, restated per entry so a failure names the entry.
        const i32 statSum = p.startStr + p.startDex + p.startInt;
        if (statSum != prof::kRevolutionStartStatTotal) {
            std::printf("  FAIL: '%s' starts with %d stat points, not %d\n",
                        p.id.c_str(), statSum, prof::kRevolutionStartStatTotal);
            ++g_failures;
        }
        ++g_checks;

        Check(p.startSkillA != p.startSkillB,
              "the two starting skills are actually different");
        Check(!p.income.empty(), "every profession has a way to earn");
    }
}

// --------------------------------------------------------------------------
void TestCreationFitsTheServer() {
    Section("creation: the Revolution rule fits inside what Source-X allows");

    // 50.0 + 50.0 = 100.0, which is EXACTLY CChar::InitPlayer's creation
    // ceiling. If either constant moves, character creation silently starts
    // getting clamped, so this is asserted rather than assumed.
    const i32 startSum =
        prof::kRevolutionStartSkillEach * prof::kRevolutionStartSkillCount;
    Check(startSum == prof::kServerCreateSkillSumMax,
          "two skills at 50.0 exactly fills the server's 100.0 creation cap");
    Check(prof::kRevolutionStartSkillEach <= prof::kServerCreateSkillEachMax,
          "50.0 per skill is within the server's per-skill creation cap");
    Check(prof::kRevolutionStartStatTotal < prof::kServerCreateStatSumMax,
          "Revolution's 50 stat points sit UNDER the server's 80 -- the rule "
          "is our choice, not a limit we are fighting");

    for (const prof::Profession& p : prof::All()) {
        Check(p.startStr <= prof::kServerCreateStatEachMax &&
              p.startDex <= prof::kServerCreateStatEachMax &&
              p.startInt <= prof::kServerCreateStatEachMax,
              "no starting stat exceeds the server's per-stat creation cap");
    }
}

// --------------------------------------------------------------------------
void TestBuildsAreEarnedNotGranted() {
    Section("catalogue: targets are goals, not grants");

    const rules::Profile& rp = rules::Revolution();
    for (const prof::Profession& p : prof::All()) {
        // A target the character already has at creation would be a grant.
        // Every target must be ABOVE what creation gives, or be a skill
        // creation does not touch at all.
        for (const prof::SkillTargetSpec& t : p.targets) {
            const bool isStartSkill =
                t.skillId == p.startSkillA || t.skillId == p.startSkillB;
            if (isStartSkill) {
                // Equal is legitimate and means "hold here" -- the alchemist
                // wants Magery 50.0 and no more. Only a target BELOW the
                // starting value would be incoherent.
                Check(t.tenths >= prof::kRevolutionStartSkillEach,
                      "a target on a starting skill is never BELOW the 50.0 "
                      "creation already grants");
            }
        }
        const prof::ProfCheck c = prof::Validate(rp, p);
        Check(c.targetSkillSum <= rp.totalSkillCapTenths,
              "the finished build fits the 700-point budget");
        Check(c.targetStatSum <= rp.totalStatCap,
              "the finished build fits the 225 stat budget");
    }
}

// --------------------------------------------------------------------------
void TestRefusals() {
    Section("catalogue: illegal professions are refused");

    const rules::Profile& rp = rules::Revolution();
    const prof::Profession* base = prof::Find("lumberjack_swordsman");
    Check(base != nullptr, "the M4 character is in the catalogue");
    if (!base) return;

    {   // One starting skill is not the Revolution rule.
        prof::Profession bad = *base;
        bad.startSkillB = bad.startSkillA;
        Check(prof::Validate(rp, bad).violation ==
                  prof::ProfViolation::NotTwoStartSkills,
              "a profession with one starting skill is refused");
    }
    {   // 80 stat points is the SERVER's allowance, not Revolution's rule.
        prof::Profession bad = *base;
        bad.startStr = 40; bad.startDex = 35; bad.startInt = 5;   // M4's split
        Check(prof::Validate(rp, bad).violation ==
                  prof::ProfViolation::StartStatsWrongTotal,
              "M4's legal-but-not-Revolution 80-point split is now refused");
    }
    {   // Resisting Spells did not run on Revolution.
        prof::Profession bad = *base;
        bad.targets.push_back({rules::kMagicResistance, 1000, 1, false});
        bad.unresolvedTenths = 1000;
        Check(prof::Validate(rp, bad).violation ==
                  prof::ProfViolation::InactiveSkill,
              "an inactive skill is refused as a target");
    }
    {   // Over the 700 budget.
        prof::Profession bad = *base;
        bad.unresolvedTenths += 1;
        Check(prof::Validate(rp, bad).violation ==
                  prof::ProfViolation::SkillBudgetExceeded,
              "700.1 points is refused");
    }
    {   // A life with no income.
        prof::Profession bad = *base;
        bad.income.clear();
        Check(prof::Validate(rp, bad).violation == prof::ProfViolation::NoIncome,
              "a profession with no way to earn is refused");
    }
}

// --------------------------------------------------------------------------
void TestArchetypesDiffer() {
    Section("catalogue: professions are meaningfully different");

    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    const prof::Profession* ms = prof::Find("miner_smith");
    const prof::Profession* mg = prof::Find("mage");
    Check(lj && ms && mg, "the three live archetypes are present");
    if (!lj || !ms || !mg) return;

    // The M5 gate asks for "meaningfully different behaviour". These are the
    // fields the behaviour layers actually branch on, so if they were equal
    // the professions would be cosmetic.
    Check(lj->gathers != ms->gathers, "they gather different resources");
    Check(lj->startStr != mg->startStr || lj->startInt != mg->startInt,
          "a mage and a lumberjack do not start with the same body");
    Check(mg->riskTolerance < lj->riskTolerance,
          "a mage is more cautious than a swordsman");
    Check(ms->goldReserve != lj->goldReserve,
          "different lives hold back different reserves");
    Check(!mg->consumes.empty(),
          "the mage must BUY reagents -- its newbie kit contains none, which "
          "is what makes it the archetype that proves the supplier path");
    Check(lj->consumes.empty(),
          "the lumberjack needs nothing from another character");
    Check(!ms->consumes.empty() && !ms->produces.empty(),
          "the smith both consumes and produces -- the M7 interdependence link");
}

// --------------------------------------------------------------------------
void TestMinerConstraintIsRecorded() {
    Section("miner: the pickaxe STR gate is expressed, not hidden");

    const prof::Profession* ms = prof::Find("miner_smith");
    Check(ms != nullptr, "the miner/smith exists");
    if (!ms) return;

    // Mining needs a WIELDED tool (skill45 @PreStart reads SRC.WEAPON), the
    // shovel cannot be worn (tiledata layer 0, verified with uo_mul_dump), and
    // the pickaxe carries ReqStr=50. Under a 50-point creation pool the
    // character therefore cannot mine on day one.
    bool pickaxeWielded = false;
    for (const prof::ToolNeed& t : ms->tools) {
        if (t.name == "pickaxe") pickaxeWielded = t.mustBeWielded;
    }
    Check(pickaxeWielded,
          "the pickaxe is marked as needing to be WIELDED, which is why its "
          "ReqStr=50 gates the whole profession");
    Check(ms->startStr < 50,
          "the miner does NOT dump all 50 points into STR just to lift a "
          "pickaxe on day one");
    Check(ms->targetStr >= 50,
          "STR 50 is a stated TARGET -- the gate is something to grow into");

    // It starts as a smith, which the newbie kit actually supports: BLACKSMITHING
    // hands over tongs and 50 iron ingots.
    bool craftsFirst = false;
    for (prof::Income i : ms->income) {
        if (i == prof::Income::Craft) { craftsFirst = true; break; }
        if (i == prof::Income::Gather) break;
    }
    Check(craftsFirst,
          "the miner/smith earns by CRAFTING before gathering -- it can smith "
          "the 50 starting ingots long before it can lift the pickaxe");
}


// --------------------------------------------------------------------------
void TestPlanFromProfession() {
    Section("plan: a profession becomes a legal BuildPlan");

    const rules::Profile& rp = rules::Revolution();
    for (const prof::Profession& p : prof::All()) {
        const life::BuildPlan plan = life::PlanFromProfession(p);
        const life::PlanCheck pc = life::ValidatePlan(rp, plan);
        if (!pc.ok) {
            std::printf("  FAIL: plan for '%s' is illegal: %s\n", p.id.c_str(),
                        life::PlanViolationName(pc.violation));
            ++g_failures;
        }
        ++g_checks;

        // The creation request is the Revolution rule, verbatim -- two skills,
        // 50.0 each. If PlanFromProfession ever "helpfully" adds a third, the
        // server clamps it and the character silently gets less than asked.
        Check(plan.createSkills.size() == 2,
              "the creation request carries exactly two skills");
        i32 sum = 0;
        for (const life::SkillTarget& t : plan.createSkills) sum += t.tenths;
        Check(sum == prof::kServerCreateSkillSumMax,
              "the creation request is exactly 100.0 total");

        // The parallel arrays must stay parallel; NextSkillToBuy indexes them.
        Check(plan.viaTrainer.size() == plan.skills.size() &&
              plan.priority.size() == plan.skills.size(),
              "viaTrainer/priority stay in step with skills");
    }
}

// --------------------------------------------------------------------------
void TestNextSkillToBuy() {
    Section("trainer: what a life pays an NPC for, and when it stops");

    const prof::Profession* mg = prof::Find("mage");
    Check(mg != nullptr, "the mage exists");
    if (!mg) return;
    const life::BuildPlan plan = life::PlanFromProfession(*mg);

    // A generic tradesman teaches to 30.0 (NPCTrainPercent=30 of a GM's
    // 100.0); a guildmaster to 50.0 (TRAINSKILLMAX). Both ceilings are
    // exercised, because the answer must change with the ceiling.
    life::Observation obs;
    const int firstBuy = life::NextSkillToBuy(plan, obs, 300);
    Check(firstBuy >= 0,
          "a brand-new mage has something worth buying from a trainer");

    // Priority order decides, not table order.
    int bestPri = -1, expect = -1;
    for (usize i = 0; i < plan.skills.size(); ++i) {
        if (!plan.viaTrainer[i]) continue;
        if (plan.priority[i] > bestPri) {
            bestPri = plan.priority[i];
            expect = plan.skills[i].skillId;
        }
    }
    Check(firstBuy == expect, "the highest-priority trainable skill is chosen");

    // Already past the trainer's ceiling -> stop paying. This is the check
    // that keeps a bot from handing gold to an NPC for nothing, which is the
    // bot-side half of the anti-arbitrage invariant.
    obs.skills.push_back({firstBuy, 300});
    Check(life::NextSkillToBuy(plan, obs, 300) != firstBuy,
          "a skill at the trainer ceiling is no longer bought");
    Check(life::NextSkillToBuy(plan, obs, 500) == firstBuy,
          "the same skill IS still worth buying from a guildmaster at 50.0");

    // Nothing left to buy must be -1, not 0 -- skill id 0 is Alchemy, and a
    // 0 sentinel would send every finished character to an alchemy trainer.
    life::Observation done;
    for (usize i = 0; i < plan.skills.size(); ++i) {
        done.skills.push_back({plan.skills[i].skillId, 500});
    }
    Check(life::NextSkillToBuy(plan, done, 300) == -1,
          "a character past every trainer ceiling buys nothing (-1, not 0)");

    // A skill the plan never asked for is never bought, however cheap.
    life::Observation zero;
    const int pick = life::NextSkillToBuy(plan, zero, 300);
    bool inPlan = false;
    for (const life::SkillTarget& t : plan.skills) {
        if (t.skillId == pick) inPlan = true;
    }
    Check(inPlan, "the chosen skill is one the plan actually targets");
}

}  // namespace

int main() {
    std::printf("m5_professions\n");
    TestEveryEntryIsLegal();
    TestCreationFitsTheServer();
    TestBuildsAreEarnedNotGranted();
    TestRefusals();
    TestArchetypesDiffer();
    TestMinerConstraintIsRecorded();
    TestPlanFromProfession();
    TestNextSkillToBuy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
