// tests/activity_recovery.cpp -- docs/BOT_ARCHITECTURE.md sections 25, 47.
//
// Death on this shard is FULL LOOT, so the corpse run is a life's whole
// equipment. Which is exactly why it has to be able to say no.
//
// The handler these rules replace walked straight back with no evaluation of
// any kind -- no health check, no danger memory, no attempt limit -- which is
// the precise shape of a death loop. This project has never seen one only
// because in 21 recorded runs no bot has ever died, so these rules are
// written BEFORE the first death rather than after the first loop.
//
// No server, no MULs.

#include "uo/activities/recovery.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void ExpectStep(const RecoveryPlan& got, RecoveryStep want, const char* what) {
    ++g_checks;
    if (got.step != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    RecoveryStepName(want), RecoveryStepName(got.step),
                    got.reason);
        ++g_failures;
    }
}

RecoverySight Fallen() {
    RecoverySight s;
    s.corpseKnown = true;
    s.corpseDistance = 40;
    s.hpFraction = 1.0;
    return s;
}

RecoveryTuning Default() { return RecoveryTuning{}; }

void TestAGhostDoesNothingElse() {
    std::printf("[recovery: a ghost can only seek resurrection]\n");
    RecoverySight ghost = Fallen();
    ghost.dead = true;
    ghost.dangerHeatAtCorpse = 1.0;   // even somewhere lethal
    ghost.attemptsSoFar = 99;         // even after giving up
    ExpectStep(DecideRecovery(ghost, Default()), RecoveryStep::SeekResurrection,
               "nothing else is possible while dead");
}

// THE DEATH LOOP. UO raises a ghost at roughly a tenth of its health, and
// walking back into the fight that killed you at a tenth is not courage.
void TestTooHurtToWalkBack() {
    std::printf("[recovery: heal before walking back into what killed you]\n");
    RecoverySight raised = Fallen();
    raised.hpFraction = 0.10;         // as a resurrection leaves you
    ExpectStep(DecideRecovery(raised, Default()), RecoveryStep::Recover,
               "a tenth of health is how the loop starts");

    raised.hpFraction = 0.54;
    ExpectStep(DecideRecovery(raised, Default()), RecoveryStep::Recover,
               "just under the bar is still under it");

    raised.hpFraction = 0.60;
    ExpectStep(DecideRecovery(raised, Default()), RecoveryStep::TravelToCorpse,
               "healthy enough to go");
}

// "A cautious bot may abandon a corpse in extreme danger" -- and the gear is
// genuinely gone. That is still cheaper than the loop.
void TestDangerousPlacesAreAbandoned() {
    std::printf("[recovery: a place that keeps killing you is not worth it]\n");
    RecoverySight lethal = Fallen();
    lethal.dangerHeatAtCorpse = 0.95;
    lethal.attemptsSoFar = 1;  // one prepared recovery was already tried
    ExpectStep(DecideRecovery(lethal, Default()), RecoveryStep::Abandon,
               "died there again and again");

    // A BOLD character goes back into more than a timid one will. Personality
    // decides, not the activity.
    RecoveryTuning bold = Default();
    bold.riskTolerance = 1.0;
    ExpectStep(DecideRecovery(lethal, bold), RecoveryStep::TravelToCorpse,
               "the same place, a character that does not care");

    RecoveryTuning timid = Default();
    timid.riskTolerance = 0.0;
    RecoverySight mild = Fallen();
    mild.dangerHeatAtCorpse = 0.40;
    mild.attemptsSoFar = 1;
    ExpectStep(DecideRecovery(mild, timid), RecoveryStep::Abandon,
               "mildly dangerous is enough for a cautious life");
    ExpectStep(DecideRecovery(mild, Default()), RecoveryStep::TravelToCorpse,
               "and not enough for an ordinary one");

    RecoverySight firstTry = Fallen();
    firstTry.dangerHeatAtCorpse = 1.0;
    firstTry.attemptsSoFar = 0;
    ExpectStep(DecideRecovery(firstTry, timid), RecoveryStep::TravelToCorpse,
               "even a cautious life makes one prepared full-loot recovery attempt");
}

// A corpse decays; an errand that cannot count does not.
void TestAttemptsRunOut() {
    std::printf("[recovery: enough trips is enough]\n");
    RecoverySight tried = Fallen();
    tried.attemptsSoFar = 3;
    ExpectStep(DecideRecovery(tried, Default()), RecoveryStep::Abandon,
               "three walks at one corpse");

    tried.attemptsSoFar = 2;
    ExpectStep(DecideRecovery(tried, Default()), RecoveryStep::TravelToCorpse,
               "two is not yet three");
}

// A character that loots its armour and walks off still wearing nothing has
// recovered its things and not its safety.
void TestGearGoesBackOn() {
    std::printf("[recovery: recovered gear belongs on the paperdoll]\n");
    RecoverySight looted = Fallen();
    looted.gearInPack = true;
    ExpectStep(DecideRecovery(looted, Default()), RecoveryStep::ReEquip,
               "in the pack is not on the body");

    // ...and it outranks going back for more, but never outranks being dead.
    looted.dead = true;
    ExpectStep(DecideRecovery(looted, Default()), RecoveryStep::SeekResurrection,
               "unless the character is a ghost again");
}

void TestNothingToRecover() {
    std::printf("[recovery: an empty or unknown corpse ends the errand]\n");
    RecoverySight unknown;
    unknown.corpseKnown = false;
    ExpectStep(DecideRecovery(unknown, Default()), RecoveryStep::Done,
               "nowhere to go back to");

    RecoverySight empty = Fallen();
    empty.corpseEmpty = true;
    ExpectStep(DecideRecovery(empty, Default()), RecoveryStep::Done,
               "already stripped");
}

void TestStandingOverItLoots() {
    std::printf("[recovery: at the corpse, take the things]\n");
    RecoverySight here = Fallen();
    here.corpseDistance = 1;
    ExpectStep(DecideRecovery(here, Default()), RecoveryStep::Loot,
               "standing over it");
}

void TestEveryPlanSaysWhy() {
    std::printf("[recovery: no silent decisions]\n");
    const RecoveryPlan cases[] = {
        DecideRecovery(Fallen(), Default()),
        DecideRecovery(RecoverySight{}, Default()),
    };
    for (const RecoveryPlan& p : cases)
        Expect(p.reason && p.reason[0], "the plan states its reasoning");
    for (int i = 0; i <= static_cast<int>(RecoveryStep::Done); ++i)
        Expect(RecoveryStepName(static_cast<RecoveryStep>(i))[0] != '?',
               "every step has a name");
}

}  // namespace

int main() {
    std::printf("activity_recovery\n");
    TestAGhostDoesNothingElse();
    TestTooHurtToWalkBack();
    TestDangerousPlacesAreAbandoned();
    TestAttemptsRunOut();
    TestGearGoesBackOn();
    TestNothingToRecover();
    TestStandingOverItLoots();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
