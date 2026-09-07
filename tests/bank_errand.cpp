// tests/bank_errand.cpp -- the "only banker in sight" defect and its fix.
//
// Evidence (fleet122c30_20260907, tools/log_slice.py): Breniel, Halar and
// Arvdris each stood at the market/bank spot next to Hyman (0x1033, the
// banker per world_query.py), scanned once (8 paperdolls requested out of
// 14-28 mobiles nearby -- Client::ActionScanMobiles's own per-call cap),
// never had Hyman's title come back, and reported "no banker in sight"
// within two seconds of that single scan:
//   Breniel.console.txt:996-1041, Halar.console.txt:406-447,
//   Arvdris.console.txt:493-516.
//
// Two things had to change in src/life/interaction/BankErrand.cpp:
//   1. Step::Find only ever scanned once per errand (gated behind a 20s
//      "is the scan stale" clock) -- fixed by looking kMaxScanRounds times,
//      close together, before believing a crowd has nobody in it.
//   2. rotation_.Skip() can filter out the only banker NearestMobileWithTrade
//      would otherwise find, which used to fall straight into "no banker in
//      sight" about a banker standing right there. DecideOnlyBankerRetry is
//      that decision, pulled out pure (no Client, no MUL data) so ctest can
//      prove the cooldown-then-retry-then-give-up shape directly, the same
//      way uo/activities/buy.h's Decide() is tested -- BankErrand's own
//      Client::NearestMobileWithTrade lookup is hard-gated on Client's LOS
//      check, which needs real MUL/navgrid world data this suite does not
//      load (see TestOccludedShopkeeperIsStillFound in trade_verify.cpp for
//      the same limitation on the sibling shopkeeper lookup).
//
// The other two scenarios (a crowded-market fast-fail, and a late box open
// not being mistaken for a failure) go through the real BankErrand + a real
// offline Client, exactly like tests/life_world_harness.cpp: no socket, no
// MULs, DispatchPacketForTest for the 0x24 box-open confirmation.

#include "Client.h"
#include "uo/interaction/bank_errand.h"
#include "uo/endian.h"
#include "uo/life.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace uo;
using namespace uo::life;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

std::unique_ptr<Client> MakeOfflineClient(i64 nowMs) {
    Client::Config cfg{};
    cfg.loginHost = "127.0.0.1";
    cfg.username = cfg.password = "bank_errand";
    cfg.version = "2.0.7";
    cfg.sendSeed = false;
    cfg.sessionTag = "bank_errand";
    auto client = std::make_unique<Client>(cfg);
    client->SetOfflineForTest(true);
    client->SetInWorldForTest();
    client->SetClockForTest(nowMs);
    return client;
}

// 0x24 DRAW_CONTAINER (Client.cpp OnDrawContainer): serial(4) gumpId(2).
// gumpId != 0xFFFF (that value means "spellbook", see ActionOnContainerOpened)
// so it is adopted as the bank box while an OpenBank action is outstanding.
std::vector<u8> MakeDrawContainer(u32 serial, u16 gumpId) {
    std::vector<u8> p(7, 0);
    p[0] = 0x24;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[5], gumpId);
    return p;
}

