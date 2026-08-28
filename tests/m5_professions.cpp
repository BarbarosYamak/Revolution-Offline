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

}  // namespace

int main() {
    std::printf("m5_professions\n");
    TestEveryEntryIsLegal();
    TestCreationFitsTheServer();
    TestBuildsAreEarnedNotGranted();
    TestRefusals();
    TestArchetypesDiffer();
    TestMinerConstraintIsRecorded();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
