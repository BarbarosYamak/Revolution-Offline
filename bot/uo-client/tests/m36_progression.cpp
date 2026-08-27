// Deterministic tests for the M3.6 units: the navigation recovery lifecycle,
// the historical Magery training bands, and the Revolution rules profile
// (build legality, the poison distinction, and NPC teaching arithmetic).
//
// The numbers here are RevolutionUO's own or this shard's own, never invented:
//
//   * 700.0 total skill cap -- eleven forum builds across 2008-2010, plus the
//     official `.skilldusur` command's 670.0 floor.
//   * Resisting Spells inactive -- the official gameplay guide names it.
//   * Magery bands -- the RevolutionUO training guide, forum topic 59111.
//   * Teaching 30% / 1gp per 0.1 -- runtime sphere.ini, proven live in M3.5
//     (Arms Lore 2.6 -> 21.7 for a quoted 191 gold).

#include "uo/rules.h"
#include "uo/travel_mode.h"
#include "uo/training.h"
#include "uo/world_model.h"
#include "travel/Journey.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace uo;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

// A minimal one-leg route so a Journey can be driven without a world.
route::WorldRoute OneWalkLeg(i32 x, i32 y) {
    route::WorldRoute r;
    route::RouteLeg leg;
    leg.kind = route::LegKind::Walk;
    leg.target = wm::Point{x, y, 0};
    r.legs.push_back(leg);
    r.ok = true;
    return r;
}

route::WorldRoute NoRoute(const char* why) {
    route::WorldRoute r;
    r.ok = false;
    r.failure = why;
    return r;
}

// ---------------------------------------------------------------------------
// Navigation recovery. This is the M3.5 bug: a journey that could not plan from
// where it stood was ENDED, and the escape walk that started to fix it carried
// on with nobody waiting for it.
// ---------------------------------------------------------------------------

void TestRecoveryParksTheJourney() {
    Section("nav recovery: the parent journey parks, it does not end");
    travel::Journey j;
    j.Begin("Britain banker", 1425, 1690, 5, 0);
    j.SetRoute(NoRoute("no world route to the destination"), 0);

    Check(j.CurrentPhase() == travel::Phase::Failed, "a plan that fails, fails");
    Check(j.FailureReason() == travel::Failure::NoRoute,
          "and being sealed in reports NoRoute, not Unreachable");

    Check(j.BeginPositionRecovery("route unusable from here", 100),
          "recovery may begin from a failed plan");
    Check(j.Recovering(), "the journey is now recovering");
    Check(j.Active(), "AND STILL ACTIVE -- the M3.5 bug was reporting it finished");
    Check(j.CurrentPhase() == travel::Phase::Recovering, "with an explicit phase");
    Check(j.Label() == "Britain banker", "it keeps its identity");
    Check(j.GoalX() == 1425 && j.GoalY() == 1690, "and its destination");
    Check(j.ArriveRadius() == 5, "and its arrive radius");
    Check(j.PositionRecoveries() == 1, "one attempt spent");
}

void TestRecoveryOwnsMovement() {
    Section("nav recovery: only one thing moves at a time");
    travel::Journey j;
    j.Begin("somewhere", 100, 100, 2, 0);
    j.SetRoute(NoRoute("unreachable"), 0);
    j.BeginPositionRecovery("sealed in", 0);

    // The client owns the escape walk. If the journey also asked for a walk or
    // a replan here there would be two movement owners -- which is exactly how
    // the orphaned recovery happened.
    Check(j.NextCommand(0) == travel::Command::Wait,
          "the journey issues Wait while recovery is in flight");
    Check(j.NextCommand(999999) == travel::Command::Wait,
          "and keeps issuing Wait however long it takes");
}

