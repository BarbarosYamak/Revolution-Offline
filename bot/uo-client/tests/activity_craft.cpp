// tests/activity_craft.cpp -- docs/BOT_ARCHITECTURE.md sections 18, 19.
//
// The arithmetic of crafting. The WAIT is tested by interaction_progress and
// interaction_handshake; this covers the decision that precedes it.
//
// No server, no MULs.

#include "uo/activities/craft.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void ExpectStep(const CraftPlan& got, CraftStep want, const char* what) {
    ++g_checks;
    if (got.step != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    CraftStepName(want), CraftStepName(got.step), got.reason);
        ++g_failures;
    }
}

CraftRequest Daggers(int total, int reserve) {
    CraftRequest r;
    r.item = "i_dagger";
    r.desiredTotal = total;
    r.minimumMaterialsReserve = reserve;
    return r;
}

void TestBatchIsATotal() {
    std::printf("[craft: a batch is a total to hold, not a number to swing]\n");
    ExpectStep(DecideCraft(Daggers(5, 0), /*held=*/5, /*inputs=*/100),
               CraftStep::Done, "five wanted, five held");
    ExpectStep(DecideCraft(Daggers(5, 0), 9, 100), CraftStep::Done,
               "more than wanted is still done");

    const CraftPlan p = DecideCraft(Daggers(5, 0), 2, 100);
    ExpectStep(p, CraftStep::Make, "two held, three to go");
    Expect(p.remaining == 3, "and it says three");
}

// "A smith that turns every ingot into daggers cannot smith tomorrow."
void TestTheWorkingReserveIsProtected() {
    std::printf("[craft: the working reserve is not spare material]\n");
    ExpectStep(DecideCraft(Daggers(10, 20), 0, 20), CraftStep::ReserveHit,
               "exactly the reserve left: do not eat it");
    ExpectStep(DecideCraft(Daggers(10, 20), 0, 14), CraftStep::ReserveHit,
               "below the reserve is still the reserve");

    const CraftPlan p = DecideCraft(Daggers(10, 20), 0, 23);
    ExpectStep(p, CraftStep::Make, "three above the reserve");
    Expect(p.remaining == 3, "and only those three are usable");
}

void TestNoInputsAtAll() {
    std::printf("[craft: nothing to work with is its own answer]\n");
    const CraftPlan p = DecideCraft(Daggers(5, 0), 0, 0);
    ExpectStep(p, CraftStep::ShortOfInputs, "no ingots");
    Expect(p.reason && p.reason[0], "and it says so");
    // Distinct from ReserveHit on purpose: one is answered by buying inputs,
    // the other by leaving the reserve alone.
    Expect(p.step != CraftStep::ReserveHit,
           "having nothing is not the same as having only the reserve");
}

// Refusing until every input for the whole batch is present is how a crafter
// stands still holding half a batch.
void TestMakesWhatTheMaterialsAllow() {
    std::printf("[craft: make what is possible, do not wait for perfection]\n");
    const CraftPlan p = DecideCraft(Daggers(20, 0), 0, 7);
    ExpectStep(p, CraftStep::Make, "seven sets for a batch of twenty");
    Expect(p.remaining == 7, "make seven now and come back");
}

void TestEveryPlanSaysWhy() {
    std::printf("[craft: no silent decisions]\n");
    const CraftPlan cases[] = {
        DecideCraft(Daggers(5, 0), 5, 10),
        DecideCraft(Daggers(5, 0), 0, 10),
        DecideCraft(Daggers(5, 5), 0, 5),
        DecideCraft(Daggers(5, 0), 0, 0),
    };
    for (const CraftPlan& p : cases)
        Expect(p.reason && p.reason[0], "the plan states its reasoning");
    for (int i = 0; i <= static_cast<int>(CraftStep::ReserveHit); ++i) {
        const char* n = CraftStepName(static_cast<CraftStep>(i));
        Expect(n && n[0] && n[0] != '?', "every step has a name");
    }
}

}  // namespace

int main() {
    std::printf("activity_craft\n");
    TestBatchIsATotal();
    TestTheWorkingReserveIsProtected();
    TestNoInputsAtAll();
    TestMakesWhatTheMaterialsAllow();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
