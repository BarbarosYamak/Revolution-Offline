// Deterministic tests for the M3.5 units: fleet connection admission, bounded
// route variation, and economic transformation knowledge.
//
// The connection numbers here are the shard's real ones, read out of
// runtime/sphere.ini and runtime/scripts/core/serv_triggers.scp, because the
// whole point of the controller is to model THIS server's guard:
//
//     MaxConnectRequestsPerIP=50   (does not decay; resets only after NetTTL
//                                   of silence since the LAST attempt)
//     NetTTL=60*5                  (300s)
//     f_onserver_connectreq_ex     (reject AND ban for ~300s)
//
// M3 earned that ban live and then re-earned it on every retry. These tests
// exist so that cannot happen again by accident.

#include "uo/economy.h"
#include "uo/fleet.h"
#include "uo/route_style.h"

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

// The measured server guard.
fleet::Policy RevolutionPolicy() {
    fleet::Policy p;
    p.serverMaxRequestsPerIp = 50;
    p.budget = 30;              // deliberate margin under the server's 50
    p.counterResetMs = 300000;  // NetTTL
    p.banMs = 300000;           // LOCAL.BAN_TIMEOUT
    p.minSpacingMs = 3000;
    p.jitterPercent = 0;        // off, so the arithmetic below is exact
    return p;
}

// ---------------------------------------------------------------------------

void TestAdmissionBasics() {
    Section("fleet admission: budget and spacing");
    fleet::AdmissionController c(RevolutionPolicy(), 1234);

    Check(c.Request(0).allowed, "the first connection of a session is allowed");
    Check(c.BudgetRemaining() == 30, "asking does not spend budget");

    c.NoteAttempt(0);
    c.NoteSuccess(0);
    Check(c.BudgetRemaining() == 29, "attempting does");

    // Spacing: a second login one second later is too eager.
    fleet::Verdict v = c.Request(1000);
    Check(!v.allowed, "a second attempt 1s later is refused");
    Check(v.refusal == fleet::Refusal::Spacing, "for spacing");
    Check(v.retryAfterMs == 2000, "with the exact remaining wait");

    Check(c.Request(3000).allowed, "and allowed once the stagger has passed");
}

void TestBudgetIsUnderServerThreshold() {
    Section("fleet admission: margin under the server threshold");
    fleet::Policy p = RevolutionPolicy();
    fleet::AdmissionController c(p, 7);

    Check(p.budget < p.serverMaxRequestsPerIp,
          "our ceiling sits below the server's, on purpose");

    i64 t = 0;
    for (u32 i = 0; i < p.budget; ++i) {
        Check(c.Request(t).allowed, "each attempt inside the budget is allowed");
        c.NoteAttempt(t);
        c.NoteSuccess(t);
        t += p.minSpacingMs;
    }

    const fleet::Verdict v = c.Request(t);
    Check(!v.allowed, "the budget stops us");
    Check(v.refusal == fleet::Refusal::BudgetSpent, "and says why");
    Check(c.AttemptsUsed() == p.budget, "having spent exactly the budget");
    Check(c.AttemptsUsed() < p.serverMaxRequestsPerIp,
          "which is still short of the server's threshold, so no ban was risked");
}

void TestCounterResetsOnlyAfterSilence() {
    Section("fleet admission: the counter does not decay");
    fleet::Policy p = RevolutionPolicy();
    fleet::AdmissionController c(p, 99);

    i64 t = 0;
    for (u32 i = 0; i < p.budget; ++i) {
        c.NoteAttempt(t);
        c.NoteSuccess(t);
        t += p.minSpacingMs;
    }
    Check(c.BudgetRemaining() == 0, "budget spent");

    // The loop leaves `t` one stagger PAST the final attempt, so measure the
    // window from where the server would: the last attempt itself.
    const i64 lastAttempt = t - p.minSpacingMs;

    // A leaky bucket would have refilled by now. This one must not: the
    // server's counter "does not decay".
    Check(!c.Request(lastAttempt + p.counterResetMs - 1).allowed,
          "one millisecond short of the silent window is still refused");

    Check(c.Request(lastAttempt + p.counterResetMs).allowed,
          "a full silent window since the LAST attempt clears it");
    Check(c.BudgetRemaining() == p.budget, "and the whole budget returns at once");
}