void TestRecoverySucceedsAndResumes() {
    Section("nav recovery: reaching the anchor resumes the ORIGINAL goal");
    travel::Journey j;
    j.Begin("Britain banker", 1425, 1690, 5, 0);
    j.SetRoute(NoRoute("no world route"), 0);
    j.BeginPositionRecovery("sealed in", 0);

    j.OnPositionRecovered(/*reached=*/true, 1000);
    Check(j.CurrentPhase() == travel::Phase::NeedRoute,
          "the journey wants a new route");
    Check(j.NextCommand(2000) == travel::Command::PlanRoute,
          "and asks for one");
    Check(j.GoalX() == 1425 && j.GoalY() == 1690,
          "for the destination it never gave up on");
    Check(j.RoutePlans() == 0,
          "with its replan budget refreshed -- the old failures were about the "
          "old position");

    // And it can now actually finish.
    j.SetRoute(OneWalkLeg(1425, 1690), 2000);
    Check(j.CurrentPhase() == travel::Phase::Walking, "a good route walks");
}

void TestRecoveryBoundedAndFailsCleanly() {
    Section("nav recovery: bounded, and fails with the real reason");
    travel::Journey j;
    travel::Limits lim;
    lim.maxPositionRecoveries = 3;
    j.SetLimits(lim);
    j.Begin("nowhere", 50, 50, 1, 0);
    j.SetRoute(NoRoute("no world route"), 0);

    Check(j.BeginPositionRecovery("sealed in", 0), "attempt 1");
    j.OnPositionRecovered(false, 10);
    Check(j.Recovering(), "a short fall still leaves it recovering");
    Check(j.BeginPositionRecovery("sealed in", 20), "attempt 2");
    j.OnPositionRecovered(false, 30);
    Check(j.BeginPositionRecovery("sealed in", 40), "attempt 3");

    j.OnPositionRecovered(false, 50);
    Check(j.CurrentPhase() == travel::Phase::Failed,
          "the third failure ends it -- no infinite escape loop");
    Check(j.FailureReason() == travel::Failure::Unreachable,
          "reported as unreachable");
    Check(j.FailureDetail() == "sealed in",
          "carrying the reason recovery STARTED for, not a generic 'no route'");
    Check(!j.BeginPositionRecovery("again", 60),
          "and a finished journey cannot start another recovery");
}

void TestRecoveryCounterIsPerJourney() {
    Section("nav recovery: the budget belongs to the trip");
    travel::Journey j;
    j.Begin("a", 10, 10, 1, 0);
    j.SetRoute(NoRoute("x"), 0);
    j.BeginPositionRecovery("sealed", 0);
    Check(j.PositionRecoveries() == 1, "one attempt");

    j.Begin("b", 20, 20, 1, 100);
    Check(j.PositionRecoveries() == 0, "a new trip starts with a full budget");
    Check(!j.Recovering(), "and is not recovering");
}

// ---------------------------------------------------------------------------
// Magery training bands.
// ---------------------------------------------------------------------------

train::Context MageAt(int skillTenths, int mana) {
    train::Context c;
    c.skillTenths = skillTenths;
    c.manaNow = mana;
    c.manaMax = 100;
    c.haveReagents = true;
    c.safe = true;
    return c;
}

void TestBandSelection() {
    Section("magery training: the historical band, not the convenient one");
    const std::vector<train::Band>& bands = train::MageryBands();

    // The correction this milestone exists for.
    const train::Action* a = train::ChooseAction(bands, MageAt(509, 100));
    Check(a != nullptr, "Magery 50.9 has an action");
    Check(a && a->name == "Greater Heal",
          "and it is GREATER HEAL -- M3 trained this band with Night Sight");
    Check(a && a->spellId == 29, "[SPELL 29]");

    a = train::ChooseAction(bands, MageAt(250, 100));
    Check(a && a->name == "Night Sight", "Night Sight is the 0-30 band");
    a = train::ChooseAction(bands, MageAt(350, 100));
    Check(a && a->name == "Bless", "Bless is 30-40");
    a = train::ChooseAction(bands, MageAt(650, 100));
    Check(a && a->name == "Magic Reflection", "Magic Reflection is 60-70");
    a = train::ChooseAction(bands, MageAt(950, 100));
    Check(a != nullptr, "the top band has an action too");

    // Boundaries are half-open, so 40.0 is already Greater Heal.
    a = train::ChooseAction(bands, MageAt(400, 100));
    Check(a && a->name == "Greater Heal", "40.0 exactly is the Greater Heal band");
    a = train::ChooseAction(bands, MageAt(399, 100));
    Check(a && a->name == "Bless", "39.9 is still Bless");
}

