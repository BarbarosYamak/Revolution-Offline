// Deterministic tests for the M2 action layer's pure state machines.
//
// These exercise include/uo/actions.h -- the exact header the client compiles
// against -- so targeting, action results, drag transactions and life state
// cannot drift from shipping behaviour. They test local logic only; proving
// the server actually does these things is the job of the live scenarios.

#include "uo/actions.h"

#include <cstdio>
#include <string>

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

// ---------------------------------------------------------------------------
// Target cursor: generations are what stop a stale reply answering the wrong
// request, and what keeps two sessions independent.
// ---------------------------------------------------------------------------
void TestTargetState() {
    Section("target state");

    act::TargetState t;
    Check(!t.Active(), "starts with no cursor");
    Check(!t.CanReply(0), "cannot reply before a cursor is armed");

    t.OnArmed(0x2CE, 0, 0, 1000);
    Check(t.Active(), "armed");
    Check(t.Generation() == 1, "first cursor is generation 1");
    Check(t.Current().id == 0x2CE, "cursor id retained for echoing back");
    Check(t.CanReply(1), "the live generation may reply");
    Check(t.CanReply(0), "generation 0 means 'whatever is current'");

    // Replying consumes the cursor: Sphere's cursor is single-shot
    // (CClient::Event_Target clears the target mode before dispatching).
    t.OnReplied();
    Check(!t.Active(), "cursor consumed by the reply");
    Check(!t.CanReply(1), "cannot reply twice to the same cursor");

    // A second cursor supersedes the first. An action still holding
    // generation 1 must not be able to answer it.
    t.OnArmed(0x2CA, 0, 1, 2000);
    Check(t.Generation() == 2, "second cursor bumps the generation");
    Check(t.Current().subtype == 1, "harmful flag retained");
    Check(t.IsStale(1), "the old generation is stale");
    Check(!t.IsStale(2), "the current generation is not stale");
    Check(!t.CanReply(1), "a stale generation is refused");
    Check(t.CanReply(2), "the current generation is accepted");

    // Cancelling leaves no cursor but keeps the generation moving forward.
    t.OnCancelled();
    Check(!t.Active(), "cancel clears the cursor");
    t.OnArmed(0x2C9, 1, 0, 3000);
    Check(t.Generation() == 3 && t.Current().type == 1,
          "ground cursor armed as generation 3");

    // Two sessions must not share any of this.
    act::TargetState a, b;
    a.OnArmed(0x2CE, 0, 0, 10);
    Check(a.Active() && !b.Active(), "one session's cursor is not the other's");
    Check(a.Generation() == 1 && b.Generation() == 0,
          "generations are per-session");
    b.OnArmed(0x2CA, 0, 0, 20);
    Check(a.Current().id == 0x2CE && b.Current().id == 0x2CA,
          "cursor ids stay separate");
}

// ---------------------------------------------------------------------------
// Action results: the whole point is telling "sent" from "server-confirmed".
// ---------------------------------------------------------------------------
void TestAction() {
    Section("action result model");

    act::Action a;
    Check(!a.Active(), "idle action is not active");
    Check(a.kind == act::Kind::None, "idle action has no kind");

    a.Begin(act::Kind::MoveItem, 1000, 4000);
    Check(a.Active(), "started");
    Check(a.result == act::Result::Pending, "starts pending");
    Check(!act::Finished(a.result), "pending is not finished");
    Check(a.deadlineMs == 5000, "deadline is start + timeout");

    // Not yet due.
    Check(!a.ExpireIfDue(4999), "does not expire before the deadline");
    Check(a.Active(), "still active just before the deadline");

    Check(a.Finish(act::Result::Success), "finishing returns true once");
    Check(!a.Active(), "finished action is not active");
    Check(a.result == act::Result::Success, "result recorded");
    Check(act::Finished(a.result), "success is finished");
    Check(!a.Finish(act::Result::Rejected),
          "a finished action cannot be finished again");
    Check(a.result == act::Result::Success, "the first result stands");

    // Timeout path.
    act::Action t;
    t.Begin(act::Kind::CastSpell, 0, 100);
    Check(t.ExpireIfDue(100), "expires exactly at the deadline");
    Check(t.result == act::Result::Timeout, "expiry records a timeout");
    Check(!t.ExpireIfDue(200), "expiry only fires once");

    // Begin() must clear the previous action's context so stale serials can
    // never leak into the next action's confirmation rules.
    act::Action r;
    r.Begin(act::Kind::Equip, 0, 10);
    r.subject = 0xAABBCCDD;
    r.layer = 7;
    r.awaitingTarget = true;
    r.Begin(act::Kind::UseObject, 50, 10);
    Check(r.subject == 0 && r.layer == 0 && !r.awaitingTarget,
          "Begin clears the previous context");
    Check(r.kind == act::Kind::UseObject, "new kind set");

    // Names exist for every result and kind (they end up in the logs).
    Check(std::string(act::ResultName(act::Result::ServerFailure)) ==
              "server_failure", "result names");
    Check(std::string(act::KindName(act::Kind::VendorBuy)) == "vendor_buy",
          "kind names");
}