void TestSilenceIsMeasuredFromLastAttempt() {
    Section("fleet admission: a rejected attempt still restarts the clock");
    fleet::Policy p = RevolutionPolicy();
    fleet::AdmissionController c(p, 5);

    i64 t = 0;
    for (u32 i = 0; i < p.budget; ++i) { c.NoteAttempt(t); t += p.minSpacingMs; }
    const i64 lastAttempt = t - p.minSpacingMs;

    // Wait most of the way out, then let something touch the socket anyway --
    // which is precisely what M3's retry loop did.
    const i64 nearlyThere = lastAttempt + p.counterResetMs - 1000;
    c.NoteAttempt(nearlyThere);

    Check(!c.Request(lastAttempt + p.counterResetMs).allowed,
          "the wait we had almost served is gone");
    Check(c.CounterResetsAtMs() == nearlyThere + p.counterResetMs,
          "the reset moved out to a full window after the NEW attempt");
}

void TestBanIsAbsolute() {
    Section("fleet admission: never touch the socket while banned");
    fleet::Policy p = RevolutionPolicy();
    fleet::AdmissionController c(p, 3);

    c.NoteAttempt(0);
    c.NoteBanned(0);
    Check(c.Banned(0), "we know we are banned");
    Check(c.BanUntilMs() == p.banMs, "for the configured ban length");

    const fleet::Verdict v = c.Request(1000);
    Check(!v.allowed, "and so we do not connect");
    Check(v.refusal == fleet::Refusal::Banned, "for that reason");
    Check(v.retryAfterMs == p.banMs - 1000, "waiting out the remainder");

    // The failure mode this whole class exists to prevent: a retry during the
    // ban would be rejected AND would push the server's reset clock forward,
    // so the fleet would never recover. The controller must refuse for the
    // entire window, every time it is asked.
    for (i64 t = 0; t < p.banMs; t += 25000)
        Check(!c.Request(t).allowed, "refused at every point inside the ban");

    Check(c.Request(p.banMs + p.counterResetMs).allowed,
          "and recovers cleanly once the ban and the silence have both passed");
}

void TestBackoffAndBreaker() {
    Section("fleet admission: backoff and circuit breaker");
    fleet::Policy p = RevolutionPolicy();
    p.backoffBaseMs = 5000;
    p.breakerFailures = 3;
    p.breakerCooldownMs = 600000;
    fleet::AdmissionController c(p, 11);

    c.NoteAttempt(0);
    c.NoteFailure(0);
    Check(c.ConsecutiveFailures() == 1, "one failure counted");
    Check(!c.Request(4000).allowed, "backoff holds us past the plain stagger");
    Check(c.Request(5000).allowed, "and releases at the base delay");

    c.NoteAttempt(5000);
    c.NoteFailure(5000);
    Check(!c.Request(5000 + 9999).allowed, "the second backoff has doubled");
    Check(c.Request(5000 + 10000).allowed, "to 10s");

    c.NoteAttempt(20000);
    c.NoteFailure(20000);
    Check(c.CircuitOpen(20000), "three consecutive failures opens the breaker");
    Check(c.Request(21000).refusal == fleet::Refusal::CircuitOpen,
          "and everything is refused while it is open");

    c.NoteAttempt(20000 + p.breakerCooldownMs);
    c.NoteSuccess(20000 + p.breakerCooldownMs);
    Check(!c.CircuitOpen(20000 + p.breakerCooldownMs + 1), "a success closes it");
    Check(c.ConsecutiveFailures() == 0, "and clears the failure streak");
}