void TestManaGating() {
    Section("magery training: mana is a real constraint");
    const std::vector<train::Band>& bands = train::MageryBands();

    // This is not hypothetical: RevolutionMage2 has INT 20, so a 20 mana pool,
    // and Greater Heal costs 11. It spends most of its day waiting.
    std::string why;
    const train::Action* a = train::ChooseAction(bands, MageAt(509, 10), &why);
    Check(a == nullptr, "10 mana cannot pay for an 11 mana spell");
    Check(why == "not enough mana", "and the reason is actionable");

    a = train::ChooseAction(bands, MageAt(509, 11), &why);
    Check(a != nullptr, "11 mana exactly is enough");
}

void TestSpellbookAndSafety() {
    Section("magery training: book contents and danger");
    const std::vector<train::Band>& bands = train::MageryBands();
    std::string why;

    train::Context c = MageAt(509, 100);
    c.knownSpells = {6, 17};                 // no Greater Heal
    Check(train::ChooseAction(bands, c, &why) == nullptr,
          "a spell that is not in the book cannot be cast");
    Check(why == "spell not in the book", "and it says so");

    c.knownSpells = {29};
    Check(train::ChooseAction(bands, c) != nullptr, "with it, training proceeds");

    c = MageAt(509, 100);
    c.safe = false;
    Check(train::ChooseAction(bands, c, &why) == nullptr,
          "a character in trouble does not stand and practise");
    Check(why == "not safe to train here", "and says why");
}

void TestBandAlternativesNeedALegalTarget() {
    Section("magery training: alternatives are chosen, not sprayed");
    const std::vector<train::Band>& bands = train::MageryBands();
    const train::Band* b = train::BandFor(bands, 750);
    Check(b != nullptr && b->actions.size() >= 3,
          "the 70-80 band really does offer several spells");

    // Reveal targets ground and comes first, so it wins without a companion.
    train::Context c = MageAt(750, 100);
    c.haveOtherCharacter = false;
    const train::Action* a = train::ChooseAction(bands, c);
    Check(a && a->target != train::TargetKind::Character,
          "with nobody else about, a character-targeted spell is not chosen");
}

// ---------------------------------------------------------------------------
// Rules profile.
// ---------------------------------------------------------------------------

rules::BuildSkill S(int id, i32 tenths) { return rules::BuildSkill{id, tenths}; }

void TestSevenHundredCap() {
    Section("rules: 700 points, spent over as many skills as you like");
    const rules::Profile& p = rules::Revolution();
    Check(p.totalSkillCapTenths == 7000, "the cap is 700.0");

    // HX-09 Schoulzen, 8 skills, exactly 700.
    std::vector<rules::BuildSkill> eight = {
        S(25, 1000), S(40, 1000), S(27, 1000), S(16, 800),
        S(30, 800),  S(17, 800),  S(1, 800),   S(46, 800),
    };
    rules::BuildCheck c = rules::ValidateBuild(p, eight);
    Check(c.ok, "a real 8-skill 700 build is legal");
    Check(c.totalTenths == 7000, "and totals exactly 700.0");

    // Seven skills at 100 -- the shape "7x" is usually pictured as.
    std::vector<rules::BuildSkill> seven = {
        S(25, 1000), S(46, 1000), S(16, 1000), S(30, 1000),
        S(17, 1000), S(1, 1000),  S(23, 1000),
    };
    Check(rules::ValidateBuild(p, seven).ok, "seven GM skills is also legal");

    // HX-05 dRead, NINE skills, still exactly 700. This is the one that proves
    // the constraint is the sum and not the count.
    std::vector<rules::BuildSkill> nine = {
        S(25, 1000), S(41, 1000), S(40, 1000), S(27, 1000), S(17, 800),
        S(1, 800),   S(16, 750),  S(46, 500),  S(30, 150),
    };
    rules::BuildCheck n = rules::ValidateBuild(p, nine);
    Check(n.ok, "and NINE skills summing to 700 is legal too");
    Check(n.totalTenths == 7000, "exactly 700.0");
    Check(n.skillCount == 9, "across nine skills");
}

