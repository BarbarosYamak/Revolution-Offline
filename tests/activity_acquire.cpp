// tests/activity_acquire.cpp -- docs/BOT_ARCHITECTURE.md sections 15 and 18.
//
// THE SIX HEATER SHIELDS, as a test.
//
// Corwyn's backpack held six i_shield_heater. Each was bought because the
// hand slot was empty and an armorer sells shields. Nothing asked whether he
// could WEAR one -- it needs STR 90 and he has 56 -- so the slot stayed
// empty and the next tick bought another. Across the recorded runs that shape
// produced 11,645 "buying" lines, 5,140 on Cassia alone.
//
// The decision function below is the one that did not exist.
//
// No server, no MULs.

#include "uo/activities/acquire.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;

using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

void ExpectStep(const AcquirePlan& got, AcquireStep want, const char* what) {
    ++g_checks;
    if (got.step != want) {
        std::printf("  FAIL: %s -- wanted %s, got %s (%s)\n", what,
                    AcquireStepName(want), AcquireStepName(got.step),
                    got.reason);
        ++g_failures;
    }
}

constexpr uo::u16 kHeaterShield = 0x1B76;
constexpr uo::u8  kHand2 = 0x02;

AcquireRequest Shield(bool wearable) {
    AcquireRequest r;
    r.graphic = kHeaterShield;
    r.item = "a heater shield";
    r.layer = kHand2;
    r.mustWear = true;
    r.wearable = wearable;
    r.unwearableReason = "it needs 90 strength and this character has 56";
    return r;
}

// THE BUG. Held one, cannot wear it, slot empty -- and the old code read that
// as "the slot is empty, buy one".
void TestNeverBuyWhatCannotBeWorn() {
    std::printf("[acquire: gold is never spent on what cannot be worn]\n");

    const AcquirePlan carrying = DecideAcquire(Shield(false), /*held=*/1, /*worn=*/0);
    ExpectStep(carrying, AcquireStep::Refuse,
               "one in the pack, unwearable, slot empty");
    Expect(carrying.step != AcquireStep::Buy, "and emphatically not Buy");

    // And with SIX already carried, which is where Corwyn actually got to.
    const AcquirePlan six = DecideAcquire(Shield(false), 6, 0);
    ExpectStep(six, AcquireStep::Refuse, "six in the pack changes nothing");

    // Even with none held: the shop is not the answer either.
    const AcquirePlan none = DecideAcquire(Shield(false), 0, 0);
    ExpectStep(none, AcquireStep::Refuse,
               "unwearable and unheld is still not a purchase");

    Expect(none.reason && none.reason[0], "and the refusal names the reason");
}

// The wear pass and the buy pass used to be separate loops that could not see
// each other, so a piece already carried did not stop the shopping.
void TestCarriedGearIsWornNotRebought() {
    std::printf("[acquire: what is in the pack gets worn, not re-bought]\n");
    ExpectStep(DecideAcquire(Shield(true), 1, 0), AcquireStep::Wear,
               "held and wearable: put it on");
    ExpectStep(DecideAcquire(Shield(true), 3, 0), AcquireStep::Wear,
               "three held: still wear, still do not buy");
}

// Done means the PAPERDOLL, never the pack (section 18).
void TestDoneMeansWorn() {
    std::printf("[acquire: done is the paperdoll, not the backpack]\n");
    ExpectStep(DecideAcquire(Shield(true), 0, kHeaterShield), AcquireStep::Done,
               "on the layer with none spare: finished");
    ExpectStep(DecideAcquire(Shield(true), 2, kHeaterShield), AcquireStep::Done,
               "on the layer with two spare: still finished");

    // A DIFFERENT piece on the layer is not this request satisfied.
    const AcquirePlan other = DecideAcquire(Shield(true), 1, 0x1B73 /*buckler*/);
    ExpectStep(other, AcquireStep::Wear,
               "somebody else's shield on the layer is not ours worn");
}

void TestBuyWhenNoneHeld() {
    std::printf("[acquire: buy only when none is held and it may be worn]\n");
    ExpectStep(DecideAcquire(Shield(true), 0, 0), AcquireStep::Buy,
               "none held, wearable, slot empty");
}

// Not everything acquired is equipment. Stock is counted, not worn.
void TestPlainStock() {
    std::printf("[acquire: stock is counted, equipment is worn]\n");
    AcquireRequest bottles;
    bottles.graphic = 0x0F0E;
    bottles.item = "empty bottles";
    bottles.desiredTotal = 100;
    bottles.mustWear = false;
    bottles.layer = 0;

    ExpectStep(DecideAcquire(bottles, 100, 0), AcquireStep::Done,
               "a hundred wanted, a hundred held");
    ExpectStep(DecideAcquire(bottles, 140, 0), AcquireStep::Done,
               "more than wanted is still done");
    ExpectStep(DecideAcquire(bottles, 40, 0), AcquireStep::Buy,
               "forty held, sixty short");

    // `wearable` is irrelevant to stock: a character too weak to swing a
    // shield may still carry bottles.
    bottles.wearable = false;
    ExpectStep(DecideAcquire(bottles, 40, 0), AcquireStep::Buy,
               "wearability does not gate things that are not worn");
}

void TestEveryPlanSaysWhy() {
    std::printf("[acquire: no silent decisions]\n");
    const AcquirePlan cases[] = {
        DecideAcquire(Shield(true), 0, kHeaterShield),
        DecideAcquire(Shield(true), 1, 0),
        DecideAcquire(Shield(true), 0, 0),
        DecideAcquire(Shield(false), 1, 0),
    };
    for (const AcquirePlan& p : cases)
        Expect(p.reason && p.reason[0], "the plan states its reasoning");

    for (int i = 0; i <= static_cast<int>(AcquireStep::Refuse); ++i) {
        const char* n = AcquireStepName(static_cast<AcquireStep>(i));
        Expect(n && n[0] && n[0] != '?', "every step has a name");
    }
}

}  // namespace

int main() {
    std::printf("activity_acquire\n");
    TestNeverBuyWhatCannotBeWorn();
    TestCarriedGearIsWornNotRebought();
    TestDoneMeansWorn();
    TestBuyWhenNoneHeld();
    TestPlainStock();
    TestEveryPlanSaysWhy();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