void TestNeverSpins() {
    Section("fleet admission: a refusal always carries a wait");
    fleet::Policy p = RevolutionPolicy();
    p.jitterPercent = 25;
    fleet::AdmissionController c(p, 4242);

    c.NoteAttempt(0);
    c.NoteBanned(0);
    // Whatever the reason, retryAfterMs > 0 -- a caller that honours it can
    // never busy-loop against the server.
    for (i64 t = 0; t < 900000; t += 17000) {
        const fleet::Verdict v = c.Request(t);
        if (!v.allowed) Check(v.retryAfterMs > 0, "refusal names a positive wait");
    }
}

void TestJitterIsBoundedAndSeeded() {
    Section("fleet admission: jitter is bounded and reproducible");
    fleet::Policy p = RevolutionPolicy();
    p.jitterPercent = 25;

    fleet::AdmissionController a(p, 777);
    fleet::AdmissionController b(p, 777);
    a.NoteAttempt(0);
    b.NoteAttempt(0);

    const i64 wa = a.Request(1000).retryAfterMs;
    const i64 wb = b.Request(1000).retryAfterMs;
    Check(wa == wb, "same seed, same wait -- tests stay reproducible");
    Check(wa >= 2000, "jitter never shortens a wait below what the server needs");
    Check(wa <= 2000 + 500, "and is bounded by the configured percentage");

    fleet::AdmissionController other(p, 778);
    other.NoteAttempt(0);
    // Different members of a fleet should not re-converge after an outage.
    // (Not asserted as inequality -- one draw may collide -- but the seed
    // must at least be honoured, which the equality above already proves.)
    Check(other.Request(1000).retryAfterMs >= 2000, "another member also waits at least the stagger");
}

// ---------------------------------------------------------------------------

void TestStyleIsStablePerCharacter() {
    Section("route variation: a character's habits are stable");
    const routing::Style a1 = routing::StyleForCharacter("RevolutionBot01");
    const routing::Style a2 = routing::StyleForCharacter("revolutionbot01");
    Check(a1.seed == a2.seed, "case does not change who you are");
    Check(a1.preference == a2.preference, "nor your preference");

    const routing::Style b = routing::StyleForCharacter("RevolutionFisher");
    Check(a1.seed != b.seed, "different characters get different seeds");
    Check(a1.laneWidth <= 2 && b.laneWidth <= 2, "drift stays bounded");
}

void TestPopulationSpread() {
    Section("route variation: a population is not uniform");
    int counts[static_cast<int>(routing::Preference::Count)] = {0};
    char name[32];
    for (int i = 0; i < 200; ++i) {
        std::snprintf(name, sizeof(name), "RevolutionBot%03d", i);
        ++counts[static_cast<int>(routing::StyleForCharacter(name).preference)];
    }
    int used = 0;
    for (int i = 0; i < static_cast<int>(routing::Preference::Count); ++i)
        if (counts[i] > 0) ++used;
    Check(used == static_cast<int>(routing::Preference::Count),
          "every preference occurs across 200 characters");
}

void TestCellBiasBoundedAndDeterministic() {
    Section("route variation: spatial bias is bounded and repeatable");
    routing::Variation v(routing::StyleForCharacter("Shika"));

    for (i32 x = 1400; x < 1460; ++x) {
        for (i32 y = 1740; y < 1780; y += 7) {
            const i32 b = v.CellBias(x, y, 4);
            Check(b >= 0 && b <= 4, "bias stays inside the requested bound");
            Check(b == v.CellBias(x, y, 4), "and is the same every time it is asked");
        }
    }
    Check(v.CellBias(1450, 1765, 0) == 0, "a zero bound means no variation at all");
}

void TestDifferentBotsDifferentTiles() {
    Section("route variation: two bots, same destination, different tiles");
    routing::Variation a(routing::StyleForCharacter("RevolutionMage2"));
    routing::Variation b(routing::StyleForCharacter("RevolutionFisher"));

    // Over a stretch of ground, the two must disagree somewhere -- otherwise
    // the whole layer is decoration.
    int differing = 0;
    for (i32 x = 1400; x < 1500; ++x)
        if (a.CellBias(x, 1765, 4) != b.CellBias(x, 1765, 4)) ++differing;
    Check(differing > 20, "their preferred ground genuinely differs");
}

