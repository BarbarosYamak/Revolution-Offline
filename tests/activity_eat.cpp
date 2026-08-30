// tests/activity_eat.cpp -- docs/BOT_ARCHITECTURE.md sections 3, 11, 54.
//
// One decision about food, for every character. The case it exists for is
// v1_Corwyn on 2026-08-30: CONTENT (FOOD=12/15, no hungry=1 ever logged) and
// still four goal picks and three cross-town trips after a baker, because
// carrying zero food scored 62.5 and everything better was blocked.
//
// No server, no MULs.

#include "uo/activities/eat.h"

#include <cstdio>
#include <initializer_list>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

void ExpectStep(const EatPlan& got, EatStep want, const char* what) {
    ++g_checks;
    if (got.step != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    EatStepName(want), EatStepName(got.step), got.reason);
        ++g_failures;
    }
}

EatTuning Default() { return EatTuning{}; }

// The server's eight levels, read as a player reads them. Order matters:
// "very hungry" contains "hungry" and "well fed" contains "fed", so a naive
// matcher reads a starving character as merely peckish.
void TestTheServerSaysEightThings() {
    std::printf("[eat: all eight of the server's own hunger levels]\n");
    struct { const char* line; Hunger want; } cases[] = {
        {"You are starving.",        Hunger::Starving},
        {"You are very hungry.",     Hunger::VeryHungry},
        {"You are hungry.",          Hunger::Hungry},
        {"You are fairly content.",  Hunger::FairlyContent},
        {"You are content.",         Hunger::Content},
        {"You are fed.",             Hunger::Fed},
        {"You are well fed.",        Hunger::WellFed},
        {"You are stuffed.",         Hunger::Stuffed},
    };
    for (const auto& c : cases) {
        Hunger got = Hunger::Unknown;
        const bool ok = ParseHunger(c.line, &got);
        ++g_checks;
        if (!ok || got != c.want) {
            std::printf("  FAIL: '%s' read as %s\n", c.line, HungerName(got));
            ++g_failures;
        }
    }
    Hunger ignored = Hunger::Unknown;
    Expect(!ParseHunger("You see a horse.", &ignored),
           "an unrelated line is not a hunger report");
}

// CORWYN'S THREE TRIPS. Content, zero food carried, no seller in sight.
void TestNotHungryDoesNotCrossATown() {
    std::printf("[eat: below hungry, food is not worth a journey]\n");
    EatSight corwyn;
    corwyn.hunger = Hunger::Content;
    corwyn.foodCarried = 0;
    corwyn.sellerNearby = false;
    corwyn.gold = 9310;
    const EatPlan p = DecideEat(corwyn, Default());
    ExpectStep(p, EatStep::Nothing, "content, empty pack, nobody near");
    Expect(p.step != EatStep::BuyNow,
           "and emphatically not a dedicated shopping trip");

    // ...but standing next to a seller, top up. That is nearly free.
    corwyn.sellerNearby = true;
    ExpectStep(DecideEat(corwyn, Default()), EatStep::BuyIfPassing,
               "the same character, beside a baker");
}

void TestHungryIsWorthAJourney() {
    std::printf("[eat: hungry with an empty pack IS worth going for]\n");
    EatSight s;
    s.hunger = Hunger::Hungry;
    s.foodCarried = 0;
    s.gold = 500;
    ExpectStep(DecideEat(s, Default()), EatStep::BuyNow,
               "the server said hungry");

    s.foodCarried = 3;
    ExpectStep(DecideEat(s, Default()), EatStep::EatNow,
               "with food in the pack, just eat it");
}

void TestStarvingBehavesLikeHungry() {
    std::printf("[eat: worse than hungry is still hungry, only sooner]\n");
    for (Hunger h : {Hunger::Starving, Hunger::VeryHungry, Hunger::Hungry}) {
        EatSight s;
        s.hunger = h;
        s.foodCarried = 2;
        ExpectStep(DecideEat(s, Default()), EatStep::EatNow, HungerName(h));
    }
}

// Hunger is one of the few needs a character can always answer with its own
// hands. No food, no money, and hungry is a reason to fish, not to stall.
void TestBrokeAndHungryForages() {
    std::printf("[eat: no food and no money is a reason to go and get some]\n");
    EatSight s;
    s.hunger = Hunger::VeryHungry;
    s.foodCarried = 0;
    s.gold = 0;
    ExpectStep(DecideEat(s, Default()), EatStep::Forage,
               "hungry, broke, and empty-handed");
}

// A character that has never been told must not conclude it is full.
void TestUnknownIsNotFull() {
    std::printf("[eat: never having heard is not the same as being stuffed]\n");
    EatSight s;
    s.hunger = Hunger::Unknown;
    s.foodCarried = 0;
    s.sellerNearby = true;
    s.gold = 500;
    ExpectStep(DecideEat(s, Default()), EatStep::BuyIfPassing,
               "top up while it is cheap to, rather than assume either way");
}

void TestWellStockedDoesNothing() {
    std::printf("[eat: fed and carrying enough is an answer]\n");
    EatSight s;
    s.hunger = Hunger::WellFed;
    s.foodCarried = 6;
    s.sellerNearby = true;
    s.gold = 9000;
    ExpectStep(DecideEat(s, Default()), EatStep::Nothing,
               "nothing to do, even standing in the bakery");
}

void TestEveryPlanSaysWhy() {
    std::printf("[eat: no silent decisions]\n");
    const EatPlan cases[] = {
        DecideEat(EatSight{}, Default()),
    };
    for (const EatPlan& p : cases)
        Expect(p.reason && p.reason[0], "the plan states its reasoning");
    for (int i = 0; i <= static_cast<int>(EatStep::Forage); ++i)
        Expect(EatStepName(static_cast<EatStep>(i))[0] != '?',
               "every step has a name");
    for (int i = 0; i <= static_cast<int>(Hunger::Unknown); ++i)
        Expect(HungerName(static_cast<Hunger>(i))[0] != '?',
               "every hunger level has a name");
}

}  // namespace

int main() {
    std::printf("activity_eat\n");
    TestTheServerSaysEightThings();
    TestNotHungryDoesNotCrossATown();
    TestHungryIsWorthAJourney();
    TestStarvingBehavesLikeHungry();
    TestBrokeAndHungryForages();
    TestUnknownIsNotFull();
    TestWellStockedDoesNothing();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