// ---------------------------------------------------------------------------
// 1. DecideOnlyBankerRetry: pure, no Client, no clock but the one passed in.
// ---------------------------------------------------------------------------
void TestOnlyBankerRetryPolicy() {
    Section("bank errand: the skip list must not permanently hide the only "
            "banker");

    // Nothing to decide when the skip list is empty (somebody else to try,
    // or nobody has been asked yet) -- Step::Find's ordinary path handles
    // that case.
    {
        OnlyBankerState st;
        i64 wait = -1;
        const auto d = DecideOnlyBankerRetry(/*skipNonEmpty=*/false,
                                             /*anyBankerFound=*/true, 1000,
                                             st, 5000, 2, &wait);
        Check(d == OnlyBankerDecision::NotApplicable,
              "an empty skip list is not this defect");
        Check(wait == 0, "and no wait is reported");
    }

    // Nor when there is genuinely nobody, even blind -- that is the
    // "no banker within N tiles" fast fail, not this recovery.
    {
        OnlyBankerState st;
        i64 wait = -1;
        const auto d = DecideOnlyBankerRetry(true, /*anyBankerFound=*/false,
                                             1000, st, 5000, 2, &wait);
        Check(d == OnlyBankerDecision::NotApplicable,
              "no banker at all, blind or not, is not this defect either");
    }

    // THE ACTUAL DEFECT: skip list non-empty, but a banker still exists once
    // the skip filter is lifted. First tick starts the cooldown and waits.
    OnlyBankerState st;
    i64 wait = -1;
    auto d = DecideOnlyBankerRetry(true, true, 10'000, st, 5000, 2, &wait);
    Check(d == OnlyBankerDecision::Wait,
          "the only banker on the skip list gets a rest, not a failure");
    Check(wait == 5000, "the full cooldown is reported on the first look");

    // Still resting.
    d = DecideOnlyBankerRetry(true, true, 12'000, st, 5000, 2, &wait);
    Check(d == OnlyBankerDecision::Wait, "still inside the cooldown");
    Check(wait == 3000, "the remaining rest, not the full cooldown again");

    // Cooldown elapsed: retry now, and the round counter advances.
    d = DecideOnlyBankerRetry(true, true, 15'000, st, 5000, 2, &wait);
    Check(d == OnlyBankerDecision::RetryNow,
          "cooldown over -- give the sole banker another look");
    Check(st.rounds == 1, "one retry round spent");

    // A second failure after the retry starts a fresh cooldown rather than
    // retrying immediately every tick.
    d = DecideOnlyBankerRetry(true, true, 15'100, st, 5000, 2, &wait);
    Check(d == OnlyBankerDecision::Wait,
          "a fresh cooldown starts after the retry, it does not hammer the "
          "same banker every tick");
    d = DecideOnlyBankerRetry(true, true, 25'000, st, 5000, 2, &wait);
    Check(d == OnlyBankerDecision::RetryNow, "second cooldown also elapses");
    Check(st.rounds == 2, "two retry rounds spent");

    // BOUNDED: a banker that never answers still fails the errand honestly
    // instead of being retried forever (project rule: reach failures strike
    // off after a small fixed number of approaches, never unboundedly).
    d = DecideOnlyBankerRetry(true, true, 99'999, st, 5000, 2, &wait);
    Check(d == OnlyBankerDecision::GiveUp,
          "two rounds spent -- the errand gives up rather than retrying "
          "this banker forever");
}

// ---------------------------------------------------------------------------
// 2. A crowded market: several scans before the honest "nobody here" fail,
//    with a reason that names the distance searched instead of the bare
//    "no banker in sight" that used to cover this AND the false negative.
// ---------------------------------------------------------------------------
void TestCrowdedMarketFastFailsWithDistanceReason() {
    Section("bank errand: nobody here at all fails fast, with a distance "
            "reason");

    auto client = MakeOfflineClient(1000000);

    BankErrand errand;
    errand.Begin();
    Observation obs;
    obs.nowMs = 1000000;
    obs.x = 1425;
    obs.y = 1690;

    // No mobiles registered at all (an empty pasture, like Aelia's spot) --
    // NearestMobileWithTrade returns 0 regardless of the skip list, so this
    // exercises the genuinely-nobody path, not the only-banker recovery.
    int rounds = 0;
    BankErrandResult r;
    for (; rounds < 10 && errand.Running(); ++rounds) {
        r = errand.Tick(*client, obs);
        if (r.status != ActivityStatus::Waiting) break;
    }

    Check(r.status == ActivityStatus::RetryableFailure,
          "an empty spot fails, it does not wait forever");
    Check(!errand.Running(), "and the errand stops running");
    Check(rounds >= 2 && rounds <= 6,
          "a small bounded number of looks, not one and not unbounded");
    Check(r.why.find("1425") != std::string::npos &&
              r.why.find("1690") != std::string::npos,
          "the reason names where it looked");
    Check(r.why.find("no banker in sight") == std::string::npos,
          "the bare old message -- which used to cover this AND the "
          "crowded-market false negative -- is gone");
}

// ---------------------------------------------------------------------------
// 3. A box that opens late must still be a success, not a failure, however
//    long the answer took to arrive.
// ---------------------------------------------------------------------------
void TestLateBoxOpenIsStillSuccess() {
    Section("bank errand: a bank box that opens late is still success");

    auto client = MakeOfflineClient(2000000);

    BankErrand errand;
    errand.Begin();
    errand.SetAtKnownBank(true);
    Observation obs;
    obs.nowMs = 2000000;

    const BankErrandResult issued = errand.Tick(*client, obs);
    Check(issued.status == ActivityStatus::Waiting,
          "the ask went out and the errand is waiting on it");
    Check(client->ActionBusy(), "an open_bank action is now in flight");

    // Time passes -- far more than the ask's own deadline would allow --
    // before the server answers. Nothing in this client or errand has any
    // reason to have marked the attempt failed on its own (ActionTick(), the
    // deadline sweep, is never invoked here), so this proves the box is
    // still recognised: BankErrand::Tick checks client.BankContainer() FIRST,
    // every tick, ahead of any step logic.
    client->SetClockForTest(2000000 + 45000);
    obs.nowMs = 2000000 + 45000;

    const u32 kBox = 0x40010001;
    const auto pkt = MakeDrawContainer(kBox, 0x004A);
    client->DispatchPacketForTest(pkt.data(), pkt.size());

    const BankErrandResult r = errand.Tick(*client, obs);
    Check(r.status == ActivityStatus::Success,
          "a late-arriving box is success, not a timeout failure");
    Check(r.box == kBox, "the box serial is the one that just opened");
    Check(!errand.Running(), "the errand is done");
}

}  // namespace

int main() {
    TestOnlyBankerRetryPolicy();
    TestCrowdedMarketFastFailsWithDistanceReason();
    TestLateBoxOpenIsStillSuccess();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