void TestApproachSelection() {
    Section("route variation: equivalent approach tiles");
    std::vector<wm::Point> cand;
    for (i32 i = 0; i < 6; ++i) cand.push_back(wm::Point{1450 + i, 1765, 0});

    routing::Style hurried = routing::StyleForCharacter("Anyone");
    hurried.preference = routing::Preference::Shortest;
    routing::Variation quick(hurried);
    Check(quick.PickApproach(cand, 1) == 0,
          "a character in a hurry takes the nearest tile offered");

    routing::Style wanderer = hurried;
    wanderer.preference = routing::Preference::Mixed;
    routing::Variation slow(wanderer);
    const usize pick = slow.PickApproach(cand, 1);
    Check(pick < cand.size(), "everyone else picks a legal one of the offered tiles");
    Check(pick == slow.PickApproach(cand, 1), "stably");

    std::vector<wm::Point> none;
    Check(slow.PickApproach(none, 1) == 0, "an empty candidate set is handled");
}

void TestRecentPathPenalty() {
    Section("route variation: a bot varies against itself");
    routing::Style s = routing::StyleForCharacter("Wanderer");
    s.preference = routing::Preference::LowCongestion;
    routing::Variation v(s);

    Check(v.RecentPenalty(1450, 1765, 0) == 0, "an unvisited tile is free");
    v.NotePassed(1450, 1765, 0);
    Check(v.RecentPenalty(1450, 1765, 0) > 0, "one just walked is not");
    Check(v.RecentPenalty(1450, 1765, 0) >= v.RecentPenalty(1450, 1765, 300000),
          "and the penalty decays with age");
    Check(v.RecentPenalty(1450, 1765, routing::Variation::kRecentWindowMs) == 0,
          "reaching the window forgets it");

    // Bounded memory: a long walk must not grow without limit.
    for (i32 i = 0; i < 1000; ++i) v.NotePassed(1000 + i, 2000, i);
    Check(v.RecentCount() <= routing::Variation::kRecentMax,
          "recent-path memory is capped");

    v.ForgetOlderThan(10000000);
    Check(v.RecentCount() == 0, "and can be pruned wholesale");
}

void TestHurriedCharacterIgnoresHistory() {
    Section("route variation: preference actually changes behaviour");
    routing::Style s = routing::StyleForCharacter("Courier");
    s.preference = routing::Preference::Shortest;
    routing::Variation quick(s);
    quick.NotePassed(1450, 1765, 0);

    s.preference = routing::Preference::LowCongestion;
    routing::Variation fussy(s);
    fussy.NotePassed(1450, 1765, 0);

    Check(quick.TileCost(1450, 1765, 0, 4) < fussy.TileCost(1450, 1765, 0, 4),
          "the character in a hurry retraces its own steps happily");
}

// ---------------------------------------------------------------------------

void TestPriceProvenance() {
    Section("economy: historical prices cannot leak into a live decision");
    econ::PriceBook book;

    econ::Price forum;
    forum.graphic = 0x09CC;
    forum.gold = 2000;                       // a forum-era fish-net haul
    forum.source = econ::PriceSource::HistoricalForum;
    forum.note = "revolutionuo forum topic 53536";
    book.Observe(forum);

    Check(book.Best(0x09CC) == nullptr,
          "the archive alone does not give a bot a price to trade on");
    Check(book.HistoricalBaseline(0x09CC) != nullptr,
          "but it is still there when asked for by name");

    econ::Price live;
    live.graphic = 0x09CC;
    live.gold = 1;                           // measured, m3_sell2
    live.source = econ::PriceSource::VendorObserved;
    live.atMs = 1000;
    book.Observe(live);

    const econ::Price* best = book.Best(0x09CC);
    Check(best && best->gold == 1, "an observation on THIS shard is what counts");
    Check(!econ::IsLivePrice(econ::PriceSource::HistoricalForum),
          "and forum evidence is never classed as live");
}

