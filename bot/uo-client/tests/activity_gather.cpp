// tests/activity_gather.cpp -- docs/BOT_ARCHITECTURE.md section 22.
//
// One framework for chopping, mining and fishing. Every case below is a
// session this shard produced, and the first one is the worst of them.
//
// No server, no MULs.

#include "uo/activities/gather.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void ExpectStep(const GatherPlan& got, GatherStep want, const char* what) {
    ++g_checks;
    if (got.step != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    GatherStepName(want), GatherStepName(got.step),
                    got.reason);
        ++g_failures;
    }
}

GatherRequest Logs() {
    GatherRequest r;
    r.resource = "logs";
    r.loadWorthTaking = 20;
    r.toolMustBeWielded = true;
    return r;
}

GatherSight AtWork() {
    GatherSight s;
    s.toolInPack = true;
    s.toolWielded = true;
    s.targetInReach = true;
    return s;
}

// THE FORTY COMPLETIONS. "every tree within 24 tiles is worked out" returned
// goal-complete, handing control to a planner with no new information -- so
// it re-picked GATHER_LOGS, for the same character, standing in the same
// clearing, and said the same sentence again. Forty times, progress=0, in
// under two minutes.
void TestAWorkedOutAreaIsNeverDone() {
    std::printf("[gather: a worked-out area sends you elsewhere, not home]\n");
    GatherSight s = AtWork();
    s.targetInReach = false;
    s.areaWorkedOut = true;

    const GatherPlan p = DecideGather(Logs(), s);
    ExpectStep(p, GatherStep::LeaveArea, "nothing left within reach");
    Expect(p.step != GatherStep::Done,
           "and it must NEVER be Done -- that is the forty-completion bug");
    Expect(p.reason && p.reason[0], "with a reason a human can read");
}

// Sphere's mining reads SRC.WEAPON.USESCUR: a pickaxe in the backpack digs
// nothing. Corran carried one for a whole session and mined none.
void TestTheToolMustBeInHand() {
    std::printf("[gather: a tool in the pack is not a tool in hand]\n");
    GatherSight s = AtWork();
    s.toolWielded = false;
    ExpectStep(DecideGather(Logs(), s), GatherStep::ArmTool,
               "carried but not wielded");

    // ...unless the trade does not need it wielded, like fishing, where the
    // pole is armed by the cast itself.
    GatherRequest fishing = Logs();
    fishing.resource = "fish";
    fishing.toolMustBeWielded = false;
    ExpectStep(DecideGather(fishing, s), GatherStep::Swing,
               "a fisher's pole does not need wielding first");
}

void TestNoToolAtAll() {
    std::printf("[gather: no tool is a different problem from a stowed one]\n");
    GatherSight s = AtWork();
    s.toolInPack = false;
    s.toolWielded = false;
    const GatherPlan p = DecideGather(Logs(), s);
    ExpectStep(p, GatherStep::NeedTool, "nothing to work with");
    Expect(p.step != GatherStep::ArmTool,
           "and it is not solved by taking something in hand");
}

// Weight is checked BEFORE the swing. A character that tests it only on
// arrival spends its last ten swings gaining nothing.
void TestAFullPackStopsTheSwing() {
    std::printf("[gather: a full pack is checked before the swing, not after]\n");
    GatherSight s = AtWork();
    s.weightFraction = 0.71;
    ExpectStep(DecideGather(Logs(), s), GatherStep::TakeItIn,
               "over the threshold with a tree right there");

    s.weightFraction = 0.69;
    ExpectStep(DecideGather(Logs(), s), GatherStep::Swing,
               "just under, so carry on");
}

// A full pack outranks even having no tool: the load has to go somewhere
// before anything else can be done about it.
void TestWeightOutranksEverything() {
    std::printf("[gather: the load comes first, whatever else is wrong]\n");
    GatherSight s;
    s.weightFraction = 0.95;
    s.toolInPack = false;
    s.areaWorkedOut = true;
    ExpectStep(DecideGather(Logs(), s), GatherStep::TakeItIn,
               "no tool, no trees, and a pack that must be emptied");
}

void TestALoadWorthTheTrip() {
    std::printf("[gather: a load worth the trip, with nothing left to add]\n");
    GatherSight s;
    s.toolInPack = true;
    s.toolWielded = true;
    s.targetInReach = false;
    s.held = 24;
    ExpectStep(DecideGather(Logs(), s), GatherStep::TakeItIn,
               "twenty-four logs and no tree in reach");

    s.held = 3;
    ExpectStep(DecideGather(Logs(), s), GatherStep::LeaveArea,
               "three logs is not a trip; find more trees");
}

void TestEveryPlanSaysWhy() {
    std::printf("[gather: no silent decisions]\n");
    GatherSight s = AtWork();
    const GatherPlan cases[] = {
        DecideGather(Logs(), s),
        DecideGather(Logs(), GatherSight{}),
    };
    for (const GatherPlan& p : cases)
        Expect(p.reason && p.reason[0], "the plan states its reasoning");
    for (int i = 0; i <= static_cast<int>(GatherStep::Done); ++i)
        Expect(GatherStepName(static_cast<GatherStep>(i))[0] != '?',
               "every step has a name");
}

}  // namespace

int main() {
    std::printf("activity_gather\n");
    TestAWorkedOutAreaIsNeverDone();
    TestTheToolMustBeInHand();
    TestNoToolAtAll();
    TestAFullPackStopsTheSwing();
    TestWeightOutranksEverything();
    TestALoadWorthTheTrip();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