void TestCapViolations() {
    Section("rules: what is not legal");
    const rules::Profile& p = rules::Revolution();

    std::vector<rules::BuildSkill> over = {S(25, 1000), S(46, 1000), S(16, 1000),
                                           S(30, 1000), S(17, 1000), S(1, 1000),
                                           S(23, 1000), S(40, 1)};
    rules::BuildCheck c = rules::ValidateBuild(p, over);
    Check(!c.ok, "700.1 is over the cap");
    Check(c.violation == rules::Violation::OverTotal, "reported as over_total");

    std::vector<rules::BuildSkill> tooHigh = {S(25, 1001)};
    Check(rules::ValidateBuild(p, tooHigh).violation == rules::Violation::OverPerSkill,
          "no single skill may exceed 100.0");

    // The runtime offers ten GM skills. Revolution did not.
    std::vector<rules::BuildSkill> tenGm;
    for (int i = 0; i < 10; ++i) tenGm.push_back(S(i + 1, 1000));
    Check(!rules::ValidateBuild(p, tenGm).ok,
          "a ten-GM build the SERVER would accept is not a Revolution build");
}

void TestInactiveSkills() {
    Section("rules: the nine skills Revolution did not run");
    const rules::Profile& p = rules::Revolution();

    Check(!rules::SkillActive(rules::kMagicResistance), "Resisting Spells is inactive");
    Check(!rules::SkillActive(rules::kHerding), "Herding");
    Check(!rules::SkillActive(rules::kRemoveTrap), "Remove Trap");
    Check(!rules::SkillActive(rules::kEnticement), "Enticement");
    Check(!rules::SkillActive(rules::kPeacemaking), "Peacemaking");
    Check(!rules::SkillActive(rules::kProvocation), "Provocation");
    Check(!rules::SkillActive(rules::kSpiritSpeak), "Spirit Speak");
    Check(!rules::SkillActive(rules::kForensics), "Forensic Evaluation");
    Check(!rules::SkillActive(rules::kTasteId), "Taste Identification");
    Check(rules::InactiveSkills().size() == 9, "nine of them, as the guide lists");

    Check(rules::SkillActive(rules::kMagery), "Magery is active");
    Check(rules::SkillActive(rules::kPoisoning), "Poisoning is active");

    // The one a generator would get wrong by trusting the server.
    std::vector<rules::BuildSkill> withResist = {S(25, 1000), S(rules::kMagicResistance, 1000)};
    rules::BuildCheck c = rules::ValidateBuild(p, withResist);
    Check(!c.ok, "a build containing Resisting Spells is rejected");
    Check(c.violation == rules::Violation::InactiveSkill, "as an inactive skill");
    Check(c.skillId == rules::kMagicResistance, "naming it");

    // Zero in an inactive skill is not a build choice, so it is not an error.
    std::vector<rules::BuildSkill> zeroResist = {S(25, 1000), S(rules::kMagicResistance, 0)};
    Check(rules::ValidateBuild(p, zeroResist).ok, "but a zero entry is harmless");
}