void TestPriceRanking() {
    Section("economy: observation beats the ruleset's own VALUE");
    econ::PriceBook book;

    econ::Price def;
    def.graphic = 0x097A; def.gold = 3;      // i_fish_cut_raw VALUE=3
    def.source = econ::PriceSource::ItemdefValue;
    book.Observe(def);
    Check(book.Best(0x097A)->gold == 3, "VALUE is used when nothing better is known");

    econ::Price seen;
    seen.graphic = 0x097A; seen.gold = 2;    // what Cassiel actually paid
    seen.source = econ::PriceSource::VendorObserved;
    seen.atMs = 500;
    book.Observe(seen);
    Check(book.Best(0x097A)->gold == 2,
          "what a vendor actually paid outranks what the itemdef says");

    econ::Price later = seen;
    later.gold = 2; later.atMs = 900;
    book.Observe(later);
    Check(book.Size() == 2, "a fresher observation replaces its predecessor");
}

void TestCarvingIsKnownAndPreferred() {
    Section("economy: the M3 carving discovery is knowledge, not folklore");
    const std::vector<econ::Transformation>& all = econ::KnownTransformations();
    Check(!all.empty(), "the shard's known transformations are recorded");

    const econ::Transformation* carve = nullptr;
    for (const econ::Transformation& t : all)
        if (t.inputGraphic == 0x09CC && t.outputGraphic == 0x097A) carve = &t;
    Check(carve != nullptr, "including carving a fish");
    if (!carve) return;

    Check(carve->outputPerInput == 4, "one fish makes four steaks");
    Check(carve->skillId == -1, "with no skill check, as the server has none");
    Check(!carve->toolGraphics.empty(), "but it does need a blade");
    Check(carve->inputWeightTenths == 50 && carve->outputWeightTenths == 1,
          "and the weights are the shard's own");

    econ::PriceBook book;
    econ::Price fish;  fish.graphic = 0x09CC; fish.gold = 1;
    fish.source = econ::PriceSource::VendorObserved; book.Observe(fish);
    econ::Price steak; steak.graphic = 0x097A; steak.gold = 2;
    steak.source = econ::PriceSource::VendorObserved; book.Observe(steak);

    const econ::ProfitEstimate e = econ::EstimateTransformation(*carve, 12, book);
    Check(e.valid, "with both prices known the comparison is answerable");
    Check(e.revenue == 96, "12 fish carve into 48 steaks worth 96gp");
    Check(e.cost == 12, "against 12gp for selling them whole");
    Check(e.margin == 84, "so carving is worth 84gp more");
    Check(e.marginPerInputUnit == 7, "seven extra gold per fish");
    Check(e.marginPerStone > 0, "and it is worth more per stone carried, too");
}

void TestMissingPriceIsAdmitted() {
    Section("economy: an unknown price is reported, not invented");
    econ::PriceBook book;
    econ::Price fish; fish.graphic = 0x09CC; fish.gold = 1;
    fish.source = econ::PriceSource::VendorObserved; book.Observe(fish);

    const econ::ProfitEstimate e =
        econ::EstimateTransformation(econ::KnownTransformations()[0], 10, book);
    Check(!e.valid, "with no price for the product, there is no estimate");
    Check(e.margin == 0, "and no number a caller could mistake for one");
}

}  // namespace

int main() {
    std::printf("m3.5 authenticity: fleet / routing / economy tests\n\n");
    TestAdmissionBasics();
    TestBudgetIsUnderServerThreshold();
    TestCounterResetsOnlyAfterSilence();
    TestSilenceIsMeasuredFromLastAttempt();
    TestBanIsAbsolute();
    TestBackoffAndBreaker();
    TestNeverSpins();
    TestJitterIsBoundedAndSeeded();

    TestStyleIsStablePerCharacter();
    TestPopulationSpread();
    TestCellBiasBoundedAndDeterministic();
    TestDifferentBotsDifferentTiles();
    TestApproachSelection();
    TestRecentPathPenalty();
    TestHurriedCharacterIgnoresHistory();

    TestPriceProvenance();
    TestPriceRanking();
    TestCarvingIsKnownAndPreferred();
    TestMissingPriceIsAdmitted();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