// ---------------------------------------------------------------------------
// Drag transactions: a rejected move must leave no belief that it happened.
// ---------------------------------------------------------------------------
void TestDragState() {
    Section("drag transaction");

    act::DragState d;
    Check(!d.InFlight(), "idle");
    Check(d.phase() == act::DragState::Phase::Idle, "idle phase");

    d.BeginLift(0x40001FF9, 1, 500);
    Check(d.InFlight(), "lift in flight");
    Check(d.phase() == act::DragState::Phase::Lifted, "lifted phase");
    Check(d.Serial() == 0x40001FF9 && d.Amount() == 1, "lift recorded");

    d.OnDropSent(0x40001FFE);
    Check(d.phase() == act::DragState::Phase::Dropped, "dropped phase");
    Check(d.Destination() == 0x40001FFE, "destination recorded");
    Check(d.InFlight(), "still in flight until the server confirms");

    // A 0x27 drag cancel, or a timeout, resets everything: nothing moved.
    d.Reset();
    Check(!d.InFlight() && d.Serial() == 0 && d.Destination() == 0,
          "reset clears the whole transaction");

    act::DragState x, y;
    x.BeginLift(1, 1, 0);
    Check(x.InFlight() && !y.InFlight(), "drag state is per-session");
}

// ---------------------------------------------------------------------------
// Life state is derived from the body the server sends -- never set locally.
// ---------------------------------------------------------------------------
void TestLifeState() {
    Section("life state");

    Check(!act::IsGhostBody(0x0190), "male body is alive");
    Check(!act::IsGhostBody(0x0191), "female body is alive");
    Check(act::IsGhostBody(0x0192), "0x192 is a ghost");
    Check(act::IsGhostBody(0x0193), "0x193 is a ghost");

    Check(act::LifeStateFromBody(0x0190) == act::LifeState::Alive,
          "alive from body");
    Check(act::LifeStateFromBody(0x0192) == act::LifeState::Dead,
          "dead from body");

    Check(std::string(act::LifeStateName(act::LifeState::Dead)) == "dead",
          "state name dead");
    Check(std::string(act::LifeStateName(act::LifeState::Alive)) == "alive",
          "state name alive");

    // The transition a death/resurrection cycle goes through.
    act::LifeState s = act::LifeStateFromBody(0x0190);
    s = act::LifeStateFromBody(0x0192);
    Check(s == act::LifeState::Dead, "alive -> dead");
    s = act::LifeStateFromBody(0x0190);
    Check(s == act::LifeState::Alive, "dead -> alive");
}

// ---------------------------------------------------------------------------
// System-message classification: wave2 2026-09-01 root cause. cast_spell and
// vendor_sell sat pending for their full deadline because Sphere's own
// refusal text ("You lack ... for this spell", "You are selling too fast.")
// was not recognised, so a real refusal read back as a timeout instead of a
// ServerFailure. Pulled into act:: (uo/actions.h) so it is testable here
// without a live Client/socket.
// ---------------------------------------------------------------------------
void TestSysMessageClassification() {
    Section("system-message classification");

    // core/messages.scp:891 spell_try_noregs, verbatim (run_gates/
    // g_Illyria.console.txt:86, g_Selene.console.txt:91).
    Check(act::IsSpellCastRefusal("You lack Sulfurous Ash for this spell"),
          "reagent refusal recognised");
    Check(act::IsSpellCastRefusal("You lack Mandrake Root for this spell"),
          "a different reagent name still matches");
    // core/messages.scp:889 spell_try_nomana, verbatim.
    Check(act::IsSpellCastRefusal("You lack sufficient mana for this spell"),
          "mana refusal recognised");
    Check(act::IsSpellCastRefusal("More reagents are needed for this spell"),
          "the older 'more reagents' phrasing still matches");
    Check(act::IsSpellCastRefusal("The spell fizzles."),
          "fizzle still matches");
    Check(!act::IsSpellCastRefusal("You cast the spell."),
          "an unrelated line is not a refusal");
    Check(!act::IsSpellCastRefusal(nullptr), "null text is not a refusal");

    // core/messages.scp:759-760 npc_vendor_buyfast/sellfast, verbatim
    // (run_gates/g_Dorvar.console.txt:455).
    Check(act::IsVendorRateLimited("You are selling too fast."),
          "sell rate limit recognised");
    Check(act::IsVendorRateLimited("You are buying too fast."),
          "buy rate limit recognised");
    Check(!act::IsVendorRateLimited("You are selling that too cheap."),
          "an unrelated vendor line is not a rate limit");

    // Case-insensitive, matching Client::ActionOnSysMessage's own `contains`
    // (both are ASCII system text from the same source).
    Check(act::IsSpellCastRefusal("YOU LACK SULFUROUS ASH FOR THIS SPELL"),
          "classification is case-insensitive");
}

