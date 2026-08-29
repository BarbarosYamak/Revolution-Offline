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
    // CORRECTED 2026-08-29: the owner's figure is 80, the full ceiling, not
    // 50. A new character spends every point it is allowed. This used to
    // assert the opposite and was wrong for as long as it stood -- every bot
    // created under it was born a third weaker than a real player.
    Check(prof::kRevolutionStartStatTotal == prof::kServerCreateStatSumMax,
          "Revolution's 80 stat points are EXACTLY the server's ceiling -- "
          "a new character spends everything it is allowed");

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
    {   // M4's original 40/35/5 = 80 is CORRECT and must be accepted. It was
        // refused here for a long time on a mistaken 50-point rule; the split
        // that is actually wrong is one that does not spend the full 80.
        prof::Profession good = *base;
        good.startStr = 40; good.startDex = 35; good.startInt = 5;
        Check(prof::Validate(rp, good).violation == prof::ProfViolation::None,
              "M4's 40/35/5 = 80 split is legal Revolution creation");
        prof::Profession bad = *base;
        bad.startStr = 20; bad.startDex = 25; bad.startInt = 5;   // only 50
        Check(prof::Validate(rp, bad).violation ==
                  prof::ProfViolation::StartStatsWrongTotal,
              "a split that leaves 30 points unspent is refused");
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
    Check(ms->startStr < prof::kRevolutionStartStatTotal,
          "the miner does NOT dump all 80 points into STR just to lift a "
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


// --------------------------------------------------------------------------
void TestARefusalIsRemembered() {
    Section("trainer: a refusal is learned once, not rediscovered forever");

    // The first live M5 run walked a mage across Britain to a mage NPC, was
    // told "You already know as much as I can teach of EvaluatingIntel", and
    // then asked the same NPC the same question every ~2.5 seconds for the
    // rest of the session. The refusal was written to an event log nothing
    // read. This is that defect, as a test.
    const prof::Profession* mg = prof::Find("mage");
    Check(mg != nullptr, "the mage exists");
    if (!mg) return;
    const life::BuildPlan plan = life::PlanFromProfession(*mg);

    life::Observation obs;
    obs.skills.push_back({rules::kEvaluatingIntel, 118});   // the live value
    obs.skills.push_back({rules::kInscription,      77});   // ditto

    const int first = life::NextSkillToBuy(plan, obs, 300);
    Check(first == rules::kEvaluatingIntel,
          "before asking, Evaluating Intelligence is the highest-priority buy");

    // Now the NPC has said no. Source-X sets a trainer's ceiling from ITS OWN
    // skill (CCharNPCStatus.cpp:514), so no amount of walking changes this.
    obs.trainerRefusedSkills.push_back(rules::kEvaluatingIntel);
    const int second = life::NextSkillToBuy(plan, obs, 300);
    Check(second != rules::kEvaluatingIntel,
          "after the refusal the character stops choosing that skill");
    // The pure mage has exactly ONE skill an NPC will teach -- Evaluating
    // Intelligence. Magery and Meditation are viaTrainer=false because they
    // are practised, not bought, and Inscription left the build in 2026-08-29
    // when scroll-writing went back to being the scribe's identity. So the
    // honest answer after a refusal is "nothing", not a fallback.
    Check(second == -1,
          "a pure mage whose one buyable skill is refused wants nothing else");

    // Refuse everything: no target at all, rather than looping on the last one.
    obs.trainerRefusedSkills.push_back(rules::kInscription);
    Check(life::NextSkillToBuy(plan, obs, 300) == -1,
          "a life whose every trainable skill was refused buys nothing");
}

// --------------------------------------------------------------------------
void TestTrainerMemory() {
    Section("memory: verdicts are per (skill, trade, NPC) and replaceable");

    // CHANGED DELIBERATELY. This test used to assert that ONE refusal marked
    // the whole trade refused, and that there was one row per (skill, trade).
    // Both encoded a mistake: Source-X caps teaching at min(THAT NPC's own
    // skill x NPCTrainPercent, NPCTrainMax, the student's cap), so the ceiling
    // belongs to the individual and two mages stop in different places. Under
    // the old rule Alenne's "nothing left to give" at Meditation 21.9 wrote
    // off every mage on the shard and left Ysolde with no buyable skill at
    // all (run_m5/p0gate2, `want_train=nothing`). A trade is given up only
    // after kTradeExhaustedAfter different NPCs of it have said no.
    life::Memory mem;
    Check(!mem.TrainerRefused(rules::kEvaluatingIntel, "mage"),
          "nothing is refused before anyone has been asked");

    life::TrainerVerdict v;
    v.skillId = rules::kEvaluatingIntel;
    v.trade = "mage";
    v.npcSerial = 0x1111;
    v.taught = false;
    v.atTenths = 118;
    v.why = "the trainer has nothing left to give";
    mem.NoteTrainerVerdict(v);

    Check(mem.TrainerRefusedByNpc(rules::kEvaluatingIntel, 0x1111),
          "the refusal is remembered against the NPC that gave it");
    Check(!mem.TrainerRefused(rules::kEvaluatingIntel, "mage"),
          "but ONE mage's answer does not write off mages");
    Check(!mem.TrainerRefusedByNpc(rules::kEvaluatingIntel, 0x2222),
          "and says nothing about a mage who was never asked");
    Check(!mem.TrainerRefused(rules::kEvaluatingIntel, "scribe"),
          "nor about a DIFFERENT trade");
    Check(!mem.TrainerRefused(rules::kInscription, "mage"),
          "nor about a different skill from the same trade");

    v.npcSerial = 0x2222; mem.NoteTrainerVerdict(v);
    v.npcSerial = 0x3333; mem.NoteTrainerVerdict(v);
    Check(mem.TrainerRefused(rules::kEvaluatingIntel, "mage"),
          "three different mages refusing DOES exhaust the trade");

    // The same NPC teaching it later replaces its own refusal, so a verdict
    // is never a permanent lie about the world.
    v.npcSerial = 0x1111;
    v.taught = true;
    v.why = "taught";
    mem.NoteTrainerVerdict(v);
    Check(!mem.TrainerRefused(rules::kEvaluatingIntel, "mage"),
          "a later success drops that NPC back below the exhaustion line");
    Check(mem.Trainers().size() == 3,
          "one row per (skill, trade, NPC), not one per asking");
}


// --------------------------------------------------------------------------
void TestSkillRoles() {
    Section("roles: every build has one primary, and utility stays utility");

    const rules::Profile& rp = rules::Revolution();
    for (const prof::Profession& p : prof::All()) {
        if (p.targets.empty()) continue;
        int primaries = 0;
        for (const prof::SkillTargetSpec& t : p.targets) {
            if (t.role == prof::SkillRole::Primary) ++primaries;
        }
        if (primaries != 1) {
            std::printf("  FAIL: '%s' has %d primary skills, not 1\n",
                        p.id.c_str(), primaries);
            ++g_failures;
        }
        ++g_checks;
    }

    // The SCRIBE, not the mage: this exercises the Utility-role rule and
    // needs a build that HAS a utility skill. The pure mage is three
    // skills, all Primary/Secondary; the scribe carries Meditation as
    // Utility, which is exactly the shape this rule is about.
    const prof::Profession* base = prof::Find("scribe");
    Check(base != nullptr, "the mage exists");
    if (!base) return;

    {   // Two primaries: the paperdoll title would be arbitrary.
        prof::Profession bad = *base;
        for (prof::SkillTargetSpec& t : bad.targets) t.role = prof::SkillRole::Primary;
        Check(prof::Validate(rp, bad).violation ==
                  prof::ProfViolation::NotExactlyOnePrimary,
              "a build with two primaries is refused");
    }
    {   // No primary at all.
        prof::Profession bad = *base;
        for (prof::SkillTargetSpec& t : bad.targets) t.role = prof::SkillRole::Secondary;
        Check(prof::Validate(rp, bad).violation ==
                  prof::ProfViolation::NotExactlyOnePrimary,
              "a build with no primary is refused");
    }
    {   // A "utility" skill at the per-skill cap is a second profession, and
        // on this shard it would also break travel-magic rarity: Recall opens
        // at 26+ and Gate at 90+, so a GM utility Magery is a free gate.
        prof::Profession bad = *base;
        bool found = false;
        for (prof::SkillTargetSpec& t : bad.targets) {
            if (t.role != prof::SkillRole::Utility) continue;
            // Pay for the raise out of the unresolved budget, so this stays a
            // test of the ROLE rule rather than tripping the 700-point one.
            bad.unresolvedTenths -= (rp.perSkillCapTenths - t.tenths);
            t.tenths = rp.perSkillCapTenths;
            found = true;
            break;
        }
        Check(found, "the mage has a utility skill to corrupt");
        if (found) {
            Check(prof::Validate(rp, bad).violation ==
                      prof::ProfViolation::UtilityAtFullCap,
                  "a utility skill at 100.0 is refused");
        }
    }
}

// --------------------------------------------------------------------------
void TestTierIsObservedNotAssigned() {
    Section("tier: computed from the server's numbers, never handed out");

    const i32 cap = rules::Revolution().totalSkillCapTenths;   // 7000

    // A brand-new Revolution character has exactly 100.0 of a 700.0 budget.
    // It must read as a Novice: it has done nothing yet.
    Check(prof::TierFromSkillSum(prof::kRevolutionStartSkillEach *
                                 prof::kRevolutionStartSkillCount, cap) ==
              prof::Tier::Novice,
          "a freshly created character is a Novice");
    Check(prof::TierFromSkillSum(0, cap) == prof::Tier::Novice,
          "and so is one with nothing at all");
    Check(prof::TierFromSkillSum(cap, cap) == prof::Tier::Grandmaster,
          "a finished 700-point build is a Grandmaster");

    // Monotonic: more skill never means a lower tier.
    int last = -1;
    for (i32 sum = 0; sum <= cap; sum += 50) {
        const int t = static_cast<int>(prof::TierFromSkillSum(sum, cap));
        if (t < last) {
            std::printf("  FAIL: tier went DOWN at sum %d\n", sum);
            ++g_failures;
            break;
        }
        last = t;
    }
    ++g_checks;

    // Nonsense in, Novice out -- never a crash and never a flattering answer.
    Check(prof::TierFromSkillSum(-500, cap) == prof::Tier::Novice,
          "a negative total is a Novice, not an error");
    Check(prof::TierFromSkillSum(9999, 0) == prof::Tier::Novice,
          "a zero cap is a Novice, not a division by zero");

    // The population curve is a yardstick, not a spawn table. It has to sum
    // to 100 to be either.
    int total = 0;
    for (int i = 0; i <= static_cast<int>(prof::Tier::Grandmaster); ++i) {
        total += prof::PopulationSharePercent(static_cast<prof::Tier>(i));
    }
    Check(total == 100, "the population shares sum to 100");
    Check(prof::PopulationSharePercent(prof::Tier::Grandmaster) <
          prof::PopulationSharePercent(prof::Tier::Journeyman),
          "Grandmasters are rarer than Journeymen -- the curve is a bell, "
          "not a flat roll");
}

}  // namespace

// --------------------------------------------------------------------------
// THE THIRD CREATION SLOT. Source-X randomises every skill at creation into
// [0.0, 19.9) (CChar.cpp:1768-1772, sphere.ini MaxBaseSkill=200) and then
// overwrites the three requested slots, so the only skill a new character can
// hold at LITERALLY 0.0 is whichever one occupies this slot. Naming a plan
// skill there is what makes "learns a previously-zero skill" reachable at all.
//
// Two things must stay true or it stops being honest:
//   * the zero skill is never one of the two 50.0 skills -- that would throw
//     away half the creation budget;
//   * it is a skill this build actually MEANS to train, never a filler.
// The [NEWBIE] constraint (a skill named here must grant no kit) cannot be
// asserted without the shard's scripts, so it is recorded in professions.h
// and checked by hand -- Meditation is the only qualifying plan skill.
void TestTheZeroSkillSlot() {
    Section("creation: the third slot starts a real plan skill at 0.0");
    int withZero = 0;
    for (const prof::Profession& p : prof::All()) {
        if (p.startZeroSkill < 0) continue;
        ++withZero;
        Check(p.startZeroSkill != p.startSkillA &&
              p.startZeroSkill != p.startSkillB,
              "the zero skill is not one of the two starting fifties");
        bool planned = false;
        for (const prof::SkillTargetSpec& t : p.targets) {
            if (t.skillId == p.startZeroSkill) { planned = true; break; }
        }
        Check(planned, "the zero skill is a skill this build intends to earn");
    }
    Check(withZero > 0,
          "at least one build starts a skill from literally nothing");
}

int main() {
    std::printf("m5_professions\n");
    TestEveryEntryIsLegal();
    TestCreationFitsTheServer();
    TestTheZeroSkillSlot();
    TestBuildsAreEarnedNotGranted();
    TestRefusals();
    TestArchetypesDiffer();
    TestMinerConstraintIsRecorded();
    TestPlanFromProfession();
    TestNextSkillToBuy();
    TestARefusalIsRemembered();
    TestTrainerMemory();
    TestSkillRoles();
    TestTierIsObservedNotAssigned();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