void TestPoisonDistinction() {
    Section("rules: the poison rule, corrected");
    const rules::Profile& p = rules::Revolution();

    // M3.5's compendium said "a poisoner may not exceed Magery 40.0", which
    // would have outlawed the single best-attested build family on the shard.
    // The guide actually restricts USING a poisoned weapon and nothing else.
    Check(rules::CanTrainPoisoning(p, 1000),
          "a Magery 100 character may train Poisoning");
    Check(rules::CanCastPoisonSpell(p, 1000),
          "and may cast the Poison spell -- Poisoning even boosts it");
    Check(rules::CanApplyPoisonToWeapon(p, 1000),
          "the Poisoning skill is what applies poison to a weapon");
    Check(!rules::CanUsePoisonedWeapon(p, 1000),
          "but a Magery 100 warrior may not USE the poisoned weapon");

    Check(rules::CanUsePoisonedWeapon(p, 400),
          "Magery exactly 40.0 may -- the guide says ABOVE 40.0");
    Check(!rules::CanUsePoisonedWeapon(p, 401), "40.1 may not");
    Check(rules::CanUsePoisonedWeapon(p, 0), "and a pure warrior certainly may");

    // The build this vindicates: HX-06 drumatic, "Tam 7x", July 2010.
    std::vector<rules::BuildSkill> warlock = {
        S(25, 850), S(42, 1000), S(27, 1000), S(16, 750),
        S(30, 1000), S(17, 800), S(46, 600), S(1, 1000),
    };
    rules::BuildCheck c = rules::ValidateBuild(p, warlock);
    Check(c.ok, "a Magery 85 / Poisoning 100 warlock is a legal Revolution build");
    Check(c.totalTenths == 7000, "at exactly 700.0");
}

void TestTeaching() {
    Section("rules: NPC teaching arithmetic, as measured live");
    const rules::Profile& p = rules::Revolution();

    Check(rules::TeachingMaxTenths(p, 1000) == 300,
          "a GM trainer teaches to 30.0 -- 30% of its own skill");
    Check(rules::TeachingQuote(p, 0, 1000) == 300,
          "so 0 -> 30.0 costs exactly 300 gold, as remembered");

    // The live M3.5 case: Georgetta's Arms Lore was 72.3, the student had 2.6,
    // and she quoted 191.
    Check(rules::TeachingMaxTenths(p, 723) == 216, "30% of 72.3 is 21.6");
    Check(rules::TeachingQuote(p, 26, 723) == 190, "and 2.6 -> 21.6 quotes 190");

    Check(rules::TeachingMaxTenths(p, 1400) == 420,
          "NPCTrainMax caps the ceiling at 42.0 however good the trainer is");
    Check(rules::TeachingQuote(p, 500, 1000) == 0,
          "a student already past the ceiling is quoted nothing");

    // The finding that is not in anyone's memory.
    Check(p.teacherKeepsChange, "the trainer keeps the change");
    Check(rules::PaymentIsExact(p, 191, 191), "so pay the quote exactly");
    Check(!rules::PaymentIsExact(p, 191, 250),
          "overpaying 250 against a 191 quote loses 59 gold -- measured live");
    Check(!rules::PaymentIsExact(p, 191, 100), "and underpaying is not paying the quote");
}