// ---------------------------------------------------------------------------
// Eating: 2026-09-02 root cause. A double-click on food is answered by text
// alone, and none of that text was recognised, so every eat timed out and the
// hunger flag -- read as "was it EVER said" -- never moved. Both halves are
// pure text classification, so both are checked here.
// ---------------------------------------------------------------------------
void TestEatClassification() {
    Section("eat outcome and hunger statements");

    // Verbatim from run_gates/g_Halain.console.txt:144,150 (food_full_3 and
    // food_full_4, messages.scp:171-172).
    Check(act::ClassifyEatMessage(
              "You eat the food, and begin to feel more satiated.") ==
              act::EatOutcome::Ate,
          "food_full_3 is a confirmed meal");
    Check(act::ClassifyEatMessage(
              "You are nearly stuffed, but manage to eat the food.") ==
              act::EatOutcome::Ate,
          "food_full_4 is a confirmed meal");
    Check(act::ClassifyEatMessage(
              "You eat the food, but are still extremely hungry.") ==
              act::EatOutcome::Ate,
          "food_full_1 is still a meal, hungry or not");
    Check(act::ClassifyEatMessage("You are stuffed!") == act::EatOutcome::Ate,
          "food_full_6 is a confirmed meal");
    // messages.scp:167 food_canteatf -- nothing was eaten, and that is not a
    // failure the caller should retry.
    Check(act::ClassifyEatMessage("You are simply too full to eat any more!") ==
              act::EatOutcome::AlreadyFull,
          "a full stomach is its own outcome");
    Check(act::ClassifyEatMessage("You can't really eat this.") ==
              act::EatOutcome::CannotEat,
          "inedible is a server refusal");
    Check(act::ClassifyEatMessage("You put the logs in your pack.") ==
              act::EatOutcome::None,
          "an unrelated line is not an eat outcome");
    Check(act::ClassifyEatMessage(nullptr) == act::EatOutcome::None,
          "null text is not an eat outcome");

    // The hunger table: every statement resolves, on the shared eight-band
    // status scale, to the right side of "hungry".
    struct Row { const char* line; bool hungry; bool starving; };
    static const Row kRows[] = {
        {"You are starving",                                   true,  true},
        {"You are very hungry",                                true,  false},
        {"You are hungry",                                     true,  false},
        {"You are fairly content",                             false, false},
        {"You are stuffed",                                    false, false},
        {"You eat the food, but are still extremely hungry.",  true,  false},
        {"After eating the food, you feel much less hungry.",  true,  false},
        {"You eat the food, and begin to feel more satiated.", false, false},
        {"You are nearly stuffed, but manage to eat the food.",false, false},
        {"You feel quite full after consuming the food.",      false, false},
        {"You are simply too full to eat any more!",           false, false},
    };
    usize rows = 0;
    const act::HungerStatement* table = act::HungerStatements(&rows);
    for (const Row& r : kRows) {
        int level = -1;
        // Most-specific-first, exactly as Runner::Observe reads it.
        for (usize i = 0; i < rows; ++i) {
            if (act::ContainsCI(r.line, table[i].text)) { level = table[i].level; break; }
        }
        Check(level >= 0, r.line);
        const bool hungry   = (level >= 0 && level <= act::kHungerLevelHungry);
        const bool starving = (level >= 0 && level <= act::kHungerLevelStarving);
        Check(hungry == r.hungry && starving == r.starving, r.line);
    }

    // The regression itself: the login line says hungry, the meal answers
    // later, and the LATER statement is the one that counts.
    int loginLevel = -1, mealLevel = -1;
    for (usize i = 0; i < rows; ++i) {
        if (loginLevel < 0 && act::ContainsCI("You are hungry", table[i].text))
            loginLevel = table[i].level;
        if (mealLevel < 0 &&
            act::ContainsCI("You are nearly stuffed, but manage to eat the food.",
                            table[i].text))
            mealLevel = table[i].level;
    }
    Check(loginLevel <= act::kHungerLevelHungry &&
              mealLevel > act::kHungerLevelHungry,
          "eating moves the character out of the hungry bands");
}

// ---------------------------------------------------------------------------
// Equipping something already worn is not an equip: the server strips it into
// the pack, which reads back as "the hand is empty" and starts the loop again.
// ---------------------------------------------------------------------------
void TestEquipNoOp() {
    Section("equip no-op");

    Check(!act::EquipWouldBeNoOp(-1, 0), "not worn: layer 0 must really equip");
    Check(!act::EquipWouldBeNoOp(-1, 1), "not worn: hand1 must really equip");
    Check(act::EquipWouldBeNoOp(1, 0),
          "worn in hand1, asked for 'server chooses' -> no-op");
    Check(act::EquipWouldBeNoOp(2, 2), "worn on the very layer asked for -> no-op");
    Check(act::EquipWouldBeNoOp(1, 1), "the fishing-pole case: hand1 -> hand1");
    Check(!act::EquipWouldBeNoOp(1, 2),
          "worn in hand1 but asked for hand2 is a real move");
    Check(!act::EquipWouldBeNoOp(0x15, 1),
          "the backpack layer is not the hand that was asked for");
}

}  // namespace

int main() {
    std::printf("m2 action layer tests\n\n");
    TestTargetState();
    TestAction();
    TestDragState();
    TestLifeState();
    TestSysMessageClassification();
    TestEatClassification();
    TestEquipNoOp();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
