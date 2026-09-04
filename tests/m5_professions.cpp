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
#include "uo/production.h"
#include "uo/faucets.h"
#include "uo/spellcast.h"

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

    const prof::Profession* fencer = prof::Find("fencer");
    const prof::Profession* pk = prof::Find("pk");
    Check(fencer && fencer->startStr >= 50,
          "a new fencer starts strong enough for an actual warrior kit");
    Check(pk && pk->startStr >= 50,
          "a new PK starts strong enough for an actual warrior kit");

    for (const prof::Profession& p : prof::All()) {
        Check(p.startStr == 50,
              "every new life starts at the normal 50 Strength baseline");
        if (p.id == "mage" || p.id == "warlock") continue;
        Check(p.startInt <= 10,
              "only mage and warlock use an Intelligence-weighted remainder");
    }

    // A hunter's support skills come from its actual life: fighting raises
    // Tactics/Anatomy/Parrying and bandaging raises Healing.  Sending a new
    // warrior around town to buy all four postpones its first safe hunt.
    for (const char* id : {"fencer", "macer", "archer", "warlock", "pk"}) {
        const prof::Profession* hunter = prof::Find(id);
        Check(hunter != nullptr, "combat profession is present");
        if (!hunter) continue;
        for (const prof::SkillTargetSpec& t : hunter->targets) {
            const bool combatSupport =
                t.skillId == rules::kTactics || t.skillId == rules::kAnatomy ||
                t.skillId == rules::kHealing || t.skillId == rules::kParrying;
            if (combatSupport) {
                Check(!t.viaTrainer,
                      "combat support skills are practised in the field, not bought");
            }
        }
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

// EVERY RECIPE INPUT MUST BE DECLARED.
//
// Runner.cpp builds obs.pack by counting ONLY the items a profession lists in
// `produces` or `consumes`. Anything else reads as ZERO however many are
// actually in the backpack -- so a missing entry does not degrade behaviour,
// it makes the item invisible and the life buys it forever.
//
// This has now bitten three times in one day:
//   i_kindling  -- unlisted for the fisher, so bought kindling read as zero
//                  and cooking could never begin
//   i_reag_nightshade -- unlisted for the alchemist, so Voris bought ten at a
//                  time, three times over, and was still told
//                  "i_potion_poison needs 10 x i_reag_nightshade" with an
//                  empty purse
//   i_rolling_pin -- needed as a tool before a fisher could cook at all
//
// It is a table-consistency error, so it belongs at build time rather than in
// a live run at two in the morning.
void TestEveryRecipeInputIsDeclared() {
    for (const prof::Profession& p : prof::All()) {
        for (const std::string& made : p.produces) {
            const prod::Recipe* r = prod::FindRecipe(made.c_str());
            if (!r) continue;                     // gathered, not crafted
            for (const prod::Ingredient& in : r->inputs) {
                if (!in.item || in.qty <= 0) continue;
                bool declared = false;
                for (const std::string& c : p.consumes)
                    if (c == in.item) { declared = true; break; }
                for (const std::string& m : p.produces)
                    if (m == in.item) { declared = true; break; }
                if (!declared) {
                    std::printf("  FAIL: %s makes %s which needs %s, but that "
                                "input is in neither produces nor consumes -- "
                                "it will count as zero in the pack\n",
                                p.id.c_str(), made.c_str(), in.item);
                    ++g_failures;
                }
                ++g_checks;
            }
        }
    }
}

// --------------------------------------------------------------------------
// EVERY WORKING LIFE HAS SOMETHING IT CAN MAKE AND SELL.
//
// Wave 2 (2026-09-01) found three lives that could never work: a tailor, a
// merchant/tinker and a lumberjack/carpenter reported
//   BLOCKED_NEED CRAFT: this life makes nothing sellable (nothing to make)
// for an entire session (Aelia x44, Odessa x8). None of them makes anything an
// NPC may buy -- which is the deliberate shape of Revolution's economy, not a
// gap -- and ChooseCraft was gating on the NPC question alone.
//
// This is the regression test for the DATA half: if a life declares products,
// at least one of them must have SOME legitimate destination.
void TestEveryProducingLifeHasSomewhereToSell() {
    Section("catalogue: a life that makes things has somewhere to take them");
    for (const prof::Profession& p : prof::All()) {
        if (p.produces.empty()) continue;
        bool anySellable = false;
        bool anyCraftable = false;
        const bool classIsPlayerMarket =
            faucet::OutputClassIsPlayerMarket(p.id.c_str());
        for (const std::string& made : p.produces) {
            const faucet::SaleRoute route = faucet::RouteForItem(made.c_str());
            const prod::Recipe* r = prod::FindRecipe(made.c_str());
            const bool sellable =
                route == faucet::SaleRoute::Npc ||
                route == faucet::SaleRoute::PlayerMarket ||
                (route == faucet::SaleRoute::Unrecorded && r &&
                 classIsPlayerMarket);
            if (!sellable) continue;
            anySellable = true;
            // inputs[0] empty == gathered, not crafted (a fisher's fish).
            if (r && r->inputs[0].item) anyCraftable = true;
        }
        if (!anySellable) {
            std::printf("  FAIL: %s declares %d product(s) and not one of them "
                        "may be sold to anybody -- this life can never work\n",
                        p.id.c_str(), static_cast<int>(p.produces.size()));
            ++g_failures;
        }
        ++g_checks;
        // A life whose income is Craft must additionally have a RECIPE, not
        // just a sellable gathered good: crafting is how it earns.
        bool craftsForALiving = false;
        for (const prof::Income i : p.income)
            if (i == prof::Income::Craft) craftsForALiving = true;
        if (craftsForALiving && !anyCraftable) {
            std::printf("  FAIL: %s earns by Craft but has no sellable RECIPE "
                        "-- only gathered goods\n", p.id.c_str());
            ++g_failures;
        }
        ++g_checks;
    }
}

// The three named lives, by name, and the reason each one used to fail.
void TestThePlayerMarketIsADestination() {
    Section("faucets: a player-market good is a destination, not a refusal");

    // The distinction the whole fix rests on: RefusePlayerMarket refuses the
    // NPC and NOT the trade.
    using faucet::SaleRoute;
    Check(faucet::AllowedForItem("i_robe") == nullptr,
          "no NPC may buy a robe -- the tailor row is unchanged");
    Check(faucet::RouteForItem("i_robe") == SaleRoute::PlayerMarket,
          "but a robe has a player-market route, so it may be made");
    Check(faucet::AllowedForItem("i_gears") == nullptr,
          "no NPC may buy tinker gears either");
    Check(faucet::RouteForItem("i_gears") == SaleRoute::PlayerMarket,
          "and gears are the tinker's player-market good");
    Check(faucet::AllowedForItem("i_board") == nullptr,
          "boards are a material, refused at the NPC counter");
    Check(faucet::RouteForItem("i_board") == SaleRoute::PlayerMarket,
          "and are exactly what a carpenter sells to a player");

    // The class rule: a tailor's registry row is keyed on i_robe and speaks
    // for the cloth it is cut from, so the rest of the trade's output rides
    // with it -- but only for THAT trade, and only for something that IS its
    // work. On its own the class rule is not a licence.
    Check(faucet::RouteForItem("i_sash") == SaleRoute::Unrecorded,
          "the registry does not name a sash at all");
    Check(faucet::OutputClassIsPlayerMarket("tailor"),
          "but a tailor's whole output class is a player-market good");
    Check(faucet::OutputClassIsPlayerMarket("merchant_tinker"),
          "and so is the tinker's -- its row carries the catalogue id now");
    Check(!faucet::OutputClassIsPlayerMarket("fisher"),
          "a fisher sells to NPCs; nothing about its class says otherwise");
    Check(!faucet::OutputClassIsPlayerMarket(nullptr),
          "and with no life named there is no class to speak for");

    // ABSENCE IS STILL NOT EVIDENCE, and no NPC route was opened anywhere.
    Check(faucet::RouteForItem("i_fish_big_1") == SaleRoute::Npc,
          "the fish tap is untouched");
    Check(faucet::AllowedForItem("i_ingot_iron") == nullptr,
          "and selling ingots to an NPC is refused exactly as before");
    Check(faucet::RouteForItem(nullptr) == SaleRoute::None,
          "a null item has no route");
}

// The BEHAVIOUR half: given a stocked pack and the skill, does the life
// actually choose something to make?
void TestTheThreeStuckLivesCanNowWork() {
    Section("craft: the three lives that made nothing now choose something");

    struct Case { const char* id; int skill; i32 tenths; const char* input;
                  i32 qty; };
    const Case kCases[] = {
        // Tailoring 4.5 -> i_sash from cloth (Production.cpp sm_cloth_misc).
        {"tailor",              rules::kTailoring,  600, "i_cloth",       50},
        // Tinkering 14.7 -> i_gears from an ingot (sm_tinker).
        {"merchant_tinker",     rules::kTinkering,  600, "i_ingot_iron",  20},
        // Carpentry 0.0 -> i_board from a log (sm_carpentry).
        {"lumberjack_swordsman", rules::kCarpentry, 600, "i_log",         20},
    };
    for (const Case& c : kCases) {
        const prof::Profession* p = prof::Find(c.id);
        if (!p) { std::printf("  FAIL: no catalogue entry %s\n", c.id);
                  ++g_failures; ++g_checks; continue; }
        life::Observation obs;
        obs.nowMs = 1000;
        obs.skills.push_back({c.skill, c.tenths});
        obs.pack.push_back({c.input, c.qty});
        const life::CraftIntent intent = life::ChooseCraft(*p, obs, 1);
        if (!intent.item) {
            std::printf("  FAIL: %s still makes nothing (%s)\n", c.id,
                        intent.why);
            ++g_failures;
        } else {
            // Printed, not asserted: WHICH product a life reaches for depends
            // on what is stocked, and freezing that here would be a test of
            // the pack rather than of the fix.
            std::printf("  %-20s -> %-16s (%s)%s\n", c.id, intent.item,
                        intent.why,
                        intent.missing.empty()
                            ? ""
                            : " [short of an input]");
        }
        ++g_checks;
    }
}

// --------------------------------------------------------------------------
// A CRAFTER'S DAY IS SEVERAL CRAFTS, NOT ONE REPEATED.
//
// "full crafters should not only do 1 craft always through the day; maybe
// different craft focuses" (project owner, 2026-09-01).
void TestCraftFocusRotates() {
    Section("craft: a sitting satiates its product, and the bench moves on");

    life::CraftFocus f;
    const i64 t0 = 1000;
    Check(!f.Satiated("i_board", t0), "nothing has been made yet");
    for (i32 i = 0; i < life::CraftFocus::kFocusRun - 1; ++i)
        f.NoteMade("i_board", t0);
    Check(!f.Satiated("i_board", t0),
          "a short run is not a monopoly -- the sitting has to finish first");
    f.NoteMade("i_board", t0);
    Check(f.Satiated("i_board", t0),
          "a full run of sittings on one product satiates it");
    Check(!f.Satiated("i_club", t0),
          "and satiates only THAT product, not the whole trade");

    // A different product breaks the streak, exactly as Planner::NoteRan does
    // one level up.
    f.NoteMade("i_club", t0);
    Check(!f.Satiated("i_board", t0),
          "one turn at something else and the board is welcome again");

    // And it fades rather than standing as a permanent ban.
    life::CraftFocus g;
    for (i32 i = 0; i < life::CraftFocus::kFocusRun; ++i)
        g.NoteMade("i_board", t0);
    Check(!g.Satiated("i_board", t0 + life::CraftFocus::kFocusFadeMs),
          "a product worked hard a while ago is not still being avoided");

    // THE CHOICE ITSELF. A lumberjack holding logs AND boards can make either
    // a board or a club; after a run of boards the club must win.
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    if (!lj) { std::printf("  FAIL: no lumberjack_swordsman\n"); ++g_failures;
               ++g_checks; return; }
    life::Observation obs;
    obs.nowMs = t0;
    obs.skills.push_back({rules::kCarpentry, 600});
    obs.pack.push_back({"i_log", 40});
    const life::CraftIntent first = life::ChooseCraft(*lj, obs, 1);
    Check(first.item != nullptr, "with logs in the pack there is work to do");

    life::CraftFocus busy;
    for (i32 i = 0; i < life::CraftFocus::kFocusRun; ++i)
        busy.NoteMade(first.item, t0);
    const life::CraftIntent next = life::ChooseCraft(*lj, obs, 1, &busy);
    Check(next.item != nullptr, "and there is still work to do afterwards");
    if (first.item && next.item) {
        Check(std::string(next.item) != first.item,
              "but it is a DIFFERENT product -- the focus rotated");
    }

    // BOUNDED. A profession with only one thing it can make repeats it rather
    // than standing idle: rotation is a preference, never a ban.
    const prof::Profession* alch = prof::Find("alchemist");
    if (alch) {
        life::Observation one;
        one.nowMs = t0;
        // No inputs at all, so nothing is fully stocked -- the satiation
        // branch is never reached and the old answer must survive.
        const life::CraftIntent a = life::ChooseCraft(*alch, one, 1);
        life::CraftFocus fa;
        if (a.item)
            for (i32 i = 0; i < life::CraftFocus::kFocusRun; ++i)
                fa.NoteMade(a.item, t0);
        const life::CraftIntent b = life::ChooseCraft(*alch, one, 1, &fa);
        Check((a.item == nullptr) == (b.item == nullptr),
              "a life with nothing stocked answers the same either way");
    }
}

// THE THREE OUTPUTS THAT REFUSED THEMSELVES ALL WAVE (2026-09-02).
//
// i_board x45 (Cyras, Halain, Vorar), i_gears x30 (Serena) and i_ingot_iron
// x10 (Draver) each logged goal_failed=CRAFT reason="REFUSE_MISSING_RECIPE"
// no menu path known -- 17 goal_spinning=CRAFT flags and no craft output
// anywhere in the fleet. Two were missing rows in the route table; the third
// is not a menu craft at all and belongs to SMELT.
//
// Sources for the two new rows, both read from this runtime's own scripts:
//   sm_legacy_carpentry.scp:15-16   ON=i_board boards / MAKEITEM=i_board
//   sm_legacy_tinkering.scp:18      ON=i_clock_parts Parts -> sm_parts
//   sm_legacy_tinkering.scp:201-202 ON=i_gears <name> (<resmake>)
// and crafting_settings.scp:26-33 (every NewCrafting_* is 0), which is what
// makes the LEGACY menus the ones a bot will actually be shown.
void TestTheThreeRefusedCraftsHaveARoute() {
    Section("craft: the three outputs that spun on REFUSE_MISSING_RECIPE");

    const life::CraftMenuPath* board = life::CraftMenuFor("i_board");
    Check(board != nullptr, "i_board has a craft-menu route at all");
    if (board) {
        Check(std::string(board->step1) == "boards",
              "and it is the flat top-level 'boards' entry of sm_carpentry");
        Check(board->step2 == nullptr,
              "carpentry's board entry opens no submenu");
    }

    const life::CraftMenuPath* gears = life::CraftMenuFor("i_gears");
    Check(gears != nullptr, "i_gears has a craft-menu route at all");
    if (gears) {
        Check(std::string(gears->step1) == "Parts",
              "tinkering reaches gears through the 'Parts' submenu");
        Check(gears->step2 != nullptr && std::string(gears->step2) == "gears",
              "and the leaf option is the itemdef's own name");
    }

    // The third is a different failure with the same symptom. Ore is smelted
    // by double-clicking it beside a forge -- Provenance::WorldProcessed, "a
    // station transforms it; no craft menu" -- so the fix is a hand-off to
    // SMELT, not a table row. Assert the two facts the hand-off branch in
    // Runner::DoCraft reads: no menu route, and a world-processed recipe.
    Check(life::CraftMenuFor("i_ingot_iron") == nullptr,
          "i_ingot_iron deliberately has NO menu route -- it is smelted");
    const prod::Recipe* ingot = prod::FindRecipe("i_ingot_iron");
    Check(ingot != nullptr, "i_ingot_iron is still in the recipe graph");
    if (ingot) {
        Check(ingot->provenance == prod::Provenance::WorldProcessed,
              "and it is world-processed, which is what routes it to SMELT");
    }

    // Both new rows must still be things their trade can actually make, or a
    // route that resolves would just fail one step later.
    for (const char* item : {"i_board", "i_gears"}) {
        const prod::Recipe* r = prod::FindRecipe(item);
        Check(r != nullptr && r->inputs[0].item != nullptr,
              "a routed output has a recipe with an input to open the menu");
    }
}

// A REFUSAL THAT IS NOT REMEMBERED IS A REFUSAL THAT REPEATS.
//
// ChooseCraft knows the recipe graph, not the route table, so before this it
// handed CRAFT the same unreachable output on the very next tick -- forever.
// Three strikes and the output steps aside for the next thing this life can
// make. Session-scoped, and only ever a skip: it must not empty a trade.
void TestAnUnreachableCraftStandsAside() {
    Section("craft: three refusals and the bench moves to something reachable");

    life::CraftFocus f;
    Check(!f.Unreachable("i_board"),
          "an output nobody has refused yet is fair game");
    for (i32 i = 0; i < life::CraftFocus::kNoRouteStrikes - 1; ++i)
        f.NoteNoRoute("i_board");
    Check(!f.Unreachable("i_board"),
          "one or two refusals could be a half-read menu -- keep trying");
    f.NoteNoRoute("i_board");
    Check(f.Unreachable("i_board"), "three is the route table, not the menu");
    Check(!f.Unreachable("i_club"),
          "and it stands aside for THAT output only, not the whole trade");

    // The choice itself: a lumberjack/carpenter holding logs picks something,
    // and once that something has refused three times it picks something else.
    const prof::Profession* lj = prof::Find("lumberjack_swordsman");
    if (!lj) { std::printf("  FAIL: no lumberjack_swordsman\n"); ++g_failures;
               ++g_checks; return; }
    life::Observation obs;
    obs.nowMs = 1000;
    obs.skills.push_back({rules::kCarpentry, 600});
    obs.pack.push_back({"i_log", 40});
    const life::CraftIntent first = life::ChooseCraft(*lj, obs, 1);
    Check(first.item != nullptr, "with logs in the pack there is work to do");
    if (first.item) {
        life::CraftFocus blocked;
        for (i32 i = 0; i < life::CraftFocus::kNoRouteStrikes; ++i)
            blocked.NoteNoRoute(first.item);
        const life::CraftIntent next = life::ChooseCraft(*lj, obs, 1, &blocked);
        Check(next.item == nullptr || std::string(next.item) != first.item,
              "the refused output is not offered a fourth time");
    }
}

// M5 -- WHAT EACH LIFE WEARS.
//
// "mage wears only mage equipment, sell the rest" (project owner). The
// catalogue now states it (Profession::wears / maysShield) instead of the
// runner inferring it from whether the character happens to have Magery. What
// is checked here is CONSISTENCY, not taste: a life that casts for a living
// must not be listed as wearing metal, and an archer must never be listed as
// carrying a shield, because the bow needs both hands.
void TestWhatEachLifeWears() {
    std::printf("[gear: what each life will wear, stated not inferred]\n");
    using W = prof::Profession::Wear;
    for (const prof::Profession& p : prof::All()) {
        const bool casts =
            p.startSkillA == rules::kMagery || p.startSkillB == rules::kMagery;
        bool magePrimary = false;
        for (const prof::SkillTargetSpec& t : p.targets) {
            if (t.skillId == rules::kMagery &&
                t.role == prof::SkillRole::Primary)
                magePrimary = true;
        }
        if (casts || magePrimary) {
            if (p.wears != W::Cloth) {
                std::printf("  FAIL: %s casts for a living but wears armour\n",
                            p.id.c_str());
                ++g_failures;
            }
            if (p.maysShield) {
                std::printf("  FAIL: %s casts, and a shield hand is a spell "
                            "hand\n", p.id.c_str());
                ++g_failures;
            }
            ++g_checks;
        }
        if (p.id == "archer" && p.maysShield) {
            std::printf("  FAIL: an archer cannot hold a shield and a bow\n");
            ++g_failures;
        }
        // A shield is armour too: nothing that refuses armour may carry one.
        if (p.wears == W::Cloth && p.maysShield) {
            std::printf("  FAIL: %s wears cloth but is allowed a shield\n",
                        p.id.c_str());
            ++g_failures;
        }
        ++g_checks;
    }
}

// M5/section 21 -- the combat strategy is DATA, and it has to agree with the
// rest of the record. A life listed as wearing cloth and refusing a shield
// has no business being told to close to melee range.
void TestCombatStrategyAgreesWithTheBuild() {
    std::printf("[strategy: how a life fights matches what it is]\n");
    using W = prof::Profession::Wear;
    for (const prof::Profession& p : prof::All()) {
        const bool fights = uo::life::WantsToHunt(p);

        // A life with no weapon skill must not be given a fighting strategy:
        // that is how a miner ends up swinging a pickaxe at a lich.
        if (!fights && p.combatStrategy != uo::life::CombatStrategyId::AvoidCombat &&
            p.combatStrategy != uo::life::CombatStrategyId::Mage &&
            p.combatStrategy != uo::life::CombatStrategyId::Tamer) {
            std::printf("  FAIL: %s has no weapon skill but fights as %s\n",
                        p.id.c_str(),
                        uo::life::CombatStrategyName(p.combatStrategy));
            ++g_failures;
        }
        ++g_checks;

        // A melee strategy needs armour: closing to one tile in a robe is a
        // way to die, and M5's wearable class already says who may.
        if (p.combatStrategy == uo::life::CombatStrategyId::Melee &&
            p.wears == W::Cloth) {
            std::printf("  FAIL: %s closes to melee in cloth\n", p.id.c_str());
            ++g_failures;
        }
        ++g_checks;

        // An archer may never be given a shield -- the bow needs both hands
        // -- and the strategy is the second place that has to know it.
        if (p.combatStrategy == uo::life::CombatStrategyId::Ranged &&
            p.maysShield) {
            std::printf("  FAIL: %s shoots a bow and carries a shield\n",
                        p.id.c_str());
            ++g_failures;
        }
        ++g_checks;
    }
}

// A SCROLL RUNG WHOSE SPELL THE BOOK LACKS IS NOT A CANDIDATE (2026-09-04).
//
// Shard fact, verified live: the inscription menu offers only the spells the
// scribe's own book already holds. Lyra and Thalia both reach Inscription 40 /
// Magery 30 -- enough for a Recall scroll -- and both carry Poison but not
// Recall, so the top rung of the scribe's ladder ended every craft goal at
//   goal_failed=CRAFT reason="REFUSE_MISSING_RECIPE" this menu offers none of
//   'Spell Circle 4' / 'recall'
// (run_gates/g_Lyra.console.txt ~13:04, g_Thalia ~13:03). A rung nobody can
// write must be skipped where the CHOICE is made, and it must not count as the
// top rung -- otherwise the buyable-shortfall rule keeps handing the sitting to
// a recipe the menu will refuse.
void TestAScrollRungNeedsItsSpellInTheBook() {
    Section("craft: a scribe cannot write a scroll whose spell it lacks");

    // Two real rows of data/revolution_spells.tsv (tools/spellgen.py's export
    // of the shard's own spells/spells_magery.scp), copied verbatim.
    const std::string tsv =
        "spell\tdefname\tname\tcircle\tminskill\tmana\tflags\treagents\n"
        "20\ts_poison\tPoison\t3\t300\t9\tspellflag_harm\ti_reag_nightshade\n"
        "32\ts_recall\tRecall\t4\t400\t11\tspellflag_playeronly\t"
        "i_reag_black_pearl,i_reag_blood_moss,i_reag_mandrake_root\n";
    Check(spell::LoadSpellTableFromText(tsv) == 2, "the two spell rows load");

    Check(life::SpellTaughtByScroll("i_scroll_recall") == 32,
          "a Recall scroll is written with s_recall (spell 32)");
    Check(life::SpellTaughtByScroll("i_scroll_blank") == 0,
          "a blank scroll teaches nothing -- there is no s_blank");
    Check(life::SpellTaughtByScroll("i_log") == 0,
          "and a log is not a scroll at all");

    const prof::Profession* scribe = prof::Find("scribe");
    if (!scribe) { std::printf("  FAIL: no scribe\n"); ++g_failures; ++g_checks;
                   return; }

    // Lyra's live numbers: Inscription 70.0, Magery 50.0, and a pack holding
    // every input BOTH writable rungs ask for, so nothing here is decided by a
    // shortfall.
    life::Observation obs;
    obs.nowMs = 1;
    obs.skills.push_back({rules::kInscription, 700});
    obs.skills.push_back({rules::kMagery, 500});
    obs.pack.push_back({"i_scroll_blank", 50});
    obs.pack.push_back({"i_reag_nightshade", 50});
    obs.pack.push_back({"i_reag_black_pearl", 50});
    obs.pack.push_back({"i_reag_blood_moss", 50});
    obs.pack.push_back({"i_reag_mandrake_root", 50});

    // AN UNREAD BOOK REFUSES NOTHING. knownSpells empty means "not looked in",
    // never "empty", so the answer must be exactly what it was before.
    const life::CraftIntent unread = life::ChooseCraft(*scribe, obs, 1);
    Check(unread.item && std::string(unread.item) == "i_scroll_recall",
          "with no reading of the book the top rung still wins");
    Check(unread.wantSpell == 0,
          "and nothing is claimed about a spell we have not looked for");

    // THE BOOK AS LYRA ACTUALLY CARRIES IT: Poison, no Recall.
    obs.knownSpells = {20};
    const life::CraftIntent read = life::ChooseCraft(*scribe, obs, 1);
    Check(read.item && std::string(read.item) == "i_scroll_poison",
          "a book without Recall drops to the rung this scribe CAN write");
    Check(read.wantSpell == 32,
          "and names Recall as the spell that would unblock the ladder");
    Check(read.wantSpellItem &&
              std::string(read.wantSpellItem) == "i_scroll_recall",
          "together with the rung that wanted it");

    // ONCE THE SCROLL IS IN THE BOOK the ladder climbs again by itself --
    // provided the cast can be paid for. INT 0 here means "not observed", so
    // the mana rule below stays out of the way.
    obs.knownSpells = {20, 32};
    const life::CraftIntent both = life::ChooseCraft(*scribe, obs, 1);
    Check(both.item && std::string(both.item) == "i_scroll_recall",
          "with Recall known the top rung is chosen again");
    Check(both.wantSpell == 0, "and nothing is reported as blocked");

    // THE SECOND WALL: MANA. Every inscription leaf is
    // `TESTIF=<cancast s_x 00>` and CANCAST answers for mana too, so Sphere
    // hides a whole circle whose spells the character cannot afford to cast.
    // Circles 1-3 cost 4, 6 and 9 mana; circle 4 costs 11. Thalia's INT is 10
    // (run_gates/g_Thalia.console.txt:47) and the live menu offered exactly
    // "first circle / second circle / third circle" -- with Recall already in
    // her book. No purchase can fix that, so it must not become a shopping
    // errand.
    obs.intel = 10;
    const life::CraftIntent poor = life::ChooseCraft(*scribe, obs, 1);
    Check(poor.item && std::string(poor.item) == "i_scroll_poison",
          "10 mana cannot pay for an 11-mana Recall, so the writable rung wins");
    Check(poor.lowManaSpell == 32 && poor.lowManaCost == 11,
          "and the mana wall is named -- spell 32 at 11 mana");
    Check(poor.wantSpell == 0,
          "but NOT as a spell to buy: the scroll is already in the book");

    obs.intel = 11;
    const life::CraftIntent rich = life::ChooseCraft(*scribe, obs, 1);
    Check(rich.item && std::string(rich.item) == "i_scroll_recall",
          "one more point of INT and the top rung is reachable again");
    Check(rich.lowManaSpell == 0, "with no wall left to report");
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
    TestEveryRecipeInputIsDeclared();
    TestEveryProducingLifeHasSomewhereToSell();
    TestThePlayerMarketIsADestination();
    TestTheThreeStuckLivesCanNowWork();
    TestCraftFocusRotates();
    TestTheThreeRefusedCraftsHaveARoute();
    TestAnUnreachableCraftStandsAside();
    TestWhatEachLifeWears();
    TestCombatStrategyAgreesWithTheBuild();
    TestAScrollRungNeedsItsSpellInTheBook();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