void TestTrainerDecision() {
    Section("rules: paying a teacher is a choice, made on the numbers");
    const rules::Profile& p = rules::Revolution();

    rules::TrainerSituation s;
    s.currentTenths = 125;          // Mining 12.5
    s.targetTenths = 800;           // the build wants 80
    s.trainerSkillTenths = 1000;    // a GM miner: teaches to 30.0
    s.gold = 1000;
    rules::TrainerDecision d = rules::DecideTraining(p, s);
    Check(d.verdict == rules::TrainerVerdict::Pay, "worth paying");
    Check(d.buyToTenths == 300, "bought up to the trainer's 30.0 ceiling");
    Check(d.pointsBought == 175, "17.5 points");
    Check(d.quote == 175, "for 175 gold -- 1gp per 0.1");

    // Never buy past what the build wants.
    s.targetTenths = 200;
    d = rules::DecideTraining(p, s);
    Check(d.buyToTenths == 200, "a build that only wants 20.0 buys only to 20.0");

    // Already past the ceiling.
    s.currentTenths = 400; s.targetTenths = 800;
    Check(rules::DecideTraining(p, s).verdict == rules::TrainerVerdict::NothingToTeach,
          "a student above the ceiling is told there is nothing to buy");

    // Poor: buys what it can, and the reduced quote is still exact.
    s.currentTenths = 0; s.gold = 60; s.goldReserve = 10;
    d = rules::DecideTraining(p, s);
    Check(d.verdict == rules::TrainerVerdict::Pay, "a poor character still buys something");
    Check(d.quote == 50, "spending only what is spendable");
    Check(rules::PaymentIsExact(p, d.quote, d.quote), "and paying it exactly");

    s.gold = 5; s.goldReserve = 10;
    Check(rules::DecideTraining(p, s).verdict == rules::TrainerVerdict::CannotAfford,
          "below the reserve it does not train at all");

    // Never train a skill Revolution did not run.
    s.gold = 1000; s.goldReserve = 0; s.skillIsActive = false;
    Check(rules::DecideTraining(p, s).verdict == rules::TrainerVerdict::SkillInactive,
          "and never buys an inactive skill");

    // Never spend build budget the character does not have.
    s.skillIsActive = true; s.buildHeadroomTenths = 0;
    Check(rules::DecideTraining(p, s).verdict == rules::TrainerVerdict::ExceedsBuildBudget,
          "a build already at 700 cannot take more points, however cheap");

    s.trainerSkillTenths = 0;
    Check(rules::DecideTraining(p, s).verdict == rules::TrainerVerdict::SkillInactive ||
          rules::DecideTraining(p, s).verdict == rules::TrainerVerdict::NoTrainer,
          "no trainer, no decision");
}

void TestNoStatCapInvented() {
    Section("rules: the stat cap stays unknown");
    // Deliberately absent from Profile. If someone adds one it should be
    // because a source turned up, and this test should be updated with it.
    const rules::Profile& p = rules::Revolution();
    Check(p.totalSkillCapTenths > 0, "skills have a documented cap");
    Check(p.skillLowerFloorTenths == 6700,
          "and the official .skilldusur floor of 670.0 is recorded");
    std::printf("[note] no stat cap is encoded: no archive source states one\n");
}

// ---------------------------------------------------------------------------
// Travel mode selection.
// ---------------------------------------------------------------------------

travelmode::Capability Mage() {
    travelmode::Capability c;
    c.mageryTenths = 600;
    c.manaNow = 50;
    c.haveReagents = true;
    return c;
}

void TestWalkAlwaysWorks() {
    Section("travel: walking is the floor, and never fails");
    travelmode::Capability c;          // knows nothing, owns nothing, no skill
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::Walk,
          "a character with no options still gets there on foot");
    const travelmode::Option w = travelmode::Evaluate(travelmode::Mode::Walk, c, 1800);
    Check(w.usable, "walking is always usable");
    Check(w.estimatedSeconds > 0, "and costs real time");
}

void TestRunebookPreferredOverLooseRune() {
    Section("travel: a page beats a rune, because the page survives");
    travelmode::Capability c = Mage();
    c.haveMarkedRune = true;
    c.haveRunebookPage = true;
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::RunebookRecall,
          "with both available the runebook wins");

    const travelmode::Option loose =
        travelmode::Evaluate(travelmode::Mode::LooseRuneRecall, c, 1800);
    const travelmode::Option book =
        travelmode::Evaluate(travelmode::Mode::RunebookRecall, c, 1800);
    Check(loose.consumesRune, "a loose rune wears out");
    Check(!book.consumesRune, "a runebook page does not");
}

