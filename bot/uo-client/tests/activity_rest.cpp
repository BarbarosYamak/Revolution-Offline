// tests/activity_rest.cpp -- docs/BOT_ARCHITECTURE.md sections 12, 32, 47.
//
// "bots shouldnt be idle unless its state specifically" (project owner).
// Idling used to WIN -- 73% of Kaelen's picks, up to 85% elsewhere -- because
// every real errand was blocked and the no-op was the only thing that scored.
//
// No server, no MULs.

#include "uo/activities/rest.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void ExpectStep(const RestPlan& got, RestStep want, const char* what) {
    ++g_checks;
    if (got.step != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    RestStepName(want), RestStepName(got.step), got.reason);
        ++g_failures;
    }
}

RestTuning Default() { return RestTuning{}; }

// Logging out in a hostile place leaves a ghost, and a ghost silently fails
// every later run for that character.
void TestEndingSomewhereSafeOutranksAll() {
    std::printf("[rest: never log out where it is not safe to]\n");
    RestSight ending;
    ending.sessionEnding = true;
    ending.somewhereSafe = false;
    ending.worthExploring = true;      // even with somewhere to go
    ending.hpFraction = 0.3;           // even hurt
    ExpectStep(DecideRest(ending, Default()), RestStep::Settle,
               "the session is ending in the wrong place");

    ending.somewhereSafe = true;
    ExpectStep(DecideRest(ending, Default()), RestStep::Rest,
               "already safe: stay put");
}

void TestRestingToRecoverIsRealWork() {
    std::printf("[rest: standing still to mend is doing something]\n");
    RestSight hurt;
    hurt.regenerating = true;
    hurt.hpFraction = 0.5;
    hurt.worthExploring = true;
    ExpectStep(DecideRest(hurt, Default()), RestStep::Rest,
               "mending beats wandering off");

    hurt.regenerating = false;
    ExpectStep(DecideRest(hurt, Default()), RestStep::Explore,
               "but not if standing here mends nothing");
}

// A full crafter finished a whole session having visited ONE place, which is
// exactly why he knew no supplier for any of the three tools he lacked.
void TestExploringIsTheDefault() {
    std::printf("[rest: blocked for want of knowing is a reason to walk]\n");
    RestSight idle;
    idle.worthExploring = true;
    ExpectStep(DecideRest(idle, Default()), RestStep::Explore,
               "there is somewhere unvisited worth seeing");
}

// Section 47: detect bots that are alive and doing nothing.
void TestLongIdleIsAFault() {
    std::printf("[rest: twenty minutes of nothing is a fault, not a state]\n");
    RestSight stuck;
    stuck.worthExploring = false;
    stuck.blockedForMs = 25 * 60 * 1000;
    const RestPlan p = DecideRest(stuck, Default());
    ExpectStep(p, RestStep::Stagnant, "everything blocked for 25 minutes");
    Expect(p.step != RestStep::Rest,
           "and calling that Rest is what hid 73% of a session");

    stuck.blockedForMs = 5 * 60 * 1000;
    ExpectStep(DecideRest(stuck, Default()), RestStep::Rest,
               "five minutes is a breather, not a fault");
}

void TestEveryPlanSaysWhy() {
    std::printf("[rest: no silent decisions]\n");
    const RestPlan cases[] = {
        DecideRest(RestSight{}, Default()),
    };
    for (const RestPlan& p : cases)
        Expect(p.reason && p.reason[0], "the plan states its reasoning");
    for (int i = 0; i <= static_cast<int>(RestStep::Stagnant); ++i)
        Expect(RestStepName(static_cast<RestStep>(i))[0] != '?',
               "every step has a name");
}

}  // namespace

int main() {
    std::printf("activity_rest\n");
    TestEndingSomewhereSafeOutranksAll();
    TestRestingToRecoverIsRealWork();
    TestExploringIsTheDefault();
    TestLongIdleIsAFault();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