void TestPlannerRejectsUnusableRunebook() {
    Section("travel: an unusable runebook is rejected, with a reason");
    travelmode::Capability c = Mage();
    c.haveRunebookPage = true;
    c.mageryTenths = 200;              // below Recall's 40.0
    c.runebookCharges = 0;

    travelmode::Option o = travelmode::Evaluate(travelmode::Mode::RunebookRecall, c, 1800);
    Check(!o.usable, "Magery 20 cannot cast Recall from an uncharged book");
    Check(!o.why.empty(), "and the planner can say why");
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::Walk, "so it walks");

    // The 13.05.2009 rule: a charge removes the Magery requirement, because a
    // charge is a stored Recall scroll and a scroll supplies the skill.
    c.runebookCharges = 1;
    o = travelmode::Evaluate(travelmode::Mode::RunebookRecall, c, 1800);
    Check(o.usable, "a CHARGED book works at Magery 20");
    Check(o.chargeCost == 1, "spending one charge");
    Check(o.manaCost == 0, "and no mana of the caster's own");
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::RunebookRecall,
          "so the planner uses it");
}

void TestFallbackToMoongate() {
    Section("travel: falls back to the moongate, then to walking");
    travelmode::Capability c;
    c.moongateRouteKnown = true;
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::Moongate,
          "no magic, but a known gate pair beats walking a continent");

    c.moongateRouteKnown = false;
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::Walk, "otherwise, walk");

    // Short trips are not worth a gate or a spell.
    travelmode::Capability m = Mage();
    m.haveRunebookPage = true;
    m.moongateRouteKnown = true;
    Check(travelmode::Choose(m, 6) == travelmode::Mode::Walk,
          "six tiles away, walking is simply quicker");
}

void TestStateBlocksCasting() {
    Section("travel: state the character is in blocks the magical options");
    travelmode::Capability c = Mage();
    c.haveRunebookPage = true;
    c.haveMarkedRune = true;

    c.dead = true;
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::Walk,
          "a ghost does not Recall");
    c.dead = false;
    c.inCombat = true;
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::Walk,
          "nor does a character in combat");

    c.inCombat = false;
    c.manaNow = 3;
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::Walk,
          "nor one without the mana");
    c.runebookCharges = 2;
    Check(travelmode::Choose(c, 1800) == travelmode::Mode::RunebookRecall,
          "unless the book is charged, which needs no mana at all");
}

void TestRankExplainsItself() {
    Section("travel: the planner reports what it rejected and why");
    travelmode::Capability c;
    c.haveRunebookPage = true;         // owned, but no skill and no charge
    const std::vector<travelmode::Option> r = travelmode::Rank(c, 500);
    Check(r.size() == static_cast<usize>(travelmode::Mode::Count),
          "every mode is evaluated, not just the winners");
    bool sawReason = false;
    for (const travelmode::Option& o : r)
        if (!o.usable && !o.why.empty()) sawReason = true;
    Check(sawReason, "and each rejection carries its reason");
    Check(r.front().usable, "the best option is first and usable");
}

}  // namespace

int main() {
    std::printf("m3.6 recovery / training / rules tests\n\n");
    TestRecoveryParksTheJourney();
    TestRecoveryOwnsMovement();
    TestRecoverySucceedsAndResumes();
    TestRecoveryBoundedAndFailsCleanly();
    TestRecoveryCounterIsPerJourney();

    TestBandSelection();
    TestManaGating();
    TestSpellbookAndSafety();
    TestBandAlternativesNeedALegalTarget();

    TestSevenHundredCap();
    TestCapViolations();
    TestInactiveSkills();
    TestPoisonDistinction();
    TestTeaching();
    TestTrainerDecision();
    TestNoStatCapInvented();

    TestWalkAlwaysWorks();
    TestRunebookPreferredOverLooseRune();
    TestPlannerRejectsUnusableRunebook();
    TestFallbackToMoongate();
    TestStateBlocksCasting();
    TestRankExplainsItself();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
